---
name: release
description: Cut a versioned per-target GitHub release of Stencil (m4l or vst). Bumps the source-of-truth version (vst/CMakeLists.txt project(...) for vst, m4l/package.json for m4l), verifies repo state (clean / synced / CI green / artifact freshness), drafts release notes from the per-target commit log, and runs the tag → push → gh release create flow with explicit user approval at each step.
argument-hint: "<m4l|vst> [major|minor|patch]"
allowed-tools: Read, Write, Edit, Bash(git *), Bash(gh *), Bash(stat *), Bash(ls *), Bash(rm /tmp/stencil-*)
---

# Release Stencil

Cut a versioned per-target GitHub release. The first $ARGUMENT
selects the target (`m4l` or `vst`); the second is the bump
(`major` / `minor` / `patch`, default `patch`).

Tags are namespaced per target: `<target>-vX.Y.Z`. Each target
versions independently — m4l hotfixes don't bump vst, and vice versa.
This matches oedipa's convention (ADR 005 §Distribution posture; the
two targets ship on independent cadences).

The release asset is target-specific:

- **m4l** → `dist/Stencil.amxd` — frozen `.amxd`. Manual freeze
  in Max required (snowflake button → *File → Save As*). See
  [ADR 004 §Bake / distribution](../../../docs/ai/adr/004-m4l-bake-distribution.md)
  and [ADR 006 §Release verification](../../../docs/ai/adr/006-m4l-release-verification.md).
- **vst** → both `dist/Stencil.dmg` AND `dist/Stencil.pkg`. The pair
  is signed + notarized + stapled by
  [`vst/scripts/build-dmg.sh`](../../../vst/scripts/build-dmg.sh) and
  [`vst/scripts/build-pkg.sh`](../../../vst/scripts/build-pkg.sh)
  respectively, both run after `codesign.sh` + `notarize.sh`. The two
  artifacts serve different install paths (per ADR 007 §Distribution):
  - **DMG** — drag-to-install, user picks the target folder. Custom
    Audio Plug-Ins paths and segregated dev/release directories.
  - **PKG** — signed system-wide installer with admin auth. Places
    bundles at `/Library/Audio/Plug-Ins/{VST3,Components,CLAP}/`.
    Per-format opt-out in the Customize step.

  Per [ADR 005 §Distribution posture](../../../docs/ai/adr/archive/005-product-split.md)
  vst is a paid release; if/when the channel decision (gumroad / itch /
  own site) requires off-GitHub delivery instead of attaching to the
  GH Release, **HALT at Step 3 and confirm with the user** before
  uploading. Default in this skill is to attach both artifacts,
  mirroring m4l's amxd attachment.

## Pre-flight checks (do these BEFORE creating the tag)

Tags are durable. Once pushed, a release with downloads is harder to
undo cleanly. Run all checks; STOP and ask the user if any fail.

### Check 1 — Working tree is clean

`git status --porcelain` must be empty. Uncommitted changes leak
into the release context if you tag now. Halt if dirty.

### Check 2 — main is synced with origin

```bash
git fetch origin --quiet
git rev-list --count main..origin/main   # must be 0 (origin not ahead)
git rev-list --count origin/main..main   # must be 0 (local not ahead)
```

If origin is ahead, `git pull`. If local is ahead, push first
(via `/commit` for any unstaged work, then a normal push). Then
re-run.

### Check 3 — CI is green on HEAD

```bash
gh run list --branch main --limit 5 --json conclusion,headSha,workflowName
```

The most recent completed run for the current HEAD SHA must have
`conclusion: "success"`. If still in progress or failed, halt and
ask.

### Check 4 — Artifact exists and reflects current target source

The artifact is gitignored, so it lives only on the build machine.
Verify per target.

#### m4l target

```bash
ls -la dist/Stencil.amxd
stat -f '%m' dist/Stencil.amxd                  # mtime as epoch
git log -1 --format=%ct -- m4l/Stencil.maxpat \
                            m4l/stencil.mjs \
                            m4l/registerRing.jsui.js \
                            m4l/registerRing.subpatcher.maxpat \
                            m4l/engine m4l/host        # latest m4l-source commit time
```

`dist/Stencil.amxd` mtime must be **>=** the latest m4l-source
commit time. If older, halt and remind:

> Open `m4l/Stencil.amxd` in Max → click the snowflake (Freeze)
> button in the patcher toolbar → *File → Save As*
> `dist/Stencil.amxd`.

