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
  bitAngle,
  bitPosition,
  computeGeometry,
  createModel,
  hitTest,
  pointerTip,
  setHovered,
  setRegister,
  toggleBitAt,
} from "./registerRing.logic.ts";

// ---- createModel -----------------------------------------------------------

test("createModel — length=8 produces 8 zero bits, hovered=-1", () => {
  const m = createModel(8);
  assert.equal(m.bits.length, 8);
  assert.deepEqual(m.bits, [0, 0, 0, 0, 0, 0, 0, 0]);
  assert.equal(m.hovered, -1);
});

test("createModel — clamps length below MIN_LENGTH", () => {
  const m = createModel(1);
  assert.equal(m.bits.length, MIN_LENGTH);
});

test("createModel — clamps length above MAX_LENGTH", () => {
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
  const g = computeGeometry(200, 140, 8);
  const maxRadius = 140 / 2 - CANVAS_MARGIN;
  assert.ok(g.radius <= maxRadius);
  assert.ok(g.radius >= maxRadius - MAX_BIT_RADIUS - 1);
});

test("computeGeometry — bitRadius capped at MAX_BIT_RADIUS", () => {
  const g = computeGeometry(800, 800, 4);
  assert.equal(g.bitRadius, MAX_BIT_RADIUS);
});

test("computeGeometry — bitRadius shrinks with more bits", () => {
  const g8 = computeGeometry(200, 200, 8);
  const g32 = computeGeometry(200, 200, 32);
  assert.ok(g32.bitRadius < g8.bitRadius);
});

test("computeGeometry — bitRadius stays positive at MAX_LENGTH on small canvas", () => {
  const g = computeGeometry(60, 60, 32);
  assert.ok(g.bitRadius >= 1, `bitRadius=${g.bitRadius}`);
});

test("computeGeometry — length echoed in geometry", () => {
  const g = computeGeometry(200, 200, 16);
  assert.equal(g.length, 16);
});

// ---- bitAngle (CCW arrangement) -------------------------------------------

test("bitAngle — idx 0 sits at top (-pi/2)", () => {
  // Bit 0 is the read head: always under the fixed playhead triangle.
  assert.ok(Math.abs(bitAngle(0, 8) - -Math.PI / 2) < 1e-9);
});

test("bitAngle — idx 1 sits upper-LEFT (CCW from top)", () => {
  // CCW arrangement: bit 1 is one stepAngle to the LEFT of bit 0. Tells
  // the user visually that bit 1 -- the next bit to play after the
  // engine's right-shift -- is adjacent.
  const a = bitAngle(1, 8);
  const expected = -Math.PI / 2 - (Math.PI * 2) / 8;
  assert.ok(Math.abs(a - expected) < 1e-9);
});

test("bitAngle — period equals length (full revolution returns to start)", () => {
  const normalize = (x: number) =>
    ((x % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2);
  for (let len of [2, 4, 8, 16, 32]) {
    const a0 = bitAngle(0, len);
    const aFull = bitAngle(len, len);
    assert.ok(
      Math.abs(normalize(a0) - normalize(aFull)) < 1e-9,
      `len=${len}`,
    );
  }
});

// ---- bitPosition -----------------------------------------------------------

test("bitPosition — idx 0 sits at top of ring", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(0, g);
  assert.ok(Math.abs(p.x - g.cx) < 1e-9);
  assert.ok(Math.abs(p.y - (g.cy - g.radius)) < 1e-9);
});

test("bitPosition — idx 1 sits upper-LEFT (CCW)", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(1, g);
  assert.ok(p.x < g.cx, "bit 1 must be LEFT of center");
  assert.ok(p.y < g.cy, "bit 1 must be ABOVE center");
});

test("bitPosition — idx length/4 sits at LEFT (length=8 -> idx 2)", () => {
  // Quarter-turn CCW from top lands at the left edge.
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(2, g);
  assert.ok(Math.abs(p.x - (g.cx - g.radius)) < 1e-9);
  assert.ok(Math.abs(p.y - g.cy) < 1e-9);
});

test("bitPosition — idx length/2 sits at bottom", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(4, g);
  assert.ok(Math.abs(p.x - g.cx) < 1e-9);
  assert.ok(Math.abs(p.y - (g.cy + g.radius)) < 1e-9);
});

