#include "PluginEditor.h"

StencilEditor::StencilEditor(stencil::plugin::StencilProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(600, 400);
}

void StencilEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("Stencil — Turing Machine + Quantizer", getLocalBounds(), juce::Justification::centred);
}

void StencilEditor::resized() {}