Even when the mtime check passes, **manual smoke test in a fresh
Live track is recommended before tagging** — drag
`dist/Stencil.amxd` onto a new MIDI track, confirm it loads,
plays, the bit ring renders, FREEZE / ROLL respond. CI does not
(and cannot) cover this.

#### vst target

Two artifacts: `dist/Stencil.dmg` (drag-to-install) AND
`dist/Stencil.pkg` (system-wide installer). Both built by a single
`make release-vst` invocation at the repo root:

```bash
(cd vst && make build && make test && make verify-artefacts)
DEVELOPER_TEAM_ID=XXXXXXXXXX make release-vst
```

`make release-vst` chains `codesign.sh → notarize.sh → build-dmg.sh →
build-pkg.sh`; all four scripts require `DEVELOPER_TEAM_ID` (Developer
ID team — selects both the "Developer ID Application" cert for the
bundles + dmg and the "Developer ID Installer" cert for the pkg) and a
`notarytool` keychain profile named `im9-notary` (shared across im9
plugins; override via `NOTARY_PROFILE`). One-time setup of the keychain
profile:

```bash
xcrun notarytool store-credentials im9-notary \
  --apple-id <APPLE_ID> --team-id <TEAM_ID> --password <APP_SPECIFIC_PW>
```

Verify freshness — both artifacts must post-date the latest vst-source
commit:

```bash
ls -la dist/Stencil.dmg dist/Stencil.pkg
stat -f '%m' dist/Stencil.dmg dist/Stencil.pkg            # mtime as epoch
git log -1 --format=%ct -- vst/Source vst/CMakeLists.txt  # latest vst-source commit time
```

Both `dist/Stencil.dmg` and `dist/Stencil.pkg` mtimes must be **>=**
the latest vst-source commit time. If either is older (or missing),
rerun `make release-vst` (which rebuilds, re-signs, re-notarizes, and
regenerates both artifacts in one pass — the source build clears
prior signatures so partial reruns are not supported).

Per-artifact stapling check before tagging:

```bash
xcrun stapler validate dist/Stencil.dmg
xcrun stapler validate dist/Stencil.pkg
pkgutil --check-signature dist/Stencil.pkg     # pkg-specific signature check
```

Manual host smoke (Logic AU MIDI FX + Bitwig VST3/CLAP MIDI fx) is
recommended before tagging — ADR 007's host-load matrix is the
gating manual step.

## Drafting

After pre-flight passes:

### Step 0 — Bump the source-of-truth version

The displayed version in the editor (`v0.1.x` label in the header
right) is fed from `project(Stencil VERSION ...)` in
`vst/CMakeLists.txt` via `STENCIL_VERSION_STRING`. The tag and the
in-binary version **must move together** — otherwise the loaded
plugin claims one version while the GH release announces another.

```bash
# Determine next version (logic in Step 1 below) and edit BEFORE tagging:
#   vst target → vst/CMakeLists.txt   line 2: project(Stencil VERSION x.y.z)
#   m4l target → m4l/package.json     "version": "x.y.z"
#                m4l/engine/package.json + m4l/host/package.json if you
#                version them in lockstep (currently 0.0.0 placeholders)
```

Commit this bump as `chore(<target>): vX.Y.Z` BEFORE creating the
tag, so the tag points at a commit whose source already reports the
new version. Then re-run pre-flight Check 4 (artifact mtime must now
post-date the bump commit — for m4l, this means re-baking the
`.amxd`).

**Confirm with the user before committing the bump** — same gate as
any other commit.

### Step 1 — Determine next version

```bash
git tag -l '<target>-v*' | sort -V | tail -1
```

Parse the highest `<target>-vX.Y.Z` tag. Bump per the second
$ARGUMENT (default `patch`). If no prior tag for this target,
propose `<target>-v0.1.0`.

Show the proposed version to the user and **confirm before
proceeding to Step 0**. The user can override.

### Step 2 — Draft release notes

Generate the draft from the commit log between the previous
**per-target** tag and HEAD:

```bash
PREV=$(git tag -l '<target>-v*' | sort -V | tail -1)
git log "${PREV:-}"..HEAD --pretty=format:'- %s'
```

If `$PREV` is empty (first release for this target), use a wider
range or the project root commit as the lower bound. For stencil's
**first** vst release specifically, scope the log to vst-touching
commits:

```bash
git log --pretty=format:'- %s' -- vst/ docs/ai/adr/
```

