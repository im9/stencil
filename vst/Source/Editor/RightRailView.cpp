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
// Layout column widths inside a fieldset's inner area. paint() and resized()
// both compute row geometry from these constants; keep them in lockstep.
constexpr int kLabelColW = 40;
constexpr int kValueColW = 56;
constexpr int kSeedValueColW = 84;   // seeds can be up to 10 decimal digits
constexpr int kRollBtnW = 28;
constexpr int kColGap = 4;

void styleSlider(juce::Slider& s)
{
    s.setColour(juce::Slider::trackColourId,         theme::oliveAlpha(0.6f));
    s.setColour(juce::Slider::backgroundColourId,    theme::lzBorder);
    s.setColour(juce::Slider::thumbColourId,         theme::olive);
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    // No text box — the right-rail's paint() draws every label and value
    // itself so the row can flow as [LABEL] [slider] [VALUE] with type
    // and palette tokens that match inboil. The slider's built-in text
    // box also misformats float values when SliderAttachment binds (the
    // attachment's parameter formatter overrides any textFromValueFunction
    // we set, producing "0.500..." for a 0.50 lock value).
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
}

void styleCombo(juce::ComboBox& c)
{
    c.setColour(juce::ComboBox::backgroundColourId, theme::bg);
    c.setColour(juce::ComboBox::textColourId,       theme::fg);
    c.setColour(juce::ComboBox::outlineColourId,    theme::lzBorderMid);
    c.setColour(juce::ComboBox::arrowColourId,      theme::fg);
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

// Tiny inner Timer to poll choice-parameter values for pill-radio sync
// AND repaint the content layer so APVTS value changes (slider drag,
// host automation, preset recall) propagate into the manually-rendered
// row values. Kept private to the RightRailView translation unit — no
// ownership shared with anything outside this view.
class RightRailView::PillSync : public juce::Timer
{
public:
    explicit PillSync(RightRailView& v) : view_(v) { startTimerHz(15); }
    void timerCallback() override
    {
        view_.refreshPills();
        view_.content_.repaint();
    }
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

    lengthAtt_  = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::length,  lengthSlider_);
    lockAtt_    = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::lock,    lockSlider_);
    densityAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::density, densitySlider_);
    for (auto* s : { &lengthSlider_, &lockSlider_, &densitySlider_ })
        content_.addAndMakeVisible(*s);

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

    rangeLoAtt_       = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::rangeLo,        rangeLoSlider_);
    rangeHiAtt_       = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::rangeHi,        rangeHiSlider_);
    outputVelocityAtt_= std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputVelocity, outputVelocitySlider_);
    outputGateAtt_    = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputGate,     outputGateSlider_);
    outputChannelAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::outputChannel,  outputChannelSlider_);
    for (auto* s : { &rangeLoSlider_, &rangeHiSlider_,
                     &outputVelocitySlider_, &outputGateSlider_, &outputChannelSlider_ })
        content_.addAndMakeVisible(*s);

    // Subdivision combo populated from the same StringArray APVTS uses,
    // so the indices match the engine enum directly.
    for (int i = 0; i < plugin::subdivisionChoices.size(); ++i)
        subdivisionCombo_.addItem(plugin::subdivisionChoices[i], i + 1);
    styleCombo(subdivisionCombo_);
    subdivisionAtt_ = std::make_unique<ComboBoxAttachment>(processor_.getApvts(), plugin::pid::subdivision, subdivisionCombo_);
    content_.addAndMakeVisible(subdivisionCombo_);

    // ── Trigger ───────────────────────────────────────────────────
    for (int i = 0; i < (int) triggerPills_.size(); ++i) {
        auto& b = triggerPills_[(std::size_t) i];
        stylePill(b);
        b.onClick = [this, i] { writeChoice(plugin::pid::triggerMode, i); };
        content_.addAndMakeVisible(b);
    }
    // Input channel as a 17-item dropdown (OMNI / 1..16). Item index
    // 0..16 maps directly to the AudioParameterInt value via
    // ComboBoxAttachment, so the param range (0..16) is unchanged.
    inputChannelCombo_.addItem("OMNI", 1);
    for (int ch = 1; ch <= 16; ++ch)
        inputChannelCombo_.addItem(juce::String(ch), ch + 1);
    styleCombo(inputChannelCombo_);
    inputChannelAtt_ = std::make_unique<ComboBoxAttachment>(processor_.getApvts(), plugin::pid::inputChannel, inputChannelCombo_);
    content_.addAndMakeVisible(inputChannelCombo_);

    // ── Reproducibility ───────────────────────────────────────────
    styleSlider(seedSlider_);
    seedAtt_ = std::make_unique<SliderAttachment>(processor_.getApvts(), plugin::pid::seed, seedSlider_);
    content_.addAndMakeVisible(seedSlider_);

    // Reroll glyph U+21BB; constructed through fromUTF8 to dodge
    // juce::String(const char*) decode hazards (see RightRailView.h).
    rollBtn_.setButtonText(juce::String::fromUTF8("\xE2\x86\xBB"));
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

