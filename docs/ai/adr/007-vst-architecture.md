# ADR 007: Stencil VST architecture — JUCE MIDI Effect (VST3 + AU + CLAP)

## Status: Proposed

**Created**: 2026-05-09
**Revised**: 2026-05-10 — §MIDI processing porting omission fix:
mode dispatch (gate = range midpoint, velocity = `(0.3 + frac × 0.7) × outputVelocity`),
"bit-tap active" semantic (`active = (reg & 1) || densityDraw`),
seed-mode register-freeze semantics. Original draft described semantics
not matching the m4l reference; corrected against `m4l/host/host.ts`.
**Revised**: 2026-05-10 — §FREEZE / ROLL semantics clarified: the
"vst inherits inboil's click→ROLL" framing was correct for vst but
incorrectly implied m4l did the same. Section now records both as
target-specific UX choices (m4l = direct bit edit via `setBit`
because the compact strip has no FREEZE / ROLL buttons; vst =
inboil-style click-to-ROLL because the dialog-class editor exposes
the explicit buttons). Also dropped the inaccurate
"`tmStep` re-derives register from `(seed, position)`" claim that
had been used to justify the persistence requirement.
**Revised**: 2026-05-10 — Hung-note flush expanded to cover the
remaining §Note-off discipline paths (parameter change, state
load, bypass enable) and §Audit follow-ups checklist added so
the residual items from the 2026-05-10 audit can be addressed
incrementally without re-opening §Implementation checklist.
**Revised**: 2026-05-10 — §Audit follow-ups extended with two
new sub-sections (Real-time safety, Edge cases / behavior) from
the 2026-05-10 RT-safety + latent-bug pass: audio-thread vector
allocation churn, editor-snapshot tuple coherence, input-MIDI
block quantization, `outputGate=0` zero-length notes, seed-mode
register wipe on transport start, ROLL same-ms double-click,
plus a `RightRailView::pollTimer_` dead-field cleanup.
**Revised**: 2026-05-10 — judgment-free §Audit follow-ups items
landed (7 of 16): rangeLo / rangeHi clamp direction documented in
§Parameter surface; `mapToNote` +1 framing documented in §Engine
port; `concept.md` §Transport reworded to "reset on transport
start"; §MIDI processing "pure-density" replaced with the
inboil-vs-Stencil mix description; §Persistence "13 parameters"
annotated as vst-APVTS count; §MIDI processing intro reworded
around `audio.clear()`; ROLL seeding switched to `juce::Random`
across `RingView` / `ActionsView` / `RightRailView`; dead
`pollTimer_` field removed from `RightRailView.h`.

This ADR specifies the `vst/` target's architecture: the plugin formats
shipped, the C++17 source layout (`Engine` / `Plugin` / `Editor`), the
APVTS-backed parameter surface, MIDI processing, editor design ported
from inboil's `TuringSheet.svelte`, the CMake build (including CLAP via
`clap-juce-extensions`), and the test strategy. It is the
`per-product vst-architecture ADR` that
[ADR 005 §vst/AU architecture](archive/005-product-split.md) defers to.

The m4l-side `live.*` parameter shape, jsui register-ring renderer, and
n4m host bridge from [ADR 002][adr2] / [ADR 003][adr3] do **not** carry
over — vst is a ground-up C++/JUCE implementation. Only the cross-target
contract (engine semantics + test vectors per [ADR 001][adr1]) and the
canonical parameter surface from [concept.md §Parameter surface][concept-params]
are shared.

[adr1]: archive/001-engine-interface.md
[adr2]: archive/002-m4l-architecture.md
[adr3]: archive/003-m4l-ui-design.md
[concept-params]: ../concept.md#parameter-surface-canonical

## Context

### Musical motivation

The m4l target is Ableton-only. Stencil's musical role —
chromatic-pitch generator that drives a downstream Pointsman scale
quantizer or a sampler / single-pitch synth — is equally valid in
Logic Pro and Bitwig Studio (the two primary non-Live MIDI-effect
hosts on macOS), with Reaper and Studio One as best-effort. Every
non-Live workflow currently loses access to Stencil. The vst target
opens the same musical behavior to those workflows without compromise:
the same shift register evolution, the same
`(seed, length, lock, position)` determinism, the same TM → QT chain
idiom, just hosted by a native plugin instead of `[node.script]`.

Cubase / Nuendo and FL Studio are explicitly out of scope (see
§Scope §Out of scope) — both refuse to host third-party VST3 / CLAP
note-effect plugins in any MIDI-effect-equivalent slot, so there is no
DAW surface to ship into. This matches the oedipa support matrix
verbatim; the rejection is a host-policy / spec-shape constraint
shared by every DAW-native MIDI generator at im9, not a per-product
decision.

The cross-target conformance contract from [ADR 001][adr1] is the
floor: for any `(seed, length, lock, density, range)`, the vst plugin
emits the same `(note, active)` sequence as m4l, bit-for-bit, against
the shared test vectors. Users moving a project between Live and
Logic must hear the same loop.

### Architectural motivation

The scaffold at `vst/` already exists from [ADR 005 Phase 3][adr5-phase3]
trim — JUCE submodule, `CMakeLists.txt`, `Makefile`, an empty
`PluginProcessor` skeleton. No production code: no engine port,
no parameters, no MIDI processing, no editor. The hosting surface
question (formats, threading, parameter model, persistence) is
unresolved.

[adr5-phase3]: archive/005-product-split.md#phase-3--stencil-repo-migration-this-repo

The companion product **oedipa** (`~/src/vst/oedipa/`) is a
production-shipped JUCE MIDI Effect with VST3 + AU + CLAP and a
working test harness. Its
[`vst/CMakeLists.txt`](../../../../oedipa/vst/CMakeLists.txt),
`Source/Engine` / `Source/Plugin` / `Source/Editor` split, custom
test main, and `clap-juce-extensions` wiring are the proven template
for this ADR — Stencil's vst inherits the same shape with
TM-specific source files in place of Tonnetz / Walker / Lattice.

### Visual reference

The editor follows inboil's `TuringSheet.svelte` (`~/src/front/inboil/src/lib/components/TuringSheet.svelte`)
as the visual specification — the m4l jsui register-ring is
*not* the reference. inboil's TM panel is the original design and
sits in a desktop-class window; vst has the same canvas budget. The
m4l version was specifically compressed to fit a 320×130-ish
Ableton device strip and traded layout density for that constraint.

The inboil layout (port verbatim in spirit):

