# ADR 005: Product Split — Stencil (TM) and Pointsman (QT)

## Status: Implemented

**Created**: 2026-05-07
**Implemented**: 2026-05-08 (Stencil-side trim landed: TM-only m4l workspace, RNG primitives extracted to `rng.ts` + `rng-test-vectors.json`, QT assets removed, single-product bake script. Pointsman bootstrap pushed to `im9/pointsman` with QT-scoped `CLAUDE.md` and `001-pointsman-base.md`; further migration is owned by Pointsman. RNG cross-repo sync verified byte-identical at the data level. Functional verification of post-split Stencil delegated to ADR 006.)

This ADR splits Stencil into two independently-shipped products with
separate repositories, sets per-product naming, and resolves the VST
target architecture left open by [ADR 002 line 77-80][adr2-vst]. It also
disposes of [ADR 003][adr3] (m4l UI design) and [ADR 004][adr4] (m4l
bake / distribution), whose pre-split scopes are bisected by the
decision below.

[adr2-vst]: 002-m4l-architecture.md
[adr3]: 003-m4l-ui-design.md
[adr4]: 004-m4l-bake-distribution.md

## Context

### Musical motivation

Each generative unit is musically complete on its own:

- **TM alone** is a valid drum-machine driver. Feeding the bit-derived
  pitch into a sampler treats the register as a probabilistic
  trigger pattern; quantization is irrelevant when the destination has
  one fixed pitch per pad.
- **QT alone** is a valid keyboard / arpeggiator companion. Played
  input, MPE controllers, Tonnetz walks, and chord clips all benefit
  from scale-locked snap independent of any TM upstream.
- The **TM → QT chain** remains the canonical combined idiom (see
  [concept.md §Composition][concept-comp]) but is one of three
  use cases, not the only one.

[concept-comp]: ../../concept.md#composition--tm--qt-chain

Treating TM and QT as one combined product framed around the chain
hides the standalone musicality and forces a "buy both, learn both"
framing. The combined-product framing also relies on initialism
(`Stencil-TM`, `Stencil-QT`, "TM mode," "QT mode"). For DAW-native
distribution, where users find tools by searching `MIDI scale
quantizer` or `random MIDI generator`, initialism communicates only to
insiders and costs discoverability.

### Architectural motivation

m4l already ships TM and QT as two separate `.amxd` devices per
[ADR 002 §Topology][adr2-topology] — that decision is settled. What
ADR 002 left open in line 77-80 was VST architecture:

> The VST target will revisit this — combining into one plugin is a
> reasonable choice when chaining friction is higher (separate plugin
> windows, separate parameter automation lanes). VST's architecture
> lives in its own ADR.

[adr2-topology]: 002-m4l-architecture.md#topology--two-devices-not-one

After product-side reconsideration (2026-05-07), combining into one
VST plugin is rejected. The cost of combining (loss of standalone
discoverability, internal inconsistency between m4l and vst, sustained
initialism in branding) outweighs the friction it would avoid
(per-DAW MIDI plugin chaining UX variance). Major hosts — Logic
(AU MIDI FX slots), Cubase (MIDI Inserts), Reaper, Bitwig, Studio One
— support multiple MIDI plugins in chain natively. The friction is
real but not blocking.

The decision below extends the per-device topology to vst and to the
repository structure, with per-product naming.

### Shared-logic data

Engine code investigation (2026-05-07) confirmed that
[m4l/engine/turing.ts][turing] and [m4l/engine/quantizer.ts][quantizer]
have **no cross-dependence**. The only shared symbols are the RNG
primitives (`seedRng`, `nextU32`, `RngState`) currently colocated in
`turing.ts` — `host-qt/humanize.ts` imports them for humanize draws.
The RNG (xoshiro128++ + SplitMix64 seeding) is contractually frozen
by [ADR 001 §Implementation][adr1-impl] and verified via
[turing-test-vectors.json][tv-tm].

[turing]: ../../../../m4l/engine/turing.ts
[quantizer]: ../../../../m4l/engine/quantizer.ts
[adr1-impl]: 001-engine-interface.md
[tv-tm]: ../../turing-test-vectors.json

