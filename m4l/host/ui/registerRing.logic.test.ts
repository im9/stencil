import { test } from "node:test";
import assert from "node:assert/strict";

import {
  BIT_GAP,
  CANVAS_MARGIN,
  MAX_BIT_RADIUS,
  MAX_LENGTH,
  MIN_LENGTH,
  POINTER_GAP,
  POINTER_HALF_WIDTH,
  POINTER_HEIGHT,
  type RingModel,
  advanceReadHead,
  bitPosition,
  bitPositionRotated,
  computeGeometry,
  createModel,
  hitTest,
  hitTestRotated,
  pointerTip,
  readingIndexAt,
  setHovered,
  setReadHead,
  setRegister,
  toggleBitAt,
} from "./registerRing.logic.ts";

// ---- createModel -----------------------------------------------------------

test("createModel — length=8 produces 8 zero bits, readHead=0, hovered=-1", () => {
  const m = createModel(8);
  assert.equal(m.bits.length, 8);
  assert.deepEqual(m.bits, [0, 0, 0, 0, 0, 0, 0, 0]);
  assert.equal(m.readHead, 0);
  assert.equal(m.hovered, -1);
});

test("createModel — clamps length below MIN_LENGTH", () => {
  // length=1 is below ADR 002 minimum (2). Renderer should never see this,
  // but defend at the boundary so a stray init doesn't produce a 0-length
  // bits[] that breaks hitTest math.
  const m = createModel(1);
  assert.equal(m.bits.length, MIN_LENGTH);
});

test("createModel — clamps length above MAX_LENGTH", () => {
  // ADR 002 caps length at 32. A patcher bug sending 64 should be coerced.
  const m = createModel(64);
  assert.equal(m.bits.length, MAX_LENGTH);
});

// ---- computeGeometry -------------------------------------------------------

test("computeGeometry — center at canvas midpoint", () => {
  const g = computeGeometry(200, 140, 8);
  assert.equal(g.cx, 100);
  assert.equal(g.cy, 70);
});

test("computeGeometry — radius constrained by smaller dimension", () => {
  // 200×140 canvas: limiting dimension is height=140. Max usable radius =
  // 140/2 - margin = 70 - CANVAS_MARGIN. Final radius = maxRadius - bitRadius
  // (so dots stay inside the canvas, not just their centers).
  const g = computeGeometry(200, 140, 8);
  const maxRadius = 140 / 2 - CANVAS_MARGIN;
  assert.ok(g.radius <= maxRadius);
  assert.ok(g.radius >= maxRadius - MAX_BIT_RADIUS - 1);
});

test("computeGeometry — bitRadius capped at MAX_BIT_RADIUS", () => {
  // Large canvas, few bits → arc spacing huge → would-be radius >> MAX.
  // Cap is musical: dots beyond ~14px look like buttons, not bits.
  const g = computeGeometry(800, 800, 4);
  assert.equal(g.bitRadius, MAX_BIT_RADIUS);
});

test("computeGeometry — bitRadius shrinks with more bits", () => {
  // Same canvas, more bits → tighter arc spacing → smaller dots. Required
  // so dots don't overlap at length=32.
  const g8 = computeGeometry(200, 200, 8);
  const g32 = computeGeometry(200, 200, 32);
  assert.ok(g32.bitRadius < g8.bitRadius);
});

test("computeGeometry — bitRadius stays positive even at MAX_LENGTH on small canvas", () => {
  // Defensive: π·R/32 - gap could go negative on tiny canvases. Renderer
  // would draw degenerate dots; logic must clamp to ≥ 1.
  const g = computeGeometry(60, 60, 32);
  assert.ok(g.bitRadius >= 1, `bitRadius=${g.bitRadius}`);
});

test("computeGeometry — length echoed in geometry", () => {
  const g = computeGeometry(200, 200, 16);
  assert.equal(g.length, 16);
});

// ---- bitPosition -----------------------------------------------------------

