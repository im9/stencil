// Stencil TM host — pure logic per ADR 002 §Stencil TM.
//
// Owns TmHostState (register, rng, position, held/seed input tracking) and
// exposes methods that the Max bridge calls. Returns NoteEvent arrays;
// the bridge schedules them (delaySteps → ms via msPerStep). No Max API,
// no timers, no I/O — fully testable under node --test.

import {
  createRegister,
  mapToNote,
  nextU32,
  probabilityThreshold,
  registerToFraction,
  seedRng,
  shiftAndFlip,
  shiftAndForce,
  type RngState,
} from "../engine/turing.ts";

export type MidiNote = number; // 0..127
export type Channel = number; // 1..16
export type Subdivision = "8th" | "16th" | "32nd" | "8T" | "16T";
export type TriggerMode = "auto" | "gate" | "seed";

export type NoteEvent =
  | { type: "noteOn"; pitch: MidiNote; velocity: number; channel: Channel; delaySteps: number }
  | { type: "noteOff"; pitch: MidiNote; channel: Channel; delaySteps: number };

export interface HostParams {
  length: number; // 2..32
  lock: number; // 0..1
  rangeLo: MidiNote; // 0..127
  rangeHi: MidiNote; // 0..127, ≥ rangeLo
  density: number; // 0..1
  subdivision: Subdivision;
  seed: number; // u31
  triggerMode: TriggerMode;
  inputChannel: number; // 0..16, 0 = omni
  outputVelocity: number; // 1..127
  outputGate: number; // 0..1, fraction of step
  outputChannel: Channel; // 1..16
}

export const DEFAULT_PARAMS: HostParams = {
  length: 8,
  lock: 0.5,
  rangeLo: 48,
  rangeHi: 72,
  density: 1.0,
  subdivision: "16th",
  seed: 42,
  triggerMode: "auto",
  inputChannel: 0,
  outputVelocity: 100,
  outputGate: 0.5,
  outputChannel: 1,
};

function noteKey(pitch: number, channel: number): string {
  return `${pitch}:${channel}`;
}

// ParamKey is the union of all HostParams scalar keys (excludes the
// range-tuple setRange() path). Used for generic setParam(key, value).
export type ParamKey = keyof HostParams;

export class TmHost {
  private params: HostParams;
  private register: number;
  private rng: RngState;
  private position: number;
  private heldInputs: Set<string>; // gate mode
  private seedActivated: boolean; // seed mode: false until first input
  // Pre-shift snapshot captured at the start of step() — equals the
  // register that was just emitted (bit 0 is the LSB the listener heard).
  // The anticipation-animation ring (vst spec ported 2026-05-16) draws
  // this snapshot during the playing portion of the step, then eases bit 1
  // to the top during the trailing portion so it becomes bit 0 of the
  // next snapshot. Defaults to the constructor's initial register so the
  // first paint has something coherent to show before the first step.
  private lastEmittedRegister: number;

  constructor(params: HostParams = DEFAULT_PARAMS) {
    this.params = { ...params };
    this.heldInputs = new Set();
    this.seedActivated = false;
    const init = this.freshRegister();
    this.register = init.register;
    this.rng = init.rng;
    this.position = 0;
    this.lastEmittedRegister = init.register;
  }

  private freshRegister(): { register: number; rng: RngState } {
    const rng = seedRng(BigInt(this.params.seed));
    const r = createRegister(this.params.length, rng);
    return { register: r.register, rng: r.state };
  }

  private channelMatches(ch: number): boolean {
    return this.params.inputChannel === 0 || ch === this.params.inputChannel;
  }

  // Transport: advance to host step index `_position`. The position param is
  // informational (passed through for UI side-channels in the bridge); the
  // host advances exactly one step per call. Patcher ensures one step per
  // subdivision tick.
  step(_position: number): NoteEvent[] {
    const events: NoteEvent[] = [];

    // gate mode + no input held → silent, register and rng both frozen
    if (this.params.triggerMode === "gate" && this.heldInputs.size === 0) {
      return events;
    }

    // Snapshot the pre-shift register: its LSB is the bit this step plays,
    // and the anticipation-animation ring reads it to render the playhead
    // and the salmon mutated-bit halo.
    this.lastEmittedRegister = this.register;

    // Read current register for output (read-then-shift per ADR 001)
    const f = registerToFraction(this.register, this.params.length);

    // Bit-tap gate (vst spec 2026-05-16): the LSB is the gate; bit 0 (off)
    // is always silent (white ring bit = no audible note, the visual
    // contract). When LSB=1, density is the probability that the gate
    // opens, so density acts as a rhythmic-thinning knob applied to the
    // bit pattern. density=1.0 (default) opens the gate every time. The
    // density draw is consumed unconditionally so the rng thread advances
    // identically regardless of bit outcome — keeps the cross-target
    // turing-test-vectors.json parity intact.
    const bit0 = (this.register & 1) === 1;
    const dDraw = nextU32(this.rng);
    const dThresh = probabilityThreshold(this.params.density);
    const gateOpens = dDraw.value < dThresh;
    this.rng = dDraw.state;
    const active = bit0 && gateOpens;

    // Single-dispatch output (vst spec 2026-05-15): every active step
    // emits one noteOn carrying (pitch, velocity, gate). Pitch is the
    // varying reg-derived attribute; velocity and gate are constant
    // slider values. The earlier note / gate / velocity mode dispatch
    // treated the three attributes as mutually exclusive branches,
    // which doesn't match what a MIDI note actually is. The old
    // gate-mode "pitch = range midpoint" is recoverable by setting
    // rangeLo == rangeHi; the old velocity-mode reg-driven velocity
    // shaping is dropped (see concept.md §Future extensions for the
    // shape any future per-attribute modulation should take).
    const note: MidiNote = mapToNote(
      f.num,
      f.den,
      this.params.rangeLo,
      this.params.rangeHi,
    );
    const velocity = this.params.outputVelocity;

    // Register advancement
    const isSeedActive =
      this.params.triggerMode === "seed" && this.seedActivated;
    if (!isSeedActive) {
      // auto / seed-pre-activation / gate-with-input → shiftAndFlip
      const sf = shiftAndFlip(
        this.register,
        this.params.length,
        this.params.lock,
        this.rng,
      );
      this.register = sf.register;
      this.rng = sf.state;
    }
    // seed-active: register is frozen (input drives it); rng was advanced
    // for density only, no flip draw.

    this.position++;

    if (active) {
      const ch = this.params.outputChannel;
      events.push({
        type: "noteOn",
        pitch: note,
        velocity,
        channel: ch,
        delaySteps: 0,
      });
      events.push({
        type: "noteOff",
        pitch: note,
        channel: ch,
        delaySteps: this.params.outputGate,
      });
    }

    return events;
  }

