#include "Editor/ActionsView.h"

#include "Editor/RingLogic.h"
#include "Editor/Theme.h"
#include "Engine/Rng.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

ActionsView::ActionsView(plugin::StencilProcessor& p)
    : processor_(p)
{
    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, theme::bg);
        b.setColour(juce::TextButton::textColourOffId, theme::fg);
        b.setColour(juce::TextButton::textColourOnId, theme::blue);
    };
    styleButton(freezeBtn_);
    styleButton(rollBtn_);
    freezeBtn_.setClickingTogglesState(false);

    freezeBtn_.onClick = [this] { onFreeze(); };
    rollBtn_   .onClick = [this] { onRoll();   };

    addAndMakeVisible(freezeBtn_);
    addAndMakeVisible(rollBtn_);

    startTimerHz(15);
}

ActionsView::~ActionsView() { stopTimer(); }

void ActionsView::resized()
{
    const int btnW = 80;
    const int btnH = theme::rowHeight;
    const int gap = 8;
    const int totalW = btnW * 2 + gap;
    int x = (getWidth() - totalW) / 2;
    const int y = (getHeight() - btnH) / 2;
    freezeBtn_.setBounds(x, y, btnW, btnH);
    x += btnW + gap;
    rollBtn_.setBounds(x, y, btnW, btnH);
}

void ActionsView::timerCallback()
{
    // Track external lock changes (DAW automation, preset recall) so the
    // FREEZE button's frozen / not-frozen visual stays in sync without
    // requiring us to subscribe to APVTS::Listener (which fires on the
    // message thread anyway).
    const float lock = *processor_.getApvts().getRawParameterValue(plugin::pid::lock);
    if (lock != lastDrawnLock_) {
        const bool frozen = (lock == 1.0f);
        freezeBtn_.setToggleState(frozen, juce::dontSendNotification);
        // textColourOnId is `blue` for the frozen state; toggling drives
        // the visual change without a custom paint override.
        lastDrawnLock_ = lock;
    }
}

void ActionsView::onFreeze()
{
    const float current = *processor_.getApvts().getRawParameterValue(plugin::pid::lock);
    const auto result = RingLogic::freezeAction(current, prevLock_);
    prevLock_ = result.newPrevLock;
    if (auto* p = processor_.getApvts().getParameter(plugin::pid::lock)) {
        p->setValueNotifyingHost(p->convertTo0to1(result.newLock));
    }
}

void ActionsView::onRoll()
{
    auto rng = engine::seedRng(static_cast<uint64_t>(juce::Time::getMillisecondCounter()));
    const int newSeed = RingLogic::rollAction(rng);
    if (auto* p = processor_.getApvts().getParameter(plugin::pid::seed)) {
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newSeed)));
    }
    // Pressing ROLL clears the FREEZE stash so the next FREEZE press
    // captures the lock value as of the new loop, not the pre-roll one.
    prevLock_.reset();
}

}  // namespace editor
}  // namespace stencil
