# Stencil

Probabilistic MIDI sequence generator: a Music Thing-style **Turing
Machine** shift register that emits notes from a controlled-randomness
loop. MIDI effect, DAW-native. Named after Herbert Stencil from Thomas
Pynchon's *V.*

Stencil is the TM-side product of the Stencil + Pointsman pair (see
[ADR 005](docs/ai/adr/archive/005-product-split.md)). The QT-side companion
ships from `~/src/vst/pointsman/`. The two are independent products;
the musical relationship is the canonical `Stencil → Pointsman` MIDI
chain.

## Targets

Stencil is developed in parallel across multiple targets that share the
same musical concept (Turing Machine shift register) but differ in UI
and platform. Each target lives in its own directory and has its own
build system. **Both targets are first-class production releases**, not
prototypes for one another.

- `m4l/` — **Max for Live** device (current primary target). Ableton
  Live MIDI effect, Ableton-optimized. Fastest iteration path and
  matches the author's primary DAW workflow; ships as a standalone
  product.
- `vst/` — **VST3 + AU + CLAP MIDI Effect** plugin (C++17/JUCE).
  DAW-native, cross-platform. Ships as a standalone product alongside
  m4l, on a slower cadence. Per ADR 005, single-purpose MIDI Effect
  formats (VST3 + AU + CLAP); paired in the DAW with Pointsman's vst
  plugin for scale-locked chains.

Core logic, parameter design, and ADRs are shared across this repo's
targets via `docs/ai/`. Code is not shared between targets — each
target is a ground-up implementation in its native stack. RNG
primitives are also synchronized cross-repo with Pointsman via
`docs/ai/rng-test-vectors.json` per ADR 005 §RNG sharing.

## Origin

