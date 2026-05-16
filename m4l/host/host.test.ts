import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import { DEFAULT_PARAMS, TmHost, type HostParams, type NoteEvent } from "./host.ts";
import { createRegister, seedRng, shiftAndForce } from "../engine/turing.ts";

const __dirname = dirname(fileURLToPath(import.meta.url));
const VECTORS_PATH = join(
  __dirname,
  "../../docs/ai/turing-test-vectors.json",
);
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const V: any = JSON.parse(readFileSync(VECTORS_PATH, "utf8"));

function makeHost(overrides: Partial<HostParams> = {}): TmHost {
  return new TmHost({ ...DEFAULT_PARAMS, ...overrides });
}

function notesFromEvents(events: NoteEvent[]): {
  noteOns: NoteEvent[];
  noteOffs: NoteEvent[];
} {
  return {
    noteOns: events.filter((e) => e.type === "noteOn"),
    noteOffs: events.filter((e) => e.type === "noteOff"),
  };
}

test("constructor — deterministic initial register from seed", () => {
  const host = makeHost({ seed: 1, length: 8 });
  // Match the engine's createRegister(8, seedRng(1n)) — verified via vectors
  // register_init case for seed=1 length=8.
  const expected = V.register_init.find(
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (c: any) => c.length === 8 && c.seed.decimal === "1",
  );
  assert.ok(expected, "vectors must include seed=1 length=8");
  assert.equal(host.getRegister(), expected.register);
  assert.equal(host.getPosition(), 0);
});

test("step auto mode density=1.0 + bit0=1 — emits noteOn + delayed noteOff", () => {
  // Under the bit-tap gate (vst spec 2026-05-16): density=1 + bit0=1 fires;
  // bit0 must be 1 for the gate to even consider the density draw, so the
  // test forces bit0 via setBit before stepping.
  const host = makeHost({
    seed: 1,
    length: 8,
    lock: 1.0,
    density: 1.0,
    rangeLo: 60,
    rangeHi: 72,
    outputVelocity: 100,
    outputGate: 0.5,
    outputChannel: 1,
  });
  host.setBit(0, 1);
  const events = host.step(0);
  const { noteOns, noteOffs } = notesFromEvents(events);
  assert.equal(noteOns.length, 1);
  assert.equal(noteOffs.length, 1);
  assert.equal(noteOns[0].velocity, 100);
  assert.equal(noteOns[0].channel, 1);
  assert.equal(noteOns[0].delaySteps, 0);
  assert.equal(noteOffs[0].pitch, noteOns[0].pitch);
  assert.equal(noteOffs[0].delaySteps, 0.5);
});

test("step pitch matches mapToNote(register_in) on every step (vector parity)", () => {
  // The bit-tap gate decides WHETHER a step fires (LSB=1 && density draw
  // opens the gate). The note VALUE when it does fire is still
  // mapToNote(register_in, range) — the canonical pitch mapping from
  // ADR 001. Cross-target conformance lives in turing-test-vectors.json
  // (engine layer); here we just verify the host wires the same pitch
  // path for every step the gate happens to open.
  const sc = V.tm_step.find(
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (c: any) => c.name === "perfect loop (lock=1.0, density=1.0)",
  );
  assert.ok(sc, "vectors must include perfect-loop scenario");
  const host = makeHost({
    seed: Number(sc.seed.decimal),
    length: sc.length,
    lock: sc.lock,
    density: sc.density,
    rangeLo: sc.range[0],
    rangeHi: sc.range[1],
  });
  let fired = 0;
  for (const expected of sc.trace) {
    const gateBit = expected.register_in & 1;
    const events = host.step(expected.step);
    const noteOns = events.filter((e) => e.type === "noteOn");
    // density=1 means the gate always opens; under bit-tap the host fires
    // iff bit0 of register_in is 1.
    if (gateBit === 1) {
      assert.equal(noteOns.length, 1, `step ${expected.step} bit0=1 must fire`);
      assert.equal(
        noteOns[0].pitch,
        expected.note,
        `step ${expected.step} pitch must match vector`,
      );
      fired++;
    } else {
      assert.equal(noteOns.length, 0, `step ${expected.step} bit0=0 must be silent`);
    }
  }
  // The 16-step perfect loop with seed=1 length=8 has mixed bits, so we
  // expect at least one fire and at least one silence — a sanity check that
  // the bit-tap gate is actually exercised in both directions.
  assert.ok(fired > 0 && fired < sc.trace.length, "loop exercises both bit values");
});

