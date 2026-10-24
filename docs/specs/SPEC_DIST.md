# Distortion: behavioural specification for the implementer (character)

You are re-deriving ReBirth's distortion CHARACTER to match measured behaviour.
Work ONLY from this document, `docs/research/PLAN_DIST.md` (the measured facts), the
measured audio under `renders/dist-grid-2026-09-17/` and `renders/
dist-probe-2026-09-17/`, and the existing `reborn/` source. Do NOT consult any
third-party analysis, disassembly or decompilation of ReBirth; every requirement
was measured from ReBirth's own audio, and you fit your own arithmetic to it.

The file to change is `src/effects/distortion.cpp`. Scope THIS round is the
waveshaping CHARACTER (the harmonic generation) and the two controls. The gain
law, the DIST bus pan-law, the position-in-chain, the automation decode and the
phantom `level` param are OUT of scope (Role A follow-ups).

## What ReBirth's distortion is (measured)

Amp-style dirt ("like turning up a guitar amp", manual). Two controls, and the
byte-ID is settled (dist-grid): **Amount = DIST byte 2** (the drive; 0 = clean,
already decoded into the `amount` param), **Shape = DIST byte 1** (the character;
decoded into the `tone` param today; RENAME it `shape`). It is a HARD /
aggressive clipper: on a 303 saw drone it adds **+17 to +50 dB in the 2-16 kHz
bands**, and the harmonics decay SLOWLY (h8/h10 only ~15-22 dB below the
fundamental). Our current `tanh` soft-clip is **18 to 33 dB short in HF** on the
identical input (measured with the `dist_wav` harness), and that is the whole
audible gap (BACKLOG 2).

**Shape is non-monotonic in brightness.** On the drone, at fixed Amount, HF and
centroid RISE with Shape to a peak around Shape ~32-64, then FALL by Shape 127
(Amount 127: HF 4.3/5.4/7.9/6.0/1.5 %, centroid 569/559/720/457/297 Hz at Shape
0/32/64/96/127). So high Shape is not "more HF"; the energy shifts toward the
lower harmonics (grungier, not brighter). Your model must reproduce this shape,
not a monotonic brightness.

## What ours gets wrong

`distortion.cpp` is a `tanh` soft-clip (far too gentle), it models Shape as a
tone-TILT EQ (wrong: Shape is a waveshaping-character control), and its ADAA
anti-aliasing may be removing fold-back energy that ReBirth actually produces
(ReBirth is late-90s software and likely does not oversample, so its "character"
includes aliasing). Match ReBirth's measured spectrum, adding no more
anti-aliasing than the match needs; if the hard clip must alias to match, let it.

## What to build

A hard-clip (or similarly aggressive) static waveshaper whose OUTPUT SPECTRUM on
the drone matches ReBirth across the Amount x Shape grid:
- **Amount** (`amount`, from byte2) is the DRIVE. Amount 0 must be an exact
  bypass (clean). Fit the drive law so the HF generation tracks ReBirth as Amount
  rises.
- **Shape** (`shape`, from byte1) parametrises the CURVE to reproduce the
  measured non-monotonic behaviour (bright HF near mid-Shape, shifting to
  low-harmonic grunge near Shape 127). You choose the mechanism (clip hardness,
  bias/asymmetry, a curve morph, a small waveshaper bank), whatever reproduces
  the measured surface with as few constants as possible; do not free-fit a large
  table.
Keep the stereo handling and the stability guards. Remove the tone-tilt EQ.

## Fit / verify method (the `dist_wav` harness)

Rebuild it: `cmake --build build --config Release --target dist_wav`. It feeds a
WAV through our distortion at a given Amount/Shape, so you compare against
ReBirth's OWN render on the IDENTICAL input (no source confound):

```
build/Release/dist_wav.exe  renders/dist-grid-2026-09-17/d_off.wav  <out.wav>  <amount01>  <shape01>
```
where amount01 = byte2/127, shape01 = byte1/127. Compare `<out.wav>` to ReBirth's
`renders/dist-grid-2026-09-17/g_b1{byte1:03d}_b2{byte2:03d}.wav` with
`tools/measure/dist_spectrum.py` (per-band energy fractions + centroid; these are
NORMALISED, so absolute level (the out-of-scope gain law) does not affect the
character match). The clean drone is `d_off.wav`.

Fit across the full 5x5 grid (byte1,byte2 in {0,32,64,96,127}). If the 5 Shape
points are too coarse to fit the non-monotonic curve, say so and Role A will
measure a denser Shape sweep rather than you guessing between the points.

## Acceptance targets

- **Amount 0 = exact bypass** (output == input; HF unchanged from the clean drone).
- **HF (2-16 kHz) energy within ~3 dB of ReBirth** at every grid cell where
  ReBirth's HF is significant (closing the current 18-33 dB gap), and the centroid
  tracking ReBirth's non-monotonic Shape trend (peak near mid-Shape, drop at 127).
- Per-band spectral shape broadly matching ReBirth across the grid (report the
  residuals honestly; this is a character match, not a null).
- `ctest` green; a non-distorted render (distortion off) byte-identical to before.

## Commit

Commit on `dist-supertask` (or a `dist-cleanroom` branch off it; Role A will say
which) with the project's git author and the `Co-Authored-By` trailer. Report the fitted model
(the curve form, the Amount and Shape laws, and any constants), the HF/centroid
residuals across the grid, and anything you could not match and why.
