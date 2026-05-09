// Stencil TM register-ring renderer (jsui).
// Spec: docs/ai/adr/003-m4l-ui-design.md "TM register ring".
// Reference: inboil's TuringSheet.svelte ring visualization.
//
// Revolver model (ADR 003, amended 2026-05-09): a fixed pointer
// triangle marks the read position at the top of the ring; the bit
// ring rotates CW by `cumulativeSteps * (2*PI/length)` so each shift
// carries the next bit under the pointer. CW mirrors inboil
// TuringSheet's positive `rotationDeg` applied via SVG transform:
// rotate() (positive deg = CW in screen coords); the original ADR 003
// said CCW with an "engine shift alignment" rationale, but engine
// shift direction (indices UP) is independent of visual rotation, and
// CW is the musically natural choice (clock-hand convention = time
// advancing). cumulativeSteps is host.position (monotonic counter,
// reset on transport start/stop and seed change), arriving via the
// bridge's `ringHead` outlet. The host also emits `triggerFlash 0|1`
// per step so the bit at the pointer flashes salmon precisely when
// the audible step fires.
//
// Pure layout & hit-test logic lives in m4l/host-tm/ui/registerRing.logic.ts
// (with unit tests). Max's [jsui] runs Max's bundled JS engine, not Node,
// so the formula is re-implemented here as plain JS rather than imported.
// Keep MIN_LENGTH / MAX_LENGTH / MAX_BIT_RADIUS / BIT_GAP / CANVAS_MARGIN /
// POINTER_GAP / POINTER_HALF_WIDTH / POINTER_HEIGHT in sync with
// registerRing.logic.ts. A drift test (registerRing.mirror.test.ts) asserts
// the constants line up.
//
// Comments and string literals are ASCII; non-ASCII glyphs are written as
// \uXXXX escapes -- Max's classic JS parser has been observed to choke on
// UTF-8 in source files (oedipa convention).

inlets = 1
outlets = 1

mgraphics.init()
mgraphics.relative_coords = 0
mgraphics.autofill = 0

post('registerRing.jsui.js loaded build=2026-05-09 revolver-cw+flash\n')

// --- Constants (mirror m4l/host-tm/ui/registerRing.logic.ts) ---

var MIN_LENGTH = 2
var MAX_LENGTH = 32
var MAX_BIT_RADIUS = 14
var BIT_GAP = 2
var CANVAS_MARGIN = 4
var POINTER_GAP = 4
var POINTER_HALF_WIDTH = 3
var POINTER_HEIGHT = 6

// --- Visual identity (sampled from inboil src/app.css) ---
//
// Exact hex values per ADR 003 "Visual identity". These are the inboil
// design tokens transcoded to 0..1 RGB for mgraphics. Update both the
// ADR and this block together if the palette ever changes.

var COL_BG          = [0.929, 0.910, 0.863] // #EDE8DC --color-bg     warm cream
var COL_FG          = [0.118, 0.125, 0.157] // #1E2028 --color-fg     dark navy (text)
var COL_ACTIVE_FILL = [0.471, 0.471, 0.271] // #787845 --color-olive  olive (active bits)
var COL_HIGHLIGHT   = [0.910, 0.627, 0.565] // #E8A090 --color-salmon salmon (read head)
// Inactive-bit outline: olive at 0.55 alpha. Inboil uses 0.35 but at
// inboil's larger scale (250x250 svg viewport) 0.35 reads fine; in the
// M4L 320x132 jsui the dots are smaller and 0.35 reads as nearly
// invisible. 0.55 keeps the same hue but with enough presence to be
// legible at the smaller scale.
var COL_OUTLINE     = [0.471, 0.471, 0.271] // olive base
var OUTLINE_ALPHA   = 0.55

// --- State ---
//
// bits: 0/1 array; length implies the current TM register length, replaced
// wholesale by the bridge's `register` outlet on every step and on length
// changes.
// cumulativeSteps: host.position (monotonic step counter, resets to 0 on
// transport start/stop and seed change). Drives the revolver rotation.
// Empty until the bridge's first `register` message arrives. paint()
// handles bits.length === 0 by drawing a DEFAULT_LENGTH-dot outline
// fallback so the device looks alive on first paint.

