#include "Engine/Rng.h"

namespace stencil::engine
{
namespace
{
constexpr uint64_t kSplitMixIncr = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kSplitMixMul1 = 0xbf58476d1ce4e5b9ULL;
constexpr uint64_t kSplitMixMul2 = 0x94d049bb133111ebULL;

inline uint32_t rotl32(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

uint64_t splitMix64Step(uint64_t& state)
{
    state += kSplitMixIncr;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * kSplitMixMul1;
    z = (z ^ (z >> 27)) * kSplitMixMul2;
    return z ^ (z >> 31);
}
}

RngState seedRng(uint64_t seed)
{
    uint64_t state = seed;
    const uint64_t z1 = splitMix64Step(state);
    const uint64_t z2 = splitMix64Step(state);
    return RngState{ { static_cast<uint32_t>(z1 & 0xffffffffULL),
                       static_cast<uint32_t>((z1 >> 32) & 0xffffffffULL),
                       static_cast<uint32_t>(z2 & 0xffffffffULL),
                       static_cast<uint32_t>((z2 >> 32) & 0xffffffffULL) } };
}

uint32_t nextU32(RngState& rng)
{
    const uint32_t result = rotl32(rng.s[0] + rng.s[3], 7) + rng.s[0];
    const uint32_t t = rng.s[1] << 9;
    rng.s[2] ^= rng.s[0];
    rng.s[3] ^= rng.s[1];
    rng.s[1] ^= rng.s[2];
    rng.s[0] ^= rng.s[3];
    rng.s[2] ^= t;
    rng.s[3] = rotl32(rng.s[3], 11);
    return result;
}

double drawUniform(RngState& rng)
{
    return static_cast<double>(nextU32(rng)) / 4294967296.0;
}
}
