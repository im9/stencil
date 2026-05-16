// Sequencer tests per ADR 007 Phase 1 + 2026-05-15 §Output revision:
//   - subdivisionsPerQuarter / detectBoundaries (PPQ → step events)
//   - Sequencer single output dispatch (pitch = mapToNote, velocity =
//     outputVelocity; no per-mode branch — see 2026-05-15 Status block)
//   - Sequencer triggerMode branches (Auto / Gate-no-input / Gate-with-input
//     / Seed-pre-activation / Seed-post-activation)
//   - Bit-tap active semantic (LSB primary trigger, density fills empties)
//   - Transport stop clears input state

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>

#include "Engine/Rng.h"
#include "Engine/Sequencer.h"
#include "Engine/Turing.h"

using stencil::engine::createRegister;
using stencil::engine::detectBoundaries;
using stencil::engine::RegisterBits;
using stencil::engine::RngState;
using stencil::engine::seedRng;
using stencil::engine::Sequencer;
using stencil::engine::SequencerParams;
using stencil::engine::StepBoundary;
using stencil::engine::StepOutput;
using stencil::engine::Subdivision;
using stencil::engine::subdivisionsPerQuarter;
using stencil::engine::TriggerMode;

// ─── subdivisionsPerQuarter ────────────────────────────────────────────────

TEST_CASE("subdivisionsPerQuarter values", "[sequencer][subdivision]")
{
    // Eighth = 2 (two 8th-notes per quarter); 16th = 4; 32nd = 8;
    // 8T = 3 (eighth-note triplet, three notes per quarter);
    // 16T = 6 (sixteenth-note triplet, six notes per quarter).
    CHECK(subdivisionsPerQuarter(Subdivision::Eighth) == 2.0);
    CHECK(subdivisionsPerQuarter(Subdivision::Sixteenth) == 4.0);
    CHECK(subdivisionsPerQuarter(Subdivision::ThirtySecond) == 8.0);
    CHECK(subdivisionsPerQuarter(Subdivision::EighthTriplet) == 3.0);
    CHECK(subdivisionsPerQuarter(Subdivision::SixteenthTriplet) == 6.0);
}

// ─── detectBoundaries ──────────────────────────────────────────────────────

TEST_CASE("detectBoundaries empty for pathological inputs", "[sequencer][boundaries]")
{
    // blockSamples ≤ 0 → no boundaries (degenerate block, irrespective of bpm).
    CHECK(detectBoundaries(0.0, 120.0, 48000.0, 0, Subdivision::Sixteenth).empty());
    // bpm ≤ 0 → no boundaries (transport not running at meaningful tempo).
    CHECK(detectBoundaries(0.0, 0.0, 48000.0, 256, Subdivision::Sixteenth).empty());
    // sampleRate ≤ 0 → no boundaries (no time base).
    CHECK(detectBoundaries(0.0, 120.0, 0.0, 256, Subdivision::Sixteenth).empty());
}

TEST_CASE("detectBoundaries 16th @ 120bpm @ 48kHz from PPQ 0", "[sequencer][boundaries]")
{
    // 16th = 4 per quarter. At 120bpm, one quarter = 0.5s → 16th = 0.125s.
    // 0.125s × 48000 sr = 6000 samples per 16th. A 6000-sample block from
    // PPQ 0 should give exactly one boundary (step 0 at sample 0); the next
    // boundary at sample 6000 is the start of the *next* block (half-open).
    const auto b = detectBoundaries(0.0, 120.0, 48000.0, 6000, Subdivision::Sixteenth);
    REQUIRE(b.size() == 1);
    CHECK(b[0].sampleOffset == 0);
    CHECK(b[0].stepIndex == 0);
}

TEST_CASE("detectBoundaries 16th @ 120bpm — 24000-sample block holds 4 steps",
          "[sequencer][boundaries]")
{
    // 24000 samples = 0.5s = one quarter at 120bpm. One quarter at 16th =
    // 4 boundaries (step 0..3), each 6000 samples apart.
    const auto b = detectBoundaries(0.0, 120.0, 48000.0, 24000, Subdivision::Sixteenth);
    REQUIRE(b.size() == 4);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(b[i].stepIndex == i);
        CHECK(b[i].sampleOffset == i * 6000);
    }
}