Categorize commits by their `type(scope):` prefix into sections:

- **Features** — `feat:`
- **Fixes** — `fix:`
- **Docs / housekeeping** — `docs:` / `chore:` / `style:` / `refactor:`
- **CI / build** — `ci:`

Drop the `Co-Authored-By` lines and trailing housekeeping noise.
Keep the section short — release notes are for users, not
contributors; detailed history is in `git log`.

For the very first release of a target (no prior `<target>-v*` tag),
use a project-intro template instead of a changelog: "What it does"
/ "Install" / "Requirements".

Write the draft to `/tmp/stencil-<tag>-notes.md` and show it to the
user. **Wait for explicit "ok" or edit instructions** before Step 3.

### Step 3 — Tag, push, create release

```bash
TAG=<target>-vX.Y.Z
TITLE="Stencil <target> vX.Y.Z"

git tag "$TAG"
git push origin "$TAG"
```

Then per target:

```bash
# m4l — attach the frozen .amxd
gh release create "$TAG" dist/Stencil.amxd \
  --title "$TITLE" \
  --notes-file "/tmp/stencil-$TAG-notes.md"

# vst — attach BOTH the signed-and-notarized DMG and PKG (or HALT
#       if ADR 005 channel decision routes either off-GitHub)
gh release create "$TAG" dist/Stencil.dmg dist/Stencil.pkg \
  --title "$TITLE" \
  --notes-file "/tmp/stencil-$TAG-notes.md"
```

### Step 4 — Verify

```bash
gh release view "$TAG" --json name,tagName,assets,url
```

Confirm:

- For m4l: `assets[0].name == "Stencil.amxd"`, `assets[0].size > 0`,
  and matches the local file's size.
- For vst: two assets — one `Stencil.dmg`, one `Stencil.pkg`, both
  `size > 0` and matching the local files' sizes (assets list order
  is not guaranteed; check by name not index). Unless ADR 005
  channel decision routed either off-GitHub → expect `assets ==
  []` or a partial list per that decision.
- The release URL is reachable.

Show the release URL to the user.

### Step 5 — Cleanup

```bash
rm "/tmp/stencil-$TAG-notes.md"
```

## Rules

- **Bump before tag.** `project(Stencil VERSION ...)` (vst) or
  `package.json` (m4l) must be edited and committed BEFORE the tag
  exists. The editor reads the version at compile time, so a tag
  that pre-dates the bump points at a plugin binary reporting the
  OLD version.
- **Asset is target-specific.** m4l → `dist/Stencil.amxd` (frozen);
  vst → both `dist/Stencil.dmg` AND `dist/Stencil.pkg` (each signed
  + notarized + stapled). Never mix targets; never ship vst with
  only one of the two unless ADR 005 channel decision explicitly
  routes one off-GitHub.
- **Tag namespace is per-target.** `m4l-vX.Y.Z` and `vst-vX.Y.Z`
  are independent versioning lines.
- **Manual Freeze required for m4l.** Max has no CLI freeze; this
  skill does not automate it.
- **Tag once, never re-tag.** If a tag for the proposed version
  already exists, bump again rather than overwrite. Force-deleting
  a pushed tag is messy and breaks anyone who pulled it.
- **Notes via `--notes-file`, not `--notes`.** The temp-file flow
  lets the user edit before publish.
- **Halt on any pre-flight failure.** Don't release past a red
  gate.
- **Halt on any user-confirmation gate.** Steps 0 (bump commit),
  1 (version number), 2 (notes) each require explicit "ok" — don't
  proceed silently.
- **vst artifacts must be signed + notarized + stapled** before
  tagging. `build-dmg.sh` and `build-pkg.sh` both enforce this:
  they require `DEVELOPER_TEAM_ID`, run `codesign` (dmg) /
  `productsign` (pkg) on the wrapper, submit to `notarytool`, and
  staple. The AU / VST3 / CLAP bundles inside must already be
  signed + notarized via `codesign.sh` + `notarize.sh` (covered by
  `make release-vst`). Unsigned artifacts break on Gatekeeper for
  end users — don't ship.
- **Confirm GH attachment vs. off-GitHub channel.** ADR 005's paid
  distribution channel may route delivery off-GitHub (gumroad /
  itch / own site). At Step 3, confirm whether the user wants both
  the DMG and PKG attached to the GH Release, only one of them, or
  a tag-only release with both distributed separately.