test("step density=0 — no events, but rng still advances", () => {
  // bit-tap (vst spec 2026-05-16): density=0 closes the gate on every step.
  // The density draw is still consumed so the rng thread advances
  // identically, and the register evolves via shiftAndFlip's lock draw.
  const host = makeHost({ seed: 1, length: 8, density: 0.0 });
  const reg0 = host.getRegister();
  const events = host.step(0);
  assert.equal(events.length, 0);
  // Register advanced (auto mode shiftAndFlip ran)
  assert.notEqual(host.getRegister(), reg0);
});

// ── bit-tap gate (vst spec 2026-05-16: bit0 && density-draw) ─────────────────

test("bit-tap: bit0=0 silent regardless of density (visual contract: white = no sound)", () => {
  // The LSB is the gate. A "0" bit at the read head — drawn as a white
  // circle on the ring — must NEVER produce an audible note, no matter
  // what density is set to. This is the visual contract: what you see is
  // what you hear. Replaces the old `||` semantic where density=1 could
  // "fill" off-bits stochastically — that broke the contract.
  for (const density of [0.0, 0.5, 1.0]) {
    const host = makeHost({ seed: 1, length: 8, density, lock: 1.0 });
    host.setBit(0, 0); // force bit0 = 0
    const events = host.step(0);
    const noteOns = events.filter((e) => e.type === "noteOn");
    assert.equal(noteOns.length, 0, `bit0=0 + density=${density} must be silent`);
  }
});

test("bit-tap: bit0=1 + density=1 fires (gate fully open)", () => {
  // The common-case default: density=1.0 opens the gate every time, so
  // every on-bit step fires. Preserves the original "lock=1 + density=1
  // → perfect loop is audible" ergonomics from the previous spec.
  const host = makeHost({ seed: 1, length: 8, density: 1.0, lock: 1.0 });
  host.setBit(0, 1);
  const events = host.step(0);
  const noteOns = events.filter((e) => e.type === "noteOn");
  assert.equal(noteOns.length, 1);
});

test("bit-tap: bit0=1 + density=0 silent (gate closed even on on-bit)", () => {
  // density=0 closes the gate on every step. Under the new && semantic
  // even an on-bit is silent; the user gets a frozen loop with no audio,
  // which is the natural "mute" extreme of the rhythmic-thinning knob.
  const host = makeHost({ seed: 1, length: 8, density: 0.0, lock: 1.0 });
  host.setBit(0, 1);
  const events = host.step(0);
  const noteOns = events.filter((e) => e.type === "noteOn");
  assert.equal(noteOns.length, 0);
});

test("step gate mode without held input — silent and frozen", () => {
  const host = makeHost({ triggerMode: "gate", seed: 1, length: 8 });
  const reg0 = host.getRegister();
  const events = host.step(0);
  assert.equal(events.length, 0);
  // Register did NOT advance
  assert.equal(host.getRegister(), reg0);
});

test("step gate mode with held input — advances normally", () => {
  const host = makeHost({ triggerMode: "gate", seed: 1, length: 8, density: 1.0 });
  host.setBit(0, 1); // bit-tap gate needs LSB=1 for density=1 to fire
  const reg0 = host.getRegister();
  host.noteIn(60, 100, 1);
  const events = host.step(0);
  const noteOns = events.filter((e) => e.type === "noteOn");
  assert.equal(noteOns.length, 1);
  assert.notEqual(host.getRegister(), reg0);
});

test("noteIn seed mode — register changes via shiftAndForce(1)", () => {
  const host = makeHost({ triggerMode: "seed", seed: 1, length: 8 });
  const reg0 = host.getRegister();
  host.noteIn(60, 100, 1);
  const expected = shiftAndForce(reg0, 8, 1);
  assert.equal(host.getRegister(), expected);
});