TEST_CASE("detectBoundaries respects half-open [startPpq, endPpq) interval",
          "[sequencer][boundaries]")
{
    // Block [0.25, 0.5) PPQ at 120bpm, 16th subdivision: step 1 at 0.25
    // (stepPpq=0.25) is on the lower bound and IS included; step 2 at 0.5
    // is on the upper bound and is NOT included.
    const double samplesPerPpq = 48000.0 * 60.0 / 120.0;        // 24000 sa/quarter
    const double blockPpqStart = 0.25;
    const int blockSamples = static_cast<int>(samplesPerPpq * 0.25);  // 6000
    const auto b = detectBoundaries(blockPpqStart, 120.0, 48000.0, blockSamples,
                                     Subdivision::Sixteenth);
    REQUIRE(b.size() == 1);
    CHECK(b[0].stepIndex == 1);
    CHECK(b[0].sampleOffset == 0);
}

TEST_CASE("detectBoundaries 8th @ 60bpm — half the rate", "[sequencer][boundaries]")
{
    // 60bpm: one quarter = 1.0s = 48000 samples. 8th = 2 per quarter →
    // 24000 samples per 8th. A 24000-sample block from PPQ 0 should hold
    // exactly one boundary (step 0).
    const auto b = detectBoundaries(0.0, 60.0, 48000.0, 24000, Subdivision::Eighth);
    REQUIRE(b.size() == 1);
    CHECK(b[0].stepIndex == 0);
    CHECK(b[0].sampleOffset == 0);
}

TEST_CASE("detectBoundaries 8T @ 120bpm — three steps per quarter",
          "[sequencer][boundaries]")
{
    // 120bpm: quarter = 24000 samples. 8T = 3/quarter → 8000 samples each.
    const auto b = detectBoundaries(0.0, 120.0, 48000.0, 24000,
                                     Subdivision::EighthTriplet);
    REQUIRE(b.size() == 3);
    CHECK(b[0].sampleOffset == 0);
    CHECK(b[1].sampleOffset == 8000);
    CHECK(b[2].sampleOffset == 16000);
}

// ─── Sequencer — initial state & reset ─────────────────────────────────────

TEST_CASE("Sequencer constructs deterministically from seed+length",
          "[sequencer][reset]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;

    Sequencer seq(p);
    // Matches engine createRegister(8, seedRng(1)).
    RngState refRng = seedRng(1);
    const RegisterBits expected = createRegister(8, refRng);
    CHECK(seq.getRegister() == expected);
    CHECK(seq.getPosition() == 0);
    CHECK_FALSE(seq.isSeedActivated());
    CHECK(seq.heldInputCount() == 0);
}

TEST_CASE("Sequencer reset re-creates register from seed/length",
          "[sequencer][reset]")
{
    SequencerParams p;
    p.seed = 42;
    p.length = 8;

    Sequencer seq(p);
    const RegisterBits initial = seq.getRegister();
    // Run several steps to mutate state.
    for (int i = 0; i < 5; ++i)
        seq.processStep();
    CHECK(seq.getPosition() == 5);

    seq.reset();
    CHECK(seq.getRegister() == initial);
    CHECK(seq.getPosition() == 0);
}

// ─── Sequencer — single output dispatch (ADR 007 §Output, 2026-05-15) ─────

TEST_CASE("Sequencer single dispatch: pitch from reg/range, velocity = outputVelocity",
          "[sequencer][output]")
{
    // Justification: ADR 007 §Output (2026-05-15) — every active step
    // emits one noteOn with `pitch = mapToNote(reg, range)` and
    // `velocity = outputVelocity` (constant). The earlier per-mode
    // dispatch (note/gate/velocity) was removed because a MIDI note
    // event carries pitch + velocity + gate simultaneously, not as
    // mutually exclusive branches.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;       // freeze register
    p.density = 1.0;    // always active so we can observe output
    p.rangeLo = 60;
    p.rangeHi = 72;
    p.outputVelocity = 100;

    Sequencer seq(p);
    const RegisterBits reg = seq.getRegister();
    const auto frac = stencil::engine::registerToFraction(reg, 8);
    const int expectedPitch = stencil::engine::mapToNote(frac.num, frac.den, 60, 72);

    const StepOutput o = seq.processStep();
    CHECK(o.active);
    CHECK(o.note == expectedPitch);
    CHECK(o.velocity == 100);
    CHECK(o.channel == p.outputChannel);
}

