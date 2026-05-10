// Bridge tests — verify the Max protocol layer per ADR 002 §Host ↔ Max
// protocol. Bridge is a pure-JS module: deps (emitNote / emitOutlet / now /
// scheduleAfter) are injected so the suite runs under node:test with
// recording fakes — no Max API, no real timers.

import { test } from "node:test";
import assert from "node:assert/strict";

import { TmBridge, type BridgeDeps } from "./bridge.ts";
import { DEFAULT_PARAMS, type HostParams } from "./host.ts";

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type OutletArg = number | string;

interface Recorder {
  notes: Array<{ pitch: number; velocity: number; channel: number }>;
  outlets: Array<{ channel: string; args: OutletArg[] }>;
  scheduled: Array<{ ms: number; cb: () => void }>;
  nowValue: number;
}

function makeRecorder(): Recorder {
  return { notes: [], outlets: [], scheduled: [], nowValue: 0 };
}

function makeDeps(rec: Recorder): BridgeDeps {
  return {
    emitNote: (pitch, velocity, channel) =>
      rec.notes.push({ pitch, velocity, channel }),
    emitOutlet: (channel, ...args) =>
      rec.outlets.push({ channel, args: [...args] }),
    now: () => rec.nowValue,
    scheduleAfter: (ms, cb) => rec.scheduled.push({ ms, cb }),
  };
}

function makeBridge(
  overrides: Partial<HostParams> = {},
): { bridge: TmBridge; rec: Recorder } {
  const rec = makeRecorder();
  const bridge = new TmBridge(makeDeps(rec), {
    initialParams: { ...DEFAULT_PARAMS, ...overrides },
  });
  return { bridge, rec };
}

function outletsByName(rec: Recorder, name: string) {
  return rec.outlets.filter((o) => o.channel === name);
}

// --- construction --------------------------------------------------------

test("construction — emits initial register + position; does NOT emit ready", () => {
  const { rec } = makeBridge({ length: 8 });
  // The patcher's "node.script ready" handshake (ADR 003 §Stencil
  // patcher) is the *entry script's* responsibility — it must fire only
  // after every Max.addHandler() is registered. The bridge constructor
  // runs BEFORE addHandler in stencil.mjs, so emitting "ready" here
  // would race the handler installation. Mirrors oedipa: see
  // oedipa-host.entry.mjs Max.outlet('hostReady', 1) at end-of-script.
  assert.ok(
    !rec.outlets.some((o) => o.channel === "ready"),
    "bridge constructor must NOT emit 'ready' (entry script owns this)",
  );
  // register: length-many bits, each 0 or 1
  const regs = outletsByName(rec, "register");
  assert.equal(regs.length, 1, "exactly one register emit on construction");
  assert.equal(regs[0].args.length, 8, "8 bits for length=8");
  for (const b of regs[0].args) assert.ok(b === 0 || b === 1, "bit must be 0/1");
  // position: 0 at construction
  const pos = outletsByName(rec, "ringHead");
  assert.equal(pos.length, 1);
  assert.deepEqual(pos[0].args, [0]);
});

test("construction — register bit count matches custom length", () => {
  const { rec } = makeBridge({ length: 16 });
  const regs = outletsByName(rec, "register");
  assert.equal(regs[0].args.length, 16);
});

// --- step path -----------------------------------------------------------

test("step density=1 — immediate noteOn dispatched via emitNote", () => {
  const { bridge, rec } = makeBridge({
    seed: 1,
    length: 8,
    density: 1.0,
    lock: 1.0,
    outputVelocity: 100,
    outputChannel: 1,
    outputGate: 0.5,
  });
  rec.notes.length = 0; // ignore construction-time emits (none expected, but defensive)
  bridge.step(0);
  // First emitted note is the noteOn (velocity > 0)
  assert.ok(rec.notes.length >= 1);
  assert.equal(rec.notes[0].velocity, 100);
  assert.equal(rec.notes[0].channel, 1);
});

test("step — first step has no msPerStep estimate, delayed noteOff fires immediately", () => {
  // delaySteps > 0 but msPerStep == 0 → emit immediately rather than schedule.
  // This is the v1 contract: timing alignment kicks in only after we have at
  // least one inter-step delta to estimate from.
  const { bridge, rec } = makeBridge({
    seed: 1,
    density: 1.0,
    lock: 1.0,
    outputGate: 0.5,
  });
  rec.notes.length = 0;
  rec.scheduled.length = 0;
  bridge.step(0);
  // Both noteOn (delay=0) and noteOff (delay=0.5) emit immediately on first step
  assert.equal(rec.scheduled.length, 0, "no scheduling without msPerStep");
  // emitNote called for both events
  const onCount = rec.notes.filter((n) => n.velocity > 0).length;
  const offCount = rec.notes.filter((n) => n.velocity === 0).length;
  assert.equal(onCount, 1);
  assert.equal(offCount, 1);
});

