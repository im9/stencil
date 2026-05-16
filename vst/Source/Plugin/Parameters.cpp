#include "Plugin/Parameters.h"

#include <algorithm>
#include <limits>

namespace stencil::plugin
{
juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using PID = juce::ParameterID;

    // Bump if any param ID changes meaning or a Choice array is reordered.
    constexpr int kParamVersion = 1;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<API>(
        PID{ pid::length, kParamVersion }, "Length",
        2, 32, defaults::length));

    layout.add(std::make_unique<APF>(
        PID{ pid::lock, kParamVersion }, "Lock",
        juce::NormalisableRange<float>(0.0f, 1.0f), defaults::lock));

    layout.add(std::make_unique<APF>(
        PID{ pid::density, kParamVersion }, "Density",
        juce::NormalisableRange<float>(0.0f, 1.0f), defaults::density));

    layout.add(std::make_unique<API>(
        PID{ pid::rangeLo, kParamVersion }, "Range Lo",
        0, 127, defaults::rangeLo));

    layout.add(std::make_unique<API>(
        PID{ pid::rangeHi, kParamVersion }, "Range Hi",
        0, 127, defaults::rangeHi));

    layout.add(std::make_unique<APC>(
        PID{ pid::subdivision, kParamVersion }, "Subdivision",
        subdivisionChoices, defaults::subdivisionIdx));

    // Seed: APVTS int caps at signed 32-bit; concept.md's u31 (0..2^31-1)
    // fits exactly. The high bit is musically irrelevant (SplitMix64 mixes
    // the value before xoshiro state is built).
    layout.add(std::make_unique<API>(
        PID{ pid::seed, kParamVersion }, "Seed",
        0, std::numeric_limits<juce::int32>::max(), defaults::seed));

    layout.add(std::make_unique<APC>(
        PID{ pid::triggerMode, kParamVersion }, "Trigger Mode",
        triggerModeChoices, defaults::triggerModeIdx));

    layout.add(std::make_unique<API>(
        PID{ pid::inputChannel, kParamVersion }, "Input Channel",
        0, 16, defaults::inputChannel));

    layout.add(std::make_unique<API>(
        PID{ pid::outputVelocity, kParamVersion }, "Output Velocity",
        1, 127, defaults::outputVelocity));

    layout.add(std::make_unique<APF>(
        PID{ pid::outputGate, kParamVersion }, "Output Gate",
        juce::NormalisableRange<float>(0.0f, 1.0f), defaults::outputGate));

    layout.add(std::make_unique<API>(
        PID{ pid::outputChannel, kParamVersion }, "Output Channel",
        1, 16, defaults::outputChannel));

    return layout;
}

engine::SequencerParams readParams(const juce::AudioProcessorValueTreeState& apvts)
{
    engine::SequencerParams p;
    p.length = static_cast<int>(*apvts.getRawParameterValue(pid::length));
    p.lock = static_cast<double>(*apvts.getRawParameterValue(pid::lock));
    p.density = static_cast<double>(*apvts.getRawParameterValue(pid::density));

    int lo = static_cast<int>(*apvts.getRawParameterValue(pid::rangeLo));
    int hi = static_cast<int>(*apvts.getRawParameterValue(pid::rangeHi));
    if (lo > hi)
        std::swap(lo, hi);  // engine sees a valid range; APVTS values may drift
                             // briefly between listener fire and processBlock entry
    p.rangeLo = lo;
    p.rangeHi = hi;

    p.subdivision = static_cast<engine::Subdivision>(
        static_cast<int>(*apvts.getRawParameterValue(pid::subdivision)));
    p.seed = static_cast<uint32_t>(*apvts.getRawParameterValue(pid::seed));
    p.triggerMode = static_cast<engine::TriggerMode>(
        static_cast<int>(*apvts.getRawParameterValue(pid::triggerMode)));
    p.inputChannel = static_cast<int>(*apvts.getRawParameterValue(pid::inputChannel));
    p.outputVelocity = static_cast<int>(*apvts.getRawParameterValue(pid::outputVelocity));
    p.outputGate = static_cast<double>(*apvts.getRawParameterValue(pid::outputGate));
    p.outputChannel = static_cast<int>(*apvts.getRawParameterValue(pid::outputChannel));
    return p;
}

}  // namespace stencil::plugin
