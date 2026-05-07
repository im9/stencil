// Tests for the bake script (maxpat-to-amxd.mjs). Pure unit tests over
// the synthesize-from-scratch AMPF wrapper; no real .maxpat / .amxd
// touched here. Patcher content is validated separately in
// patcher.test.mjs.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import {
  AMXD_NAME,
  HEADER_SIZE,
  MAXPAT_NAME,
  SIZE_OFFSET,
  TRAILER,
  bake,
  unwrapAmxd,
  wrapMaxpatJson,
} from './maxpat-to-amxd.mjs'

const MINIMAL_PATCHER_JSON =
  '{"patcher":{"fileversion":1,"appversion":{"major":8,"minor":6,"revision":5},"rect":[0,0,320,180],"boxes":[],"lines":[]}}'

async function withTmpRoot(fn) {
  const root = await mkdtemp(join(tmpdir(), 'stencil-bake-'))
  try {
    return await fn(root)
  } finally {
    await rm(root, { recursive: true, force: true })
  }
}

// ---- wrapMaxpatJson: header bytes ---------------------------------------

test('wrapMaxpatJson — first 4 bytes are "ampf"', () => {
  // Verified empirically against oedipa's Max-written .amxd.
  const out = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  assert.equal(out.subarray(0, 4).toString('ascii'), 'ampf')
})

test('wrapMaxpatJson — bytes 4..7 are LE uint32 = 4 (format version)', () => {
  // Format version 4 is what every Max 8.x .amxd we have on disk uses.
  // Diverging would risk Live's loader rejecting the file.
  const out = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  assert.equal(out.readUInt32LE(4), 4)
})

test('wrapMaxpatJson — bytes 8..11 are "mmmm" container magic', () => {
  const out = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  assert.equal(out.subarray(8, 12).toString('ascii'), 'mmmm')
})

test('wrapMaxpatJson — bytes 12..15 are "ptch" chunk magic', () => {
  const out = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  assert.equal(out.subarray(12, 16).toString('ascii'), 'ptch')
})

test('wrapMaxpatJson — size field at offset 16 = JSON length + TRAILER length', () => {
  // Per AMPF layout: chunk size covers the JSON plus the 2-byte trailer.
  // Off-by-two here would make Live silently truncate the patch.
  const json = Buffer.from(MINIMAL_PATCHER_JSON, 'utf8')
  const out = wrapMaxpatJson(json)
  assert.equal(out.readUInt32LE(SIZE_OFFSET), json.length + TRAILER.length)
})

test('wrapMaxpatJson — last 2 bytes are 0x0a 0x00 trailer', () => {
  // "\n\0" — verified verbatim against oedipa's .amxd tail.
  const out = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  assert.equal(out[out.length - 2], 0x0a)
  assert.equal(out[out.length - 1], 0x00)
})

test('wrapMaxpatJson — total size = header + JSON + trailer', () => {
  // Sanity: no padding, no other chunks. Stencil .amxd files only
  // contain the patcher chunk; if Max ever started writing additional
  // chunks (extra "mmmm" sections) we'd want to know.
  const json = Buffer.from(MINIMAL_PATCHER_JSON, 'utf8')
  const out = wrapMaxpatJson(json)
  assert.equal(out.length, HEADER_SIZE + json.length + TRAILER.length)
})

test('wrapMaxpatJson — JSON is preserved byte-for-byte at offset 20', () => {
  // No re-serialization: the bytes the user wrote land in the .amxd
  // exactly. Whitespace, key ordering, comments-as-strings all preserved.
  const json = Buffer.from(MINIMAL_PATCHER_JSON, 'utf8')
  const out = wrapMaxpatJson(json)
  assert.deepEqual(
    out.subarray(HEADER_SIZE, HEADER_SIZE + json.length),
    json,
  )
})

// ---- unwrapAmxd: inverse + error paths ----------------------------------

test('unwrapAmxd — round-trips wrapMaxpatJson', () => {
  // wrap then unwrap returns exactly the input bytes.
  const json = Buffer.from(MINIMAL_PATCHER_JSON, 'utf8')
  const amxd = wrapMaxpatJson(json)
  const recovered = unwrapAmxd(amxd)
  assert.deepEqual(recovered, json)
})

