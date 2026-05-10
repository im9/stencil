cmake_minimum_required(VERSION 3.15)
project(Stencil VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

# --- Test dependencies (ADR 007 Phase 1) --------------------------------------
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
# Pure C++17. NO juce_* link or include — the test-link / future-port
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

# --- Plugin core static library (ADR 007 Phase 2) -----------------------------
# AudioProcessor + APVTS + Editor sources, separated from the JUCE plugin
# bundle so the test binary can link them without pulling in
# juce_audio_plugin_client (which provides the AU/VST3/CLAP wrappers and
# is plugin-only). Matches oedipa's oedipa_plugin_core pattern.
#
# Phase 2 leaves PluginEditor.{cpp,h} at Source/ as a placeholder; Phase 3
# reorganizes into Source/Editor/ alongside the inboil TuringSheet port.
add_library(stencil_plugin_core STATIC
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/Parameters.cpp
    Source/Plugin/Parameters.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)
target_include_directories(stencil_plugin_core PUBLIC Source)
target_link_libraries(stencil_plugin_core PUBLIC
    stencil_engine
    juce::juce_audio_basics
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
)
target_compile_definitions(stencil_plugin_core PUBLIC
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_STANDALONE_APPLICATION=0
)

# --- Plugin (AU / VST3 / Standalone wrappers) ---------------------------------
# CLAP added in Phase 4 via clap-juce-extensions.
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

# The plugin target itself owns no sources; everything lives in
# stencil_plugin_core. juce_add_plugin still requires at least one source
# for the wrapper / SharedCode target generation, so we point at a tiny
# stub — the createPluginFilter() entry point lives in plugin_core.
target_sources(Stencil PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginProcessor.h
    Source/Plugin/Parameters.cpp
    Source/Plugin/Parameters.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)
target_include_directories(Stencil PRIVATE Source)

target_link_libraries(Stencil PRIVATE
    stencil_engine
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

# --- Tests --------------------------------------------------------------------
# Custom main (tests/main.cpp) scopes JUCE init/shutdown around Catch2
# Session().run() so AudioProcessor / MessageManager lifetimes stay valid
# for plugin tests (Phase 2+).
add_executable(stencil_tests
    tests/main.cpp
    tests/test_Rng.cpp
    tests/test_Turing.cpp
    tests/test_Sequencer.cpp
    tests/test_Plugin.cpp
)
target_link_libraries(stencil_tests PRIVATE
    stencil_plugin_core   # transitively brings stencil_engine + JUCE modules
    Catch2::Catch2
    nlohmann_json::nlohmann_json
)
target_compile_features(stencil_tests PRIVATE cxx_std_17)
target_compile_definitions(stencil_tests PRIVATE
    STENCIL_RNG_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../docs/ai/rng-test-vectors.json"
    STENCIL_TURING_VECTORS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../docs/ai/turing-test-vectors.json"
)
