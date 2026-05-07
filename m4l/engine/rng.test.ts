import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import { nextU32, seedRng, type RngState } from "./rng.ts";

const __dirname = dirname(fileURLToPath(import.meta.url));
const VECTORS_PATH = join(__dirname, "../../docs/ai/rng-test-vectors.json");
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const V: any = JSON.parse(readFileSync(VECTORS_PATH, "utf8"));

test("xoshiro state words after SplitMix64 seeding", () => {
  for (const tc of V.splitmix64_init) {
    const seed = BigInt(tc.seed.decimal);
    const s = seedRng(seed);
    const expected = tc.xoshiro_state_s.map(
      (w: { decimal: number }) => w.decimal >>> 0,
    );
    assert.deepEqual([s[0], s[1], s[2], s[3]], expected,
      `seed=${tc.seed.hex}`);
  }
});

test("xoshiro128++ first-N draws match vectors", () => {
  for (const tc of V.prng) {
    const seed = BigInt(tc.seed.decimal);
    let rng: RngState = seedRng(seed);
    for (let i = 0; i < tc.draws.length; i++) {
      const r = nextU32(rng);
      assert.equal(
        r.value >>> 0,
        tc.draws[i].decimal >>> 0,
        `seed=${tc.seed.hex} draw[${i}]`,
      );
      rng = r.state;
    }
  }
});