test("bitPosition — index 0 sits at top of ring", () => {
  // Convention from inboil: angle = (i/n)·2π - π/2, so i=0 → -π/2 →
  // (cos=0, sin=-1) → (cx, cy - radius). Top.
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(0, g);
  assert.ok(Math.abs(p.x - g.cx) < 1e-9);
  assert.ok(Math.abs(p.y - (g.cy - g.radius)) < 1e-9);
});

test("bitPosition — clockwise: index length/4 sits at right (length=8 → idx 2)", () => {
  // Increasing index walks clockwise in screen coords (y down). Confirms
  // sign of the angle formula matches inboil.
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(2, g);
  assert.ok(Math.abs(p.x - (g.cx + g.radius)) < 1e-9);
  assert.ok(Math.abs(p.y - g.cy) < 1e-9);
});

test("bitPosition — index length/2 sits at bottom", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(4, g);
  assert.ok(Math.abs(p.x - g.cx) < 1e-9);
  assert.ok(Math.abs(p.y - (g.cy + g.radius)) < 1e-9);
});

test("bitPosition — index 3·length/4 sits at left", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(6, g);
  assert.ok(Math.abs(p.x - (g.cx - g.radius)) < 1e-9);
  assert.ok(Math.abs(p.y - g.cy) < 1e-9);
});

// ---- bitPositionRotated (revolver model) -----------------------------------

test("bitPositionRotated — cumulativeSteps=0 reduces to bitPosition", () => {
  // No rotation = identical to the unrotated layout; ensures the rotation
  // formula is additive and round-trippable rather than a separate code path.
  const g = computeGeometry(200, 200, 8);
  for (let i = 0; i < 8; i++) {
    const a = bitPosition(i, g);
    const b = bitPositionRotated(i, g, 0);
    assert.ok(Math.abs(a.x - b.x) < 1e-9, `x mismatch at idx ${i}`);
    assert.ok(Math.abs(a.y - b.y) < 1e-9, `y mismatch at idx ${i}`);
  }
});

test("bitPositionRotated — period equals length (full revolution returns to start)", () => {
  // After `length` shifts the cylinder is back where it started. Mirrors
  // inboil's `rotationDeg = cumulativeSteps * stepAngle` modulo 360.
  const g = computeGeometry(200, 200, 8);
  for (let i = 0; i < 8; i++) {
    const a = bitPosition(i, g);
    const b = bitPositionRotated(i, g, 8);
    assert.ok(Math.abs(a.x - b.x) < 1e-9, `period: x mismatch at idx ${i}`);
    assert.ok(Math.abs(a.y - b.y) < 1e-9, `period: y mismatch at idx ${i}`);
  }
});

test("bitPositionRotated — 1 step CCW shifts bit 0 to angle -π/2 - 2π/length", () => {
  // ADR 003 §TM register ring: rotation is CCW to match the engine's
  // shift direction (`register >>> 1` moves indices DOWN, so the bit
  // consumed at index 0 visually flows away from the pointer in the
  // CCW direction). bit 0 starts at top (angle -π/2) and after 1 step
  // lands at the next CCW slot (angle -π/2 - 2π/length, upper-LEFT).
  const g = computeGeometry(200, 200, 8);
  const expectedAngle = -Math.PI / 2 - (Math.PI * 2) / 8;
  const ex = g.cx + g.radius * Math.cos(expectedAngle);
  const ey = g.cy + g.radius * Math.sin(expectedAngle);
  const p = bitPositionRotated(0, g, 1);
  assert.ok(Math.abs(p.x - ex) < 1e-9, `x: got ${p.x}, expected ${ex}`);
  assert.ok(Math.abs(p.y - ey) < 1e-9, `y: got ${p.y}, expected ${ey}`);
});

// ---- readingIndexAt --------------------------------------------------------

test("readingIndexAt — pre-step (0): bit 0 sits at top by convention", () => {
  // Initial pose has bit 0 at angle -π/2 (top), so before any rotation the
  // pointer reads bit 0.
  assert.equal(readingIndexAt(0, 8), 0);
});

