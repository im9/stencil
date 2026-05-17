// Plugin tests (ADR 007 Phase 2): APVTS layout / defaults / ranges,
// state save-restore round-trip, range invariant clamp, processBlock
// dispatch with a mock AudioPlayHead.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <vector>

#include "Engine/Sequencer.h"
#include "Plugin/Parameters.h"
#include "Plugin/PluginProcessor.h"

using stencil::engine::SequencerParams;
using stencil::engine::Subdivision;
using stencil::engine::TriggerMode;
using stencil::plugin::makeParameterLayout;
using stencil::plugin::readParams;
using stencil::plugin::StencilProcessor;
namespace pid = stencil::plugin::pid;
namespace defaults = stencil::plugin::defaults;

namespace
{
// Pumping the message queue requires JUCE_MODAL_LOOPS_PERMITTED, which is
// off in plugin builds by default. The APVTS::Listener-based clamp
// therefore can't be exercised synchronously from this test binary; its
// UX behaviour is verified manually in real hosts (ADR 007 §Verification).
// The engine-side invariant — that processBlock always sees a valid
// rangeLo ≤ rangeHi — is covered by readParams() and tested directly.

class MockPlayHead : public juce::AudioPlayHead
{
public:
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        return position;
    }
    juce::AudioPlayHead::PositionInfo position;
};
}

// ─── APVTS layout ─────────────────────────────────────────────────────────

TEST_CASE("APVTS layout has all 13 expected parameters", "[plugin][apvts]")
{
    StencilProcessor proc;
    const auto& apvts = proc.getApvts();

    REQUIRE(apvts.getParameter(pid::length)         != nullptr);
    REQUIRE(apvts.getParameter(pid::lock)           != nullptr);
    REQUIRE(apvts.getParameter(pid::density)        != nullptr);
    REQUIRE(apvts.getParameter(pid::rangeLo)        != nullptr);
    REQUIRE(apvts.getParameter(pid::rangeHi)        != nullptr);
    REQUIRE(apvts.getParameter(pid::subdivision)    != nullptr);
    REQUIRE(apvts.getParameter(pid::seed)           != nullptr);
    REQUIRE(apvts.getParameter(pid::triggerMode)    != nullptr);
    REQUIRE(apvts.getParameter(pid::inputChannel)   != nullptr);
    REQUIRE(apvts.getParameter(pid::outputVelocity) != nullptr);
    REQUIRE(apvts.getParameter(pid::outputGate)     != nullptr);
    REQUIRE(apvts.getParameter(pid::outputChannel)  != nullptr);
}

TEST_CASE("APVTS defaults match concept.md", "[plugin][apvts][defaults]")
{
    StencilProcessor proc;
    const auto& apvts = proc.getApvts();

    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::length))         == defaults::length);
    CHECK(*apvts.getRawParameterValue(pid::lock)                              == defaults::lock);
    CHECK(*apvts.getRawParameterValue(pid::density)                           == defaults::density);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeLo))        == defaults::rangeLo);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeHi))        == defaults::rangeHi);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::subdivision))    == defaults::subdivisionIdx);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::seed))           == defaults::seed);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::triggerMode))    == defaults::triggerModeIdx);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::inputChannel))   == defaults::inputChannel);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::outputVelocity)) == defaults::outputVelocity);
    CHECK(*apvts.getRawParameterValue(pid::outputGate)                        == defaults::outputGate);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::outputChannel))  == defaults::outputChannel);
}

TEST_CASE("APVTS int param ranges match concept.md", "[plugin][apvts][ranges]")
{
    StencilProcessor proc;
    auto& apvts = proc.getApvts();

    auto checkIntRange = [&](const char* id, int want_lo, int want_hi)
    {
        auto* p = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(id));
        REQUIRE(p != nullptr);
        INFO(id);
        CHECK(p->getRange().getStart() == want_lo);
        CHECK(p->getRange().getEnd() == want_hi);
    };

    checkIntRange(pid::length, 2, 32);
    checkIntRange(pid::rangeLo, 0, 127);
    checkIntRange(pid::rangeHi, 0, 127);
    checkIntRange(pid::inputChannel, 0, 16);
    checkIntRange(pid::outputVelocity, 1, 127);
    checkIntRange(pid::outputChannel, 1, 16);
}