test("bitPosition — idx 3*length/4 sits at RIGHT (length=8 -> idx 6)", () => {
  const g = computeGeometry(200, 200, 8);
  const p = bitPosition(6, g);
  assert.ok(Math.abs(p.x - (g.cx + g.radius)) < 1e-9);
  assert.ok(Math.abs(p.y - g.cy) < 1e-9);
});

// ---- pointerTip ------------------------------------------------------------

test("pointerTip — sits above outermost dot edge by POINTER_GAP+HEIGHT", () => {
  const g = computeGeometry(200, 200, 8);
  const tip = pointerTip(g);
  assert.equal(tip.x, g.cx);
  assert.equal(
    tip.y,
    g.cy - g.radius - g.bitRadius - POINTER_GAP - POINTER_HEIGHT,
  );
});

test("pointerTip — sits clear of the bit dot at top", () => {
  const g = computeGeometry(200, 200, 8);
  const tip = pointerTip(g);
  const topDotUpperEdge = g.cy - g.radius - g.bitRadius;
  assert.ok(tip.y + POINTER_HEIGHT <= topDotUpperEdge);
});

test("POINTER_HALF_WIDTH stays positive (renderer expects a triangle)", () => {
  assert.ok(POINTER_HALF_WIDTH > 0);
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
  assert.equal(hitTest(0, 0, g), -1);
  assert.equal(hitTest(200, 200, g), -1);
});

test("hitTest — click at ring center returns -1", () => {
  const g = computeGeometry(200, 200, 8);
  assert.equal(hitTest(g.cx, g.cy, g), -1);
});

test("hitTest — click between two adjacent bits returns -1", () => {
  const g = computeGeometry(200, 200, 8);
  const a = bitPosition(0, g);
  const b = bitPosition(1, g);
  const mx = (a.x + b.x) / 2;
  const my = (a.y + b.y) / 2;
  assert.equal(hitTest(mx, my, g), -1);
});

test("hitTest — length=32 dots remain hit-testable at every index", () => {
  const g = computeGeometry(200, 200, 32);
  for (let i = 0; i < 32; i++) {
    const p = bitPosition(i, g);
    assert.equal(hitTest(p.x, p.y, g), i, `bit ${i} center should hit ${i}`);
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
  const m = createModel(4);
  const before = m.bits.slice();
  toggleBitAt(m, 2);
  assert.deepEqual(m.bits, before);
});

test("toggleBitAt — out-of-bounds index returns model unchanged", () => {
  const m = createModel(4);
  assert.equal(toggleBitAt(m, 4), m);
  assert.equal(toggleBitAt(m, -1), m);
  assert.equal(toggleBitAt(m, 1.5), m);
});

// ---- setRegister -----------------------------------------------------------

test("setRegister — replaces bits from Max varargs", () => {
  const m = createModel(4);
  const next = setRegister(m, [1, 0, 1, 1]);
  assert.deepEqual(next.bits, [1, 0, 1, 1]);
});

test("setRegister — sanitizes non-binary input to 0/1", () => {
  const m = createModel(4);
  const next = setRegister(m, [3, 2, 1, 0]);
  assert.deepEqual(next.bits, [1, 0, 1, 0]);
});

// ---- setHovered ------------------------------------------------------------

test("setHovered — sets hovered to given index", () => {
  const m = createModel(4);
  const next = setHovered(m, 2);
  assert.equal(next.hovered, 2);
});

test("setHovered — out-of-bounds clears hover", () => {
  const m: RingModel = { bits: [0, 0, 0, 0], hovered: 1 };
  assert.equal(setHovered(m, -1).hovered, -1);
  assert.equal(setHovered(m, 4).hovered, -1);
  assert.equal(setHovered(m, 1.5).hovered, -1);
});

test("setHovered — does not affect bits", () => {
  const base = setRegister(createModel(4), [1, 0, 1, 0]);
  const next = setHovered(base, 1);
  assert.deepEqual(next.bits, [1, 0, 1, 0]);
});

// ---- constants exported for the mirror drift test --------------------------

test("constants exported for mirror drift check", () => {
  assert.equal(typeof MIN_LENGTH, "number");
  assert.equal(typeof MAX_LENGTH, "number");
  assert.equal(typeof MAX_BIT_RADIUS, "number");
  assert.equal(typeof BIT_GAP, "number");
  assert.equal(typeof CANVAS_MARGIN, "number");
  assert.equal(typeof POINTER_GAP, "number");
  assert.equal(typeof POINTER_HALF_WIDTH, "number");
  assert.equal(typeof POINTER_HEIGHT, "number");
});
