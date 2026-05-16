# Stencil

Probabilistic MIDI sequence generator inspired by Music Thing
Modular's Turing Machine — a shift register that emits notes from a
controlled-randomness loop.

Named after Herbert Stencil, the protagonist of Thomas Pynchon's *V.* —
a man assembling a pattern from random clues, "always becoming." The
device does something similar audibly: a circulating bit register,
nudged by probability, congeals into a melodic figure and drifts away
again as `lock` is loosened.

## What it does

On each host step, Stencil reads the LSB of its **shift register** —
if it's `1` (visually an on-bit at the playhead) the gate considers
firing with probability `density`; if it's `0` (off-bit) the step is
always silent. When the gate opens, Stencil maps the register's value
into a user-set MIDI note range and emits the note. It then shifts the
register one position — the bit cycling out is flipped with probability
`1 - lock` before being reinserted at the head.

User parameters:

- **length** — register width sets loop length
- **lock** — stability of the loop. `1.0` = perfect loop; `0.0` = pure
  noise; `0.95` is the classic slowly-evolving-pattern sweet spot
- **density** — gate probability for on-bits. `1.0` (default) plays
  every on-bit; lower values thin the rhythm stochastically. Off-bits
  are always silent (visual contract: white = no sound)
- **lo / hi** — MIDI note range the register output is mapped into
- **triggerMode** — `auto` (transport-driven), `gate` (advance only
  while a key is held), `seed` (incoming `noteOn` / `noteOff` writes
  the head bit; the player becomes the bit source)

Stencil emits unquantized chromatic notes. Chain a
[Pointsman](https://github.com/im9/pointsman) device downstream for
scale-locked output.

Full musical model: [`docs/ai/concept.md`](docs/ai/concept.md).

## Status

`m4l/` is feature-complete for v1 and in distribution prep; the
manual-Live verification gate is tracked by [ADR 006][adr6].

`vst/` is paused at scaffold. The plugin builds but is not
host-verified; the vst-internal architecture ADR is authored when vst
work resumes.

[adr6]: docs/ai/adr/006-m4l-release-verification.md

## Use (Max for Live)

Drop `m4l/Stencil.amxd` onto a MIDI track in Ableton Live and put an
instrument after it. Press play. Adjust **lock** and **density** to
taste. Chain a `Pointsman` device downstream to scale-lock the output.

Building from source is only needed if you want to modify the device —
see [Build](#build) below.

## Targets

| Target | Status | Notes |
|---|---|---|
| [Max for Live](m4l/) | v1 prep | Ableton Live MIDI effect. Current primary target. |
| [VST3](vst/) | Scaffold | Paused; resumes per a future vst-architecture ADR. |
| [AU](vst/) | Scaffold | Same codebase as the VST3. Paused. |

Musical logic is shared as a specification, not as code. m4l and vst
are independent native implementations. Cross-target conformance is
verified against
[`docs/ai/turing-test-vectors.json`](docs/ai/turing-test-vectors.json).
RNG primitives are also synchronized cross-repo with Pointsman via
[`docs/ai/rng-test-vectors.json`](docs/ai/rng-test-vectors.json) per
[ADR 005][adr5].

[adr5]: docs/ai/adr/archive/005-product-split.md

## Origin

The Turing Machine generator is adapted from
[inboil](https://github.com/im9/inboil), a browser-based groove box
where it lives inside a scene graph as one generative node among many.
Stencil lifts that node out into a standalone DAW-native MIDI effect —
the musical model and parameter design carry over; the scene-graph
architecture does not.

Stencil ships paired with [Pointsman](https://github.com/im9/pointsman),
the quantizer counterpart. The two are independent products; the
canonical chain is `Stencil → Pointsman` (see ADR 005).

## Build

Per-target build commands:

| Target | First time | Build | Test |
|---|---|---|---|
| `m4l/` (workspace) | `cd m4l && pnpm install` | `pnpm -r build` | `pnpm -r test` |
| `vst/` (VST3 + AU) | `git submodule update --init --recursive` | `cd vst && make build` | `cd vst && make test` |

m4l rebake after source edits: `cd m4l && pnpm bake` (refreshes
`Stencil.amxd` from `Stencil.maxpat`).

## Design docs

The musical model lives at [`docs/ai/concept.md`](docs/ai/concept.md).
Architectural decisions live under [`docs/ai/adr/`](docs/ai/adr/) —
start with [`docs/ai/adr/INDEX.md`](docs/ai/adr/INDEX.md) and read
individual ADRs only when the relevant area is being touched.

## License

[MIT](LICENSE). The Max for Live build ships free under the `im9`
label (see [`im9/stencil-m4l`](https://github.com/im9/stencil-m4l)
for the binary distribution). Native plugin builds (VST3 / AU /
CLAP) are planned as paid releases.
