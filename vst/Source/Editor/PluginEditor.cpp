#include "Editor/PluginEditor.h"

#include "Editor/Theme.h"

namespace stencil {
namespace editor {

StencilEditor::StencilEditor(plugin::StencilProcessor& p)
    : AudioProcessorEditor(&p),
      processor_(p),
      actions_(p),
      ring_(p),
      rail_(p),
      history_(p)
{
    addAndMakeVisible(actions_);
    addAndMakeVisible(ring_);
    addAndMakeVisible(rail_);
    addAndMakeVisible(history_);

    setResizable(true, true);
    setResizeLimits(640, 360, 1800, 1200);
    // 820 × 540 is the inboil TuringSheet dialog footprint scaled to
    // the right-rail's 280 px width budget. The body becomes:
    //   ring 540 px wide × 364 px tall, rail 280 × 364, header 820 × 40,
    //   history 820 × 136 — matches the ADR 007 §Layout sketch.
    setSize(820, 540);
}

void StencilEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::bg);

    // Header row — title + 1 px divider, mirroring inboil .t-header.
    // Plugin window has no × button: the host owns close in VST3 / AU /
    // CLAP, and an in-editor × is either a no-op or fights the host.
    // The plugin name itself is the host's responsibility (track header,
    // window chrome) — we surface only the panel identity, matching
    // inboil's TuringSheet header verbatim.
    g.setColour(theme::fg);
    g.setFont(theme::dataFont(theme::fsXl, true));
    g.drawText("Stencil",
               theme::railPad, 0,
               getWidth() - theme::railPad * 2, theme::headerHeight,
               juce::Justification::centredLeft);

    g.setColour(theme::lzBorder);
    g.drawLine(0.0f, (float) theme::headerHeight,
               (float) getWidth(), (float) theme::headerHeight, 1.0f);
}

void StencilEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(theme::headerHeight);

    auto historyArea = bounds.removeFromBottom(theme::historyHeight);
    history_.setBounds(historyArea);

    auto railArea = bounds.removeFromRight(theme::railWidth);
    rail_.setBounds(railArea);

    // Body — actions row sits above the ring, both flex with the
    // remaining width. ActionsView height is the row height + small
    // padding; ring takes the rest.
    auto actionsArea = bounds.removeFromTop(theme::ringActionsBarHeight);
    actions_.setBounds(actionsArea);
    ring_.setBounds(bounds);
}

}  // namespace editor
}  // namespace stencil
