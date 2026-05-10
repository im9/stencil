#include "Editor/RightRailView.h"

#include <algorithm>

#include "Editor/RingLogic.h"
#include "Editor/Theme.h"
#include "Engine/Rng.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

namespace
{
void styleSlider(juce::Slider& s)
{
    s.setColour(juce::Slider::trackColourId,         theme::oliveAlpha(0.6f));
    s.setColour(juce::Slider::backgroundColourId,    theme::lzBorder);
    s.setColour(juce::Slider::thumbColourId,         theme::olive);
    s.setColour(juce::Slider::textBoxTextColourId,   theme::fg);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, theme::bg);
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, theme::rowHeight);
}

void stylePill(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId,     theme::bg);
    b.setColour(juce::TextButton::buttonOnColourId,   theme::oliveBg);
    b.setColour(juce::TextButton::textColourOffId,    theme::fgAlpha(0.5f));
    b.setColour(juce::TextButton::textColourOnId,     theme::olive);
    b.setClickingTogglesState(false);
}
}  // namespace

// Tiny inner Timer to poll choice-parameter values for pill-radio sync.
// Kept private to the RightRailView translation unit — no ownership
// shared with anything outside this view.
class RightRailView::PillSync : public juce::Timer
{
public:
    explicit PillSync(RightRailView& v) : view_(v) { startTimerHz(15); }
    void timerCallback() override { view_.refreshPills(); }
private:
    RightRailView& view_;
};

