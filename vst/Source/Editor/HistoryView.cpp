#include "Editor/HistoryView.h"

#include <algorithm>

#include "Editor/NoteFormat.h"
#include "Editor/Theme.h"
#include "Engine/Rng.h"
#include "Engine/Turing.h"
#include "Plugin/Parameters.h"

namespace stencil {
namespace editor {

namespace
{
// Pure helper for option-D step-sequencer forward simulation. Given the
// engine's CURRENT state at the moment of the most recent emission
// (snap.currentReg, snap.currentRng), simulate the next `length - 1`
// steps with tmStep + the spec's bit-tap gate so each cell shows the
// note that will actually play at that loop position. The slot for the
// just-emitted position is filled from the snapshot's own (reg, note,
// active) — that's the cell currently under the playhead, and it
// reflects what already played rather than a future prediction.
//
// Returns true iff the snapshot has any real data to render (steps > 0).
// When steps == 0 (pre-first-emission or after a lifecycle reset) the
// caller should draw empty cells rather than displaying a prediction
// from a register that hasn't been advanced yet.
bool simulateForwardForHistory(const plugin::StencilProcessor::EditorSnapshot& snap,
                               const engine::SequencerParams& params,
                               std::array<HistoryView::StepSnap, 32>& slotsOut,
                               int& playheadIdxOut)
{
    const int length = std::clamp(params.length, 2, 32);
    for (auto& s : slotsOut) s = HistoryView::StepSnap{};

    if (snap.steps <= 0) {
        playheadIdxOut = -1;
        return false;
    }

    // Playhead = position of the most recent emission. snap.steps == N
    // means N steps have already played, so the just-emitted position
    // is (N - 1) % length.
    const int playheadIdx = (snap.steps - 1) % length;
    playheadIdxOut = playheadIdx;

    // Just-emitted cell: use the snapshot directly (the actual value
    // that played). snap.note is lifecycle-driven (last audible) and
    // would be wrong for silent steps, so derive note from snap.reg
    // via mapToNote for the silent branch — same value as the engine
    // computes for both active and silent emissions, so this is
    // authoritative for both branches.
    {
        const auto frac = engine::registerToFraction(snap.reg, params.length);
        const int note = engine::mapToNote(frac.num, frac.den,
                                           params.rangeLo, params.rangeHi);
        slotsOut[(std::size_t)playheadIdx] = HistoryView::StepSnap{
            snap.reg, note, snap.active, true
        };
    }

    // Simulate the next length-1 steps from the engine's current state.
    // tmStep returns density-only `active`; AND with bit0 of the pre-
    // shift register locally to match the spec's bit-tap gate (the
    // same correction HistoryView already applied; cf. the historic
    // 2026-05-23 tmStep discussion).
    engine::RegisterBits reg = snap.currentReg;
    engine::RngState rng = snap.currentRng;
    for (int i = 1; i < length; ++i) {
        const int pos = (playheadIdx + i) % length;
        const bool bit0 = (reg & 1u) != 0u;
        const auto r = engine::tmStep(reg, params.length, params.lock,
                                      params.density,
                                      params.rangeLo, params.rangeHi, rng);
        slotsOut[(std::size_t)pos] = HistoryView::StepSnap{
            reg, r.note, r.active && bit0, true
        };
        reg = r.reg;
    }

    return true;
}
}  // namespace

HistoryView::HistoryView(plugin::StencilProcessor& p)
    : processor_(p)
{
    setOpaque(false);
    startTimerHz(15);
}

HistoryView::~HistoryView() { stopTimer(); }

void HistoryView::paint(juce::Graphics& g)
{
    g.setColour(theme::lzBorder);
    g.drawLine(0.0f, 0.5f, static_cast<float>(getWidth()), 0.5f, 1.0f);

    const auto params = plugin::readParams(processor_.getApvts());
    const int length = std::clamp(params.length, 2, 32);
    if (length <= 0) return;

    const auto snap = processor_.readEditorSnapshot();
    int playheadIdx = -1;
    simulateForwardForHistory(snap, params, slots_, playheadIdx);

    const int cellW = theme::historyBarWidth;
    const int barGap = theme::historyBarGap;
    const int barMaxH = theme::historyBarMaxHeight;
    const int cellH = std::min(barMaxH, 28);
    const int totalW = length * (cellW + barGap);

    const int startX = std::max(theme::railPad, (getWidth() - totalW) / 2);
    const int baseY = (getHeight() - barMaxH) / 2;
    const int cellY = baseY + (barMaxH - cellH) / 2;

    g.setFont(theme::dataFont(theme::fsSm, false));
    for (int i = 0; i < length; ++i) {
        const auto& s = slots_[(std::size_t)i];
        const int x = startX + i * (cellW + barGap);
        juce::Rectangle<float> cell(static_cast<float>(x),
                                    static_cast<float>(cellY),
                                    static_cast<float>(cellW),
                                    static_cast<float>(cellH));

        const bool isPlaying = (i == playheadIdx);

        if (!s.valid) {
            // No snapshot data yet (transport hasn't fired any step).
            g.setColour(theme::fgAlpha(0.20f));
            g.drawRoundedRectangle(cell, 2.0f, 0.5f);
            continue;
        }

        if (s.active) {
            g.setColour(isPlaying ? theme::olive : theme::oliveAlpha(0.7f));
            g.fillRoundedRectangle(cell, 2.0f);
        } else {
            g.setColour(theme::fgAlpha(0.30f));
            g.drawRoundedRectangle(cell, 2.0f, 1.0f);
        }
        if (isPlaying) {
            g.setColour(theme::fg);
            g.drawRoundedRectangle(cell, 2.0f, 1.5f);
        }

        // Note label below every populated cell. Active vs silent uses
        // alpha to differentiate without dropping the pitch info.
        g.setColour(s.active ? theme::fgAlpha(0.7f) : theme::fgAlpha(0.35f));
        g.drawText(noteLabel(s.note),
                   x - 4, cellY + cellH + 2,
                   cellW + 8, 12,
                   juce::Justification::centred);
    }
}

void HistoryView::timerCallback()
{
    // Snapshot-driven repaint: the snapshot's `steps` increments on each
    // emission, and changes to seed / length / lock / density / range
    // flow through the snapshot via the audio thread's republish on
    // parameter change. A bump of any of these is the signal to redraw.
    const int steps = processor_.getCumulativeSteps();
    const auto params = plugin::readParams(processor_.getApvts());
    bool dirty = false;
    if (steps != lastSteps_)            { lastSteps_ = steps; dirty = true; }
    if (params.seed != lastSeed_)       { lastSeed_ = params.seed; dirty = true; }
    if (params.length != lastLength_)   { lastLength_ = params.length; dirty = true; }
    if (params.lock != lastLock_)       { lastLock_ = static_cast<float>(params.lock); dirty = true; }
    if (params.density != lastDensity_) { lastDensity_ = static_cast<float>(params.density); dirty = true; }
    if (params.rangeLo != lastRangeLo_) { lastRangeLo_ = params.rangeLo; dirty = true; }
    if (params.rangeHi != lastRangeHi_) { lastRangeHi_ = params.rangeHi; dirty = true; }
    if (dirty) repaint();
}

}  // namespace editor
}  // namespace stencil
