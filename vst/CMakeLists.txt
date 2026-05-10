cmake_minimum_required(VERSION 3.15)
project(Stencil VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

# --- Test dependencies (ADR 007 Phase 1) --------------------------------------
# Catch2 v3 + nlohmann/json v3 fetched at configure time. Both are header-only-
# friendly via FetchContent and avoid an out-of-tree submodule. nlohmann/json's
# JSON_BuildTests is forced off so we don't compile its own test suite.
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(Catch2 nlohmann_json)

# --- Engine static library (ADR 007 §Source layout) ---------------------------
# Pure C++17. NO juce_* link or include — this is the test-link / future-port
# boundary. Both the plugin (Stencil) and the test binary (stencil_tests) link
# this library; nothing in here may use JUCE.
add_library(stencil_engine STATIC
    Source/Engine/Rng.cpp
    Source/Engine/Rng.h
    Source/Engine/Turing.cpp
    Source/Engine/Turing.h
    Source/Engine/Sequencer.cpp
    Source/Engine/Sequencer.h
)
target_include_directories(stencil_engine PUBLIC Source)
target_compile_features(stencil_engine PUBLIC cxx_std_17)

# --- Plugin -------------------------------------------------------------------
# Phase 1 leaves PluginProcessor / PluginEditor at their existing scaffold paths
# (Source/PluginProcessor.{cpp,h} / Source/PluginEditor.{cpp,h}); Phase 2
# refactors them into Source/Plugin/ and Source/Editor/ alongside engine wiring.
juce_add_plugin(Stencil
    COMPANY_NAME "im9"
    BUNDLE_ID "com.im9.stencil"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    PLUGIN_MANUFACTURER_CODE "Im9x"
    PLUGIN_CODE "Stnl"
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Stencil"
)

target_sources(Stencil PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_link_libraries(Stencil PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_plugin_client
)

target_compile_definitions(Stencil PRIVATE
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

juce_generate_juce_header(Stencil)

# --- Tests --------------------------------------------------------------------
# Custom main (tests/main.cpp) so JUCE init/shutdown can be added later
# (Phase 3 introduces Editor tests that require juce::initialiseJuce_GUI()).
# In Phase 1 the engine alone needs no JUCE; main.cpp just runs Catch2.
# Test vector paths are passed as compile defs so test_*.cpp can include them
# regardless of the runtime working directory.
add_executable(stencil_tests
    tests/main.cpp
    tests/test_Rng.cpp
    tests/test_Turing.cpp
    tests/test_Sequencer.cpp
)
target_link_libraries(stencil_tests PRIVATE
    stencil_engine
    Catch2::Catch2
    nlohmann_json::nlohmann_json
)
target_compile_features(stencil_tests PRIVATE cxx_std_17)
target_compile_definitions(stencil_tests PRIVATE
    STENCIL_RNG_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../docs/ai/rng-test-vectors.json"
    STENCIL_TURING_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../docs/ai/turing-test-vectors.json"
)
