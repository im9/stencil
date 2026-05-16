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
  valueToX,
  xToValue,
} from "./rangeSlider.logic.ts";

// ---- trackBounds ----------------------------------------------------------

test("trackBounds — padded on both ends so thumbs don't clip", () => {
  const tr = trackBounds(200, 20);
  assert.equal(tr.left, TRACK_PAD);
  assert.equal(tr.right, 200 - TRACK_PAD);
  assert.equal(tr.width, 200 - 2 * TRACK_PAD);
});

test("trackBounds — cy at vertical midpoint", () => {
  const tr = trackBounds(200, 20);
  assert.equal(tr.cy, 10);
});

test("trackBounds — degenerate narrow canvas yields non-negative width", () => {
  const tr = trackBounds(2 * TRACK_PAD - 4, 20);
  assert.ok(tr.width >= 0);
  assert.ok(tr.right >= tr.left);
});

// ---- valueToX / xToValue --------------------------------------------------

test("valueToX — MIN_VALUE maps to track left edge", () => {
  const tr = trackBounds(200, 20);
  assert.equal(valueToX(MIN_VALUE, tr), tr.left);
});

test("valueToX — MAX_VALUE maps to track right edge", () => {
  const tr = trackBounds(200, 20);
  assert.equal(valueToX(MAX_VALUE, tr), tr.right);
});

test("xToValue — left edge maps to MIN_VALUE, right edge to MAX_VALUE", () => {
  const tr = trackBounds(200, 20);
  assert.equal(xToValue(tr.left, tr), MIN_VALUE);
  assert.equal(xToValue(tr.right, tr), MAX_VALUE);
});

test("xToValue — clamps clicks outside the track", () => {
  const tr = trackBounds(200, 20);
  assert.equal(xToValue(-100, tr), MIN_VALUE);
  assert.equal(xToValue(9999, tr), MAX_VALUE);
});

test("valueToX -> xToValue round-trips at every integer MIDI note", () => {
  const tr = trackBounds(400, 20);
  for (let v = MIN_VALUE; v <= MAX_VALUE; v++) {
    assert.equal(xToValue(valueToX(v, tr), tr), v, `value=${v}`);
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
  const tr = trackBounds(200, 20);
  const loX = valueToX(48, tr);
  assert.equal(pickThumb(loX, 48, 72, tr), 0);
});

test("pickThumb — click on HI thumb returns 1", () => {
  const tr = trackBounds(200, 20);
  const hiX = valueToX(72, tr);
  assert.equal(pickThumb(hiX, 48, 72, tr), 1);
});

test("pickThumb — click far from both thumbs returns -1", () => {
  const tr = trackBounds(400, 20);
  const farX = tr.left + tr.width / 2;
  assert.equal(pickThumb(farX, 0, 127, tr), -1);
});

test("pickThumb — equidistant click prefers LO", () => {
  const tr = trackBounds(200, 20);
  const x = valueToX(60, tr);
  assert.equal(pickThumb(x, 60, 60, tr), 0);
});

// ---- applyDrag ------------------------------------------------------------

test("applyDrag — dragging LO updates only LO (HI unchanged when valid)", () => {
  const tr = trackBounds(200, 20);
  const x = valueToX(50, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 0, x, tr);
  assert.equal(next.lo, 50);
  assert.equal(next.hi, 72);
});

test("applyDrag — dragging HI updates only HI (LO unchanged when valid)", () => {
  const tr = trackBounds(200, 20);
  const x = valueToX(80, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 1, x, tr);
  assert.equal(next.lo, 48);
  assert.equal(next.hi, 80);
});

test("applyDrag — LO dragged past HI pulls HI along (lo <= hi invariant)", () => {
  const tr = trackBounds(200, 20);
  const x = valueToX(90, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 0, x, tr);
  assert.equal(next.lo, 90);
  assert.equal(next.hi, 90);
});

test("applyDrag — HI dragged below LO pulls LO along", () => {
  const tr = trackBounds(200, 20);
  const x = valueToX(30, tr);
  const next = applyDrag({ lo: 48, hi: 72 }, 1, x, tr);
  assert.equal(next.lo, 30);
  assert.equal(next.hi, 30);
});

test("applyDrag — drag outside canvas clamps to range bounds", () => {
  const tr = trackBounds(200, 20);
  const a = applyDrag({ lo: 48, hi: 72 }, 0, -50, tr);
  assert.equal(a.lo, MIN_VALUE);
  const b = applyDrag({ lo: 48, hi: 72 }, 1, 9999, tr);
  assert.equal(b.hi, MAX_VALUE);
});

// ---- constants ------------------------------------------------------------

test("constants exported for mirror drift check", () => {
  for (const c of [MIN_VALUE, MAX_VALUE, THUMB_RADIUS, TRACK_PAD, TRACK_HEIGHT, HIT_RADIUS_MULT]) {
    assert.equal(typeof c, "number");
  }
});