// ── Paint helpers ────────────────────────────────────────────────
namespace
{
// Rounded rectangle frame + legend notch for a fieldset, mirroring
// inboil's <fieldset><legend> pair. Legend sits straddling the frame's
// top edge with a bg fill behind it so the border appears to dip
// under the text.
void drawFieldsetFrame(juce::Graphics& g, juce::Rectangle<int> frame,
                       const juce::String& legend)
{
    g.setColour(theme::lzBorder);
    g.drawRoundedRectangle(frame.toFloat().reduced(0.5f), 2.0f, 1.0f);

    g.setFont(theme::dataFont(theme::fsSm, true));
    const int legendW = g.getCurrentFont().getStringWidth(legend) + 8;
    const juce::Rectangle<int> legendBox{
        frame.getX() + 8, frame.getY() - static_cast<int>(theme::fsSm) / 2 - 1,
        legendW, static_cast<int>(theme::fsSm) + 2 };
    g.setColour(theme::bg);
    g.fillRect(legendBox);
    g.setColour(theme::fgAlpha(0.4f));
    g.drawText(legend, legendBox, juce::Justification::centred);
}

// Row label on the left side of a control row (LEN / LOCK / DENS / ...).
void drawRowLabel(juce::Graphics& g, int x, int y, int rh,
                  const juce::String& text)
{
    g.setColour(theme::fgAlpha(0.6f));
    g.setFont(theme::dataFont(theme::fsMd, true));
    g.drawText(text, x, y, kLabelColW, rh, juce::Justification::centredLeft);
}

// Right-aligned value text (slider value rendered manually since the
// slider's textBox is disabled). Optional suffix is rendered in the
// same row (e.g. " bits" after LEN's value).
void drawRowValue(juce::Graphics& g, int x, int y, int w, int rh,
                  const juce::String& value, const juce::String& suffix = {})
{
    g.setColour(theme::fg);
    g.setFont(theme::dataFont(theme::fsLg, true));
    if (suffix.isEmpty()) {
        g.drawText(value, x, y, w, rh, juce::Justification::centredRight);
    } else {
        const auto font = g.getCurrentFont();
        const int suffixW = font.getStringWidth(suffix);
        g.drawText(value, x, y, w - suffixW, rh, juce::Justification::centredRight);
        g.setColour(theme::fgAlpha(0.4f));
        g.setFont(theme::dataFont(theme::fsSm, false));
        g.drawText(suffix, x + w - suffixW, y, suffixW, rh, juce::Justification::centredRight);
    }
}

juce::String fmtInt(const juce::AudioProcessorValueTreeState& apvts, const char* pid)
{
    return juce::String(static_cast<int>(*apvts.getRawParameterValue(pid)));
}

juce::String fmtFloat2(const juce::AudioProcessorValueTreeState& apvts, const char* pid)
{
    return juce::String(static_cast<double>(*apvts.getRawParameterValue(pid)), 2);
}
}  // namespace

void RightRailView::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);
    g.setColour(theme::lzBorder);
    g.drawLine(0.0f, 0.0f, 0.0f, static_cast<float>(getHeight()), 1.0f);
}

void RightRailView::Content::paint(juce::Graphics& g)
{
    view_.paintContent(g);
}