TEST_CASE("Sequencer single dispatch: rangeLo == rangeHi pins pitch (replaces old gate mode)",
          "[sequencer][output]")
{
    // Justification: the deprecated `gate` mode emitted pitch =
    // floor((lo + hi) / 2). With single dispatch, the same musical
    // effect (fixed-pitch rhythmic articulation) is recoverable by
    // setting `rangeLo == rangeHi` — mapToNote always returns that
    // single value regardless of register state. Pins the design
    // claim made in ADR 007 §Output that gate mode is recoverable
    // via range collapse.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;
    p.density = 1.0;
    p.rangeLo = 60;
    p.rangeHi = 60;
    p.outputVelocity = 100;

    Sequencer seq(p);
    const StepOutput o = seq.processStep();
    CHECK(o.active);
    CHECK(o.note == 60);
    CHECK(o.velocity == 100);
}

TEST_CASE("Sequencer single dispatch: outputVelocity passes through verbatim",
          "[sequencer][output]")
{
    // Justification: 2026-05-15 removed the `velocity` mode's
    // `(0.3 + frac × 0.7) × outputVelocity` scaling. Velocity is now
    // the slider value directly, with no register-derived modulation.
    // Spot-check at three slider values to pin the verbatim mapping.
    for (int v : {1, 64, 127}) {
        SequencerParams p;
        p.seed = 1;
        p.length = 8;
        p.lock = 1.0;
        p.density = 1.0;
        p.rangeLo = 60;
        p.rangeHi = 72;
        p.outputVelocity = v;
        Sequencer seq(p);
        const StepOutput o = seq.processStep();
        REQUIRE(o.active);
        CHECK(o.velocity == v);
    }
}

// ─── Sequencer — bit-tap active semantic ──────────────────────────────────

TEST_CASE("Sequencer bit-tap: density=0 + LSB=1 still fires",
          "[sequencer][bit_tap]")
{
    // Initial register for seed=0 length=2 is 3 (0b11) per register_init
    // vectors — LSB is 1. With density=0, pure-density semantics would
    // be active=false; bit-tap semantics give active=true.
    SequencerParams p;
    p.seed = 0;
    p.length = 2;
    p.lock = 1.0;
    p.density = 0.0;  // density never fires alone
    p.rangeLo = 60;
    p.rangeHi = 72;

    Sequencer seq(p);
    REQUIRE((seq.getRegister() & 1u) == 1u);  // sanity: LSB is 1 for this seed/length
    const StepOutput o = seq.processStep();
    // density=0 but bit0=1 → bit-tap active
    CHECK(o.active);
}

TEST_CASE("Sequencer bit-tap: density=0 + LSB=0 → silent",
          "[sequencer][bit_tap]")
{
    // Use a register with LSB=0. We rely on shiftAndForce-like manipulation
    // via reset+step pattern: with seed=0 length=2, register=3 has LSB=1, so
    // we step once with lock=1 to get register>>1 = 1, then again to get 0…
    // Simpler: use seed=1 length=8 (register has LSB=1) and step once with
    // density=0+lock=1 to push the LSB out — then test the *next* step.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;
    p.density = 1.0;  // first step active so we can advance
    p.rangeLo = 60;
    p.rangeHi = 72;

    Sequencer seq(p);
    // Find a step where, post-shift, the register's LSB is 0.
    // Iterate a few steps until we observe LSB == 0 at processStep entry.
    for (int i = 0; i < 32; ++i)
    {
        if ((seq.getRegister() & 1u) == 0u)
        {
            // Now flip density to 0 for this single observation.
            auto p2 = seq.getParams();
            p2.density = 0.0;
            seq.setParams(p2);
            const StepOutput o = seq.processStep();
            CHECK_FALSE(o.active);
            return;
        }
        seq.processStep();
    }
    FAIL("No register state with LSB=0 encountered in 32 steps; test setup bug");
}

// ─── Sequencer — triggerMode branches ─────────────────────────────────────

TEST_CASE("Sequencer triggerMode=Auto: every step processes",
          "[sequencer][trigger_mode][auto]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;
    p.density = 1.0;
    p.rangeLo = 60;
    p.rangeHi = 72;
    p.triggerMode = TriggerMode::Auto;

    Sequencer seq(p);
    const int posBefore = seq.getPosition();
    seq.processStep();
    seq.processStep();
    seq.processStep();
    CHECK(seq.getPosition() == posBefore + 3);
}

