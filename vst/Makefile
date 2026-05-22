.PHONY: build configure release debug clean open kill test verify-artefacts

build: configure release

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