void RightRailView::paintContent(juce::Graphics& g) const
{
    const auto& a = processor_.getApvts();
    const int innerXv = theme::railPad + theme::groupPadX;
    const int frameX = theme::railPad;
    const int contentW = std::max(0, viewport_.getWidth() - viewport_.getScrollBarThickness());
    const int frameW = contentW - theme::railPad * 2;
    const int innerW = frameW - theme::groupPadX * 2;
    const int rh = theme::rowHeight;
    const int legendH = static_cast<int>(theme::fsSm) + 4;

    auto rowY = [&](int rowIdx, int frameY) {
        return frameY + theme::groupPadY + legendH + rowIdx * (rh + theme::rowGap);
    };

    // 1. Parameters
    {
        const int fy = paramsFrameY_;
        drawFieldsetFrame(g, { frameX, fy, frameW, paramsHeight_ }, "Parameters");
        const int valX = innerXv + innerW - kValueColW;
        drawRowLabel(g, innerXv, rowY(0, fy), rh, "LEN");
        drawRowLabel(g, innerXv, rowY(1, fy), rh, "LOCK");
        drawRowLabel(g, innerXv, rowY(2, fy), rh, "DENS");
        drawRowValue(g, valX, rowY(0, fy), kValueColW, rh, fmtInt(a, plugin::pid::length), " bits");
        drawRowValue(g, valX, rowY(1, fy), kValueColW, rh, fmtFloat2(a, plugin::pid::lock));
        drawRowValue(g, valX, rowY(2, fy), kValueColW, rh, fmtFloat2(a, plugin::pid::density));
    }

    // 2. Mode (legend only — pills are full-width, value column unused).
    // Mode-descriptor line below the pill row maps the active mode to
    // a one-line semantic hint (matches inboil .mode-desc).
    {
        const int fy = modeFrameY_;
        drawFieldsetFrame(g, { frameX, fy, frameW, modeHeight_ }, "Mode");
        const int modeIdx = static_cast<int>(*a.getRawParameterValue(plugin::pid::mode));
        const char* desc = (modeIdx == 0) ? "bits \xE2\x86\x92 pitch"
                         : (modeIdx == 1) ? "bits \xE2\x86\x92 on / off"
                         :                  "bits \xE2\x86\x92 velocity";
        g.setColour(theme::fgAlpha(0.4f));
        g.setFont(theme::dataFont(theme::fsSm, false));
        g.drawText(juce::String::fromUTF8(desc),
                   innerXv, rowY(1, fy), innerW, rh,
                   juce::Justification::centredLeft);
    }

    // 3. Output
    // Row 0 is a "RANGE" sub-heading; rows 1-2 are the LO / HI sliders
    // sharing the same label column as VEL / GATE / SUBDIV below.
    {
        const int fy = outputFrameY_;
        drawFieldsetFrame(g, { frameX, fy, frameW, outputHeight_ }, "Output");
        const int valX = innerXv + innerW - kValueColW;

        g.setColour(theme::fgAlpha(0.45f));
        g.setFont(theme::dataFont(theme::fsSm, true));
        g.drawText("RANGE", innerXv, rowY(0, fy), innerW, rh,
                   juce::Justification::centredLeft);

        drawRowLabel(g, innerXv, rowY(1, fy), rh, "LO");
        drawRowLabel(g, innerXv, rowY(2, fy), rh, "HI");
        drawRowLabel(g, innerXv, rowY(3, fy), rh, "VEL");
        drawRowLabel(g, innerXv, rowY(4, fy), rh, "GATE");
        drawRowLabel(g, innerXv, rowY(5, fy), rh, "SUBDIV");
        drawRowValue(g, valX, rowY(1, fy), kValueColW, rh, fmtInt(a, plugin::pid::rangeLo));
        drawRowValue(g, valX, rowY(2, fy), kValueColW, rh, fmtInt(a, plugin::pid::rangeHi));
        drawRowValue(g, valX, rowY(3, fy), kValueColW, rh, fmtInt(a, plugin::pid::outputVelocity));
        drawRowValue(g, valX, rowY(4, fy), kValueColW, rh, fmtFloat2(a, plugin::pid::outputGate));
        // Row 5 (SUBDIV) — combo box renders its own selected item; no
        // separate value column needed.
    }

    // 4. Trigger
    {
        const int fy = triggerFrameY_;
        drawFieldsetFrame(g, { frameX, fy, frameW, triggerHeight_ }, "Trigger");
        // Row 0 is the AUTO/GATE/SEED pill row (full-width, no label).
        // Row 1's value is rendered by the inputChannelCombo_ itself.
        drawRowLabel(g, innerXv, rowY(1, fy), rh, "IN-CH");
    }

    // 5. Reproducibility
    {
        const int fy = reproFrameY_;
        drawFieldsetFrame(g, { frameX, fy, frameW, reproHeight_ }, "Reproducibility");
        drawRowLabel(g, innerXv, rowY(0, fy), rh, "SEED");
        // Wider value column for big seeds (up to 10 decimal digits).
        const int seedValX = innerXv + innerW - kSeedValueColW - kRollBtnW - kColGap;
        drawRowValue(g, seedValX, rowY(0, fy), kSeedValueColW, rh,
                     fmtInt(a, plugin::pid::seed));
    }
}

