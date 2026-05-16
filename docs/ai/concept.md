# Concept

Stencil ports a generator from
[inboil](https://github.com/im9/inboil) into DAW-native form: a Music
Thing-style **Turing Machine** — a looping shift register that mutates
under user-controlled stability. Per [ADR 005][adr5], Stencil ships as
its own product from this repo; the scale-quantizer counterpart ships
separately as Pointsman ([~/src/vst/pointsman/](../../../pointsman/)).

[adr5]: adr/archive/005-product-split.md

This document describes Stencil's **musical model** — the parts that
are shared across the `m4l/` and `vst/` targets. Per-target UI,
parameter surface, and interaction design live in each target's own
ADRs.

## Topology — single-purpose MIDI effect

Stencil is a single-purpose MIDI effect: it emits MIDI notes from a
probabilistic shift register. The canonical chain is `Stencil →
Pointsman → Synth` for scale-locked random melodies, but Stencil is
also useful on its own — feed an unquantized random pitch stream into
a synth that has its own scale handling, or into a sampler where one
fixed pitch per pad makes the shift register a probabilistic trigger
pattern, or print the chromatic register output to a clip and edit
there.

ADR 005 §Architectural motivation records why the VST target follows
the same single-purpose-per-product split rather than collapsing TM
and quantizer into one plugin (brand consistency with m4l, standalone
discoverability, single-purpose UI focus).

## What Stencil does

On each step of the host transport, Stencil:

1. Reads the current **shift register** as an integer of `length` bits.
2. Maps the register's value to a normalized fraction `0..1` and then to a
   MIDI note within the user-set range `[lo, hi]`.
3. With probability `density`, emits the note as a `noteOn`. On fail, the
   step is silent but timing advances normally — the listener hears a hole
   in the rhythm, not a beat shift.
4. Shifts the register one position: the bit that falls off becomes the
   write-bit candidate; with probability `(1 - lock)` it is flipped before
   being inserted at the head.

`lock = 1.0` freezes the register into a perfect loop of length `length`.
`lock = 0.0` flips every bit — the register is pure noise, no loop emerges.
Intermediate values gradually mutate the loop: `lock = 0.95` is the classic
"slowly evolving pattern" sweet spot.

The musical intent is **a steered loop, not an authored sequence** — it
emerges from initial randomness + lock and is shaped by the user holding
`lock` high during a section they like, not by drawing notes. This is
why Stencil has no per-step program (cf. Oedipa's cells): the program
*is* the register, and the canonical interactions are `lock`, `seed`,
and `length`.

Targets are free to expose a direct bit-toggle affordance (m4l's
`setBit` lets the user flip individual bits in the running register;
the bits then shift naturally under subsequent steps) when it fits
that target's UI surface. Direct toggle is a *shortcut* for "I want
this specific bit pattern right now" — equivalent to nudging seed
until the same bits emerge, but quicker on a small device. The
musical model is unchanged either way: the register is what plays;
how the user nudges it is target-specific UX.

## Composition — Stencil → Pointsman chain

The intended primary use is:

```
[Stencil] -> [Pointsman] -> [Synth]
```

Stencil emits chromatic notes from `[lo, hi]`; a downstream Pointsman
device snaps them to scale. The chain produces the canonical "Music
Thing TM + Quantizer" sound.

Both products are MIDI effects. The host's MIDI routing handles the
chain — Stencil and Pointsman communicate via MIDI notes only, no
internal IPC or shared state. See Pointsman's
[concept.md](../../../pointsman/docs/ai/concept.md) for the
quantizer side.

## MIDI semantics

Stencil is a MIDI effect: it consumes transport (clock + position) and
emits MIDI notes. Sample-accurate timing against the host clock is
expected on all targets.

### Input handling

`triggerMode` parameter controls how MIDI input affects Stencil:

- `auto` (default) — Stencil advances on host transport; input is ignored
- `gate` — Stencil only advances while a key is held; release stops the
  clock (held register, no shift)
- `seed` — incoming `noteOn` writes a `1` bit at the head of the register
  (the player "writes the program"). `noteOff` writes `0`. The user
  becomes the bit source; `lock` no longer governs the head bit while
  the seed mode is active.

`inputChannel` selects which channel Stencil listens to (default `0` =
omni).

### Note-off discipline

On any state change that could leave a hung note (transport stop, bypass,
preset change, parameter change that affects active output, panic), all
currently-sounding notes must receive `noteOff`. Panic (all-notes-off on
all channels) is required behavior, not optional.

### Polyphony

Stencil is monophonic — one note per step.

### Transport

The shift register and rng are re-derived from `(seed, length)` on
every transport start (not stop) — m4l's `transportStart()` and
vst's `processBlock` start-edge handler both call into the same
`createRegister` path. Each press of play therefore replays the
same loop from the same seed: stop / start cycles do not drift the
output away from the seed-defined evolution. Seeded determinism is
a core contract.

Mid-loop scrubbing (host moves PPQ to a non-zero position without
stopping) is *not* re-derived from `(seed, length, lock, position)`
in v1; the engine continues stepping monotonically from its current
register state. This is a known simplification — the targets agree
on it, and a position-aware re-derivation is left as a future
extension if scrub fidelity becomes a user-facing concern.

## What Stencil is not

Clarifying scope by exclusion:

- **Not a step sequencer.** Stencil has no editable per-step pattern.
  The user shapes the loop via `lock`, `length`, and `seed`, not by
  drawing notes.
- **Not a generative synth.** No oscillators, no audio. MIDI only.
- **Not a scale quantizer.** Scale-locking happens downstream — chain a
  Pointsman (or any other quantizer plugin).
- **Not a scene graph node.** inboil embeds TM as a node in a broader
  generative system; Stencil flattens that into a single MIDI effect.
- **Not an unseeded random walker.** Stencil's bit evolution is
  reproducible for fixed `(seed, length, lock, position)`.

## Future extensions

Listed so the surface stays small and these don't get quietly
designed-around. The original "TM output modes" framing (`note` /
`gate` / `velocity`, [archived ADR 003][adr3]) was wrong: a MIDI note
event is a single tuple `(pitch, velocity, gate)` and the modes
treated those attributes as mutually exclusive dispatch branches.
The vst spec corrects this: every step emits one note whose pitch
is reg-derived, velocity and gate are constant slider values.
ADR 007 §Output is the live spec; m4l still implements the old
mode dispatch and will follow.

[adr3]: adr/archive/003-m4l-ui-design.md

- **MPE output** — keep the note-emission abstraction loose enough that
  per-note pitch bend / pressure / timbre can be added without a rewrite.
- **Preset / slot system** — oedipa-style 4-slot preset bank with
  MIDI-triggered recall; useful once the product is in real use.
- **Length sync to bar count** — currently `length` is in bits and the
  loop length in time depends on `subdivision` × `length`. Consider a
  "bars per loop" parameterization that internally derives `length`.

## Parameter surface (canonical)

Targets must expose this minimum set. Additional parameters (MIDI
routing specifics, GUI-only state) may be added per target.

| Parameter         | Type                          | Notes                                              |
|-------------------|-------------------------------|----------------------------------------------------|
| `length`          | int `2..32`                   | shift register length in bits                      |
| `lock`            | float `0..1`                  | `1` = frozen loop, `0` = pure noise                |
| `range`           | `[int 0..127, int 0..127]`    | output MIDI note range, `lo ≤ hi`                  |
| `density`         | float `0..1`                  | active-step probability (default `1.0`)            |
| `subdivision`     | `8th \| 16th \| 32nd \| 8T \| 16T` | step unit; default `16th`                     |
| `seed`            | int                           | RNG seed for reproducibility                       |
| `triggerMode`     | `auto \| gate \| seed`        | input handling; default `auto`                     |
| `inputChannel`    | int `0..16`                   | MIDI input channel; `0` = omni; default `0`        |
| `outputVelocity`  | int `1..127`                  | output note velocity; default `100`                |
| `outputGate`      | float `0..1`                  | gate length as fraction of step; default `0.5`     |

## Origin notes

Stencil has two ancestors:

- **inboil's `generative.ts`** provided the algorithm — shift register
  math, lock semantics, multi-mode TM output. inboil's scene graph
  does not carry over: Stencil is a flat MIDI effect, not a generative
  graph node.
- **Music Thing Modular's [Turing Machine](https://musicthing.co.uk/Turing-Machine/)**
  (Tom Whitwell, 2014) is the source of the shift-register algorithm:
  an 8-bit register with a probability-controlled write-bit. Stencil
  generalizes the length to `2..32` and parameterizes lock continuously,
  but the musical intent (steered loop, not authored sequence) is
  identical.

The TM + Quantizer pairing is a long-standing Eurorack idiom (Music
Thing TM into Mutable Instruments Yarns or similar). Stencil and
Pointsman are the DAW-native expression of that idiom.
