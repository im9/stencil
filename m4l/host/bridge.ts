// Stencil TM bridge — Max protocol layer per ADR 002 §Host ↔ Max protocol.
//
// Pure JS/TS routing between the Max patch and the TmHost class. All Max-
// specific I/O (Max.outlet, Max.addHandler, Date.now, setTimeout) is
// injected via deps so this module is testable under node:test with
// recording fakes — no Max API, no real timers.
//
// Responsibilities:
// - Validate / coerce incoming Max messages at the boundary
// - Convert per-event `delaySteps` (step fractions, ADR 001 / 002) to ms
//   via a running estimate of step interval; dispatch immediate vs.
//   scheduled
// - Track msPerStep across step calls; reset alignment on transportStop /
//   panic
// - Emit `register` / `position` side-channels at the right moments for
//   the jsui register ring (ADR 003)

import {
  DEFAULT_PARAMS,
  TmHost,
  type HostParams,
  type NoteEvent,
  type Subdivision,
  type TmMode,
  type TriggerMode,
} from "./host.ts";

export interface BridgeDeps {
  // Send a MIDI event downstream. velocity=0 means note-off in this protocol.
  emitNote: (pitch: number, velocity: number, channel: number) => void;
  // Side-channel outlets keyed by Max outlet name.
  emitOutlet: (channel: string, ...args: Array<number | string>) => void;
  // Time provider — Date.now() in production, mock in tests.
  now: () => number;
  // Schedule a callback `ms` milliseconds in the future. setTimeout in
  // production; tests pass a synchronous fake that records (ms, cb).
  scheduleAfter: (ms: number, cb: () => void) => void;
}

export interface BridgeOptions {
  initialParams?: Partial<HostParams>;
}

const SUBDIVISIONS: readonly Subdivision[] = [
  "8th",
  "16th",
  "32nd",
  "8T",
  "16T",
];
const TRIGGER_MODES: readonly TriggerMode[] = ["auto", "gate", "seed"];
const TM_MODES: readonly TmMode[] = ["note", "gate", "velocity"];

// EMA factor for msPerStep estimate. Same shape as oedipa's bridge: weight
// the running estimate 0.7, the latest sample 0.3 — smooths jitter from
// transport scheduling without lagging tempo changes too long.
const MS_PER_STEP_EMA_OLD = 0.7;
const MS_PER_STEP_EMA_NEW = 0.3;

// Defensive guard on inter-step dt. Anything > 5s likely means the
// transport stalled (Live paused, edit menu open) and the dt is not a
// real tempo signal.
const MAX_STEP_DT_MS = 5000;

export class TmBridge {
  private host: TmHost;
  private deps: BridgeDeps;
  private lastStepTime: number | null = null;
  private lastStepPos: number | null = null;
  private msPerStep = 0;

  constructor(deps: BridgeDeps, options: BridgeOptions = {}) {
    this.deps = deps;
    this.host = new TmHost({ ...DEFAULT_PARAMS, ...options.initialParams });
    // Push initial UI state (jsui ring needs initial register; position
    // widget needs the 0). The "node.script ready" handshake is NOT emitted
    // here — that signal must fire only after every Max.addHandler() in
    // the entry script, otherwise the patcher's setParam cascade races
    // handler installation. Mirrors oedipa: see oedipa-host.entry.mjs's
    // Max.outlet('hostReady', 1) at end-of-script.
    this.emitRegister();
    this.emitPosition();
  }

  // ---- Max → host handlers ----------------------------------------------

  step(pos: number): void {
    if (!Number.isFinite(pos)) return;
    this.recordStepTiming(pos);
    const events = this.host.step(pos);
    // Active flag is "did the step emit a noteOn?" — derived from events
    // rather than re-running the bit-tap formula here, so the flash always
    // matches what the user actually hears.
    const wasActive = events.some((e) => e.type === "noteOn");
    for (const ev of events) this.dispatch(ev);
    // Mode A (ADR 003 §TM register ring): the visualization shows the
    // initial register snapshot rotating. Per-step `register` emit would
    // make jsui's `bits[]` jump under us. Lifecycle-only emits live in
    // transportStart / setBit / setParam(length|seed) / noteIn-seed.
    this.emitPosition();
    this.emitTriggerFlash(wasActive);
  }

  setBit(index: number, value: number): void {
    if (!Number.isInteger(index)) return;
    const bit = ((value as number) & 1) as 0 | 1;
    this.host.setBit(index, bit);
    // Always re-emit: even on out-of-bounds (host no-op) we re-broadcast
    // the unchanged register so a desynced UI corrects on the next click.
    this.emitRegister();
  }

  noteIn(pitch: number, velocity: number, channel: number): void {
    if (!Number.isFinite(pitch) || !Number.isFinite(channel)) return;
    const wasSeed = this.host.getParams().triggerMode === "seed";
    const events = this.host.noteIn(pitch, velocity, channel);
    for (const ev of events) this.dispatch(ev);
    // seed mode shifts the register on input; auto/gate modes don't.
    if (wasSeed) this.emitRegister();
  }

  noteOff(pitch: number, channel: number): void {
    if (!Number.isFinite(pitch) || !Number.isFinite(channel)) return;
    const wasSeed = this.host.getParams().triggerMode === "seed";
    const events = this.host.noteOff(pitch, channel);
    for (const ev of events) this.dispatch(ev);
    if (wasSeed) this.emitRegister();
  }

  panic(): void {
    for (const ev of this.host.panic()) this.dispatch(ev);
    this.resetTimingAlignment();
  }

  transportStart(): void {
    for (const ev of this.host.transportStart()) this.dispatch(ev);
    this.emitRegister();
    this.emitPosition();
    this.resetTimingAlignment();
  }

