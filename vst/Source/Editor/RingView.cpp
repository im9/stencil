#include "Editor/RingView.h"

#include <algorithm>
#include <cmath>

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
    // 60 Hz so the γ-anticipation CW rotation animates smoothly through
    // the last ~20% of each step (≈25 ms at 16th @ 120bpm). timerCallback
    // gates the repaint on phase + snapshot change so the static window
    // doesn't burn frames.
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
    // drawn at the un-rotated top position under the playhead triangle.
    // Single coherent read so reg / mutated / note all come from the
    // same boundary — per-field accessors could tear across two steps
    // (ADR 007 §Audit follow-ups — tuple coherence).
    const auto snap = processor_.readEditorSnapshot();
    const auto reg = snap.reg;
    const int mutated = snap.mutated;

    // γ-anticipation phase: fraction of step elapsed since the last
    // step boundary fired. Inputs come from atomic wall-clock anchors
    // published by the audio thread; phase is clamped inside RingLogic.
    const int64_t lastStepUs = processor_.getLastStepTimeMicros();
    const int64_t stepDurUs  = processor_.getStepDurationMicros();
    double phase = 0.0;
    if (lastStepUs > 0 && stepDurUs > 0) {
        const int64_t nowUs = static_cast<int64_t>(
            juce::Time::getMillisecondCounterHiRes() * 1000.0);
        phase = static_cast<double>(nowUs - lastStepUs)
              / static_cast<double>(stepDurUs);
    }
    const float rotation = RingLogic::phaseRotationDegrees(phase, len);

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
    // "the head" — the ring rotates underneath it. Tip is closer to the ring
    // than the base so the triangle visually points down at the bit it
    // marks.
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

    // 3) Bit circles. Apply γ-anticipation CW rotation around the ring
    // center, then draw each bit at its un-rotated logical position.
    // With CCW bit arrangement and CW rotation, bit 1 (upper-left)
    // eases CW into the top during the last 20% of the step, then the
    // snapshot snaps to the next register where bit 0 == previous bit 1
    // (same value, continuous through the rotation reset).
    const auto centerPt = Point2{ geo.cx, geo.cy };
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(
        juce::degreesToRadians(rotation), geo.cx, geo.cy));
    for (int i = 0; i < len; ++i) {
        const auto p = RingLogic::bitPosition(i, len, geo.radius, centerPt);
        const bool bitOn = ((reg >> i) & 1u) != 0u;
        // Bit 0 of the pre-shift snapshot is always the just-emitted bit
        // sitting under the playhead triangle. `mutated` is 0 when
        // shiftAndFlip flipped that bit on the most recent step; salmon
        // takes precedence so the flip event is unambiguous.
        const bool isReading = (i == 0);
        const bool isMutated = (i == mutated);

        juce::Rectangle<float> circle(p.x - geo.bitRadius, p.y - geo.bitRadius,
                                      geo.bitRadius * 2.0f, geo.bitRadius * 2.0f);

        if (isMutated) {
            // Salmon: shiftAndFlip just flipped this bit (parity with
            // inboil .bit-mutated).
            g.setColour(theme::salmon);
            g.fillEllipse(circle);
            g.drawEllipse(circle, 1.5f);
        } else if (isReading) {
            // Reading-head highlight on bit 0: open circle with heavy
            // olive stroke (inboil .bit-reading). Reinforces the
            // playhead triangle visually even when bit 0 is "off".
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
    g.drawText(noteLabel(snap.note),
               static_cast<int>(geo.cx - 60.0f),
               static_cast<int>(geo.cy + 2.0f),
               120, 18,
               juce::Justification::centred);
}

void RingView::mouseDown(const juce::MouseEvent& e)
{
    const auto geo = currentGeometry();
    const int len = currentLength(processor_.getApvts());

    // Hit-test runs in un-rotated space: invert the paint's γ-rotation
    // so a click on the bit visually at top during the anticipation
    // window resolves to the bit the user is aiming at.
    const int64_t lastStepUs = processor_.getLastStepTimeMicros();
    const int64_t stepDurUs  = processor_.getStepDurationMicros();
    double phase = 0.0;
    if (lastStepUs > 0 && stepDurUs > 0) {
        const int64_t nowUs = static_cast<int64_t>(
            juce::Time::getMillisecondCounterHiRes() * 1000.0);
        phase = static_cast<double>(nowUs - lastStepUs)
              / static_cast<double>(stepDurUs);
    }
    const float rotation = RingLogic::phaseRotationDegrees(phase, len);
    const auto inverse = juce::AffineTransform::rotation(
        juce::degreesToRadians(rotation), geo.cx, geo.cy).inverted();
    juce::Point<float> click = e.position;
    click.applyTransform(inverse);

    const int idx = RingLogic::hitTest(Point2{ click.x, click.y },
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
    // Repaint on (a) snapshot / step-count change — must redraw bits +
    // re-anchor the playhead; or (b) inside the γ-anticipation window
    // where rotation is moving continuously. The static window (≥80%
    // of each step) is the cheap early-out path.
    const auto snap = processor_.readEditorSnapshot();
    bool dirty = false;
    if (snap.reg != lastDrawnRegister_ || snap.steps != lastDrawnSteps_) {
        lastDrawnRegister_ = snap.reg;
        lastDrawnSteps_ = snap.steps;
        dirty = true;
    }
    if (!dirty) {
        const int64_t lastStepUs = processor_.getLastStepTimeMicros();
        const int64_t stepDurUs  = processor_.getStepDurationMicros();
        if (lastStepUs > 0 && stepDurUs > 0) {
            const int64_t nowUs = static_cast<int64_t>(
                juce::Time::getMillisecondCounterHiRes() * 1000.0);
            const double phase = static_cast<double>(nowUs - lastStepUs)
                               / static_cast<double>(stepDurUs);
            // Matches RingLogic::phaseRotationDegrees default animation
            // start; a few-ms drift only delays the animation by one
            // timer tick (~17ms @ 60Hz) which is below the perceptual
            // floor.
            if (phase > 0.8 && phase <= 1.05) dirty = true;
        }
    }
    if (dirty) repaint();
}

}  // namespace editor
}  // namespace stencil
