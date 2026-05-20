// Stencil TM register-ring renderer (jsui).
// Spec: m4l/host/ui/registerRing.logic.ts (pure layout + hit-test math).
//
// Static snapshot model (vst spec, ported to m4l 2026-05-16): the bridge
// emits a register snapshot per step. Bit 0 of the snapshot is the LSB
// the listener just heard. Bits are arranged CCW around the ring (bit 0
// top, bit 1 upper-left, ...); the engine's right-shift therefore
// "rotates" the snapshot CW step-by-step (each new snapshot has the
// previous bit 1 as its bit 0). This jsui does NOT animate the rotation
// -- m4l's cramped 312x136 canvas reads poorly under a 60Hz ease, so we
// snap to each new snapshot instead. Anticipation animation lives in
// vst's RingView only.
//
// Palette: Ableton M4L dark device-strip rather than the inboil cream
// reference. The strip background shows through (we don't fill bg in
// paint), so the ring floats on Live's native color. Active bits are
// drawn in Live's mid-light gray (the same tone Live uses for live.*
// widget text); inactive bits get a dim outline. Bit 0 always renders
// with a heavier off-white stroke so the playhead reads even when bit 0
// is "off."
//
// Pure layout & hit-test logic lives in m4l/host/ui/registerRing.logic.ts
// (with unit tests). Max's [jsui] runs Max's bundled JS engine, not Node,
// so the formula is re-implemented here as plain JS rather than imported.
// Keep MIN_LENGTH / MAX_LENGTH / MAX_BIT_RADIUS / BIT_GAP / CANVAS_MARGIN /
// POINTER_GAP / POINTER_HALF_WIDTH / POINTER_HEIGHT in sync with
// registerRing.logic.ts. A drift test (registerRing.mirror.test.ts)
// asserts the constants line up.
//
// Comments and string literals are ASCII; non-ASCII glyphs are written as
// \uXXXX escapes -- Max's classic JS parser has been observed to choke
// on UTF-8 in source files (oedipa convention).

inlets = 1
outlets = 1

mgraphics.init()
mgraphics.relative_coords = 0
mgraphics.autofill = 0

post('registerRing.jsui.js loaded build=2026-05-19 currentNote-readout\n')

// --- Constants (mirror m4l/host/ui/registerRing.logic.ts) ---

var MIN_LENGTH = 2
var MAX_LENGTH = 32
var MAX_BIT_RADIUS = 14
var BIT_GAP = 2
var CANVAS_MARGIN = 4
var POINTER_GAP = 4
var POINTER_HALF_WIDTH = 3
var POINTER_HEIGHT = 6

// --- Palette: Live M4L device-strip dark + single orange accent ---
//
// Background is left transparent so Live's strip color shows through.
// Active bits use Ableton's mid-grey (the same tone Live uses for
// live.* widget text). Only the read-head (trigger position at the
// top of the ring) uses Live's orange so the bit currently being
// played stands out from the rest of the active pattern.
// Inactive bits get a dim grey outline.

var COL_ACTIVE_FILL = [0.72, 0.72, 0.72]  // Ableton mid-grey (active bits)
var COL_OUTLINE     = [0.55, 0.55, 0.55]  // dim grey (inactive outline)
var OUTLINE_ALPHA   = 0.85
var COL_READHEAD    = [1.00, 0.66, 0.20]  // Live orange (trigger / read-head)
var COL_POINTER     = [0.85, 0.85, 0.85]  // playhead triangle
var COL_READOUT     = [0.85, 0.85, 0.85]  // center note-name readout

// --- State ---

var bits = []
// MIDI pitch of the note currently sounding, or -1 when silent. The bridge
// (host/bridge.ts) forks every noteOn/noteOff to the `currentNote` outlet;
// noteOn -> 0..127, noteOff -> -1. Display lifecycle = audible-note
// lifecycle, so silent steps (LSB=0 / density-fail / between gate-closed
// and next noteOn) leave the readout blank.
var currentNote = -1

// --- Message dispatch ---
//
// register <bit0> <bit1> ... <bitN-1>   bridge-published snapshot; redraw.
// ringHead / stepBeat / triggerFlash    accepted but unused (kept on the
//   bridge for back-compat with patcher routing; this renderer is purely
//   snapshot-driven now).

