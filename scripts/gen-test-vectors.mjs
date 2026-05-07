// scripts/gen-test-vectors.mjs
//
// Generates docs/ai/rng-test-vectors.json and
// docs/ai/turing-test-vectors.json per ADR 001 / ADR 005.
//
// This is an INDEPENDENT reference implementation of the spec —
// deliberately separate from m4l/engine/ and vst/Source/, so the test
// vectors are not fitted to any single target's implementation. Each
// target's engine is the unit-under-test; this script's output is the spec.
//
// rng-test-vectors.json is also the cross-repo synchronization artifact
// between Stencil and Pointsman per ADR 005 §RNG sharing — both repos
// vendor identical RNG primitives and verify against byte-identical JSON.
//
// PRNG references:
//   xoshiro128++  https://prng.di.unimi.it/xoshiro128plusplus.c
//   SplitMix64    https://prng.di.unimi.it/splitmix64.c
//
// Run:  node scripts/gen-test-vectors.mjs
// Re-run any time vector cases change; do not hand-edit the JSONs.

import { writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const __dirname = dirname(fileURLToPath(import.meta.url));
const REPO = join(__dirname, "..");
const OUT_RNG = join(REPO, "docs/ai/rng-test-vectors.json");
const OUT_TM = join(REPO, "docs/ai/turing-test-vectors.json");

// ============================================================
// PRNG: SplitMix64 + xoshiro128++ (Vigna)
// ============================================================

const U64 = (1n << 64n) - 1n;
const U32_BIG = (1n << 32n) - 1n;

function splitMix64Next(state) {
  // state: bigint u64. Returns { value: u64, state: u64 } both as bigint.
  const newState = (state + 0x9e3779b97f4a7c15n) & U64;
  let z = newState;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & U64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & U64;
  z = (z ^ (z >> 31n)) & U64;
  return { value: z, state: newState };
}

// Seeding convention (CANONICAL — see meta in turing-test-vectors.json):
//   call SplitMix64 twice, splitting each u64 into [low32, high32]:
//     s = [low(z1), high(z1), low(z2), high(z2)]
function xoshiroSeed(seedU64) {
  let st = seedU64 & U64;
  const r1 = splitMix64Next(st);
  st = r1.state;
  const r2 = splitMix64Next(st);
  return [
    Number(r1.value & U32_BIG),
    Number((r1.value >> 32n) & U32_BIG),
    Number(r2.value & U32_BIG),
    Number((r2.value >> 32n) & U32_BIG),
  ];
}

function rotl32(x, k) {
  return ((x << k) | (x >>> (32 - k))) >>> 0;
}

// Pure: takes state, returns new { value, state }. Does not mutate input.
function xoshiroNext(s) {
  const result = ((rotl32((s[0] + s[3]) >>> 0, 7) + s[0]) >>> 0);
  const t = (s[1] << 9) >>> 0;
  const ns = [s[0], s[1], s[2], s[3]];
  ns[2] = (ns[2] ^ ns[0]) >>> 0;
  ns[3] = (ns[3] ^ ns[1]) >>> 0;
  ns[1] = (ns[1] ^ ns[2]) >>> 0;
  ns[0] = (ns[0] ^ ns[3]) >>> 0;
  ns[2] = (ns[2] ^ t) >>> 0;
  ns[3] = rotl32(ns[3], 11);
  return { value: result, state: ns };
}

// ============================================================
// TM ops mirroring ADR 001
// ============================================================

function maskBits(length) {
  if (length >= 32) return 0xffffffff;
  return ((1 << length) - 1) >>> 0;
}

// Threshold for u32-space probability comparison.
//   rawU32 < threshold  ⇔  (rawU32 / 2^32) < probability
// Boundary: probability=0 → threshold=0 (no rawU32 satisfies, never)
//           probability=1 → threshold=2^32 (every rawU32 satisfies, always)
function probabilityThreshold(p) {
  if (p <= 0) return 0;
  if (p >= 1) return 0x100000000;
  return Math.floor(p * 0x100000000);
}

// One xoshiro draw, low `length` bits → register. Documented convention.
function createRegister(length, s) {
  const r = xoshiroNext(s);
  return { register: (r.value & maskBits(length)) >>> 0, state: r.state };
}

function shiftAndFlip(register, length, lock, s) {
  const tail = register & 1;
  const draw = xoshiroNext(s);
  const threshold = probabilityThreshold(1 - lock);
  const flip = draw.value < threshold;
  const writeBit = flip ? (tail ^ 1) : tail;
  const shifted = register >>> 1;
  const result = (shifted | (writeBit << (length - 1))) & maskBits(length);
  return {
    register: result >>> 0,
    state: draw.state,
    rng_draw_u32: draw.value,
    flipped: flip,
  };
}

function shiftAndForce(register, length, forceBit) {
  const shifted = register >>> 1;
  const result = (shifted | ((forceBit & 1) << (length - 1))) & maskBits(length);
  return result >>> 0;
}

function registerToFraction(register, length) {
  const den = length >= 32 ? 0xffffffff : (((1 << length) - 1) >>> 0);
  return { num: register, den };
}

function mapToNote(num, den, lo, hi) {
  // floor(lo + (num/den) × (hi - lo + 1)), clamped to hi
  const span = hi - lo + 1;
  const offset = Math.floor((num * span) / den);
  return Math.min(lo + offset, hi);
}

function tmStep(state, params) {
  // state: { register, rng }
  // params: { length, lock, density, range: [lo, hi] }
  const f = registerToFraction(state.register, params.length);
  const note = mapToNote(f.num, f.den, params.range[0], params.range[1]);
  const dDraw = xoshiroNext(state.rng);
  const dThreshold = probabilityThreshold(params.density);
  const active = dDraw.value < dThreshold;
  const sf = shiftAndFlip(state.register, params.length, params.lock, dDraw.state);
  return {
    state: { register: sf.register, rng: sf.state },
    output: { note, active },
  };
}

// ============================================================
// Helpers for emission
// ============================================================

function hexU32(n) {
  return "0x" + (n >>> 0).toString(16).padStart(8, "0");
}
function hexU64(big) {
  return "0x" + (big & U64).toString(16).padStart(16, "0");
}
// JSON-friendly seed encoding: decimal string + hex form.
function seedField(big) {
  return { decimal: big.toString(), hex: hexU64(big) };
}

// ============================================================
// TM cases
// ============================================================

const TM_SEEDS = [0n, 1n, 0xdeadbeefn, 0x123456789abcdef0n];

function genSplitMix64InitCases() {
  return TM_SEEDS.map((seed) => {
    const r1 = splitMix64Next(seed);
    const r2 = splitMix64Next(r1.state);
    const sm_outputs = [
      { value_hex: hexU64(r1.value), value_decimal: r1.value.toString() },
      { value_hex: hexU64(r2.value), value_decimal: r2.value.toString() },
    ];
    const s = xoshiroSeed(seed);
    return {
      seed: seedField(seed),
      splitmix64_outputs: sm_outputs,
      xoshiro_state_s: s.map((w) => ({ hex: hexU32(w), decimal: w })),
    };
  });
}

function genPrngCases() {
  const N_DRAWS = 8;
  return TM_SEEDS.map((seed) => {
    let s = xoshiroSeed(seed);
    const draws = [];
    for (let i = 0; i < N_DRAWS; i++) {
      const r = xoshiroNext(s);
      s = r.state;
      draws.push({ hex: hexU32(r.value), decimal: r.value });
    }
    return { seed: seedField(seed), draws };
  });
}

function genRegisterInitCases() {
  // Cover length boundaries: 2 (min), 8 (typical), 16, 31, 32 (max)
  const cases = [];
  for (const seed of TM_SEEDS) {
    for (const length of [2, 8, 16, 31, 32]) {
      const s = xoshiroSeed(seed);
      const r = createRegister(length, s);
      cases.push({
        seed: seedField(seed),
        length,
        register: r.register,
        register_hex: hexU32(r.register),
      });
    }
  }
  return cases;
}

function genRegisterToFractionCases() {
  // Exact rational form num/den. Edge cases: zero, all-ones, alternating.
  const cases = [];
  const samples = [
    [2, 0], [2, 1], [2, 3],
    [8, 0], [8, 1], [8, 0xaa], [8, 0x55], [8, 0xff],
    [16, 0], [16, 0xffff],
    [32, 0], [32, 0xffffffff],
  ];
  for (const [length, register] of samples) {
    const f = registerToFraction(register, length);
    cases.push({
      register,
      register_hex: hexU32(register),
      length,
      fraction: { num: f.num, den: f.den },
    });
  }
  return cases;
}

function genMapToNoteCases() {
  // The fraction is (num/den) ∈ [0, 1]. Cases drive boundary + clamp +
  // single-note range (lo == hi).
  const cases = [
    // fraction = 0 → lo
    { num: 0, den: 1, range: [60, 72], note: 60 },
    // fraction = 1 → hi (clamp)
    { num: 1, den: 1, range: [60, 72], note: 72 },
    // fraction = 1/2 → midpoint (60 + floor(0.5 × 13) = 60 + 6 = 66)
    { num: 1, den: 2, range: [60, 72], note: 66 },
    // fraction = 1/3 → 60 + floor(13/3) = 60 + 4 = 64
    { num: 1, den: 3, range: [60, 72], note: 64 },
    // fraction = 2/3 → 60 + floor(26/3) = 60 + 8 = 68
    { num: 2, den: 3, range: [60, 72], note: 68 },
    // single-note range: lo == hi → always that note
    { num: 0, den: 1, range: [60, 60], note: 60 },
    { num: 1, den: 1, range: [60, 60], note: 60 },
    { num: 7, den: 9, range: [60, 60], note: 60 },
    // full MIDI range
    { num: 0, den: 1, range: [0, 127], note: 0 },
    { num: 1, den: 1, range: [0, 127], note: 127 },
    { num: 1, den: 2, range: [0, 127], note: 64 }, // 0 + floor(128/2) = 64
    // 8-bit register all-ones: num=255, den=255 → fraction=1.0, clamp to hi
    { num: 255, den: 255, range: [60, 72], note: 72 },
    // 8-bit alternating bits: num=170 (0xAA), den=255 → 60 + floor(170*13/255) = 60 + 8 = 68
    { num: 170, den: 255, range: [60, 72], note: 68 },
  ];
  return cases;
}

function genShiftAndFlipCases() {
  // Two regimes:
  //   (A) lock ∈ {0.0, 1.0} — deterministic regardless of draw value
  //   (B) lock ∈ (0,1) — outcome depends on the seeded first draw; we
  //       compute and record the actual draw + outcome
  const cases = [];
  // Regime A: lock = 1.0 (never flip — write tail unchanged)
  for (const [register, length] of [[0xb3, 8], [0x55, 8], [0x00, 8], [0xff, 8], [0x3, 2]]) {
    const r = shiftAndFlip(register, length, 1.0, xoshiroSeed(1n));
    cases.push({
      label: `lock=1.0 register=${hexU32(register)} length=${length} (never flip)`,
      seed: seedField(1n),
      register,
      register_hex: hexU32(register),
      length,
      lock: 1.0,
      register_after: r.register,
      register_after_hex: hexU32(r.register),
      rng_draw_u32: r.rng_draw_u32,
      rng_draw_hex: hexU32(r.rng_draw_u32),
      flipped: r.flipped,
    });
  }
  // Regime A: lock = 0.0 (always flip — write tail XOR 1)
  for (const [register, length] of [[0xb3, 8], [0x55, 8], [0x00, 8], [0xff, 8], [0x3, 2]]) {
    const r = shiftAndFlip(register, length, 0.0, xoshiroSeed(1n));
    cases.push({
      label: `lock=0.0 register=${hexU32(register)} length=${length} (always flip)`,
      seed: seedField(1n),
      register,
      register_hex: hexU32(register),
      length,
      lock: 0.0,
      register_after: r.register,
      register_after_hex: hexU32(r.register),
      rng_draw_u32: r.rng_draw_u32,
      rng_draw_hex: hexU32(r.rng_draw_u32),
      flipped: r.flipped,
    });
  }
  // Regime B: intermediate lock — draw-dependent. Exercises the comparison.
  for (const seed of [1n, 0xdeadbeefn]) {
    for (const lock of [0.25, 0.5, 0.75]) {
      const r = shiftAndFlip(0xb3, 8, lock, xoshiroSeed(seed));
      cases.push({
        label: `lock=${lock} register=0xb3 length=8 seed=${hexU64(seed)}`,
        seed: seedField(seed),
        register: 0xb3,
        register_hex: hexU32(0xb3),
        length: 8,
        lock,
        register_after: r.register,
        register_after_hex: hexU32(r.register),
        rng_draw_u32: r.rng_draw_u32,
        rng_draw_hex: hexU32(r.rng_draw_u32),
        flipped: r.flipped,
      });
    }
  }
  // length=32 boundary case under lock=0
  {
    const reg = 0xdeadbeef >>> 0;
    const r = shiftAndFlip(reg, 32, 0.0, xoshiroSeed(1n));
    cases.push({
      label: `lock=0.0 register=0xdeadbeef length=32 (mask boundary)`,
      seed: seedField(1n),
      register: reg,
      register_hex: hexU32(reg),
      length: 32,
      lock: 0.0,
      register_after: r.register,
      register_after_hex: hexU32(r.register),
      rng_draw_u32: r.rng_draw_u32,
      rng_draw_hex: hexU32(r.rng_draw_u32),
      flipped: r.flipped,
    });
  }
  return cases;
}

function genShiftAndForceCases() {
  const cases = [];
  // length=8: noteOn (force=1) and noteOff (force=0) on assorted registers
  for (const register of [0x00, 0xff, 0xb3, 0x55, 0xaa]) {
    for (const forceBit of [0, 1]) {
      const r = shiftAndForce(register, 8, forceBit);
      cases.push({
        label: `register=${hexU32(register)} length=8 force=${forceBit}`,
        register,
        register_hex: hexU32(register),
        length: 8,
        force_bit: forceBit,
        register_after: r,
        register_after_hex: hexU32(r),
      });
    }
  }
  // length=2 boundary
  for (const register of [0b00, 0b01, 0b10, 0b11]) {
    for (const forceBit of [0, 1]) {
      const r = shiftAndForce(register, 2, forceBit);
      cases.push({
        label: `register=${register} length=2 force=${forceBit}`,
        register,
        register_hex: hexU32(register),
        length: 2,
        force_bit: forceBit,
        register_after: r,
        register_after_hex: hexU32(r),
      });
    }
  }
  // length=32 boundary
  {
    const r = shiftAndForce(0xffffffff, 32, 0);
    cases.push({
      label: `register=0xffffffff length=32 force=0`,
      register: 0xffffffff,
      register_hex: hexU32(0xffffffff),
      length: 32,
      force_bit: 0,
      register_after: r,
      register_after_hex: hexU32(r),
    });
  }
  return cases;
}

function genTmStepCases() {
  const cases = [];
  const scenarios = [
    {
      name: "perfect loop (lock=1.0, density=1.0)",
      seed: 1n, length: 8, lock: 1.0, density: 1.0, range: [60, 72], n_steps: 16,
    },
    {
      name: "no lock (lock=0.0) — pure walker",
      seed: 1n, length: 8, lock: 0.0, density: 1.0, range: [60, 72], n_steps: 16,
    },
    {
      name: "intermediate lock (0.5)",
      seed: 1n, length: 8, lock: 0.5, density: 1.0, range: [60, 72], n_steps: 16,
    },
    {
      name: "density=0 — every step inactive, register still evolves",
      seed: 1n, length: 8, lock: 0.5, density: 0.0, range: [60, 72], n_steps: 8,
    },
    {
      name: "density=0.5 — half of steps active (probabilistically)",
      seed: 0xdeadbeefn, length: 8, lock: 0.95, density: 0.5, range: [60, 72], n_steps: 16,
    },
    {
      name: "single-note range — note always == lo (= hi)",
      seed: 1n, length: 8, lock: 0.5, density: 1.0, range: [60, 60], n_steps: 8,
    },
    {
      name: "length=2 minimum",
      seed: 1n, length: 2, lock: 0.5, density: 1.0, range: [60, 67], n_steps: 8,
    },
    {
      name: "length=32 maximum",
      seed: 1n, length: 32, lock: 0.95, density: 1.0, range: [0, 127], n_steps: 8,
    },
  ];
  for (const sc of scenarios) {
    const initialRng = xoshiroSeed(sc.seed);
    const init = createRegister(sc.length, initialRng);
    let state = { register: init.register, rng: init.state };
    const params = { length: sc.length, lock: sc.lock, density: sc.density, range: sc.range };
    const trace = [];
    for (let i = 0; i < sc.n_steps; i++) {
      const before = state;
      const r = tmStep(state, params);
      trace.push({
        step: i,
        register_in: before.register,
        register_in_hex: hexU32(before.register),
        note: r.output.note,
        active: r.output.active,
        register_out: r.state.register,
        register_out_hex: hexU32(r.state.register),
      });
      state = r.state;
    }
    cases.push({
      name: sc.name,
      seed: seedField(sc.seed),
      length: sc.length,
      lock: sc.lock,
      density: sc.density,
      range: sc.range,
      n_steps: sc.n_steps,
      initial_register: init.register,
      initial_register_hex: hexU32(init.register),
      trace,
    });
  }
  return cases;
}

// ============================================================
// Compose JSONs
// ============================================================

const rngJson = {
  spec:
    "ADR 001 / ADR 005 RNG conformance vectors. Cross-repo synchronized " +
    "between Stencil and Pointsman per ADR 005 §RNG sharing — both repos " +
    "must vendor byte-identical copies of this file.",
  generated_by: "scripts/gen-test-vectors.mjs",
  generator_note:
    "Re-run scripts/gen-test-vectors.mjs to regenerate. Do not hand-edit. " +
    "The seed/step prefix here is replicated inside turing-test-vectors.json " +
    "(TM uses these seeds directly); the two files must agree on shared rows.",
  meta: {
    prng: {
      algorithm: "xoshiro128++ (Vigna 2019)",
      reference: "https://prng.di.unimi.it/xoshiro128plusplus.c",
      state_words: 4,
      state_word_bits: 32,
      output_bits: 32,
    },
    seeding: {
      algorithm: "SplitMix64 (Vigna)",
      reference: "https://prng.di.unimi.it/splitmix64.c",
      convention:
        "From a u64 seed, call SplitMix64 twice. Split each output u64 " +
        "into [low32, high32]. The xoshiro128++ state is " +
        "[low(z1), high(z1), low(z2), high(z2)].",
    },
  },
  splitmix64_init: genSplitMix64InitCases(),
  prng: genPrngCases(),
};

const tmJson = {
  spec: "ADR 001 Turing Machine engine conformance vectors",
  generated_by: "scripts/gen-test-vectors.mjs",
  generator_note:
    "Re-run scripts/gen-test-vectors.mjs to regenerate. Do not hand-edit. " +
    "This file is the cross-target spec — both m4l/engine and vst/Source " +
    "engines must produce values that match these cases bit-for-bit.",
  meta: {
    prng: {
      algorithm: "xoshiro128++ (Vigna 2019)",
      reference: "https://prng.di.unimi.it/xoshiro128plusplus.c",
      state_words: 4,
      state_word_bits: 32,
      output_bits: 32,
    },
    seeding: {
      algorithm: "SplitMix64 (Vigna)",
      reference: "https://prng.di.unimi.it/splitmix64.c",
      convention:
        "From a u64 seed, call SplitMix64 twice. Split each output u64 " +
        "into [low32, high32]. The xoshiro128++ state is " +
        "[low(z1), high(z1), low(z2), high(z2)].",
    },
    create_register: {
      convention:
        "One xoshiro128++ draw; the low `length` bits of the resulting u32 " +
        "form the initial register. Bits at positions ≥ length are masked " +
        "to zero. This consumes a single PRNG step regardless of length.",
    },
    flip_decision: {
      rule:
        "Draw u32 from xoshiro128++. flip ⇔ rawU32 < threshold where " +
        "threshold = floor((1 - lock) × 2^32). lock=1 → threshold=0 (never " +
        "flip); lock=0 → threshold=2^32 (always flip). Comparison done in " +
        "u32 space to avoid float-rounding divergence between targets.",
    },
    density_decision: {
      rule:
        "Draw u32 from xoshiro128++ before the flip draw. active ⇔ rawU32 < " +
        "threshold where threshold = floor(density × 2^32). density=0 → " +
        "threshold=0 (never active); density=1 → threshold=2^32 (always " +
        "active).",
    },
    tm_step: {
      draw_order: "density_draw_first, then flip_draw",
      output_ordering:
        "register is read for the output note BEFORE shiftAndFlip mutates " +
        "it. Step n's emitted note reflects register state at the start " +
        "of step n.",
    },
  },
  splitmix64_init: genSplitMix64InitCases(),
  prng: genPrngCases(),
  register_init: genRegisterInitCases(),
  register_to_fraction: genRegisterToFractionCases(),
  map_to_note: genMapToNoteCases(),
  shift_and_flip: genShiftAndFlipCases(),
  shift_and_force: genShiftAndForceCases(),
  tm_step: genTmStepCases(),
};

writeFileSync(OUT_RNG, JSON.stringify(rngJson, null, 2) + "\n");
writeFileSync(OUT_TM, JSON.stringify(tmJson, null, 2) + "\n");

console.log(`wrote ${OUT_RNG}`);
console.log(`wrote ${OUT_TM}`);
console.log(`rng sections: splitmix=${rngJson.splitmix64_init.length}, prng=${rngJson.prng.length}`);
console.log(`tm sections: prng=${tmJson.prng.length}, splitmix=${tmJson.splitmix64_init.length}, ` +
  `register_init=${tmJson.register_init.length}, fraction=${tmJson.register_to_fraction.length}, ` +
  `map=${tmJson.map_to_note.length}, flip=${tmJson.shift_and_flip.length}, ` +
  `force=${tmJson.shift_and_force.length}, step=${tmJson.tm_step.length}`);