TEST_CASE("Sequencer triggerMode=Gate, no input: silent + register/rng frozen",
          "[sequencer][trigger_mode][gate]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 0.5;            // would normally mutate register
    p.density = 1.0;
    p.rangeLo = 60;
    p.rangeHi = 72;
    p.triggerMode = TriggerMode::Gate;

    Sequencer seq(p);
    const RegisterBits regBefore = seq.getRegister();
    const int posBefore = seq.getPosition();

    const StepOutput o = seq.processStep();
    CHECK_FALSE(o.active);
    CHECK(seq.getRegister() == regBefore);  // frozen
    CHECK(seq.getPosition() == posBefore);  // frozen
}

TEST_CASE("Sequencer triggerMode=Gate, with held input: processes normally",
          "[sequencer][trigger_mode][gate]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;            // pin register so bit-tap is predictable
    p.density = 1.0;
    p.rangeLo = 60;
    p.rangeHi = 72;
    p.triggerMode = TriggerMode::Gate;
    p.inputChannel = 0;       // omni

    Sequencer seq(p);
    seq.onInputNoteOn(60, 1);
    CHECK(seq.heldInputCount() == 1);
    const int posBefore = seq.getPosition();
    const StepOutput o = seq.processStep();
    CHECK(o.active);                            // normal step path
    CHECK(seq.getPosition() == posBefore + 1);  // not frozen

    seq.onInputNoteOff(60, 1);
    CHECK(seq.heldInputCount() == 0);
    // Now back to silent.
    const StepOutput o2 = seq.processStep();
    CHECK_FALSE(o2.active);
}

TEST_CASE("Sequencer triggerMode=Seed pre-activation: behaves like Auto",
          "[sequencer][trigger_mode][seed]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;
    p.density = 1.0;
    p.rangeLo = 60;
    p.rangeHi = 72;
    p.triggerMode = TriggerMode::Seed;

    Sequencer seq(p);
    CHECK_FALSE(seq.isSeedActivated());
    const int posBefore = seq.getPosition();
    seq.processStep();
    CHECK(seq.getPosition() == posBefore + 1);  // not frozen pre-activation
    CHECK_FALSE(seq.isSeedActivated());
}

TEST_CASE("Sequencer triggerMode=Seed input shiftAndForce + activation",
          "[sequencer][trigger_mode][seed]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 1.0;
    p.triggerMode = TriggerMode::Seed;

    Sequencer seq(p);
    const RegisterBits regBefore = seq.getRegister();

    seq.onInputNoteOn(64, 1);
    CHECK(seq.isSeedActivated());
    // shiftAndForce(reg, length, 1) — the register MSB-bit should be 1 after.
    // (Direct re-derivation against engine helper.)
    const RegisterBits expected =
        stencil::engine::shiftAndForce(regBefore, 8, 1);
    CHECK(seq.getRegister() == expected);

    const RegisterBits regAfterOn = seq.getRegister();
    seq.onInputNoteOff(64, 1);
    const RegisterBits expectedOff =
        stencil::engine::shiftAndForce(regAfterOn, 8, 0);
    CHECK(seq.getRegister() == expectedOff);
}

TEST_CASE("Sequencer triggerMode=Seed post-activation: register frozen on step",
          "[sequencer][trigger_mode][seed]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 0.0;            // would normally flip every bit on every step
    p.density = 1.0;
    p.triggerMode = TriggerMode::Seed;

    Sequencer seq(p);
    seq.onInputNoteOn(60, 1);  // activate
    REQUIRE(seq.isSeedActivated());

    const RegisterBits regBefore = seq.getRegister();
    seq.processStep();
    // Register MUST be unchanged (input drives it; transport step does not).
    CHECK(seq.getRegister() == regBefore);
    // Position still advances so caller can drive UI / clocking.
    CHECK(seq.getPosition() == 1);
}

// ─── Sequencer — input channel filtering ──────────────────────────────────

TEST_CASE("Sequencer inputChannel filters non-matching channels",
          "[sequencer][input_channel]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.triggerMode = TriggerMode::Seed;
    p.inputChannel = 5;

    Sequencer seq(p);
    seq.onInputNoteOn(60, 3);  // wrong channel
    CHECK_FALSE(seq.isSeedActivated());
    seq.onInputNoteOn(60, 5);  // matches
    CHECK(seq.isSeedActivated());
}