var bits = []
var cumulativeSteps = 0
var flashing = false

// --- Message dispatch ---
//
// register <bit0> <bit1> ... <bitN-1>   replace bits (Mode A: lifecycle only), redraw
// ringHead <n>                          set cumulativeSteps, redraw
// triggerFlash <0|1>                    set flash state, redraw
//
// Outlet symbol from the bridge is `ringHead` (NOT `position`): when a
// [jsui] inlet receives a message whose first symbol matches a Max
// box-level attribute name (`position` is one such reserved word), Max
// interprets it as a setter and shifts the box's screen position --
// observed empirically as a 1px-per-message creep in M4L locked view.
// `ringHead` is a domain-specific non-colliding name. Keep this in
// sync with bridge.ts's emitOutlet call and the patcher's
// [route ... ringHead] / [prepend ringHead] objects.

function anything() {
  var msg = messagename
  var args = arrayfromargs(arguments)
  if (msg === 'register') { setRegister(args); return }
  if (msg === 'ringHead') { setCumulativeSteps(args[0]); return }
  if (msg === 'triggerFlash') { setTriggerFlash(args[0]); return }
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

function setCumulativeSteps(n) {
  n = Number(n)
  if (!isFinite(n)) return
  cumulativeSteps = Math.floor(n)
  mgraphics.redraw()
}

function setTriggerFlash(v) {
  flashing = (Number(v) === 1)
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
  var arcHalf = (Math.PI * maxRadius) / Math.max(len, 4)
  var bitRadius = arcHalf - BIT_GAP
  if (bitRadius > MAX_BIT_RADIUS) bitRadius = MAX_BIT_RADIUS
  if (bitRadius < 1) bitRadius = 1
  var radius = maxRadius - bitRadius
  if (radius < 0) radius = 0
  return { cx: cx, cy: cy, radius: radius, bitRadius: bitRadius, length: len }
}

function bitPositionRotated(idx, g, steps) {
  var stepAngle = (Math.PI * 2) / g.length
  // CW rotation mirrors inboil TuringSheet's positive rotationDeg via SVG
  // transform: rotate() (ADR 003 TM register ring, amended 2026-05-09)
  var angle = (idx / g.length) * Math.PI * 2 - Math.PI / 2 + steps * stepAngle
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

function readingIndexAt(steps, len) {
  if (len <= 0) return -1
  var k = Math.floor(steps)
  // CW rotation: bit at top has logical index `(len - k) mod len`
  return (((len - k) % len) + len) % len
}

function hitTestRotated(x, y, g, steps) {
  var r2 = g.bitRadius * g.bitRadius
  for (var i = 0; i < g.length; i++) {
    var p = bitPositionRotated(i, g, steps)
    var dx = x - p.x
    var dy = y - p.y
    if (dx * dx + dy * dy <= r2) return i
  }
  return -1
}

// --- Drawing ---

function fillCircle(x, y, r, c) {
  mgraphics.set_source_rgba(c[0], c[1], c[2], 1)
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
  mgraphics.set_source_rgba(c[0], c[1], c[2], alpha)
  mgraphics.move_to(x1, y1)
  mgraphics.line_to(x2, y2)
  mgraphics.line_to(x3, y3)
  mgraphics.close_path()
  mgraphics.fill()
}

// Default ring length to draw before the bridge has emitted its first
// `register` message. Matches DEFAULT_PARAMS.length in host-tm/host.ts.
// Without this, a freshly-loaded device shows a blank cream rectangle
// until the first transport step (or any param dump) -- confusing.
var DEFAULT_LENGTH = 8

function paint() {
  var w = box.rect[2] - box.rect[0]
  var h = box.rect[3] - box.rect[1]

  // Background fill so the ring sits on the inboil cream rather than
  // Live's default device gray.
  mgraphics.set_source_rgba(COL_BG[0], COL_BG[1], COL_BG[2], 1)
  mgraphics.rectangle(0, 0, w, h)
  mgraphics.fill()

  // Empty state: draw an outlined ring at DEFAULT_LENGTH so the device
  // looks alive on first load. The bridge replaces this with the real
  // register on `ready` (see TmBridge constructor) within one event
  // loop tick; this is a fallback for the in-between frame.
  var len = bits.length > 0 ? bits.length : DEFAULT_LENGTH
  var g = computeGeometry(w, h, len)
  var steps = bits.length > 0 ? cumulativeSteps : 0
  var readIdx = bits.length > 0 ? readingIndexAt(steps, len) : -1

  for (var i = 0; i < len; i++) {
    var p = bitPositionRotated(i, g, steps)
    var isHead = (i === readIdx)
    var isOn = (bits.length > 0 && bits[i] === 1)

    // Always render the bit's on/off state so the user can see whether the
    // step at the read head triggers a note. Diverges from inboil's
    // .bit-reading (which forces hollow), because the trigger / no-trigger
    // distinction at the play-head is the most musically informative pixel
    // on the device and shouldn't be overdrawn.
    if (isOn) {
      fillCircle(p.x, p.y, g.bitRadius, COL_ACTIVE_FILL)
    } else {
      // Inactive: hollow olive at low alpha. 1.5px line matches inboil
      // `.bit-circle { stroke-width: 1.5 }`.
      strokeCircle(p.x, p.y, g.bitRadius, COL_OUTLINE, OUTLINE_ALPHA, 1.5)
    }

    // Read-head marker: salmon halo just outside the bit's outer edge,
    // overlayed on top of the on/off rendering above. Halo radius stays
    // within CANVAS_MARGIN so it never clips the canvas.
    if (isHead) {
      strokeCircle(p.x, p.y, g.bitRadius + 2, COL_HIGHLIGHT, 1.0, 2.0)
      // Active-step flash (ADR 003 Active-step flash): the bit at the
      // pointer is overlaid with a salmon disk inside the halo when the
      // host emits triggerFlash 1 (i.e., this step actually fired). The
      // user sees an instant on/off correlation between the visualization
      // and the audible event. Cleared on triggerFlash 0.
      if (flashing) {
        fillCircle(p.x, p.y, g.bitRadius * 0.5, COL_HIGHLIGHT)
      }
    }
  }

  // Fixed pointer triangle at top, pointing down toward the bit currently
  // at the read position. Drawn last so it overlays cleanly on the ring.
  var tip = pointerTip(g)
  fillTriangle(
    tip.x, tip.y + POINTER_HEIGHT,
    tip.x - POINTER_HALF_WIDTH, tip.y,
    tip.x + POINTER_HALF_WIDTH, tip.y,
    COL_FG, 0.5
  )
}

// --- Mouse interaction ---
//
// Single primary-button click toggles the bit at the cursor. Out-of-bound
// or modifier clicks are ignored -- modifiers reserved for future drag-paint
// (ADR 003 "Open questions"). Hit-test uses the rotated visual position so
// the click maps to the LOGICAL bit index the user pressed.

function onclick(x, y, button, cmd, shift, capslock, option, ctrl) {
  if (button !== 1) return
  if (cmd || shift || option || ctrl) return
  if (bits.length === 0) return

  var w = box.rect[2] - box.rect[0]
  var h = box.rect[3] - box.rect[1]
  var g = computeGeometry(w, h, bits.length)

  var idx = hitTestRotated(x, y, g, cumulativeSteps)
  if (idx < 0) return

  // Optimistic local toggle so the UI feels instant; the bridge's
  // re-emitted `register` will correct this within one round-trip if the
  // host disagrees (e.g. setBit ignored due to validation).
  var newValue = (bits[idx] === 1) ? 0 : 1
  bits[idx] = newValue
  mgraphics.redraw()
  outlet(0, 'setBit', idx, newValue)
}
