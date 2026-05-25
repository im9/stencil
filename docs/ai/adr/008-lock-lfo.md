# ADR 008: Lock LFO — periodic modulation of the loop mutation rate

## Status: Proposed

**Created**: 2026-05-25

## Context

The `lock` parameter currently takes a static value the user sets by
hand. At `lock = 1.0` the register freezes into a perfect loop; at
`lock = 0.0` every bit flips and the register is pure noise.
Intermediate values give a fixed mutation rate that persists until
the user moves the slider.

A fixed lock value produces a uniform "evolve rate" across the
entire performance. Musical phrasing usually wants the loop to
**breathe** — settle for a bar or two when lock is high, then
loosen into mutation when lock drops, and repeat. The TB-303-style
acid lineage especially relies on this kind of slow drift in the
sequence's stability. Getting it currently means continuously
moving the lock slider by hand.

Lock LFO drives `lock` from a small internal LFO so the
mutation rate cycles on its own. The user picks shape / rate /
amount; the host computes each step's lock value from the LFO
output and feeds it to the engine. Sine produces a smooth breath;
square at high amount produces alternating "freeze / mutate"
sections that present a different fixed loop after each
unfreeze; saw and triangle sit between.

Identity alignment: this deepens an existing parameter without
introducing per-step authoring affordances. The probabilistic
character of the loop is preserved — only the parameter that
controls its evolution becomes time-varying.

## Decision

Add a host-side LFO that modulates `lock` per step. The engine
signature is unchanged: the host computes the per-step lock value
and passes it via the existing `params.lock` field.

```
lock_per_step = clamp(base_lock + lfo_signal(shape, phase) × amount, 0, 1)
phase         = (host_ppq mod period_in_ppq) / period_in_ppq
```

`lfo_signal` is in `[-1, +1]`. `amount` is in `[0, 1]`. When
`amount = 0` the LFO contributes nothing and behavior is
bit-for-bit identical to pre-008.

### Parameters (new host-level state)

| Parameter   | Type / Range                     | Default | Notes                                           |
|-------------|----------------------------------|---------|-------------------------------------------------|
| `lfoShape`  | enum (`sine` / `tri` / `saw` / `sqr`) | `sine`  | LFO wave shape                                  |
| `lfoRate`   | enum (`1/4`, `1/2`, `1`, `2`, `4`, `8` bars) | `1` bar | host-sync period                       |
| `lfoAmount` | float `0..1`                     | `0.0`   | modulation depth (`0` = LFO disabled)           |

### LFO signal

Pure function, deterministic, no PRNG:

- `sine(phase)` = `sin(2π × phase)`
- `tri(phase)` = `4 × |phase − 0.5| − 1` (peaks at phase 0.5)
- `saw(phase)` = `2 × phase − 1` (rising)
- `sqr(phase)` = `phase < 0.5 ? +1 : −1`

Phase derives from host PPQ, so transport scrub / stop / start
behavior is automatic (phase realigns to the new transport
position). Two targets given the same `(shape, base_lock, amount,
host_ppq)` produce bit-identical lock values.

### Engine impact

None. `tmStep(state, params)` signature unchanged; the engine
treats each call as an independent step using whatever `params.lock`
it receives. Existing `turing-test-vectors.json` continues to pass.

## Persistence

- **m4l**: three `live.*` params (`live.menu` × 2 for shape and
  rate, `live.dial` for amount). Saved by Live's preset system,
  host-automatable, MIDI-mappable.
- **vst**: three APVTS params (`AudioParameterChoice` × 2,
  `AudioParameterFloat` × 1) added to
  `vst/Source/Plugin/Parameters.{h,cpp}`.

## UI

A new **"Lock LFO"** fieldset added to
`vst/Source/Editor/RightRailView`. The existing rail already hosts
stacked fieldsets (Parameters / Output / Trigger / Reproducibility)
in a scrollable column at 280 px wide; this fieldset extends that
pattern — no popup, no new window mechanism. m4l mirrors the same
fieldset / row visual language.

Layout (3 rows, same row geometry as Parameters / Output):

```
┌─── Lock LFO ────────────────────────────┐
│ SHAPE   [ SINE                  ▾ ]      │
│ RATE    [ 1 bar                 ▾ ]      │
│ AMT     [ █████░░░░░░░ ]            0.30 │
└─────────────────────────────────────────┘
```

- `SHAPE`: `juce::ComboBox`, populated from `lfoShapeChoices`,
  styled with the existing `styleCombo()` helper.
- `RATE`: `juce::ComboBox`, populated from `lfoRateChoices`,
  styled with `styleCombo()`.
- `AMT`: `juce::Slider` (LinearHorizontal), styled with
  `styleSlider()`; value rendered by `RightRailView::paintContent`
  in the same right-aligned column used by `LOCK` / `DENS`.

The fieldset reuses the existing `drawFieldsetFrame()` helper and
the inboil palette tokens (`theme::bg`, `theme::fg`, `theme::olive`,
`theme::lzBorder`). Placement within the rail stack TBD in Phase 5
(open question below).

### Logic layer

Pure function `lfoSignal(shape, phase) → [-1, +1]` testable in
Node (m4l) and Catch2 (vst). Phase computation
`(host_ppq mod period) / period` is also a pure function, tested
independently. Cross-target conformance via a small shared
`docs/ai/lfo-test-vectors.json` (new): each shape at phases 0,
0.25, 0.5, 0.75, plus edge cases.

## Scope

**In scope:**
- Sine / triangle / saw / square shapes
- Host-sync period from `1/4` to `8` bars
- Bipolar modulation centered on the user's `base_lock`
- Engine signature and existing test vectors unchanged