```
┌────────────────────────────────────────────────────────────┐
│ STENCIL — TURING MACHINE                              [×] │
├────────────────────────────────────────┬───────────────────┤
│                                        │ ┌─Parameters───┐ │
│        ┌──────FREEZE  ROLL──────┐      │ │ LEN  16  bits │ │
│        │           ▲             │      │ │ LOCK ▭▭ 0.18 │ │
│        │      (head bit         │      │ │ DENS ▭▭ 0.38 │ │
│        │       salmon)          │      │ └───────────────┘ │
│        │  ◯  ◯       ●  ◯       │      │ ┌─Mode──────────┐ │
│        │     bit ring           │      │ │ NOT GAT VEL   │ │
│        │  ●  0.83 / E6   ◯      │      │ │ bits → pitch  │ │
│        │     ◯  ◯  ◯  ◯         │      │ └───────────────┘ │
│        │                        │      │ ┌─Output────────┐ │
│        └────────────────────────┘      │ │ RANGE 48..72  │ │
│                                        │ │ VEL 100  GT.5 │ │
│                                        │ │ SUBDIV 16th   │ │
│                                        │ └───────────────┘ │
│                                        │ ┌─Trigger───────┐ │
│                                        │ │ AUTO GATE SEED│ │
│                                        │ │ IN CH  0(omni)│ │
│                                        │ └───────────────┘ │
│                                        │ ┌─Reproducibility┐ │
│                                        │ │ SEED 42  [↻]  │ │
│                                        │ └───────────────┘ │
├────────────────────────────────────────┴───────────────────┤
│ ▌ ▍ ▏  ▎ ▍ ▌  ...  bottom history bars (read-only)         │
└────────────────────────────────────────────────────────────┘
```

inboil's `Target` fieldset (MERGE/REPLACE/FILL + TRACK + PRESET) is
inboil-internal scene-graph routing — not applicable to a DAW-native
MIDI plugin. Stencil's right rail keeps Parameters + Mode and adds
Output / Trigger / Reproducibility fieldsets that map to the
canonical parameter surface in [concept.md][concept-params].

inboil palette tokens used by the renderer (sourced from
`~/src/front/inboil/src/app.css`):

| Token             | Value      | Role                                            |
|-------------------|------------|-------------------------------------------------|
| `--color-bg`      | `#EDE8DC`  | warm cream — plugin background                  |
| `--color-fg`      | `#1E2028`  | dark navy — text, inactive bit outline          |
| `--color-olive`   | `#787845`  | olive — active bit, slider accent, history bar  |
| `--color-blue`    | `#4472B4`  | steel blue — frozen-state indicator             |
| `--color-salmon`  | `#E8A090`  | salmon — head bit / mutated bit (the read head) |

`Source/Editor/Theme.cpp/.h` exposes these as `juce::Colour`
constants; every editor component reads from `Theme` rather than
hard-coding hex.

## Decision

### Plugin formats

Ship three formats; keep one for development only.

| Format     | Status                | Rationale                                                                                                  |
|------------|-----------------------|------------------------------------------------------------------------------------------------------------|
| **VST3**   | Shipped               | Bitwig Studio (Note FX), Reaper, Studio One (MIDI fx). Bitwig + Reaper are oedipa-verified, Studio One best-effort. |
| **AU**     | Shipped (`aumi`)      | Logic Pro AU MIDI FX slot; AU is the only path to Logic. (Logic does not host CLAP.)                       |
| **CLAP**   | Shipped (note-effect) | Bitwig native, Reaper. Forward-leaning format; same `note-effect` tag oedipa ships.                        |
| Standalone | Dev only              | Keep enabled for `make open`-style dev iteration (matches oedipa). Not in any release.                     |

