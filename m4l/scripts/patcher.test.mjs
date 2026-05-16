// Consistency + guard tests for Stencil.maxpat. Encodes ADR 003
// §Stencil patcher checklist and ADR 004 §Guard tests. Hand-written
// .maxpat JSON is easy to drift; this suite catches the cases Live's
// own loader would surface as silent no-wires or "Node script not
// ready".
//
// What's covered:
//   ADR 004 guards:
//     - abs-path scrub (no /Users/, /home/, drive letters)
//     - external-file resolution (every `filename` field is a real
//       sibling file under m4l/)
//     - structural sanity (parses, has patcher.boxes / patcher.lines)
//   ADR 003 §Stencil patcher checklist:
//     - devicewidth = 1000, openinpresentation = 1, presentation height ~180
//     - header band (no in-strip product banner; no "im9" byline)
//     - [jsui] referencing registerRing.jsui.js (flat, m4l/ root)
//     - [node.script stencil.mjs ...] present (flat path, m4l/ root)
//     - live.* widgets per ADR 002 §live.* parameter surface:
//       longname / shortname / parameter_type / mmin / mmax / initial
//     - every live.* change fires `setParam <bridgeKey>` to node.script
//     - register / position outlets route from node.script to jsui
//     - jsui setBit outlet routes to node.script
//     - midiin / midiparse / noteout for MIDI I/O
//     - transport-driven `step` path
//
// Out-of-scope (manual): visual quality, host-loading behavior in Live,
// font / palette correctness, audio output. These are pre-release manual
// checks per CLAUDE.md "GUI / UI components".

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const M4L_ROOT = resolve(__dirname, '..')

const MAXPAT = resolve(M4L_ROOT, 'Stencil.maxpat')

// ---- helpers ------------------------------------------------------------

function loadPatcher(path) {
  const raw = readFileSync(path, 'utf8')
  const parsed = JSON.parse(raw)
  return { raw, parsed, boxes: parsed.patcher?.boxes ?? [], lines: parsed.patcher?.lines ?? [] }
}

function boxesByMaxclass(boxes, cls) {
  return boxes.filter((b) => b.box?.maxclass === cls)
}

function findLiveWidget(boxes, longname) {
  // ADR 002 §live.* parameter surface: every widget is identified by
  // its parameter_longname (the symbol Live shows in the parameter list).
  // Searching by longname is more stable than by box id since ids are
  // arbitrary while the longname is a public API.
  return boxes.find(
    (b) =>
      b.box?.saved_attribute_attributes?.valueof?.parameter_longname === longname,
  )
}

function widgetParamAttrs(box) {
  return box.box.saved_attribute_attributes.valueof
}

function findPrependBox(boxes, prefix) {
  // `[newobj] prepend <prefix>` — patcher-side message factory. We search
  // by exact `text` so a typo (`setParams` vs `setParam`) shows up as a
  // missing-prepend rather than a silent no-op in Live.
  return boxes.find(
    (b) => b.box?.maxclass === 'newobj' && b.box?.text === `prepend ${prefix}`,
  )
}

function followsLineFromTo(lines, srcId, dstId) {
  // Direct edge srcId -> dstId on any inlet/outlet. Order-tolerant: we
  // care that the wire exists, not which port number is used (port nums
  // would be redundant with the box's own outlettype declaration).
  return lines.some(
    (l) =>
      l.patchline?.source?.[0] === srcId && l.patchline?.destination?.[0] === dstId,
  )
}

// Reachability with bounded depth — for "live.numbox -> ... -> node.script"
// where intermediate nodes (prepend, sel, route) aren't important. Uses
// BFS; depth cap prevents pathological cycles from looping forever.
function reachable(lines, srcId, dstId, maxDepth = 6) {
  if (srcId === dstId) return true
  const visited = new Set([srcId])
  let frontier = [srcId]
  for (let d = 0; d < maxDepth; d++) {
    const next = []
    for (const id of frontier) {
      for (const l of lines) {
        if (l.patchline?.source?.[0] !== id) continue
        const to = l.patchline?.destination?.[0]
        if (!to) continue
        if (to === dstId) return true
        if (!visited.has(to)) {
          visited.add(to)
          next.push(to)
        }
      }
    }
    frontier = next
  }
  return false
}

