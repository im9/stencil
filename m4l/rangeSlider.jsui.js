// Stencil TM range-slider jsui.
// Spec: m4l/host/ui/rangeSlider.logic.ts.
//
// Two-thumb VERTICAL MIDI-range slider, ~56x148 inside a bpatcher. HI
// thumb at top, LO at bottom -- pitch=up intuition (higher MIDI note
// number = visually higher on screen). The slider sits next to the
// register ring; same vertical extent (148 px) so the two read as a
// "bits -> note range" pair in one visual unit.
//
// Drives rangeLo / rangeHi (MIDI notes 0..127). The lo <= hi invariant
// is enforced inside the widget: dragging LO above HI pulls HI along,
// and vice versa. The actual live.dial-rangeLo / -rangeHi widgets stay
// off-presentation in Stencil.maxpat as the source-of-truth for Live's
// parameter system; this jsui is the user-facing control on top.
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

post('rangeSlider.jsui.js loaded build=2026-05-17\n')

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
  var top = TRACK_PAD
  var bottom = h - TRACK_PAD
  if (bottom < top) bottom = top
  return { top: top, bottom: bottom, cx: w / 2, height: bottom - top }
}

function valueToY(value, tr) {
  var v = Number(value)
  if (!isFinite(v) || v < MIN_VALUE) v = MIN_VALUE
  else if (v > MAX_VALUE) v = MAX_VALUE
  var t = (v - MIN_VALUE) / (MAX_VALUE - MIN_VALUE)
  return tr.bottom - t * tr.height
}

function yToValue(y, tr) {
  if (tr.height <= 0) return MIN_VALUE
  var t = (tr.bottom - y) / tr.height
  if (t < 0) t = 0
  if (t > 1) t = 1
  return Math.round(MIN_VALUE + t * (MAX_VALUE - MIN_VALUE))
}

function pickThumb(y, tr) {
  var loY = valueToY(lo, tr)
  var hiY = valueToY(hi, tr)
  var dLo = Math.abs(y - loY)
  var dHi = Math.abs(y - hiY)
  var threshold = THUMB_RADIUS * HIT_RADIUS_MULT
  if (dLo > threshold && dHi > threshold) return -1
  return dLo <= dHi ? 0 : 1
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
  var loY = valueToY(lo, tr)
  var hiY = valueToY(hi, tr)

  // Track sits on the LEFT half of the canvas; numeric value labels go
  // to the RIGHT of the track. Track-x is offset from canvas center
  // toward the left so labels have room without clipping the right
  // edge.
  var trackX = 12
  if (trackX < THUMB_RADIUS + 1) trackX = THUMB_RADIUS + 1

  // Inactive track (full height, dark grey).
  fillRect(trackX - TRACK_HEIGHT / 2, tr.top, TRACK_HEIGHT, tr.height, COL_TRACK_BG, 1)
  // Active segment [lo..hi] in Live blue accent. hi is at lower y
  // (top of canvas), lo is at higher y (bottom), so the active segment
  // spans hiY..loY.
  fillRect(trackX - TRACK_HEIGHT / 2, hiY, TRACK_HEIGHT, loY - hiY, COL_TRACK_ACTIVE, 1)

  // Thumbs: cream fill + dark outline for definition against the dark
  // strip.
  fillCircle(trackX, loY, THUMB_RADIUS, COL_THUMB)
  strokeCircle(trackX, loY, THUMB_RADIUS, COL_THUMB_RING, 1.0)
  fillCircle(trackX, hiY, THUMB_RADIUS, COL_THUMB)
  strokeCircle(trackX, hiY, THUMB_RADIUS, COL_THUMB_RING, 1.0)

  // Numeric value labels to the RIGHT of each thumb so the user sees
  // the exact MIDI note at a glance. Andale Mono 9pt is roughly
  // 5.5 px/char; the label is aligned to a fixed x so HI and LO line
  // up vertically.
  var labelX = trackX + THUMB_RADIUS + 4
  drawLabel(labelX, hiY, String(hi))
  drawLabel(labelX, loY, String(lo))
}

function drawLabel(leftX, centerY, text) {
  // 9pt baseline is roughly 3 px below the visual center -- the
  // baseline correction lines the digits up with the thumb center.
  var baselineY = centerY + 3
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
  var pick = pickThumb(y, tr)
  if (pick < 0) return
  dragging = pick
  updateFromY(y, tr)
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
  updateFromY(y, tr)
}

function updateFromY(y, tr) {
  var v = yToValue(y, tr)
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
