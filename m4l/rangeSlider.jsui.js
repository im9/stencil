// Stencil TM range-slider jsui.
// Spec: m4l/host/ui/rangeSlider.logic.ts.
//
// Two-thumb HORIZONTAL MIDI-range slider, ~108x32 inside a bpatcher.
// LO thumb at left, HI thumb at right -- pitch=right intuition. The
// slider sits directly above the SEED field in the device's left
// column; the spare horizontal width there is wider than tall so
// horizontal orientation fits without crowding the SEED row below.
//
// Drives rangeLo / rangeHi (MIDI notes 0..127). The lo <= hi invariant
// is enforced inside the widget: dragging LO past HI pulls HI along,
// and vice versa. The actual live.dial-rangeLo / -rangeHi widgets stay
// off-presentation in Stencil.maxpat as the source-of-truth for Live's
// parameter system; this jsui is the user-facing control on top.
//
// Each thumb's current value is shown as an Ableton-style note name
// ("C3", "F#4", etc) directly below the thumb. Live's clip-view piano
// roll uses the same Yamaha convention (MIDI 60 = "C3"), so the readout
// reads consistently with Live's own UI.
//
// Palette: Live device-strip dark with a blue accent on the active
// segment, matching Ableton's MIDI-effect colour language.
//
// Comments and string literals are ASCII; non-ASCII glyphs must be
// written as \uXXXX (Max's classic JS parser chokes on UTF-8).

inlets = 1
outlets = 1

mgraphics.init()
mgraphics.relative_coords = 0
mgraphics.autofill = 0

post('rangeSlider.jsui.js loaded build=2026-05-19\n')

// --- Constants (mirror m4l/host/ui/rangeSlider.logic.ts) ---

var MIN_VALUE = 0
var MAX_VALUE = 127
var THUMB_RADIUS = 5
var TRACK_PAD = 6
var TRACK_HEIGHT = 3
var HIT_RADIUS_MULT = 2

// --- Palette: Live device-strip dark + blue active accent ---

var COL_TRACK_BG     = [0.30, 0.30, 0.30]   // dark grey inactive track
var COL_TRACK_ACTIVE = [0.28, 0.63, 0.92]   // Live blue accent (selected range)
var COL_THUMB        = [0.93, 0.93, 0.93]   // near-white thumb fill
var COL_THUMB_RING   = [0.20, 0.20, 0.20]   // thumb outline for definition
var COL_LABEL        = [0.72, 0.72, 0.72]   // Live UI text grey

// --- State ---

var lo = 48
var hi = 72
var dragging = -1

// --- Message dispatch ---

function anything() {
  var msg = messagename
  var args = arrayfromargs(arguments)
  if (msg === 'setLo') { setLo(args[0]); return }
  if (msg === 'setHi') { setHi(args[0]); return }
  post('rangeSlider.jsui.js: unhandled message ' + msg + '\n')
}

function setLo(v) {
  var n = clampValue(v)
  lo = n
  if (lo > hi) hi = lo
  mgraphics.redraw()
}

function setHi(v) {
  var n = clampValue(v)
  hi = n
  if (hi < lo) lo = hi
  mgraphics.redraw()
}

// --- Logic (mirrors rangeSlider.logic.ts) ---

function clampValue(v) {
  var n = Number(v)
  if (!isFinite(n)) return MIN_VALUE
  if (n < MIN_VALUE) return MIN_VALUE
  if (n > MAX_VALUE) return MAX_VALUE
  return Math.round(n)
}

function trackBounds() {
  var w = box.rect[2] - box.rect[0]
  var h = box.rect[3] - box.rect[1]
  var left = TRACK_PAD
  var right = w - TRACK_PAD
  if (right < left) right = left
  return { left: left, right: right, cy: h / 2, width: right - left }
}

function valueToX(value, tr) {
  var v = Number(value)
  if (!isFinite(v) || v < MIN_VALUE) v = MIN_VALUE
  else if (v > MAX_VALUE) v = MAX_VALUE
  var t = (v - MIN_VALUE) / (MAX_VALUE - MIN_VALUE)
  return tr.left + t * tr.width
}

function xToValue(x, tr) {
  if (tr.width <= 0) return MIN_VALUE
  var t = (x - tr.left) / tr.width
  if (t < 0) t = 0
  if (t > 1) t = 1
  return Math.round(MIN_VALUE + t * (MAX_VALUE - MIN_VALUE))
}

function pickThumb(x, tr) {
  var loX = valueToX(lo, tr)
  var hiX = valueToX(hi, tr)
  var dLo = Math.abs(x - loX)
  var dHi = Math.abs(x - hiX)
  var threshold = THUMB_RADIUS * HIT_RADIUS_MULT
  if (dLo > threshold && dHi > threshold) return -1
  return dLo <= dHi ? 0 : 1
}

// Live's clip view labels MIDI 60 as "C3" (Yamaha convention). Mirror
// the conversion here so the renderer can show note names without
// importing logic.ts (Max's JS sandbox can't reach the TS source).
var NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

