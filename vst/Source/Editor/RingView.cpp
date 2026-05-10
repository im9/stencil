#include "Editor/RingView.h"

#include <algorithm>
#include <cmath>

#include "Editor/RingLogic.h"
#include "Editor/Theme.h"
#include "Engine/Turing.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

namespace
{
const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

juce::String noteLabel(int midiNote)
{
    const int n = std::clamp(midiNote, 0, 127);
    return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 1);
}

// Read length straight from APVTS — single source of truth for "how many
// bits to draw." The atomic register snapshot may briefly contain a value
// computed under a different `length` if the user just resized; masking
// with the current length keeps the visual coherent in that window.
int currentLength(const juce::AudioProcessorValueTreeState& apvts)
{
    return std::clamp(static_cast<int>(*apvts.getRawParameterValue(plugin::pid::length)), 2, 32);
}
}  // namespace

RingView::RingView(plugin::StencilProcessor& p)
    : processor_(p)
{
    setOpaque(false);  // body fill comes from the editor's bg paint
    // 15 Hz matches oedipa's LatticeView. The ring's two animated paths
    // (rotation + head highlight) update on subdivision boundaries; 15 Hz
    // is well above the human "stutter" threshold for a quarter-note grid
    // without paying the resize-flicker cost of 60 Hz.
    startTimerHz(15);
}

RingView::~RingView() { stopTimer(); }

RingView::Geometry RingView::currentGeometry() const
{
    const float w = static_cast<float>(getWidth());
    const float h = static_cast<float>(getHeight() - theme::ringActionsBarHeight);
    const float side = std::max(0.0f, std::min(w, h)) - 2.0f * theme::ringMargin;
    const float radius = std::max(8.0f, side * 0.5f);

    const int len = currentLength(processor_.getApvts());
    // Match inboil's bitR cap: floor at theme::bitRadiusMaxPx; otherwise
    // use the arc length between adjacent bits minus 2 px padding so big
    // length=32 doesn't overlap. Floor at bitRadiusMinPx so length=32
    // still has clickable targets.
    const float arcSpace = (3.14159265358979323846f * radius) / std::max(len, 4);
    const float bitR = std::clamp(arcSpace - 2.0f,
                                  theme::bitRadiusMinPx,
                                  theme::bitRadiusMaxPx);

    const float cx = static_cast<float>(getWidth()) * 0.5f;
    const float cy = static_cast<float>(theme::ringActionsBarHeight)
                   + static_cast<float>(getHeight() - theme::ringActionsBarHeight) * 0.5f;
    return Geometry{ radius, bitR, cx, cy };
}

