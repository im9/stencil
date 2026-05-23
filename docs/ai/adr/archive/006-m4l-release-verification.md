# ADR 006: Stencil m4l v1 — release

## Status: Implemented

**Created**: 2026-05-07
**Implemented**: 2026-05-23 (m4l-v0.1.1 shipped via GitHub Releases + maxforlive.com link-only listing 15366 + im9.fm product page; §Verification + §Distribution ticked; item 3 amended in-place from MP3 export to YouTube upload; only the explicitly Optional second-machine cross-path test at line 235 left `[ ]`.)
**Revised**: 2026-05-08 — added §Release artifact tooling (esbuild
bundle of the n4m entry, root `Makefile`, `dist/` path) so the
§Distribution checklist has a concrete shippable artifact to upload.
The bundle step is necessary because Max Freeze does not follow ES
module `import` statements; the same mitigation is in place on the
QT side via oedipa-style ADR 007 §Phase 5 (cross-repo reference).

This ADR is the post-split Stencil-side home for the items deferred
from [archived ADR 003][adr3] (m4l UI design — manual Live checks)
and [archived ADR 004][adr4] (bake & distribution — bake outputs +
distribution work), per [ADR 005][adr5] §Relationship to prior ADRs.
It defines the full release gate for Stencil m4l v1: manual
verification AND per-channel distribution. The QT-side parallel
ADR is `pointsman-002`, authored in the new repo during ADR 005
Phase 2.

[adr3]: archive/003-m4l-ui-design.md
[adr4]: archive/004-m4l-bake-distribution.md
[adr5]: archive/005-product-split.md

## Context

ADR 003 (UI design) and ADR 004 (bake & distribution) shipped their
specs and code; the manual-Live verification (003 §Verification,
004 §Bake outputs) and the channel distribution work (004
§Distribution) could not be flipped by the test suites and were
deferred at archive time. ADR 005 split the project into two
products, retiring the "both devices in one repo" framing those
sections assumed. The TM-side residuals need an undisputed
post-split home; this ADR is that home. QT-side residuals have
their parallel home in `pointsman-002`.

The bullets here are **strict carry-forward** from 003 / 004:
- QT-only items dropped (10, 11-as-[x], 12-15 of 003 §Verification;
  bake:qt, QT smoke from 004 §Bake outputs; QT-portion of 004's
  audio demo) — they go to `pointsman-002`.
