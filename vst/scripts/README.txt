Stencil — probabilistic MIDI sequence generator
im9


About
-----

Stencil is a Music Thing-style Turing Machine shift register that
emits notes from a controlled-randomness loop. On each host step a
shift register of `length` bits is read, mapped to a pitch within
the user-set MIDI range, and emitted as a single noteOn carrying
that pitch, the configured output velocity, and a gate length equal
to `outputGate × step duration`.

The register mutates under `lock`: at 1.0 the loop is frozen and
the same sequence repeats forever; at 0.0 every step writes a
fresh random bit; in between the register stays musically coherent
but slowly evolves. `density` further gates whether the step
actually emits — at 1.0 every step plays, at lower values the
output thins out probabilistically.

For a fixed `(seed, length, lock, density, range)` the walk is
deterministic — scrubbing the transport or resuming playback from
any position reproduces the same output.

Full musical model: docs/ai/concept.md in the source repository
(https://github.com/im9/stencil). Stencil pairs with Pointsman
(scale quantizer) for scale-locked chains: Stencil → Pointsman →
Synth.


Parameters
----------

Shift register:

  LEN           2..32        register length in bits
  LOCK          0..1         bit-flip probability lock; 1.0 = frozen
  DENS          0..1         per-step output probability gate
  SEED          0..65535     PRNG seed for reproducibility

Output:

  RANGE LO      0..127 MIDI  output pitch range floor
  RANGE HI      0..127 MIDI  output pitch range ceiling
  VEL           1..127       output note velocity (default 100)
  GATE          0..1         gate length as fraction of step (default 0.5)
  SUBDIV        enum         8th | 16th | 32nd | 8T | 16T (default 16th)

Routing:

  TRG           enum         auto | gate | seed (trigger source)
  IN-CH         0..16        MIDI input channel; 0 = omni
  OUT-CH        1..16        MIDI output channel


Changelog
---------

v0.1.0     Initial release.
           AU + VST3 + CLAP macOS bundles, signed and notarized.


License
-------

Proprietary, source-available — https://github.com/im9/stencil/blob/main/vst/LICENSE