RightRailView::RightRailView(plugin::StencilProcessor& p)
    : processor_(p)
{
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);

    // ── Parameters ────────────────────────────────────────────────
    for (auto* s : { &lengthSlider_, &lockSlider_, &densitySlider_ })
        styleSlider(*s);
    lengthSlider_.textFromValueFunction  = [](double v){ return juce::String((int) v); };
    lockSlider_.textFromValueFunction    = [](double v){ return juce::String(v, 2); };
    densitySlider_.textFromValueFunction = [](double v){ return juce::String(v, 2); };

    lengthAtt_  = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::length,  lengthSlider_);
    lockAtt_    = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::lock,    lockSlider_);
    densityAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::density, densitySlider_);
    for (auto* s : { &lengthSlider_, &lockSlider_, &densitySlider_ }) {
        s->updateText();  // refresh after attachment overrode textFromValueFunction
        content_.addAndMakeVisible(*s);
    }

    // ── Mode pills ────────────────────────────────────────────────
    for (int i = 0; i < (int) modePills_.size(); ++i) {
        auto& b = modePills_[(std::size_t) i];
        stylePill(b);
        b.onClick = [this, i] { writeChoice(plugin::pid::mode, i); };
        content_.addAndMakeVisible(b);
    }

    // ── Output ────────────────────────────────────────────────────
    for (auto* s : { &rangeLoSlider_, &rangeHiSlider_,
                     &outputVelocitySlider_, &outputGateSlider_, &outputChannelSlider_ })
        styleSlider(*s);
    rangeLoSlider_.textFromValueFunction        = [](double v){ return juce::String((int) v); };
    rangeHiSlider_.textFromValueFunction        = [](double v){ return juce::String((int) v); };
    outputVelocitySlider_.textFromValueFunction = [](double v){ return juce::String((int) v); };
    outputGateSlider_.textFromValueFunction     = [](double v){ return juce::String(v, 2); };
    outputChannelSlider_.textFromValueFunction  = [](double v){ return juce::String((int) v); };

    rangeLoAtt_       = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::rangeLo,        rangeLoSlider_);
    rangeHiAtt_       = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::rangeHi,        rangeHiSlider_);
    outputVelocityAtt_= std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputVelocity, outputVelocitySlider_);
    outputGateAtt_    = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputGate,     outputGateSlider_);
    outputChannelAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputChannel,  outputChannelSlider_);
    for (auto* s : { &rangeLoSlider_, &rangeHiSlider_,
                     &outputVelocitySlider_, &outputGateSlider_, &outputChannelSlider_ }) {
        s->updateText();
        content_.addAndMakeVisible(*s);
    }

    // Subdivision combo populated from the same StringArray APVTS uses,
    // so the indices match the engine enum directly.
    for (int i = 0; i < plugin::subdivisionChoices.size(); ++i)
        subdivisionCombo_.addItem(plugin::subdivisionChoices[i], i + 1);
    subdivisionCombo_.setColour(juce::ComboBox::backgroundColourId, theme::bg);
    subdivisionCombo_.setColour(juce::ComboBox::textColourId, theme::fg);
    subdivisionCombo_.setColour(juce::ComboBox::outlineColourId, theme::lzBorderMid);
    subdivisionAtt_ = std::make_unique<ComboBoxAttachment>(processor_.getApvts(), plugin::pid::subdivision, subdivisionCombo_);
    content_.addAndMakeVisible(subdivisionCombo_);

    // ── Trigger ───────────────────────────────────────────────────
    for (int i = 0; i < (int) triggerPills_.size(); ++i) {
        auto& b = triggerPills_[(std::size_t) i];
        stylePill(b);
        b.onClick = [this, i] { writeChoice(plugin::pid::triggerMode, i); };
        content_.addAndMakeVisible(b);
    }
    styleSlider(inputChannelSlider_);
    inputChannelSlider_.textFromValueFunction = [](double v){
        const int n = (int) v;
        return n == 0 ? juce::String("OMNI") : juce::String(n);
    };
    inputChannelAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::inputChannel, inputChannelSlider_);
    inputChannelSlider_.updateText();
    content_.addAndMakeVisible(inputChannelSlider_);

    // ── Reproducibility ───────────────────────────────────────────
    styleSlider(seedSlider_);
    seedSlider_.textFromValueFunction = [](double v){ return juce::String((int) v); };
    seedAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::seed, seedSlider_);
    seedSlider_.updateText();
    content_.addAndMakeVisible(seedSlider_);

    rollBtn_.setColour(juce::TextButton::buttonColourId, theme::bg);
    rollBtn_.setColour(juce::TextButton::textColourOffId, theme::olive);
    rollBtn_.onClick = [this] {
        // juce::Random advances state per call so two presses in the
        // same millisecond still produce different seeds (matches the
        // ring + ActionsView ROLL paths).
        const int newSeed = juce::Random::getSystemRandom().nextInt(0x7FFFFFFF);
        if (auto* p = processor_.getApvts().getParameter(plugin::pid::seed))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newSeed)));
    };
    content_.addAndMakeVisible(rollBtn_);

    pillSync_ = std::make_unique<PillSync>(*this);
    refreshPills();
}

RightRailView::~RightRailView() = default;

void RightRailView::writeChoice(const char* paramId, int idx)
{
    auto* p = processor_.getApvts().getParameter(paramId);
    if (p == nullptr) return;
    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(idx)));
}

void RightRailView::refreshPills()
{
    const int modeIdx = (int) *processor_.getApvts().getRawParameterValue(plugin::pid::mode);
    for (int i = 0; i < (int) modePills_.size(); ++i)
        modePills_[(std::size_t) i].setToggleState(i == modeIdx, juce::dontSendNotification);

    const int trigIdx = (int) *processor_.getApvts().getRawParameterValue(plugin::pid::triggerMode);
    for (int i = 0; i < (int) triggerPills_.size(); ++i)
        triggerPills_[(std::size_t) i].setToggleState(i == trigIdx, juce::dontSendNotification);
}

void RightRailView::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);
    g.setColour(theme::lzBorder);
    g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);
}

