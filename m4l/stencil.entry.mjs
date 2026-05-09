// Stencil n4m entry source. Thin wrapper over TmBridge — wires the
// Max API (Max.outlet, Max.addHandler, Date.now, setTimeout) into the
// bridge's injected deps. No musical logic lives here.
//
// This file is the *source* entry. `pnpm bundle:host` (esbuild)
// inlines the import chain (`./host/dist/host/bridge.js` → host.js,
// engine/turing.js, engine/rng.js, ui/registerRing.logic.js) into
// the bundled output `m4l/stencil.mjs`, with `max-api` as the only
// remaining external. The bundled file is what `[node.script]`
// actually loads from `Stencil.amxd`, and is what Max's Freeze step
// can capture into the shipped `dist/Stencil.amxd` — Freeze does NOT
// follow ES `import` statements (verified by oedipa-007 Phase 5), so
// the entry must arrive at Freeze pre-bundled. (See ADR 006 §Release
// artifact production.)
//
// Path conventions (flat, m4l/ root):
// - The bundle output `m4l/stencil.mjs` MUST live at the m4l/ root
//   (NOT under host/), because Max [node.script]'s `filename`
//   attribute does not reliably resolve subdirectory paths in M4L
//   presentation view. Observed empirically: a [node.script] with
//   `host/index.mjs` as filename reports "No such file or directory"
//   in Max log even when the file exists. Same constraint as Max
//   [jsui], which is why `registerRing.jsui.js` is also at the m4l/
//   root. (See ADR 004 §Patcher path conventions.) This source entry
//   sits next to its bundled output for the same reason — the bundle
//   is single-file and esbuild's import resolution starts here.
// - The compiled bridge dist still lives under `host/dist/host/`,
//   imported relatively from this entry — esbuild follows the `./`
//   path at bundle time, Max never sees it.
//
// `.mjs` (not `.js`) is load-bearing for distribution: when the
// `.amxd` is baked / frozen, [node.script] extracts the script to a
// tempdir with no sibling package.json. `.js` would default to CJS
// there and the `import Max from "max-api"` line below would fail to
// parse, leaving [node.script] permanently in "Node script not ready"
// state. `.mjs` is unconditionally ESM. The same constraint applies
// to the bundled output, which is why esbuild's `--outfile` is
// `stencil.mjs`. (Convention ported from oedipa, validated 2026-05-01
// against dev/dist Max console comparison.)
//
// `max-api` is provided by Max at runtime; it MUST NOT appear in
// package.json dependencies — the npm version conflicts with the
// injected one. Bundle config marks it `--external:max-api` so
// esbuild leaves the import statement intact. Running this file
// under plain Node fails to resolve 'max-api'; the bridge logic is
// fully tested in bridge.test.ts and doesn't touch it.
//
// Protocol (see ADR 002 §Host ↔ Max protocol):
//
//   Max → here:
//     step <pos>                           advance to host step index
//     panic                                all notes off
//     setParam <key> <value>               scalar param update
//     setRange <lo> <hi>                   tuple param (TM range)
//     setBit <index> <value>               direct register write (jsui ring)
//     noteIn <pitch> <velocity> <channel>  incoming MIDI note-on
//     noteOff <pitch> <channel>            incoming MIDI note-off
//     transportStart                       pre-roll snapshot at 0→1
//     transportStop                        flush + reset position
//
//   here → Max (via Max.outlet):
//     note <pitch> <velocity> <channel>    velocity=0 = note-off
//     ready                                fired once after all handlers
//                                          are installed; the patcher gates
//                                          its initial live.* dump on this
//                                          (ADR 003 §Stencil patcher)
//     register <bit0> <bit1> … <bitN>      current register, bit-unpacked
//     ringHead <n>                         current step index (for jsui ring)

import Max from "max-api";
import { TmBridge } from "./host/dist/host/bridge.js";

Max.post("stencil: stencil.mjs loaded");

const bridge = new TmBridge({
  emitNote: (pitch, velocity, channel) =>
    Max.outlet("note", pitch, velocity, channel),
  emitOutlet: (channel, ...args) => Max.outlet(channel, ...args),
  now: () => Date.now(),
  scheduleAfter: (ms, cb) => setTimeout(cb, ms),
});

Max.addHandler("step", (pos) => bridge.step(Number(pos)));
Max.addHandler("panic", () => bridge.panic());
Max.addHandler("setParam", (key, value) => bridge.setParam(String(key), value));
Max.addHandler("setRange", (lo, hi) => bridge.setRange(Number(lo), Number(hi)));
Max.addHandler("setBit", (idx, val) =>
  bridge.setBit(Number(idx), Number(val)));
Max.addHandler("noteIn", (pitch, velocity, channel) =>
  bridge.noteIn(Number(pitch), Number(velocity), Number(channel)));
Max.addHandler("noteOff", (pitch, channel) =>
  bridge.noteOff(Number(pitch), Number(channel)));
Max.addHandler("transportStart", () => bridge.transportStart());
Max.addHandler("transportStop", () => bridge.transportStop());

// Signal the patcher that node.script is up and every handler is wired.
// The patcher's [route ... ready ...] outlet bangs each live.* widget on
// this so the 12 setParam dispatches arrive AFTER addHandler installation,
// not before (which would drop them with "Node script not ready").
Max.outlet("ready", 1);