test("readingIndexAt — 1 step CCW puts bit 1 under the pointer", () => {
  // CCW rotation by stepAngle moves the bit originally at upper-RIGHT
  // (logical index 1, angle -π/2 + 2π/length) up to the top slot. So
  // after k steps, the bit at the pointer has logical index `k mod length`.
  assert.equal(readingIndexAt(1, 8), 1);
  assert.equal(readingIndexAt(2, 8), 2);
  assert.equal(readingIndexAt(7, 8), 7);
});

test("readingIndexAt — wraps with period length", () => {
  // host.position is monotonic and never resets except on transport
  // start/seed, so the index calculation must wrap cleanly for arbitrarily
  // large counters.
  for (let k = 0; k < 8; k++) {
    assert.equal(readingIndexAt(k + 8, 8), readingIndexAt(k, 8));
    assert.equal(readingIndexAt(k + 16, 8), readingIndexAt(k, 8));
  }
});

test("readingIndexAt — large cumulativeSteps stays in [0, length)", () => {
  // Defensive: a long-running session can push position into the thousands.
  // The result must remain a valid bit index with no off-by-one drift.
  for (const k of [100, 1000, 12345]) {
    const idx = readingIndexAt(k, 8);
    assert.ok(idx >= 0 && idx < 8, `out-of-range: k=${k} idx=${idx}`);
  }
});

test("readingIndexAt — degenerate length=0 returns -1", () => {
  // Bridge can momentarily emit an empty register on init/length-shrink;
  // the renderer queries readingIndexAt during paint and must not throw.
  assert.equal(readingIndexAt(0, 0), -1);
});

// ---- pointerTip ------------------------------------------------------------

test("pointerTip — sits above outermost dot edge by POINTER_GAP+HEIGHT", () => {
  // Vertical position derives from radius + bitRadius (bottom of dot at
  // top of ring) + POINTER_GAP (padding) + POINTER_HEIGHT (triangle height).
  // The tip is the apex; the base sits at tip.y + POINTER_HEIGHT.
  const g = computeGeometry(200, 200, 8);
  const tip = pointerTip(g);
  assert.equal(tip.x, g.cx);
  assert.equal(
    tip.y,
    g.cy - g.radius - g.bitRadius - POINTER_GAP - POINTER_HEIGHT,
  );
});

test("pointerTip — sits clear of the bit dot at top", () => {
  // Sanity: tip plus full triangle height must not overlap the top dot's
  // upper edge. Otherwise the pointer would visually fuse with the dot.
  const g = computeGeometry(200, 200, 8);
  const tip = pointerTip(g);
  const topDotUpperEdge = g.cy - g.radius - g.bitRadius;
  assert.ok(tip.y + POINTER_HEIGHT <= topDotUpperEdge);
});

test("POINTER_HALF_WIDTH stays positive (renderer expects a triangle)", () => {
  // Renderer draws a triangle from (tip) to (cx ± POINTER_HALF_WIDTH, base).
  // A zero or negative width would degenerate to a line; verify the constant
  // expresses a real triangle.
  assert.ok(POINTER_HALF_WIDTH > 0);
});

// ---- hitTestRotated --------------------------------------------------------

test("hitTestRotated — clicking the rotated visual position returns the LOGICAL bit index", () => {
  // After rotation, the user clicks at the on-screen location of bit 3
  // post-rotation; setBit must target logical index 3, not whatever bit
  // currently happens to sit at the unrotated slot 3. Otherwise the user
  // toggles a bit different from the one they pressed.
  const g = computeGeometry(200, 200, 8);
  for (const k of [0, 1, 4, 7, 100]) {
    for (let i = 0; i < 8; i++) {
      const p = bitPositionRotated(i, g, k);
      assert.equal(
        hitTestRotated(p.x, p.y, g, k),
        i,
        `cumulativeSteps=${k}, idx=${i}: hit-test must return logical idx`,
      );
    }
  }
});

test("hitTestRotated — empty space returns -1 regardless of rotation", () => {
  const g = computeGeometry(200, 200, 8);
  for (const k of [0, 3, 7]) {
    assert.equal(hitTestRotated(g.cx, g.cy, g, k), -1, `center, k=${k}`);
    assert.equal(hitTestRotated(0, 0, g, k), -1, `corner, k=${k}`);
  }
});