function anything() {
  var msg = messagename
  var args = arrayfromargs(arguments)
  if (msg === 'register') { setRegister(args); return }
  if (msg === 'currentNote') { setCurrentNote(args[0]); return }
  if (msg === 'ringHead' || msg === 'stepBeat' || msg === 'triggerFlash') return
  post('registerRing.jsui.js: unhandled message ' + msg + '\n')
}

function setRegister(args) {
  var next = []
  for (var i = 0; i < args.length; i++) {
    next.push(Number(args[i]) & 1)
  }
  bits = next
  mgraphics.redraw()
}

function setCurrentNote(v) {
  var n = Number(v)
  if (!isFinite(n)) { currentNote = -1 }
  else if (n < 0) { currentNote = -1 }
  else if (n > 127) { currentNote = 127 }
  else { currentNote = Math.round(n) }
  mgraphics.redraw()
}

// MIDI -> note name, Live convention (MIDI 60 = "C3"). Mirrored from
// m4l/host/ui/rangeSlider.logic.ts midiToNoteName; the renderer's mirror
// test asserts both copies (this one + rangeSlider.jsui.js) carry the
// same pitch-class table. The bridge could pre-format and emit a string,
// but emitting the raw MIDI number keeps the outlet protocol numeric
// (same shape as `register` / `ringHead`) and the renderer owns its
// own display formatting.
var NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

function midiToNoteName(midi) {
  var n = Number(midi)
  if (!isFinite(n)) return '?'
  if (n < 0) n = 0
  else if (n > 127) n = 127
  n = Math.round(n)
  var octave = Math.floor(n / 12) - 2
  var pc = n % 12
  return NOTE_NAMES[pc] + octave
}

// --- Layout (mirrors registerRing.logic.ts) ---

function clampLength(n) {
  n = Number(n)
  if (!isFinite(n)) return MIN_LENGTH
  var r = Math.round(n)
  if (r < MIN_LENGTH) return MIN_LENGTH
  if (r > MAX_LENGTH) return MAX_LENGTH
  return r
}

function computeGeometry(boxW, boxH, len) {
  len = clampLength(len)
  var cx = boxW / 2
  var cy = boxH / 2
  var minDim = boxW < boxH ? boxW : boxH
  var maxRadius = minDim / 2 - CANVAS_MARGIN
  if (maxRadius < 0) maxRadius = 0
  // Chord-based bit sizing (mirrors registerRing.logic.ts) so bits
  // don't visually overlap at small/medium N. The earlier arc-length
  // formula failed around N = 10..14.
  var sinHalfAngle = Math.sin(Math.PI / Math.max(len, 4))
  var bitRadius = (maxRadius * sinHalfAngle - BIT_GAP / 2) / (1 + sinHalfAngle)
  if (bitRadius > MAX_BIT_RADIUS) bitRadius = MAX_BIT_RADIUS
  if (bitRadius < 1) bitRadius = 1
  var radius = maxRadius - bitRadius
  if (radius < 0) radius = 0
  return { cx: cx, cy: cy, radius: radius, bitRadius: bitRadius, length: len }
}

function bitPosition(idx, g) {
  // CCW arrangement: idx 0 at top (-pi/2), idx 1 upper-left, ...
  var angle = -Math.PI / 2 - (idx / g.length) * Math.PI * 2
  return {
    x: g.cx + g.radius * Math.cos(angle),
    y: g.cy + g.radius * Math.sin(angle)
  }
}

function pointerTip(g) {
  return {
    x: g.cx,
    y: g.cy - g.radius - g.bitRadius - POINTER_GAP - POINTER_HEIGHT
  }
}

function hitTest(x, y, g) {
  var r2 = g.bitRadius * g.bitRadius
  for (var i = 0; i < g.length; i++) {
    var p = bitPosition(i, g)
    var dx = x - p.x
    var dy = y - p.y
    if (dx * dx + dy * dy <= r2) return i
  }
  return -1
}

// --- Drawing ---

function fillCircle(x, y, r, c, alpha) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], alpha === undefined ? 1 : alpha)
  mgraphics.ellipse(x - r, y - r, r * 2, r * 2)
  mgraphics.fill()
}

function strokeCircle(x, y, r, c, alpha, lineW) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], alpha)
  mgraphics.set_line_width(lineW)
  mgraphics.ellipse(x - r, y - r, r * 2, r * 2)
  mgraphics.stroke()
}

