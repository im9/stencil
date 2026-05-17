// Stencil VST processor (ADR 007 Phase 2).
//
//   - APVTS holds all 13 parameters from concept.md §Parameter surface.
//   - processBlock reads AudioPlayHead, calls engine::Sequencer per
//     subdivision boundary, writes MidiBuffer events. NoteOff timing
//     follows outputGate × stepDuration, scheduled across blocks via
//     pendingNoteOffs_.
//   - Range clamp (rangeLo ≤ rangeHi) runs as an APVTS::Listener and is
//     also enforced engine-side in readParams() so the engine sees a
//     valid range even within the listener's settling window.
//   - State I/O via APVTS getStateInformation / setStateInformation.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "Engine/Sequencer.h"

namespace stencil::plugin
{
class StencilProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    StencilProcessor();
    ~StencilProcessor() override;

    // --- juce::AudioProcessor overrides ------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Stencil"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- Accessors ---------------------------------------------------------
    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }
    const juce::AudioProcessorValueTreeState& getApvts() const { return apvts; }

    // Test-facing inspection.
    const engine::Sequencer& getSequencerForTest() const { return sequencer_; }

    // ── Editor-thread snapshots (ADR 007 §Threading) ──────────────────────
    // Audio thread publishes after each step via publishSnapshot under a
    // seqlock version counter; editor thread reads with no locking.
    // Eventually-consistent — the editor may show a register one step
    // behind under load; never used as the source of MIDI emission.
    //
    // `reg` carries the *pre-shift* register state — the value whose LSB
    // was just consumed for emission. Bit 0 of this register is the bit
    // the user is currently hearing, which lets the editor render "bit
    // just emitted under the playhead triangle at moment of sounding"
    // (ADR 007 §Visual playhead alignment).
    //
    // `mutated` is 0 when shiftAndFlip flipped the just-emitted bit (the
    // new MSB value differs from the consumed LSB), -1 otherwise. With
    // the pre-shift snapshot the flipped bit lives at LSB (index 0) of
    // the displayed register — not at the post-shift MSB position. Drives
    // the salmon "mutated" highlight in RingView.
    struct EditorSnapshot
    {
        engine::RegisterBits reg     = 0;
        int                  steps   = 0;
        int                  note    = 60;
        bool                 active  = false;
        int                  mutated = -1;
    };

    // Coherent multi-field read for the editor — single call returns a
    // tuple captured at one audio-thread step. Spins under in-flight
    // writes (bounded; writer is ~6 atomic ops). Prefer this over the
    // per-field accessors when the editor needs more than one field in
    // one paint, otherwise field stores from different steps can tear
    // (ADR 007 §Audit follow-ups — tuple coherence).
    EditorSnapshot readEditorSnapshot() const;

    // Per-field accessors. Each returns a single atomic load. Safe when
    // the caller only needs one field; coherent with itself but NOT
    // with the other accessors if called separately.
    engine::RegisterBits getRegisterSnapshot() const { return snapshotReg_.load(std::memory_order_relaxed); }
    int  getCumulativeSteps()    const { return snapshotSteps_.load(std::memory_order_relaxed); }
    int  getLastNote()           const { return snapshotNote_.load(std::memory_order_relaxed); }
    bool getLastActive()         const { return snapshotActive_.load(std::memory_order_relaxed); }
    int  getMutatedBitSnapshot() const { return snapshotMutated_.load(std::memory_order_relaxed); }

    // ── γ-anticipation playhead timing (ADR 007 §Visual) ──────────────────
    // Wall-clock timestamp of the most recent step boundary in
    // microseconds (from juce::Time::getMillisecondCounterHiRes), and
    // the duration of a single subdivision step at the current bpm.
    // RingView computes `phase = (now - lastStepTime) / stepDuration`
    // and feeds that to RingLogic::phaseRotationDegrees so the ring
    // eases CW into the next emission across the last 20% of each step.
    int64_t getLastStepTimeMicros() const { return lastStepTimeMicros_.load(std::memory_order_relaxed); }
    int64_t getStepDurationMicros() const { return stepDurationMicros_.load(std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState apvts;

    engine::Sequencer sequencer_;

    // Cached snapshots so seed/length param changes can re-init the
    // sequencer at processBlock entry without re-init on every callback.
    int lastSeed_ = 0;
    int lastLength_ = 0;

    // Cross-block noteOff scheduling. samplesUntilFire is "samples after
    // the START of the next block until this noteOff fires"; decremented
    // by blockSamples each processBlock entry. When < blockSamples, the
    // noteOff fires inside the current block at that offset.
    struct PendingNoteOff
    {
        int samplesUntilFire;
        int note;
        int channel;
    };
    std::vector<PendingNoteOff> pendingNoteOffs_;

    // Reused across processBlock invocations to avoid audio-thread
    // allocations from detectBoundaries (ADR 007 §Audit follow-ups —
    // RT safety). Capacity reserved in prepareToPlay; size grows on
    // first few blocks and is then stable.
    std::vector<engine::StepBoundary> boundariesBuffer_;

    // Detect transport stop edges across blocks for hung-note flush.
    bool wasPlaying_ = false;
    double sampleRate_ = 44100.0;

    // Bypass edge tracking. processBlockBypassed override emits panic on
    // the active→bypass edge (ADR 007 §Note-off discipline). Both bypass
    // entry / exit are observed on the audio thread, so a plain bool is
    // sufficient — no atomic needed.
    bool wasBypassed_ = false;

    // Hung-note flush request from the message thread (parameter changes
    // that affect note emission, setStateInformation). Audio thread
    // consumes the flag at the start of the next processBlock and emits
    // noteOff for every entry in pendingNoteOffs_ at offset 0. Drain-only
    // (no CC 123) — matches m4l host.ts setParam's flushNotesOn shape.
    std::atomic<bool> flushPendingRequested_{false};

    // Editor-thread snapshot, published after each step from processBlock
    // via publishSnapshot under a seqlock version counter. The audio
    // thread is the sole writer; fields are individually atomic so
    // per-field accessors stay well-defined, and snapshotVersion_'s
    // release/acquire orders the field stores into a coherent tuple for
    // readEditorSnapshot.
    //
    // `snapshotReg_` carries the pre-shift register (Sequencer::
    // getLastEmittedRegister) so bit 0 = "bit just emitted" lines up
    // with the playhead triangle at the moment of sounding. Default 0
    // renders an all-empty ring until the first step lands; harmless
    // because lastStepTimeMicros_ stays 0 too so the editor stays in
    // "static, no animation" mode.
    //
    // `snapshotMutated_` is 0 when shiftAndFlip flipped the just-emitted
    // bit (visible at LSB of the pre-shift snapshot), -1 = no flip.
    // Reset to -1 on prepareToPlay / setStateInformation / transport-
    // start re-roll so a stale mutation doesn't linger through
    // lifecycle resets.
    mutable std::atomic<uint32_t>     snapshotVersion_{0};
    std::atomic<engine::RegisterBits> snapshotReg_{0};
    std::atomic<int>                  snapshotSteps_{0};
    std::atomic<int>                  snapshotNote_{60};
    std::atomic<bool>                 snapshotActive_{false};
    std::atomic<int>                  snapshotMutated_{-1};
    // γ-anticipation playhead anchor + tempo. Microseconds keep the
    // atomics lock-free on 64-bit platforms. Zero on idle / lifecycle
    // reset so RingView treats "no recent step" as the static phase.
    std::atomic<int64_t> lastStepTimeMicros_{0};
    std::atomic<int64_t> stepDurationMicros_{0};

    // Range invariant listener: clamps rangeHi up when rangeLo crosses it,
    // and rangeLo down when rangeHi crosses it (matches m4l host setParam
    // shape — the side that just moved is the side that gets clamped).
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Seqlock-bracketed write of the editor snapshot tuple. Audio thread
    // is the sole writer; bumps snapshotVersion_ to odd before the field
    // stores and back to even after. Reader (readEditorSnapshot) spins
    // while it observes an odd version or a version change mid-read.
    // Wait-free for the writer; bounded retries for the reader.
    void publishSnapshot(engine::RegisterBits reg, int steps,
                         int note, bool active, int mutated);

    // Helpers
    void drainPendingNoteOffs(juce::MidiBuffer& midi, int blockSamples);
    // Drain pendingNoteOffs_ as noteOff events at sampleOffset and clear
    // the list. No CC 123 — used by parameter-change / state-load flushes.
    void flushPending(juce::MidiBuffer& midi, int sampleOffset);
    // flushPending + CC 123 (All Notes Off) on every channel.
    void emitPanic(juce::MidiBuffer& midi, int sampleOffset);
    int stepDurationSamples(double bpm, engine::Subdivision subdivision) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StencilProcessor)
};

}  // namespace stencil::plugin
