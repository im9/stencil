// Drift detector for rangeSlider.jsui.js against rangeSlider.logic.ts.
// The renderer can't `import` from TS so constants are mirrored by hand;
// this test reads the renderer source as text and asserts each named
// constant from logic.ts appears with the same numeric value.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

import {
  HIT_RADIUS_MULT,
  MAX_VALUE,
  MIN_VALUE,
  THUMB_RADIUS,
  TRACK_HEIGHT,
  TRACK_PAD,
} from "./rangeSlider.logic.ts";

const __dirname = dirname(fileURLToPath(import.meta.url));
const RENDERER_PATH = join(__dirname, "..", "..", "rangeSlider.jsui.js");
const RENDERER_SRC = readFileSync(RENDERER_PATH, "utf8");

function findVarDecl(name: string): number {
  const re = new RegExp(`var\\s+${name}\\s*=\\s*(-?\\d+(?:\\.\\d+)?)`, "m");
  const m = re.exec(RENDERER_SRC);
  if (!m) throw new Error(`renderer missing var ${name}`);
  return Number(m[1]);
}

test("renderer mirrors MIN_VALUE from logic.ts", () => {
  assert.equal(findVarDecl("MIN_VALUE"), MIN_VALUE);
});

test("renderer mirrors MAX_VALUE from logic.ts", () => {
  assert.equal(findVarDecl("MAX_VALUE"), MAX_VALUE);
});

test("renderer mirrors THUMB_RADIUS from logic.ts", () => {
  assert.equal(findVarDecl("THUMB_RADIUS"), THUMB_RADIUS);
});

test("renderer mirrors TRACK_PAD from logic.ts", () => {
  assert.equal(findVarDecl("TRACK_PAD"), TRACK_PAD);
});

test("renderer mirrors TRACK_HEIGHT from logic.ts", () => {
  assert.equal(findVarDecl("TRACK_HEIGHT"), TRACK_HEIGHT);
});

test("renderer mirrors HIT_RADIUS_MULT from logic.ts", () => {
  assert.equal(findVarDecl("HIT_RADIUS_MULT"), HIT_RADIUS_MULT);
});

test("renderer is ASCII-only (Max classic JS parser constraint)", () => {
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

test("renderer declares the inlet message handlers the patcher emits", () => {
  assert.match(RENDERER_SRC, /msg === ['"]setLo['"]/);
  assert.match(RENDERER_SRC, /msg === ['"]setHi['"]/);
});

test("renderer emits the rangeLo / rangeHi outlet messages the patcher routes", () => {
  assert.match(RENDERER_SRC, /outlet\([^)]*['"]rangeLo['"]/);
  assert.match(RENDERER_SRC, /outlet\([^)]*['"]rangeHi['"]/);
});
