// Editor RingLogic tests (ADR 007 Phase 3). Pure-math layer split off
// from RingView per CLAUDE.md "GUI / UI components": geometry, hit
// testing, revolver rotation, head-bit index, and freeze/roll action
// resolution. No JUCE dependency in this translation unit.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <optional>

#include "Editor/RingLogic.h"
#include "Engine/Rng.h"

using stencil::editor::RingLogic;
using stencil::editor::Point2;

namespace
{
constexpr float kEps = 1e-4f;
}

// ─── bitPosition ──────────────────────────────────────────────────────────

TEST_CASE("bitPosition: bit 0 sits at top of the ring", "[editor][ring][geometry]")
{
    // Justification: inboil's bitPos sets `angle = (idx/total)*2π - π/2`,
    // so idx=0 has angle=-π/2 and (cos, sin) = (0, -1). With center=(125,125)
    // and R=105, bit 0 lands at (125, 20). Top-of-ring is the conventional
    // 12-o'clock origin for read-head placement.
    const Point2 c{125.0f, 125.0f};
    const auto p = RingLogic::bitPosition(0, 8, 105.0f, c);
    REQUIRE_THAT(p.x, Catch::Matchers::WithinAbs(125.0, kEps));
    REQUIRE_THAT(p.y, Catch::Matchers::WithinAbs(20.0,  kEps));
}

TEST_CASE("bitPosition: quarter-turn lands at left (CCW arrangement)",
          "[editor][ring][geometry]")
{
    // Justification: CCW bit arrangement (bit 0 top, bit 1 upper-left,
    // bit 2 left, ...). idx=length/4 advances CCW by π/2: angle =
    // -(2/8)*2π - π/2 = -π, so (cos, sin) = (-1, 0). Bit length/4
    // should sit at center.x - R, center.y. Picked to match inboil's
    // CW visual value-flow; see ADR 007 §Visual playhead alignment.
    const Point2 c{125.0f, 125.0f};
    const auto p = RingLogic::bitPosition(2, 8, 105.0f, c);
    REQUIRE_THAT(p.x, Catch::Matchers::WithinAbs(20.0,  kEps));
    REQUIRE_THAT(p.y, Catch::Matchers::WithinAbs(125.0, kEps));
}

TEST_CASE("bitPosition: bit 1 sits at upper-left (CCW arrangement)",
          "[editor][ring][geometry]")
{
    // Justification: with the CCW arrangement, bit 1 is at angle
    // -(1/8)*2π - π/2 = -3π/4, i.e., upper-left of center. This is the
    // arrangement that makes the register's right-shift look CW on
    // screen — bit 1's value flowing to bit 0's slot is upper-left →
    // top, a clockwise motion.
    const Point2 c{125.0f, 125.0f};
    const auto p = RingLogic::bitPosition(1, 8, 105.0f, c);
    const float expected = 125.0f - 105.0f / std::sqrt(2.0f);  // ≈ 50.74
    REQUIRE_THAT(p.x, Catch::Matchers::WithinAbs((double) expected, 0.01));
    REQUIRE_THAT(p.y, Catch::Matchers::WithinAbs((double) expected, 0.01));
}

TEST_CASE("bitPosition: full sweep distributes around the circle", "[editor][ring][geometry]")
{
    // Justification: every bit must lie on the circle of radius R about
    // the center. Catches a sign / scale regression in the angle math.
    const Point2 c{200.0f, 150.0f};
    const float R = 80.0f;
    for (int len : {2, 4, 8, 16, 32}) {
        for (int i = 0; i < len; ++i) {
            const auto p = RingLogic::bitPosition(i, len, R, c);
            const float dx = p.x - c.x;
            const float dy = p.y - c.y;
            const float r  = std::sqrt(dx * dx + dy * dy);
            REQUIRE_THAT(r, Catch::Matchers::WithinAbs((double) R, 1e-3));
        }
    }
}

// ─── hitTest ──────────────────────────────────────────────────────────────

TEST_CASE("hitTest: click on bit 0 returns 0", "[editor][ring][hit]")
{
    // Justification: clicking the dead center of bit 0 (top of ring) must
    // resolve to index 0. Bit-toggle UX requires this to drive ROLL on
    // direct bit clicks (ADR 007 §FREEZE / ROLL semantics).
    const Point2 c{125.0f, 125.0f};
    const auto bit0 = RingLogic::bitPosition(0, 8, 105.0f, c);
    REQUIRE(RingLogic::hitTest(bit0, 8, 105.0f, /*bitRadius*/ 12.0f, c) == 0);
}

TEST_CASE("hitTest: click between bits returns -1", "[editor][ring][hit]")
{
    // Justification: clicks outside any bit's bitRadius must not register
    // a bit hit. Center of ring is the canonical "no bit" location.
    const Point2 c{125.0f, 125.0f};
    REQUIRE(RingLogic::hitTest(c, 8, 105.0f, 12.0f, c) == -1);
}