void RightRailView::resized()
{
    viewport_.setBounds(getLocalBounds());
    const int contentW = std::max(0, viewport_.getWidth() - viewport_.getScrollBarThickness());
    const int pad = theme::railPad;
    const int rh = theme::rowHeight;
    const int gap = theme::rowGap;
    const int legendH = static_cast<int>(theme::fsSm) + 4;

    // Per-fieldset heights. Mode now has 2 rows (pill row + descriptor)
    // so the layout includes the inboil-parity `bits → pitch` hint.
    // Output gains a "RANGE" sub-heading row above LO / HI, so it's 6
    // rows total (RANGE, LO, HI, VEL, GATE, SUBDIV).
    paramsHeight_  = legendH + rh * 3 + gap * 2 + theme::groupPadY * 2;
    modeHeight_    = legendH + rh * 2 + gap * 1 + theme::groupPadY * 2;
    outputHeight_  = legendH + rh * 6 + gap * 5 + theme::groupPadY * 2;
    triggerHeight_ = legendH + rh * 2 + gap * 1 + theme::groupPadY * 2;
    reproHeight_   = legendH + rh * 1 + theme::groupPadY * 2;

    int y = pad;
    const int innerX = pad + theme::groupPadX;
    const int innerW = contentW - pad * 2 - theme::groupPadX * 2;
    // Standard slider sits between the label column on the left and the
    // value column on the right.
    const int sliderX = innerX + kLabelColW + kColGap;
    const int sliderW = innerW - kLabelColW - kColGap - kValueColW - kColGap;

    auto rowY = [&](int rowIdx, int frameY) {
        return frameY + theme::groupPadY + legendH + rowIdx * (rh + gap);
    };

    // ── Parameters ────────────────────────────────────────────────
    {
        paramsFrameY_ = y;
        lengthSlider_ .setBounds(sliderX, rowY(0, y), sliderW, rh);
        lockSlider_   .setBounds(sliderX, rowY(1, y), sliderW, rh);
        densitySlider_.setBounds(sliderX, rowY(2, y), sliderW, rh);
        y += paramsHeight_ + theme::groupGap;
    }

    // ── Mode ──────────────────────────────────────────────────────
    // Pills span the full innerW (no left label); descriptor occupies
    // row 1 inside paint().
    {
        modeFrameY_ = y;
        const int row = rowY(0, y);
        const int pillW = (innerW - 2 * kColGap) / 3;
        int x = innerX;
        for (auto& b : modePills_) { b.setBounds(x, row, pillW, rh); x += pillW + kColGap; }
        y += modeHeight_ + theme::groupGap;
    }

    // ── Output (RANGE sub-heading, LO, HI, VEL, GATE, SUBDIV) ─────
    {
        outputFrameY_ = y;
        // Row 0 is the "RANGE" sub-heading (paint-only, no slider).
        rangeLoSlider_       .setBounds(sliderX, rowY(1, y), sliderW, rh);
        rangeHiSlider_       .setBounds(sliderX, rowY(2, y), sliderW, rh);
        outputVelocitySlider_.setBounds(sliderX, rowY(3, y), sliderW, rh);
        outputGateSlider_    .setBounds(sliderX, rowY(4, y), sliderW, rh);
        // SUBDIV row uses a combo, so no separate value column — combo
        // takes the slider+value column width as its body.
        subdivisionCombo_    .setBounds(sliderX, rowY(5, y), sliderW + kValueColW + kColGap, rh);
        y += outputHeight_ + theme::groupGap;
    }

    // ── Trigger ───────────────────────────────────────────────────
    {
        triggerFrameY_ = y;
        const int row0 = rowY(0, y);
        const int pillW = (innerW - 2 * kColGap) / 3;
        int x = innerX;
        for (auto& b : triggerPills_) { b.setBounds(x, row0, pillW, rh); x += pillW + kColGap; }
        // Combo takes the slider + value column width (same as SUBDIV).
        inputChannelCombo_.setBounds(sliderX, rowY(1, y), sliderW + kValueColW + kColGap, rh);
        y += triggerHeight_ + theme::groupGap;
    }

    // ── Reproducibility ───────────────────────────────────────────
    // SEED row has a wider value column for large integers, then the
    // reroll glyph button on the far right.
    {
        reproFrameY_ = y;
        const int seedSliderW = innerW - kLabelColW - kColGap - kSeedValueColW - kColGap - kRollBtnW - kColGap;
        seedSlider_.setBounds(sliderX, rowY(0, y), seedSliderW, rh);
        rollBtn_   .setBounds(innerX + innerW - kRollBtnW, rowY(0, y), kRollBtnW, rh);
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