Generative engine extracted from
[inboil](https://github.com/im9/inboil) (browser-based groove box).
inboil's Turing Machine generator lives in the scene graph as a
generative node; Stencil ships it as a single standalone DAW-native
MIDI effect. The Quantizer counterpart ships separately as Pointsman
per ADR 005.

Key references in inboil:
- `src/lib/sceneActions.ts` — `executeGenChain()`, Turing Machine logic
- `src/lib/types.ts` — `SceneNode.generative` field, TM parameters
- `docs/ai/adr/` — ADR 078 (generative nodes), related ADRs

The inboil implementation is JavaScript/Svelte. The `m4l/` target
reuses the TM logic style (TypeScript), while `vst/` is a C++17/JUCE
reimplementation — no code ported directly, but musical logic and
parameter design carry over.

## Layout

```
m4l/                 — Max for Live device (Stencil only)
  engine/            — Turing Machine + RNG (TypeScript)
    rng.ts             shared RNG primitives (xoshiro128++ + SplitMix64)
    rng.test.ts        vectors against docs/ai/rng-test-vectors.json
    turing.ts          shift register + lock + density
    turing.test.ts     vectors against docs/ai/turing-test-vectors.json
    dist/              compiled output (loaded by host)
    package.json, tsconfig.json
  host/              — n4m host layer (TypeScript)
    bridge.ts, host.ts, *.test.ts
    ui/                jsui logic + tests
    dist/              compiled output
    package.json, tsconfig.json
  stencil.mjs        — n4m entry, loaded by [node.script] (flat path)
  registerRing.jsui.js — bit ring renderer (flat path; loaded by [jsui])
  registerRing.subpatcher.maxpat — companion subpatcher (flat path; bpatcher in Stencil.maxpat)
  Stencil.maxpat     — Max patcher source (tracked in git)
  Stencil.amxd       — Max for Live device (built artifact, tracked)
  scripts/
    maxpat-to-amxd.mjs   bake script (single product, no argv)
  package.json, pnpm-workspace.yaml
vst/                 — VST3 + AU + CLAP MIDI Effect plugin (C++17/JUCE)
  Source/
    Engine/          — pure C++17, no juce_* (iOS-reuse / test-link boundary)
      Rng.{h,cpp}        xoshiro128++ + SplitMix64
      Turing.{h,cpp}     createRegister, shiftAndFlip, shiftAndForce,
                         registerToFraction, mapToNote, tmStep
      Sequencer.{h,cpp}  PPQ → subdivision boundaries, hung-note tracker, panic
    Plugin/          — JUCE AudioProcessor + APVTS
      PluginProcessor.{h,cpp}
      Parameters.{h,cpp} APVTS layout, IDs, ranges, defaults
    Editor/          — JUCE Editor, inboil TuringSheet port
      PluginEditor.{h,cpp}     top-level layout (header / body / history)
      Theme.{h,cpp}            inboil palette + typography tokens
      RingLogic.{h,cpp}        pure hit-test math (testable)
      RingView.{h,cpp}         left-side bit ring (revolver rotation)
      ActionsView.{h,cpp}      FREEZE / ROLL buttons
      RightRailView.{h,cpp}    fieldset stack with APVTS attachments
      HistoryView.{h,cpp}      bottom output-history bars
  JUCE/              — JUCE framework (git submodule, 8.0.12+)
  clap-juce-extensions/  — CLAP wrapper for JUCE plugins (git submodule, 0.26.x)
  tests/             — Catch2 v3 unit tests (custom main owns JUCE init)
  scripts/           — build helpers (check-artefacts.sh)
  CMakeLists.txt, Makefile
docs/ai/             — design docs, ADRs, test vectors
  concept.md
  rng-test-vectors.json       cross-repo synced with Pointsman
  turing-test-vectors.json    Stencil-specific
  adr/
```

## Setup

```bash
git clone --recursive <repo-url>   # fetches JUCE + clap-juce-extensions submodules under vst/
```

## Build

### m4l/

`m4l/` is a pnpm workspace. Packages: `@stencil/engine`, `@stencil/host`.

```bash
cd m4l
pnpm install         # first time, installs all workspace packages
pnpm -r test         # run tests across all packages
pnpm -r build        # compile dist/ for all packages
pnpm -r typecheck    # type-check without emit
pnpm bake            # rebuild Stencil.amxd from Stencil.maxpat
pnpm bake:check      # guard tests on .maxpat (abs-path scrub, sibling-file resolve)
```

Per-package (e.g. just engine):

```bash
cd m4l/engine
pnpm test            # run tests against TS source
pnpm build           # compile dist/
```

Open `Stencil.amxd` in Max for Live to use the device. The device
loads `dist/` artifacts via `[node.script stencil.mjs]`, so run
`pnpm -r build` after engine or host changes (and `pnpm bake` after
`.maxpat` edits).

The `[node.script]` filename `stencil.mjs` resolves to the bundled
output of `pnpm bundle:host` (esbuild), not the tracked source
`stencil.entry.mjs`. The bundle inlines the host + engine import
chain because Max's Freeze step does NOT follow ES `import`
statements (verified by oedipa-007 Phase 5); without bundling, the
shipped frozen `.amxd` would lose its dependencies. `pnpm bake`
runs `bundle:host` first, so the dev iteration loop is unchanged.

**Do NOT add `max-api` to dependencies.** It's injected by Max at
runtime; the npm version conflicts with the injected one. The
bundle config marks it `--external:max-api` so esbuild leaves the
import statement intact for Max to resolve. (Same convention as
oedipa.)

**Distribution (release builds).** `make release` (from repo root)
runs build + bake and prepares `dist/`. The baked dev `.amxd`
references the bundled `stencil.mjs` as a sibling on disk, so it
only loads on the build machine. To ship: open `m4l/Stencil.amxd`
in Max → click the **snowflake (Freeze)** button in the patcher
toolbar (inlines every referenced JS into the `.amxd` binary) →
*File → Save As* `dist/Stencil.amxd`. The frozen file is
self-contained and works on any Live install. See ADR 006
§Release artifact production.

### vst/

```bash
cd vst
make build              # configure + build (Release) — produces VST3 + AU + CLAP + Standalone
make debug              # configure + build (Debug)
make clean              # remove build directory
make test               # build + run tests
make verify-artefacts   # validate VST3 + AU + CLAP + Standalone bundles are present
make open               # launch the Standalone build (dev only)
```

## Design

- MIDI effect: emits probabilistic note sequences from a shift register
- Turing Machine: shift register with probability-controlled bit flip
  (cf. Music Thing Turing Machine)
- Output: every active step emits one noteOn carrying `(pitch, velocity,
  gate)` simultaneously — pitch from the register via `mapToNote`,
  velocity from `outputVelocity`, gate length from `outputGate × stepDur`
  (see ADR 007 §Output)
- Scale-locked output is achieved by chaining downstream Pointsman
  (`Stencil → Pointsman → Synth` is the canonical use)
- Parameters normalized in plugin/host layer
- Label: im9. M4L build ships free under the brand-promo posture
  (`Stencil.amxd` attached to `m4l-v*` GitHub Releases on this repo,
  oedipa pattern). VST3 / AU / CLAP builds are planned as paid
  releases (price / channel TBD). See ADR 005 §Distribution posture.

## Mandatory Workflow

**Every implementation task follows these gates in order. Do not skip
gates. Do not reorder.**

### Gate 0 — Read before doing

Before writing any code:
1. Read `docs/ai/concept.md`
2. Read relevant ADR in `docs/ai/adr/`

### Gate 1 — Tests first (TDD)

**Write or update tests BEFORE editing any implementation file.** This
applies per target:

- `m4l/` — update `m4l/<package>/*.test.ts` before editing
  `m4l/<package>/*.ts`
- `vst/` — update `vst/tests/*` before editing `vst/Source/*`

Applicable cases:

- New feature → write tests that describe the expected behavior
- Bug fix → write a test that reproduces the bug
- Refactor → verify existing tests cover the behavior, add if not
- Constant/enum changes that propagate across files → write a
  consistency test that asserts the new count and accesses all indices

#### Shared test vectors

Cross-target engine semantics (Turing Machine bit flip behavior, RNG
primitives) are captured in `docs/ai/turing-test-vectors.json` and
`docs/ai/rng-test-vectors.json`. Each target's test suite reads the
appropriate JSON and iterates the cases. The RNG vectors are also
synchronized cross-repo with Pointsman per ADR 005 §RNG sharing —
when changing `rng.ts` or its vectors, both repos must stay
byte-identical (verified by ADR 005 Phase 4).

When adding a new semantic case, add it to the JSON — do not duplicate
the data in per-target test code.

#### GUI / UI components

UI work cannot be unit-tested the way pure logic can — visual quality,
interaction feel, and host loading behavior require human eyes and a
real DAW. Split UI components into a **logic layer** (parameter
mapping, state machines, hit testing, drag-to-value math) and a
**renderer** (the actual drawing). Tests target the logic layer; the
renderer reads model state and is not unit-tested.

- **vst/ (JUCE)**: Instantiate the component in the test, simulate
  input via the public JUCE API (`mouseDown` / `mouseDrag` / `mouseUp`
  / `keyPressed`), and assert against parameter values and internal
  state. Expose minimal `getXxxForTest()` inspection methods only when
  state is otherwise private.
- **m4l/ (jsui)**: Keep logic functions as pure exported TypeScript
  runnable in Node. Compile to `dist/` for jsui consumption. The
  jsui-specific drawing and event callbacks live in a thin wrapper
  that calls into the pure logic.
- Do not snapshot-test pixel output. Font rendering and environment
  differences make image hashing brittle.

What stays manual (not covered by Gate 1 tests):

- Visual quality — does the shift register UI look right
- Interaction feel — tap / drag in the real host
- Host compatibility — load in Ableton (m4l, vst), load in Logic
  (vst), edit, save, reopen, verify no crash

These manual checks are part of pre-release verification, not
optional polish.

### Gate 2 — Implement

Now edit implementation files. Keep changes minimal and focused.

### Gate 3 — Build and test

Run the target's test command:

- `m4l/` — `cd m4l && pnpm -r test` (runs `node --test` across workspace)
- `vst/` — `cd vst && make test`

For m4l, also run `pnpm -r build` to refresh `dist/` artifacts and
`pnpm bake` to refresh `Stencil.amxd` before loading the device in
Live. `pnpm -r typecheck` checks types without emitting.

All tests must pass. Do not proceed with failing tests.

### Gate 4 — Commit (only with explicit approval)

**Never commit or push without explicit user approval.** Even after
`/commit`, confirm before creating a commit.

## Conventions

- All in English
- Commit messages: imperative mood, concise