TEST_CASE("hitTest: click far outside ring returns -1", "[editor][ring][hit]")
{
    // Justification: outside the ring + bitRadius envelope, no bit can be
    // hit; UI must not resolve random clicks into bit edits.
    const Point2 c{125.0f, 125.0f};
    REQUIRE(RingLogic::hitTest(Point2{5.0f, 5.0f}, 8, 105.0f, 12.0f, c) == -1);
}

TEST_CASE("hitTest: each bit centroid resolves to its index", "[editor][ring][hit]")
{
    // Justification: round-trip property — every bit's centroid maps to
    // its own index for all supported lengths. Fails if hitTest's
    // distance threshold or angle-to-index inversion drifts from
    // bitPosition.
    const Point2 c{200.0f, 200.0f};
    const float R = 105.0f;
    const float bitR = 10.0f;
    for (int len : {2, 4, 8, 16, 32}) {
        for (int i = 0; i < len; ++i) {
            const auto p = RingLogic::bitPosition(i, len, R, c);
            REQUIRE(RingLogic::hitTest(p, len, R, bitR, c) == i);
        }
    }
}

// Ring rotation removed 2026-05-22 (ADR 007 §Editor revision): vst now
// follows m4l's snap-only behavior. RingLogic no longer carries a phase
// rotation function; bit positions are read directly from bitPosition
// without an AffineTransform in the paint loop.

// ─── freezeAction ─────────────────────────────────────────────────────────

TEST_CASE("freezeAction: from non-frozen latches lock to 1 and stashes prev", "[editor][freeze]")
{
    // Justification: ADR 007 §FREEZE / ROLL semantics — first FREEZE press
    // sets lock=1 and remembers the previous lock so unfreeze restores it.
    const auto r = RingLogic::freezeAction(0.42f, std::nullopt);
    REQUIRE(r.newLock == 1.0f);
    REQUIRE(r.newPrevLock.has_value());
    REQUIRE(r.newPrevLock.value() == 0.42f);
}

TEST_CASE("freezeAction: from frozen restores stashed prev and clears", "[editor][freeze]")
{
    // Justification: second FREEZE press un-freezes — restores the lock the
    // user had before the first press, and clears the stash so the next
    // freeze captures fresh.
    const auto r = RingLogic::freezeAction(1.0f, std::optional<float>{0.42f});
    REQUIRE(r.newLock == 0.42f);
    REQUIRE_FALSE(r.newPrevLock.has_value());
}

TEST_CASE("freezeAction: from frozen with no stash falls back to 0.5", "[editor][freeze]")
{
    // Justification: cold-load case — the editor was reopened while lock=1
    // (e.g. a saved preset froze the loop) and the transient `prevLock`
    // is empty. Fall back to inboil's default 0.5 so the user always has
    // a sane starting point post-unfreeze.
    const auto r = RingLogic::freezeAction(1.0f, std::nullopt);
    REQUIRE(r.newLock == 0.5f);
    REQUIRE_FALSE(r.newPrevLock.has_value());
}

// ─── rollAction ───────────────────────────────────────────────────────────

TEST_CASE("rollAction: returns a value in [0, 2^31)", "[editor][roll]")
{
    // Justification: ADR 007 §FREEZE / ROLL semantics — ROLL writes a fresh
    // value to the seed parameter, drawn uniformly from 0..2^31-1 (the
    // APVTS Seed parameter range matches concept.md). High bit must not
    // leak through; setValueNotifyingHost would clip but it should never
    // be set in the first place.
    auto rng = stencil::engine::seedRng(1234);
    for (int i = 0; i < 256; ++i) {
        const int seed = RingLogic::rollAction(rng);
        REQUIRE(seed >= 0);
        REQUIRE(seed <= 2147483647);  // INT32_MAX
    }
}

TEST_CASE("rollAction: deterministic given the same rng state", "[editor][roll]")
{
    // Justification: identical RngState seeds must produce identical rolls.
    // Without this the test_Editor mouseDown ROLL semantics test could not
    // reproduce a known seed value to assert against.
    auto a = stencil::engine::seedRng(42);
    auto b = stencil::engine::seedRng(42);
    for (int i = 0; i < 16; ++i) {
        REQUIRE(RingLogic::rollAction(a) == RingLogic::rollAction(b));
    }
}

TEST_CASE("rollAction: consecutive rolls differ", "[editor][roll]")
{
    // Justification: a stream of rolls should not stick on a constant —
    // the user pressing ROLL twice gets two different seeds. Catches
    // accidental "always returns 0" regressions if the bitmask is wrong.
    auto rng = stencil::engine::seedRng(7);
    int prev = RingLogic::rollAction(rng);
    int diffs = 0;
    for (int i = 0; i < 32; ++i) {
        const int next = RingLogic::rollAction(rng);
        if (next != prev) ++diffs;
        prev = next;
    }
    REQUIRE(diffs >= 30);  // overwhelmingly distinct over a 32-step window
}
