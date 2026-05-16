// TM register-ring jsui pure logic.
//
// Pure data + math, no Max APIs. Runs in Node for tests. Mirrored (by
// hand, ASCII-only) into registerRing.jsui.js for Max's [jsui] consumer.
// A drift test (registerRing.mirror.test.ts) asserts the named constants
// below appear in the renderer text.
//
// Static-snapshot model: the bridge publishes a fresh pre-shift register
// snapshot per step (bit 0 = the LSB the listener just heard). The
// renderer redraws on each new snapshot but does NOT animate the
// shift -- m4l's cramped 312x136 strip reads poorly under the
// anticipation ease that vst's RingView runs at 60Hz, so we snap. CCW
// bit arrangement is preserved (bit 0 top, bit 1 upper-left, ...) so
// the visual still tells the user "bit 1 is what plays next."

export type Bit = 0 | 1;

export const MIN_LENGTH = 2;
export const MAX_LENGTH = 32;

// Caps on the per-bit dot radius: anything larger reads as a button
// rather than a register bit. Anything smaller than 1 is degenerate.
export const MAX_BIT_RADIUS = 14;

// Pixel gap subtracted from the arc-half spacing when sizing dots, so
// adjacent bits never touch even at MAX_LENGTH.
export const BIT_GAP = 2;

// Padding between the canvas edge and the outermost dot edge.
export const CANVAS_MARGIN = 4;

// Gap between the outer edge of the bit dot and the tip of the fixed
// pointer triangle drawn at the top of the ring.
export const POINTER_GAP = 4;

// Pointer triangle base half-width and height in px.
export const POINTER_HALF_WIDTH = 3;
export const POINTER_HEIGHT = 6;

export interface RingModel {
  bits: Bit[];
  hovered: number;
}

export interface RingGeometry {
  cx: number;
  cy: number;
  radius: number;
  bitRadius: number;
  length: number;
}

export interface Point {
  x: number;
  y: number;
}

export function createModel(length: number): RingModel {
  const len = clampLength(length);
  const bits: Bit[] = new Array(len).fill(0) as Bit[];
  return { bits, hovered: -1 };
}

export function computeGeometry(
  canvasWidth: number,
  canvasHeight: number,
  length: number,
): RingGeometry {
  const len = clampLength(length);
  const cx = canvasWidth / 2;
  const cy = canvasHeight / 2;
  const maxRadius = Math.max(
    0,
    Math.min(canvasWidth, canvasHeight) / 2 - CANVAS_MARGIN,
  );
  // Chord-based bit sizing: adjacent bit centers sit `2*r*sin(pi/N)`
  // apart on the placement circle, NOT `2*pi*r/N` (arc length). The
  // earlier arc-length formula over-estimated spacing at small/medium
  // N and produced visually overlapping bits (worst at N around
  // 10..14 where chord/arc diverge most). Solve the constraint
  //   2*placementR*sin(pi/N) - 2*bitR >= BIT_GAP
  // with placementR = maxR - bitR:
  //   bitR <= (maxR*sin(pi/N) - BIT_GAP/2) / (1 + sin(pi/N))
  const sinHalfAngle = Math.sin(Math.PI / Math.max(len, 4));
  const bitRadius = Math.max(
    1,
    Math.min(
      MAX_BIT_RADIUS,
      (maxRadius * sinHalfAngle - BIT_GAP / 2) / (1 + sinHalfAngle),
    ),
  );
  const radius = Math.max(0, maxRadius - bitRadius);
  return { cx, cy, radius, bitRadius, length: len };
}

// Logical bit index -> on-screen angle in radians. CCW arrangement:
//   idx=0           -> -pi/2 (top, the playhead position)
//   idx=1           -> -pi/2 - 2pi/length (upper-LEFT)
//   idx=length-1    -> upper-RIGHT (one stepAngle CW from bit 0)
// This tells the user visually that bit 1 is the bit about to be played
// next (it's adjacent CW to bit 0 in the engine's right-shift order).
export function bitAngle(idx: number, length: number): number {
  const len = clampLength(length);
  return -Math.PI / 2 - (idx / len) * Math.PI * 2;
}

export function bitPosition(idx: number, geometry: RingGeometry): Point {
  const angle = bitAngle(idx, geometry.length);
  return {
    x: geometry.cx + geometry.radius * Math.cos(angle),
    y: geometry.cy + geometry.radius * Math.sin(angle),
  };
}

export function pointerTip(geometry: RingGeometry): Point {
  return {
    x: geometry.cx,
    y:
      geometry.cy -
      geometry.radius -
      geometry.bitRadius -
      POINTER_GAP -
      POINTER_HEIGHT,
  };
}

export function hitTest(
  x: number,
  y: number,
  geometry: RingGeometry,
): number {
  const r2 = geometry.bitRadius * geometry.bitRadius;
  for (let i = 0; i < geometry.length; i++) {
    const p = bitPosition(i, geometry);
    const dx = x - p.x;
    const dy = y - p.y;
    if (dx * dx + dy * dy <= r2) return i;
  }
  return -1;
}

export function toggleBitAt(model: RingModel, index: number): RingModel {
  if (
    !Number.isInteger(index) ||
    index < 0 ||
    index >= model.bits.length
  ) {
    return model;
  }
  const bits = model.bits.slice();
  bits[index] = (bits[index] === 1 ? 0 : 1) as Bit;
  return { ...model, bits };
}

export function setRegister(
  model: RingModel,
  bits: ReadonlyArray<number>,
): RingModel {
  const sanitized: Bit[] = [];
  for (let i = 0; i < bits.length; i++) {
    sanitized.push((bits[i] & 1) as Bit);
  }
  return { ...model, bits: sanitized };
}

export function setHovered(model: RingModel, index: number): RingModel {
  if (
    !Number.isInteger(index) ||
    index < 0 ||
    index >= model.bits.length
  ) {
    return { ...model, hovered: -1 };
  }
  return { ...model, hovered: index };
}

function clampLength(length: number): number {
  if (!Number.isInteger(length)) return MIN_LENGTH;
  return Math.max(MIN_LENGTH, Math.min(MAX_LENGTH, length));
}
