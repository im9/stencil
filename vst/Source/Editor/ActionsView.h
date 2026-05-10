// FREEZE / ROLL action row above the ring (ADR 007 §Editor layout).
//
// FREEZE toggles `lock` between the user's previous value and 1.0; the
// "previous lock" stash is editor-local transient state (never persisted
// — see ADR 007 §Persistence). ROLL writes a fresh `seed`. Both buttons
// also exist visually at the bottom of the right rail (Reproducibility
// fieldset) per ADR; this view is the canonical action surface inboil
// drew above the ring.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

#include "Plugin/PluginProcessor.h"

namespace stencil {
namespace editor {

class ActionsView : public juce::Component, private juce::Timer
{
public:
    explicit ActionsView(plugin::StencilProcessor&);
    ~ActionsView() override;

    void resized() override;

private:
    void timerCallback() override;
    void onFreeze();
    void onRoll();

    plugin::StencilProcessor& processor_;
    juce::TextButton freezeBtn_{ "FREEZE" };
    juce::TextButton rollBtn_{ "ROLL" };
    std::optional<float> prevLock_;
    float lastDrawnLock_ = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActionsView)
};

}  // namespace editor
}  // namespace stencil
