// MIDI -> note name string for editor labels (ring center text, Output
// panel NOTE row, anything that displays a sounding-or-range pitch).
//
// Yamaha / Ableton convention: MIDI 60 = "C3". Most DAWs (Ableton Live,
// Logic Pro, Bitwig, Reaper, Studio One, Cubase) label MIDI 60 as C3 in
// their piano-roll views. The earlier scientific-notation form
// (`n / 12 - 1`, C4 = 60) was a port bug: it showed the editor's pitch
// labels one octave above what every host's own clip view labels them
// as, which is confusing on every host this plugin targets. Test
// vst/tests/test_NoteFormat.cpp pins this convention.
//
// Cross-target: m4l/host/ui/rangeSlider.logic.ts `midiToNoteName` uses
// the same formula. Keep these two implementations in sync; a one-line
// drift here is silent regression in the visible UI.

#pragma once

#include <algorithm>
#include <juce_core/juce_core.h>

namespace stencil::editor {

// MIDI 0   -> "C-2"
// MIDI 60  -> "C3"   (middle C, Ableton / Logic piano-roll default)
// MIDI 127 -> "G8"
inline juce::String noteLabel(int midiNote)
{
    static constexpr const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int n = std::clamp(midiNote, 0, 127);
    return juce::String(kNoteNames[n % 12]) + juce::String(n / 12 - 2);
}

}  // namespace stencil::editor