TEST_CASE("APVTS choice params expose correct option counts",
          "[plugin][apvts][choices]")
{
    StencilProcessor proc;
    auto& apvts = proc.getApvts();

    auto* sub = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter(pid::subdivision));
    REQUIRE(sub != nullptr);
    CHECK(sub->choices.size() == 5);  // 8th, 16th, 32nd, 8T, 16T

    auto* tm = dynamic_cast<juce::AudioParameterChoice*>(
        apvts.getParameter(pid::triggerMode));
    REQUIRE(tm != nullptr);
    CHECK(tm->choices.size() == 3);  // auto, gate, seed
}

// ─── readParams ───────────────────────────────────────────────────────────

TEST_CASE("readParams maps APVTS into engine SequencerParams",
          "[plugin][read_params]")
{
    StencilProcessor proc;
    const SequencerParams p = readParams(proc.getApvts());
    CHECK(p.length         == defaults::length);
    CHECK(p.lock           == static_cast<double>(defaults::lock));
    CHECK(p.density        == static_cast<double>(defaults::density));
    CHECK(p.rangeLo        == defaults::rangeLo);
    CHECK(p.rangeHi        == defaults::rangeHi);
    CHECK(p.subdivision    == Subdivision::Sixteenth);
    CHECK(p.seed           == static_cast<uint32_t>(defaults::seed));
    CHECK(p.triggerMode    == TriggerMode::Auto);
    CHECK(p.inputChannel   == defaults::inputChannel);
    CHECK(p.outputVelocity == defaults::outputVelocity);
    CHECK(p.outputGate     == static_cast<double>(defaults::outputGate));
    CHECK(p.outputChannel  == defaults::outputChannel);
}

// Range invariant — APVTS::Listener fires synchronously inside
// AudioParameter::Listener::parameterValueChanged (verified empirically
// against JUCE 8.0.12). The clamp is observable immediately after
// setValueNotifyingHost without pumping the message thread. readParams
// also swaps engine-side as a defensive belt-and-braces; that path
// can't be exercised through APVTS in tests because the listener
// always settles to lo ≤ hi first.

TEST_CASE("rangeLo above rangeHi: listener pushes rangeHi up to match",
          "[plugin][apvts][range_clamp]")
{
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    auto* loParam = apvts.getParameter(pid::rangeLo);

    // Defaults: lo=48 hi=72. Move lo to 90 (above hi).
    loParam->setValueNotifyingHost(loParam->convertTo0to1(90.0f));

    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeLo)) == 90);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeHi)) == 90);
}

TEST_CASE("rangeHi below rangeLo: listener pushes rangeLo down to match",
          "[plugin][apvts][range_clamp]")
{
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    auto* hiParam = apvts.getParameter(pid::rangeHi);

    // Defaults: lo=48 hi=72. Move hi to 30 (below lo).
    hiParam->setValueNotifyingHost(hiParam->convertTo0to1(30.0f));

    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeLo)) == 30);
    CHECK(static_cast<int>(*apvts.getRawParameterValue(pid::rangeHi)) == 30);
}

// ─── State save / restore ────────────────────────────────────────────────

TEST_CASE("getStateInformation / setStateInformation round-trip",
          "[plugin][state]")
{
    StencilProcessor proc1;
    auto& apvts1 = proc1.getApvts();

    // Push non-default values into apvts1.
    auto* lockParam = apvts1.getParameter(pid::lock);
    auto* lengthParam = apvts1.getParameter(pid::length);
    auto* seedParam = apvts1.getParameter(pid::seed);
    auto* velParam = apvts1.getParameter(pid::outputVelocity);
    REQUIRE(lockParam != nullptr);
    REQUIRE(lengthParam != nullptr);
    REQUIRE(seedParam != nullptr);
    REQUIRE(velParam != nullptr);

    lockParam->setValueNotifyingHost(lockParam->convertTo0to1(0.7f));
    lengthParam->setValueNotifyingHost(lengthParam->convertTo0to1(16.0f));
    seedParam->setValueNotifyingHost(seedParam->convertTo0to1(12345.0f));
    velParam->setValueNotifyingHost(velParam->convertTo0to1(77.0f));

    // Serialize.
    juce::MemoryBlock block;
    proc1.getStateInformation(block);
    REQUIRE(block.getSize() > 0);

    // Restore into a fresh processor.
    StencilProcessor proc2;
    proc2.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    auto& apvts2 = proc2.getApvts();

    CHECK(*apvts2.getRawParameterValue(pid::lock)  == 0.7f);
    CHECK(static_cast<int>(*apvts2.getRawParameterValue(pid::length)) == 16);
    CHECK(static_cast<int>(*apvts2.getRawParameterValue(pid::seed))   == 12345);
    CHECK(static_cast<int>(*apvts2.getRawParameterValue(pid::outputVelocity)) == 77);
}

