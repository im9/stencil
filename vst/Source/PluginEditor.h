#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Plugin/PluginProcessor.h"

// Phase 2 placeholder. Phase 3 reorganizes into Source/Editor/ with the
// inboil TuringSheet port (RingView / RightRailView / HistoryView).
class StencilEditor : public juce::AudioProcessorEditor
{
public:
    explicit StencilEditor(stencil::plugin::StencilProcessor&);
    ~StencilEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    stencil::plugin::StencilProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StencilEditor)
};
