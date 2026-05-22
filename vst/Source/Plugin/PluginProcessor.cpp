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
    pid::subdivision, pid::outputChannel
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
    // lastEmittedRegister == register here (Sequencer ctor initializes
    // it to the freshly-created register).
    publishSnapshot(sequencer_.getLastEmittedRegister(),
                    /*steps*/ 0, /*note*/ -1, /*active*/ false, /*mutated*/ -1);
}

void StencilProcessor::publishSnapshot(engine::RegisterBits reg, int steps,
                                       int note, bool active, int mutated)
{
    // Seqlock writer half: bump version to odd to flag in-flight write,
    // then store fields under relaxed (the version's release ordering
    // commits the field stores), then bump to even to mark complete.
    const auto v = snapshotVersion_.load(std::memory_order_relaxed);
    snapshotVersion_.store(v + 1, std::memory_order_release);
    snapshotReg_.store(reg, std::memory_order_relaxed);
    snapshotSteps_.store(steps, std::memory_order_relaxed);
    snapshotNote_.store(note, std::memory_order_relaxed);
    snapshotActive_.store(active, std::memory_order_relaxed);
    snapshotMutated_.store(mutated, std::memory_order_relaxed);
    snapshotVersion_.store(v + 2, std::memory_order_release);
}

StencilProcessor::EditorSnapshot StencilProcessor::readEditorSnapshot() const
{
    // Seqlock reader: spin while an in-flight write is observed (odd
    // version) or the version changed during the field reads. Writer
    // is wait-free and finishes in ~6 atomic ops, so 1–2 iterations is
    // the steady-state cost under contention.
    EditorSnapshot s;
    while (true)
    {
        const auto v1 = snapshotVersion_.load(std::memory_order_acquire);
        if ((v1 & 1u) == 0)
        {
            s.reg     = snapshotReg_.load(std::memory_order_relaxed);
            s.steps   = snapshotSteps_.load(std::memory_order_relaxed);
            s.note    = snapshotNote_.load(std::memory_order_relaxed);
            s.active  = snapshotActive_.load(std::memory_order_relaxed);
            s.mutated = snapshotMutated_.load(std::memory_order_relaxed);
            const auto v2 = snapshotVersion_.load(std::memory_order_acquire);
            if (v1 == v2) return s;
        }
    }
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
    // ADR 007 §Audit follow-ups — pre-reserve audio-thread buffers so the
    // typical processBlock path does no allocation. Sizes are generous-
    // but-bounded: 64 in-flight noteOffs covers any realistic sustained-
    // gate scenario at sub-32nd subdivisions, and 16 boundaries per block
    // covers the worst case (32nd @ high bpm with large blockSamples).
    pendingNoteOffs_.reserve(64);
    boundariesBuffer_.reserve(16);
    wasPlaying_ = false;
    snapshotMutated_.store(-1, std::memory_order_relaxed);
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
            // mutation step to point at. Single-field write — leaves the
            // other snapshot fields untouched (they're refreshed on the
            // next processBlock step). seqlock invariant tolerates this
            // because the message-thread store happens before audio
            // resumes; readers cannot see a torn tuple in practice.
            snapshotMutated_.store(-1, std::memory_order_relaxed);
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

    // Panic = no notes are sounding; clear the ring's center-label note
    // so a stale name doesn't linger after CC 123 (matches m4l bridge
    // panic()'s emitOutlet("currentNote", -1)).
    snapshotNote_.store(-1, std::memory_order_relaxed);
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
        // Reset cumulative step counter so the history strip realigns
        // with the new (seed, length) pair. Otherwise ROLL would
        // advance the visual position past the loop boundary.
        // ROLL / seed-or-length change: clear the center label (note=-1)
        // so the loop-just-rerolled state doesn't keep showing the last
        // played note. Matches m4l bridge's panic() / lifecycle clear.
        publishSnapshot(sequencer_.getLastEmittedRegister(),
                        /*steps*/ 0, /*note*/ -1,
                        /*active*/ false, /*mutated*/ -1);
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

    // Transport edge: playing → stopped. Emit panic at offset 0 (also
    // clears the center label via emitPanic's snapshotNote_ store).
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
        // ADR 007 §Audit follow-ups — seed-mode register preservation.
        // Skip the re-derive when the user has written a pattern via
        // input notes (triggerMode == Seed && seedActivated_); the
        // seeded-determinism contract still governs auto / gate.
        const bool preserveUserPattern =
            params.triggerMode == engine::TriggerMode::Seed
            && sequencer_.isSeedActivated();
        if (!preserveUserPattern)
            sequencer_.reset();
        // Center label starts fresh: -1 = no audible note. The label
        // updates on the first active step (matches m4l bridge's
        // transportStart() + dispatch() lifecycle).
        publishSnapshot(sequencer_.getLastEmittedRegister(),
                        /*steps*/ 0, /*note*/ -1,
                        /*active*/ false, /*mutated*/ -1);
    }

    // Drain any noteOffs scheduled to fire in this block.
    drainPendingNoteOffs(outMidi, blockSamples);

    // ADR 007 §Audit follow-ups — sample-accurate input MIDI. When playing,
    // input events are interleaved with subdivision boundaries below so a
    // gate / seed event lands in Sequencer state at its actual
    // samplePosition rather than being block-quantized to offset 0. When
    // NOT playing there are no boundaries to interleave against, so a bulk
    // drain at block-start is the only correct behavior (seed-mode pattern
    // entry while transport is stopped).
    auto routeInputMessage = [this](const juce::MidiMessage& msg)
    {
        if (msg.isNoteOn())
            sequencer_.onInputNoteOn(msg.getNoteNumber(), msg.getChannel());
        else if (msg.isNoteOff())
            sequencer_.onInputNoteOff(msg.getNoteNumber(), msg.getChannel());
        // Other MIDI messages (CC, pitch bend, etc.) are dropped — Stencil
        // is a generator, not a passthrough.
    };

    if (!isPlaying)
    {
        for (const auto meta : midi)
            routeInputMessage(meta.getMessage());
    }

    // Detect subdivision boundaries and process each step.
    if (isPlaying)
    {
        engine::detectBoundaries(boundariesBuffer_, startPpq, bpm,
                                 sampleRate_, blockSamples, params.subdivision);

        const int stepDur = stepDurationSamples(bpm, params.subdivision);

        // Interleave cursor over the input MIDI buffer. Input events with
        // samplePosition ≤ current boundary's sampleOffset are drained
        // BEFORE the boundary's processStep so gate / seed state at the
        // boundary reflects events that already arrived. Tie-breaks at
        // equal sampleOffset open the gate for that step (input event is
        // logically "at" the boundary, not after it).
        auto midiIt = midi.cbegin();
        const auto midiEnd = midi.cend();

        for (const auto& b : boundariesBuffer_)
        {
            while (midiIt != midiEnd && (*midiIt).samplePosition <= b.sampleOffset)
            {
                routeInputMessage((*midiIt).getMessage());
                ++midiIt;
            }

            // Gate-mode silence: in triggerMode = gate with the held-set
            // empty, Sequencer::processStep is a no-op (register / rng
            // frozen per ADR 007 §MIDI processing). Mirror that freeze
            // on the editor side — skip the snapshot publication so the
            // ring doesn't keep advancing through bits that never sound.
            if (params.triggerMode == engine::TriggerMode::Gate
                && sequencer_.heldInputCount() == 0)
                continue;

            // Capture register before stepping so we can detect whether
            // shiftAndFlip flipped the bit (new MSB ≠ consumed LSB).
            const auto preStepReg = sequencer_.getRegister();

            const engine::StepOutput o = sequencer_.processStep();

            // Publish editor snapshot immediately after each step so the
            // ring view, history strip, and center "fraction / note" text
            // see the freshest state. ADR 007 §Threading + §Audit follow-
            // ups (tuple coherence): all five fields are bracketed under
            // snapshotVersion_ so the editor's readEditorSnapshot sees
            // a tuple captured at this boundary, never a torn mix.
            //
            // Pre-shift register (Sequencer::getLastEmittedRegister) is
            // what the editor renders so bit 0 == "bit just emitted" sits
            // under the playhead triangle at the moment of sounding.
            // Post-shift register is only used here to detect the
            // shiftAndFlip mutation for the salmon highlight.
            const auto postStepReg = sequencer_.getRegister();

            // Mutated bit: shiftAndFlip writes the (possibly flipped) old
            // LSB into the new MSB. With the pre-shift snapshot, that
            // flipped bit is visible at bit 0 of the displayed register
            // (the just-emitted bit), so the editor highlights index 0
            // salmon when a flip happened. preStepReg == postStepReg
            // means the seed-active branch took (no shiftAndFlip), so no
            // mutation either way.
            const int length = params.length;
            const auto oldBit0 = preStepReg & 1u;
            const auto newMsb = length > 0
                ? (postStepReg >> (length - 1)) & 1u
                : 0u;
            const int mutated = (preStepReg != postStepReg && oldBit0 != newMsb)
                ? 0
                : -1;
            const int newSteps = snapshotSteps_.load(std::memory_order_relaxed) + 1;
            // Center label follows m4l's currentNote lifecycle: update on
            // an audible step, preserve across silent steps (so the
            // last-played name stays visible during density-fail or LSB=0
            // gaps). Audio thread is the sole snapshotNote_ writer, so
            // loading our own previous value is well-defined.
            const int snapNote = o.active
                ? o.note
                : snapshotNote_.load(std::memory_order_relaxed);
            publishSnapshot(sequencer_.getLastEmittedRegister(),
                            newSteps, snapNote, o.active, mutated);

            if (!o.active)
                continue;

            outMidi.addEvent(
                juce::MidiMessage::noteOn(o.channel, o.note,
                                          static_cast<juce::uint8>(o.velocity)),
                b.sampleOffset);

            // ADR 007 §Audit follow-ups — outputGate=0 must not collapse
            // noteOff onto noteOn (synths render that as a click or
            // silence). Floor the scheduled distance at 1 sample so the
            // emission is always a well-formed note event with nonzero
            // duration. "outputGate=0 = silent step" was never an
            // advertised feature; if the user wants mute, they have
            // bypass and density.
            const int gateSamples = std::max(1,
                static_cast<int>(std::floor(params.outputGate * stepDur + 0.5)));
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

        // Drain input events past the final boundary so held / seed state
        // is correct for the NEXT block. Without this, a noteOn / noteOff
        // landing after the last boundary would never reach Sequencer.
        while (midiIt != midiEnd)
        {
            routeInputMessage((*midiIt).getMessage());
            ++midiIt;
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
