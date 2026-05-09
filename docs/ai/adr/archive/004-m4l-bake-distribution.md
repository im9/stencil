# ADR 004: m4l Bake & Distribution

## Status: Implemented

**Created**: 2026-05-02
**Implemented**: 2026-05-07 (bake script + path conventions + abs-path / external-ref guard tests shipped; both devices bake to .amxd. argv parameterization obsolete post-split — replaced per-repo by ADR 005 Phase 2/3 single-product bake. §Distribution and device-smoke items move to per-product distribution ADRs in each post-split repo.)

> **Pricing posture note (2026-05-09)** — references in this ADR
> to "free distribution" / "im9 / free distribution" (lines 148,
> 171, 223) were accurate at the time of writing. The canonical
> posture has since been amended in
> [ADR 005 §Distribution posture](005-product-split.md#distribution-posture)
> (2026-05-09 in-place amendment): m4l builds remain free,
> VST3 / AU / CLAP builds are planned as paid releases. ADR 004's
> wording is preserved as historical record per
> `feedback_porting_bug_amend_not_supersede`.

This ADR specifies how the `.maxpat` source files are baked into
distributable `.amxd` artifacts, the path / external-reference conventions
inside the patcher JSON, the guard tests that defend the bake pipeline,
and the packaging conventions for distribution channel upload.

## Context

[ADR 002](002-m4l-architecture.md) defines the file layout — `.maxpat`
source tracked in git, `.amxd` is a built artifact — and resolves the
script choice ("port from oedipa with argv parameterization"). It does
not specify the bake pipeline itself, the path conventions inside
`.maxpat`, the guard tests, or how the resulting `.amxd` files are
packaged for distribution channel upload.

[ADR 003](003-m4l-ui-design.md) covers the UI inside the `.maxpat` files
but is silent on the path / freeze layer. Without an ADR to anchor the
bake pipeline, the bake script port and its guard tests have no spec to
verify against — they would just match oedipa's behavior, which is fine
operationally but leaves Stencil-specific decisions undocumented (two
devices instead of one, bundle vs split listing).

[oedipa ADR 007](https://github.com/im9/oedipa) is the reference for the
single-device case. This ADR ports + extends.

## Decision

### Bake script

`m4l/scripts/maxpat-to-amxd.mjs`, ported from oedipa with two changes:

1. Accept `argv[2] ∈ {TM, QT}` (validated; reject else).
2. Resolve I/O as
   `m4l/Stencil-${argv[2]}.maxpat` → `m4l/Stencil-${argv[2]}.amxd`
   (flat, matching oedipa's `m4l/Oedipa.amxd` and the project root
   `CLAUDE.md` §Layout).

Core operations carry verbatim from oedipa: AMPF header splice, JSON
validation, `--check` mode that returns nonzero without writing.

Workspace scripts at `m4l/package.json`:

```
pnpm bake:tm           # node scripts/maxpat-to-amxd.mjs TM
pnpm bake:qt           # node scripts/maxpat-to-amxd.mjs QT
pnpm bake:check:tm     # ... TM --check
pnpm bake:check:qt     # ... QT --check
pnpm bake              # bake:tm && bake:qt
pnpm bake:check        # bake:check:tm && bake:check:qt
```

### Patcher path conventions

- `[node.script]` references its host entry by **flat sibling filename**
  at the `m4l/` root (`stencil-tm.mjs`, `stencil-qt.mjs`) — not absolute,
  not subdirectory-pathed. Empirically Max [node.script]'s `filename`
  resolution does not handle subdirectory paths in M4L presentation view
  (observed: "No such file or directory" in Max log even when the file
  exists at the relative subdirectory location). Same constraint as
  Max [jsui] above. Compiled bridge artifacts still live under
  `host-tm/dist/host-tm/` etc.; the flat entry imports them with relative
  Node ESM paths (`./host-tm/dist/host-tm/bridge.js`), which Node's
  resolver handles correctly — the subdir constraint is on Max's
  `filename` attribute, not on what the script itself imports.
  The `.mjs` extension is load-bearing: when [node.script] extracts the
  script to a tempdir during baked-`.amxd` load, there's no sibling
  `package.json` to set `"type":"module"`, so a `.js` filename would be
  parsed as CJS and the `import` statement would fail. (See header
  comment in `m4l/stencil-tm.mjs`.)
- `[jsui]` references its renderer file by **flat sibling filename**
  at the `m4l/` root (`registerRing.jsui.js`, `scaleKeyboard.jsui.js`).
  Empirically Max's [jsui] `filename` resolution does not reliably
  handle subdirectory paths in M4L presentation view — a
  subdirectory-pathed renderer rendered as a generic gray placeholder
  in Live instead of running the renderer code. The corresponding
  `*.logic.ts` and `*.logic.test.ts` stay under
  `host-{tm,qt}/ui/` (those don't pass through Max's resolver).
- No absolute paths anywhere in `.maxpat` JSON: no `/Users/`, no
  `/home/`, no `C:\`, no Windows drive letters.
- All sub-patcher refs (if any are introduced later) follow the same
  bare-sibling rule.
- Bridge outlet symbols emitted to `[jsui]` (via `Max.outlet` →
  `[route ...]` → `[prepend ...]`) must NOT collide with Max box
  attribute names. `position` is the known offender — Max parses it
  as an attribute setter and shifts the jsui box by N pixels per
  inlet message (1px-per-step creep observed in M4L locked view,
  2026-05-03). Use domain-specific camelCase (`ringHead`, not
  `position`); same rule as oedipa's `latticeCenter` / `setCells`.

### Guard tests

Run via `pnpm bake:check:*`. Failures are nonzero exit and block bake.

1. **Abs-path scrub** — fail if `.maxpat` JSON contains `/Users/`,
   `/home/`, or matches a Windows-absolute path regex.
2. **External validation** — fail if any `[node.script]` or `[jsui]`
   filename referenced in `.maxpat` does not exist as a sibling file
   relative to the `.maxpat` root. Both kinds are validated by the same
   pass — every `filename`-bearing object in the patcher JSON must
   resolve.
3. **Live.* parameter consistency** *(optional, deferred)* — fail if any
   `live.*` widget references a parameter name not in ADR 002's table.

Tests run on each device's `.maxpat` independently — TM can pass while
QT fails.

### Build artifact tracking

- Host TS sources compile to `dist/` per package
  (`m4l/host-tm/dist/`, `m4l/host-qt/dist/`). Committed to git;
  `[node.script]` loads them directly without a build step on the user's
  machine. (Already specified in ADR 002 §File layout.)
- `.amxd` outputs land at `m4l/Stencil-{TM,QT}.amxd` (flat, alongside
  the matching `.maxpat`). Committed to git as the dev/distributable
  artifact — matches oedipa's `m4l/Oedipa.amxd` convention and the
  project root `CLAUDE.md` §Layout. Frozen / channel-upload artifacts
  (post-Max-Freeze) live at the repo root `dist/` per the same oedipa
  convention; created at upload time, not by `pnpm bake`.

### Distribution packaging

Two packaging modes are supported; the choice is made at upload time
based on the channel's upload policy:

- **Bundle**: a single archive `Stencil.zip` containing
  `Stencil-TM.amxd`, `Stencil-QT.amxd`, plus a short `README.txt` (use
  case + chain example) and an optional demo `.mid` clip.
- **Split**: two separate listings, each with its single `.amxd` and
  per-device README/demo.

Both modes are achievable from the same baked `.amxd` set; the listing
material differs.

### Listing materials

Required at upload time (v1):

- Device screenshot — Live's device strip view, both devices visible if
  bundle, single device if split.
- Audio demo — 30–60 sec MP3 covering: TM solo, QT solo, TM→QT chain.
- Description copy — 1 paragraph use case + 1 paragraph features.
- Author / license: `im9 / free distribution` per project README.

Format / dimensions are channel-specific; resolve at upload time.

## Open questions

These are flagged for follow-up rather than blocking implementation.
Resolution events are noted where known.

- **Distribution channel** — maxforlive.com is the obvious default for
  m4l devices but its listing format and pricing model may force
  packaging decisions. Alternatives: gumroad, itch.io, GitHub releases
  (free only). Resolves at upload time when the v1 verification
  checklist is green.
- **Bundle vs split listing** — depends on the channel above. Both are
  achievable from the same artifact set; only the listing material
  differs. (Moved here from ADR 002 §Open questions.)
- ~~**`.amxd` output path**~~ — *resolved 2026-05-03*. Bake writes
  `m4l/Stencil-{TM,QT}.amxd` (flat). The earlier draft of this ADR
  assumed `m4l/dist/` "matched oedipa" — that was a misread; oedipa
  keeps the dev `.amxd` at `m4l/Oedipa.amxd` and reserves the repo-root
  `dist/` for frozen/release artifacts. Stencil follows the same
  convention.
- **Free vs paid** — README says "free distribution" but channels may
  offer name-your-price or tip-jar models. Non-architectural; resolves
  at upload time.
- **Versioning / update flow** — once v1 ships, how are updates pushed
  (re-upload? channel-specific update mechanics? semver tags on git?).
  Defer to first-update event; not blocking v1.
- **Listing screenshot / demo specifics** — exact pixel dimensions,
  audio length, demo musical content. Resolve at upload time when the
  channel's upload form is open.

## Implementation checklist

### Bake pipeline

- [x] Port `m4l/scripts/maxpat-to-amxd.mjs` from oedipa
- [x] Parameterize `argv[2]` for `TM` | `QT`; validate
- [x] Wire `pnpm bake:tm`, `bake:qt`, `bake`, and `bake:check:*` at
      `m4l/package.json`
- [x] Abs-path scrub guard test
- [x] External-validation guard test (verify `[node.script]` filenames
      resolve as sibling files)

### Bake outputs

- [x] `pnpm bake:tm` produces `m4l/Stencil-TM.amxd` from a working
      `Stencil-TM.maxpat`
- [ ] `pnpm bake:qt` produces `m4l/Stencil-QT.amxd` from a working
      `Stencil-QT.maxpat`
- [ ] Both `.amxd` load in Live without console errors
- [ ] `pnpm bake:check` passes on a fresh checkout
- [ ] TM smoke: trigger modes `auto` / `gate` / `seed` each produce sound
      in Live (covers ADR 002 host behavior in the real device)
- [ ] QT smoke: scale snap + humanize coverage audible across input
      pitches; root mode `controlChannel` updates take effect
- [ ] TM → QT chain produces a musically coherent scale-locked melody
- [ ] Transport stop / start / scrub leaves no hung notes on either device

### Distribution

- [ ] Choose distribution channel; close the channel Open Q
- [ ] Decide bundle vs split based on channel; close that Open Q
- [ ] Prepare screenshot at channel-required dimensions
- [ ] Record audio demo (TM solo / QT solo / chain) and export MP3
- [ ] Write description copy
- [ ] Upload v1; first public version live
- [ ] Flip ADR 002 / 003 / 004 to *Implemented* and archive

## Out of scope

- **Versioning / update mechanics post-v1** — open question, deferred
  to first-update event.
- **Marketing / pricing strategy** — non-architectural; the README
  states free distribution and that holds unless the channel forces
  otherwise.
- **vst distribution** — separate target, separate ADR series, post-v1
  per the ADR set's implicit roadmap.
- **CI for bake** — running `bake:check` in GitHub Actions on PR is a
  nice-to-have; add when there's a second contributor or the manual
  check becomes burdensome.
