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
    // Audio thread publishes after each step; editor thread reads with no
    // locking. Eventually-consistent — the editor may show a register one
    // step behind under load; never used as the source of MIDI emission.
    engine::RegisterBits getRegisterSnapshot() const { return registerSnapshot_.load(std::memory_order_relaxed); }
    int  getCumulativeSteps() const { return cumulativeStepsSnapshot_.load(std::memory_order_relaxed); }
    int  getLastNote() const        { return lastNoteSnapshot_.load(std::memory_order_relaxed); }
    bool getLastActive() const      { return lastActiveSnapshot_.load(std::memory_order_relaxed); }

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

    // Editor-thread snapshots, published after each step from processBlock.
    // Default register=0 is fine: the editor reads "current register bits"
    // and 0 just renders an all-empty ring until the first step lands.
    std::atomic<engine::RegisterBits> registerSnapshot_{0};
    std::atomic<int>  cumulativeStepsSnapshot_{0};
    std::atomic<int>  lastNoteSnapshot_{60};
    std::atomic<bool> lastActiveSnapshot_{false};

    // Range invariant listener: clamps rangeHi up when rangeLo crosses it,
    // and rangeLo down when rangeHi crosses it (matches m4l host setParam
    // shape — the side that just moved is the side that gets clamped).
    void parameterChanged(const juce::String& parameterID, float newValue) override;

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
