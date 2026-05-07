// Turing Machine engine — pure functions per ADR 001.
// RNG primitives are extracted into ./rng.ts (shared cross-repo per ADR 005).
// Cross-target conformance vectors: docs/ai/turing-test-vectors.json

import { nextU32, type RngState } from "./rng.ts";

export { nextU32, seedRng, type RngState } from "./rng.ts";

export type RegisterBits = number; // u32, low `length` bits hold the state
export type Length = number; // 2..32

function maskBits(length: Length): number {
  if (length >= 32) return 0xffffffff;
  return ((1 << length) - 1) >>> 0;
}

// Probability threshold for u32-space comparison.
// rawU32 < threshold ⇔ (rawU32 / 2^32) < probability
function probabilityThreshold(p: number): number {
  if (p <= 0) return 0;
  if (p >= 1) return 0x100000000;
  return Math.floor(p * 0x100000000);
}

// ============================================================
// TM core
// ============================================================

export function createRegister(
  length: Length,
  rng: RngState,
): { register: RegisterBits; state: RngState } {
  const r = nextU32(rng);
  return { register: (r.value & maskBits(length)) >>> 0, state: r.state };
}

export function shiftAndFlip(
  register: RegisterBits,
  length: Length,
  lock: number,
  rng: RngState,
): { register: RegisterBits; state: RngState } {
  const tail = register & 1;
  const draw = nextU32(rng);
  const threshold = probabilityThreshold(1 - lock);
  const flip = draw.value < threshold;
  const writeBit = flip ? (tail ^ 1) : tail;
  const shifted = register >>> 1;
  const result = (shifted | (writeBit << (length - 1))) & maskBits(length);
  return { register: result >>> 0, state: draw.state };
}

export function shiftAndForce(
  register: RegisterBits,
  length: Length,
  forceBit: 0 | 1,
): RegisterBits {
  const shifted = register >>> 1;
  return ((shifted | ((forceBit & 1) << (length - 1))) & maskBits(length)) >>> 0;
}

export function registerToFraction(
  register: RegisterBits,
  length: Length,
): { num: number; den: number } {
  const den = length >= 32 ? 0xffffffff : (((1 << length) - 1) >>> 0);
  return { num: register, den };
}

// floor(lo + (num/den) × (hi - lo + 1)), clamped to hi.
// Computed as (num × span) / den (integer-first) to avoid float drift.
export function mapToNote(
  num: number,
  den: number,
  lo: number,
  hi: number,
): number {
  const span = hi - lo + 1;
  const offset = Math.floor((num * span) / den);
  return Math.min(lo + offset, hi);
}

// ============================================================
// Step composition
// ============================================================

export interface TmState {
  register: RegisterBits;
  rng: RngState;
}

export interface TmParams {
  length: Length;
  lock: number;
  density: number;
  range: readonly [number, number];
}

export interface TmStepResult {
  state: TmState;
  output: { note: number; active: boolean };
}

export function tmStep(state: TmState, params: TmParams): TmStepResult {
  const f = registerToFraction(state.register, params.length);
  const note = mapToNote(f.num, f.den, params.range[0], params.range[1]);
  // Density draw FIRST, then flip draw — fixed for cross-target reproducibility.
  const dDraw = nextU32(state.rng);
  const dThreshold = probabilityThreshold(params.density);
  const active = dDraw.value < dThreshold;
  const sf = shiftAndFlip(state.register, params.length, params.lock, dDraw.state);
  return {
    state: { register: sf.register, rng: sf.state },
    output: { note, active },
  };
}