// ─── processBlock dispatch ────────────────────────────────────────────────

TEST_CASE("processBlock with playing transport emits noteOn at boundaries",
          "[plugin][processBlock]")
{
    // 120 BPM, 48 kHz, 24000-sample block = exactly one quarter note.
    // 16th-note subdivision → 4 boundaries in the block. Pre-2026-05-16
    // density semantics let density-fill fire empty bits; the new
    // `bit0 && densityPass` rule means bit=0 steps are silent. Force
    // an all-1s register with length=2, seed=0 (vector: register=0b11)
    // and lock=1.0 so every step's bit-tap fires. density=1.0 keeps the
    // gate open.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();

    apvts.getParameter(pid::length)->setValueNotifyingHost(
        apvts.getParameter(pid::length)->convertTo0to1(2.0f));
    apvts.getParameter(pid::seed)->setValueNotifyingHost(
        apvts.getParameter(pid::seed)->convertTo0to1(0.0f));
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::outputGate)->setValueNotifyingHost(0.5f);


    const double sampleRate = 48000.0;
    const int blockSamples = 24000;
    proc.prepareToPlay(sampleRate, blockSamples);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, blockSamples);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    int noteOnCount = 0;
    int noteOffCount = 0;
    std::vector<int> noteOnSampleOffsets;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            ++noteOnCount;
            noteOnSampleOffsets.push_back(meta.samplePosition);
        }
        else if (msg.isNoteOff())
            ++noteOffCount;
    }
    CHECK(noteOnCount == 4);
    // outputGate=0.5 × stepDur=6000 = 3000 samples. NoteOff should land
    // inside the same block for every noteOn (3000 < 6000 step distance).
    CHECK(noteOffCount == 4);
    REQUIRE(noteOnSampleOffsets.size() == 4);
    CHECK(noteOnSampleOffsets[0] == 0);
    CHECK(noteOnSampleOffsets[1] == 6000);
    CHECK(noteOnSampleOffsets[2] == 12000);
    CHECK(noteOnSampleOffsets[3] == 18000);
}

TEST_CASE("processBlock publishes mutated bit snapshot on shiftAndFlip flip",
          "[plugin][processBlock][snapshot]")
{
    // ADR 007 §Audit follow-ups (mutated-bit salmon highlight) + ADR 007
    // §Visual playhead alignment. Editor consumes registerSnapshot_ as
    // the PRE-shift register so bit 0 = just-emitted bit. When
    // shiftAndFlip flips the consumed LSB, that flipped value lives at
    // bit 0 of the pre-shift snapshot (the bit the user is currently
    // hearing), so the salmon highlight target is index 0 — not the
    // post-shift MSB index. Semantics:
    //   - lock = 0 → flip prob = 1 → every step flips → snapshot = 0
    //   - lock = 1 → flip prob = 0 → no flip → snapshot = -1
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    SECTION("lock = 0 → every step flips → snapshot is 0 (just-emitted bit)")
    {
        apvts.getParameter(pid::lock)->setValueNotifyingHost(0.0f);
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
        CHECK(proc.getMutatedBitSnapshot() == 0);
    }

    SECTION("lock = 1 → no flips → snapshot stays at -1")
    {
        apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
        CHECK(proc.getMutatedBitSnapshot() == -1);
    }
}

TEST_CASE("processBlock with stopped transport emits no notes",
          "[plugin][processBlock]")
{
    StencilProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(false);  // stopped
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 512);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    int noteOnCount = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            ++noteOnCount;
    CHECK(noteOnCount == 0);
}

TEST_CASE("processBlock playing→stopped edge emits all-notes-off panic",
          "[plugin][processBlock][panic]")
{
    StencilProcessor proc;
    proc.prepareToPlay(48000.0, 24000);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    // First block: playing, emits notes.
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // Second block: stopped — should emit panic CC 123 on all 16 channels.
    ph.position.setIsPlaying(false);
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);

        int allNotesOffCount = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (msg.isControllerOfType(123))
                ++allNotesOffCount;
        }
        CHECK(allNotesOffCount == 16);  // CC 123 on every channel
    }
}

