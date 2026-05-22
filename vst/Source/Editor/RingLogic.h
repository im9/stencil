// Pure-math layer for RingView (ADR 007 §Editor — Logic / renderer split).
// No JUCE dependency: lives under Source/Editor/ but compiles as plain
// C++17 so tests can exercise hit-testing, geometry, and FREEZE/ROLL
// resolution without instantiating a JUCE Component or pumping a
// MessageManager.
//
// Spec inheritance: every function here mirrors a $derived expression or
// pure helper from inboil's TuringSheet.svelte (referenced inline). The
// vst port keeps the math identical so the visual feel of the bit ring
// (rotation cadence, head-bit position, click target sizing) carries
// across hosts.

#pragma once

#include <cstdint>
#include <optional>

#include "Engine/Rng.h"

namespace stencil {
namespace editor {

struct Point2
{
    float x;
    float y;
};

// Result of a FREEZE button press, reported back so the caller can write
// the new lock value to APVTS and stash the new prev-lock for the next
// toggle. Both values must be applied atomically: writing only `newLock`
// loses the round-trip prev value, writing only `newPrevLock` desyncs
// the displayed lock from the parameter.
struct FreezeResult
{
    float newLock;
    std::optional<float> newPrevLock;
};

class RingLogic
{
public:
    // Position of bit `idx` of an `length`-bit register on a circle of
    // radius `ringRadius` centered at `center`. Bit 0 sits at the top
    // (12 o'clock) so the read-head pointer above the ring naturally
    // aligns with the LSB on the un-rotated frame. Mirrors inboil's
    // bitPos: angle = (idx / length) * 2π - π/2.
    static Point2 bitPosition(int idx, int length, float ringRadius, Point2 center);

    // Returns the bit index (0..length-1) of the first bit whose centroid
    // is within `bitRadius` of `click`, or -1 when the click misses every
    // bit. ADR 007 §FREEZE / ROLL semantics — vst v1 routes a positive
    // bit hit to ROLL; persistent register edits are deferred (concept.md
    // §What Stencil does — "shape the loop via lock + seed, not by
    // drawing bits").
    static int hitTest(Point2 click, int length, float ringRadius,
                       float bitRadius, Point2 center);

    // FREEZE button press. ADR 007 §FREEZE / ROLL semantics — toggles
    // lock between the user's previous value and 1.0. Cold-start
    // (currentLock == 1.0 with no stash) restores 0.5 to give the user
    // an audible loop after un-freezing a saved-frozen preset.
    static FreezeResult freezeAction(float currentLock,
                                     std::optional<float> prevLock);

    // ROLL button press. ADR 007 §FREEZE / ROLL semantics — draws a fresh
    // seed from the supplied rng, masked into [0, 2^31-1] to fit the
    // APVTS Seed parameter range (concept.md §Parameter surface).
    static int rollAction(stencil::engine::RngState& rng);
};

}  // namespace editor
}  // namespace stencil
