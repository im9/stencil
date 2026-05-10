// Read-only output history strip below the ring + right rail
// (ADR 007 §Editor layout).
//
// Inboil's TuringSheet draws bars by `turingSimulate(params, targetSteps,
// seed)` — i.e. it re-derives the next N steps from the current params,
// and highlights the bar at the playhead step. Stencil/vst follows the
// same approach: simulate `length` steps fresh each repaint and highlight
// the bar at `cumulativeSteps % length`. The cost is bounded (≤ 32 calls
// to engine::tmStep at 15 Hz repaints) and avoids carrying audio-thread
// history into the editor.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

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

private:
    void timerCallback() override;

    struct StepSnap
    {
        engine::RegisterBits reg;
        int note;
        bool active;
    };

    // Re-runs the engine for `length` steps from the current params. Pure
    // function so the simulation result is independent of editor or
    // audio-thread mutable state — playback just shifts which bar is
    // highlighted.
    static std::vector<StepSnap> simulate(const engine::SequencerParams& p);

    plugin::StencilProcessor& processor_;

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