TEST_CASE("processBlock noteOff schedules across blocks for long gates",
          "[plugin][processBlock][cross_block]")
{
    // outputGate=1.0 × 16th-note (6000 samples at 120bpm 48kHz) = 6000.
    // With a 4096-sample block, the noteOff lands in the next block.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::outputGate)->setValueNotifyingHost(1.0f);


    proc.prepareToPlay(48000.0, 4096);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    // Block 1: noteOn at sample 0 (boundary 0); noteOff scheduled at
    // sample 6000 — past blockSamples=4096, so deferred.
    {
        juce::AudioBuffer<float> audio(0, 4096);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);

        int noteOnCount = 0, noteOffCount = 0;
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOn()) ++noteOnCount;
            else if (meta.getMessage().isNoteOff()) ++noteOffCount;
        }
        CHECK(noteOnCount == 1);
        CHECK(noteOffCount == 0);  // deferred to next block
    }

    // Block 2: noteOff fires at sample (6000 - 4096) = 1904 within this block.
    ph.position.setPpqPosition(juce::makeOptional(4096.0 / 24000.0));  // advance ppq
    {
        juce::AudioBuffer<float> audio(0, 4096);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);

        int noteOffAt1904 = 0;
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOff() && meta.samplePosition == 1904)
                ++noteOffAt1904;
        }
        CHECK(noteOffAt1904 == 1);
    }
}

TEST_CASE("processBlock seed-mode input shifts register via shiftAndForce",
          "[plugin][processBlock][trigger_mode]")
{
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(2.0f));  // seed


    proc.prepareToPlay(48000.0, 512);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(false);  // not playing — input only
    proc.setPlayHead(&ph);

    // Send a single noteOn via the MIDI input buffer.
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8{ 100 }), 0);

    juce::AudioBuffer<float> audio(0, 512);
    proc.processBlock(audio, midi);

    // Sequencer should now be seed-activated.
    CHECK(proc.getSequencerForTest().isSeedActivated());
}

// ─── Hung-note flush (ADR 007 §Note-off discipline) ──────────────────────
//
// Helper: drive proc through one block at 120 BPM with outputGate=1.0 so a
// noteOn lands at offset 0 and its noteOff is scheduled for 6000 samples
// later — past the 4096-sample block, so it lives in pendingNoteOffs_.

namespace
{
struct LongGateFixture
{
    LongGateFixture(StencilProcessor& proc)
    {
        auto& apvts = proc.getApvts();
        apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
        apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
        apvts.getParameter(pid::outputGate)->setValueNotifyingHost(1.0f);
        proc.prepareToPlay(48000.0, 4096);

        ph.position.setBpm(juce::makeOptional(120.0));
        ph.position.setPpqPosition(juce::makeOptional(0.0));
        ph.position.setIsPlaying(true);
        proc.setPlayHead(&ph);

        // Block 1: noteOn at offset 0; noteOff scheduled across blocks.
        juce::AudioBuffer<float> audio(0, 4096);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);

        int noteOnCount = 0, noteOffCount = 0;
        for (const auto meta : midi)
        {
            if (meta.getMessage().isNoteOn()) ++noteOnCount;
            else if (meta.getMessage().isNoteOff()) ++noteOffCount;
        }
        REQUIRE(noteOnCount == 1);
        REQUIRE(noteOffCount == 0);  // pending — must flush on next event
    }

    MockPlayHead ph;
};

// Run a single block of the given size and return (noteOff-count, has-CC123).
struct DrainResult
{
    int noteOffCount = 0;
    int allNotesOffCount = 0;
};

DrainResult drainOneBlock(StencilProcessor& proc, int blockSamples)
{
    juce::AudioBuffer<float> audio(0, blockSamples);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    DrainResult r;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOff() && meta.samplePosition == 0) ++r.noteOffCount;
        else if (m.isControllerOfType(123)) ++r.allNotesOffCount;
    }
    return r;
}
}  // namespace

