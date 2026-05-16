import { test } from "node:test";
import assert from "node:assert/strict";

import {
  HIT_RADIUS_MULT,
  MAX_VALUE,
  MIN_VALUE,
  THUMB_RADIUS,
  TRACK_HEIGHT,
  TRACK_PAD,
  applyDrag,
  clampValue,
  pickThumb,
  trackBounds,
  valueToY,
  yToValue,
} from "./rangeSlider.logic.ts";

// ---- trackBounds ----------------------------------------------------------

test("trackBounds — padded on both vertical ends so thumbs don't clip", () => {
  const tr = trackBounds(48, 200);
  assert.equal(tr.top, TRACK_PAD);
  assert.equal(tr.bottom, 200 - TRACK_PAD);
  assert.equal(tr.height, 200 - 2 * TRACK_PAD);
});

test("trackBounds — cx at horizontal midpoint", () => {
  const tr = trackBounds(48, 200);
  assert.equal(tr.cx, 24);
});

test("trackBounds — degenerate short canvas yields non-negative height", () => {
  const tr = trackBounds(48, 2 * TRACK_PAD - 4);
  assert.ok(tr.height >= 0);
  assert.ok(tr.bottom >= tr.top);
});

// ---- valueToY / yToValue --------------------------------------------------

// HI at top, LO at bottom: pitch=up intuition (higher MIDI note = visually
// higher on screen). MAX_VALUE -> top, MIN_VALUE -> bottom.

test("valueToY — MIN_VALUE maps to track bottom edge", () => {
  const tr = trackBounds(48, 200);
  assert.equal(valueToY(MIN_VALUE, tr), tr.bottom);
});

test("valueToY — MAX_VALUE maps to track top edge", () => {
  const tr = trackBounds(48, 200);
  assert.equal(valueToY(MAX_VALUE, tr), tr.top);
});

test("yToValue — top edge maps to MAX_VALUE, bottom edge to MIN_VALUE", () => {
  const tr = trackBounds(48, 200);
  assert.equal(yToValue(tr.top, tr), MAX_VALUE);
  assert.equal(yToValue(tr.bottom, tr), MIN_VALUE);
});

test("yToValue — clamps clicks outside the track", () => {
  const tr = trackBounds(48, 200);
  // y < top (above canvas) clamps to MAX, y > bottom clamps to MIN
  assert.equal(yToValue(-100, tr), MAX_VALUE);
  assert.equal(yToValue(9999, tr), MIN_VALUE);
});

test("valueToY -> yToValue round-trips at every integer MIDI note", () => {
  // Tall canvas: 400 px gives ~3.1 px per MIDI step, well above the
  // sub-pixel rounding threshold so each step is independently
  // representable.
  const tr = trackBounds(48, 400);
  for (let v = MIN_VALUE; v <= MAX_VALUE; v++) {
    assert.equal(yToValue(valueToY(v, tr), tr), v, `value=${v}`);
  }
});

// ---- clampValue -----------------------------------------------------------

test("clampValue — out-of-range values clamp to nearest endpoint", () => {
  assert.equal(clampValue(-10), MIN_VALUE);
  assert.equal(clampValue(200), MAX_VALUE);
});

test("clampValue — non-finite input falls to MIN_VALUE", () => {
  assert.equal(clampValue(NaN), MIN_VALUE);
  assert.equal(clampValue(Infinity), MIN_VALUE);
});

test("clampValue — rounds float input to nearest integer", () => {
  assert.equal(clampValue(60.4), 60);
  assert.equal(clampValue(60.6), 61);
});

// ---- pickThumb ------------------------------------------------------------

test("pickThumb — click on LO thumb returns 0", () => {
  const tr = trackBounds(48, 200);
  const loY = valueToY(48, tr);
  assert.equal(pickThumb(loY, 48, 72, tr), 0);
});

test("pickThumb — click on HI thumb returns 1", () => {
  const tr = trackBounds(48, 200);
  const hiY = valueToY(72, tr);
  assert.equal(pickThumb(hiY, 48, 72, tr), 1);
});

test("pickThumb — click far from both thumbs returns -1", () => {
  const tr = trackBounds(48, 400);
  // Midpoint of full-range [0, 127]: y at value 63. Each thumb is at an
  // endpoint, ~200 px away — well outside hit threshold (5 * 2 = 10 px).
  const farY = valueToY(63, tr);
  assert.equal(pickThumb(farY, 0, 127, tr), -1);
});

test("pickThumb — equidistant click prefers LO", () => {
  const tr = trackBounds(48, 200);
  const y = valueToY(60, tr);
  assert.equal(pickThumb(y, 60, 60, tr), 0);
});

// ---- applyDrag ------------------------------------------------------------

test("applyDrag — dragging LO updates only LO (HI unchanged when valid)", () => {
  const tr = trackBounds(48, 200);
  const y = valueToY(50, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 0, y, tr);
  assert.equal(next.lo, 50);
  assert.equal(next.hi, 72);
});

test("applyDrag — dragging HI updates only HI (LO unchanged when valid)", () => {
  const tr = trackBounds(48, 200);
  const y = valueToY(80, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 1, y, tr);
  assert.equal(next.lo, 48);
  assert.equal(next.hi, 80);
});

test("applyDrag — LO dragged past HI pulls HI along (lo <= hi invariant)", () => {
  const tr = trackBounds(48, 200);
  const y = valueToY(90, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 0, y, tr);
  assert.equal(next.lo, 90);
  assert.equal(next.hi, 90);
});

test("applyDrag — HI dragged below LO pulls LO along", () => {
  const tr = trackBounds(48, 200);
  const y = valueToY(30, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 1, y, tr);
  assert.equal(next.lo, 30);
  assert.equal(next.hi, 30);
});

test("applyDrag — drag outside canvas clamps to range bounds", () => {
  const tr = trackBounds(48, 200);
  // LO dragged below canvas (y > bottom) -> MIN_VALUE
  const a = applyDrag({ lo: 48, hi: 72 }, 0, 9999, tr);
  assert.equal(a.lo, MIN_VALUE);
  // HI dragged above canvas (y < top) -> MAX_VALUE
  const b = applyDrag({ lo: 48, hi: 72 }, 1, -50, tr);
  assert.equal(b.hi, MAX_VALUE);
});

// ---- constants ------------------------------------------------------------

test("constants exported for mirror drift check", () => {
  for (const c of [MIN_VALUE, MAX_VALUE, THUMB_RADIUS, TRACK_PAD, TRACK_HEIGHT, HIT_RADIUS_MULT]) {
    assert.equal(typeof c, "number");
  }
});