The total shared surface is ~100 lines of well-specified code. Repo
split is therefore low-cost: duplicate the RNG with test-vector sync,
no shared package overhead.

## Decision

### Product naming

| Pre-split (m4l)   | Post-split product | Pynchon source                     |
|-------------------|--------------------|------------------------------------|
| `Stencil-TM.amxd` | `Stencil`          | *V.* — Herbert Stencil             |
| `Stencil-QT.amxd` | `Pointsman`        | *Gravity's Rainbow* — Edward Pointsman |

`Stencil` retains the V.-derived name because Herbert Stencil's
narrative role (assembling pattern from random clues, "always
becoming") matches the TM shift register's behavior at the
metaphorical level.

`Pointsman` is the QT product. The English railway sense — a signalman
who throws the switch routing an incoming train to a discrete track —
is exact for what a quantizer does (route input pitch to a discrete
scale degree). The Pynchon character (Pavlovian behaviorist, the
deterministic foil to Roger Mexico's probabilistic worldview) gives
the deeper resonance and matches the project's existing Pynchon-name
family (Stencil from V., Oedipa from Lot 49, Slothrop already in use).

Naming candidates pooled and held in reserve for future products:
Maskelyne, Mondaugen, Owlglass, Vheissu, Tristero (reserved). They are
not committed to any product slot in this ADR.

### Distribution posture

Both products inherit the existing im9 pattern established by Stencil
and oedipa:

- **License** — per-target split, matching oedipa's structure:
  `m4l/LICENSE` and `docs/LICENSE` ship under MIT (free reuse);
  `vst/LICENSE` is proprietary source-available (read / self-build
  for personal non-commercial use / contributions permitted;
  redistribution and commercial use require explicit permission).
  The Phase 2 `git clone` preserves these files verbatim into
  Pointsman.
- **Repo visibility** — public. Both `im9/stencil` and `im9/pointsman`
  source repos are publishable on GitHub from day one; the proprietary
  vst license + the build-pipeline complexity (JUCE + CMake + signing
  + notarization) are the moat for the paid native builds rather than
  source secrecy.
- **Pricing** — split by target as of 2026-05-09 amendment:
  - **M4L builds** ship **free** as brand-promo, in the same posture
    as oedipa's m4l. Distributed via GitHub Releases on each product's
    main source repo (`im9/stencil` / `im9/pointsman`) under the
    per-target tag namespace `m4l-vX.Y.Z`, with `Stencil.amxd` /
    `Pointsman.amxd` attached as release assets.
  - **VST3 / AU / CLAP builds** are planned as **paid** releases
    (price / channel TBD). Native-plugin investment (cross-platform
    DAW compatibility, code signing, notarization) is the rationale
    for the split. Same product, same musical experience as the
    free m4l (cross-target conformance is preserved per the test
    vectors in `docs/ai/`); the paid form factor pays for the
    native build effort.

  Distribution-channel bundling, listing copy, and per-channel
  mechanics remain as in ADR 004's lineage (carried forward into
  per-product distribution ADRs).

  > **Amended 2026-05-09 — pricing posture split (m4l free /
  > native paid).** The original wording said "free distribution.
  > Both Stencil and Pointsman serve as brand-presence products
  > under the im9 label rather than revenue drivers, in the same
  > posture as oedipa." That framing held while only the m4l
  > target was being shipped. With native-plugin distribution
  > coming into scope, the cost structure differs (cross-platform
  > compatibility, code signing, notarization), so the m4l brand-
  > promo posture is preserved while VST3 / AU / CLAP move to
  > paid distribution. Per
  > `feedback_porting_bug_amend_not_supersede`: amended in place,
  > not superseded. oedipa's posture is owned by the oedipa repo
  > and is not cascaded from here.

  > **Amended 2026-05-17 — source-available vst.** The original
  > "Repo visibility: private during development, public at
  > distribution time" framing assumed the vst Source/ would stay
  > closed even after release. Following oedipa's lead, vst Source/
  > is now public under a proprietary source-available license
  > (read / self-build / personal non-commercial use / PRs
  > permitted; redistribution and commercial use forbidden without
  > explicit permission). Paid binaries remain the revenue path;
  > the build-pipeline complexity is the practical moat. Same
  > pricing posture (m4l free / native paid) — only repo visibility
  > and license shape change.

  > **Amended 2026-05-17 — drop per-product slim binary repos in
  > favor of oedipa-style releases on the source repo.** The
  > original wording said "Distributed via per-product slim public
  > repos (`im9/stencil-m4l`, `im9/pointsman-m4l`) with
  > `Stencil.amxd` / `Pointsman.amxd` hosted on GitHub Releases."
  > That pattern was an early experiment; oedipa has been shipping
  > from `im9/oedipa` directly under `m4l-vX.Y.Z` tags since
  > 2026-05-10 without the slim binary repo. The split added a
  > second URL for users to find and a second repo to maintain,
  > with no offsetting benefit (the source repo is already public
  > and the asset is a single `.amxd`). Cutover: the early canary
  > `Stencil v1.0.0` release on `im9/stencil-m4l` is treated as
  > withdrawn (undisclosed); the repo is set to public-archive
  > while `im9/stencil` remains private (so the v1.0.0 download
  > URL stays reachable), and deleted once `im9/stencil` goes
  > public. First cutover release on `im9/stencil` is
  > `m4l-v0.1.0`. Per `feedback_porting_bug_amend_not_supersede`:
  > amended in place, not superseded. The pointsman side requires
  > the same cutover in the Pointsman repo and is owned there.

### Repository split

Per-product repositories, each independent. Filesystem layout
matches the existing `~/src/vst/` location used by Stencil and oedipa:

```
~/src/vst/
├── stencil/        ← this repo, becomes Stencil (TM) only
│   ├── m4l/        Stencil.maxpat, Stencil.amxd, host/, engine/
│   ├── vst/        Stencil VST3/AU MIDI Effect, TM only
│   └── docs/ai/    ADRs 001..004 retained in archive/, 005 lives here, 006 follows
├── pointsman/      ← new repo, Pointsman (QT)
│   ├── m4l/        Pointsman.maxpat, Pointsman.amxd, host/, engine/
│   ├── vst/        Pointsman VST3/AU MIDI Effect, QT only
│   └── docs/ai/    forked ADR set; pointsman-001 inherits relevant pre-split content
└── oedipa/         ← unchanged
```

Each repo has its own pnpm workspace, JUCE submodule, bake script,
test vector JSON files, and release cadence. There is no shared
parent or git submodule between them.

### RNG sharing

Both repos vendor identical copies of the RNG primitives. The shared
verification artifact is a new `docs/ai/rng-test-vectors.json`
extracted from the seed/step prefix of the existing
[turing-test-vectors.json][tv-tm]:

- Stencil keeps both `rng-test-vectors.json` and
  `turing-test-vectors.json` (TM uses both).
- Pointsman keeps `rng-test-vectors.json` and
  `quantizer-test-vectors.json` (QT uses RNG only via humanize).

Each repo's RNG implementation passes its `rng-test-vectors.json`
byte-for-byte. Drift between repos is caught by running the same
vectors against both implementations.

This is preferred over a third shared `stencil-rng` package because:

- The RNG is small (~100 lines) and frozen by ADR 001
- A third repo introduces submodule / registry overhead disproportionate
  to the surface
- Test vectors are already the cross-target sync mechanism per ADR 001;
  applying them cross-repo extends that mechanism, not invents one

### File organization within each post-split repo

```
m4l/
  engine/
    rng.ts          shared RNG primitives (extracted from turing.ts)
    rng.test.ts     vectors check against rng-test-vectors.json
    turing.ts       (Stencil only) imports rng; TM logic
    turing.test.ts  (Stencil only) vectors against turing-test-vectors.json
    quantizer.ts    (Pointsman only) imports rng for humanize
    quantizer.test.ts (Pointsman only) vectors against quantizer-test-vectors.json
  host/
    bridge.ts, host.ts, *.test.ts
    ui/             jsui logic + tests
  Stencil.maxpat / Pointsman.maxpat
  Stencil.amxd   / Pointsman.amxd
  registerRing.jsui.js     (Stencil only, flat path)
  scaleKeyboard.jsui.js    (Pointsman only, flat path)
  scripts/
    maxpat-to-amxd.mjs     no argv (single product)
vst/
  Source/
  JUCE/
  CMakeLists.txt, Makefile
  tests/
docs/ai/
  concept.md            scoped to that product's musical model
  rng-test-vectors.json shared sync artifact
  {turing,quantizer}-test-vectors.json  product-specific
  adr/                  per-repo ADR series
```

Engine package name in each repo is `@<repo>/engine` (e.g.
`@stencil/engine`, `@pointsman/engine`). Each is its own package
within its workspace; they do not interoperate at the package level.

### vst/AU architecture

Each product implements its own MIDI Effect plugin in C++17 / JUCE:

- **Stencil vst** — TM only. Generates probabilistic MIDI from the
  shift register. Plugin formats: VST3 + AU MIDI FX (Logic).
- **Pointsman vst** — QT only. Snaps incoming MIDI to scale, with
  chord and harmony modes. Plugin formats: VST3 + AU MIDI FX.

Each plugin is independent. Users chain `Stencil → Pointsman` in their
DAW's MIDI routing for the canonical Music Thing TM + Quantizer sound;
each plugin is also valid alone.

The combined-into-one-plugin alternative is rejected for the reasons
in §Architectural motivation: brand consistency with m4l, single-purpose
UI focus, and per-tool standalone discoverability outweigh the
per-DAW MIDI-plugin-chaining friction.

vst-internal architecture (APVTS shape, GUI design, JUCE module
choices, parameter automation) is delegated to per-product
vst-architecture ADRs in each repo, written when vst implementation
starts. This ADR commits the scaffold (one product = one plugin,
MIDI Effect format, VST3 + AU) and stops there.

### Relationship to prior ADRs

| ADR | Pre-split status | Post-split disposition                                             |
|-----|------------------|--------------------------------------------------------------------|
| 001 | Implemented (archived) | Preserved. Engine interface contract holds; cross-repo sync via shared `rng-test-vectors.json` extends ADR 001's mechanism. |
| 002 | Implemented (archived) | Preserved with partial supersession (see §Supersedes). m4l-internal layering decisions carry into each post-split repo unchanged. |
| 003 | Proposed              | Mark **Implemented** (m4l UI design landed for both devices). TM-side §Verification items (manual Live checks) carry forward to ADR 006; QT-side items go to the Pointsman repo's own ADR set. Archive. |
| 004 | Proposed              | Mark **Implemented** for the bake/check pipeline (already shipped). The argv parameterization is obsolete post-split (each repo has one product) and is replaced per-repo (each repo's own single-product bake). §Distribution items move to per-product distribution ADRs. Archive. |

## Scope

### In scope

- Product naming (Stencil = TM, Pointsman = QT)
- Repository split into `stencil/` and `pointsman/`
- RNG primitive duplication strategy with `rng-test-vectors.json` sync
- vst/AU plugin format (MIDI Effect, VST3 + AU, one plugin per product)
- m4l asset migration / rename mechanics
- Disposition of ADRs 001–004 post-split

### Out of scope

- **vst plugin internal architecture** (APVTS, GUI, JUCE modules) —
  deferred to per-product vst-architecture ADRs in each repo. Reason:
  vst implementation has not begun; designing the parameter tree and
  GUI without target contact (load in Logic / Live / Reaper) is
  speculation. Each repo's vst ADR is written when vst work starts.
- **Brand visual identity** for Stencil and Pointsman as separate
  products (logo, color palette, typeface, listing copy). Reason:
  distribution-time concern, not architectural. Resolved per the
  channel at upload time.
- **Pointsman's full ADR series** — the new repo will fork what it
  needs from Stencil's archive at creation time, but this ADR (in
  Stencil) does not enumerate Pointsman's ADRs. Reason: Pointsman's
  ADR layout is internal to that repo.
- **Cross-product distribution bundling** ("Stencil + Pointsman bundle"
  listing). Reason: distribution-time decision, depends on channel
  policies. Each product is independently shippable.
- **Future Pynchon-named products** beyond Stencil and Pointsman.
  Reason: no defined product slot exists yet. Name candidates pooled,
  not committed.

## Implementation checklist

The migration is structured as **clone first, then each repo cleans
up independently**. Phase 1 aligns docs in this repo. Phase 2
bootstraps `pointsman/` from a clone of `stencil/` and seeds it
with a QT-scoped `CLAUDE.md` plus a Pointsman-side migration ADR;
the migration itself is owned by Pointsman and tracked entirely
inside the Pointsman repo. Phase 3 then does the symmetric cleanup
inside `stencil/` (rename TM, extract RNG, delete QT) — its
checklist is stencil-self-contained. Phase 4 verifies the two
repos' RNG implementations stayed byte-identical across the
independent extractions.

Cloning is preferred over per-file copy because it preserves git
history, the JUCE submodule reference, and all working state in one
operation — and because the in-repo cleanup that follows is the same
shape as Stencil's own cleanup in Phase 3, giving the two repos
symmetric migration paths.

Phase 2 reads from `stencil/` only via `git clone`; `stencil/` is not
modified by the migration itself (only by ADR 005's own checkbox
flips). Each phase's bullets are doable when the phase's other
bullets are done — no cross-phase prerequisites embedded inside
earlier checklists.

### Phase 1 — docs alignment (this repo)

- [x] [docs/ai/concept.md](../../concept.md) — replace `Stencil TM` /
      `Stencil QT` references with `Stencil` / `Pointsman`. Update
      §Topology header and prose. Pointsman's `concept.md` is the
      Pointsman repo's own concern post-split.
- [x] [CLAUDE.md](../../../../CLAUDE.md) — update §Targets, §Layout,
      §Build to reflect single-product (TM-only) state of this repo.
      Pointsman's `CLAUDE.md` is authored separately in Phase 2.
- [x] [docs/ai/adr/INDEX.md](INDEX.md) — flip ADR 003 / ADR 004 to
      *Implemented* and move both files to `archive/`. Add ADR 006
      row (the ADR 005 row is already present).

### Phase 2 — Pointsman repo bootstrap (this session)

Bootstrap the new repo to the point where it can be handed off to
its own session. This phase covers: cloning, GitHub repo
provisioning, the QT-scoped `CLAUDE.md`, and the Pointsman-side
migration ADR (`001-pointsman-base.md`). The structural
transformation itself is owned by Pointsman and is not tracked
here.

The clone leaves `vst/` untouched; the existing Stencil-TM
scaffold present in the cloned tree is Pointsman's concern from
that point on.

- [x] `git clone ~/src/vst/stencil ~/src/vst/pointsman`. Run
      `git submodule update --init --recursive` in `pointsman/` to
      materialize the JUCE submodule. Provision the GitHub private
      repo (`im9/pointsman`) and replace `pointsman/.git/config`
      `[remote "origin"]` (which the local clone left pointing at
      `~/src/vst/stencil`) with the GitHub URL; `git push -u origin
      main` so the new repo has a real upstream from day one. Per
      §Distribution posture the repo stays private during
      development.
- [x] Replace `pointsman/CLAUDE.md` with QT-scoped content
      orienting future Pointsman sessions to the post-split shape.
      Include a pointer to `001-pointsman-base.md` as the
      migration plan.
- [x] Author `pointsman/docs/ai/adr/001-pointsman-base.md` as
      the Pointsman-side migration ADR. Once authored, its
      contents are owned by Pointsman; this ADR does not track
      or duplicate them.

Once these three bullets land (committed to `pointsman/` and
pushed to GitHub), this ADR's responsibility for Pointsman is
complete. Anything further on the Pointsman side happens in the
Pointsman repo and is not tracked here.

### Phase 3 — Stencil repo migration (this repo)

Once Phase 2 is complete (Pointsman has its own clone), this
repo can be trimmed to TM-only. The bullets below are
stencil-self-contained: each item is doable in this repo without
referring to Pointsman.

- [x] Rename `m4l/Stencil-TM.maxpat` → `m4l/Stencil.maxpat`. Update
      `[node.script]` filename to `stencil.mjs`.
- [x] Rename `m4l/host-tm/` → `m4l/host/`. Update `package.json`
      `name`, `tsconfig.json` paths, all relative imports, and
      rename the n4m entry to `m4l/stencil.mjs`.
- [x] In `m4l/engine/`: extract RNG primitives from `turing.ts` into
      `rng.ts` (functions `seedRng`, `nextU32`, types `RngState`).
      Re-point `turing.ts` imports to `./rng`.
- [x] Generate `docs/ai/rng-test-vectors.json` from the seed/step
      prefix of `turing-test-vectors.json`. Add `m4l/engine/rng.test.ts`
      running against it.
- [x] Delete QT-only assets in `m4l/`: `Stencil-QT.maxpat`,
      `Stencil-QT.amxd`, `m4l/stencil-qt.mjs`, `host-qt/`,
      `scaleKeyboard.jsui.js`, `engine/quantizer.ts`,
      `engine/quantizer.test.ts`. Delete
      `docs/ai/quantizer-test-vectors.json`.
- [x] Simplify `m4l/scripts/maxpat-to-amxd.mjs` to single-product
      shape (no argv; fixed I/O `Stencil.maxpat` → `Stencil.amxd`).
      Update guard tests to drop per-device branching.
- [x] `m4l/package.json` — replace `bake:tm` / `bake:qt` / `bake:check:tm`
      / `bake:check:qt` with a single `bake` / `bake:check`. Edit
      `m4l/pnpm-workspace.yaml` to drop `host-qt` and rename
      `host-tm` → `host`. Run `pnpm install` to refresh the lock
      file against the new workspace shape.
- [x] Run `pnpm -r test`, `pnpm -r typecheck`, `pnpm -r build` from
      `m4l/`; all green (TM-only suite).
- [x] Run `pnpm bake` and `pnpm bake:check`;
      `m4l/Stencil.amxd` produced, guards pass.

### Phase 4 — RNG cross-repo sync verification

The RNG is extracted independently in each repo (Pointsman owns
its own extraction; Stencil's is under Phase 3 above). This phase
confirms the two extractions produced byte-identical results.

- [x] `diff stencil/m4l/engine/rng.ts pointsman/m4l/engine/rng.ts`
      reports no difference (modulo blank lines).
- [x] `diff stencil/docs/ai/rng-test-vectors.json
      pointsman/docs/ai/rng-test-vectors.json` reports no difference.
- [x] Both repos' `rng.test.ts` pass against their own (byte-identical)
      `rng-test-vectors.json`.

## Verification

Post-split Stencil functional verification lives in
[ADR 006 §Verification](../006-m4l-release-verification.md#verification).
The §Musical motivation TM-alone premise is grounded in ongoing
standalone use of Stencil pre-split, not re-checked here.

## Per-target notes

- **m4l**: each repo's `m4l/` is a single-product workspace. One
  `.amxd`, one host package, one engine package, simplified bake
  script (no argv).
- **vst**: each repo's `vst/` is a single-product JUCE plugin (MIDI
  Effect, VST3 + AU). Per-product vst-architecture ADRs follow when
  target work begins.
- **Engine semantics** (ADR 001 contract) remain identical across both
  products' implementations. Test vectors — shared
  `rng-test-vectors.json` plus product-specific
  `turing-test-vectors.json` (Stencil) and
  `quantizer-test-vectors.json` (Pointsman) — are the cross-repo
  synchronization mechanism.

## Supersedes

- **ADR 002 §File layout** — partially superseded. The mono-package
  layout (`m4l/` containing both `host-tm/` and `host-qt/`, shared
  `engine/`) is replaced by the per-product repo split in §Repository
  split above. m4l-internal layering decisions within a single product
  (host package shape, engine package shape, `live.*` parameter
  surface, bake conventions) carry forward into each post-split repo
  unchanged.
