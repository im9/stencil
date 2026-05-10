#pragma once

// Turing Machine engine — pure functions per ADR 001 §TM interface.
// Cross-target conformance vectors: docs/ai/turing-test-vectors.json
// (m4l TS reference: m4l/engine/turing.ts).

#include <cstdint>

#include "Engine/Rng.h"

namespace stencil::engine
{
using RegisterBits = uint32_t;

// (1 << length) - 1 with length=32 special-cased to 0xffffffff.
uint32_t maskBits(int length);

// Probability threshold in u32 space: floor(p × 2^32). Returns 0 for p ≤ 0,
// 2^32 for p ≥ 1. Comparison vs raw u32 draws keeps the flip / density
// decisions identical across targets (no float rounding divergence).
uint64_t probabilityThreshold(double p);

// Initialize a fresh register: one xoshiro draw, low `length` bits.
RegisterBits createRegister(int length, RngState& rng);

// Shift toward tail; insert flipped/preserved bit at head.
//   flip ⇔ rawU32 < probabilityThreshold(1 - lock).
RegisterBits shiftAndFlip(RegisterBits reg, int length, double lock, RngState& rng);

// Shift toward tail; insert forceBit at head (no rng draw, no lock).
RegisterBits shiftAndForce(RegisterBits reg, int length, int forceBit);

struct Fraction
{
    uint32_t num;
    uint32_t den;
};

// num = register, den = (1 << length) - 1 (or 0xffffffff if length=32).
Fraction registerToFraction(RegisterBits reg, int length);

// floor(lo + (num × span) / den), clamped to hi.
// Computed as integer division to avoid float drift across targets.
int mapToNote(uint32_t num, uint32_t den, int rangeLo, int rangeHi);

struct TmStepResult
{
    RegisterBits reg;
    int note;
    bool active;
};

// One TM step. Read register for the output note BEFORE shiftAndFlip mutates
// it (step n's emitted note reflects register state at the start of step n).
// Draw order: density draw FIRST, then flip draw — binding for cross-target
// reproducibility.
TmStepResult tmStep(RegisterBits reg,
                    int length,
                    double lock,
                    double density,
                    int rangeLo,
                    int rangeHi,
                    RngState& rng);
}
