// Editor smoke tests (ADR 007 Phase 3 §Implementation checklist).
//
// Coverage:
//   - StencilEditor instantiates without crashing or asserting (paint
//     happens during the JUCE message-thread dispatch on construction).
//   - mouseDown on the ring routes to ROLL — the seed parameter changes
//     after the simulated press, matching ADR 007 §FREEZE / ROLL
//     semantics for v1.
//
// Renderer fidelity (palette match, font, layout proportions) is
// verified manually in real DAWs per ADR 007 §Verification.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Editor/PluginEditor.h"
#include "Editor/RingView.h"
#include "Plugin/Parameters.h"
#include "Plugin/PluginProcessor.h"

using stencil::editor::StencilEditor;
using stencil::plugin::StencilProcessor;
namespace pid = stencil::plugin::pid;

TEST_CASE("StencilEditor instantiates with the default 820x600 size", "[editor][smoke]")
{
    // Justification: ADR 007 §Editor specified 820 × 540, but height grew
    // to 600 post-2026-05-16 RANGE consolidation so the right rail's
    // Parameters / Output / Trigger / Reproducibility fieldsets fit
    // without a scrollbar (see PluginEditor.cpp setSize comment).
    // Verifying constructor success + default size catches both
    // linker-level regressions (e.g. Editor sources missing from the
    // test binary) and an accidental setSize call drift.
    StencilProcessor proc;
    StencilEditor editor(proc);
    REQUIRE(editor.getWidth() == 820);
    REQUIRE(editor.getHeight() == 600);
}

TEST_CASE("StencilEditor::paint runs without crashing", "[editor][smoke]")
{
    // Justification: cheapest possible smoke for the renderer chain —
    // Theme font load, ring AffineTransform composition, history
    // simulation, right-rail attachments. A null-pointer in any of those
    // surfaces here as a SIGSEGV during paint.
    StencilProcessor proc;
    StencilEditor editor(proc);
    editor.setBounds(0, 0, 820, 540);

    // Must use a software image with the Image::ARGB format so paint's
    // alpha-blended fills don't require a hardware context.
    juce::Image img(juce::Image::ARGB, 820, 540, true);
    juce::Graphics g(img);
    REQUIRE_NOTHROW(editor.paintEntireComponent(g, false));
}

TEST_CASE("RingView mouseDown routes a bit-circle hit to ROLL", "[editor][ring][roll]")
{
    // Justification: ADR 007 §FREEZE / ROLL semantics, v1 — clicking any
    // bit on the ring writes a fresh seed. This is the only end-to-end
    // assert that proves the editor's click → APVTS path is wired; the
    // RingLogic layer alone can't catch a missing parameter write.
    StencilProcessor proc;
    StencilEditor editor(proc);
    editor.setBounds(0, 0, 820, 540);

    // Force one paint pass so the RingView's child Component has its
    // cached bounds set (mouseDown reads getWidth() / getHeight()).
    juce::Image img(juce::Image::ARGB, 820, 540, true);
    juce::Graphics g(img);
    editor.paintEntireComponent(g, false);

    auto& ring = editor.ringViewForTest();

    const int seedBefore = (int) *proc.getApvts().getRawParameterValue(pid::seed);

    // Synthesize a press at the visual top of the ring (where bit 0
    // sits before any rotation has happened — cumulativeSteps starts
    // at 0 in a fresh processor). Use the ring's local origin.
    const float cx = (float) ring.getWidth() * 0.5f;
    // Top bit lives roughly at ringActionsBarHeight + ringMargin + bitRadius;
    // 32 + 24 + 12 ≈ 68, well inside the bit's hit envelope.
    const float topY = 68.0f;

    juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                       juce::Point<float>(cx, topY),
                       juce::ModifierKeys::leftButtonModifier,
                       0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                       &ring, &ring,
                       juce::Time::getCurrentTime(),
                       juce::Point<float>(cx, topY),
                       juce::Time::getCurrentTime(),
                       1, false);
    ring.mouseDown(e);

    const int seedAfter = (int) *proc.getApvts().getRawParameterValue(pid::seed);

    // Default seed is 42 (concept.md). After ROLL, the seed should have
    // changed to a fresh value — either to a different number, or
    // (vanishingly unlikely) to 42 again. Use !=. If that flakes, the
    // millisecond-counter seed feeding rollAction is too coarse and
    // the implementation needs a different entropy source.
    REQUIRE(seedAfter != seedBefore);
}