TEST_CASE("setStateInformation flushes pending noteOffs at next block",
          "[plugin][hung_note][state_load]")
{
    // concept.md §Note-off discipline: state load must clear sounding notes.
    StencilProcessor proc;
    LongGateFixture fx(proc);

    // Capture state of a fresh processor and load it — replaceState fires
    // listeners, which must request a hung-note flush.
    StencilProcessor donor;
    juce::MemoryBlock block;
    donor.getStateInformation(block);
    proc.setStateInformation(block.getData(), static_cast<int>(block.getSize()));

    // Next block: the pending noteOff from block 1 must drain at offset 0.
    fx.ph.position.setPpqPosition(juce::makeOptional(4096.0 / 24000.0));
    const auto r = drainOneBlock(proc, 4096);
    CHECK(r.noteOffCount >= 1);  // drained pending at offset 0
}

TEST_CASE("length parameter change flushes pending noteOffs at next block",
          "[plugin][hung_note][param_change]")
{
    // ADR 007 §Note-off discipline lists length as a flush trigger.
    StencilProcessor proc;
    LongGateFixture fx(proc);

    proc.getApvts().getParameter(pid::length)->setValueNotifyingHost(
        proc.getApvts().getParameter(pid::length)->convertTo0to1(16.0f));

    fx.ph.position.setPpqPosition(juce::makeOptional(4096.0 / 24000.0));
    const auto r = drainOneBlock(proc, 4096);
    CHECK(r.noteOffCount >= 1);
}

TEST_CASE("processBlockBypassed on entry flushes pending noteOffs + emits panic",
          "[plugin][hung_note][bypass]")
{
    // ADR 007 §Note-off discipline: bypass enable must clear sounding notes.
    StencilProcessor proc;
    LongGateFixture fx(proc);

    juce::AudioBuffer<float> audio(0, 4096);
    juce::MidiBuffer midi;
    proc.processBlockBypassed(audio, midi);

    int noteOffAt0 = 0;
    int allNotesOffCount = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOff() && meta.samplePosition == 0) ++noteOffAt0;
        else if (m.isControllerOfType(123)) ++allNotesOffCount;
    }
    CHECK(noteOffAt0 >= 1);
    CHECK(allNotesOffCount == 16);  // CC 123 on every channel — full panic
}

TEST_CASE("processBlock !playing→playing edge resets register to seed-derived state",
          "[plugin][processBlock][transport_reset]")
{
    // ADR 007 §Cross-target audible parity / concept.md §Transport.
    // After running the engine, stopping, and restarting at the same
    // (seed, length), the register must replay the same loop — m4l does
    // this in transportStart()'s freshRegister() call; vst must too,
    // otherwise cross-target parity breaks after the first stop/start.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::lock)->setValueNotifyingHost(0.5f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);

    const double sampleRate = 48000.0;
    const int blockSamples = 24000;  // one quarter @ 120 BPM
    proc.prepareToPlay(sampleRate, blockSamples);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, blockSamples);

    // Run 1: starting from stopped construction state, play 4 16th-note steps.
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    const auto regAfterFirstRun = proc.getSequencerForTest().getRegister();

    // Stop.
    ph.position.setIsPlaying(false);
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // Run 2: start again at ppq=0. Register must be re-derived from seed.
    ph.position.setIsPlaying(true);
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    const auto regAfterSecondRun = proc.getSequencerForTest().getRegister();

    CHECK(regAfterSecondRun == regAfterFirstRun);
}

TEST_CASE("processBlock !playing→playing edge resets cumulative step counter",
          "[plugin][processBlock][transport_reset]")
{
    // The editor's ring rotation reads getCumulativeSteps(); on transport
    // restart it must zero so the head pointer realigns with bit 0
    // (matches m4l, where bridge.ringHead emits host.position which
    // resets on transportStart).
    StencilProcessor proc;
    proc.prepareToPlay(48000.0, 24000);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 24000);

    // Run 1: 4 steps.
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    CHECK(proc.getCumulativeSteps() == 4);

    // Stop.
    ph.position.setIsPlaying(false);
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // Start again at ppq=0. Run 2's 4 steps land on a freshly-zeroed counter,
    // so the editor sees 4 (not 8) — head pointer realigned.
    ph.position.setIsPlaying(true);
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    {
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    CHECK(proc.getCumulativeSteps() == 4);
}

