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
    // ADR 007 §Output (2026-05-15): single dispatch — pitch is reg-derived
    // via mapToNote, velocity is the slider value verbatim. The earlier
    // per-mode branch (note/gate/velocity) is removed; gate's "midpoint
    // pitch" is recoverable by setting rangeLo == rangeHi, and velocity
    // mode's reg-derived scaling is dropped without replacement.
    const Fraction f = registerToFraction(register_, params_.length);
    const int note = mapToNote(f.num, f.den, params_.rangeLo, params_.rangeHi);
    const int velocity = params_.outputVelocity;

    // Active = LSB-1 AND density-gate-passes. Density is the probability
    // that an on-bit fires; off-bits are always silent (visual contract:
    // white circle = no sound). The earlier `||` semantic (density fills
    // empties) violated that contract — see ADR 007 §Output 2026-05-16
    // revision. Density draw is always consumed so the rng thread stays
    // in lockstep with cross-target vector tests regardless of LSB.
    const bool bit0 = (register_ & 1u) == 1u;
    const uint32_t dDraw = nextU32(rng_);
    const uint64_t dThresh = probabilityThreshold(params_.density);
    const bool densityPass = dDraw < dThresh;
    const bool active = bit0 && densityPass;

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
