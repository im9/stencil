#include "Engine/Sequencer.h"

#include <algorithm>
#include <cmath>

namespace stencil::engine
{
double subdivisionsPerQuarter(Subdivision s)
{
    switch (s)
    {
        case Subdivision::Eighth:           return 2.0;
        case Subdivision::Sixteenth:        return 4.0;
        case Subdivision::ThirtySecond:     return 8.0;
        case Subdivision::EighthTriplet:    return 3.0;
        case Subdivision::SixteenthTriplet: return 6.0;
    }
    return 4.0;  // unreachable; default to 16th
}

// ─── detectBoundaries ────────────────────────────────────────────────────

std::vector<StepBoundary> detectBoundaries(double startPpq,
                                            double bpm,
                                            double sampleRate,
                                            int blockSamples,
                                            Subdivision subdivision)
{
    std::vector<StepBoundary> result;
    if (blockSamples <= 0 || bpm <= 0.0 || sampleRate <= 0.0)
        return result;

    const double stepPpq = 1.0 / subdivisionsPerQuarter(subdivision);
    const double samplesPerPpq = (60.0 / bpm) * sampleRate;
    const double endPpq = startPpq + static_cast<double>(blockSamples) / samplesPerPpq;

    // First step index N where N × stepPpq ≥ startPpq. Use a tiny epsilon
    // to keep boundaries that fall exactly on startPpq from drifting up.
    constexpr double kEps = 1e-9;
    int N = static_cast<int>(std::floor(startPpq / stepPpq + kEps));
    if (N * stepPpq + kEps < startPpq)
        ++N;

    while (true)
    {
        const double stepPos = N * stepPpq;
        if (stepPos >= endPpq - kEps)
            break;
        const double offsetSamples = (stepPos - startPpq) * samplesPerPpq;
        int sampleOffset = static_cast<int>(std::floor(offsetSamples + 0.5));
        if (sampleOffset >= blockSamples)
            break;
        if (sampleOffset < 0)
            sampleOffset = 0;
        result.push_back(StepBoundary{ sampleOffset, N });
        ++N;
    }
    return result;
}

// ─── Sequencer ───────────────────────────────────────────────────────────

Sequencer::Sequencer() : Sequencer(SequencerParams{}) {}

Sequencer::Sequencer(const SequencerParams& p)
    : params_(p), register_(0), lastEmittedRegister_(0),
      rng_{ { 0, 0, 0, 0 } }, position_(0), seedActivated_(false)
{
    recreateRegister();
}

void Sequencer::setParams(const SequencerParams& p)
{
    const bool seedChanged = (p.seed != params_.seed);
    const bool lengthChanged = (p.length != params_.length);
    const bool triggerModeChanged = (p.triggerMode != params_.triggerMode);
    params_ = p;
    if (seedChanged || lengthChanged)
        recreateRegister();
    if (triggerModeChanged)
    {
        heldInputs_.clear();
        seedActivated_ = false;
    }
}

void Sequencer::reset()
{
    recreateRegister();
    position_ = 0;
    heldInputs_.clear();
    seedActivated_ = false;
}

void Sequencer::recreateRegister()
{
    rng_ = seedRng(static_cast<uint64_t>(params_.seed));
    register_ = createRegister(params_.length, rng_);
    // Editor reads lastEmittedRegister_ as "register state at moment of
    // last emission." Before any step runs, bit 0 of the initial register
    // is the first to emit, so the initial value is also a valid "just
    // about to emit" view; no special idle state needed.
    lastEmittedRegister_ = register_;
}

bool Sequencer::channelMatches(int channel) const
{
    return params_.inputChannel == 0 || channel == params_.inputChannel;
}

void Sequencer::onInputNoteOn(int pitch, int channel)
{
    if (!channelMatches(channel))
        return;
    if (params_.triggerMode == TriggerMode::Gate)
    {
        // Add to held set if not already there.
        for (const auto& h : heldInputs_)
            if (h.first == pitch && h.second == channel)
                return;
        heldInputs_.emplace_back(pitch, channel);
    }
    else if (params_.triggerMode == TriggerMode::Seed)
    {
        register_ = shiftAndForce(register_, params_.length, 1);
        seedActivated_ = true;
    }
    // Auto: input ignored.
}

void Sequencer::onInputNoteOff(int pitch, int channel)
{
    if (!channelMatches(channel))
        return;
    if (params_.triggerMode == TriggerMode::Gate)
    {
        for (auto it = heldInputs_.begin(); it != heldInputs_.end(); ++it)
        {
            if (it->first == pitch && it->second == channel)
            {
                heldInputs_.erase(it);
                return;
            }
        }
    }
    else if (params_.triggerMode == TriggerMode::Seed)
    {
        register_ = shiftAndForce(register_, params_.length, 0);
        seedActivated_ = true;
    }
}

StepOutput Sequencer::processStep()
{
    StepOutput out{ false, 0, 0, params_.outputChannel };

    // Gate mode + no input held: silent, register and rng frozen.
    if (params_.triggerMode == TriggerMode::Gate && heldInputs_.empty())
        return out;

    // Read register for output (read-then-shift per ADR 001).
    const Fraction f = registerToFraction(register_, params_.length);
    const double frac = f.den > 0 ? static_cast<double>(f.num) / static_cast<double>(f.den) : 0.0;

    // Mode dispatch.
    int note;
    int velocity;
    if (params_.mode == Mode::Gate)
    {
        note = (params_.rangeLo + params_.rangeHi) / 2;
        velocity = params_.outputVelocity;
    }
    else if (params_.mode == Mode::Velocity)
    {
        note = mapToNote(f.num, f.den, params_.rangeLo, params_.rangeHi);
        const double vNorm = 0.3 + frac * 0.7;
        int vRaw = static_cast<int>(std::floor(vNorm * params_.outputVelocity));
        if (vRaw < 1) vRaw = 1;
        if (vRaw > 127) vRaw = 127;
        velocity = vRaw;
    }
    else  // Mode::Note
    {
        note = mapToNote(f.num, f.den, params_.rangeLo, params_.rangeHi);
        velocity = params_.outputVelocity;
    }

    // Bit-tap active = LSB-fires OR density-draw-fires. Density draw is
    // ALWAYS consumed so the rng thread stays in lockstep across the
    // mode/density combinations.
    const bool bit0 = (register_ & 1u) == 1u;
    const uint32_t dDraw = nextU32(rng_);
    const uint64_t dThresh = probabilityThreshold(params_.density);
    const bool fillFire = dDraw < dThresh;
    const bool active = bit0 || fillFire;

    // Capture pre-shift register so the editor renders "bit just emitted
    // under the playhead at moment of sounding" (ADR 007 §Visual playhead
    // alignment). Even in seed-active mode (no shift below) this captures
    // the register state used for this step's bit-tap.
    lastEmittedRegister_ = register_;

    // Register advancement: skip in seed-active state (input drives the
    // register; transport step is read-only in that mode).
    const bool isSeedActive =
        (params_.triggerMode == TriggerMode::Seed && seedActivated_);
    if (!isSeedActive)
        register_ = shiftAndFlip(register_, params_.length, params_.lock, rng_);

    ++position_;

    if (active)
    {
        out.active = true;
        out.note = note;
        out.velocity = velocity;
    }
    return out;
}

void Sequencer::onTransportStop()
{
    heldInputs_.clear();
    seedActivated_ = false;
    position_ = 0;
    // Register intentionally preserved (resume-the-loop).
}

}  // namespace stencil::engine