**Out of scope:**
- **Free-running (Hz) LFO rate** — host-sync covers the primary
  musical use case; free-running adds UI surface (rate slider +
  unit toggle) without clear demand yet.
- **Multiple LFOs / a modulation matrix** — one LFO on `lock`
  is the focused intent. If `density` or other params want
  modulation later, each gets its own per-param LFO in its own
  ADR.
- **Custom or drawable LFO shapes** — the four built-in shapes
  cover common musical needs.
- **External MIDI to drive the LFO phase** — LFO is fully
  internal in v1.

## Implementation checklist

Phased per CLAUDE.md TDD gates. Each phase independently
executable.

- [ ] **Phase 1 — Shared LFO logic + vectors (m4l reference)**
  Add `lfoSignal(shape, phase)` pure function to `m4l/host/lfo.ts`
  (host-side, not engine — the LFO doesn't touch the register).
  Add `docs/ai/lfo-test-vectors.json` with shape × phase cases and
  edge values. `m4l/host/lfo.test.ts` iterates the vectors.
  `turing-test-vectors.json` and `turing.test.ts` unchanged.

- [ ] **Phase 2 — m4l host wiring + UI fieldset**
  Add `lfoShape`, `lfoRate`, `lfoAmount` live.* params to the
  patcher. In the host loop, compute `lock_per_step` from
  `base_lock` + LFO + transport position, write into `params.lock`
  before invoking `tmStep`. Arrange the three controls as a
  fieldset-equivalent group adjacent to the existing Parameters
  group, matching vst's RightRail visual language. `pnpm bake`
  produces the updated `.amxd`.

- [ ] **Phase 3 — vst LFO port + tests**
  Port `lfoSignal` to C++ as `vst/Source/Engine/Lfo.{h,cpp}`
  (pure C++17, no juce_*). Catch2 tests under `vst/tests/` iterate
  the same `lfo-test-vectors.json` and assert bit-identical
  output vs m4l. `make test` passes.

- [ ] **Phase 4 — vst APVTS + processor**
  Add `lfoShape`, `lfoRate`, `lfoAmount` to
  `vst/Source/Plugin/Parameters.{h,cpp}`. In
  `PluginProcessor::processBlock`, compute the per-step lock from
  `base_lock` + LFO + transport PPQ before each `tmStep` call.

- [ ] **Phase 5 — vst UI fieldset**
  Extend `RightRailView` with a "Lock LFO" fieldset containing two
  combos + one slider, using existing `styleCombo()` /
  `styleSlider()` helpers and `drawFieldsetFrame()`. Wire APVTS
  attachments. Decide final stack placement (open question below).

## Verification

Manual checks against real hosts. Tick when actually run.

- [ ] With `lfoAmount = 0`, output is bit-for-bit identical to
      pre-008 across all combinations of `(seed, base_lock,
      density, range)` (regression guard — LFO off = legacy).
- [ ] m4l + vst: `lfoShape = sine`, `lfoRate = 1 bar`,
      `lfoAmount = 0.3`, `base_lock = 0.7` — the loop audibly
      breathes between tighter and looser variants on a 1-bar
      cycle. No glitches at phase wrap.
- [ ] m4l + vst: `lfoShape = sqr`, `lfoRate = 2 bars`,
      `lfoAmount = 1.0`, `base_lock = 0.5` — 2 bars of fixed
      loop alternating with 2 bars of rapid mutation. Each
      "freeze" section presents a different fixed loop than the
      previous one (LFO doesn't reset the register).
- [ ] Transport scrub / stop / start at non-zero PPQ: LFO phase
      resumes coherently with host position; no audible
      discontinuity.
- [ ] Project save / close / reopen preserves LFO params exactly
      (m4l: Live set; vst: APVTS via host).
- [ ] Host automation on each of `lfoShape` / `lfoRate` /
      `lfoAmount` works (m4l Cmd-M, vst DAW automation lanes in
      Logic + Bitwig).

## Per-target notes

`lfoSignal` and the phase computation are shared deterministic
math — both targets ship the same function and assert against the
shared vector file. The TS reference lands first (Phase 1–2); the
C++ port (Phase 3) re-uses the vectors. m4l + vst byte-identical
PRNG isn't relevant here (LFO has no PRNG), but the same
discipline applies: vectors are the source of truth for
cross-target conformance.

The engine (`m4l/engine/turing.ts`,
`vst/Source/Engine/Turing.{h,cpp}`) is **not modified** by this
ADR. The LFO lives entirely host-side; the engine sees only the
final per-step lock value.

## Open Questions

- **Rail-stack placement.** Insert the "Lock LFO" fieldset between
  Parameters and Output (semantic adjacency to `lock` itself), or
  append after Reproducibility (chronological "added later")? Lean
  semantic adjacency. Settle in Phase 5.
- **Default editor height vs Reproducibility visibility.** Adding
  a fieldset adds ~3 rows to the rail. At the default editor size
  (820 × 600), Reproducibility might push below the fold. Either
  bump the default height a notch, or accept the scrollbar — pick
  in Phase 5 after measuring.
- **Amount slider taper.** Linear `0..1` is the obvious default,
  but subtle LFO amounts are often the most musically interesting
  — consider an exponential / sqrt taper to give finer resolution
  at low amounts. Keep linear for v1; revisit if it feels coarse
  in use.
- **Visual feedback for LFO running.** Should the AMT slider or
  the fieldset legend animate / pulse with the live LFO phase?
  Nice-to-have for discoverability; not required for the feature
  to work. Decide in Phase 5 based on visual polish budget.