test("noteOff seed mode — register changes via shiftAndForce(0)", () => {
  const host = makeHost({ triggerMode: "seed", seed: 1, length: 8 });
  host.noteIn(60, 100, 1); // activates seed
  const reg1 = host.getRegister();
  host.noteOff(60, 1);
  const expected = shiftAndForce(reg1, 8, 0);
  assert.equal(host.getRegister(), expected);
});

test("step seed mode after activation — register frozen, no flip", () => {
  const host = makeHost({
    triggerMode: "seed",
    seed: 1,
    length: 8,
    lock: 0.0, // would always flip in auto, but seed mode bypasses
    density: 1.0,
  });
  host.noteIn(60, 100, 1); // activates seed
  const regAfterSeed = host.getRegister();
  host.step(0);
  // Register unchanged after step (seed-active = read-only)
  assert.equal(host.getRegister(), regAfterSeed);
});

test("step seed mode before any input — falls back to auto", () => {
  const host = makeHost({
    triggerMode: "seed",
    seed: 1,
    length: 8,
    lock: 1.0,
    density: 1.0,
  });
  const reg0 = host.getRegister();
  host.step(0);
  // Acted as auto: register evolved (lock=1.0 with all-tail-zero edge depends
  // on initial register; seed=1 length=8 register is non-trivial, so shift
  // changes it). Just assert position advanced — value-level coverage is in
  // the perfect-loop trace test.
  assert.equal(host.getPosition(), 1);
  // With lock=1.0 the register cycles but doesn't sit still: the canonical
  // loop test verifies the cycle. Here we just verify auto-fallback kicked
  // in (no early-return as in gate mode).
  void reg0;
});

test("inputChannel filter — omni accepts all, specific filters", () => {
  // omni
  const omni = makeHost({ triggerMode: "seed", inputChannel: 0, seed: 1 });
  const reg0 = omni.getRegister();
  omni.noteIn(60, 100, 5);
  assert.notEqual(omni.getRegister(), reg0);

  // specific channel — non-matching ignored
  const specific = makeHost({ triggerMode: "seed", inputChannel: 1, seed: 1 });
  const reg0s = specific.getRegister();
  specific.noteIn(60, 100, 5);
  assert.equal(specific.getRegister(), reg0s);
  // matching accepted
  specific.noteIn(60, 100, 1);
  assert.notEqual(specific.getRegister(), reg0s);
});

test("transportStart — re-inits register and resets position", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.step(0);
  host.step(1);
  const beforeStart = host.getRegister();
  assert.equal(host.getPosition(), 2);
  host.transportStart();
  // Register restored to fresh-from-seed
  const expectedFresh = createRegister(8, seedRng(1n)).register;
  assert.equal(host.getRegister(), expectedFresh);
  assert.equal(host.getPosition(), 0);
  void beforeStart;
});

test("transportStop — resets position, register preserved", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.step(0);
  host.step(1);
  const reg = host.getRegister();
  host.transportStop();
  assert.equal(host.getRegister(), reg, "register preserved across stop");
  assert.equal(host.getPosition(), 0);
});

test("setParam length — re-inits register, position preserved", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.step(0);
  host.setParam("length", 16);
  const expected = createRegister(16, seedRng(1n)).register;
  assert.equal(host.getRegister(), expected);
  // Position not reset by length change (only transportStart does that)
  assert.equal(host.getPosition(), 1);
});

test("setParam seed — re-inits register from new seed", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.setParam("seed", 42);
  const expected = createRegister(8, seedRng(42n)).register;
  assert.equal(host.getRegister(), expected);
});

test("setParam lock — no register reset", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.step(0);
  const reg = host.getRegister();
  host.setParam("lock", 0.9);
  assert.equal(host.getRegister(), reg, "lock change does not touch register");
  assert.equal(host.getParams().lock, 0.9);
});

test("setParam triggerMode — clears mode-specific input state", () => {
  const host = makeHost({ triggerMode: "seed", seed: 1, length: 8 });
  host.noteIn(60, 100, 1); // activates seed
  // Switch to auto — seedActivated should clear, so next step does shiftAndFlip
  host.setParam("triggerMode", "auto");
  const regBefore = host.getRegister();
  host.step(0);
  // auto mode advanced the register
  assert.notEqual(host.getRegister(), regBefore);
});

