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

`m4l/` is at v0.1.0; first release ships as `m4l-v0.1.0` on this repo's
GitHub Releases (free, brand-promo per [ADR 005][adr5]).

`vst/` is at v0.1.0: AU + VST3 + CLAP bundles built, signed, and
notarized as a macOS DMG. Architecture and host-load matrix are
captured by [ADR 007][adr7]. Paid release channel TBA.

## Use (Max for Live)

Drop `m4l/Stencil.amxd` onto a MIDI track in Ableton Live and put an
instrument after it. Press play. Adjust **lock** and **density** to
taste. Chain a `Pointsman` device downstream to scale-lock the output.

Building from source is only needed if you want to modify the device —
see [Build](#build) below.

## Targets

| Target | Status | Notes |
|---|---|---|
| [Max for Live](m4l/) | v0.1.0 | Ableton Live MIDI effect. Free brand-promo build via this repo's GitHub Releases (`m4l-v*` tags). |
| [VST3](vst/) | v0.1.0 | macOS, C++17 / JUCE. Self-build via `make build`; paid release TBA. |
| [AU](vst/) | v0.1.0 | macOS. Same codebase as the VST3. Self-build via `make build`; paid release TBA. |
| [CLAP](vst/) | v0.1.0 | macOS. Same codebase. `note-effect` build for Bitwig + Reaper. |

Musical logic is shared as a specification, not as code. m4l and vst
are independent native implementations. Cross-target conformance is
verified against
[`docs/ai/turing-test-vectors.json`](docs/ai/turing-test-vectors.json).
RNG primitives are also synchronized cross-repo with Pointsman via
[`docs/ai/rng-test-vectors.json`](docs/ai/rng-test-vectors.json) per
[ADR 005][adr5].

[adr5]: docs/ai/adr/archive/005-product-split.md

## DAW support

macOS only for v1 (per [ADR 007][adr7]). Windows / Linux distribution is
deferred. The vst/ build produces AU, VST3, and CLAP bundles together;
the table below covers per-host compatibility on macOS.

| DAW | Format | Status | Notes |
|---|---|---|---|
| Logic Pro | AU | ✅ Primary | AU MIDI FX slot on a software-instrument track. (Logic does not host CLAP.) Verified 2026-05-17. |
| Bitwig Studio | VST3 / CLAP | ✅ Primary | Note FX slot in front of an instrument. CLAP is Bitwig's native plug-in format. CLAP + VST3 verified 2026-05-17. |
| Reaper | VST3 / CLAP | ⚠️ Best-effort | VST3 or CLAP in any MIDI FX chain. Load verified 2026-05-17; not exhaustively tested for v1. |
| Studio One | VST3 | ⚠️ Best-effort | VST3 in MIDI fx slot. Not formally tested for v1 (no host on hand). CLAP build is also produced but unverified in Studio One. |
| Ableton Live | — | Use m4l/ | Live does not accept third-party VST3 / AU plug-ins in its MIDI Effect rack (host design, not a format limitation) and does not host CLAP. The [Max for Live device](m4l/) is the supported path. |
| Cubase / Nuendo | — | ❌ Out of scope | The VST3 spec has no "MIDI Effect" sub-category and Cubase rejects third-party VST3 in its MIDI Inserts slot (Steinberg policy). Loading Stencil as an Instrument with two-track MIDI-out routing works mechanically but conflicts with the "MIDI fx, not synth" identity Stencil is built on. Revisit only if Cubase opens its MIDI Inserts to third-party VST3. |
| FL Studio | — | ❌ Out of scope | FL has no MIDI fx routing on any plug-in surface: VST3 channel slot accepts only instruments, mixer hosts only audio fx, no MIDI fx slot exists; CLAP `note-effect` plug-ins are not bridged to FL's internal note bus. Reconsider only if FL adds a native MIDI fx track concept. |

[adr7]: docs/ai/adr/archive/007-vst-architecture.md

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
| `vst/` (VST3 + AU + CLAP) | `git submodule update --init --recursive` | `cd vst && make build` | `cd vst && make test` |

m4l rebake after source edits: `cd m4l && pnpm bake` (refreshes
`Stencil.amxd` from `Stencil.maxpat`).

## Design docs

The musical model lives at [`docs/ai/concept.md`](docs/ai/concept.md).
Architectural decisions live under [`docs/ai/adr/`](docs/ai/adr/) —
start with [`docs/ai/adr/INDEX.md`](docs/ai/adr/INDEX.md) and read
individual ADRs only when the relevant area is being touched.

## License

Licensed per target:

- `m4l/` — [MIT](m4l/LICENSE). Free to use, modify, and redistribute.
  Binary distribution via this repo's GitHub Releases under the
  `m4l-v*` tag namespace.
- `vst/` — [Proprietary, source-available](vst/LICENSE). Read, self-build,
  and personal non-commercial use are permitted. Redistribution and
  commercial use require permission from im9. Binaries are sold by im9.
- `docs/` — [MIT](docs/LICENSE). Shared design notes and ADRs.

Third-party components under `vst/JUCE/`, `vst/clap-juce-extensions/`, and
the CMake `_deps/` tree retain their own licenses.
