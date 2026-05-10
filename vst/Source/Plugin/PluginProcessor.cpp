#include "Plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "Editor/PluginEditor.h"
#include "Plugin/Parameters.h"

namespace stencil::plugin
{
namespace
{
// Parameters that must trigger a hung-note flush on change, per ADR 007
// §Note-off discipline. Range listeners also drive the lo ≤ hi clamp;
// the others are listed solely for the flush.
const char* const kFlushOnChangeParamIds[] = {
    pid::rangeLo, pid::rangeHi, pid::length, pid::seed,
    pid::subdivision, pid::mode, pid::outputChannel
};
}  // namespace

StencilProcessor::StencilProcessor()
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "STENCIL", makeParameterLayout())
{
    for (const char* id : kFlushOnChangeParamIds)
        apvts.addParameterListener(id, this);

    // Sync sequencer with initial APVTS values (covers the case where the
    // host load happened with non-default values via setStateInformation
    // before processBlock fires).
    const engine::SequencerParams initial = readParams(apvts);
    sequencer_.setParams(initial);
    lastSeed_ = static_cast<int>(initial.seed);
    lastLength_ = initial.length;

    // Seed the editor snapshot with the freshly-created register so the
    // ring view shows real bits at first paint (before processBlock has
    // had a chance to publish). Otherwise the editor opens to an all-
    // empty ring even though the engine already has a non-zero register.
    registerSnapshot_.store(sequencer_.getRegister(), std::memory_order_relaxed);
}

StencilProcessor::~StencilProcessor()
{
    for (const char* id : kFlushOnChangeParamIds)
        apvts.removeParameterListener(id, this);
}

void StencilProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    pendingNoteOffs_.clear();
    wasPlaying_ = false;
    mutatedBitSnapshot_.store(-1, std::memory_order_relaxed);
}

void StencilProcessor::releaseResources()
{
    pendingNoteOffs_.clear();
}

juce::AudioProcessorEditor* StencilProcessor::createEditor()
{
    return new editor::StencilEditor(*this);
}

void StencilProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void StencilProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            // ADR 007 §Note-off discipline: state load must clear sounding
            // notes. replaceState fires per-parameter listeners which
            // already raise the flush flag, but request explicitly so
            // identical-state restores (no listener fires) still flush.
            flushPendingRequested_.store(true, std::memory_order_release);
            // Clear the salmon highlight: the loaded state has no recent
            // mutation step to point at.
            mutatedBitSnapshot_.store(-1, std::memory_order_relaxed);
        }
    }
}

void StencilProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Range invariant clamp. The side that just moved past the other gets
    // clamped: if rangeLo > rangeHi, push rangeHi up to match rangeLo;
    // if rangeHi < rangeLo, push rangeLo down to match rangeHi. This
    // differs from m4l's "snap rangeLo down" — APVTS UX expects the
    // *other* side to follow, not the side the user just moved.
    if (parameterID == pid::rangeLo)
    {
        const int lo = static_cast<int>(newValue);
        const int hi = static_cast<int>(*apvts.getRawParameterValue(pid::rangeHi));
        if (lo > hi)
        {
            if (auto* hiParam = apvts.getParameter(pid::rangeHi))
                hiParam->setValueNotifyingHost(hiParam->convertTo0to1(static_cast<float>(lo)));
        }
    }
    else if (parameterID == pid::rangeHi)
    {
        const int hi = static_cast<int>(newValue);
        const int lo = static_cast<int>(*apvts.getRawParameterValue(pid::rangeLo));
        if (hi < lo)
        {
            if (auto* loParam = apvts.getParameter(pid::rangeLo))
                loParam->setValueNotifyingHost(loParam->convertTo0to1(static_cast<float>(hi)));
        }
    }

    // Hung-note flush per ADR 007 §Note-off discipline. Every parameter
    // wired into kFlushOnChangeParamIds reaches this method, so an
    // unconditional flag raise is correct. Drain-only (no CC 123);
    // matches m4l's host.ts setParam flushNotesOn shape.
    flushPendingRequested_.store(true, std::memory_order_release);
}

int StencilProcessor::stepDurationSamples(double bpm, engine::Subdivision subdivision) const
{
    if (bpm <= 0.0 || sampleRate_ <= 0.0)
        return 0;
    const double samplesPerQuarter = (60.0 / bpm) * sampleRate_;
    const double samplesPerStep = samplesPerQuarter / engine::subdivisionsPerQuarter(subdivision);
    return static_cast<int>(std::floor(samplesPerStep + 0.5));
}