function fillTriangle(x1, y1, x2, y2, x3, y3, c, alpha) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], alpha === undefined ? 1 : alpha)
  mgraphics.move_to(x1, y1)
  mgraphics.line_to(x2, y2)
  mgraphics.line_to(x3, y3)
  mgraphics.close_path()
  mgraphics.fill()
}

// Default ring length to draw before the bridge has emitted its first
// `register` message. Matches DEFAULT_PARAMS.length in host/host.ts.
var DEFAULT_LENGTH = 8

function paint() {
  var w = box.rect[2] - box.rect[0]
  var h = box.rect[3] - box.rect[1]

  // No background fill: let Live's strip color show through so the ring
  // sits on the host device's native tone rather than a bright canvas.

  var len = bits.length > 0 ? bits.length : DEFAULT_LENGTH
  var g = computeGeometry(w, h, len)

  for (var i = 0; i < len; i++) {
    var p = bitPosition(i, g)
    var isOn = (bits.length > 0 && bits[i] === 1)
    var isHead = (i === 0)

    if (isHead) {
      // Read-head: heavy near-white outline, no fill. Pops against the
      // dark Live strip and reads as "the playhead bit" even when bit 0
      // is "off." Matches vst RingView's inboil .bit-reading treatment.
      strokeCircle(p.x, p.y, g.bitRadius, COL_READHEAD, 1.0, 2.0)
      // When bit 0 is also "on," fill the inside with the active tone so
      // the user still sees the audible-vs-silent distinction at the
      // playhead.
      if (isOn) {
        fillCircle(p.x, p.y, g.bitRadius - 1.5, COL_ACTIVE_FILL, 1)
      }
    } else if (isOn) {
      fillCircle(p.x, p.y, g.bitRadius, COL_ACTIVE_FILL, 1)
    } else {
      strokeCircle(p.x, p.y, g.bitRadius, COL_OUTLINE, OUTLINE_ALPHA, 1.0)
    }
  }

  // Fixed pointer triangle at top, pointing down toward bit 0. Drawn
  // last so it overlays cleanly on the ring.
  var tip = pointerTip(g)
  fillTriangle(
    tip.x, tip.y + POINTER_HEIGHT,
    tip.x - POINTER_HALF_WIDTH, tip.y,
    tip.x + POINTER_HALF_WIDTH, tip.y,
    COL_POINTER, 0.85
  )

  // Center-text readout of the note currently sounding. Drawn last so
  // it sits on top of any bit-shape that visually intrudes on the
  // ring's interior. currentNote < 0 means silent (bridge cleared it
  // via noteOff fork) -- skip the draw entirely so the ring shows
  // nothing rather than a stale label.
  if (currentNote >= 0) {
    var label = midiToNoteName(currentNote)
    // Andale Mono ~5.5 px / glyph at the chosen size. Center the
    // baseline against the geometry center so the text reads as
    // "what the playhead is emitting." Font size scales with the
    // available radius so small rings (LEN=32) don't overflow.
    var fontSize = Math.max(10, Math.min(18, Math.floor(g.radius * 0.42)))
    var charWidth = fontSize * 0.62
    var labelWidth = label.length * charWidth
    var leftX = g.cx - labelWidth / 2
    var baselineY = g.cy + fontSize / 3
    mgraphics.set_source_rgba(COL_READOUT[0], COL_READOUT[1], COL_READOUT[2], 1)
    mgraphics.select_font_face('Andale Mono')
    mgraphics.set_font_size(fontSize)
    mgraphics.move_to(leftX, baselineY)
    mgraphics.show_text(label)
  }
}

// --- Mouse interaction ---
//
// Single primary-button click toggles the bit at the cursor. Out-of-bound
// or modifier clicks are ignored.

function onclick(x, y, button, cmd, shift, capslock, option, ctrl) {
  if (button !== 1) return
  if (cmd || shift || option || ctrl) return
  if (bits.length === 0) return

  var w = box.rect[2] - box.rect[0]
  var h = box.rect[3] - box.rect[1]
  var g = computeGeometry(w, h, bits.length)

  var idx = hitTest(x, y, g)
  if (idx < 0) return

  // Optimistic local toggle so the UI feels instant; the bridge's
  // re-emitted `register` on the next step will correct this if the
  // host disagrees.
  var newValue = (bits[idx] === 1) ? 0 : 1
  bits[idx] = newValue
  mgraphics.redraw()
  outlet(0, 'setBit', idx, newValue)
}
