// Bundle guard test (ADR 006 §Release artifact, Phase 2).
//
// `m4l/stencil.mjs` is the bundled n4m entry produced by
// `pnpm bundle:host` (run as part of `pnpm bake`). It must contain only
// `max-api` as a runtime ESM external — Max injects `max-api` at runtime,
// every other dependency must be inlined into the bundle so that Max's
// Freeze step (which does NOT follow ES `import` chains; verified by
// oedipa-007 Phase 5) captures the entire host + engine in the frozen
// `.amxd`.
//
// On fresh checkouts the bundle hasn't been built yet, so the tests skip
// rather than fail. Running `pnpm bake` from `m4l/` populates it.

import { describe, test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const BUNDLE = resolve(
  dirname(fileURLToPath(import.meta.url)),
  "..",
  "stencil.mjs",
);

const skipMsg = !existsSync(BUNDLE)
  ? "bundle not built — run `pnpm bake` from m4l/"
  : false;

describe("stencil.mjs bundle (ADR 006 §Release artifact)", () => {
  test("bundle file exists", { skip: skipMsg }, () => {
    assert.ok(existsSync(BUNDLE));
  });

  test("only 'max-api' remains as runtime import", { skip: skipMsg }, () => {
    const text = readFileSync(BUNDLE, "utf8");
    // Match top-level ESM static imports (with or without `from`).
    const importRe =
      /(?:^|\n)\s*import\s+(?:[\s\S]*?\sfrom\s+)?['"]([^'"]+)['"]/g;
    const externals = new Set<string>();
    let m: RegExpExecArray | null;
    while ((m = importRe.exec(text)) !== null) externals.add(m[1]);
    const allowed = new Set(["max-api"]);
    const unexpected = [...externals].filter((s) => !allowed.has(s));
    assert.deepEqual(
      unexpected,
      [],
      `Bundled stencil.mjs has unexpected runtime imports: ${unexpected.join(", ")}.\n` +
        `Only 'max-api' should be external (Max provides it at runtime). All other deps\n` +
        `must be bundled in. If freeze is to capture the entire host, the entry needs to\n` +
        `be self-contained except for the Max-injected runtime.`,
    );
  });
});
