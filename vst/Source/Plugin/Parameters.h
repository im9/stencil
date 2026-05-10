// APVTS parameter IDs and layout factory (ADR 007 Phase 2).
//
// IDs, ranges, defaults, and choice strings mirror concept.md §Parameter
// surface and m4l/host/host.ts (DEFAULT_PARAMS). The Choice index is what
// APVTS persists, so the order of *Choices arrays is the wire format —
// NEVER reorder; only append.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Sequencer.h"

namespace stencil::plugin
{
namespace pid
{
inline constexpr const char* length         = "length";
inline constexpr const char* lock           = "lock";
inline constexpr const char* density        = "density";
inline constexpr const char* rangeLo        = "rangeLo";
inline constexpr const char* rangeHi        = "rangeHi";
inline constexpr const char* subdivision    = "subdivision";
inline constexpr const char* seed           = "seed";
inline constexpr const char* mode           = "mode";
inline constexpr const char* triggerMode    = "triggerMode";
inline constexpr const char* inputChannel   = "inputChannel";
inline constexpr const char* outputVelocity = "outputVelocity";
inline constexpr const char* outputGate     = "outputGate";
inline constexpr const char* outputChannel  = "outputChannel";
}  // namespace pid

// Choice strings — ordered to match `engine::Subdivision` /
// `engine::Mode` / `engine::TriggerMode` enum values one-to-one.
// readParams() relies on this ordering; reordering would require a
// migration shim in setStateInformation.
inline const juce::StringArray subdivisionChoices { "8th", "16th", "32nd", "8T", "16T" };
inline const juce::StringArray modeChoices        { "note", "gate", "velocity" };
inline const juce::StringArray triggerModeChoices { "auto", "gate", "seed" };

// Defaults centralized for the round-trip test ("fresh instance has
// these exact values"). Match concept.md §Parameter surface.
namespace defaults
{
inline constexpr int   length         = 8;
inline constexpr float lock           = 0.5f;
inline constexpr float density        = 1.0f;
inline constexpr int   rangeLo        = 48;
inline constexpr int   rangeHi        = 72;
inline constexpr int   subdivisionIdx = 1;     // "16th"
inline constexpr int   seed           = 42;
inline constexpr int   modeIdx        = 0;     // "note"
inline constexpr int   triggerModeIdx = 0;     // "auto"
inline constexpr int   inputChannel   = 0;     // omni
inline constexpr int   outputVelocity = 100;
inline constexpr float outputGate     = 0.5f;
inline constexpr int   outputChannel  = 1;
}  // namespace defaults

// APVTS layout factory. Called once from the processor constructor.
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

// Snapshot APVTS values into engine-typed SequencerParams. Reads happen
// via getRawParameterValue (atomic, audio-thread safe). Range clamp
// (rangeLo ≤ rangeHi) is applied here so the engine sees a valid range
// even if APVTS values briefly drift apart between the listener fire and
// processBlock entry.
engine::SequencerParams readParams(const juce::AudioProcessorValueTreeState& apvts);

}  // namespace stencil::plugin