test("setRange — orders lo ≤ hi", () => {
  const host = makeHost();
  host.setRange(72, 60); // reversed
  assert.equal(host.getParams().rangeLo, 60);
  assert.equal(host.getParams().rangeHi, 72);
});

test("setParam rangeLo > rangeHi — clamped to rangeHi", () => {
  const host = makeHost({ rangeLo: 48, rangeHi: 72 });
  host.setParam("rangeLo", 96);
  assert.equal(host.getParams().rangeLo, 72);
});

// --- setBit (ADR 002 §register direct write) -------------------------------
//
// Direct random-access write to register[index]. No shift, no rng advance,
// no interaction with lock or seed-mode shift semantics. Valid in any
// triggerMode. The bridge re-emits the `register` outlet after each call;
// here we verify only host-level state.

test("setBit — write 1 to bit 0 (tail / LSB)", () => {
  const host = makeHost({ seed: 1, length: 8 });
  const reg0 = host.getRegister();
  host.setBit(0, 1);
  // Bit 0 is the tail (read-then-shift removes it). After setBit, the
  // register equals reg0 with bit 0 forced to 1: reg0 | 1.
  assert.equal(host.getRegister(), reg0 | 1);
});

test("setBit — write 0 clears the bit", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.setBit(0, 1);
  const after1 = host.getRegister();
  host.setBit(0, 0);
  // Bit 0 cleared: after1 with low bit masked off.
  assert.equal(host.getRegister(), after1 & ~1);
  assert.equal(host.getRegister() & 1, 0);
});

test("setBit — write to MSB (bit length-1, the shift head)", () => {
  const host = makeHost({ seed: 1, length: 8 });
  host.setBit(7, 1);
  assert.equal((host.getRegister() >>> 7) & 1, 1);
  host.setBit(7, 0);
  assert.equal((host.getRegister() >>> 7) & 1, 0);
});

test("setBit — idempotent same-value write leaves register unchanged", () => {
  const host = makeHost({ seed: 1, length: 8 });
  const reg0 = host.getRegister();
  const bit0 = reg0 & 1;
  host.setBit(0, bit0);
  assert.equal(host.getRegister(), reg0);
});

test("setBit — out-of-bounds index ignored (no throw, no state change)", () => {
  // Defensive: Max can deliver any int from a numbox or list. Out-of-bounds
  // values must be silently ignored rather than corrupting the register.
  const host = makeHost({ seed: 1, length: 8 });
  const reg0 = host.getRegister();
  host.setBit(8, 1); // length boundary (valid range is 0..length-1 = 0..7)
  host.setBit(-1, 1);
  host.setBit(100, 1);
  assert.equal(host.getRegister(), reg0);
});

test("setBit — value coerced to 0/1 (LSB only, like shiftAndForce)", () => {
  // Engine's shiftAndForce applies `forceBit & 1`. Mirror that contract here
  // so setBit(_, 2) is equivalent to setBit(_, 0), not a third state.
  const host = makeHost({ seed: 1, length: 8 });
  host.setBit(0, 1);
  const after1 = host.getRegister();
  host.setBit(0, 2 as 0 | 1); // 2 & 1 == 0
  assert.equal(host.getRegister(), after1 & ~1);
});

test("setBit — does not advance rng (idempotent writes leave step trajectory unchanged)", () => {
  // RNG advance would shift the density / flip draws on subsequent step()
  // calls. Under bit-tap the active flag depends on the register too, so
  // we can't compare active flags between hosts with different bits. Use
  // an idempotent-setBit baseline instead: host A writes each bit back to
  // its current value (no-op effect on register), so any rng drift would
  // come ONLY from setBit consuming draws. Step both hosts and compare
  // register evolution — if rng matches, registers must too.
  const params: Partial<HostParams> = {
    seed: 1,
    length: 8,
    density: 0.5,
    lock: 0.7,
  };
  const a = makeHost(params);
  const b = makeHost(params);
  const reg = a.getRegister();
  for (let i = 0; i < 8; i++) {
    a.setBit(i, ((reg >> i) & 1) as 0 | 1); // no-op write
  }
  // After the no-op writes, register and rng must match between A and B.
  assert.equal(a.getRegister(), b.getRegister(), "no-op setBit must not change register");
  for (let i = 0; i < 12; i++) {
    a.step(i);
    b.step(i);
    assert.equal(a.getRegister(), b.getRegister(), `step ${i}: register drift`);
  }
});

