#include "Plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>

#include "Editor/PluginEditor.h"
#include "Plugin/Parameters.h"

namespace stencil::plugin
{
StencilProcessor::StencilProcessor()
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "STENCIL", makeParameterLayout())
{
    apvts.addParameterListener(pid::rangeLo, this);
    apvts.addParameterListener(pid::rangeHi, this);

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
    apvts.removeParameterListener(pid::rangeLo, this);
    apvts.removeParameterListener(pid::rangeHi, this);
}

void StencilProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate;
    pendingNoteOffs_.clear();
    wasPlaying_ = false;
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
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
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

void StencilProcessor::emitPanic(juce::MidiBuffer& midi, int sampleOffset)
{
    // Drain any pending noteOffs first so a hung note from a long gate
    // gets its noteOff before the all-notes-off CC.
    for (const auto& p : pendingNoteOffs_)
        midi.addEvent(juce::MidiMessage::noteOff(p.channel, p.note), sampleOffset);
    pendingNoteOffs_.clear();

    // CC 123 (All Notes Off) on every channel.
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

    // Transport edge: playing → stopped. Emit panic at offset 0.
    if (wasPlaying_ && !isPlaying)
    {
        emitPanic(outMidi, 0);
        sequencer_.onTransportStop();
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
            const engine::StepOutput o = sequencer_.processStep();

            // Publish editor snapshots immediately after each step so the
            // ring view, history strip, and center "fraction / note" text
            // see the freshest state. ADR 007 §Threading: relaxed atomic
            // store; eventually-consistent for the editor.
            registerSnapshot_.store(sequencer_.getRegister(), std::memory_order_relaxed);
            cumulativeStepsSnapshot_.fetch_add(1, std::memory_order_relaxed);
            lastNoteSnapshot_.store(o.note, std::memory_order_relaxed);
            lastActiveSnapshot_.store(o.active, std::memory_order_relaxed);

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

}  // namespace stencil::plugin

// JUCE plugin client looks up createPluginFilter() at global scope; keep
// it outside the stencil::plugin namespace so the symbol name matches.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new stencil::plugin::StencilProcessor();
}