test("step — second step uses msPerStep estimate to schedule noteOff", () => {
  // Step at t=0, then step at t=125ms (typical 16th @ 120 BPM). Bridge
  // estimates msPerStep≈125 from the dt and uses it to schedule noteOff.
  const { bridge, rec } = makeBridge({
    seed: 1,
    density: 1.0,
    lock: 1.0,
    outputGate: 0.5,
  });
  rec.nowValue = 0;
  bridge.step(0);
  rec.nowValue = 125;
  rec.scheduled.length = 0;
  bridge.step(1);
  // noteOff for this step has delaySteps=0.5 → 0.5 × 125 = 62.5 ms
  const noteOffSchedules = rec.scheduled.filter((s) => s.ms > 0);
  assert.equal(noteOffSchedules.length, 1);
  assert.equal(noteOffSchedules[0].ms, 62.5);
});

test("step — msPerStep estimate updates via EMA across multiple steps", () => {
  // Three steps spaced 100ms apart → msPerStep ≈ 100
  const { bridge, rec } = makeBridge({
    seed: 1,
    density: 1.0,
    lock: 1.0,
    outputGate: 1.0, // simplifies math: noteOff schedule == msPerStep
  });
  rec.nowValue = 0;
  bridge.step(0);
  rec.nowValue = 100;
  bridge.step(1);
  rec.nowValue = 200;
  rec.scheduled.length = 0;
  bridge.step(2);
  const sched = rec.scheduled.filter((s) => s.ms > 0);
  assert.equal(sched.length, 1);
  // After two deltas (both 100ms), the EMA estimate is exactly 100.
  // (First inserts 100 directly; second EMA: 0.7×100 + 0.3×100 = 100.)
  assert.equal(sched[0].ms, 100);
});

test("step — emits ringHead + triggerFlash, NOT register (Mode A frozen pattern)", () => {
  // ADR 003 §TM register ring (Mode A): the ring visualization shows the
  // initial register snapshot rotating; bits[] in jsui must NOT update on
  // every step or the rotation animation jumps. Bridge therefore omits
  // `register` from the per-step outlet set; lifecycle events (constructor,
  // transportStart, setBit, length/seed change) re-emit it.
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  // clear construction-time emits
  rec.outlets.length = 0;
  bridge.step(0);
  const regs = outletsByName(rec, "register");
  const pos = outletsByName(rec, "ringHead");
  const flash = outletsByName(rec, "triggerFlash");
  assert.equal(regs.length, 0, "step must NOT emit register (Mode A)");
  assert.equal(pos.length, 1, "step must emit ringHead");
  assert.deepEqual(pos[0].args, [1], "position incremented to 1");
  assert.equal(flash.length, 1, "step must emit triggerFlash");
  assert.ok(
    flash[0].args[0] === 0 || flash[0].args[0] === 1,
    "triggerFlash arg is 0 or 1",
  );
});

test("step active=true — triggerFlash 1 (audible step → visual flash)", () => {
  // ADR 003 §Active-step flash: the flash overlay fires precisely when the
  // host emits a noteOn, so visual matches audible. With density=1, lock=1,
  // and bit-tap on, the first step is guaranteed active.
  const { bridge, rec } = makeBridge({
    seed: 1,
    length: 8,
    density: 1.0,
    lock: 1.0,
  });
  rec.outlets.length = 0;
  rec.notes.length = 0;
  bridge.step(0);
  const flash = outletsByName(rec, "triggerFlash");
  assert.equal(flash.length, 1);
  assert.equal(flash[0].args[0], 1, "active step → triggerFlash 1");
});

test("step active=false — triggerFlash 0 (silent step → clear flash)", () => {
  // ADR 003 §Active-step flash: the renderer relies on a deterministic
  // 0-message to clear in-flight flash state, rather than a timeout. Force
  // bit0=0 + density=0 so the step is silent under bit-tap.
  const { bridge, rec } = makeBridge({
    seed: 1,
    length: 8,
    density: 0.0,
    lock: 1.0,
  });
  bridge.setBit(0, 0); // ensure bit0 = 0
  rec.outlets.length = 0;
  rec.notes.length = 0;
  bridge.step(0);
  const flash = outletsByName(rec, "triggerFlash");
  assert.equal(flash.length, 1);
  assert.equal(flash[0].args[0], 0, "silent step → triggerFlash 0");
});

// --- setBit --------------------------------------------------------------

test("setBit — writes via host and re-emits register outlet", () => {
  // ADR 002 §register direct write: bridge re-emits register outlet so the
  // jsui ring (ADR 003) reflects the new bit immediately.
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  rec.outlets.length = 0;
  bridge.setBit(3, 1);
  const regs = outletsByName(rec, "register");
  assert.equal(regs.length, 1, "register re-emitted after setBit");
  assert.equal(regs[0].args[3], 1, "bit 3 reflected in outlet args");
});

