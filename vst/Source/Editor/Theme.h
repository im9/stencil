// Single source for editor palette + typography (ADR 007 §Visual reference,
// §Editor — inboil TuringSheet port). All Editor/* views must read colours
// and fonts from here — literal hex / px values in views are a review
// blocker. Mirrors inboil's app.css tokens; the palette is shared verbatim
// with oedipa's Editor/Theme.h since both products inherit inboil's design
// system.

#pragma once

#include <juce_graphics/juce_graphics.h>

namespace stencil {
namespace editor {
namespace theme {

// ── Inboil palette (app.css :root) — see ADR 007 §Visual reference ─────
const juce::Colour bg     = juce::Colour::fromRGB(0xED, 0xE8, 0xDC);  // warm cream
const juce::Colour fg     = juce::Colour::fromRGB(0x1E, 0x20, 0x28);  // dark navy
const juce::Colour olive  = juce::Colour::fromRGB(0x78, 0x78, 0x45);  // active bit / slider accent
const juce::Colour blue   = juce::Colour::fromRGB(0x44, 0x72, 0xB4);  // frozen-state indicator
const juce::Colour salmon = juce::Colour::fromRGB(0xE8, 0xA0, 0x90);  // head bit / mutated bit

// fg-on-bg overlays. Alpha values are inboil's --lz-* / --olive-bg
// opacities, not freely tweakable: the visual hierarchy was tuned in
// inboil and we mirror it 1:1.
inline juce::Colour fgAlpha(float a)    { return fg.withAlpha(a); }
inline juce::Colour oliveAlpha(float a) { return olive.withAlpha(a); }

const auto lzDivider     = fgAlpha(0.06f);
const auto lzBgHover     = fgAlpha(0.06f);
const auto lzBgActive    = fgAlpha(0.08f);
const auto lzBorder      = fgAlpha(0.10f);
const auto lzBorderMid   = fgAlpha(0.12f);
const auto lzBorderStrong= fgAlpha(0.15f);
const auto oliveBg       = oliveAlpha(0.15f);
const auto oliveBgSubtle = oliveAlpha(0.08f);

// ── Type scale (inboil --fs-*) ───────────────────────────────────
constexpr float fsSm = 9.0f;   // group legends
constexpr float fsMd = 10.0f;  // control labels
constexpr float fsLg = 11.0f;  // values, primary labels
constexpr float fsXl = 14.0f;  // ring center note label
constexpr float fsXxl = 20.0f; // ring center fraction value

// JetBrains Mono if installed, monospace otherwise. Fallback chain
// matches inboil --font-data.
juce::Font dataFont(float pointSize, bool bold = false);

// ── Layout tokens (right rail, header, ring, history) ────────────
constexpr int headerHeight = 40;          // inboil .t-header padding 8 + title 16 + pad 8 ≈ 40
constexpr int historyHeight = 136;        // ADR 007 §Layout: HistoryView fixed h
constexpr int railWidth = 280;            // ADR 007 §Layout: RightRailView fixed w
constexpr int rowHeight = 22;
constexpr int rowGap = 4;
constexpr int groupGap = 8;
constexpr int groupPadX = 8;
constexpr int groupPadY = 6;
constexpr int railPad = 12;

// Ring-specific. Centered inside RingView's local bounds at runtime;
// the value here is the natural radius for the 540 - 40 - 136 = 364
// vertical body budget at the default editor size 820 × 540.
constexpr int ringMargin = 24;            // padding around ring within RingView bounds
constexpr int ringActionsBarHeight = 32;  // FREEZE / ROLL row above ring
constexpr float bitRadiusMaxPx = 14.0f;   // matches inboil bitR cap
constexpr float bitRadiusMinPx = 4.0f;    // floor so length=32 doesn't render dots

// History bars (inboil BAR_W / BAR_GAP / BAR_MAX_H).
constexpr int historyBarWidth = 18;
constexpr int historyBarGap = 4;
constexpr int historyBarMaxHeight = 100;

}  // namespace theme
}  // namespace editor
}  // namespace stencil