function midiToNoteName(midi) {
  var n = Number(midi)
  if (!isFinite(n)) return '?'
  if (n < MIN_VALUE) n = MIN_VALUE
  else if (n > MAX_VALUE) n = MAX_VALUE
  n = Math.round(n)
  var octave = Math.floor(n / 12) - 2
  var pc = n % 12
  return NOTE_NAMES[pc] + octave
}

// --- Drawing ---

function fillRect(x, y, w, h, c, alpha) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], alpha === undefined ? 1 : alpha)
  mgraphics.rectangle(x, y, w, h)
  mgraphics.fill()
}

function fillCircle(x, y, r, c) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], 1)
  mgraphics.ellipse(x - r, y - r, r * 2, r * 2)
  mgraphics.fill()
}

function strokeCircle(x, y, r, c, lineW) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], 1)
  mgraphics.set_line_width(lineW)
  mgraphics.ellipse(x - r, y - r, r * 2, r * 2)
  mgraphics.stroke()
}

function paint() {
  // No background fill: let Live's strip color show through.

  var tr = trackBounds()
  var loX = valueToX(lo, tr)
  var hiX = valueToX(hi, tr)

  // Track sits in the UPPER half of the canvas; note-name labels go
  // BELOW each thumb so the readout reads like an axis tick. Track-y is
  // a fixed offset from canvas top, leaving the bottom section for
  // labels.
  var trackY = 10
  if (trackY < THUMB_RADIUS + 1) trackY = THUMB_RADIUS + 1

  // Inactive track (full width, dark grey).
  fillRect(tr.left, trackY - TRACK_HEIGHT / 2, tr.width, TRACK_HEIGHT, COL_TRACK_BG, 1)
  // Active segment [lo..hi] in Live blue accent. lo is at lower x
  // (left of canvas), hi is at higher x (right), so the active segment
  // spans loX..hiX.
  fillRect(loX, trackY - TRACK_HEIGHT / 2, hiX - loX, TRACK_HEIGHT, COL_TRACK_ACTIVE, 1)

  // Thumbs: cream fill + dark outline for definition against the dark
  // strip.
  fillCircle(loX, trackY, THUMB_RADIUS, COL_THUMB)
  strokeCircle(loX, trackY, THUMB_RADIUS, COL_THUMB_RING, 1.0)
  fillCircle(hiX, trackY, THUMB_RADIUS, COL_THUMB)
  strokeCircle(hiX, trackY, THUMB_RADIUS, COL_THUMB_RING, 1.0)

  // Note-name labels BELOW each thumb so the user sees the exact pitch
  // at a glance. Andale Mono 9pt is roughly 5.5 px/char; label width is
  // approximated for centering. When the two thumbs are close enough
  // for the labels to overlap, accept the overlap -- the user sees both
  // values stacked and the slider geometry stays predictable.
  var labelY = 26
  drawCenteredLabel(loX, labelY, midiToNoteName(lo))
  drawCenteredLabel(hiX, labelY, midiToNoteName(hi))
}

function drawCenteredLabel(centerX, baselineY, text) {
  // Andale Mono 9pt: roughly 5.5 px per glyph. Offset left by half the
  // estimated label width so the digit cluster reads as centered under
  // the thumb. Clamp to the canvas edges to keep characters from
  // running off when a thumb sits at MIN_VALUE / MAX_VALUE.
  var charWidth = 5.5
  var halfWidth = (text.length * charWidth) / 2
  var leftX = centerX - halfWidth
  var canvasW = box.rect[2] - box.rect[0]
  if (leftX < 0) leftX = 0
  if (leftX + halfWidth * 2 > canvasW) leftX = canvasW - halfWidth * 2
  mgraphics.set_source_rgba(COL_LABEL[0], COL_LABEL[1], COL_LABEL[2], 1)
  mgraphics.select_font_face('Andale Mono')
  mgraphics.set_font_size(9)
  mgraphics.move_to(leftX, baselineY)
  mgraphics.show_text(text)
}

// --- Mouse interaction ---

function onclick(x, y, button, cmd, shift, capslock, option, ctrl) {
  if (button !== 1) return
  if (cmd || shift || option || ctrl) return
  var tr = trackBounds()
  var pick = pickThumb(x, tr)
  if (pick < 0) return
  dragging = pick
  updateFromX(x, tr)
}

function ondrag(x, y, button, cmd, shift, capslock, option, ctrl) {
  if (dragging < 0) return
  if (!button) {
    dragging = -1
    mgraphics.redraw()
    return
  }
  if (cmd || shift || option || ctrl) return
  var tr = trackBounds()
  updateFromX(x, tr)
}

function updateFromX(x, tr) {
  var v = xToValue(x, tr)
  if (dragging === 0) {
    if (v > hi) {
      hi = v
      outlet(0, 'rangeHi', hi)
    }
    lo = v
    outlet(0, 'rangeLo', lo)
  } else if (dragging === 1) {
    if (v < lo) {
      lo = v
      outlet(0, 'rangeLo', lo)
    }
    hi = v
    outlet(0, 'rangeHi', hi)
  }
  mgraphics.redraw()
}
