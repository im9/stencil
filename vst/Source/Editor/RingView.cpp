#include "Editor/RingView.h"

#include <algorithm>

#include "Editor/NoteFormat.h"
#include "Editor/RingLogic.h"
#include "Editor/Theme.h"
#include "Engine/Turing.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

namespace
{
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
    // 60 Hz so snapshot changes are picked up promptly even if the host
    // doesn't push us a repaint. timerCallback is snapshot-change-driven;
    // see m4l's registerRing.jsui.js for the same snap-only contract.
    startTimerHz(60);
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
    // Pre-shift register: bit 0 = bit the user is currently hearing,
    // drawn at the top position under the playhead triangle. Single
    // coherent read so reg / note come from the same boundary —
    // per-field accessors could tear across two steps (ADR 007
    // §Audit follow-ups — tuple coherence).
    const auto snap = processor_.readEditorSnapshot();
    const auto reg = snap.reg;

    // 1) Guide circle. Inboil renders this at opacity 0.15, but the inboil
    // reference is a desktop-class window with white-ish background and
    // sits next to other generative content; in the standalone Stencil
    // editor the same alpha looks washed out compared to the oedipa /
    // site reference. Bump to 0.30 so the ring reads as an actual
    // geometric construct rather than a near-invisible hint.
    g.setColour(theme::fgAlpha(0.30f));
    g.drawEllipse(geo.cx - geo.radius, geo.cy - geo.radius,
                  geo.radius * 2.0f, geo.radius * 2.0f, 0.5f);

    // 2) Read-head pointer (small triangle pointing INTO the ring from above
    // the top bit). Sits in screen space (un-rotated) so it always reads as
    // "the head". Tip is closer to the ring than the base so the triangle
    // visually points down at the bit it marks.
    {
        juce::Path tri;
        const float tipY  = geo.cy - geo.radius - geo.bitRadius - 5.0f;
        const float baseY = geo.cy - geo.radius - geo.bitRadius - 10.0f;
        tri.addTriangle(geo.cx,         tipY,
                        geo.cx - 3.0f,  baseY,
                        geo.cx + 3.0f,  baseY);
        g.setColour(theme::fgAlpha(0.30f));
        g.fillPath(tri);
    }

    // 3) Bit circles. Snap-only model (matches m4l): the snapshot itself
    // shifts CW by one slot each step, so bit 0 always sits at the top
    // and the visual update happens at the boundary. No rotation
    // transform / anticipation animation.
    const auto centerPt = Point2{ geo.cx, geo.cy };
    for (int i = 0; i < len; ++i) {
        const auto p = RingLogic::bitPosition(i, len, geo.radius, centerPt);
        const bool bitOn = ((reg >> i) & 1u) != 0u;
        // Bit 0 of the pre-shift snapshot is always the just-emitted bit
        // sitting under the playhead triangle. Salmon = "this just-
        // emitted bit sounded" (bit 0 on at the read-head position).
        // With density=1 this is exactly the bit ⟺ noteOn correspondence
        // the user expects; with density<1 the density gate is a
        // separate probability that may suppress the noteOn without
        // changing this visual (handled mentally as "density is
        // probabilistic"). The shiftAndFlip mutation event is no
        // longer surfaced visually — it desynced from audible playback
        // (some non-mutation audible notes had no salmon, breaking
        // the visual ⟺ audio mapping).
        const bool isReading = (i == 0);

        juce::Rectangle<float> circle(p.x - geo.bitRadius, p.y - geo.bitRadius,
                                      geo.bitRadius * 2.0f, geo.bitRadius * 2.0f);

        if (isReading && bitOn) {
            // Audible just-emitted bit: olive reading-head border with
            // salmon dot INSIDE the border (border untouched). The
            // salmon dot tracks every audible step at the read-head
            // position — visual signal of "this step sounded."
            g.setColour(theme::bg);
            g.fillEllipse(circle);
            g.setColour(theme::olive);
            g.drawEllipse(circle, 2.5f);
            g.setColour(theme::salmon);
            g.fillEllipse(circle.reduced(2.5f));
        } else if (isReading) {
            // Silent just-emitted bit (bit 0 off): reading-head outline
            // only, no salmon. Makes silent steps visually distinct
            // from audible ones at the playhead.
            g.setColour(theme::bg);
            g.fillEllipse(circle);
            g.setColour(theme::olive);
            g.drawEllipse(circle, 2.5f);
        } else if (bitOn) {
            g.setColour(theme::olive);
            g.fillEllipse(circle);
        } else {
            // Off bit: inboil uses olive @ 0.35 alpha for the stroke; that
            // matches its embedded scene-graph context. Stencil's
            // standalone editor follows the site / oedipa calibration —
            // neutral fg at ~0.45 alpha so the outline reads at a glance
            // against the bg cream without picking up an unwanted olive
            // tint on inactive bits.
            g.setColour(theme::bg);
            g.fillEllipse(circle);
            g.setColour(theme::fgAlpha(0.45f));
            g.drawEllipse(circle, 1.5f);
        }
    }

    // 4) Center text — fraction value above, currently-sounding note name
    // below. snap.note == -1 means "no audible note" (silent step / panic /
    // transport stop / ROLL), so the label is hidden — matching m4l's
    // currentNote lifecycle (registerRing.jsui.js).
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

    if (snap.note >= 0) {
        g.setColour(theme::olive);
        g.setFont(theme::dataFont(theme::fsXl, true));
        g.drawText(noteLabel(snap.note),
                   static_cast<int>(geo.cx - 60.0f),
                   static_cast<int>(geo.cy + 2.0f),
                   120, 18,
                   juce::Justification::centred);
    }
}

void RingView::mouseDown(const juce::MouseEvent& e)
{
    const auto geo = currentGeometry();
    const int len = currentLength(processor_.getApvts());

    // Snap-only: no rotation transform applied in paint, so the click
    // resolves against the un-rotated bit layout directly.
    const int idx = RingLogic::hitTest(Point2{ e.position.x, e.position.y },
                                       len, geo.radius, geo.bitRadius,
                                       Point2{ geo.cx, geo.cy });
    if (idx < 0) return;

    // ADR 007 §FREEZE / ROLL semantics: bit-click → ROLL. Use the JUCE
    // global Random whose state advances on every call; two clicks in
    // the same millisecond therefore still produce different seeds.
    // Quality is irrelevant: the user just wants "a different loop".
    const int newSeed = juce::Random::getSystemRandom().nextInt(0x7FFFFFFF);
    if (auto* p = processor_.getApvts().getParameter(plugin::pid::seed)) {
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(newSeed)));
    }
}

void RingView::timerCallback()
{
    // Snap-only: repaint only when the snapshot changes (matches m4l).
    // No rotation phase to drive intermediate frames.
    const auto snap = processor_.readEditorSnapshot();
    if (snap.reg == lastDrawnRegister_
        && snap.steps == lastDrawnSteps_
        && snap.note == lastDrawnNote_) {
        return;
    }
    lastDrawnRegister_ = snap.reg;
    lastDrawnSteps_ = snap.steps;
    lastDrawnNote_ = snap.note;
    repaint();
}

}  // namespace editor
}  // namespace stencil