  // MIDI input path. Channel-filtered by inputChannel (0=omni).
  noteIn(pitch: number, _velocity: number, channel: number): NoteEvent[] {
    if (!this.channelMatches(channel)) return [];
    const key = noteKey(pitch, channel);
    if (this.params.triggerMode === "gate") {
      this.heldInputs.add(key);
    } else if (this.params.triggerMode === "seed") {
      this.register = shiftAndForce(this.register, this.params.length, 1);
      this.seedActivated = true;
    }
    // auto mode: input ignored
    return [];
  }

  noteOff(pitch: number, channel: number): NoteEvent[] {
    if (!this.channelMatches(channel)) return [];
    const key = noteKey(pitch, channel);
    if (this.params.triggerMode === "gate") {
      this.heldInputs.delete(key);
    } else if (this.params.triggerMode === "seed") {
      this.register = shiftAndForce(this.register, this.params.length, 0);
      this.seedActivated = true;
    }
    return [];
  }

  // concept.md §Transport: every transport start re-derives the register
  // from (seed, length) — seeded determinism is a core contract. Stop alone
  // preserves register state for inspection, but stop+start re-rolls.
  transportStart(): NoteEvent[] {
    const init = this.freshRegister();
    this.register = init.register;
    this.rng = init.rng;
    this.position = 0;
    this.heldInputs.clear();
    this.seedActivated = false;
    return [];
  }

  transportStop(): NoteEvent[] {
    this.heldInputs.clear();
    this.seedActivated = false;
    this.position = 0;
    return [];
  }

  panic(): NoteEvent[] {
    return [];
  }

  // Generic parameter update. length / seed re-init the register;
  // triggerMode clears mode-specific input state. Per-step noteOff
  // scheduling lives at the bridge (delaySteps × msPerStep), so the host
  // does not track sounding notes here.
  setParam<K extends ParamKey>(key: K, value: HostParams[K]): NoteEvent[] {
    this.params[key] = value;
    // Re-init register on length/seed change. Position is preserved
    // (monotonic since transportStart per ADR 002).
    if (key === "length" || key === "seed") {
      const init = this.freshRegister();
      this.register = init.register;
      this.rng = init.rng;
    }
    // triggerMode change clears mode-specific input state to avoid stale
    // flags (e.g., seedActivated still true after switching to auto).
    if (key === "triggerMode") {
      this.heldInputs.clear();
      this.seedActivated = false;
    }
    // Range clamping on individual lo/hi sets — keep lo ≤ hi invariant.
    if (key === "rangeLo" && this.params.rangeLo > this.params.rangeHi) {
      this.params.rangeLo = this.params.rangeHi;
    }
    if (key === "rangeHi" && this.params.rangeHi < this.params.rangeLo) {
      this.params.rangeHi = this.params.rangeLo;
    }
    return [];
  }

  // ADR 002 §register direct write: random-access write to register[index].
  // No shift, no rng advance, no interaction with `lock` or seed-mode shift
  // semantics. Valid in any triggerMode. Out-of-bounds index is silently
  // ignored (defensive — Max can deliver any int from a numbox or list).
  // The bridge re-emits the `register` outlet after each call so the UI
  // (jsui ring, ADR 003) reflects the new state.
  setBit(index: number, value: 0 | 1): NoteEvent[] {
    if (index < 0 || index >= this.params.length) return [];
    const bit = (value & 1) as 0 | 1;
    const mask = (1 << index) >>> 0;
    if (bit === 1) {
      this.register = (this.register | mask) >>> 0;
    } else {
      this.register = (this.register & ~mask) >>> 0;
    }
    return [];
  }

  // Tuple range update. Always orders lo ≤ hi.
  setRange(lo: number, hi: number): NoteEvent[] {
    this.params.rangeLo = Math.min(lo, hi);
    this.params.rangeHi = Math.max(lo, hi);
    return [];
  }

  // Inspection (for UI side-channels and tests).
  getRegister(): number {
    return this.register;
  }
  // Pre-shift snapshot of the register at the most recent step() call —
  // bit 0 is the LSB the listener heard. Bridge publishes this to the
  // jsui anticipation ring so the playhead bit equals the bit currently
  // sounding. Before the first step() the snapshot equals the initial
  // register (no shifts have happened yet).
  getLastEmittedRegister(): number {
    return this.lastEmittedRegister;
  }
  getPosition(): number {
    return this.position;
  }
  getParams(): Readonly<HostParams> {
    return this.params;
  }
}
