#include "Engine/Turing.h"

#include <algorithm>

namespace stencil::engine
{
uint32_t maskBits(int length)
{
    if (length >= 32)
        return 0xffffffffu;
    return (1u << length) - 1u;
}

uint64_t probabilityThreshold(double p)
{
    if (p <= 0.0)
        return 0;
    if (p >= 1.0)
        return 0x100000000ULL;
    return static_cast<uint64_t>(p * 4294967296.0);
}

RegisterBits createRegister(int length, RngState& rng)
{
    return nextU32(rng) & maskBits(length);
}

RegisterBits shiftAndFlip(RegisterBits reg, int length, double lock, RngState& rng)
{
    const uint32_t tail = reg & 1u;
    const uint32_t draw = nextU32(rng);
    const uint64_t threshold = probabilityThreshold(1.0 - lock);
    const bool flip = draw < threshold;
    const uint32_t writeBit = flip ? (tail ^ 1u) : tail;
    const uint32_t shifted = reg >> 1;
    return (shifted | (writeBit << (length - 1))) & maskBits(length);
}

RegisterBits shiftAndForce(RegisterBits reg, int length, int forceBit)
{
    const uint32_t writeBit = static_cast<uint32_t>(forceBit & 1);
    const uint32_t shifted = reg >> 1;
    return (shifted | (writeBit << (length - 1))) & maskBits(length);
}

Fraction registerToFraction(RegisterBits reg, int length)
{
    return Fraction{ reg, length >= 32 ? 0xffffffffu : ((1u << length) - 1u) };
}

int mapToNote(uint32_t num, uint32_t den, int rangeLo, int rangeHi)
{
    const int span = rangeHi - rangeLo + 1;
    const uint64_t numerator = static_cast<uint64_t>(num) * static_cast<uint64_t>(span);
    const int offset = static_cast<int>(numerator / static_cast<uint64_t>(den));
    return std::min(rangeLo + offset, rangeHi);
}

TmStepResult tmStep(RegisterBits reg,
                    int length,
                    double lock,
                    double density,
                    int rangeLo,
                    int rangeHi,
                    RngState& rng)
{
    const Fraction f = registerToFraction(reg, length);
    const int note = mapToNote(f.num, f.den, rangeLo, rangeHi);
    const uint32_t dDraw = nextU32(rng);
    const uint64_t dThreshold = probabilityThreshold(density);
    const bool active = dDraw < dThreshold;
    const RegisterBits newReg = shiftAndFlip(reg, length, lock, rng);
    return TmStepResult{ newReg, note, active };
}
}