TEST_CASE("Sequencer inputChannel=0 means omni",
          "[sequencer][input_channel]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.triggerMode = TriggerMode::Gate;
    p.inputChannel = 0;        // omni

    Sequencer seq(p);
    seq.onInputNoteOn(60, 7);
    CHECK(seq.heldInputCount() == 1);
    seq.onInputNoteOn(64, 13);
    CHECK(seq.heldInputCount() == 2);
}

// ─── Sequencer — transport stop / panic ───────────────────────────────────

TEST_CASE("Sequencer onTransportStop clears input state and position",
          "[sequencer][transport]")
{
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.triggerMode = TriggerMode::Gate;

    Sequencer seq(p);
    seq.onInputNoteOn(60, 1);
    seq.processStep();
    seq.processStep();
    CHECK(seq.getPosition() > 0);
    CHECK(seq.heldInputCount() == 1);

    seq.onTransportStop();
    CHECK(seq.getPosition() == 0);
    CHECK(seq.heldInputCount() == 0);
    CHECK_FALSE(seq.isSeedActivated());
}

// ─── Sequencer — lastEmittedRegister (pre-shift snapshot) ─────────────────

TEST_CASE("Sequencer.lastEmittedRegister captures register state at moment "
          "LSB was read for emission",
          "[sequencer][snapshot]")
{
    // Justification: ADR 007 §Visual — playhead alignment fix. The editor's
    // ring view renders "bit just emitted sits under the playhead triangle
    // at the moment of sounding." processStep reads register_'s LSB BEFORE
    // shiftAndFlip consumes it; that pre-shift register is what the editor
    // must display. Without this snapshot the editor only sees the post-
    // shift register where the just-emitted bit no longer exists.
    SequencerParams p;
    p.seed = 42;
    p.length = 8;
    p.lock = 0.5;
    p.density = 1.0;
    Sequencer seq(p);
    for (int i = 0; i < 20; ++i) {
        const RegisterBits preShift = seq.getRegister();
        seq.processStep();
        CHECK(seq.getLastEmittedRegister() == preShift);
    }
}

TEST_CASE("Sequencer.lastEmittedRegister at construction equals initial register",
          "[sequencer][snapshot]")
{
    // Justification: before any step has run, the editor needs a sane
    // initial snapshot. Bit 0 of the initial register == first bit to be
    // emitted, which is consistent with the post-emission view (bit 0 ==
    // just-emitted). No special "no emission yet" state required.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    Sequencer seq(p);
    CHECK(seq.getLastEmittedRegister() == seq.getRegister());
}

TEST_CASE("Sequencer.reset() resets lastEmittedRegister to initial register",
          "[sequencer][snapshot]")
{
    // Justification: transport-start re-seeds the register; the editor's
    // last-emitted view must follow so the ring shows the fresh loop's
    // bit-0 rather than the stale just-emitted bit from the prior loop.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    Sequencer seq(p);
    for (int i = 0; i < 5; ++i) seq.processStep();
    seq.reset();
    CHECK(seq.getLastEmittedRegister() == seq.getRegister());
}

TEST_CASE("Sequencer.lastEmittedRegister in seed-active mode equals register",
          "[sequencer][snapshot]")
{
    // Justification: in seed-active mode, processStep does NOT shift the
    // register (input drives it). pre-shift == post-step == register_, so
    // the editor still sees a coherent "bit 0 = just emitted" view even
    // though no shift happened. The LSB was still read for the emission's
    // bit-tap decision.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.triggerMode = TriggerMode::Seed;
    Sequencer seq(p);
    seq.onInputNoteOn(60, 1);
    REQUIRE(seq.isSeedActivated());
    const RegisterBits regBefore = seq.getRegister();
    seq.processStep();
    CHECK(seq.getLastEmittedRegister() == regBefore);
    CHECK(seq.getRegister() == regBefore);
}

TEST_CASE("Sequencer onTransportStop preserves register (resume-the-loop)",
          "[sequencer][transport]")
{
    // m4l host.ts §transportStop: register is NOT re-initialized so
    // stop/start resumes the same loop. Position resets to 0; register
    // continues from where it was.
    SequencerParams p;
    p.seed = 1;
    p.length = 8;
    p.lock = 0.5;
    p.density = 1.0;

    Sequencer seq(p);
    seq.processStep();
    seq.processStep();
    seq.processStep();
    const RegisterBits regBefore = seq.getRegister();

    seq.onTransportStop();
    CHECK(seq.getRegister() == regBefore);
}