test("setBit — independent of lock (no flip draw consumed)", () => {
  // Different lock values, same setBit, same final register: setBit does
  // not consult or interact with the lock probability path.
  const a = makeHost({ seed: 1, length: 8, lock: 0.0 });
  const b = makeHost({ seed: 1, length: 8, lock: 1.0 });
  a.setBit(2, 1);
  b.setBit(2, 1);
  assert.equal(a.getRegister(), b.getRegister());
});

test("setBit — works in seed mode without activating it", () => {
  // ADR 002 §setBit: setBit must not flip the host into seed-active state.
  // Activation is reserved for noteIn / noteOff in seed triggerMode.
  // Probe: with lock=0 and density=1, an *un*activated seed mode falls back
  // to auto and shifts on step (changes register). If setBit accidentally
  // set seedActivated=true, the register would freeze instead.
  const host = makeHost({
    triggerMode: "seed",
    seed: 1,
    length: 8,
    lock: 0.0,
    density: 1.0,
  });
  host.setBit(0, 1);
  const before = host.getRegister();
  host.step(0);
  assert.notEqual(host.getRegister(), before, "step should advance — seed not activated");
});

test("setBit — valid in every triggerMode", () => {
  for (const mode of ["auto", "gate", "seed"] as const) {
    const host = makeHost({ triggerMode: mode, seed: 1, length: 8 });
    const reg0 = host.getRegister();
    const flipped = ((reg0 & 1) ^ 1) as 0 | 1; // flip bit 0
    host.setBit(0, flipped);
    assert.equal(host.getRegister() & 1, flipped, `mode=${mode}`);
  }
});

test("output range single-note (lo == hi) — every fired step plays lo", () => {
  // With rangeLo == rangeHi, mapToNote is pinned to that single pitch.
  // Under bit-tap (vst spec 2026-05-16), whether a step fires depends on
  // bit0 of the live register, so we don't assert "every step fires" —
  // we assert "every step that DOES fire plays exactly pitch 60."
  const host = makeHost({
    seed: 1,
    length: 8,
    lock: 0.0,
    density: 1.0,
    rangeLo: 60,
    rangeHi: 60,
  });
  let fired = 0;
  for (let i = 0; i < 16; i++) {
    const events = host.step(i);
    const noteOns = events.filter((e) => e.type === "noteOn");
    for (const n of noteOns) {
      assert.equal(n.pitch, 60, `step ${i} pitch must be 60`);
      fired++;
    }
  }
  // lock=0 walks the register randomly; over 16 steps at least one bit0=1
  // is overwhelmingly likely. Guard against accidental all-silent runs.
  assert.ok(fired > 0, "at least one fire expected over 16 walked steps");
});

test("getLastEmittedRegister — snapshots pre-shift register at step time", () => {
  // The anticipation ring (jsui) reads this snapshot so the bit at the
  // playhead is the LSB the listener just heard. After step(), the live
  // register has advanced via shiftAndFlip but lastEmittedRegister still
  // holds the pre-shift value.
  const host = makeHost({ seed: 1, length: 8, lock: 1.0 });
  const preShift = host.getRegister();
  host.step(0);
  assert.equal(host.getLastEmittedRegister(), preShift);
  // Live register advanced
  assert.notEqual(host.getRegister(), preShift);
});

test("getLastEmittedRegister — initial value equals fresh-from-seed register", () => {
  // Before any step has run, the snapshot must already hold a coherent
  // register so the renderer has something to draw on first paint.
  const host = makeHost({ seed: 42, length: 8 });
  assert.equal(host.getLastEmittedRegister(), host.getRegister());
});