TEST_CASE("processBlock seed/length param change re-creates register",
          "[plugin][processBlock][reset]")
{
    StencilProcessor proc;
    proc.prepareToPlay(48000.0, 512);

    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(false);
    proc.setPlayHead(&ph);

    // First block to sync sequencer with default seed=42.
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    const auto regBefore = proc.getSequencerForTest().getRegister();

    // Change seed via APVTS.
    proc.getApvts().getParameter(pid::seed)->setValueNotifyingHost(
        proc.getApvts().getParameter(pid::seed)->convertTo0to1(100.0f));


    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    const auto regAfter = proc.getSequencerForTest().getRegister();
    CHECK(regAfter != regBefore);  // re-init from new seed produced different state
}

// ─── Gate-mode visual freeze (ADR 007 §MIDI processing — gate branch) ────

TEST_CASE("processBlock gate-mode with no held input freezes visual snapshots",
          "[plugin][processBlock][trigger_mode][gate_idle]")
{
    // ADR 007 §MIDI processing: in triggerMode = gate, while the held-set
    // is empty, transport-driven steps are silent and register / rng are
    // frozen. The editor's ring uses getLastStepTimeMicros() as the
    // γ-anticipation anchor and getCumulativeSteps() as the dirty-flag
    // trigger; both must stay quiescent in gate-idle so the ring doesn't
    // keep rotating CW step-by-step through bits that never sounded.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(1.0f));  // gate

    // 120 BPM, 48 kHz, 24000-sample block = one quarter = 4 × 16th steps.
    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    const auto regBefore = proc.getRegisterSnapshot();

    juce::AudioBuffer<float> audio(0, 24000);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    CHECK(proc.getCumulativeSteps() == 0);
    CHECK(proc.getLastStepTimeMicros() == 0);
    CHECK(proc.getRegisterSnapshot() == regBefore);

    int noteOnCount = 0;
    for (const auto meta : midi)
        if (meta.getMessage().isNoteOn())
            ++noteOnCount;
    CHECK(noteOnCount == 0);
}

TEST_CASE("processBlock gate-mode clears animation anchor when input released",
          "[plugin][processBlock][trigger_mode][gate_idle]")
{
    // Holding a key advances cumulativeSteps and anchors lastStepTime;
    // releasing it must freeze further snapshot publication AND zero
    // lastStepTime so the ring snaps back to the static frame instead of
    // completing the in-flight γ-anticipation rotation toward a step that
    // will never sound.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(1.0f));  // gate
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    // Block 1: noteOn at sample 0 → engine processes all 4 step boundaries.
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8{ 100 }), 0);
        proc.processBlock(audio, midi);
    }
    CHECK(proc.getCumulativeSteps() == 4);
    CHECK(proc.getLastStepTimeMicros() > 0);

    // Block 2: noteOff at sample 0 → gate-idle for all 4 boundaries.
    ph.position.setPpqPosition(juce::makeOptional(0.25));
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        proc.processBlock(audio, midi);
    }
    CHECK(proc.getCumulativeSteps() == 4);          // no advance during gate-idle
    CHECK(proc.getLastStepTimeMicros() == 0);       // animation anchor cleared
}

// ─── Sample-accurate input MIDI (ADR 007 §Audit follow-ups — RT safety) ──

TEST_CASE("processBlock interleaves input MIDI with subdivision boundaries",
          "[plugin][processBlock][input_timing][rt_safety]")
{
    // ADR 007 §Audit follow-ups: input events must mutate Sequencer state
    // at their actual samplePosition within the block, not at block-start.
    // gate-mode at 120 BPM / 48 kHz / 24000-sample block places boundaries
    // at samples [0, 6000, 12000, 18000]. A noteOn at sample 7000 + noteOff
    // at sample 13000 means:
    //   - boundary 0 (0):     gate idle  → silent
    //   - boundary 1 (6000):  gate idle  → silent
    //   - boundary 2 (12000): gate open  → emit
    //   - boundary 3 (18000): gate idle  → silent
    // Block-quantized drain would open the gate before boundary 0 and
    // close it before block end, so every boundary would emit or be
    // silent depending on the noteOff offset within the queue — the
    // wrong audible result either way.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(1.0f));  // gate
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::outputGate)->setValueNotifyingHost(0.5f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 24000);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8{ 100 }), 7000);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 13000);
    proc.processBlock(audio, midi);

    int noteOnCount = 0;
    int noteOnOffset = -1;
    for (const auto meta : midi)
    {
        if (meta.getMessage().isNoteOn())
        {
            ++noteOnCount;
            noteOnOffset = meta.samplePosition;
        }
    }
    CHECK(noteOnCount == 1);          // only boundary 2 emits
    CHECK(noteOnOffset == 12000);     // sample-accurate to the boundary
}