void StencilProcessor::drainPendingNoteOffs(juce::MidiBuffer& midi, int blockSamples)
{
    auto it = pendingNoteOffs_.begin();
    while (it != pendingNoteOffs_.end())
    {
        if (it->samplesUntilFire < blockSamples)
        {
            const int offset = std::max(0, it->samplesUntilFire);
            midi.addEvent(juce::MidiMessage::noteOff(it->channel, it->note), offset);
            it = pendingNoteOffs_.erase(it);
        }
        else
        {
            it->samplesUntilFire -= blockSamples;
            ++it;
        }
    }
}

void StencilProcessor::flushPending(juce::MidiBuffer& midi, int sampleOffset)
{
    // Surgical drain: noteOff for every scheduled-but-not-yet-fired note,
    // then clear the list. Used by parameter-change / state-load /
    // bypass-not-yet flushes where CC 123 would be heavy-handed (m4l's
    // setParam flushNotesOn doesn't send CC 123 either).
    for (const auto& p : pendingNoteOffs_)
        midi.addEvent(juce::MidiMessage::noteOff(p.channel, p.note), sampleOffset);
    pendingNoteOffs_.clear();
}

void StencilProcessor::emitPanic(juce::MidiBuffer& midi, int sampleOffset)
{
    // Drain pending first so a hung note from a long gate gets its
    // noteOff before the all-notes-off CC sweeps the channel.
    flushPending(midi, sampleOffset);

    // CC 123 (All Notes Off) on every channel — concept.md §Note-off
    // discipline: panic is required behavior, not optional.
    for (int ch = 1; ch <= 16; ++ch)
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), sampleOffset);
}

void StencilProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Stencil produces no audio; clear the buffer to avoid passing junk.
    audio.clear();

    const int blockSamples = audio.getNumSamples();
    if (blockSamples <= 0)
    {
        midi.clear();  // safety
        return;
    }

    // Sync sequencer with current APVTS values. If seed or length changed,
    // re-create the register from the new (seed, length).
    engine::SequencerParams params = readParams(apvts);
    const bool seedOrLengthChanged =
        (static_cast<int>(params.seed) != lastSeed_) || (params.length != lastLength_);
    sequencer_.setParams(params);
    if (seedOrLengthChanged)
    {
        sequencer_.reset();
        lastSeed_ = static_cast<int>(params.seed);
        lastLength_ = params.length;
        // Reset cumulative step counter so the ring rotation realigns with
        // the new (seed, length) pair. Otherwise ROLL would advance the
        // visual rotation past the loop boundary, making the head pointer
        // sit on a meaningless bit position.
        cumulativeStepsSnapshot_.store(0, std::memory_order_relaxed);
        registerSnapshot_.store(sequencer_.getRegister(), std::memory_order_relaxed);
        mutatedBitSnapshot_.store(-1, std::memory_order_relaxed);
    }

    // Read transport state.
    double startPpq = 0.0;
    double bpm = 120.0;
    bool isPlaying = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto p = pos->getPpqPosition())
                startPpq = *p;
            if (auto b = pos->getBpm())
                bpm = *b;
            isPlaying = pos->getIsPlaying();
        }
    }

    juce::MidiBuffer outMidi;

    // Bypass edge: bypass → active. processBlockBypassed already drained
    // pending and emitted panic on the way in; just clear the flag here
    // so the next bypass entry triggers another panic.
    if (wasBypassed_)
        wasBypassed_ = false;

    // Hung-note flush requested from the message thread (parameter change
    // listener, setStateInformation). Consumed at offset 0. Drain-only —
    // CC 123 is reserved for transport / bypass panic paths.
    if (flushPendingRequested_.exchange(false, std::memory_order_acq_rel))
        flushPending(outMidi, 0);

    // Transport edge: playing → stopped. Emit panic at offset 0.
    if (wasPlaying_ && !isPlaying)
    {
        emitPanic(outMidi, 0);
        sequencer_.onTransportStop();
    }

    // Transport edge: stopped → playing. Re-derive the register from
    // (seed, length) so each transport start replays the same loop —
    // matches m4l's transportStart() and concept.md §Transport's
    // "Seeded determinism is a core contract." Without this, vst's
    // register would persist across stop/start while m4l's resets,
    // breaking ADR 007 §Cross-target audible parity after the first
    // stop/start cycle. emitPanic on the prior stop edge already
    // flushed any sounding notes.
    if (!wasPlaying_ && isPlaying)
    {
        sequencer_.reset();
        cumulativeStepsSnapshot_.store(0, std::memory_order_relaxed);
        registerSnapshot_.store(sequencer_.getRegister(), std::memory_order_relaxed);
        mutatedBitSnapshot_.store(-1, std::memory_order_relaxed);
    }

    // Drain any noteOffs scheduled to fire in this block.
    drainPendingNoteOffs(outMidi, blockSamples);

    // Route input MIDI: filter inputChannel before forwarding to sequencer.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            sequencer_.onInputNoteOn(msg.getNoteNumber(), msg.getChannel());
        else if (msg.isNoteOff())
            sequencer_.onInputNoteOff(msg.getNoteNumber(), msg.getChannel());
        // Other MIDI messages (CC, pitch bend, etc.) are dropped — Stencil
        // is a generator, not a passthrough.
    }

    // Detect subdivision boundaries and process each step.
    if (isPlaying)
    {
        const auto boundaries = engine::detectBoundaries(
            startPpq, bpm, sampleRate_, blockSamples, params.subdivision);

        const int stepDur = stepDurationSamples(bpm, params.subdivision);

        for (const auto& b : boundaries)
        {
            // Capture register before stepping so we can detect whether
            // shiftAndFlip flipped the bit (new MSB ≠ consumed LSB).
            const auto preStepReg = sequencer_.getRegister();

            const engine::StepOutput o = sequencer_.processStep();

            // Publish editor snapshots immediately after each step so the
            // ring view, history strip, and center "fraction / note" text
            // see the freshest state. ADR 007 §Threading: relaxed atomic
            // store; eventually-consistent for the editor.
            const auto postStepReg = sequencer_.getRegister();
            registerSnapshot_.store(postStepReg, std::memory_order_relaxed);
            cumulativeStepsSnapshot_.fetch_add(1, std::memory_order_relaxed);
            lastNoteSnapshot_.store(o.note, std::memory_order_relaxed);
            lastActiveSnapshot_.store(o.active, std::memory_order_relaxed);

            // Mutated bit: shiftAndFlip writes the (possibly flipped) old
            // LSB into the new MSB at (length-1). If the LSB was flipped,
            // the new MSB differs — that bit is the visualization target
            // (parity with inboil TuringSheet's salmon highlight).
            // Equal pre/post register means the seed-active branch took
            // (no shiftAndFlip), or shift-and-no-flip happened to land an
            // identical register; both render as "no mutation."
            const int length = params.length;
            const auto oldBit0 = preStepReg & 1u;
            const auto newMsb = length > 0
                ? (postStepReg >> (length - 1)) & 1u
                : 0u;
            const int mutated = (preStepReg != postStepReg && oldBit0 != newMsb)
                ? length - 1
                : -1;
            mutatedBitSnapshot_.store(mutated, std::memory_order_relaxed);

            if (!o.active)
                continue;

            outMidi.addEvent(
                juce::MidiMessage::noteOn(o.channel, o.note,
                                          static_cast<juce::uint8>(o.velocity)),
                b.sampleOffset);

            const int gateSamples =
                static_cast<int>(std::floor(params.outputGate * stepDur + 0.5));
            const int noteOffOffset = b.sampleOffset + gateSamples;
            if (noteOffOffset < blockSamples)
            {
                outMidi.addEvent(juce::MidiMessage::noteOff(o.channel, o.note),
                                 noteOffOffset);
            }
            else
            {
                pendingNoteOffs_.push_back(
                    PendingNoteOff{ noteOffOffset - blockSamples, o.note, o.channel });
            }
        }
    }

    midi.swapWith(outMidi);
    wasPlaying_ = isPlaying;
}

void StencilProcessor::processBlockBypassed(juce::AudioBuffer<float>& audio,
                                             juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    juce::MidiBuffer outMidi;
    if (!wasBypassed_)
    {
        // Active → bypass edge: emit panic so any sounding note from the
        // pre-bypass state stops cleanly. ADR 007 §Note-off discipline.
        // Subsequent bypassed blocks emit nothing — Stencil is silent
        // while bypassed (no input passthrough; we are a generator, not
        // a passthrough effect).
        emitPanic(outMidi, 0);
        wasBypassed_ = true;
    }
    midi.swapWith(outMidi);
}

}  // namespace stencil::plugin

// JUCE plugin client looks up createPluginFilter() at global scope; keep
// it outside the stencil::plugin namespace so the symbol name matches.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new stencil::plugin::StencilProcessor();
}