// ---- hitTest ---------------------------------------------------------------

test("hitTest — click on bit center returns that index", () => {
  const g = computeGeometry(200, 200, 8);
  for (let i = 0; i < 8; i++) {
    const p = bitPosition(i, g);
    assert.equal(hitTest(p.x, p.y, g), i, `bit ${i} center should hit ${i}`);
  }
});

test("hitTest — click outside ring returns -1", () => {
  const g = computeGeometry(200, 200, 8);
  // Far corner, beyond any bit.
  assert.equal(hitTest(0, 0, g), -1);
  assert.equal(hitTest(200, 200, g), -1);
});

test("hitTest — click at ring center returns -1", () => {
  // Center is inside the ring's enclosed circle, but not within bitRadius
  // of any dot. The middle is empty space (where the value readout will go).
  const g = computeGeometry(200, 200, 8);
  assert.equal(hitTest(g.cx, g.cy, g), -1);
});

test("hitTest — click between two adjacent bits returns -1", () => {
  // Midpoint of arc between bit 0 (top) and bit 1: should fall in the gap
  // when bitRadius < arc-half-spacing. Verifies BIT_GAP gives real gaps.
  const g = computeGeometry(200, 200, 8);
  const a = bitPosition(0, g);
  const b = bitPosition(1, g);
  const mx = (a.x + b.x) / 2;
  const my = (a.y + b.y) / 2;
  assert.equal(hitTest(mx, my, g), -1);
});

test("hitTest — click within bitRadius of a bit center returns that bit", () => {
  // Boundary: just inside the dot. (At exactly bitRadius the test uses ≤,
  // so on-boundary hits — match inboil's pointer-events behavior.)
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(3, g);
  const offset = g.bitRadius * 0.9;
  assert.equal(hitTest(p.x + offset, p.y, g), 3);
});

test("hitTest — length=32 dots remain hit-testable at every index", () => {
  // Stress: smallest dots, tightest spacing. If hit-test passes at all 32
  // indices, length=32 is usable end-to-end.
  const g = computeGeometry(200, 200, 32);
  for (let i = 0; i < 32; i++) {
    const p = bitPosition(i, g);
    assert.equal(hitTest(p.x, p.y, g), i, `length=32 bit ${i} center should hit ${i}`);
  }
});

// ---- toggleBitAt -----------------------------------------------------------

test("toggleBitAt — flips 0 to 1", () => {
  const m = createModel(4);
  const next = toggleBitAt(m, 1);
  assert.equal(next.bits[1], 1);
  assert.deepEqual(next.bits, [0, 1, 0, 0]);
});

test("toggleBitAt — flips 1 to 0", () => {
  const base = createModel(4);
  const m = setRegister(base, [0, 1, 0, 0]);
  const next = toggleBitAt(m, 1);
  assert.equal(next.bits[1], 0);
});

test("toggleBitAt — does not mutate input model", () => {
  // Renderer relies on immutability so it can compare references for redraw
  // decisions and so accidental aliasing doesn't corrupt the prior frame.
  const m = createModel(4);
  const before = m.bits.slice();
  toggleBitAt(m, 2);
  assert.deepEqual(m.bits, before);
});

test("toggleBitAt — out-of-bounds index returns model unchanged", () => {
  // Bridge re-emits register on every setBit (even out-of-bounds) so the
  // logic layer must defensively no-op here, not throw.
  const m = createModel(4);
  assert.equal(toggleBitAt(m, 4), m);
  assert.equal(toggleBitAt(m, -1), m);
  assert.equal(toggleBitAt(m, 1.5), m);
});

// ---- advanceReadHead -------------------------------------------------------

test("advanceReadHead — increments by 1", () => {
  const m = createModel(8);
  const next = advanceReadHead(m);
  assert.equal(next.readHead, 1);
});

test("advanceReadHead — wraps at length", () => {
  // Position is always 0..length-1. Bridge sends `position` as the wrapped
  // index; this helper exists for renderer-driven local advance (not the
  // current path, but cheap and obvious to provide).
  const m = setReadHead(createModel(4), 3);
  const next = advanceReadHead(m);
  assert.equal(next.readHead, 0);
});