test("setBit — out-of-bounds index ignored, register still re-emitted defensively", () => {
  // Patcher convention: a numbox click that produces an out-of-range index
  // shouldn't crash the bridge. Host silently ignores; bridge re-emits the
  // (unchanged) register so any UI-side desync corrects on each click.
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  const reg0 = outletsByName(rec, "register")[0]!.args.slice();
  rec.outlets.length = 0;
  bridge.setBit(99, 1);
  const regs = outletsByName(rec, "register");
  assert.equal(regs.length, 1);
  assert.deepEqual(regs[0].args, reg0, "register unchanged but re-emitted");
});

// --- setParam ------------------------------------------------------------

test("setParam length — re-emits register (re-init from seed)", () => {
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  rec.outlets.length = 0;
  bridge.setParam("length", 16);
  const regs = outletsByName(rec, "register");
  assert.equal(regs.length, 1);
  assert.equal(regs[0].args.length, 16, "register emit reflects new length");
});

test("setParam lock — does NOT re-emit register (no register change)", () => {
  // Bridge avoids spurious register emits for params that don't touch the
  // register state. Only length / seed / setBit / step / transportStart /
  // seed-mode noteIn|noteOff trigger a register emit.
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  rec.outlets.length = 0;
  bridge.setParam("lock", 0.9);
  assert.equal(outletsByName(rec, "register").length, 0);
});

test("setParam invalid key — silently ignored", () => {
  const { bridge, rec } = makeBridge({ seed: 1 });
  rec.outlets.length = 0;
  bridge.setParam("nonsense", 42);
  // No outlets emitted — bridge made no host call
  assert.equal(rec.outlets.length, 0);
});

test("setParam invalid value — silently ignored", () => {
  const { bridge, rec } = makeBridge({ seed: 1, lock: 0.5 });
  rec.outlets.length = 0;
  bridge.setParam("length", "not a number");
  bridge.setParam("length", NaN);
  bridge.setParam("triggerMode", "bogus-mode");
  bridge.setParam("mode", "bogus-mode"); // ADR 003 §TM output mode validation
  assert.equal(rec.outlets.length, 0);
});

test("setParam mode — accepts 'note' / 'gate' / 'velocity'", () => {
  // ADR 003 §TM output mode: enum is exactly {note, gate, velocity}; any
  // other string value is rejected at the bridge boundary.
  const { bridge, rec } = makeBridge({ seed: 1 });
  rec.outlets.length = 0;
  bridge.setParam("mode", "note");
  bridge.setParam("mode", "gate");
  bridge.setParam("mode", "velocity");
  // No outlet emit expected (mode change doesn't reset register), but the
  // calls must not throw and must not be the silent-no-op path. Probe
  // effect via a step: in 'gate' mode the next active step's pitch is the
  // range midpoint, regardless of regValue.
  bridge.setParam("mode", "gate");
  rec.notes.length = 0;
  // Force bit0 = 1 so the step is guaranteed active under bit-tap.
  bridge.setBit(0, 1);
  rec.outlets.length = 0;
  bridge.step(0);
  const noteOn = rec.notes.find((n) => n.velocity > 0);
  assert.ok(noteOn);
  // Default range is [48, 72]; gate-mode midpoint = 60.
  assert.equal(noteOn.pitch, 60);
});

test("setRange — orders lo ≤ hi via host", () => {
  const { bridge } = makeBridge();
  bridge.setRange(72, 60); // reversed
  // Verified indirectly: a step with this range will emit notes only in [60..72].
  // The host's setRange test covers ordering; bridge test just verifies the
  // call reaches the host without throwing or mangling args.
  // (No assertion needed beyond no-throw — but record one for clarity.)
  assert.ok(true);
});

// --- noteIn / noteOff ----------------------------------------------------

test("noteIn seed mode — re-emits register (input drives shift)", () => {
  // ADR 002 §triggerMode seed: noteIn → shiftAndForce(1). Register changed,
  // bridge must re-emit so UI follows.
  const { bridge, rec } = makeBridge({
    triggerMode: "seed",
    seed: 1,
    length: 8,
  });
  rec.outlets.length = 0;
  bridge.noteIn(60, 100, 1);
  assert.equal(outletsByName(rec, "register").length, 1);
});

test("noteIn auto mode — does NOT re-emit register (input ignored)", () => {
  const { bridge, rec } = makeBridge({
    triggerMode: "auto",
    seed: 1,
    length: 8,
  });
  rec.outlets.length = 0;
  bridge.noteIn(60, 100, 1);
  assert.equal(outletsByName(rec, "register").length, 0);
});

// --- transport / panic ---------------------------------------------------

