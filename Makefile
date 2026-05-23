# Cross-target distribution orchestrator. Per-target build / test commands
# live in `m4l/` (pnpm workspace) and `vst/Makefile`. This root Makefile
# only chains release flows for distribution.

.PHONY: release release-m4l release-vst

release: release-m4l release-vst

# Build + bake the m4l dev .amxd, then stage an un-frozen, version-named
# copy next to it for the user to Freeze + Save As into dist/. Freeze is
# a manual step in Max (no CLI available). See ADR 006 §Release artifact
# production. dist/ is reserved for frozen artefacts; the staging copy
# stays in m4l/ (gitignored).
release-m4l:
	@if [ -z "$(VERSION)" ]; then \
	  echo "error: VERSION not set. Usage: make release-m4l VERSION=0.1.2"; \
	  exit 1; \
	fi
	cd m4l && pnpm -r build && pnpm bake
	mkdir -p dist
	cp m4l/Stencil.amxd m4l/Stencil-v$(VERSION).amxd
	@echo ""
	@echo "Next (m4l): open m4l/Stencil-v$(VERSION).amxd in Max → click the"
	@echo "            snowflake (Freeze) button in the patcher toolbar →"
	@echo "            File → Save As → save to dist/ (the default filename"
	@echo "            Stencil-v$(VERSION).amxd is already correct; just"
	@echo "            navigate to dist/). dist/ is for frozen artefacts only."

# Requires DEVELOPER_TEAM_ID env var (Apple Developer team identifier);
# notary keychain profile defaults to im9-notary (shared across im9
# plugins), override with NOTARY_PROFILE.
#
# Produces both dist/Stencil-v<version>.dmg (drag-to-install fallback)
# and dist/Stencil-v<version>.pkg (recommended installer with
# VST3/AU/CLAP per-format choices) — ship both so end users can pick.
# Version is read from vst/CMakeLists.txt by the build scripts. See
# ADR 007 §Distribution.
release-vst:
	cd vst && $(MAKE) build
	cd vst && ./scripts/codesign.sh
	cd vst && ./scripts/notarize.sh
	cd vst && ./scripts/build-dmg.sh
	cd vst && ./scripts/build-pkg.sh