test('unwrapAmxd — rejects buffer too short for the wrapper', () => {
  // A 10-byte buffer can't contain the 20-byte header + 2-byte trailer.
  assert.throws(() => unwrapAmxd(Buffer.alloc(10)), /too short/)
})

test('unwrapAmxd — rejects buffer without "ampf" magic', () => {
  // Build a wrapped buffer, then corrupt the magic; ensures the check
  // is on the bytes, not just the length.
  const amxd = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  amxd[0] = 0x00
  assert.throws(() => unwrapAmxd(amxd), /ampf/)
})

test('unwrapAmxd — rejects buffer with mismatched chunk size', () => {
  // Live reads the size field; if it's wrong the patcher chunk won't
  // line up. Catch on bake/round-trip rather than at load time.
  const amxd = wrapMaxpatJson(Buffer.from(MINIMAL_PATCHER_JSON, 'utf8'))
  amxd.writeUInt32LE(amxd.readUInt32LE(SIZE_OFFSET) + 5, SIZE_OFFSET)
  assert.throws(() => unwrapAmxd(amxd), /chunk size mismatch/)
})

// ---- bake(): I/O behavior ------------------------------------------------

test(`bake — writes m4l/${AMXD_NAME} from m4l/${MAXPAT_NAME}`, async () => {
  // Flat layout matching oedipa's `m4l/Oedipa.amxd` (per ADR 004).
  await withTmpRoot(async (root) => {
    const maxpatPath = join(root, MAXPAT_NAME)
    await writeFile(maxpatPath, MINIMAL_PATCHER_JSON)
    const r = await bake({ check: false, m4lRoot: root })
    assert.equal(r.wrote, true)
    const amxd = await readFile(r.amxdPath)
    assert.equal(unwrapAmxd(amxd).toString('utf8'), MINIMAL_PATCHER_JSON)
  })
})

test('bake — rejects invalid JSON in .maxpat', async () => {
  // Bake-time JSON.parse so the error surfaces at bake, not in Live's
  // device loader (which gives a less actionable message).
  await withTmpRoot(async (root) => {
    await writeFile(join(root, MAXPAT_NAME), '{ not json')
    await assert.rejects(
      () => bake({ check: false, m4lRoot: root }),
      /not valid JSON/,
    )
  })
})

test('bake --check — returns upToDate when .amxd matches the .maxpat', async () => {
  // Common path on a fresh checkout: bake.amxd is checked in, --check
  // confirms it matches the source. Used by `pnpm bake:check` in CI/local.
  await withTmpRoot(async (root) => {
    await writeFile(join(root, MAXPAT_NAME), MINIMAL_PATCHER_JSON)
    await bake({ check: false, m4lRoot: root })
    const r = await bake({ check: true, m4lRoot: root })
    assert.equal(r.upToDate, true)
  })
})

test('bake --check — returns !upToDate when .maxpat changed but .amxd not re-baked', async () => {
  // The exit-1 path: dev edited .maxpat but forgot to bake. Catches
  // the stale-artifact case before it reaches a release.
  await withTmpRoot(async (root) => {
    await writeFile(join(root, MAXPAT_NAME), MINIMAL_PATCHER_JSON)
    await bake({ check: false, m4lRoot: root })
    const modified = MINIMAL_PATCHER_JSON.replace('320', '321')
    await writeFile(join(root, MAXPAT_NAME), modified)
    const r = await bake({ check: true, m4lRoot: root })
    assert.equal(r.upToDate, false)
  })
})

test('bake --check — returns !upToDate when .amxd does not exist yet', async () => {
  // Fresh checkout with .maxpat but no built .amxd: --check should
  // report stale rather than silently report up-to-date.
  await withTmpRoot(async (root) => {
    await writeFile(join(root, MAXPAT_NAME), MINIMAL_PATCHER_JSON)
    const r = await bake({ check: true, m4lRoot: root })
    assert.equal(r.upToDate, false)
  })
})

test('bake --check — does not write the .amxd', async () => {
  // The "check" name is load-bearing: it must be safe to run in
  // pre-commit hooks / read-only CI. Explicitly assert no write.
  await withTmpRoot(async (root) => {
    await writeFile(join(root, MAXPAT_NAME), MINIMAL_PATCHER_JSON)
    await bake({ check: true, m4lRoot: root })
    await assert.rejects(
      () => readFile(join(root, AMXD_NAME)),
      /ENOENT/,
    )
  })
})
