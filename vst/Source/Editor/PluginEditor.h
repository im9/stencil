// Top-level plugin editor (ADR 007 §Editor — inboil TuringSheet port).
//
// Layout: header (40 px) → body (RingView flex + RightRailView 280 px) →
// HistoryView (136 px). Initial size 820 × 540 (ADR 007 §Layout);
// resizable so users can grow the ring on a large display while the rail
// and history bar stay at fixed widths/heights.

#pragma once

#include "Editor/ActionsView.h"
#include "Editor/HistoryView.h"
#include "Editor/RightRailView.h"
#include "Editor/RingView.h"
#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace stencil {
namespace editor {

class StencilEditor : public juce::AudioProcessorEditor
{
public:
    explicit StencilEditor(plugin::StencilProcessor&);
    ~StencilEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Test surfaces — let tests/test_Editor.cpp drive a synthetic mouse
    // press into the ring without depending on the editor's child
    // hierarchy layout. ADR 007 §Verification routes "click → ROLL"
    // through the same path a real DAW user would take.
    RingView& ringViewForTest()       { return ring_; }
    ActionsView& actionsViewForTest() { return actions_; }

private:
    plugin::StencilProcessor& processor_;
    ActionsView   actions_;
    RingView      ring_;
    RightRailView rail_;
    HistoryView   history_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StencilEditor)
};

}  // namespace editor
}  // namespace stencil