CLAP support uses the
[`clap-juce-extensions`](https://github.com/free-audio/clap-juce-extensions)
submodule + JUCE 8.x. `CLAP_FEATURES` is `note-effect utility`
(matches oedipa's tag for the Tonnetz quantizer; "note-effect" is the
canonical tag for MIDI generators in CLAP's feature taxonomy).

Live (Ableton) is **not** a vst host for Stencil — Live rejects
third-party VST3 / AU in its MIDI Effect rack and does not host CLAP
(host design, not a format limitation). Live users continue to use
the m4l build (free brand-promo per
[ADR 005 §Distribution posture][adr5-dist]). The vst target serves
the supported non-Live macOS DAWs (Logic + Bitwig primary, Reaper +
Studio One best-effort); Cubase / Nuendo and FL Studio are excluded
on host-policy grounds, not as a Stencil-specific choice (see §Scope
§Out of scope for the rationale, which is identical to oedipa's).

[adr5-dist]: archive/005-product-split.md#distribution-posture

### Source layout

Mirror oedipa's split exactly so the iOS-reuse boundary
(engine = no JUCE) and the test linkage strategy (plugin core
without `juce_audio_plugin_client`) are inherited.

```
vst/
  CMakeLists.txt
  Makefile
  JUCE/                                  (submodule, 8.0.12+)
  clap-juce-extensions/                  (submodule, 0.26.x+)
  Source/
    Engine/                              pure C++17, NO juce_* link or include
      Rng.h, Rng.cpp                       xoshiro128++ + SplitMix64
      Turing.h, Turing.cpp                 createRegister, shiftAndFlip,
                                           shiftAndForce, registerToFraction,
                                           mapToNote, tmStep
      Sequencer.h, Sequencer.cpp           transport-driven step scheduler:
                                           PPQ → subdivision boundary detection,
                                           hung-note tracking, panic
    Plugin/                              JUCE AudioProcessor + APVTS
      PluginProcessor.h, PluginProcessor.cpp
      Parameters.h, Parameters.cpp         APVTS layout + IDs + ranges
    Editor/                              JUCE Editor — inboil TuringSheet port
      PluginEditor.h, PluginEditor.cpp     top-level layout (header / body / history)
      Theme.h, Theme.cpp                   palette + typography tokens
      RingView.h, RingView.cpp             left-side bit ring (revolver rotation,
                                           head bit, fraction + note text)
      RingLogic.h, RingLogic.cpp           pure hit-test math (testable)
      RightRailView.h, RightRailView.cpp   right-side fieldset stack
      HistoryView.h, HistoryView.cpp       bottom output-history bars
      ActionsView.h, ActionsView.cpp       FREEZE / ROLL buttons
  tests/
    main.cpp                             custom Catch2 main (owns JUCE init)
    test_Rng.cpp                         vectors against rng-test-vectors.json
    test_Turing.cpp                      vectors against turing-test-vectors.json
    test_Sequencer.cpp                   transport-driven step scheduling
    test_Plugin.cpp                      APVTS round-trip, state I/O
    test_RingLogic.cpp                   hit-test math (FREEZE-toggle bit)
    test_Editor.cpp                      JUCE component smoke tests
```

`Engine/` has zero JUCE dependency. The plugin and test binary both
link a static library built from `Engine/`. A second static library
(`stencil_plugin_core`) bundles `Plugin/` + `Editor/` *without*
`juce_audio_plugin_client`, so tests can instantiate
`StencilProcessor` and exercise APVTS / state / editor without
pulling in plugin wrappers (matches oedipa's `oedipa_plugin_core`
pattern).

### Engine port

Vendor copy from `m4l/engine/` semantics, ground-up C++17. Per
`project_no_shared_lib_yet` memory: the RNG and TM code are small
(~200 LOC total) and the cross-product synchronization mechanism is
the test vector JSON, not a shared package. The engine reads
`docs/ai/rng-test-vectors.json` and
`docs/ai/turing-test-vectors.json` (already present in this repo
from ADR 005 Phase 3 / ADR 001) and asserts byte-identical
conformance with m4l.

The C++ engine API mirrors ADR 001 §TM interface:

```cpp
namespace stencil::engine {

struct RngState { uint32_t s[4]; };       // xoshiro128++ state
RngState seedRng(uint64_t seed);          // SplitMix64-fill of s[]
uint32_t nextU32(RngState& rng);          // xoshiro128++ next
double   drawUniform(RngState& rng);      // [0, 1)

using RegisterBits = uint32_t;
using Length       = int;                 // 2..32
using Lock         = double;              // 0..1
using Density      = double;              // 0..1

RegisterBits createRegister(Length length, RngState& rng);
RegisterBits shiftAndFlip(RegisterBits reg, Length length, Lock lock, RngState& rng);
RegisterBits shiftAndForce(RegisterBits reg, Length length, int forceBit);
double       registerToFraction(RegisterBits reg, Length length);
int          mapToNote(double fraction, int rangeLo, int rangeHi);

struct TmStepResult { RegisterBits register_; int note; bool active; };
TmStepResult tmStep(RegisterBits reg, Length length, Lock lock,
                    Density density, int rangeLo, int rangeHi, RngState& rng);

}
```

`tmStep` draw order is **density first, flip second** (binding for
test-vector parity per [ADR 001 §Step composition][adr1-step]).

[adr1-step]: archive/001-engine-interface.md

`mapToNote` computes `floor(lo + (num × (hi - lo + 1)) / den)`
clamped to `hi` — the `+1` divides the `[lo, hi]` range into
`hi - lo + 1` equal-width buckets so every integer note in the
range receives the same fraction-of-`registerToFraction` mass.
This intentionally diverges from inboil's `round(lo + frac × (hi - lo))`,
which gives the endpoints half-buckets and so under-represents
`lo` and `hi` over a long sweep. Stencil's mapping was chosen
when porting the algorithm; turing-test-vectors.json bakes the
`+1` formula into the cross-target conformance contract.

### Parameter surface (APVTS)

`Source/Plugin/Parameters.cpp` defines the APVTS layout. Parameter
IDs, ranges, and defaults match
[concept.md §Parameter surface][concept-params] verbatim — the same
table that m4l's `live.*` set was derived from.

| Param ID         | Type                  | Range / values                          | Default  | Skew  |
|------------------|-----------------------|-----------------------------------------|----------|-------|
| `length`         | `AudioParameterInt`   | `2..32`                                 | `8`      | —     |
| `lock`           | `AudioParameterFloat` | `0.0..1.0`                              | `0.5`    | linear|
| `density`        | `AudioParameterFloat` | `0.0..1.0`                              | `1.0`    | linear|
| `rangeLo`        | `AudioParameterInt`   | `0..127`                                | `48`     | —     |
| `rangeHi`        | `AudioParameterInt`   | `0..127`                                | `72`     | —     |
| `subdivision`    | `AudioParameterChoice`| `8th, 16th, 32nd, 8T, 16T`              | `16th`   | —     |
| `seed`           | `AudioParameterInt`   | `0..2^31-1`                             | `42`     | —     |
| `mode`           | `AudioParameterChoice`| `note, gate, velocity`                  | `note`   | —     |
| `triggerMode`    | `AudioParameterChoice`| `auto, gate, seed`                      | `auto`   | —     |
| `inputChannel`   | `AudioParameterInt`   | `0..16` (`0` = omni)                    | `0`      | —     |
| `outputVelocity` | `AudioParameterInt`   | `1..127`                                | `100`    | —     |
| `outputGate`     | `AudioParameterFloat` | `0.0..1.0`                              | `0.5`    | linear|
| `outputChannel`  | `AudioParameterInt`   | `1..16`                                 | `1`      | —     |

The `rangeLo ≤ rangeHi` invariant is enforced in `PluginProcessor`
via an APVTS listener, not in the engine — engine remains a pure
function. The vst clamp pushes the *other* side to follow (move
`rangeLo` past `rangeHi` and `rangeHi` rises to match), since
APVTS UX expects the side the user did not just touch to give way.
This intentionally diverges from m4l's `host.ts` clamp, which
snaps the side the user just moved (move `rangeLo` past `rangeHi`
and `rangeLo` is pulled back down): m4l's `live.numbox` widgets
display the same slider value the user committed, while vst's
APVTS sliders move continuously and need the other endpoint to
track. Both shapes preserve `lo ≤ hi`; the difference is which
endpoint the user observes "moving on its own" after a crossing.

`AudioParameterChoice` is the right type for `subdivision` / `mode` /
`triggerMode` (CLAP and AU both surface choice params correctly).

### MIDI processing

`processBlock` is the only audio-thread callback. Stencil produces
no audio; `IS_MIDI_EFFECT TRUE` instructs hosts not to route audio
through it, but the audio buffer the host hands `processBlock` is
not necessarily zero on entry, so `processBlock` calls
`audio.clear()` defensively at the top before any MIDI work.

Per-block:

1. Read `AudioPlayHead::PositionInfo` (`getPpqPosition`,
   `getBpm`, `getIsPlaying`).
2. If transport went `playing` → `stopped`, emit panic
   (all-notes-off across all channels) and reset state.
3. For each input MIDI message in the block, filter by
   `inputChannel` (0 = omni) and route by `triggerMode`:
   - `auto` — input ignored (transport drives advance only).
   - `gate` — input `noteOn` adds to held-set, `noteOff` removes.
     While the held-set is empty, transport-driven steps are silent
     and register / rng are frozen (matches m4l).
   - `seed` — `noteOn` calls `shiftAndForce(reg, length, 1)` and
     `noteOff` calls `shiftAndForce(reg, length, 0)`. The seed write
     bypasses `lock` entirely. After the first seed-input event the
     "seed-active" flag latches and stays latched until `triggerMode`
     changes; once latched, transport steps no longer call
     `shiftAndFlip` — the register is frozen (input drives it).
     Density draws still consume one rng word per step so the rng
     thread stays in lockstep across mode/density combinations
     (matches m4l host behavior per
     [ADR 002 §Stencil TM][adr2-tm]).
4. Detect subdivision boundaries inside the block: convert each
   PPQ tick to a subdivision index using
   `subdivision_per_quarter[subdivision]` and emit step events at
   sample-accurate offsets within the buffer.
5. Each step event (when not silenced by `gate` mode):
   a. Read `reg`. Compute the per-mode `(note, velocity)` pair (see
      §Output modes below).
   b. Draw one u32 from `rng` for the density decision (always
      consumed, regardless of bit-tap outcome).
   c. **Bit-tap active**:
      `active = ((reg & 1) == 1) || (densityDraw < probabilityThreshold(density))`.
      The LSB at the read head is the primary trigger (always fires
      when set); density draws fill in on the empty bits. This
      diverges from inboil's mode-dispatched mix
      (`note` mode uses `regValue > (1-density) * 0.5`, while
      `gate` and `velocity` modes use `rng() < density`); Stencil
      uses bit-tap for *all* modes so the LSB at the read head
      remains audibly the primary rhythmic driver and `density`
      remains a uniform "empty-bit fill" knob across modes.
   d. If `active`, schedule the `(note, velocity)` noteOn at the
      subdivision sample offset and the matching noteOff at
      `outputGate × stepDuration` later (clipped to the next step
      boundary).
   e. If not in seed-active state, advance `reg` via
      `shiftAndFlip(reg, length, lock, rng)`. In seed-active state,
      the register is left untouched (rng was already advanced for
      the density draw above; no flip draw occurs).
6. Output buffer is filled in sample-accurate order via
   `MidiBuffer::addEvent` per scheduled event.

[adr2-tm]: archive/002-m4l-architecture.md

#### Output modes

`mode` parameter controls how each step's `(note, velocity)` pair is
computed from `reg` and the parameter set. All three modes share the
same active-decision and rng-advance flow above; only the
`(note, velocity)` mapping differs.

| Mode       | `note`                                    | `velocity`                                                          |
|------------|-------------------------------------------|---------------------------------------------------------------------|
| `note`     | `mapToNote(reg, length, rangeLo, rangeHi)` | `outputVelocity`                                                    |
| `gate`     | `floor((rangeLo + rangeHi) / 2)`          | `outputVelocity`                                                    |
| `velocity` | `mapToNote(reg, length, rangeLo, rangeHi)` | `clamp1to127(floor((0.3 + frac × 0.7) × outputVelocity))`           |

where `frac = registerToFraction(reg, length).num /
registerToFraction(reg, length).den` (computed in float for the
velocity dispatch path; mapToNote's integer path is unaffected).

The "gate = range midpoint" choice is a rhythmic-articulation framing:
the user sets a melodic range for `note`/`velocity` modes, but
switching to `gate` repurposes that same range as a rhythm-only mode
where the choice of pitch is incidental — the midpoint is a stable,
audible default.

The "velocity 0.3 + frac × 0.7" formula clamps the lowest output
velocity to 30 % of `outputVelocity` (so the loop is always audible)
and lets the bit-pattern modulate the upper 70 % of dynamic range.

These mappings are taken from m4l's `host-tm/host.ts` (steps 142-172
of the live reference) and must stay byte-identical with the m4l
target — they are the spec, not a target-specific embellishment.
[concept.md §Future extensions][concept-future] notes the three modes
are part of the canonical surface, not deferred.

[concept-future]: ../concept.md#future-extensions

#### Note-off discipline

Required hung-note paths (every one of these flushes
`notesOnTracker` with `noteOff` before applying the change):

- transport stop / pause (between blocks, or detected mid-block)
- bypass enable
- preset / state load (`setStateInformation`)
- parameter change to `length` / `seed` / `range` / `subdivision` /
  `mode` / `outputChannel`
- explicit `panic` (all-notes-off CC 123 on every channel)

The tracker lives in `Sequencer` (engine layer) so this logic is
unit-testable without a JUCE `MidiBuffer`.

### Editor — inboil TuringSheet port

#### Layout

```
PluginEditor (top-level)
├── HeaderBar     (h: 40px, "STENCIL — TURING MACHINE")
├── Body          (flex)
│   ├── RingView      (flex; left)
│   │   ├── ActionsView (FREEZE, ROLL — top of ring)
│   │   ├── BitRing     (svg→Path-based component)
│   │   └── CenterText  (fraction "0.83" + note "E6")
│   └── RightRailView (280px fixed; vertical fieldset stack)
│       ├── Parameters (LEN, LOCK, DENS)
│       ├── Mode       (NOT/GAT/VEL pills + description)
│       ├── Output     (RANGE lo/hi, VEL, GATE, SUBDIV)
│       ├── Trigger    (AUTO/GATE/SEED pills, IN CH)
│       └── Reproducibility (SEED + reroll button)
└── HistoryView   (h: 136px, bottom)
```

Initial size: `820 × 540` px. Resizable; `RingView` flexes,
`RightRailView` stays at 280px, `HistoryView` stays at 136px height.

#### Logic / renderer split (per CLAUDE.md "GUI / UI components")

- `RingLogic` (pure C++): hit-test math — given click `(x, y)`
  inside the ring bounds, return the bit index (or `-1` if outside
  bit circles). Returns the next register value when a bit is
  clicked. Catch2-tested via simulated coordinates; no JUCE
  dependency.
- `RingView` (JUCE `Component`): paint loop — reads register from
  `PluginProcessor`, computes positions, draws the ring. On
  `mouseDown`, calls `RingLogic::hitTest`, applies the bit flip
  via APVTS-equivalent register write, repaints. Renderer has
  no business logic.

The same split applies to bit-ring revolver rotation (cumulative
step counter → rotation degrees) — pure math in `RingLogic`,
`AffineTransform` in `RingView`.

#### FREEZE / ROLL semantics

Inherited from inboil:

- **FREEZE** toggles `lock` between the user's previous value and
  `1.0`. The "previous lock" is held in the editor's transient
  state (not persisted via APVTS); on plugin reload, `lock` reads
  from APVTS and the toggle returns to "freeze freshly" mode.
  Visual state: olive button border when not frozen, blue
  (`--color-blue`) when frozen.
- **ROLL** writes a fresh value to the `seed` parameter (drawn
  uniformly from `0..2^31-1`). On the next subdivision boundary
  the engine re-creates the register from the new seed via
  `createRegister`. Behaves as a reseed, not a register stomp.

Direct bit-toggle (clicking a bit in the ring): vst v1 inherits
inboil's choice — **clicking a bit triggers ROLL** (re-seed)
rather than direct register edit. The musical effect — "I want a
different loop" — is preserved; the implementation cost of
APVTS-persisted register state (so a manual edit survives plugin
save / reload) is deferred. (See §Open questions for the
"register persistence" follow-up.)

The m4l target takes a different path: it exposes
`setBit(index, value)` so a click flips one bit of the running
register directly. The flipped bit lives in the register and
shifts naturally under subsequent `shiftAndFlip` steps; nothing
re-derives the register from `(seed, position)` mid-loop. m4l's
register isn't APVTS-persisted either, so the same
"survives-reload" caveat applies, but the compact-strip UX (no
FREEZE / ROLL buttons, single jsui surface) makes per-bit
authoring the natural primary affordance there. vst keeps
inboil's dialog-class layout including the explicit FREEZE / ROLL
buttons, so click-to-ROLL is the natural primary affordance and
direct bit-edit is the v2 question. Both choices are
target-specific UX, not a contract drift in the engine.

### Build (CMake)

`vst/CMakeLists.txt` follows oedipa's structure verbatim except for
TM-specific source file names. Top-level shape:

```cmake
project(Stencil VERSION 0.1.0)
add_subdirectory(JUCE)
add_subdirectory(clap-juce-extensions EXCLUDE_FROM_ALL)
include(FetchContent)              # Catch2 v3, nlohmann/json v3
# stencil_engine static library (Source/Engine/, no juce_*)
# juce_add_plugin(Stencil ... FORMATS VST3 AU Standalone)
#   - IS_MIDI_EFFECT TRUE
#   - NEEDS_MIDI_INPUT TRUE / NEEDS_MIDI_OUTPUT TRUE
#   - PLUGIN_MANUFACTURER_CODE "Im9x"
#   - PLUGIN_CODE "Stnl"
#   - COPY_PLUGIN_AFTER_BUILD TRUE
# clap_juce_extensions_plugin(TARGET Stencil
#     CLAP_ID "com.im9.stencil"
#     CLAP_FEATURES note-effect utility
#     CLAP_MANUAL_URL / CLAP_SUPPORT_URL)
# stencil_plugin_core static library (Plugin/ + Editor/, no juce_audio_plugin_client)
# stencil_tests executable (custom main, links engine + plugin_core + Catch2 + json)
#   - STENCIL_TEST_VECTORS_RNG_PATH (compile def)
#   - STENCIL_TEST_VECTORS_TM_PATH (compile def)
```

`Makefile` mirrors oedipa's: `make build` (Release), `make debug`,
`make test`, `make clean`, `make open` (Standalone for dev),
`make verify-artefacts`.

### Threading

- Audio thread (`processBlock`): owns `register`, `rng`,
  `notesOnTracker`, `position`. Reads APVTS atomically via
  `getRawParameterValue` (returns `std::atomic<float>*`).
- Editor thread: reads `register` snapshot through a single
  atomic `std::atomic<RegisterBits>` published by the audio
  thread at end of each step. No locking.
- APVTS handles parameter automation thread safety internally.
- State save/load (`getStateInformation` / `setStateInformation`)
  happens on the message thread; APVTS serializes its tree, the
  audio thread is paused around the call.

The register snapshot is "eventually consistent" — the editor
may show a register value one step behind under heavy load. This
is acceptable for visual feedback; never used as the source of
truth for MIDI emission.

## Persistence

APVTS is the single source of truth for persisted state.
`getStateInformation` / `setStateInformation` serialize the APVTS
`ValueTree` (XML inside the `MemoryBlock`) including all 13
parameters above. The "13" is the vst APVTS count: it splits
[concept.md §Parameter surface][concept-params]'s 11 canonical
items by exposing `range` as `rangeLo` / `rangeHi` separately
(host automation needs distinct IDs) and adds the m4l-shared
`outputChannel` for MIDI routing.

Non-persisted state (intentional):

- `register` value — derived from `(seed, length, position)` at
  transport start, so reproducibility holds across save/load. v2
  may persist if direct bit-edit lands.
- `rng` state — same, derived.
- `notesOnTracker` — ephemeral, cleared on reload.
- Editor-local "previous lock" for FREEZE — ephemeral.

No legacy migration: this is the v0.1.0 shape, no prior format
exists.

## UI

Per CLAUDE.md "GUI / UI components":

**Logic layer** (Catch2-tested in `tests/test_RingLogic.cpp`,
`tests/test_Editor.cpp`):

- `RingLogic::hitTest(x, y, bounds, registerLength) -> int`
- `RingLogic::bitPosition(idx, length, ringRadius, ringCenter) -> {x, y}`
- `RingLogic::rotationForStep(cumulativeSteps, length) -> degrees`
- `RingLogic::readingIndex(cumulativeSteps, length) -> int`
- `HistoryLogic::layoutBars(snapshots, barWidth, barGap) -> Rect[]`
- Action handlers: `freezeAction(currentLock, prevLock) -> {newLock, newPrevLock}`,
  `rollAction(rng) -> newSeed`

**Renderer** (manual verification in real DAWs, not unit-tested):

- `RingView::paint` — draws ring guides, head pointer, bit
  circles, head-bit highlight, mutated-bit highlight, fraction +
  note text.
- `HistoryView::paint` — draws bar series + note labels.
- `RightRailView` — `juce::Slider` / `juce::ComboBox` / `juce::TextButton`
  with `SliderAttachment` / `ComboBoxAttachment` / `ButtonAttachment`
  bindings to APVTS.
- Pixel layout, font choice, color palette match inboil; verified
  side-by-side against the inboil screenshot.

## Open questions

- **Direct bit-edit (register persistence)** — inboil's `toggleBit`
  also defers to ROLL for the same reason; v2 may add persisted
  register state and direct flip-on-click. Defer to a follow-up
  ADR if/when the user asks for it. Musical reasoning: the v1
  mental model is "shape the loop via lock + seed, not by
  drawing bits" (per [concept.md §What Stencil does][concept-what])
  — direct editing is a different musical interaction worth
  evaluating separately.

[concept-what]: ../concept.md

- **Cross-platform builds (Windows / Linux)** — first release
  targets macOS only. CMake / JUCE / clap-juce-extensions all
  support Windows + Linux out of the box; no architectural
  blocker. Defer to a distribution-time ADR alongside code
  signing / installers / CI matrix. Musical experience is
  identical cross-platform; this is purely a packaging concern.

- **CLAP support level** — `note-effect utility` is the right
  feature tag for v1. CLAP-specific extensions (per-note pitch
  bend / param-modulation / state-context) are deferred until a
  user actually requests MPE-style output (see
  [concept.md §Future extensions][concept-future]).

- **Standalone format inclusion in releases** — keep it disabled
  in release builds (only macOS Audio Plug-Ins folder gets the
  `.vst3` / `.component` / `.clap`); enable Standalone via a
  CMake option for dev builds only. Decision committed.

## Scope

### In scope

- VST3 + AU + CLAP MIDI Effect plugin formats
- C++17 engine port with byte-identical RNG / TM behavior vs m4l
- APVTS-backed parameter surface matching concept.md
- Sample-accurate transport-driven step scheduling, hung-note
  discipline, panic
- Inboil-style editor: ring + revolver rotation + history bars +
  4-fieldset right rail
- Catch2 v3 test harness with shared test vector loading
- macOS build via CMake + Makefile, matching oedipa template

### Out of scope

- **Direct bit-edit on the ring** — v1 routes click → ROLL.
  *Musical reason:* inboil's mental model is "steer the loop with
  lock + seed," not "author the bits"; direct editing is a
  different musical affordance worth evaluating once v1 lands in
  user hands. Persistence support also requires register state
  in APVTS; feasibility study before committing.
- **Cross-platform builds (Windows / Linux)** — *Musical reason:*
  experience is identical, this is a packaging-time decision
  that doesn't shape the plugin's architecture. Will be resolved
  in a distribution ADR (alongside code signing, notarization,
  installers, CI matrix).
- **Cubase / Nuendo support** — *Musical reason:* Stencil's
  identity is "MIDI fx, not synth" (concept.md §Topology).
  The VST3 spec has no `MIDI Effect` sub-category and Cubase /
  Nuendo refuse third-party VST3 in the MIDI Inserts slot
  (Steinberg policy). The mechanically-possible workaround —
  ship Stencil as a VST3 instrument with a two-track MIDI-out
  routing — collapses the "single-purpose MIDI effect" framing
  the product is built on (cf. ADR 005). oedipa rejected the
  same workaround; Stencil follows. Revisit only if Steinberg
  opens the MIDI Inserts slot to third-party plugins.
- **FL Studio support** — *Musical reason:* same identity
  argument. FL has no MIDI fx routing surface at all: the
  channel slot is instrument-only, the mixer is audio-fx-only,
  and FL's CLAP host does not bridge `note-effect` plugins to
  FL's internal note bus (oedipa verified empirically
  2026-05-09). There is no slot to ship into without
  re-skinning Stencil as something it is not. Revisit only if
  FL adds a MIDI fx track concept or routes CLAP `note-effect`
  output natively.
- **MPE / per-note expression output** — *Musical reason:*
  Stencil's contract is "one monophonic note per step" per
  [concept.md §Polyphony][concept-poly]. MPE expands the musical
  model meaningfully; deserves its own ADR rather than a corner
  of this one.

[concept-poly]: ../concept.md

- **Preset / slot bank** — *Musical reason:* listed in
  [concept.md §Future extensions][concept-future] as "useful once
  the product is in real use" — i.e. specifically deferred until
  v1 is shipped and validated. Adding it pre-ship designs blind.
- **"Bars per loop" length parameterization** — *Musical reason:*
  same source; concept.md notes the rethink is needed but
  intentionally hasn't been spec'd. v1 ships bit-count `length`
  as designed.
- **Code signing / notarization / installer** —
  distribution-time, not architectural. Per ADR 005 §Distribution
  posture, vst is paid; pricing / channel / signing are TBD and
  will land in a distribution ADR when shipping is imminent.
- **State migration from m4l preset chunks** — Live presets and
  vst APVTS use unrelated formats; round-trip between the two
  hosts is by-MIDI-routing, not by state import. *Musical reason:*
  the user's musical state is the parameter values, not the
  format-specific blob; mirroring concept.md's parameter surface
  in vst means the same project sounds the same when re-set up.

## Implementation checklist

Phased per CLAUDE.md TDD gates. Each phase ends with `make test`
green; phases are sized so the working tree compiles and tests
pass at every phase boundary.

### Phase 1 — Engine port (`Source/Engine/`)

- [x] Add `Source/Engine/Rng.h` + `Rng.cpp` — `seedRng`, `nextU32`,
      `drawUniform` (xoshiro128++ + SplitMix64). `tests/test_Rng.cpp`
      loads `docs/ai/rng-test-vectors.json` via nlohmann/json and
      asserts byte-identical output for every vector case.
- [x] Add `Source/Engine/Turing.h` + `Turing.cpp` — `createRegister`,
      `shiftAndFlip`, `shiftAndForce`, `registerToFraction`,
      `mapToNote`, `tmStep`. `tests/test_Turing.cpp` loads
      `docs/ai/turing-test-vectors.json` and asserts every case.
- [x] Add `Source/Engine/Sequencer.h` + `Sequencer.cpp` — PPQ →
      subdivision-boundary detection, hung-note tracker, panic.
      `tests/test_Sequencer.cpp` covers boundary detection at
      multiple BPM / subdivisions, transport stop → panic, all
      `triggerMode` branches, mode = note / gate / velocity output
      shape.
- [x] Update `vst/CMakeLists.txt` to define `stencil_engine` static
      library and FetchContent Catch2 v3 + nlohmann/json v3. Wire
      `tests/main.cpp` (custom main owns JUCE init).
- [x] `make test` passes — engine vector parity verified
      (653 assertions / 36 test cases, cold-build green).

### Phase 2 — Plugin core (`Source/Plugin/`)

- [x] `tests/test_Plugin.cpp` — APVTS layout assertions (every
      param ID present, every range / default matches concept.md),
      state I/O round-trip (`getStateInformation` →
      `setStateInformation` returns identical tree).
- [x] `Source/Plugin/Parameters.h/.cpp` — APVTS layout factory.
- [x] `Source/Plugin/PluginProcessor.h/.cpp` — `prepareToPlay`,
      `processBlock` reading `AudioPlayHead`, calling Sequencer,
      writing `MidiBuffer`, `getStateInformation` /
      `setStateInformation`. APVTS listener clamps `rangeLo ≤ rangeHi`.
- [x] Define `stencil_plugin_core` static library in CMake (Plugin
      sources without `juce_audio_plugin_client`); link tests against it.
- [x] `make test` passes — APVTS round-trip + processing dispatch
      verified without instantiating the AU/VST3/CLAP wrappers
      (743 assertions / 50 test cases, cold-build green; plugin
      builds VST3 + AU + Standalone).

### Phase 3 — Editor logic + components (`Source/Editor/`)

- [x] `tests/test_RingLogic.cpp` — `hitTest`, `bitPosition`,
      `rotationForStep`, `readingIndex`, `freezeAction`,
      `rollAction`. Pure math; covers `length` ∈ {2, 4, 8, 16, 32}.
- [x] `tests/test_Editor.cpp` — instantiate `StencilEditor`, smoke-
      test `paint` doesn't crash, simulate `mouseDown` on ring →
      verify `seed` parameter changed (ROLL semantics).
- [x] `Source/Editor/Theme.h/.cpp` — palette + typography tokens
      from inboil reference.
- [x] `Source/Editor/RingLogic.h/.cpp`, `RingView.h/.cpp` — bit
      ring with revolver rotation, head-bit highlight, fraction +
      note center text.
- [x] `Source/Editor/ActionsView.h/.cpp` — FREEZE / ROLL buttons.
- [x] `Source/Editor/RightRailView.h/.cpp` — 5 fieldsets
      (Parameters / Mode / Output / Trigger / Reproducibility) with
      APVTS attachments for all 13 params. (`outputChannel` is
      attached and persisted but laid out at zero size; surfacing
      it as a visible row deferred to a UI iteration once the
      Trigger fieldset's vertical budget is reworked.)
- [x] `Source/Editor/HistoryView.h/.cpp` — output history bar series.
- [x] `Source/Editor/PluginEditor.h/.cpp` — top-level layout
      (header / body / history) + initial size 820 × 540.
- [x] `make test` passes — editor smoke + logic-layer cases green
      (1434 assertions / 73 test cases, cold-build green).

### Phase 4 — CLAP wiring + format matrix

- [x] Add `clap-juce-extensions` as a git submodule pinned to
      `0.26.x` (matches oedipa, exact commit
      `e8de9e8571626633b8541a54c2406fccc4272767`). Update top-level
      `git clone --recursive` instructions in `CLAUDE.md` §Setup.
- [x] CMake `clap_juce_extensions_plugin(TARGET Stencil ...)` with
      `CLAP_FEATURES "note-effect utility"`, `CLAP_ID
      "com.im9.stencil"`, `CLAP_MANUAL_URL` / `CLAP_SUPPORT_URL`
      pointing at the (planned) GitHub repo.
- [x] `make build` produces VST3 + AU + CLAP artifacts in
      `build/Stencil_artefacts/Release/{VST3,AU,CLAP}/`. Add
      `scripts/check-artefacts.sh` (port from oedipa) to validate
      bundle layout, exposed via `make verify-artefacts`.

### Phase 5 — JUCE-version-specific guards & docs

- [x] `target_compile_definitions(Stencil PRIVATE
      JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
      JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)` (matches oedipa).
- [x] Update [`CLAUDE.md`](../../../CLAUDE.md) §Build / §Layout for
      vst section: source tree, `make` targets, format list, CLAP
      submodule.
- [ ] Update [`docs/ai/adr/INDEX.md`](INDEX.md) — flip ADR 007
      row from `Proposed` to `Implemented` once §Verification is
      ticked.

## Verification

Manual + cross-target. Tick each item only after the verification
actually runs and passes; do not subdivide.

### Cross-target conformance (automated)

- [x] `make test` green: `test_Rng` (RNG vectors) + `test_Turing`
      (TM vectors) byte-identical to m4l. The same JSON files are
      consumed by both targets; no per-target hand-written cases.
      *(Verified 2026-05-15: 1455 assertions / 81 cases pass against
      `docs/ai/rng-test-vectors.json` + `docs/ai/turing-test-vectors.json`.)*

### Host load matrix (manual, macOS)

Two primary hosts, two best-effort. Cubase / Nuendo / FL / Live are
excluded by §Scope §Out of scope and do not appear here.

Primary (must pass):

- [ ] **Logic Pro** — load `Stencil.component` as AU MIDI FX in a
      software-instrument track. Plays clock, emits notes,
      survives transport start/stop. No hung notes on bypass.
- [ ] **Bitwig Studio** — load `Stencil.clap` (preferred) and
      `Stencil.vst3` in a Note FX slot in front of an instrument.
      Same checks for each format. CLAP load and VST3 load both
      verified click-free (matches oedipa's verification bar).

Best-effort (load + smoke; not blocking for v1):

- [ ] **Reaper** — load `Stencil.vst3` and `Stencil.clap` in a
      MIDI Effect chain. Plays clock, emits notes.
- [ ] **Studio One** — load `Stencil.vst3` in a MIDI fx slot.
      Loads and emits notes (formal verification deferred; CLAP
      build is produced but not verified in Studio One).

### Functional correctness (manual)

- [ ] **All 13 parameters** are visible in each host's parameter
      list and persist across save/reload.
- [ ] **Cross-target audible parity** — load `Stencil.amxd` (m4l)
      and `Stencil.vst3` with identical `(seed, length, lock,
      density, range, subdivision)`. Run both at 120 BPM through
      the same synth. Output matches by ear and event-by-event in
      a clip recording.
- [ ] **Output modes** — `note` (default pitch from bits), `gate`
      (on/off only at lowest range note), `velocity` (varying
      velocity). Each verified audibly.
- [ ] **Trigger modes** — `auto` (transport-driven), `gate` (key-
      held), `seed` (input notes seed register). Each verified
      with input MIDI in the chosen host.
- [ ] **Hung-note discipline** — transport stop emits noteOff for
      every active note. Bypass mid-note emits noteOff. Preset
      load between sounding notes emits noteOff first. Verified
      with MIDI monitor / synth release.
- [ ] **FREEZE / ROLL** — FREEZE jumps `lock` to `1.0`,
      preserves the loop; un-FREEZE restores prior lock value.
      ROLL re-seeds; new loop emerges next subdivision.
- [ ] **Visual fidelity** — ring + history side-by-side with
      inboil reference screenshot: palette, typography, layout
      proportions match within reasonable JUCE-paint tolerance.

### Build hygiene

- [x] `scripts/check-artefacts.sh` reports VST3 + AU + CLAP
      bundles present with valid Info.plist / manifest.
      *(Verified 2026-05-15: VST3 + AU + CLAP + Standalone all
      present in `build/Stencil_artefacts/Release/`.)*
- [x] `make clean && make build && make test` from scratch
      succeeds (cold-build sanity). *(Verified 2026-05-15: clean
      → configure → build all 4 formats → 1455 assertions / 81
      cases pass.)*

Status flips to *Implemented* once every box above is ticked.
Cross-platform Windows / Linux builds and code-signing /
notarization land in a follow-up ADR per §Out of scope.

## Audit follow-ups (2026-05-10)

The 2026-05-10 cross-axis audit (m4l vs vst, code vs docs, both vs
inboil) surfaced items beyond §Verification's manual host-load
checks. Resolved items are in commit history (transport-start
register reset, FREEZE / ROLL framing, hung-note flush across
parameter changes / state load / bypass). The remaining items
below are tracked here so they can be addressed incrementally
without re-opening the §Implementation checklist.

Tag legend: `(code, vst)` / `(code, m4l)` / `(doc)`.

### Cross-target consistency

- [x] **mutated-bit salmon highlight in `vst` `RingView`** (code, vst)
      — inboil `TuringSheet.svelte` paints the bit just flipped by
      `shiftAndFlip` in `--color-salmon`; vst's `RingView::paint`
      ignores the salmon palette token. Add a per-step "mutated bit
      index" snapshot from the audio thread and read it in `paint`.
      *(Done in commits `64989b6` then `d87f97b`: PluginProcessor
      publishes `mutatedBitSnapshot_`; RingView paints the
      just-emitted bit at index 0 in salmon when shiftAndFlip flipped
      the consumed LSB, with the post-`d87f97b` pre-shift snapshot
      semantics.)*
- [ ] **`m4l` `setParam` flush misses `mode` + `outputChannel`** (code,
      m4l) — vst now flushes on these (this ADR's hung-note discipline
      fix); m4l-side `host.ts:setParam`'s `flushKeys` array still
      omits both. Symmetrize.
- [x] **`rangeLo` / `rangeHi` clamp direction differs target-to-target**
      (doc) — vst clamps the *other* side (APVTS UX expects
      not-just-moved side to follow); m4l clamps the side the user
      just moved. Both are intentional but undocumented. Note in
      §Parameter surface. *(Documented in §Parameter surface.)*
- [x] **`mapToNote` span +1 framing differs from inboil** (doc) —
      Stencil m4l/vst use `floor(lo + (num × (hi-lo+1)) / den)`
      clamped to hi for fair note-bucket distribution; inboil uses
      `round(lo + frac × (hi-lo))` with uneven endpoint buckets.
      Note the deliberate divergence in §Engine port. *(Documented
      in §Engine port.)*

### Doc consistency

- [x] **`concept.md` §Transport "reset on stop" wording** (doc) —
      actual behavior is reset on transport *start* (m4l's
      `transportStart()` and vst's start-edge handler). Reword the
      §Transport paragraph so the determinism contract describes
      what the implementation actually does. *(Reworded in
      `concept.md` §Transport.)*
- [x] **§MIDI processing "pure-density framing is incorrect"** (doc)
      — the line critiques inboil as pure-density, but inboil
      actually uses `regValue > (1-density) * 0.5` for `note`
      mode and density-only for `gate` / `velocity`. Reword to
      "Stencil's bit-tap diverges from inboil's mode-dispatched
      mix" and explicitly cite inboil's per-mode active rule.
      *(Reworded in §MIDI processing step 5c.)*
- [x] **"all 13 parameters" wording** (doc) — `concept.md` lists
      11 canonical parameters (range as tuple); §Persistence here
      says 13 because vst APVTS keeps `rangeLo` / `rangeHi`
      separately and adds `outputChannel`. Annotate
      "13 (vst APVTS count)" so the two doc shapes reconcile.
      *(Annotated in §Persistence.)*
- [x] **"audio buffer empty by construction"** (doc) — §MIDI
      processing line claims the buffer is empty courtesy of
      `IS_MIDI_EFFECT TRUE`; the implementation explicitly calls
      `audio.clear()` defensively because hosts can pass non-empty
      buffers. Reword. *(Reworded in §MIDI processing intro.)*

### Real-time safety

- [ ] **Audio-thread vector allocation churn** (code, vst, RT) —
      `pendingNoteOffs_`, Sequencer's `heldInputs_`, and the
      `std::vector<StepBoundary>` returned by `detectBoundaries`
      all grow on the audio thread without pre-reservation.
      Reserve in `prepareToPlay` (e.g. 64 / 16 / max-steps-per-block)
      and refactor `detectBoundaries` to take a
      `std::vector<StepBoundary>&` output parameter so the buffer
      can be reused across blocks.
- [ ] **Editor snapshot tuple coherence** (code, vst, RT) — four
      independent `std::atomic` snapshots (`registerSnapshot_`,
      `cumulativeStepsSnapshot_`, `lastNoteSnapshot_`,
      `lastActiveSnapshot_`) all use `memory_order_relaxed`, so
      the editor can read a torn tuple (e.g. register from step N
      with lastNote from step N-1). At 15 Hz repaint the visual
      drift is bounded but real. Pack into a single
      `std::atomic<Snapshot>` (≤16 bytes — lock-free on x86_64 /
      arm64) with `release` store / `acquire` load, or use a
      seqlock pattern via a single `std::atomic<uint32_t>` version
      counter.
- [ ] **Input MIDI is block-quantized, not sample-accurate** (code,
      vst, RT) — `processBlock` drains every MIDI input message
      into Sequencer before subdivision-boundary detection, so
      `gate` / `seed` `triggerMode` events land at block-start
      instead of their actual `samplePosition`. Interleave input
      events with boundaries by samplePosition so Sequencer's
      held-input / seed-active state mutates at the correct moment
      within the block. §MIDI processing already promises
      sample-accurate *output* timing — extend the same contract
      to input.

### Edge cases / behavior

- [ ] **`outputGate = 0.0` produces zero-length notes** (code,
      vst + m4l, design) — at the APVTS lower bound,
      `gateSamples = floor(0 × stepDur + 0.5) = 0` so noteOff
      lands at the same `sampleOffset` as noteOn. Most synths
      render this as a click or silence. Two options: enforce
      `gateSamples >= 1` at the scheduling site, or raise the
      APVTS lower bound (e.g. `0.01`). Decision needed —
      "outputGate=0 = mute step" might be a valid creative use
      case worth preserving.
- [ ] **`triggerMode = seed` user-played register wipe on transport
      start** (code, vst + m4l, design) — the Issue 2 start-edge
      reset re-derives register from `seed` APVTS param,
      discarding any pattern the user wrote via input notes.
      Matches m4l, but surprises live performers. Either skip the
      reset when `triggerMode == Seed && seedActivated_`, or add
      register persistence to APVTS so the input-driven pattern
      survives transport bounces.
- [x] **Same-millisecond double-click ROLL is a no-op** (code, vst)
      — `RingView::mouseDown`, `ActionsView::onRoll`, and the
      right-rail `rollBtn_` all seeded a fresh rng from
      `juce::Time::getMillisecondCounter()`; two clicks within 1 ms
      produced identical seeds. Switched all three call sites to
      `juce::Random::getSystemRandom().nextInt(0x7FFFFFFF)` whose
      state advances per call, so successive presses always
      produce different seeds.

### Cleanup

- [x] **`engine::NotesOn` is dead code** (code, vst) — declared in
      `Engine/Sequencer.h` but `PluginProcessor` uses
      `pendingNoteOffs_` instead. Decide: surface `NotesOn` as the
      canonical hung-note tracker (and route `pendingNoteOffs_`
      through it) or remove the class. *(Done in commit `74a2dc3`:
      class removed; `pendingNoteOffs_` remains the canonical
      tracker.)*
- [x] **`RightRailView::pollTimer_` is dead code** (code, vst) —
      declared as `juce::Timer* pollTimer_ = nullptr` but never
      assigned; the actual pill-sync timer lives in `pillSync_`
      (a `std::unique_ptr<PillSync>`). Field removed.

## Per-target notes

- **vst** — this ADR.
- **m4l** — unaffected. The cross-target conformance tests are the
  only m4l touch (m4l reads the same `rng-test-vectors.json` and
  `turing-test-vectors.json` already; no change needed for vst to
  consume them too).
- **Engine semantics (ADR 001 contract)** — the C++ port is bound
  by the same test vectors as the TS port. Drift is caught at
  build time by `make test` failing on vector mismatch.

## Supersedes

None. ADR 005 §VST/AU posture explicitly delegated this ADR; the
delegation is fulfilled, not overridden.
