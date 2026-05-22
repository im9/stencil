// Read-only output history strip below the ring + right rail
// (ADR 007 §Editor layout).
//
// Step-sequencer cells: each cell represents step position 0..length-1.
// Cells are LIVE-ONLY — they fill in from the audio thread's published
// snapshot as steps fire, one slot at a time. A previously-attempted
// "simulate from seed" pre-population caused visible note-label flips
// the moment the playhead reached each cell (lock<1 makes simulated
// iteration-1 differ from the live iteration-2+), so the preview was
// dropped in favour of "never lie, just show what actually played."
// On a pattern-altering parameter change the slots are invalidated and
// fill again from the next emission.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>

#include "Engine/Turing.h"
#include "Plugin/PluginProcessor.h"

namespace stencil {
namespace editor {

class HistoryView : public juce::Component, private juce::Timer
{
public:
    explicit HistoryView(plugin::StencilProcessor&);
    ~HistoryView() override;

    void paint(juce::Graphics&) override;

    // Public so the free simulation helper (and tests, if any) can name
    // the slot type. Just data, no invariants.
    struct StepSnap
    {
        engine::RegisterBits reg = 0;
        int note = 0;
        bool active = false;
        bool valid = false;  // false until a snapshot populates the slot
    };

private:
    void timerCallback() override;

    plugin::StencilProcessor& processor_;

    // Length cap matches the APVTS max (2..32). Slot i corresponds to
    // step position i in the loop; bars beyond params.length are not
    // drawn.
    std::array<StepSnap, 32> slots_{};
    int lastSeenSteps_ = 0;

    // Diff state for the timer-driven repaint gate. Same pattern as
    // RingView: only repaint when something visible actually changed.
    int lastSteps_ = -1;
    uint32_t lastSeed_ = 0;
    int lastLength_ = 0;
    float lastLock_ = -1.0f;
    float lastDensity_ = -1.0f;
    int lastRangeLo_ = -1;
    int lastRangeHi_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryView)
};

}  // namespace editor
}  // namespace stencil