test("advanceReadHead — does not change bits", () => {
  const base = setRegister(createModel(4), [1, 0, 1, 0]);
  const next = advanceReadHead(base);
  assert.deepEqual(next.bits, [1, 0, 1, 0]);
});

// ---- setRegister -----------------------------------------------------------

test("setRegister — replaces bits from Max varargs", () => {
  // Bridge emits `register bit0 bit1 ... bitN-1`. Renderer collects args
  // into an array and calls setRegister.
  const m = createModel(4);
  const next = setRegister(m, [1, 0, 1, 1]);
  assert.deepEqual(next.bits, [1, 0, 1, 1]);
});

test("setRegister — sanitizes non-binary input to 0/1", () => {
  // Defensive: if a junk value slips through (e.g. float from a typo), keep
  // the LSB so we never store anything other than Bit. Mirrors host.setBit's
  // `(value & 1)` coercion in bridge.ts.
  const m = createModel(4);
  const next = setRegister(m, [3, 2, 1, 0]);
  assert.deepEqual(next.bits, [1, 0, 1, 0]);
});

test("setRegister — clamps readHead when new length is shorter", () => {
  // If user reduces `length` from 16 to 4 while readHead=10, the next
  // emitted register has length 4 and readHead must clamp into range.
  const long = setReadHead(createModel(16), 10);
  const next = setRegister(long, [1, 0, 1, 0]);
  assert.equal(next.bits.length, 4);
  assert.ok(next.readHead < 4);
});

// ---- setReadHead -----------------------------------------------------------

test("setReadHead — sets readHead from Max position message", () => {
  const m = createModel(8);
  const next = setReadHead(m, 5);
  assert.equal(next.readHead, 5);
});

test("setReadHead — wraps positions outside [0, length)", () => {
  // Bridge sends raw step counter; jsui should not have to know the modulus.
  // Mirrors the wrap that happens in the engine's stepping logic.
  const m = createModel(8);
  assert.equal(setReadHead(m, 8).readHead, 0);
  assert.equal(setReadHead(m, 17).readHead, 1);
  assert.equal(setReadHead(m, -1).readHead, 7);
});

test("setReadHead — non-finite input is a no-op", () => {
  const m = createModel(8);
  assert.equal(setReadHead(m, NaN).readHead, 0);
  assert.equal(setReadHead(m, Infinity).readHead, 0);
});

// ---- setHovered ------------------------------------------------------------

test("setHovered — sets hovered to given index", () => {
  const m = createModel(4);
  const next = setHovered(m, 2);
  assert.equal(next.hovered, 2);
});

test("setHovered — out-of-bounds clears hover", () => {
  // Hover-leave is encoded as setHovered(-1) by the renderer; treat any
  // invalid index the same way so off-canvas mouse-move is handled.
  const m: RingModel = { bits: [0, 0, 0, 0], readHead: 0, hovered: 1 };
  assert.equal(setHovered(m, -1).hovered, -1);
  assert.equal(setHovered(m, 4).hovered, -1);
  assert.equal(setHovered(m, 1.5).hovered, -1);
});

test("setHovered — does not affect bits or readHead", () => {
  const base = setRegister(createModel(4), [1, 0, 1, 0]);
  const m = setReadHead(base, 2);
  const next = setHovered(m, 1);
  assert.deepEqual(next.bits, [1, 0, 1, 0]);
  assert.equal(next.readHead, 2);
});

// ---- constants exposed for the mirror drift test ---------------------------

test("constants exported for mirror drift check", () => {
  // The mirror drift test reads these named exports from renderer source
  // text. Keep them as bare numeric exports (no enum / no object wrap) so
  // the renderer can mirror them as `var FOO = 14` literals.
  assert.equal(typeof MIN_LENGTH, "number");
  assert.equal(typeof MAX_LENGTH, "number");
  assert.equal(typeof MAX_BIT_RADIUS, "number");
  assert.equal(typeof BIT_GAP, "number");
  assert.equal(typeof CANVAS_MARGIN, "number");
});
