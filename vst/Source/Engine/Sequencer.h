#pragma once

// Stencil Sequencer — transport-driven step machine and supporting utilities,
// per ADR 007 §MIDI processing. Mirrors m4l/host/host.ts semantics:
//   - bit-tap active (LSB primary trigger; density fills empty bits)
//   - mode dispatch (note / gate=midpoint / velocity=0.3+frac×0.7)
//   - triggerMode branches (auto / gate-with-held-input / seed-with-freeze)
//
// All types are pure C++17 — no JUCE. Phase 2's PluginProcessor wraps these
// into JUCE's processBlock / MidiBuffer surface.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Engine/Rng.h"
#include "Engine/Turing.h"

namespace stencil::engine
{
// ─── Enums ────────────────────────────────────────────────────────────────

enum class Subdivision
{
    Eighth = 0,           // 2 per quarter
    Sixteenth = 1,        // 4 per quarter
    ThirtySecond = 2,     // 8 per quarter
    EighthTriplet = 3,    // 3 per quarter
    SixteenthTriplet = 4  // 6 per quarter
};

double subdivisionsPerQuarter(Subdivision s);

enum class Mode
{
    Note = 0,
    Gate = 1,
    Velocity = 2
};

enum class TriggerMode
{
    Auto = 0,
    Gate = 1,
    Seed = 2
};

// ─── Params (mirrors concept.md §Parameter surface) ───────────────────────

struct SequencerParams
{
    int length = 8;                                          // 2..32
    double lock = 0.5;                                       // 0..1
    double density = 1.0;                                    // 0..1
    int rangeLo = 48;                                        // 0..127
    int rangeHi = 72;                                        // ≥ rangeLo
    Subdivision subdivision = Subdivision::Sixteenth;
    uint32_t seed = 42;                                      // u31 in concept.md
    Mode mode = Mode::Note;
    TriggerMode triggerMode = TriggerMode::Auto;
    int inputChannel = 0;                                    // 0 = omni
    int outputVelocity = 100;                                // 1..127
    double outputGate = 0.5;                                 // 0..1, fraction of step
    int outputChannel = 1;                                   // 1..16
};

// ─── Boundary detection (PPQ → step events) ────────────────────────────────

struct StepBoundary
{
    int sampleOffset;  // 0..blockSamples
    int stepIndex;     // monotonic from transport zero
};

// Detect subdivision boundaries within [startPpq, startPpq + blockPpqDuration).
// Returns boundaries in increasing sampleOffset order. Empty when blockSamples
// ≤ 0 or bpm ≤ 0 or sampleRate ≤ 0.
std::vector<StepBoundary> detectBoundaries(double startPpq,
                                            double bpm,
                                            double sampleRate,
                                            int blockSamples,
                                            Subdivision subdivision);

// ─── StepEvent (MIDI event descriptor for hung-note flush + panic) ─────────

struct StepEvent
{
    enum class Kind
    {
        NoteOn,
        NoteOff,
        AllNotesOff
    };
    Kind kind;
    int sampleOffset = 0;     // within block
    int note = 0;             // 0..127, ignored for AllNotesOff
    int velocity = 0;         // 0..127, NoteOn only
    int channel = 1;          // 1..16; 0 means "all channels" for AllNotesOff
    double delayInSteps = 0.0; // NoteOff only: delay as fraction of step
};

// ─── Hung-note tracker ─────────────────────────────────────────────────────

class NotesOn
{
public:
    bool add(int pitch, int channel);     // false if duplicate
    bool remove(int pitch, int channel);  // true if found
    std::vector<StepEvent> flushAsNoteOff(int sampleOffset);
    bool empty() const { return active_.empty(); }
    std::size_t size() const { return active_.size(); }

private:
    std::vector<std::pair<int, int>> active_;  // (pitch, channel)
};

// ─── Sequencer (per-step state machine) ────────────────────────────────────

struct StepOutput
{
    bool active;
    int note;
    int velocity;
    int channel;
};

class Sequencer
{
public:
    Sequencer();
    explicit Sequencer(const SequencerParams& p);

    void setParams(const SequencerParams& p);
    const SequencerParams& getParams() const { return params_; }

    // Re-seed rng + re-create register from current seed/length. Clears
    // input state and position. Called on transport start and on
    // length/seed parameter changes.
    void reset();

    // External MIDI input. inputChannel filtering happens here.
    void onInputNoteOn(int pitch, int channel);
    void onInputNoteOff(int pitch, int channel);

    // Process one step boundary. Mutates register/rng/position per ADR 007
    // §MIDI processing. Returns the step's output decision (active + note +
    // velocity + channel). Emit timing is the caller's responsibility.
    StepOutput processStep();

    // Transport stop: clear input state, position; register preserved
    // (matches m4l so resume continues the same loop). Returns no events;
    // caller flushes hung notes via NotesOn separately.
    void onTransportStop();

    // Inspection (read-only)
    RegisterBits getRegister() const { return register_; }
    int getPosition() const { return position_; }
    bool isSeedActivated() const { return seedActivated_; }
    std::size_t heldInputCount() const { return heldInputs_.size(); }

private:
    SequencerParams params_;
    RegisterBits register_;
    RngState rng_;
    int position_;
    std::vector<std::pair<int, int>> heldInputs_;  // (pitch, channel) for gate mode
    bool seedActivated_;

    bool channelMatches(int channel) const;
    void recreateRegister();
};

}  // namespace stencil::engine
