// Shared RNG primitives — xoshiro128++ (Vigna 2019) with SplitMix64 seeding.
// Cross-target conformance vectors: docs/ai/rng-test-vectors.json
// Cross-repo synchronized with Pointsman per ADR 005 §RNG sharing —
// the implementation and rng-test-vectors.json must stay byte-identical
// between the two repos.

export type RngState = readonly [number, number, number, number]; // u32 quad

const U64 = (1n << 64n) - 1n;
const U32_BIG = (1n << 32n) - 1n;

// ============================================================
// SplitMix64 (Vigna) — used only for seeding.
// ============================================================

function splitMix64Next(state: bigint): { value: bigint; state: bigint } {
  const newState = (state + 0x9e3779b97f4a7c15n) & U64;
  let z = newState;
  z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & U64;
  z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & U64;
  z = (z ^ (z >> 31n)) & U64;
  return { value: z, state: newState };
}

// Seeding convention (canonical, see rng-test-vectors.json meta):
//   call SplitMix64 twice; split each output u64 into [low32, high32].
//   s = [low(z1), high(z1), low(z2), high(z2)]
export function seedRng(seed: bigint): RngState {
  let st = seed & U64;
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

// ============================================================
// xoshiro128++ (Vigna 2019)
// ============================================================

function rotl32(x: number, k: number): number {
  return ((x << k) | (x >>> (32 - k))) >>> 0;
}

export function nextU32(rng: RngState): { value: number; state: RngState } {
  const result = ((rotl32((rng[0] + rng[3]) >>> 0, 7) + rng[0]) >>> 0);
  const t = (rng[1] << 9) >>> 0;
  let s0 = rng[0], s1 = rng[1], s2 = rng[2], s3 = rng[3];
  s2 = (s2 ^ s0) >>> 0;
  s3 = (s3 ^ s1) >>> 0;
  s1 = (s1 ^ s2) >>> 0;
  s0 = (s0 ^ s3) >>> 0;
  s2 = (s2 ^ t) >>> 0;
  s3 = rotl32(s3, 11);
  return { value: result, state: [s0, s1, s2, s3] };
}
