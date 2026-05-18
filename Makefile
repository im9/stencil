# Cross-target distribution orchestrator. Per-target build / test commands
# live in `m4l/` (pnpm workspace) and `vst/Makefile`. This root Makefile
# only chains release flows for distribution.

.PHONY: release release-m4l release-vst

release: release-m4l release-vst

# Build + bake the m4l dev .amxd and ensure the release dir exists.
# Freeze is a manual step in Max (no CLI available). See ADR 006
# §Release artifact production.
release-m4l:
	cd m4l && pnpm -r build && pnpm bake
	mkdir -p dist
	@echo ""
	@echo "Next (m4l): open m4l/Stencil.amxd in Max → click the snowflake"
	@echo "            (Freeze) button in the patcher toolbar → File → Save As"
	@echo "            $(CURDIR)/dist/Stencil.amxd"

# Requires DEVELOPER_TEAM_ID env var (Apple Developer team identifier);
# notary keychain profile defaults to im9-notary (shared across im9
# plugins), override with NOTARY_PROFILE.
#
# Produces both dist/Stencil.dmg (drag-to-install, custom folder OK)
# and dist/Stencil.pkg (system-wide signed installer with per-format
# choice) — ship both so end users can pick. See ADR 007 §Distribution.
release-vst:
	cd vst && $(MAKE) build
	cd vst && ./scripts/codesign.sh
	cd vst && ./scripts/notarize.sh
	cd vst && ./scripts/build-dmg.sh
	cd vst && ./scripts/build-pkg.sh
