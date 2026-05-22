.PHONY: build configure release release-vst debug clean open kill test verify-artefacts

build: configure release

# Full release pipeline: build → codesign → notarize+staple bundles →
# build+sign+staple dmg → build+sign+staple pkg. Both dist/Stencil.dmg
# and dist/Stencil.pkg are produced in lockstep so the paid Polar
# upload can attach either artifact. Requires DEVELOPER_TEAM_ID env
# var and the `im9-notary` keychain profile + Developer ID
# Application / Developer ID Installer certs in the login keychain.
# Notarization waits on Apple's service so this takes several minutes.
release-vst: build verify-artefacts
	./scripts/codesign.sh
	./scripts/notarize.sh
	./scripts/build-dmg.sh
	./scripts/build-pkg.sh

configure:
	cmake -B build -DCMAKE_BUILD_TYPE=Release

release:
	cmake --build build --config Release -j8

debug:
	cmake -B build -DCMAKE_BUILD_TYPE=Debug
	cmake --build build --config Debug -j8

test: configure
	cmake --build build --target stencil_tests -j8
	./build/stencil_tests

clean:
	rm -rf build

open:
	open build/Stencil_artefacts/Release/Standalone/Stencil.app

kill:
	@pkill -x Stencil 2>/dev/null || true

verify-artefacts:
	./scripts/check-artefacts.sh build Release