  transportStop(): void {
    for (const ev of this.host.transportStop()) this.dispatch(ev);
    this.emitPosition();
    this.resetTimingAlignment();
  }

  setRange(lo: number, hi: number): void {
    if (!Number.isInteger(lo) || !Number.isInteger(hi)) return;
    if (lo < 0 || lo > 127 || hi < 0 || hi > 127) return;
    for (const ev of this.host.setRange(lo, hi)) this.dispatch(ev);
  }

  // Generic scalar parameter update. Validates key + value at the Max
  // boundary so a typo or stale dump path is a silent no-op rather than
  // a typed-cast failure in the host.
  setParam(key: string, value: unknown): void {
    let registerChanged = false;
    let events: NoteEvent[] | null = null;

    switch (key) {
      case "length": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 2 || v > 32) return;
        events = this.host.setParam("length", v);
        registerChanged = true;
        break;
      }
      case "seed": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 0 || v > 0x7fffffff) return;
        events = this.host.setParam("seed", v);
        registerChanged = true;
        break;
      }
      case "lock":
      case "density":
      case "outputGate": {
        const v = Number(value);
        if (!Number.isFinite(v)) return;
        const clamped = Math.max(0, Math.min(1, v));
        events = this.host.setParam(key, clamped);
        break;
      }
      case "rangeLo":
      case "rangeHi": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 0 || v > 127) return;
        events = this.host.setParam(key, v);
        break;
      }
      case "subdivision": {
        const v = String(value);
        if (!SUBDIVISIONS.includes(v as Subdivision)) return;
        events = this.host.setParam("subdivision", v as Subdivision);
        break;
      }
      case "triggerMode": {
        const v = String(value);
        if (!TRIGGER_MODES.includes(v as TriggerMode)) return;
        events = this.host.setParam("triggerMode", v as TriggerMode);
        break;
      }
      case "mode": {
        const v = String(value);
        if (!TM_MODES.includes(v as TmMode)) return;
        events = this.host.setParam("mode", v as TmMode);
        break;
      }
      case "inputChannel": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 0 || v > 16) return;
        events = this.host.setParam("inputChannel", v);
        break;
      }
      case "outputVelocity": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 1 || v > 127) return;
        events = this.host.setParam("outputVelocity", v);
        break;
      }
      case "outputChannel": {
        const v = Number(value);
        if (!Number.isInteger(v) || v < 1 || v > 16) return;
        events = this.host.setParam("outputChannel", v);
        break;
      }
      default:
        return;
    }

    if (events !== null) for (const ev of events) this.dispatch(ev);
    if (registerChanged) this.emitRegister();
  }

  // ---- internal --------------------------------------------------------

  private dispatch(ev: NoteEvent): void {
    const ds = ev.delaySteps ?? 0;
    if (ds <= 0 || this.msPerStep <= 0) {
      this.emitNoteEvent(ev);
      return;
    }
    const ms = ds * this.msPerStep;
    this.deps.scheduleAfter(ms, () => this.emitNoteEvent(ev));
  }

  private emitNoteEvent(ev: NoteEvent): void {
    const velocity = ev.type === "noteOn" ? ev.velocity : 0;
    this.deps.emitNote(ev.pitch, velocity, ev.channel);
  }

  private recordStepTiming(pos: number): void {
    const now = this.deps.now();
    if (this.lastStepTime !== null && this.lastStepPos !== null) {
      const dt = now - this.lastStepTime;
      const dpos = pos - this.lastStepPos;
      // dpos<=0 = scrub or wrap; dt>=MAX_STEP_DT_MS likely means transport
      // stalled. Either case, ignore as a tempo signal.
      if (dt > 0 && dt < MAX_STEP_DT_MS && dpos > 0) {
        const inst = dt / dpos;
        this.msPerStep =
          this.msPerStep === 0
            ? inst
            : this.msPerStep * MS_PER_STEP_EMA_OLD +
              inst * MS_PER_STEP_EMA_NEW;
      }
    }
    this.lastStepTime = now;
    this.lastStepPos = pos;
  }

  private resetTimingAlignment(): void {
    // Drop the inter-step alignment so a transport restart doesn't compute
    // a giant dt against the previous run. msPerStep itself is preserved
    // — it's a useful estimate carried across stop/start so the very first
    // post-restart noteOff still schedules sensibly.
    this.lastStepTime = null;
    this.lastStepPos = null;
  }

  private emitRegister(): void {
    const reg = this.host.getRegister();
    const len = this.host.getParams().length;
    const bits: number[] = [];
    for (let i = 0; i < len; i++) bits.push((reg >>> i) & 1);
    this.deps.emitOutlet("register", ...bits);
  }

  // ADR 003 §Active-step flash: visual flash overlay synced with audible
  // trigger. Emitted every step (1 = noteOn fired, 0 = silent) so the
  // renderer can deterministically clear in-flight flash without timeouts.
  private emitTriggerFlash(active: boolean): void {
    this.deps.emitOutlet("triggerFlash", active ? 1 : 0);
  }

  private emitPosition(): void {
    // Outlet symbol is `ringHead` (NOT `position`): when a [jsui]
    // receives an inlet message whose first symbol matches a Max
    // box-level attribute name (`position` is one such reserved word),
    // Max interprets it as a setter and shifts the box's screen
    // position — observed empirically as a 1px-per-message creep in
    // M4L locked view. `ringHead` is a domain-specific non-colliding
    // name. Keep this name in sync with registerRing.jsui.js's
    // anything() dispatch and the patcher's [route ... ringHead] /
    // [prepend ringHead] objects.
    this.deps.emitOutlet("ringHead", this.host.getPosition());
  }
}
