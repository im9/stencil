#include "Editor/RingLogic.h"

#include <cmath>

namespace stencil {
namespace editor {

namespace
{
constexpr float kPi = 3.14159265358979323846f;
}

Point2 RingLogic::bitPosition(int idx, int length, float ringRadius, Point2 center)
{
    if (length <= 0) return center;
    const float angle = (static_cast<float>(idx) / static_cast<float>(length))
                        * 2.0f * kPi
                        - kPi * 0.5f;
    return Point2{
        center.x + ringRadius * std::cos(angle),
        center.y + ringRadius * std::sin(angle)
    };
}

int RingLogic::hitTest(Point2 click, int length, float ringRadius,
                       float bitRadius, Point2 center)
{
    if (length <= 0) return -1;
    const float r2 = bitRadius * bitRadius;
    for (int i = 0; i < length; ++i) {
        const auto p = bitPosition(i, length, ringRadius, center);
        const float dx = click.x - p.x;
        const float dy = click.y - p.y;
        if (dx * dx + dy * dy <= r2) return i;
    }
    return -1;
}

float RingLogic::rotationDegrees(int cumulativeSteps, int length)
{
    if (length <= 0) return 0.0f;
    return static_cast<float>(cumulativeSteps)
         * (360.0f / static_cast<float>(length));
}

int RingLogic::readingIndex(int cumulativeSteps, int length)
{
    if (length <= 0 || cumulativeSteps <= 0) return -1;
    const int prev = (cumulativeSteps - 1) % length;
    return ((length - prev) % length);
}

FreezeResult RingLogic::freezeAction(float currentLock,
                                     std::optional<float> prevLock)
{
    if (currentLock == 1.0f) {
        // Unfreeze: restore stashed value, falling back to 0.5 on cold load.
        const float restored = prevLock.value_or(0.5f);
        return FreezeResult{ restored, std::nullopt };
    }
    // Freeze: jump to 1.0, stash current lock so the next press restores it.
    return FreezeResult{ 1.0f, std::optional<float>{ currentLock } };
}

int RingLogic::rollAction(stencil::engine::RngState& rng)
{
    // Mask to [0, 2^31-1] so the result fits APVTS's signed int Seed
    // range. The high bit is musically irrelevant (SplitMix64 mixes the
    // value before xoshiro state is built — see Parameters.cpp §Seed).
    const uint32_t draw = stencil::engine::nextU32(rng);
    return static_cast<int>(draw & 0x7FFFFFFFu);
}

}  // namespace editor
}  // namespace stencil
