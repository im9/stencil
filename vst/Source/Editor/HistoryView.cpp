#include "Editor/HistoryView.h"

#include <algorithm>

#include "Editor/Theme.h"
#include "Engine/Rng.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

namespace
{
const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
}

HistoryView::HistoryView(plugin::StencilProcessor& p)
    : processor_(p)
{
    setOpaque(false);
    startTimerHz(15);
}

HistoryView::~HistoryView() { stopTimer(); }

std::vector<HistoryView::StepSnap>
HistoryView::simulate(const engine::SequencerParams& p)
{
    auto rng = engine::seedRng(p.seed);
    engine::RegisterBits reg = engine::createRegister(p.length, rng);
    std::vector<StepSnap> out;
    out.reserve(static_cast<std::size_t>(p.length));
    for (int i = 0; i < p.length; ++i) {
        const auto r = engine::tmStep(reg, p.length, p.lock, p.density,
                                      p.rangeLo, p.rangeHi, rng);
        out.push_back({ r.reg, r.note, r.active });
        reg = r.reg;
    }
    return out;
}

void HistoryView::paint(juce::Graphics& g)
{
    g.setColour(theme::lzBorder);
    g.drawLine(0.0f, 0.5f, static_cast<float>(getWidth()), 0.5f, 1.0f);

    const auto params = plugin::readParams(processor_.getApvts());
    const auto snaps = simulate(params);
    if (snaps.empty()) return;

    const int barW = theme::historyBarWidth;
    const int barGap = theme::historyBarGap;
    const int barMaxH = theme::historyBarMaxHeight;
    const int totalW = static_cast<int>(snaps.size()) * (barW + barGap);

    // Center the bar series horizontally so 8-step loops don't feel
    // left-anchored at the editor's default size; this also matches
    // inboil's overflow-x:auto centering for narrow histories.
    const int startX = std::max(theme::railPad, (getWidth() - totalW) / 2);
    const int baseY = (getHeight() - barMaxH) / 2;

    const int playStep = (params.length > 0)
        ? (processor_.getCumulativeSteps() % params.length)
        : -1;

    g.setFont(theme::dataFont(theme::fsSm, false));
    for (int i = 0; i < static_cast<int>(snaps.size()); ++i) {
        const auto& s = snaps[(std::size_t) i];
        const auto frac = engine::registerToFraction(s.reg, params.length);
        const float fracValue = (frac.den == 0) ? 0.0f
            : static_cast<float>(frac.num) / static_cast<float>(frac.den);
        const int h = s.active ? std::max(3, static_cast<int>(fracValue * barMaxH)) : 3;

        const int x = startX + i * (barW + barGap);
        const int y = baseY + (barMaxH - h);
        juce::Rectangle<float> bar(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(barW), static_cast<float>(h));

        // Color hierarchy mirrors inboil .hist-bar:
        //   playing > active > rest. The "playing" cell paints as cream
        //   on a dark navy outline so it pops over the olive active bars.
        if (i == playStep && processor_.getCumulativeSteps() > 0) {
            g.setColour(theme::bg);
            g.fillRoundedRectangle(bar, 2.0f);
            g.setColour(theme::fg);
            g.drawRoundedRectangle(bar, 2.0f, 0.5f);
        } else if (s.active) {
            g.setColour(theme::oliveAlpha(0.7f));
            g.fillRoundedRectangle(bar, 2.0f);
        } else {
            g.setColour(theme::fgAlpha(0.08f));
            g.fillRoundedRectangle(bar, 2.0f);
        }

        g.setColour(theme::fgAlpha(0.4f));
        const juce::String label = s.active
            ? juce::String(kNoteNames[((s.note % 12) + 12) % 12])
            : juce::String::fromUTF8("\xC2\xB7");  // middle dot for rest
        g.drawText(label,
                   x, baseY + barMaxH + 4,
                   barW, 12,
                   juce::Justification::centred);
    }
}

void HistoryView::timerCallback()
{
    // Repaint when any sim-affecting param or playhead changed. Cheap
    // diff against the same APVTS read the next paint would do anyway.
    const int steps = processor_.getCumulativeSteps();
    const auto params = plugin::readParams(processor_.getApvts());
    bool dirty = false;
    if (steps != lastSteps_)         { lastSteps_ = steps; dirty = true; }
    if (params.seed != lastSeed_)    { lastSeed_ = params.seed; dirty = true; }
    if (params.length != lastLength_){ lastLength_ = params.length; dirty = true; }
    if (params.lock != lastLock_)    { lastLock_ = static_cast<float>(params.lock); dirty = true; }
    if (params.density != lastDensity_){ lastDensity_ = static_cast<float>(params.density); dirty = true; }
    if (params.rangeLo != lastRangeLo_){ lastRangeLo_ = params.rangeLo; dirty = true; }
    if (params.rangeHi != lastRangeHi_){ lastRangeHi_ = params.rangeHi; dirty = true; }
    if (dirty) repaint();
}

}  // namespace editor
}  // namespace stencil