- Cross-product items dropped — TM → QT chain musical coherence
  and Stencil + Pointsman bundle listing are not a Stencil-side
  release concern (each product ships standalone per
  [ADR 005 §Musical motivation](005-product-split.md#context)); if
  needed, they live in a future cross-product ADR.
- Already-`[x]` items not re-listed — the historical record stays
  in the archived 003 / 004.
- Wording adjusted only for the post-split single-device naming
  ("on both devices" → singular; `Stencil-TM.amxd` → `Stencil.amxd`).

The 2026-05-08 revision adds the build/freeze plumbing that turns
§Distribution from "upload an artifact" into a runnable flow. The
dev-mode `m4l/Stencil.amxd` produced by `pnpm bake` references
sibling JS on disk and only loads on the build machine. To ship, the
device must be frozen in Max so every referenced file is inlined.
Two coupled prerequisites surfaced when setting this up:

- **Max Freeze does NOT follow ES module `import` statements.** This
  was empirically established on the QT side in oedipa-007 Phase 5;
  the same constraint applies here. The current `m4l/stencil.mjs`
  imports `./host/dist/host/bridge.js`, whose chain reaches `host.js`
  / `engine/turing.js` / `engine/rng.js`. Freeze captures only the
  entry; the import targets are lost in the frozen `.amxd`. The
  mitigation is to pre-bundle the entry with esbuild before bake,
  with `max-api` as the only external (Max-injected at runtime).
- **There is no repo-level command for producing the shippable
  artifact.** Adding a root `Makefile` with `make release` mirrors
  oedipa's flow: `pnpm -r build && pnpm bake` → `dist/` ready →
  manual snowflake freeze in Max → save as `dist/Stencil.amxd`.

## Decision

Stencil m4l v1 ships when every checkbox in §Verification AND
§Distribution below is `[x]`. §Verification is the manual-Live
correctness gate (a flat list, no per-area subsections, mirroring
003 §Verification's shape). §Distribution is the per-channel release
work that follows verification.

When a §Verification check fails, the failure routes back to the
originating ADR (002 / 003 / 004 in archive) for fix; this ADR does
not re-spec underlying behavior, only records pass/fail.

§Bake artifact hygiene depends on ADR 005 Phase 3 having landed
(single-product bake produces `Stencil.amxd` from `Stencil.maxpat`);
running it before Phase 3 will fail on filenames. Phase 3 itself
verifies the bake produces the artifact (ADR 005 Phase 3 checklist),
so this ADR carries only the load-cleanly + bake:check items, not a
duplicate "bake produces .amxd" item.

### Release artifact production (added 2026-05-08)

The shippable artifact `dist/Stencil.amxd` is produced by:

1. **`pnpm bundle:host`** — esbuild bundles `m4l/stencil.entry.mjs`
   into `m4l/stencil.mjs`, with `max-api` as the only external. The
   entry file `stencil.entry.mjs` holds the wiring currently in
   `stencil.mjs`; the bundled `stencil.mjs` is what `[node.script]`
   actually loads (the `.maxpat` reference stays as
   `node.script stencil.mjs` — bare-sibling form per ADR 004
   §Patcher path conventions, no `.maxpat` change required).
2. **`pnpm bake`** — runs `bundle:host` first, then writes
   `m4l/Stencil.amxd` referencing the bundled `stencil.mjs` as a
   sibling. This is the developer iteration loop; the produced
   `.amxd` only loads on the build machine because the bundle still
   sits on disk next to it.
3. **`make release`** (from repo root) — runs `cd m4l && pnpm -r
   build && pnpm bake` and ensures `dist/` exists. Then prints
   instructions for the manual freeze step. Default `make` target is
   `release`, aliased to `release-m4l` (mirrors oedipa, leaves room
   for a future `release-vst`).
4. **Manual freeze** — open `m4l/Stencil.amxd` in Max → click the
   snowflake (Freeze) button in the patcher toolbar → File → Save As
   `dist/Stencil.amxd`. The frozen file is self-contained: every
   referenced JS (bundled `stencil.mjs`, `registerRing.jsui.js`,
   `registerRing.subpatcher.maxpat`) is inlined into the `.amxd`
   binary and runs on any Live install regardless of where the user
   drops it on disk.

Freeze is a manual Max action; Max provides no CLI freeze. The
distribution flow is therefore inherently two-stage: automated
build/bake for development, manual freeze for release. Acceptable per
the same reasoning as oedipa-007 §Out of scope — freezes happen per
release, not per edit.

Why bundle output is `.mjs`, not `.js`: the freeze sandbox extracts
the inlined script to a tempdir without any sibling `package.json`,
so a `.js` file would be interpreted as CommonJS and the
`import Max from "max-api"` line would fail to parse, leaving
`[node.script]` permanently in "Node script not ready" state. `.mjs`
is unconditionally ESM. Empirically established by oedipa-007 Phase 5;
the existing stencil entry already uses `.mjs` for the same reason
(per the header comment in `m4l/stencil.mjs`).

## Scope

### In scope

- Stencil-side items carried from 003 §Verification (manual Live
  checks for live.* surface, rendering, TM bit ring)
- Stencil-side items carried from 004 §Bake outputs (load-cleanly,
  bake:check, TM smoke, transport hung-note discipline)
- Stencil-side items carried from 004 §Distribution (channel,
  screenshot, demo, copy, upload)
- esbuild bundle step (`m4l/stencil.entry.mjs` → `m4l/stencil.mjs`)
  wired into `pnpm bake` (added 2026-05-08)
- Repo-root `Makefile` with `make release` / `make release-m4l`
  targets producing `dist/` (added 2026-05-08)
- Cross-path manual freeze verification on `dist/Stencil.amxd`
  (added 2026-05-08)
- CLAUDE.md §Build/m4l update for the freeze flow (added 2026-05-08)

### Out of scope

- **Pointsman / QT-side items** — `pointsman-002`.
- **Stencil → Pointsman chain verification** — not a Stencil-side
  concern; downstream composition is the user's choice, not a
  release gate.
- **Cross-product Stencil + Pointsman bundle listing** — not a
  Stencil-side concern; if pursued, lives in a future cross-product
  distribution ADR.
- **VST target verification and distribution** — separate ADR
  series; vst is paused at scaffold per ADR 005.
- **Phase 3 bake-produces-Stencil.amxd verification** — ADR 005
  Phase 3 checklist; not duplicated here.

## Implementation checklist

§Verification and §Distribution below are the manual / release-process
gates. The build-side work added in the 2026-05-08 revision (esbuild
bundle + Makefile + cross-path verification) is phased here per
CLAUDE.md TDD gates. The ADR flips to *Implemented* once every
checkbox in this section AND in §Verification AND in §Distribution is
`[x]`.

### Phase 1 — Entry split + bundle wiring

- [x] Move current contents of `m4l/stencil.mjs` to
      `m4l/stencil.entry.mjs`. Internal imports (`max-api`,
      `./host/dist/host/bridge.js`) are unchanged. Update header
      comment to reflect the new entry/bundle split (path-conventions
      language stays valid; the bundled output is what `[node.script]`
      ultimately loads).
- [x] Add `esbuild` as devDependency at the m4l workspace root
      (`m4l/package.json`).
- [x] Add `bundle:host` script to `m4l/package.json`:
      `esbuild stencil.entry.mjs --bundle --platform=node
      --format=esm --external:max-api --outfile=stencil.mjs`.
- [x] Update `bake` script: `bake = pnpm bundle:host && node
      scripts/maxpat-to-amxd.mjs`.
- [x] Add `m4l/stencil.mjs` to `.gitignore` (build artefact). Keep
      `m4l/stencil.entry.mjs` tracked. Remove the now-stale committed
      `m4l/stencil.mjs` from git.
- [x] `.maxpat` `[node.script]` filename stays `stencil.mjs` — no
      patcher edit, abs-path scrub guard test continues to pass.

### Phase 2 — Bundle guard test

- [x] Add `m4l/host/stencil-bundle.test.ts` (picked up by
      `pnpm -r test`): assert that `m4l/stencil.mjs`, when present,
      has only `max-api` as a remaining external import. Skip on
      fresh checkouts where the bundle hasn't been built yet (mirrors
      oedipa's `oedipa-host-bundle.test.ts` skip semantics).
- [x] Run `pnpm -r build && pnpm bake && pnpm -r test`: bundle test
      passes; existing host / engine / bake tests stay green.

### Phase 3 — Root Makefile + dist/

- [x] Add repo-root `Makefile` with `release` (default → depends on
      `release-m4l`) and `release-m4l` targets. `release-m4l` runs
      `cd m4l && pnpm -r build && pnpm bake`, runs `mkdir -p dist`,
      and prints the snowflake-freeze instructions (full target
      path: `$(CURDIR)/dist/Stencil.amxd`).
- [x] Add `dist/` to repo-root `.gitignore` (frozen artefact is
      regenerated from source per release; do not commit binaries).
- [x] Update CLAUDE.md §Build/m4l with a "Distribution (release
      builds)" subsection: `make release` from repo root → snowflake
      in Max → `dist/Stencil.amxd`; mention the Freeze-doesn't-follow-
      imports constraint and that the bundle step is the mitigation.

### Phase 4 — Cross-path freeze verification

- [x] Run `make release` on a clean checkout. Verify
      `m4l/Stencil.amxd` (dev) is produced and `dist/` exists with no
      errors.
- [x] Open `m4l/Stencil.amxd` in Max → click snowflake → Save As
      `dist/Stencil.amxd`. *(Done 2026-05-09; frozen artefact 80192
      bytes vs 34261-byte dev .amxd, confirming JS inlining occurred.)*
- [x] Cross-path test: copy `dist/Stencil.amxd` to a path outside
      the repo (e.g. `~/Downloads/`). Drag into a fresh Live track.
      Confirm: Max console reports `stencil: stencil.mjs loaded`,
      the `ready 1` outlet fires, parameter dump arrives, transport
      play produces MIDI through the device, and the register ring
      jsui renders + advances on transport. *(Verified 2026-05-09.)*
- [ ] (Optional, only if a second machine is conveniently available)
      Repeat the cross-path test on a second machine. Not blocking;
      the cross-path test on a single machine catches the same class
      of bug because the un-frozen `.amxd` would fail to resolve its
      sibling JS at the new path.

## Verification

Manual checks against Ableton Live.

Carried from [ADR 003 §Verification][adr3] (TM-side subset):

- [x] Each `live.*` parameter visible in Live's Device parameter list
- [x] Each `live.*` parameter responds to MIDI map (Cmd-M) and
      automation
- [x] Saving a Live set, closing, reopening preserves every
      parameter value
- [x] Right-click → "Show in Browser" / preset save round-trips
      values
- [x] At Live 100% UI scale, the Stencil device renders within the
      1000×180 presentation strip without truncation or scrollbars
- [x] At Live 150% UI scale, no widget label or jsui content is
      clipped (or document that 150% is out of v1 scope if Max
      can't handle it)
- [x] In Live's Light theme, the inboil palette reads correctly
- [x] In Live's Dark theme, the inboil palette remains readable
      (or decision recorded that v1 ships Light-theme-tuned and
      Dark is v2)
- [x] TM bit ring: clickable interaction works, read-head advances
      on transport, register change reflects in jsui within one step

Carried from [ADR 004 §Bake outputs][adr4] (TM-side subset, with
post-split renaming):

- [x] `Stencil.amxd` loads in Live without console errors
- [x] `pnpm bake:check` passes on a fresh checkout
- [x] TM smoke: trigger modes `auto` / `gate` / `seed` each produce
      sound in Live (covers ADR 002 host behavior in the real device)
- [x] Transport stop / start / scrub leaves no hung notes on the
      Stencil device

## Distribution

Per-channel release work. Carried from [ADR 004 §Distribution][adr4]
(Stencil-side subset):

- [x] Choose distribution channel; close the channel Open Q
- [x] Prepare screenshot at channel-required dimensions
- [x] Record demo video (Stencil solo) and upload to YouTube
- [x] Write description copy
- [x] Upload Stencil v1; first public version live
