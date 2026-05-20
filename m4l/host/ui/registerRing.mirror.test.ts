// Drift detector for registerRing.jsui.js (the [jsui] consumer) against
// registerRing.logic.ts (the canonical TS source). The renderer can't
// `import` from TS -- Max's classic JS engine has no module system -- so
// constants are mirrored by hand. This test reads the renderer source as
// text and asserts each named constant from logic.ts appears with the same
// numeric value.
//
// Caveat: this catches CONSTANT drift, not function-body drift. The logic
// surface is small (~5 functions, ~80 LOC) and the renderer mirrors them
// nearby with a "(mirrors registerRing.logic.ts)" comment marker; keep
// in sync by discipline. If drift becomes a real problem, the next step is
// option B from the design discussion: bundle logic via esbuild + jsui
// `include`. For v1 the constants are the most likely thing to change in
// isolation, so checking them is the highest-value cheap guard.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import {
  BIT_GAP,
  CANVAS_MARGIN,
  MAX_BIT_RADIUS,
  MAX_LENGTH,
  MIN_LENGTH,
  POINTER_GAP,
  POINTER_HALF_WIDTH,
  POINTER_HEIGHT,
} from "./registerRing.logic.ts";

const __dirname = dirname(fileURLToPath(import.meta.url));
// Renderer lives at m4l/registerRing.jsui.js (flat, not under
// host/ui/) because Max [jsui]'s `filename` resolution does not
// reliably handle subdirectory paths in M4L presentation view —
// observed empirically when subdirectory-pathed jsui rendered as a
// generic placeholder instead of the renderer's output. See ADR 004
// §Patcher path conventions.
const RENDERER_PATH = join(__dirname, "..", "..", "registerRing.jsui.js");
const RENDERER_SRC = readFileSync(RENDERER_PATH, "utf8");

function findVarDecl(name: string): number {
  // Match `var NAME = <number>` -- the renderer's mirror block uses this
  // exact form. If a future refactor switches to `const` or computes the
  // value, this regex will fail and the dev will know to update both
  // sides intentionally.
  const re = new RegExp(`var\\s+${name}\\s*=\\s*(-?\\d+(?:\\.\\d+)?)`, "m");
  const m = re.exec(RENDERER_SRC);
  if (!m) throw new Error(`renderer missing var ${name}`);
  return Number(m[1]);
}

test("renderer mirrors MIN_LENGTH from logic.ts", () => {
  assert.equal(findVarDecl("MIN_LENGTH"), MIN_LENGTH);
});

test("renderer mirrors MAX_LENGTH from logic.ts", () => {
  assert.equal(findVarDecl("MAX_LENGTH"), MAX_LENGTH);
});

test("renderer mirrors MAX_BIT_RADIUS from logic.ts", () => {
  assert.equal(findVarDecl("MAX_BIT_RADIUS"), MAX_BIT_RADIUS);
});

test("renderer mirrors BIT_GAP from logic.ts", () => {
  assert.equal(findVarDecl("BIT_GAP"), BIT_GAP);
});

test("renderer mirrors CANVAS_MARGIN from logic.ts", () => {
  assert.equal(findVarDecl("CANVAS_MARGIN"), CANVAS_MARGIN);
});

test("renderer mirrors POINTER_GAP from logic.ts", () => {
  assert.equal(findVarDecl("POINTER_GAP"), POINTER_GAP);
});

test("renderer mirrors POINTER_HALF_WIDTH from logic.ts", () => {
  assert.equal(findVarDecl("POINTER_HALF_WIDTH"), POINTER_HALF_WIDTH);
});

test("renderer mirrors POINTER_HEIGHT from logic.ts", () => {
  assert.equal(findVarDecl("POINTER_HEIGHT"), POINTER_HEIGHT);
});

test("renderer is ASCII-only (Max classic JS parser constraint)", () => {
  // oedipa convention: cellstrip-renderer.js opens with the same constraint
  // ("Max's classic JS parser has been observed to choke on UTF-8 in source
  // files"). Failing this means any non-ASCII char slipped in -- escape
  // it as \\uXXXX in the renderer source.
  for (let i = 0; i < RENDERER_SRC.length; i++) {
    const code = RENDERER_SRC.charCodeAt(i);
    if (code > 0x7f) {
      const ctxStart = Math.max(0, i - 20);
      const ctxEnd = Math.min(RENDERER_SRC.length, i + 20);
      const ctx = RENDERER_SRC.slice(ctxStart, ctxEnd);
      assert.fail(
        `non-ASCII char (0x${code.toString(16)}) at offset ${i}: "${ctx}"`,
      );
    }
  }
});

test("renderer declares the message handler the bridge emits", () => {
  // The bridge emits `register` (the pre-shift snapshot) via emitOutlet
  // on every step. The renderer must dispatch that name. Cheap text
  // check: a typo on either side breaks the link silently in Live.
  assert.match(RENDERER_SRC, /msg === ['"]register['"]/);
});

test("renderer emits the setBit message the bridge handles", () => {
  // bridge.ts `setBit(index, value)` is the upstream half of the click
  // round-trip. If the renderer renames its outlet message, the click
  // becomes a no-op. Catch it via text check.
  assert.match(RENDERER_SRC, /outlet\([^)]*['"]setBit['"]/);
});

// Currently-sounding note readout (m4l-side mirror of vst's RingView
// center text). The bridge forks every noteOn/noteOff to a `currentNote`
// outlet (m4l/host/bridge.ts emitNoteEvent); the renderer dispatches the
// message, mirrors midiToNoteName for note-name formatting, and shows
// the label inside the ring.

test("renderer dispatches the currentNote message the bridge forks", () => {
  assert.match(RENDERER_SRC, /msg === ['"]currentNote['"]/);
});

test("renderer defines a midiToNoteName function (mirror of logic.ts)", () => {
  assert.match(RENDERER_SRC, /function\s+midiToNoteName\b/);
});

test("renderer mirrors the pitch-class table (Live Yamaha convention)", () => {
  // Same regex as rangeSlider.mirror.test.ts: must be the exact 12-name
  // sharp table in order so reordering or substituting flat enharmonics
  // (Db for C#) trips the check. Both jsui files keep their own copy
  // (Max [jsui] can't import from TS), and the table is the canonical
  // anchor shared with m4l/host/ui/rangeSlider.logic.ts midiToNoteName.
  assert.match(
    RENDERER_SRC,
    /\[\s*['"]C['"]\s*,\s*['"]C#['"]\s*,\s*['"]D['"]\s*,\s*['"]D#['"]\s*,\s*['"]E['"]\s*,\s*['"]F['"]\s*,\s*['"]F#['"]\s*,\s*['"]G['"]\s*,\s*['"]G#['"]\s*,\s*['"]A['"]\s*,\s*['"]A#['"]\s*,\s*['"]B['"]\s*\]/,
  );
});