void RingView::paint(juce::Graphics& g)
{
    const auto geo = currentGeometry();
    const int len = currentLength(processor_.getApvts());
    const auto reg = processor_.getRegisterSnapshot();
    const int steps = processor_.getCumulativeSteps();
    const int reading = RingLogic::readingIndex(steps, len);
    const float rotation = RingLogic::rotationDegrees(steps, len);

    // 1) Faint guide circle (inboil .turing-sheet svg <circle> at opacity 0.15).
    g.setColour(theme::fgAlpha(0.15f));
    g.drawEllipse(geo.cx - geo.radius, geo.cy - geo.radius,
                  geo.radius * 2.0f, geo.radius * 2.0f, 0.5f);

    // 2) Read-head pointer (small triangle pointing into the ring from above
    // the top bit). Sits in screen space (un-rotated) so it always reads as
    // "the head" — the ring rotates underneath it.
    {
        juce::Path tri;
        const float tipY = geo.cy - geo.radius - geo.bitRadius - 10.0f;
        const float baseY = geo.cy - geo.radius - geo.bitRadius - 5.0f;
        tri.addTriangle(geo.cx,         tipY,
                        geo.cx - 3.0f,  baseY,
                        geo.cx + 3.0f,  baseY);
        g.setColour(theme::fgAlpha(0.30f));
        g.fillPath(tri);
    }

    // 3) Bit circles. Apply revolver rotation around the ring center, then
    // draw each bit at its un-rotated logical position. The transform
    // composes the two so the bit at logical index 0 ends up wherever
    // the rotation places it — matching inboil's <g transform="rotate(...)">.
    const auto centerPt = Point2{ geo.cx, geo.cy };
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(juce::degreesToRadians(rotation),
                                                   geo.cx, geo.cy));
    for (int i = 0; i < len; ++i) {
        const auto p = RingLogic::bitPosition(i, len, geo.radius, centerPt);
        const bool bitOn = ((reg >> i) & 1u) != 0u;
        const bool isReading = (i == reading);

        juce::Rectangle<float> circle(p.x - geo.bitRadius, p.y - geo.bitRadius,
                                      geo.bitRadius * 2.0f, geo.bitRadius * 2.0f);

        if (isReading) {
            // Reading-head highlight: open circle with a heavy olive stroke.
            // Mirrors inboil .bit-reading: bg fill, olive stroke 2.5.
            g.setColour(theme::bg);
            g.fillEllipse(circle);
            g.setColour(theme::olive);
            g.drawEllipse(circle, 2.5f);
        } else if (bitOn) {
            g.setColour(theme::olive);
            g.fillEllipse(circle);
        } else {
            g.setColour(theme::bg);
            g.fillEllipse(circle);
            g.setColour(theme::oliveAlpha(0.35f));
            g.drawEllipse(circle, 1.5f);
        }
    }
    g.restoreState();

    // 4) Center text — fraction value above, note name below. Computed
    // from the same atomic snapshot so the two read coherently.
    const auto frac = engine::registerToFraction(reg, len);
    const float fracValue = (frac.den == 0) ? 0.0f
        : static_cast<float>(frac.num) / static_cast<float>(frac.den);

    g.setColour(theme::fg);
    g.setFont(theme::dataFont(theme::fsXxl, true));
    g.drawText(juce::String(fracValue, 2),
               static_cast<int>(geo.cx - 60.0f),
               static_cast<int>(geo.cy - 28.0f),
               120, 26,
               juce::Justification::centred);

    g.setColour(theme::olive);
    g.setFont(theme::dataFont(theme::fsXl, true));
    g.drawText(noteLabel(processor_.getLastNote()),
               static_cast<int>(geo.cx - 60.0f),
               static_cast<int>(geo.cy + 2.0f),
               120, 18,
               juce::Justification::centred);
}

void RingView::mouseDown(const juce::MouseEvent& e)
{
    const auto geo = currentGeometry();
    const int len = currentLength(processor_.getApvts());

    // Hit-test runs in un-rotated space: the user's click in component
    // coordinates first has to be transformed back through the same
    // rotation the paint loop applied. Otherwise clicking the visually
    // top bit would resolve to whichever logical index happens to be
    // there, not the bit-0 the user is aiming at.
    const float steps = static_cast<float>(processor_.getCumulativeSteps());
    const auto inverse = juce::AffineTransform::rotation(
        juce::degreesToRadians(RingLogic::rotationDegrees(static_cast<int>(steps), len)),
        geo.cx, geo.cy).inverted();
    juce::Point<float> click = e.position;
    click.applyTransform(inverse);

    const int idx = RingLogic::hitTest(Point2{ click.x, click.y },
                                       len, geo.radius, geo.bitRadius,
                                       Point2{ geo.cx, geo.cy });
    if (idx < 0) return;

    // ADR 007 §FREEZE / ROLL semantics: bit-click → ROLL. Use a
    // millisecond-counter-seeded rng so successive clicks get different
    // seeds without a persistent member rng. Quality is irrelevant here:
    // the user just wants "a different loop."
    auto rng = engine::seedRng(static_cast<uint64_t>(juce::Time::getMillisecondCounter()));
    const int newSeed = RingLogic::rollAction(rng);
    if (auto* p = processor_.getApvts().getParameter(plugin::pid::seed)) {
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newSeed)));
    }
}

void RingView::timerCallback()
{
    // Repaint only on visible-state change — register snapshot or step
    // count. Editing a non-visible param (e.g. outputChannel) has no
    // effect on the ring, so unconditional repaint would burn cycles
    // mid-resize for no reason.
    const auto reg = processor_.getRegisterSnapshot();
    const int steps = processor_.getCumulativeSteps();
    if (reg != lastDrawnRegister_ || steps != lastDrawnSteps_) {
        lastDrawnRegister_ = reg;
        lastDrawnSteps_ = steps;
        repaint();
    }
}

}  // namespace editor
}  // namespace stencil