// ---- ADR 002 §live.* parameter surface ---------------------------------

// Source of truth: ADR 002 §Stencil TM live.* parameter table. The
// fields here mirror what Live persists into preset chunks. If ADR 002
// changes any of these the test will fail — that's the point: the
// patcher cannot drift from the spec without an explicit update here.
const LIVE_PARAMS = [
  // longname,             shortname, bridgeKey,         type, mmin, mmax, initial
  ['StencilTmLength',      'LEN',     'length',          1, 2,    32,         8],
  ['StencilTmLock',        'LOCK',    'lock',            0, 0,    1,          0.5],
  ['StencilTmRangeLo',     'LO',      'rangeLo',         1, 0,    127,        48],
  ['StencilTmRangeHi',     'HI',      'rangeHi',         1, 0,    127,        72],
  ['StencilTmDensity',     'DENS',    'density',         0, 0,    1,          1],
  // SEED is parameter_type 0 (Float) despite being a conceptually-integer
  // seed because M4L's Int parameter type is capped at 0..255 (256 values)
  // regardless of mmax — Cycling74 docs recommend Float storage + Int
  // unit style for integer ranges beyond 0..255. numdecimalplaces:0 +
  // parameter_unitstyle:0 (Int) on the numbox keep the display integer.
  ['StencilTmSeed',        'SEED',    'seed',            0, 0,    65535,      42],
  ['StencilTmOutputVelocity','VEL',   'outputVelocity',  1, 1,    127,        100],
  ['StencilTmOutputGate',  'GATE',    'outputGate',      0, 0,    1,          0.5],
]
// Enum widgets (live.menu) carry parameter_enum instead of mmin/mmax.
// Initial is the index into the enum; the bridge expects the string value.
// The earlier `StencilTmMode` (note / gate / velocity output dispatch)
// was removed per the vst spec (2026-05-15) — a MIDI note carries pitch,
// velocity, and gate simultaneously, so the mode dispatch was modeling
// the attributes as exclusive when they aren't. m4l caught up to the
// single-dispatch spec on 2026-05-16.
const LIVE_ENUMS = [
  // longname,                shortname, bridgeKey,      enumStrings,                                                                                                                                          initialIdx
  ['StencilTmSubdivision',    'Subdiv',  'subdivision',  ['8th', '16th', '32nd', '8T', '16T'],                                                                                                                  1],
  ['StencilTmTriggerMode',    'Trig',    'triggerMode',  ['auto', 'gate', 'seed'],                                                                                                                              0],
  // IN-CH / OUT-CH are pulldowns (live.menu) per Ableton MIDI-channel
  // convention. IN-CH enum index = MIDI channel directly (0=OMNI, 1..16);
  // bridge accepts 0..16 verbatim, no transform. OUT-CH enum starts at
  // "1" (index 0 = channel 1); a `[+ 1]` box converts the menu's
  // 0..15 index to 1..16 before the prep-outputChannel chain.
  ['StencilTmInputChannel',   'IN-CH',   'inputChannel', ['OMNI', '1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16'],                                                       0],
  ['StencilTmOutputChannel',  'OUT-CH',  'outputChannel',['1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16'],                                                                0],
]

// ---- ADR 004 §Guard tests ----------------------------------------------

test('.maxpat parses as JSON', () => {
  // Hand-written JSON is easy to break with a stray comma. Catch the
  // parse failure here rather than at bake time (where the error
  // message is less precise about line/column).
  assert.doesNotThrow(() => loadPatcher(MAXPAT))
})

test('patcher.boxes and patcher.lines are arrays', () => {
  const { boxes, lines } = loadPatcher(MAXPAT)
  assert.ok(Array.isArray(boxes), 'boxes')
  assert.ok(Array.isArray(lines), 'lines')
})

