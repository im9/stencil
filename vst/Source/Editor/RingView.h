// Bit ring with snap-only playhead (matches m4l registerRing.jsui.js),
// head-bit highlight, and center fraction / note text (ADR 007
// §Editor — inboil TuringSheet port).
//
// Logic / renderer split (CLAUDE.md §GUI / UI components):
//   - Geometry and hit-testing live in RingLogic (no JUCE), unit-tested
//     in tests/test_RingLogic.cpp.
//   - This file is the renderer + JUCE event glue. The paint loop reads
//     the processor's atomic pre-shift register snapshot (bit 0 = bit
//     just emitted, sitting under the playhead triangle) and redraws on
//     snapshot change. The snapshot itself shifts CW by one slot at each
//     step boundary, so there is no rotation transform in the paint loop.
//
// Click semantics: ADR 007 §FREEZE / ROLL semantics — vst v1 routes any
// bit-circle hit to ROLL (re-seed). Direct bit edits require persistent
// register state in APVTS and are deferred to a follow-up ADR.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Plugin/PluginProcessor.h"

namespace stencil {
namespace editor {

class RingView : public juce::Component, private juce::Timer
{
public:
    explicit RingView(plugin::StencilProcessor&);
    ~RingView() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // Compute the ring's actual paint geometry from current bounds. The
    // ring scales to fit the smaller of width / (height - actionsBar)
    // with `theme::ringMargin` padding, so the bit circles never touch
    // the bounds edge regardless of editor resize.
    struct Geometry { float radius; float bitRadius; float cx; float cy; };
    Geometry currentGeometry() const;

    plugin::StencilProcessor& processor_;

    int lastDrawnSteps_ = -1;
    engine::RegisterBits lastDrawnRegister_ = 0;
    int lastDrawnNote_ = -2;  // -1 is a valid "cleared" snapshot value

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RingView)
};

}  // namespace editor
}  // namespace stencil
