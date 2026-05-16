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

post('registerRing.jsui.js loaded build=2026-05-16 ableton-dark+static\n')

// --- Constants (mirror m4l/host/ui/registerRing.logic.ts) ---

var MIN_LENGTH = 2
var MAX_LENGTH = 32
var MAX_BIT_RADIUS = 14
var BIT_GAP = 2
var CANVAS_MARGIN = 4
var POINTER_GAP = 4
var POINTER_HALF_WIDTH = 3
var POINTER_HEIGHT = 6

// --- Palette: Live M4L device-strip dark + dual accent ---
//
// Background is left transparent so Live's strip color shows through.
// Active bits use Live's blue accent; the read-head (trigger position
// at the top of the ring) uses Live's orange so the bit currently
// being played stands out from the rest of the active pattern.
// Inactive bits get a dim grey outline.

var COL_ACTIVE_FILL = [0.28, 0.63, 0.92]  // Live blue accent (active bits)
var COL_OUTLINE     = [0.55, 0.55, 0.55]  // mid grey (inactive outline)
var OUTLINE_ALPHA   = 0.85
var COL_READHEAD    = [1.00, 0.66, 0.20]  // Live orange (trigger / read-head)
var COL_POINTER     = [0.85, 0.85, 0.85]  // playhead triangle

// --- State ---

var bits = []

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
