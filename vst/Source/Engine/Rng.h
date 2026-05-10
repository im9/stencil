#pragma once

// xoshiro128++ (Vigna 2019) with SplitMix64 seeding.
// Cross-target conformance vectors: docs/ai/rng-test-vectors.json
// Cross-repo synchronized with Pointsman per ADR 005 §RNG sharing —
// the implementation and the vectors must stay byte-identical between
// the two repos and between m4l and vst.

#include <array>
#include <cstdint>

namespace stencil::engine
{
struct RngState
{
    std::array<uint32_t, 4> s;
};

// Seed convention (canonical, matches rng-test-vectors.json meta):
//   call SplitMix64 twice on the u64 seed; split each output u64 into
//   [low32, high32]. State is [low(z1), high(z1), low(z2), high(z2)].
RngState seedRng(uint64_t seed);

// Advance state and return the next u32 draw.
uint32_t nextU32(RngState& rng);

// Convenience: uniform draw in [0, 1). nextU32 / 2^32.
double drawUniform(RngState& rng);
}
