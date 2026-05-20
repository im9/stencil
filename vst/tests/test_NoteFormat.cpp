// Pin the editor's note-label convention to Yamaha / Ableton (MIDI 60 =
// "C3"). Without this pin, a misread of the `n / 12 - K` octave offset
// silently regresses every visible pitch label in the editor (ring
// center, Output NOTE row) by an octave -- the same defect that the
// 2026-05-20 fix corrected from `n / 12 - 1` (scientific, C4 = 60).
//
// Cross-target alignment lives on the same convention: m4l/host/ui/
// rangeSlider.logic.ts `midiToNoteName`. Any change here that drifts
// from Ableton's piano-roll labels needs to land on both sides in the
// same change.

#include <catch2/catch_test_macros.hpp>

#include "Editor/NoteFormat.h"

using stencil::editor::noteLabel;

TEST_CASE("noteLabel: middle C is C3 (Yamaha / Ableton convention)", "[noteFormat]")
{
    // Derivation: Ableton Live's piano roll labels MIDI 60 as "C3", and
    // Logic / Bitwig / Reaper / Studio One / Cubase default to the same.
    // The VST currently targets Logic + Bitwig as primary hosts (ADR 005
    // §VST host matrix) — both use this convention out of the box.
    REQUIRE(noteLabel(60) == juce::String("C3"));
}

TEST_CASE("noteLabel: octave boundaries every 12 semitones", "[noteFormat]")
{
    // Derivation: each octave starts at the next multiple of 12 above the
    // previous C. From middle C (MIDI 60 = C3), MIDI 0 sits five octaves
    // below at C-2, and MIDI 127 (= 10*12 + 7) lands at G8.
    REQUIRE(noteLabel(0)   == juce::String("C-2"));
    REQUIRE(noteLabel(12)  == juce::String("C-1"));
    REQUIRE(noteLabel(24)  == juce::String("C0"));
    REQUIRE(noteLabel(36)  == juce::String("C1"));
    REQUIRE(noteLabel(48)  == juce::String("C2"));
    REQUIRE(noteLabel(72)  == juce::String("C4"));
    REQUIRE(noteLabel(84)  == juce::String("C5"));
    REQUIRE(noteLabel(120) == juce::String("C8"));
}

TEST_CASE("noteLabel: sharps written with '#' (no flat enharmonics)", "[noteFormat]")
{
    // Derivation: the pitch-class table in NoteFormat.h is the canonical
    // sharp form; any future "Db for C#" substitution is a deliberate
    // policy change and must trip this assertion to surface intent.
    REQUIRE(noteLabel(61) == juce::String("C#3"));
    REQUIRE(noteLabel(66) == juce::String("F#3"));
    REQUIRE(noteLabel(70) == juce::String("A#3"));
}

TEST_CASE("noteLabel: full pitch-class set within one octave", "[noteFormat]")
{
    // Derivation: the chromatic octave starting at middle C cycles through
    // the 12-name table in order. Asserts both ordering and the table's
    // content in a single sweep.
    const char* expected[] = {
        "C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3"
    };
    for (int i = 0; i < 12; ++i)
        REQUIRE(noteLabel(60 + i) == juce::String(expected[i]));
}

TEST_CASE("noteLabel: MIDI 127 is G8 (top of range)", "[noteFormat]")
{
    // Derivation: MIDI standard maxes at 127. 127 = 10*12 + 7 -> pitch
    // class index 7 ("G"), octave = 127/12 - 2 = 10 - 2 = 8.
    REQUIRE(noteLabel(127) == juce::String("G8"));
}

TEST_CASE("noteLabel: out-of-range input clamps to MIN/MAX", "[noteFormat]")
{
    // Derivation: the function clamps to [0, 127] before computing so a
    // stale APVTS read or a bogus host-side value still renders as a
    // visible-but-bounded label instead of garbage text from indexing
    // outside the pitch-class table.
    REQUIRE(noteLabel(-1)  == juce::String("C-2"));
    REQUIRE(noteLabel(200) == juce::String("G8"));
}