void RightRailView::resized()
{
    viewport_.setBounds(getLocalBounds());
    const int contentW = std::max(0, viewport_.getWidth() - viewport_.getScrollBarThickness());
    const int pad = theme::railPad;
    const int rh = theme::rowHeight;
    const int gap = theme::rowGap;
    const int legendH = static_cast<int>(theme::fsSm) + 4;

    // Per-fieldset heights (legend + N rows + frame padding × 2).
    paramsHeight_  = legendH + rh * 3 + gap * 2 + theme::groupPadY * 2;
    modeHeight_    = legendH + rh * 1 + theme::groupPadY * 2;
    outputHeight_  = legendH + rh * 5 + gap * 4 + theme::groupPadY * 2;
    triggerHeight_ = legendH + rh * 2 + gap * 1 + theme::groupPadY * 2;
    reproHeight_   = legendH + rh * 1 + theme::groupPadY * 2;

    int y = pad;
    auto innerX = [&]{ return pad + theme::groupPadX; };
    auto innerW = [&]{ return contentW - pad * 2 - theme::groupPadX * 2; };

    auto rowY = [&](int rowIdx, int frameY) {
        return frameY + theme::groupPadY + legendH + rowIdx * (rh + gap);
    };

    // ── Parameters ────────────────────────────────────────────────
    {
        const int frameY = y;
        lengthSlider_ .setBounds(innerX(), rowY(0, frameY), innerW(), rh);
        lockSlider_   .setBounds(innerX(), rowY(1, frameY), innerW(), rh);
        densitySlider_.setBounds(innerX(), rowY(2, frameY), innerW(), rh);
        y += paramsHeight_ + theme::groupGap;
    }

    // ── Mode ──────────────────────────────────────────────────────
    {
        const int frameY = y;
        const int row = rowY(0, frameY);
        const int pillW = (innerW() - 2 * 4) / 3;
        int x = innerX();
        for (auto& b : modePills_) { b.setBounds(x, row, pillW, rh); x += pillW + 4; }
        y += modeHeight_ + theme::groupGap;
    }

    // ── Output (RANGE-LO, RANGE-HI, VEL, GATE, SUBDIV) ────────────
    {
        const int frameY = y;
        rangeLoSlider_       .setBounds(innerX(), rowY(0, frameY), innerW(), rh);
        rangeHiSlider_       .setBounds(innerX(), rowY(1, frameY), innerW(), rh);
        outputVelocitySlider_.setBounds(innerX(), rowY(2, frameY), innerW(), rh);
        outputGateSlider_    .setBounds(innerX(), rowY(3, frameY), innerW(), rh);
        subdivisionCombo_    .setBounds(innerX(), rowY(4, frameY), innerW(), rh);
        y += outputHeight_ + theme::groupGap;
    }

    // ── Trigger ───────────────────────────────────────────────────
    {
        const int frameY = y;
        const int row0 = rowY(0, frameY);
        const int pillW = (innerW() - 2 * 4) / 3;
        int x = innerX();
        for (auto& b : triggerPills_) { b.setBounds(x, row0, pillW, rh); x += pillW + 4; }
        inputChannelSlider_.setBounds(innerX(), rowY(1, frameY), innerW(), rh);
        // OUT CH would push the fieldset to 3 rows; deferred to the next
        // editor pass — concept.md treats outputChannel as "MIDI routing
        // specifics" and the right rail already covers the canonical 13.
        // (Slider is still attached and persists via APVTS; just not
        // surfaced here yet.)
        y += triggerHeight_ + theme::groupGap;
    }

    // ── Reproducibility ───────────────────────────────────────────
    {
        const int frameY = y;
        const int rollW = 32;
        seedSlider_.setBounds(innerX(), rowY(0, frameY), innerW() - rollW - 4, rh);
        rollBtn_   .setBounds(innerX() + innerW() - rollW, rowY(0, frameY), rollW, rh);
        y += reproHeight_;
    }

    // outputChannel slider lives off-screen for now; surfaced via APVTS
    // automation only. Stash at zero size so the SliderAttachment stays
    // bound and persistence still works through getStateInformation.
    outputChannelSlider_.setBounds(0, 0, 0, 0);

    content_.setSize(contentW, std::max(y + pad, viewport_.getHeight()));
}

}  // namespace editor
}  // namespace stencil
