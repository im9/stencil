# ADR 006: Stencil m4l v1 — release

## Status: Proposed

**Created**: 2026-05-07

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

## Scope

### In scope

- Stencil-side items carried from 003 §Verification (manual Live
  checks for live.* surface, rendering, TM bit ring)
- Stencil-side items carried from 004 §Bake outputs (load-cleanly,
  bake:check, TM smoke, transport hung-note discipline)
- Stencil-side items carried from 004 §Distribution (channel,
  screenshot, demo, copy, upload)

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

This ADR has no code-side implementation. The substantive work is
the checklists in §Verification and §Distribution below; the ADR
flips to *Implemented* once every checkbox in those two sections is
`[x]`.

## Verification

Manual checks against Ableton Live.

Carried from [ADR 003 §Verification][adr3] (TM-side subset):

- [ ] Each `live.*` parameter visible in Live's Device parameter list
- [ ] Each `live.*` parameter responds to MIDI map (Cmd-M) and
      automation
- [ ] Saving a Live set, closing, reopening preserves every
      parameter value
- [ ] Right-click → "Show in Browser" / preset save round-trips
      values
- [ ] At Live 100% UI scale, the Stencil device renders within the
      1000×180 presentation strip without truncation or scrollbars
- [ ] At Live 150% UI scale, no widget label or jsui content is
      clipped (or document that 150% is out of v1 scope if Max
      can't handle it)
- [ ] In Live's Light theme, the inboil palette reads correctly
- [ ] In Live's Dark theme, the inboil palette remains readable
      (or decision recorded that v1 ships Light-theme-tuned and
      Dark is v2)
- [ ] TM bit ring: clickable interaction works, read-head advances
      on transport, register change reflects in jsui within one step

Carried from [ADR 004 §Bake outputs][adr4] (TM-side subset, with
post-split renaming):

- [ ] `Stencil.amxd` loads in Live without console errors
- [ ] `pnpm bake:check` passes on a fresh checkout
- [ ] TM smoke: trigger modes `auto` / `gate` / `seed` each produce
      sound in Live (covers ADR 002 host behavior in the real device)
- [ ] Transport stop / start / scrub leaves no hung notes on the
      Stencil device

## Distribution

Per-channel release work. Carried from [ADR 004 §Distribution][adr4]
(Stencil-side subset):

- [ ] Choose distribution channel; close the channel Open Q
- [ ] Prepare screenshot at channel-required dimensions
- [ ] Record audio demo (Stencil solo) and export MP3
- [ ] Write description copy
- [ ] Upload Stencil v1; first public version live
