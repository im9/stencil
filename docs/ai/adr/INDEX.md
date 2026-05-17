# ADR Index

Quick reference for all Architecture Decision Records. Read individual ADRs
only when relevant to the current task.

## Status Legend

- **Proposed**: Not yet fully implemented. Contains an implementation checklist; flip to Implemented once all boxes are checked.
- **Implemented**: Done. Code is the source of truth. Read only for historical rationale.
- **Superseded**: Replaced by a newer ADR. Generally skip.

## File Organization

- **Top-level** (`docs/ai/adr/`): Proposed and Accepted ADRs — active design decisions.
- **Archive** (`docs/ai/adr/archive/`): Implemented and Superseded ADRs — historical record.

## Conventions

- File name: `NNN-kebab-case-title.md` (3-digit zero-padded)
- Header: `# ADR NNN: Title`
- Status line: `## Status: Proposed | Accepted | Implemented | Superseded`
- Created date: `**Created**: YYYY-MM-DD`
- Sections: Context → Decision → (optional) Scope / Implementation notes

## Core

| #   | Title | Status | Notes |
|-----|-------|--------|-------|
| 001 | [Engine Interface — TM + Quantizer](archive/001-engine-interface.md) | Implemented | Pure-function APIs for `turing.ts` and `quantizer.ts`; types, semantics, shared test vectors. m4l reference impl 10/10 (2026-05-02). |
| 005 | [Product Split — Stencil (TM) and Pointsman (QT)](archive/005-product-split.md) | Implemented | Per-product repo split (`stencil/`, `pointsman/`); RNG primitives shared via `rng-test-vectors.json` cross-repo sync; vst/AU = single-purpose MIDI Effect per product (VST3 + AU); m4l asset rename + migration; archive ADRs 003 / 004 with deferred verification items moving to ADR 006 (TM) and pointsman-002 (QT). |

## M4L

The M4L ADRs (002–004) defined the pre-split v1. ADR 005 splits the
two devices into separate per-product repos; deferred verification
items from 003 / 004 land in ADR 006 (TM-side, this repo) and in
`pointsman-002` (QT-side, authored in the new repo during ADR 005
Phase 2). §Distribution items go to a separate per-product
distribution ADR per ADR 005 §Relationship to prior ADRs.

| #   | Title | Status | Notes |
|-----|-------|--------|-------|
| 002 | [M4L Architecture — Stencil TM + Stencil QT](archive/002-m4l-architecture.md) | Implemented | Two-device topology; per-device host/patcher/engine layering; live.* parameters; MIDI I/O & triggerMode; QT humanize layer; state ownership. Host code complete 2026-05-02 (125 unit tests). |
| 003 | [M4L UI Design — Stencil TM / Stencil QT](archive/003-m4l-ui-design.md) | Implemented | Double-height canvas; live.* params + 2 jsui widgets per device (TM clickable bit ring, QT pulse-animated scale keyboard); inboil visual identity; logic/renderer split. §Verification manual-Live items deferred to ADR 006 (TM) / pointsman-002 (QT) per ADR 005. |
| 004 | [M4L Bake & Distribution](archive/004-m4l-bake-distribution.md) | Implemented | Bake script + bare-sibling path conventions + abs-path / external-ref guard tests shipped. argv parameterization obsoleted post-split (replaced per-repo by ADR 005 Phase 2/3). §Distribution items move to per-product distribution ADRs. |
| 006 | [Stencil m4l v1 — release verification](006-m4l-release-verification.md) | Proposed | Manual-Live verification gate for Stencil m4l v1: live.* surface coverage, rendering at multiple UI scales / themes, TM bit-ring jsui interaction, TM smoke (trigger × output modes), transport behavior, bake artifact hygiene. Carries forward TM-side §Verification (ADR 003) + TM-side §Bake outputs (ADR 004); QT-side handoff happens in pointsman-002 during ADR 005 Phase 2. **Amended 2026-05-08 with §Release artifact production** (esbuild bundle of n4m entry, root `Makefile` with `make release`, `dist/` path) so the §Distribution checklist has a concrete shippable artefact to upload. |

## VST

VST is post-v1. ADR 007 fulfills ADR 005 §VST/AU posture's
delegation by specifying the C++17 / JUCE plugin architecture
(formats, source layout, APVTS surface, MIDI processing, editor
ported from inboil's `TuringSheet.svelte`, build via CMake +
`clap-juce-extensions` per oedipa's production-shipped template).
Engine semantics + test vectors carry over from ADR 001; the
m4l-specific ADRs (002–004) do not apply (vst is a fresh JUCE
implementation, not an m4l UI port).

| #   | Title | Status | Notes |
|-----|-------|--------|-------|
| 007 | [Stencil VST architecture — JUCE MIDI Effect (VST3 + AU + CLAP)](archive/007-vst-architecture.md) | Implemented | C++17 engine port with byte-identical RNG/TM vs m4l, APVTS-backed canonical parameter surface, transport-driven sample-accurate MIDI processing with hung-note discipline, JUCE editor ported from inboil's `TuringSheet.svelte` (ring + revolver rotation + history bars + 4-fieldset right rail), CMake build matrix matching oedipa's VST3 + AU + CLAP template (clap-juce-extensions submodule). macOS host scope inherits oedipa's: Logic + Bitwig primary, Reaper + Studio One best-effort; Live = m4l only, Cubase / FL out of scope on host-policy grounds. Host load verified 2026-05-17 in Logic AU + Bitwig CLAP/VST3 + Reaper; Studio One deferred (no host on hand). |