TEST_CASE("processBlock input MIDI at boundary samplePosition opens gate at that step",
          "[plugin][processBlock][input_timing][rt_safety]")
{
    // Tie-break: an input event exactly at a boundary's sampleOffset must
    // be applied BEFORE the boundary's processStep, so the boundary sees
    // the mutated held-set. NoteOn at sample 6000 must open the gate at
    // boundary 1 (sample 6000), not boundary 2.
    //
    // length=2, seed=0 gives a fixed register=0b11; lock=1.0 holds it
    // through the rotation, so bit 0 stays set across all steps and every
    // gate-open boundary emits.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::length)->setValueNotifyingHost(
        apvts.getParameter(pid::length)->convertTo0to1(2.0f));
    apvts.getParameter(pid::seed)->setValueNotifyingHost(
        apvts.getParameter(pid::seed)->convertTo0to1(0.0f));
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(1.0f));  // gate
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::outputGate)->setValueNotifyingHost(0.5f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 24000);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8{ 100 }), 6000);
    proc.processBlock(audio, midi);

    int firstNoteOnOffset = -1;
    int noteOnCount = 0;
    for (const auto meta : midi)
    {
        if (meta.getMessage().isNoteOn())
        {
            if (firstNoteOnOffset < 0) firstNoteOnOffset = meta.samplePosition;
            ++noteOnCount;
        }
    }
    CHECK(noteOnCount == 3);              // boundaries 1, 2, 3 emit
    CHECK(firstNoteOnOffset == 6000);     // boundary 1 saw the noteOn
}

// ─── Editor snapshot tuple coherence (ADR 007 §Audit follow-ups — RT) ────

TEST_CASE("readEditorSnapshot returns fields from a single boundary",
          "[plugin][snapshot][rt_safety]")
{
    // After processBlock with deterministic params, readEditorSnapshot's
    // fields are consistent with the per-field accessor returns. Single-
    // thread test exercises the API shape; the cross-thread coherence
    // guarantee comes from the version-counter (seqlock) brackets around
    // the writes, which is encoded in PluginProcessor::publishSnapshot.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::length)->setValueNotifyingHost(
        apvts.getParameter(pid::length)->convertTo0to1(2.0f));
    apvts.getParameter(pid::seed)->setValueNotifyingHost(
        apvts.getParameter(pid::seed)->convertTo0to1(0.0f));
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 24000);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    const auto s = proc.readEditorSnapshot();
    CHECK(s.reg     == proc.getRegisterSnapshot());
    CHECK(s.steps   == proc.getCumulativeSteps());
    CHECK(s.note    == proc.getLastNote());
    CHECK(s.active  == proc.getLastActive());
    CHECK(s.mutated == proc.getMutatedBitSnapshot());
    CHECK(s.steps == 4);     // 4 boundaries in this block
    CHECK(s.active);         // length=2 seed=0 register=0b11, every step active
}

// ─── outputGate=0 emission clamp (ADR 007 §Audit follow-ups) ──────────────

TEST_CASE("processBlock outputGate=0 emits noteOff at least 1 sample after noteOn",
          "[plugin][processBlock][output_gate]")
{
    // ADR 007 §Audit follow-ups: with outputGate=0 the naive computation
    // gives `gateSamples = floor(0 × stepDur + 0.5) = 0`, collapsing the
    // noteOff onto the noteOn at the same sampleOffset. Synths render
    // that as a click or silence. The processor floors gateSamples at 1
    // so every active step is a well-formed note with nonzero duration.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::length)->setValueNotifyingHost(
        apvts.getParameter(pid::length)->convertTo0to1(2.0f));
    apvts.getParameter(pid::seed)->setValueNotifyingHost(
        apvts.getParameter(pid::seed)->convertTo0to1(0.0f));
    apvts.getParameter(pid::lock)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);
    apvts.getParameter(pid::outputGate)->setValueNotifyingHost(0.0f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    juce::AudioBuffer<float> audio(0, 24000);
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);

    int firstNoteOnOffset = -1;
    int firstNoteOffOffset = -1;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && firstNoteOnOffset < 0)
            firstNoteOnOffset = meta.samplePosition;
        else if (m.isNoteOff() && firstNoteOffOffset < 0)
            firstNoteOffOffset = meta.samplePosition;
    }
    REQUIRE(firstNoteOnOffset >= 0);
    REQUIRE(firstNoteOffOffset >= 0);
    CHECK(firstNoteOffOffset >= firstNoteOnOffset + 1);
}

