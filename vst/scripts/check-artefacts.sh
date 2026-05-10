#!/usr/bin/env bash
# Verify the build produced every expected plug-in format.
#
# ADR 007 Phase 4 — VST3 + AU + CLAP are the shipped formats; Standalone
# is dev-only but produced by the same build. Exits non-zero on the first
# missing artefact set, listing all missing paths.
#
# Usage: ./scripts/check-artefacts.sh [BUILD_DIR] [CONFIG]
# Defaults: BUILD_DIR=build CONFIG=Release

set -eu

BUILD_DIR="${1:-build}"
CONFIG="${2:-Release}"
ARTEFACT_ROOT="${BUILD_DIR}/Stencil_artefacts/${CONFIG}"

EXPECTED=(
    "${ARTEFACT_ROOT}/VST3/Stencil.vst3"
    "${ARTEFACT_ROOT}/AU/Stencil.component"
    "${ARTEFACT_ROOT}/Standalone/Stencil.app"
    "${ARTEFACT_ROOT}/CLAP/Stencil.clap"
)

missing=0
for path in "${EXPECTED[@]}"; do
    if [ -e "$path" ]; then
        printf 'ok       %s\n' "$path"
    else
        printf 'MISSING  %s\n' "$path"
        missing=$((missing + 1))
    fi
done

if [ "$missing" -gt 0 ]; then
    printf '\n%d artefact(s) missing under %s\n' "$missing" "$ARTEFACT_ROOT" >&2
    exit 1
fi
