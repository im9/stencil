// Right-rail fieldset stack (ADR 007 §Editor layout). 280 px fixed
// width; vertical stack of five fieldsets — Parameters, Mode, Output,
// Trigger, Reproducibility — each with APVTS attachments to the
// 13 canonical parameters from concept.md.
//
// Owns its own juce::Viewport so the fieldsets can scroll if the
// fieldset stack outgrows the editor's vertical budget (e.g. when the
// user resizes below the default 540 px height).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

#include "Plugin/PluginProcessor.h"

namespace stencil {
namespace editor {

class RightRailView : public juce::Component
{
public:
    explicit RightRailView(plugin::StencilProcessor&);
    ~RightRailView() override;  // out-of-line: PillSync is forward-declared.

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // Each fieldset is rendered by paint() (legend + frame) and laid out
    // in resized(); child controls are owned directly here. A fieldset
    // is a logical group, not a real Component, so all controls live as
    // direct children of the rail content_.
    plugin::StencilProcessor& processor_;

    // Content delegates paint() back to RightRailView::paintContent so
    // the fieldset frames + legends + row labels scroll with the
    // viewport (they are logically anchored to content_'s coordinate
    // space, not the viewport's).
    class Content : public juce::Component
    {
    public:
        explicit Content(RightRailView& v) : view_(v) {}
        void paint(juce::Graphics& g) override;
    private:
        RightRailView& view_;
    };

    juce::Viewport viewport_;
    Content content_{ *this };

    // ── Parameters ────────────────────────────────────────────────
    juce::Slider lengthSlider_, lockSlider_, densitySlider_;
    std::unique_ptr<SliderAttachment> lengthAtt_, lockAtt_, densityAtt_;

    // ── Mode (radio pills) ────────────────────────────────────────
    std::array<juce::TextButton, 3> modePills_{
        juce::TextButton{ "NOT" }, juce::TextButton{ "GAT" }, juce::TextButton{ "VEL" } };

    // ── Output ────────────────────────────────────────────────────
    juce::Slider rangeLoSlider_, rangeHiSlider_;
    juce::Slider outputVelocitySlider_, outputGateSlider_, outputChannelSlider_;
    juce::ComboBox subdivisionCombo_;
    std::unique_ptr<SliderAttachment> rangeLoAtt_, rangeHiAtt_;
    std::unique_ptr<SliderAttachment> outputVelocityAtt_, outputGateAtt_, outputChannelAtt_;
    std::unique_ptr<ComboBoxAttachment> subdivisionAtt_;

    // ── Trigger (radio pills + input channel) ─────────────────────
    // Full names per ADR 007 §Visual reference layout sketch
    // (`AUTO GATE SEED`); the 3-letter Mode pills (`NOT GAT VEL`) follow
    // inboil's `mode.toUpperCase().slice(0, 3)`, but Trigger has no
    // inboil counterpart so the ADR layout sketch is canonical.
    std::array<juce::TextButton, 3> triggerPills_{
        juce::TextButton{ "AUTO" }, juce::TextButton{ "GATE" }, juce::TextButton{ "SEED" } };
    juce::Slider inputChannelSlider_;
    std::unique_ptr<SliderAttachment> inputChannelAtt_;

    // ── Reproducibility ───────────────────────────────────────────
    juce::Slider seedSlider_;
    std::unique_ptr<SliderAttachment> seedAtt_;
    // Reroll glyph "↻" (U+21BB). Initialised via setButtonText in the
    // cpp ctor through juce::String::fromUTF8 so the byte sequence is
    // unambiguously decoded as UTF-8 — `juce::TextButton{ "\xE2..." }`
    // routes through `juce::String(const char*)` which has hit
    // platform-specific decoding edge cases on macOS Standalone (the
    // bytes rendered as Latin-1 mojibake "[â..." in 2026-05-11 audit).
    juce::TextButton rollBtn_;

    // Pill-radio plumbing: writes the chosen index back to the choice
    // parameter; refreshPills() is driven from the inner PillSync timer
    // (forward-declared below), polling at 15 Hz so external automation
    // and preset recall sync without per-pill APVTS::Listener wiring.
    void writeChoice(const char* paramId, int idx);
    void refreshPills();

    // Frame paint helper called from Content::paint. Const-qualified —
    // it only reads APVTS atomically and writes to the Graphics ctx.
    void paintContent(juce::Graphics&) const;

    // Inboil-style fieldset metric: legend row + N controls × rowHeight.
    int paramsHeight_ = 0;
    int modeHeight_ = 0;
    int outputHeight_ = 0;
    int triggerHeight_ = 0;
    int reproHeight_ = 0;

    // Top-Y of each fieldset within content_'s coordinate space, set by
    // resized() and read by paintContent(). Decoupled so paintContent
    // never has to recompute the layout sequence.
    int paramsFrameY_ = 0;
    int modeFrameY_ = 0;
    int outputFrameY_ = 0;
    int triggerFrameY_ = 0;
    int reproFrameY_ = 0;

    class PillSync;
    std::unique_ptr<PillSync> pillSync_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RightRailView)
};

}  // namespace editor
}  // namespace stencil