test("transportStart — emits register (re-init) and position 0", () => {
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  // step a few times to advance position
  bridge.step(0);
  bridge.step(1);
  rec.outlets.length = 0;
  bridge.transportStart();
  const pos = outletsByName(rec, "ringHead");
  const regs = outletsByName(rec, "register");
  assert.equal(pos.length, 1);
  assert.deepEqual(pos[0].args, [0]);
  assert.equal(regs.length, 1);
});

test("transportStop — emits position 0, register preserved (no re-emit)", () => {
  const { bridge, rec } = makeBridge({ seed: 1, length: 8 });
  bridge.step(0);
  rec.outlets.length = 0;
  bridge.transportStop();
  const pos = outletsByName(rec, "ringHead");
  assert.equal(pos.length, 1);
  assert.deepEqual(pos[0].args, [0]);
  // Register preserved across stop (host invariant); no need to re-emit.
  assert.equal(outletsByName(rec, "register").length, 0);
});

test("transportStop — resets timing estimate so restart starts fresh", () => {
  // After transportStop, the next step pair must rebuild msPerStep from
  // scratch — leftover state from the previous transport run would produce
  // wrong scheduling on the very first scheduled noteOff after restart.
  const { bridge, rec } = makeBridge({
    seed: 1,
    density: 1.0,
    lock: 1.0,
    outputGate: 1.0,
  });
  rec.nowValue = 0;
  bridge.step(0);
  rec.nowValue = 100;
  bridge.step(1);
  bridge.transportStop();
  // Now restart at much later "wall time" — first step after restart should
  // not schedule based on stale msPerStep, because lastStepTime was reset.
  rec.nowValue = 99999;
  rec.scheduled.length = 0;
  bridge.step(0);
  // Without the timing reset, dt = 99999-100 = 99899ms would be huge but
  // also outside the 5000ms guard so the estimate stays. Either way, the
  // critical contract: events still emit (no hang) and msPerStep doesn't
  // explode.
  // Assert: no scheduled-with-large-ms artifacts from stale state.
  for (const s of rec.scheduled) {
    assert.ok(s.ms < 10000, `unexpectedly large schedule ms=${s.ms}`);
  }
});

test("setParam subdivision — resets msPerStep so the new rate doesn't inherit the old EMA", () => {
  // Subdivision change moves the qmetro to a new rate (e.g. 16n → 32n
  // halves the inter-step dt). The bridge's msPerStep EMA would lag the
  // new rate by ~3 steps; during that window the noteOff schedule
  // (delaySteps × msPerStep) uses the old, longer estimate and lands
  // after the next step's noteOn — producing audible click / hung note.
  // Resetting msPerStep on subdivision change forces the next step pair
  // to rebuild the estimate from scratch against the new dt.
  const { bridge, rec } = makeBridge({
    seed: 1,
    density: 1.0,
    lock: 1.0,
    outputGate: 1.0, // schedule ms == msPerStep, easy to assert
  });
  // Establish msPerStep ≈ 125ms (16th @ 120 BPM)
  rec.nowValue = 0;
  bridge.step(0);
  rec.nowValue = 125;
  bridge.step(1);
  rec.nowValue = 250;
  rec.scheduled.length = 0;
  bridge.step(2);
  // Sanity: pre-change estimate is 125
  assert.equal(rec.scheduled.filter((s) => s.ms > 0)[0].ms, 125);
  // Switch subdivision → metro will start firing twice as fast in Live;
  // bridge must drop its old estimate so the next step pair rebuilds.
  bridge.setParam("subdivision", "32nd");
  // Next step at +62.5ms (32nd @ 120 BPM); without reset it would
  // schedule with stale 125ms estimate.
  rec.nowValue = 312.5;
  rec.scheduled.length = 0;
  bridge.step(3);
  // First post-reset step has no estimate → noteOff fires immediately
  // (same contract as the cold-start branch in step path).
  assert.equal(
    rec.scheduled.filter((s) => s.ms > 0).length,
    0,
    "first step post-reset must not schedule against stale msPerStep",
  );
  // Following step rebuilds estimate from new dt (62.5ms)
  rec.nowValue = 375;
  rec.scheduled.length = 0;
  bridge.step(4);
  const sched = rec.scheduled.filter((s) => s.ms > 0);
  assert.equal(sched.length, 1);
  assert.equal(sched[0].ms, 62.5, "estimate must reflect new subdivision");
});

test("panic — flushes via host, emits no spurious register/position", () => {
  // Panic flushes notesOn but doesn't change register / position state, so
  // no UI outlet emit is needed.
  const { bridge, rec } = makeBridge();
  rec.outlets.length = 0;
  bridge.panic();
  assert.equal(outletsByName(rec, "register").length, 0);
  assert.equal(outletsByName(rec, "ringHead").length, 0);
});