// ─── Seed-mode register preservation across transport bounce ─────────────

TEST_CASE("processBlock seed-mode user-written register survives transport bounce",
          "[plugin][processBlock][trigger_mode][seed_persistence]")
{
    // ADR 007 §Audit follow-ups: in triggerMode = seed, the user writes
    // bits into the register via input notes. The Issue 2 start-edge
    // currently re-derives register from (seed, length) on every
    // play-press, discarding that pattern. The fix: when seedActivated_
    // is true at the start edge, skip the re-derive so the user's loop
    // resumes across a stop / start cycle.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    apvts.getParameter(pid::triggerMode)->setValueNotifyingHost(
        apvts.getParameter(pid::triggerMode)->convertTo0to1(2.0f));  // seed

    proc.prepareToPlay(48000.0, 512);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(false);
    proc.setPlayHead(&ph);

    // Sync: stopped block flushes initial state.
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // User writes the register: seed-mode noteOn shifts a 1 into the
    // head; multiple noteOns drive the register away from seed-derived.
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8{ 100 }), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 61, juce::uint8{ 100 }), 100);
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, juce::uint8{ 100 }), 200);
        proc.processBlock(audio, midi);
    }
    REQUIRE(proc.getSequencerForTest().isSeedActivated());
    const auto regAfterUserInput = proc.getSequencerForTest().getRegister();

    // Press play.
    ph.position.setIsPlaying(true);
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // Press stop.
    ph.position.setIsPlaying(false);
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // Press play again — start-edge MUST NOT re-derive register, because
    // the user's seed-driven pattern is still authoritative.
    ph.position.setIsPlaying(true);
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    {
        juce::AudioBuffer<float> audio(0, 512);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }

    // The register the player hears at the second play must equal the
    // pattern they wrote, not the seed-derived initial state.
    CHECK(proc.getSequencerForTest().getRegister() == regAfterUserInput);
    CHECK(proc.getSequencerForTest().isSeedActivated());
}

TEST_CASE("processBlock auto-mode register still re-derives on transport bounce",
          "[plugin][processBlock][trigger_mode][seed_persistence]")
{
    // The seed-mode preservation must NOT leak into auto / gate modes —
    // there the seeded-determinism contract (concept.md §Transport)
    // stands and every transport-start re-rolls. Equivalent to the
    // existing transport_reset case: after run 1, stop, run 2 at the
    // same (seed, length) plays the *same* loop, so the post-run-2
    // register matches post-run-1.
    StencilProcessor proc;
    auto& apvts = proc.getApvts();
    // Default triggerMode = auto.
    apvts.getParameter(pid::lock)->setValueNotifyingHost(0.5f);
    apvts.getParameter(pid::density)->setValueNotifyingHost(1.0f);

    proc.prepareToPlay(48000.0, 24000);
    MockPlayHead ph;
    ph.position.setBpm(juce::makeOptional(120.0));
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    ph.position.setIsPlaying(true);
    proc.setPlayHead(&ph);

    // Run 1: one quarter from a fresh start.
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    const auto regAfterRun1 = proc.getSequencerForTest().getRegister();

    // Stop, then start again at ppq=0.
    ph.position.setIsPlaying(false);
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    ph.position.setIsPlaying(true);
    ph.position.setPpqPosition(juce::makeOptional(0.0));
    {
        juce::AudioBuffer<float> audio(0, 24000);
        juce::MidiBuffer midi;
        proc.processBlock(audio, midi);
    }
    // Run 2 starts from re-derived register and runs the same loop →
    // ends in the same state as run 1.
    CHECK(proc.getSequencerForTest().getRegister() == regAfterRun1);
    // seedActivated_ stays false in auto mode.
    CHECK_FALSE(proc.getSequencerForTest().isSeedActivated());
}
