// Two-thumb MIDI-range slider jsui pure logic.
//
// Vertical orientation: HI at top of canvas, LO at bottom, so the visual
// position matches pitch=up intuition (higher MIDI note number = visually
// higher on screen). Pure data + math, no Max APIs. Runs in Node for
// tests. Mirrored (by hand, ASCII-only) into rangeSlider.jsui.js for
// Max's [jsui] consumer. A drift test (rangeSlider.mirror.test.ts)
// asserts the named constants below appear in the renderer text.

export const MIN_VALUE = 0;
export const MAX_VALUE = 127;

// Thumb radius in pixels. Small to match Live's native widget scale.
export const THUMB_RADIUS = 5;

// Vertical padding so thumb edges don't clip the canvas top/bottom
// edges at the value extremes (LO=0 or HI=127). Must be >= THUMB_RADIUS.
export const TRACK_PAD = 6;

// Track line thickness (perpendicular to the value axis).
export const TRACK_HEIGHT = 3;

// Hit threshold multiplier on THUMB_RADIUS. >1 because users miss
// slightly on small targets.
export const HIT_RADIUS_MULT = 2;

export interface TrackBounds {
  top: number;
  bottom: number;
  cx: number;
  height: number;
}

export function trackBounds(
  canvasWidth: number,
  canvasHeight: number,
): TrackBounds {
  const top = TRACK_PAD;
  const bottom = Math.max(top, canvasHeight - TRACK_PAD);
  return {
    top,
    bottom,
    cx: canvasWidth / 2,
    height: bottom - top,
  };
}

export function valueToY(value: number, tr: TrackBounds): number {
  // High value at top (y=tr.top), low value at bottom (y=tr.bottom).
  // Range-clamp but do NOT round -- visual position is continuous so the
  // slider can render automation-interpolated values smoothly. yToValue
  // is the only path that rounds (user-input return path).
  let v = value;
  if (!Number.isFinite(v) || v < MIN_VALUE) v = MIN_VALUE;
  else if (v > MAX_VALUE) v = MAX_VALUE;
  const t = (v - MIN_VALUE) / (MAX_VALUE - MIN_VALUE);
  return tr.bottom - t * tr.height;
}

export function yToValue(y: number, tr: TrackBounds): number {
  if (tr.height <= 0) return MIN_VALUE;
  let t = (tr.bottom - y) / tr.height;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return Math.round(MIN_VALUE + t * (MAX_VALUE - MIN_VALUE));
}

export function clampValue(v: number): number {
  if (!Number.isFinite(v)) return MIN_VALUE;
  if (v < MIN_VALUE) return MIN_VALUE;
  if (v > MAX_VALUE) return MAX_VALUE;
  return Math.round(v);
}

// Which thumb does the click belong to? Returns 0 (LO), 1 (HI), or -1
// (no thumb in range). Equidistant clicks default to LO so the lower
// endpoint gets priority when lo == hi.
export function pickThumb(
  y: number,
  lo: number,
  hi: number,
  tr: TrackBounds,
): -1 | 0 | 1 {
  const loY = valueToY(lo, tr);
  const hiY = valueToY(hi, tr);
  const dLo = Math.abs(y - loY);
  const dHi = Math.abs(y - hiY);
  const threshold = THUMB_RADIUS * HIT_RADIUS_MULT;
  if (dLo > threshold && dHi > threshold) return -1;
  return dLo <= dHi ? 0 : 1;
}

export interface RangeState {
  lo: number;
  hi: number;
}

// Apply a drag event: given current state + which thumb is being
// dragged + cursor y, return the new state. Enforces lo <= hi --
// dragging LO above HI pulls HI along, and vice versa.
export function applyDrag(
  state: RangeState,
  thumb: 0 | 1,
  y: number,
  tr: TrackBounds,
): RangeState {
  const v = yToValue(y, tr);
  if (thumb === 0) {
    const lo = v;
    const hi = Math.max(state.hi, lo);
    return { lo, hi };
  }
  const hi = v;
  const lo = Math.min(state.lo, hi);
  return { lo, hi };
}