test('abs-path scrub (no /Users/, /home/, drive letter)', () => {
  // ADR 004 §Guard tests #1. A leaked absolute path would make the
  // .amxd loadable on the build machine only — the export reviewer
  // would never spot it.
  const { raw } = loadPatcher(MAXPAT)
  const FORBIDDEN = [/\/Users\//, /\/home\//, /[A-Za-z]:\\/]
  for (const re of FORBIDDEN) {
    assert.ok(!re.test(raw), `forbidden absolute path matched ${re}`)
  }
})

test('every box.filename resolves as a sibling file', () => {
  // ADR 004 §Guard tests #2. Both [jsui] and [node.script @file ...]
  // (when written that way) pull a filename, plus any future bpatcher.
  // Walk every box; for each `filename` (jsui, node.script with @file,
  // ...) or `name` (bpatcher) attribute, assert the file exists
  // relative to m4l/. Catches typos (jsui.js vs .jsui.js) and missed
  // renames after a directory move.
  const { boxes } = loadPatcher(MAXPAT)
  for (const b of boxes) {
    const fn = b.box?.filename ?? (b.box?.maxclass === 'bpatcher' ? b.box?.name : undefined)
    if (typeof fn !== 'string') continue
    const resolved = resolve(M4L_ROOT, fn)
    assert.ok(
      existsSync(resolved),
      `referenced file does not exist: ${fn} (resolved to ${resolved})`,
    )
  }
})

test('every patchline source/destination id resolves to a known box', () => {
  // A typo in a patchline endpoint silently drops the wire in Live.
  // Catch it here.
  const { boxes, lines } = loadPatcher(MAXPAT)
  const ids = new Set(boxes.map((b) => b.box?.id).filter(Boolean))
  for (const l of lines) {
    const src = l.patchline?.source?.[0]
    const dst = l.patchline?.destination?.[0]
    assert.ok(ids.has(src), `unknown patchline source id: ${src}`)
    assert.ok(ids.has(dst), `unknown patchline destination id: ${dst}`)
  }
})

// ---- ADR 003 §Stencil patcher ------------------------------------------

test('devicewidth = 592 and openinpresentation = 1', () => {
  // 4 vertical sections separated by sub-pixel jsui dividers (no
  // rectangular panel frames). Panel 2 holds just the register ring;
  // RANGE slider sits in panel 3 (Output) alongside VEL / GATE /
  // SUBDIV. Panel 1 (parameters) tightened to 12 px internal padding
  // each side.
  const { parsed } = loadPatcher(MAXPAT)
  assert.equal(parsed.patcher.devicewidth, 584)
  assert.equal(parsed.patcher.openinpresentation, 1)
})

test('no in-strip product banner + no "im9" byline', () => {
  // ADR 003 §Visual identity §Device chrome: NO in-strip device-name
  // banner and NO `im9` byline. Live's device-strip header already
  // labels the device with "Stencil" and shows the author in its own
  // metadata; duplicating that inside the presentation strip adds
  // clutter at small sizes.
  const { boxes } = loadPatcher(MAXPAT)
  const comments = boxesByMaxclass(boxes, 'comment').map((b) => b.box.text)
  assert.ok(!comments.some((t) => /STENCIL/i.test(t)), 'no STENCIL banner')
  assert.ok(!comments.some((t) => /\bim9\b/.test(t)), 'no im9 byline')
})

test('no fieldset name labels (panels group by border only)', () => {
  // The 4 panel borders group widgets visually; explicit text labels
  // ("Parameters" / "Register" / "Output" / "Trigger") were dropped to
  // reduce visual noise — the widget shortnames inside each panel make
  // the grouping self-evident.
  const { boxes } = loadPatcher(MAXPAT)
  const comments = boxesByMaxclass(boxes, 'comment').map((b) => b.box.text)
  for (const t of ['Parameters', 'Register', 'Output', 'Trigger']) {
    assert.ok(
      !comments.some((c) => c === t),
      `comment "${t}" should not be present (fieldset names removed)`,
    )
  }
})

test('[bpatcher] wraps the ring sub-patcher', () => {
  // The TM register ring lives inside a sub-patcher
  // (registerRing.subpatcher.maxpat) wrapped by a [bpatcher] in the
  // main patcher. The wrapping isolates the [jsui]'s box.rect drift
  // (Max M4L re-anchors the [jsui]'s canvas to presentation origin
  // x=0 on the first inlet message) inside the sub-patcher's own
  // coordinate system, so the parent's bpatcher position stays
  // stable regardless. See ADR 003 §TM register ring + ADR 004
  // §Patcher path conventions.
  const { boxes } = loadPatcher(MAXPAT)
  const bpatchers = boxesByMaxclass(boxes, 'bpatcher')
  assert.ok(
    bpatchers.some((b) => b.box.name === 'registerRing.subpatcher.maxpat'),
    'expected bpatcher wrapping registerRing.subpatcher.maxpat',
  )
})

test('registerRing.subpatcher.maxpat parses and contains the [jsui]', () => {
  // The sub-patcher MUST exist as a sibling file at m4l/ root and must
  // contain a [jsui] referencing registerRing.jsui.js (also at flat
  // root, per the same Max filename-resolution constraint).
  const subpatcherPath = resolve(M4L_ROOT, 'registerRing.subpatcher.maxpat')
  assert.ok(existsSync(subpatcherPath), 'registerRing.subpatcher.maxpat missing')
  const sub = loadPatcher(subpatcherPath)
  const subJsuis = sub.boxes.filter((b) => b.box?.maxclass === 'jsui')
  assert.ok(
    subJsuis.some((b) => b.box.filename === 'registerRing.jsui.js'),
    'sub-patcher must contain a jsui referencing registerRing.jsui.js',
  )
})

test('[bpatcher] wraps the range-slider sub-patcher', () => {
  // The LO/HI MIDI-range control is a 2-thumb slider implemented as a
  // jsui (rangeSlider.jsui.js), wrapped by a sub-patcher and bpatcher
  // for the same box.rect-anchor isolation reason as the register ring.
  const { boxes } = loadPatcher(MAXPAT)
  const bpatchers = boxesByMaxclass(boxes, 'bpatcher')
  assert.ok(
    bpatchers.some((b) => b.box.name === 'rangeSlider.subpatcher.maxpat'),
    'expected bpatcher wrapping rangeSlider.subpatcher.maxpat',
  )
})

test('rangeSlider.subpatcher.maxpat parses and contains the [jsui]', () => {
  const subpatcherPath = resolve(M4L_ROOT, 'rangeSlider.subpatcher.maxpat')
  assert.ok(existsSync(subpatcherPath), 'rangeSlider.subpatcher.maxpat missing')
  const sub = loadPatcher(subpatcherPath)
  const subJsuis = sub.boxes.filter((b) => b.box?.maxclass === 'jsui')
  assert.ok(
    subJsuis.some((b) => b.box.filename === 'rangeSlider.jsui.js'),
    'sub-patcher must contain a jsui referencing rangeSlider.jsui.js',
  )
})

test('range-slider bpatcher round-trips with the live.dial-rangeLo/Hi widgets', () => {
  // Wiring contract:
  //   bpatcher out -> [route rangeLo rangeHi] -> live.dial-rangeLo/Hi
  //     (bare int sets value + triggers the existing setParam chain).
  //   live.dial-rangeLo/Hi out -> [prepend setLo|setHi] -> bpatcher in
  //     (display sync; jsui only emits on user drag, no feedback loop).
  const { boxes, lines } = loadPatcher(MAXPAT)
  const bp = boxesByMaxclass(boxes, 'bpatcher').find(
    (b) => b.box.name === 'rangeSlider.subpatcher.maxpat',
  )
  assert.ok(bp, 'rangeSlider bpatcher missing')
  const dialLo = boxes.find(
    (b) => b.box?.saved_attribute_attributes?.valueof?.parameter_longname === 'StencilTmRangeLo',
  )
  const dialHi = boxes.find(
    (b) => b.box?.saved_attribute_attributes?.valueof?.parameter_longname === 'StencilTmRangeHi',
  )
  assert.ok(dialLo && dialHi, 'live.dial-rangeLo / rangeHi missing')
  assert.ok(reachable(lines, bp.box.id, dialLo.box.id),
    'bpatcher -> ... -> live.dial-rangeLo chain missing')
  assert.ok(reachable(lines, bp.box.id, dialHi.box.id),
    'bpatcher -> ... -> live.dial-rangeHi chain missing')
  assert.ok(reachable(lines, dialLo.box.id, bp.box.id),
    'live.dial-rangeLo -> ... -> bpatcher chain missing')
  assert.ok(reachable(lines, dialHi.box.id, bp.box.id),
    'live.dial-rangeHi -> ... -> bpatcher chain missing')
})

test('rangeLo / rangeHi live.dial widgets stay off-presentation', () => {
  // The slider bpatcher is the user-facing control; the underlying
  // live.dial widgets exist only as the source-of-truth for Live's
  // parameter system. They must NOT appear in the device strip --
  // otherwise the user sees duplicated controls. Drift here means an
  // accidental `presentation: 1` on either dial.
  const { boxes } = loadPatcher(MAXPAT)
  for (const longname of ['StencilTmRangeLo', 'StencilTmRangeHi']) {
    const w = boxes.find(
      (b) => b.box?.saved_attribute_attributes?.valueof?.parameter_longname === longname,
    )
    assert.ok(w, `${longname} missing`)
    assert.ok(!w.box.presentation,
      `${longname} must NOT have presentation: 1 (slider is the user control)`)
  }
})

test('RND button rerolls the seed via [random] into live.numbox-seed', () => {
  // SEED parameter range is 0..65535 (16-bit) -- vst spec is 0..2^31-1
  // but M4L's parameter_type:1 (Int) is capped at 0..255 (256 values)
  // regardless of mmax, so the SEED numbox uses parameter_type:0 (Float)
  // with numdecimalplaces:0 + parameter_unitstyle:0 (Int) to display as
  // an integer while storing 16-bit values internally (Cycling74 doc
  // recommendation). 65535 gives 64K unique loop patterns, plenty for
  // musical use. The RND button bangs a [random 65536] into the seed
  // numbox; the numbox's outlet carries the new value through the
  // existing prep-seed chain to the host.
  const { boxes, lines } = loadPatcher(MAXPAT)
  const rndBtn = boxes.find((b) => b.box?.id === 'obj-rnd-btn')
  assert.ok(rndBtn, 'RND button missing')
  // live.button (not live.text) — its rendering is a clean 18x18 dot
  // that vertically centers on the adjacent live.numbox-seed without
  // the font-baseline offset live.text introduces.
  assert.equal(rndBtn.box.maxclass, 'live.button')
  const rndGen = boxes.find((b) => b.box?.text && /^random\s+65536/.test(b.box.text))
  assert.ok(rndGen, '[random 65536] box missing')
  const seedNum = boxes.find(
    (b) => b.box?.saved_attribute_attributes?.valueof?.parameter_longname === 'StencilTmSeed',
  )
  assert.ok(seedNum, 'seed numbox missing')
  // RND btn -> random -> seed numbox -> prep-seed -> node.script
  assert.ok(followsLineFromTo(lines, rndBtn.box.id, rndGen.box.id),
    'RND button -> [random] wire missing')
  assert.ok(followsLineFromTo(lines, rndGen.box.id, seedNum.box.id),
    '[random] -> live.numbox-seed wire missing')
  const nodescript = boxesByMaxclass(boxes, 'newobj').find((b) =>
    /^node\.script\s+stencil\.mjs\b/.test(b.box.text),
  )
  assert.ok(reachable(lines, seedNum.box.id, nodescript.box.id),
    'seed numbox -> ... -> node.script chain broken')
})

test('[node.script] references stencil.mjs (flat path)', () => {
  // Flat path at m4l/ root because Max [node.script]'s `filename`
  // attribute does not reliably resolve subdirectory paths in M4L
  // presentation view — observed empirically as "No such file or
  // directory" in Max log when filename was `host/index.mjs`. Same
  // constraint as Max [jsui]. (See ADR 004 §Patcher path conventions.)
  //
  // `.mjs` (not `.js`) per the load-bearing comment in m4l/stencil.mjs:
  // ".mjs is unconditionally ESM; .js would default to CJS in
  // [node.script]'s tempdir and fail to parse the import."
  const { boxes } = loadPatcher(MAXPAT)
  const newobjs = boxesByMaxclass(boxes, 'newobj')
  assert.ok(
    newobjs.some((b) => /^node\.script\s+stencil\.mjs\b/.test(b.box.text)),
    'expected node.script referencing stencil.mjs',
  )
})

test('has midiin / midiparse / noteout for MIDI I/O', () => {
  // ADR 003 §Stencil patcher: midiin -> noteIn/noteOff routing,
  // Max.outlet -> noteout. We assert the three required objects exist;
  // wiring between them is a manual visual check.
  const { boxes } = loadPatcher(MAXPAT)
  const newobjs = boxesByMaxclass(boxes, 'newobj').map((b) => b.box.text)
  assert.ok(newobjs.some((t) => /^midiin\b/.test(t)), 'midiin')
  assert.ok(newobjs.some((t) => /^midiparse\b/.test(t)), 'midiparse')
  assert.ok(newobjs.some((t) => /^(noteout|midiout)\b/.test(t)), 'noteout/midiout')
})

test('has transport / metro / counter / step path', () => {
  // ADR 002 §Patcher: "[transport] object emits a tick on each
  // subdivision step; routed to [node.script] via a `step` message".
  // Real implementation uses metro + counter + prepend step (oedipa
  // pattern) since [transport] alone doesn't generate per-step ticks.
  const { boxes } = loadPatcher(MAXPAT)
  const newobjs = boxesByMaxclass(boxes, 'newobj').map((b) => b.box.text)
  assert.ok(newobjs.some((t) => /^q?metro\b/.test(t)), 'metro / qmetro')
  assert.ok(newobjs.some((t) => /^counter\b/.test(t)), 'counter')
  assert.ok(newobjs.some((t) => /^prepend\s+step\b/.test(t)), 'prepend step')
})

for (const [longname, shortname, bridgeKey, type, mmin, mmax, initial] of LIVE_PARAMS) {
  test(`live.* widget ${longname} matches ADR 002 spec`, () => {
    // Per-row check against ADR 002. We verify enough of the widget's
    // parameter attrs that a hand-edit slip (wrong range, wrong type)
    // is caught before reaching Live's parameter list.
    const { boxes } = loadPatcher(MAXPAT)
    const w = findLiveWidget(boxes, longname)
    assert.ok(w, `widget ${longname} missing`)
    const attrs = widgetParamAttrs(w)
    assert.equal(attrs.parameter_shortname, shortname, 'shortname')
    assert.equal(attrs.parameter_type, type, 'parameter_type')
    assert.equal(attrs.parameter_mmin, mmin, 'mmin')
    assert.equal(attrs.parameter_mmax, mmax, 'mmax')
    // parameter_initial is an array (Max convention); compare first elem.
    assert.equal(attrs.parameter_initial[0], initial, 'initial')
  })

  test(`${longname} change fires setParam ${bridgeKey} to node.script`, () => {
    // Wiring: widget outlet -> [prepend setParam <bridgeKey>] -> node.script.
    // The bridge accepts only the keys listed in LIVE_PARAMS / LIVE_ENUMS
    // (see host/bridge.ts setParam switch); a missing/typo prepend would
    // silently drop the param update.
    const { boxes, lines } = loadPatcher(MAXPAT)
    const w = findLiveWidget(boxes, longname)
    const prep = findPrependBox(boxes, `setParam ${bridgeKey}`)
    assert.ok(prep, `missing [prepend setParam ${bridgeKey}]`)
    assert.ok(
      followsLineFromTo(lines, w.box.id, prep.box.id),
      `${w.box.id} -> ${prep.box.id} wire missing`,
    )
    const nodescript = boxesByMaxclass(boxes, 'newobj').find((b) =>
      /^node\.script\s+stencil\.mjs\b/.test(b.box.text),
    )
    assert.ok(
      followsLineFromTo(lines, prep.box.id, nodescript.box.id),
      `${prep.box.id} -> node.script wire missing`,
    )
  })
}

for (const [longname, shortname, bridgeKey, enumValues, initialIdx] of LIVE_ENUMS) {
  test(`enum widget ${longname} matches ADR 002 spec`, () => {
    // Enum widgets (live.menu) are validated separately because they
    // use parameter_enum / parameter_type=2 instead of mmin/mmax.
    const { boxes } = loadPatcher(MAXPAT)
    const w = findLiveWidget(boxes, longname)
    assert.ok(w, `widget ${longname} missing`)
    const attrs = widgetParamAttrs(w)
    assert.equal(attrs.parameter_shortname, shortname, 'shortname')
    assert.equal(attrs.parameter_type, 2, 'parameter_type=2 (enum)')
    assert.deepEqual(attrs.parameter_enum, enumValues, 'enum values')
    assert.equal(attrs.parameter_initial[0], initialIdx, 'initial index')
  })

  // String-enum menus (subdivision, triggerMode) fan the index into
  // discrete `setParam <key> <enumValue>` messages via [sel]; that's
  // checked here. Numeric-enum menus (inputChannel, outputChannel)
  // pipe their index straight to a prep-* via the int outlet
  // (outputChannel adds 1 first to bridge the 0..15 -> 1..16 gap), so
  // the per-value-message check doesn't apply.
  const stringEnumKeys = new Set(['subdivision', 'triggerMode'])
  if (stringEnumKeys.has(bridgeKey)) {
    test(`${longname} dispatches one [message setParam ${bridgeKey} <enum>] per value`, () => {
      const { boxes } = loadPatcher(MAXPAT)
      const messages = boxesByMaxclass(boxes, 'message').map((b) => b.box.text)
      for (const v of enumValues) {
        const expected = `setParam ${bridgeKey} ${v}`
        assert.ok(
          messages.includes(expected),
          `missing message: "${expected}"`,
        )
      }
    })
  } else {
    test(`${longname} pipes menu index to [prepend setParam ${bridgeKey}]`, () => {
      // Numeric MIDI-channel menus skip the per-value message bank --
      // the index IS the value (for inputChannel where index 0 = OMNI
      // = MIDI ch 0) or the value is index+1 (for outputChannel where
      // index 0 = "1" = MIDI ch 1). We assert the widget reaches the
      // prep-* box, leaving the optional [+ 1] adjustment between
      // them out of scope for the wiring check.
      const { boxes, lines } = loadPatcher(MAXPAT)
      const w = findLiveWidget(boxes, longname)
      const prep = findPrependBox(boxes, `setParam ${bridgeKey}`)
      assert.ok(prep, `missing [prepend setParam ${bridgeKey}]`)
      assert.ok(
        reachable(lines, w.box.id, prep.box.id),
        `${longname} -> [prepend setParam ${bridgeKey}] chain missing`,
      )
    })
  }
}

// ---- subdivision → qmetro reconfig ------------------------------------

// Each subdivision enum value maps to a Max time-value symbol (qmetro
// arg). Without this routing the menu is a dead control: it updates
// host state but the qmetro keeps its hardcoded 16n rate. Both
// `interval` and `quantize` are reconfigured together so the metro
// fires on the matching grid (quantize=16n while interval=32n would
// align fast triggers to slow grid boundaries). Map per Max docs:
// https://docs.cycling74.com/max8/vignettes/maxtime_syntax
const SUBDIVISION_TO_METRO = {
  '8th': '8n',
  '16th': '16n',
  '32nd': '32n',
  '8T': '8nt',
  '16T': '16nt',
}

test('subdivision menu reconfigures qmetro interval + quantize per enum value', () => {
  const { boxes, lines } = loadPatcher(MAXPAT)
  const messages = boxesByMaxclass(boxes, 'message')
  const metro = boxesByMaxclass(boxes, 'newobj').find((b) =>
    /^q?metro\b/.test(b.box.text),
  )
  assert.ok(metro, 'metro / qmetro box missing')
  for (const [enumValue, metroSym] of Object.entries(SUBDIVISION_TO_METRO)) {
    const expected = `interval ${metroSym}, quantize ${metroSym}`
    const msg = messages.find((b) => b.box.text === expected)
    assert.ok(msg, `missing message: "${expected}" (for subdivision ${enumValue})`)
    assert.ok(
      followsLineFromTo(lines, msg.box.id, metro.box.id),
      `${msg.box.id} ("${expected}") -> ${metro.box.id} (qmetro) wire missing`,
    )
  }
})

test('all live.* parameters per LIVE_PARAMS + LIVE_ENUMS are present (no extras, no missing)', () => {
  // Cross-check the per-widget tests with a count assertion. Catches a
  // silent duplicate longname or an extra unrelated live.* widget that
  // would inflate the parameter list shown to the user in Live.
  const { boxes } = loadPatcher(MAXPAT)
  const liveWidgets = boxes.filter((b) => {
    const cls = b.box?.maxclass
    return (cls === 'live.numbox' || cls === 'live.dial' || cls === 'live.slider' || cls === 'live.menu')
      && b.box?.saved_attribute_attributes?.valueof?.parameter_longname?.startsWith('StencilTm')
  })
  const expected = LIVE_PARAMS.length + LIVE_ENUMS.length
  assert.equal(liveWidgets.length, expected, `expected ${expected} live.* widgets`)
})

test('register / position outlets route from node.script to ring bpatcher', () => {
  // Bridge emits Max.outlet("register", ...) / Max.outlet("position", n).
  // Routing chain: [node.script] -> [route note ready register position]
  // -> [deferlow] -> [prepend register]/[prepend position] ->
  // [bpatcher registerRing.subpatcher.maxpat] (which forwards into its
  // inner [jsui]). We verify reachability rather than the exact
  // intermediate nodes.
  const { boxes, lines } = loadPatcher(MAXPAT)
  const nodescript = boxesByMaxclass(boxes, 'newobj').find((b) =>
    /^node\.script\s+stencil\.mjs\b/.test(b.box.text),
  )
  const bpatcher = boxesByMaxclass(boxes, 'bpatcher').find(
    (b) => b.box.name === 'registerRing.subpatcher.maxpat',
  )
  assert.ok(
    reachable(lines, nodescript.box.id, bpatcher.box.id),
    'node.script -> ... -> bpatcher chain missing',
  )
})

test('bpatcher setBit outlet routes to node.script (ring click handler)', () => {
  // ADR 003 ring click -> setBit. Outlet from sub-patcher's [jsui] is
  // exposed via the sub-patcher's [outlet], surfaced as the bpatcher's
  // outlet 0 in the parent, then flows through into [node.script] for
  // the host's setBit handler.
  const { boxes, lines } = loadPatcher(MAXPAT)
  const bpatcher = boxesByMaxclass(boxes, 'bpatcher').find(
    (b) => b.box.name === 'registerRing.subpatcher.maxpat',
  )
  const nodescript = boxesByMaxclass(boxes, 'newobj').find((b) =>
    /^node\.script\s+stencil\.mjs\b/.test(b.box.text),
  )
  assert.ok(
    reachable(lines, bpatcher.box.id, nodescript.box.id),
    'bpatcher -> ... -> node.script chain missing',
  )
})

test('node.script "ready" outlet bangs each live.* widget for initial value bootstrap', () => {
  // ADR 003 §Stencil patcher line 359-363: on device load the setParam
  // messages race against [node.script] startup; without a handshake they
  // drop with "Node script not ready". Fix: stencil.mjs emits
  // Max.outlet('ready') after all addHandler calls; the patcher's
  // [route ... ready ...] outlet bangs each live.* widget so it re-emits
  // its current value through the existing prep -> nodescript chain.
  // Pattern lifted from oedipa's [route hostReady -> t b] handshake.
  //
  // ADR-002 spec'd `getvalueof` mechanism does not work with live.numbox
  // / live.slider / live.menu (only live.toggle). Banging the widget
  // directly is the alternative — a bang to live.numbox / live.slider /
  // live.menu causes the widget to emit its current value through outlet
  // 0, which the existing wiring carries.
  const { boxes, lines } = loadPatcher(MAXPAT)
  const route = boxesByMaxclass(boxes, 'newobj').find((b) =>
    /^route\b.*\bready\b/.test(b.box.text),
  )
  assert.ok(route, 'expected [route ... ready ...] consuming node.script outlet')
  // Find which outlet index corresponds to the `ready` token.
  // [route a b c] outlets: 0=a, 1=b, 2=c, 3=catchall.
  const tokens = route.box.text.split(/\s+/).slice(1)
  const readyOutletIdx = tokens.indexOf('ready')
  assert.ok(readyOutletIdx >= 0, 'route must include "ready" token')
  const readyConsumers = lines
    .filter(
      (l) =>
        l.patchline?.source?.[0] === route.box.id &&
        l.patchline?.source?.[1] === readyOutletIdx,
    )
    .map((l) => l.patchline.destination[0])
  assert.ok(
    readyConsumers.length >= 1,
    `[route ${tokens.join(' ')}] outlet ${readyOutletIdx} (ready) has no consumer`,
  )
  // For each live.* widget, the ready signal must reach its inlet so the
  // widget re-emits its value through the existing setParam chain.
  for (const [longname] of [...LIVE_PARAMS, ...LIVE_ENUMS]) {
    const w = findLiveWidget(boxes, longname)
    const ok = readyConsumers.some((id) => reachable(lines, id, w.box.id))
    assert.ok(ok, `ready -> ${longname} (${w.box.id}) chain missing`)
  }
})
