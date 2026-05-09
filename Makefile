.PHONY: release release-m4l

# Default release target — currently m4l only. vst/ release builds run
# from vst/Makefile (different semantics: build type, not ship artifact).
release: release-m4l

# Build + bake the m4l dev .amxd and ensure the release dir exists.
# Freeze is a manual step in Max (no CLI available). See ADR 006
# §Release artifact production.
release-m4l:
	cd m4l && pnpm -r build && pnpm bake
	mkdir -p dist
	@echo ""
	@echo "Next: open m4l/Stencil.amxd in Max → click the snowflake"
	@echo "      (Freeze) button in the patcher toolbar → File → Save As"
	@echo "      $(CURDIR)/dist/Stencil.amxd"
