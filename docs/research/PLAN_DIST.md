# Distortion (supertask 7): measured facts

Provenance: measured from ReBirth exports (clean-room). No constant from any
binary analysis is used (`docs/PROVENANCE.md`). Branch `dist-supertask` (off
`comp-supertask`). Campaigns under `fixtures-2026-09-17/dist-*`, renders
alongside, generators `tools/rbsgen/gen_dist_*.py`, analyzer
`tools/measure/dist_spectrum.py`. Measure with `REBORN_BASS_ENGINE=tpt` or a
harness, never the Open303 voice (BACKLOG 15).

## Phase 1 inventory (manual ch.8 pp.59-60, Appendix C p.198)

Amp-style distortion ("dirtier and grungier, like turning up a guitar amp"), an
INSERT after each section, BEFORE the PCF in the chain. FOUR units (one per
section) sharing ONE control set; any number of sections can have it on at once
(unlike the PCF/compressor, which are single-section). Two controls: **Shape**
(CC 104, "the character, higher = grungier") and **Amount** (CC 105, "the higher,
the dirtier"), 0-10 on the panel, plus a per-section on/off. No curve or numeric
ranges are printed, so the waveshaping function, the two laws and the gain law are
all measurement-only.

What ours (`src/effects/distortion.cpp`) gets wrong: it is a tanh soft-clip with
an asymmetry bias; it models Shape as a TONE-TILT EQ (`tone`); its harmonic
generation is 25-38 dB short of ReBirth (BACKLOG 2); the output gain is a
centering constant (0.6105), not a fitted law; and it exposes a phantom `level`
param ReBirth does not have. Structural gaps: the DIST bus is ~3 dB hot (missing
pan law), timed Amount/Shape automation is undecoded (BACKLOG 5b), and the
per-section-chain vs parallel-bus / position-before-PCF topology needs verifying.

## Byte ID (batches 1-2, dist-probe + dist-grid): byte1 = SHAPE, byte2 = AMOUNT

The DIST chunk is `[ver/on][byte1][byte2][..]`. The full 5x5 byte grid
(dist-grid, 303 drone through one section, everything else off) settles which is
which:

- **byte2 = 0 -> NO distortion for every byte1** (HF 2-16 kHz ~0%, centroid ~clean
  122 Hz). So byte2 gates the effect: it is AMOUNT (0 = clean, rising = dirtier).
- **byte1 = 0 with byte2 up still distorts strongly** (b1=0,b2=127: centroid 569,
  HF 4.3%). byte1 is not required; it is SHAPE (the character), and its effect on
  brightness is NON-monotonic (a curve/character control, not a drive).

This matches Appendix C's order (Shape 104 then Amount 105): the file byte order
IS the MIDI order. **The current loader (`rbs.cpp`) has them SWAPPED**: it reads
`distAmount = byte1`, `distTone = byte2`, so a distorted song drives our shaper
with the Shape byte and our tone-EQ with the Amount byte. Decode fix needed:
Amount = byte2, Shape = byte1.

ReBirth's distortion adds **+17 to +50 dB in the 2-16 kHz bands** on the drone at
every setting (batch 1), far more harmonic content than our tanh. Level ranges
-18 to -30 dBFS across the grid (clean -25.8), a non-trivial gain law.

## Decode SWAPPED + fixed (2026-09-17)

`rbs.cpp`/`rbs_write.cpp`: Amount = DIST byte 2, Shape = DIST byte 1 (was
reversed). Verified: our engine now responds to byte2 as the drive (a grid
fixture at Amount 0 renders clean, at Amount 127 distorts). `distTone` still names
the SHAPE control until the DSP rework. ctest green.

## The curve is a HARD clipper; no clean sine available (batch 3)

The transfer-curve probe (dist-curve, near-sine 303) confirmed the character
mechanism and hit a methodology limit:
- At high Amount the harmonics decay SLOWLY (h8/h10 only ~15-22 dB below the
  fundamental): a HARD / aggressive clipper, the opposite of our tanh's fast
  exponential rolloff. That is the +17..+50 dB HF and the 25-38 dB shortfall.
- The 303 cannot make a clean sine: even at cutoff 40 the source carries strong
  harmonics (h2 -7.6 dB) and a 2-pole filter is too gentle to isolate the
  fundamental of a saw, so the analytic transfer curve cannot be cleanly
  reverse-engineered from harmonics. (A self-oscillating high-resonance 303 as a
  sine source is untried; a candidate if a clean curve is wanted.)
- Shape (byte1) is non-monotonic in brightness: on the drone grid it peaks HF/
  centroid around Shape 64 and REDUCES them by Shape 127 (grungier != brighter).

Parity path (not analytic derivation): fit a hard-clip waveshaper whose OUTPUT
spectrum on realistic sources matches ReBirth's. The byte grid (dist-grid) is the
fitting target and the Amount sweep probes the curve across drive levels.

## Next

- Characterise the output-spectrum surface densely for the fit target (Amount x
  Shape, maybe an input-level axis via accent, maybe a drum source too).
- Structural: the pan-law bus fix (~3 dB hot), the position-in-chain/topology
  check (per-section chains before the PCF), the DIST automation decode, and
  retire the phantom `level` param.
- Then a coefficient-free spec + a walled implementer to fit the waveshaper.

## The vocoder texture = inharmonic (aliased) content (ear + measurement, 2026-09-17)

Ear checks (listen-dist-character-*): round 2's foldback was "right ballpark but a
little more FRIED than ReBirth" (measured: +4-7 dB hot in 2-16 kHz). Round 3 tamed
that to within ~3 dB per band, but then it was "a bit undershot / muted; the
vocoder texture ReBirth's distortion has is distinctly missing." That texture is
the INHARMONIC / aliased content: a wavefolder driven without oversampling folds
high harmonics back as non-harmonic partials, which read as a metallic,
formant-like, vocoder-ish character. The band-energy metric is deaf to it.

New metric `tools/measure/dist_inharm.py`: inharmonic fraction = share of
500 Hz-16 kHz energy NOT at multiples of the (fixed) drone f0. Measured on the
dense-grid drone, ReBirth vs our round-3:

  Shp21/Amt106  ReBirth 47.9%  ours 25.1%
  Shp43/Amt106  51.6%  26.3%
  Shp43/Amt127  64.2%  47.3%
  Shp64/Amt127  69.1%  27.2%
  (ReBirth 28-69% across cells; ours 22-47%, short by 5-42 points)

So the fit now has TWO targets: the band-energy LEVEL (round 3 got this, not
fried) AND the inharmonic FRACTION (round 3 lost this, the texture). Round 3
reduced the fold too far, cutting the aliasing that is the character. The fix:
restore the inharmonic content to ReBirth's level WITHOUT re-inflating the
broadband HF back to fried (decouple texture from level, e.g. keep the fold
depth but control the level with a gentle post-rolloff, or lean on the quantizer
for inharmonic texture). Round 4 targets both metrics; then re-ear-check.

## Character SETTLED: Luke's ear sign-off (2026-09-18)

Round 4 (`509e1c0`..`185a93e`) passed the v3 ear-check
(`listen-dist-character-v3-2026-09-17`, setA Amount96/Shape43 and setB
Amount106/Shape21, each REBIRTH vs OURS behind a null pair). Luke's verdict:
"sounds right": the vocoder texture v2 lost is back, without v1's fried edge.

The signed-off mechanism (`src/effects/distortion.cpp`): in -> pre-gain(Amount,
drive = 1 + 35*amount^1.6) -> hard triangle wavefold (Shape sets the fold
threshold via a bi-Gaussian bell peaking ~0.31) -> level-quantise (Shape sets
the step count 103->35) -> post-fold one-pole low-pass (kPostLpHz=4800, the
round-4 addition that controls broadband level so the fold depth is free to
carry the aliasing texture) -> out. Amount 0 is an early-return exact bypass.
Effective-ratio knee kEffKnee=90, kEffRatio=1.6. Verified: inharmonic ~45-70%
(ReBirth 38-69%), band energy within ~3-4 dB, ctest green.

The waveshaper coefficients were fit by the walled implementer from spec +
measured data only; the clean-room wall held. The DSP was folded onto
`dist-supertask` (merge `0c2421c`, byte-identical to the approved
`dist-cleanroom` tip); build + ctest green on the consolidated branch.

**What CHARACTER settled does NOT cover.** These are structural / topological and
do not touch the waveshaper fit, and remain before a main fold:
1. DIST bus pan-law (~3 dB hot; the PCF-bus bug, still live on the DIST bus;
   see reborn/CLAUDE.md).
2. Position-in-chain: per-section chains, distortion BEFORE the PCF, four
   instances mirroring the one the UI edits (measured; engine routing check).
3. Output GAIN LAW: still the 0.6105 centering trim, not a fitted law, ~+-7 dB
   residual at the knob extremes (BACKLOG 2). Role A measures the level surface;
   any fitted coefficient stays walled.
4. DIST timed Amount/Shape automation decode (BACKLOG 5b; 12 songs hold the
   per-section switch flat, 360 events).
5. Retire the phantom `level` param ReBirth does not have.

Then Phase-5 close-out (COMPONENT_MATRIX, PARITY, FINDINGS_AUDIT) and, on Luke's
sign-off, fold to `main`.

## Structural items 1, 2, 5 done (2026-09-18)

Items 1 and 2 were ALREADY RESOLVED and the notes calling them open were stale.
The engine was rewritten to per-section chains during the PCF supertask
(`include/reborn/engine.h` ~line 186-194, `Engine::renderSlice`): there are no
dist/pcf buses any more, each section carries its own chain in `chScratch_`, the
four distortion instances exist (`dist_` for section 0 plus `distExtra_[3]`,
routed by `distortionFor`), and the order is distortion -> PCF -> compressor per
section (renderSlice lines ~819-823). Every section, distorted or clean, is
panned exactly ONCE through `panGains` (renderSlice line ~836), so the ~3 dB-hot
un-panned distortion bus (item 1, the old PCF-bus bug) cannot occur; it was
removed outright by that rewrite, not just for the PCF. Proven in audio, not just
by reading: `test_distortion_is_panned_per_section` (a distorted section hard-
panned is silent on the far side, centred is symmetric) is green. Stale notes
corrected: `reborn/CLAUDE.md` (the "DISTORTION bus still has this bug" and the
"25-38 dB short" character lines), `include/reborn/engine.h` MixerChannel
comments, and this file.

Item 5 (retire the phantom `level` param) done: nothing ever set it (ReBirth's
distortion has only Shape and Amount), it defaulted to 0.7 so it only scaled the
output by 0.7/0.7 = 1, and it was a phantom plugin knob. Removed from `kP`
(3 params -> 2), the output trim is the constant `kOutTrim` now, and the
`dist_wav` harness dropped its dead `level01` argument. ctest green.

REMAINING before a main fold: item 3 (absolute output/gain law, BACKLOG 2:
Role A measures the level surface, any fitted coefficient stays walled) and item
4 (DIST timed Amount/Shape automation decode, BACKLOG 5b). Then Phase-5 close-out.

## Items 3 and 4 done (2026-09-18): ALL FIVE structural items complete

Item 3 (output GAIN LAW). Role A measured the absolute output-level surface
(`tools/measure/dist_gain.py`, target `docs/dist_gain_target.csv`): ReBirth's
distorted RMS vs our current output across the dense Amount x Shape grid, giving
`outGain_target = kOutTrim * L_rb/L_ours` per cell. The old constant `kOutTrim`
ran up to ~13 dB too loud at high drive with a systematic -8 dB overshoot. The
walled implementer fit `outGain(amount,shape) = clamp(kOutBase*exp(kOutAmt*amount
+ kOutShp*shape), 0.05, 2.0)` (kOutBase 1.268, kOutAmt -1.262, kOutShp -0.917)
from the measured CSV only (`docs/specs/SPEC_DIST_GAIN.md`, commit 181b67a on
dist-cleanroom, folded here). Independently verified: median |gap_dB| across the
42 amount>0 cells 4.62 -> 1.99, max 13.0 -> 7.08, the systematic overshoot gone
and residuals centred; the ~2 dB scatter is the irreducible cell-to-cell aliasing
chaos (a trend-level fit, like the drive law). Character metrics unchanged (a
per-cell constant scalar cancels in the normalised bands and the inharmonic
ratio), Amount 0 exact bypass untouched, ctest green.

Item 4 (DIST timed automation decode). The DIST track (track 6) was read for its
tick-0 on/off only; its Shape/Amount and all timed rides were dropped. Added
`AutoTarget::Dist`; `rbs.cpp` now reads the track's tick-0 Shape (param 1) and
Amount (param 2) authoritatively and pushes timed rides for params 0/1/2;
`engine.cpp` applies them to `dist_`, which the four per-section instances mirror.
Verified on the shipped songs (scratch loader): Mutilated Species II 1572 DIST
events (1241 Amount, 319 Shape, 12 on/off; tick-0 Amount 0.000, riding up over
ticks 32..7520), TGV's KiloMix 1561, Groove It Up 505, and several songs START
at Amount 0 on the track, so before this they rendered with the distortion off
for the whole piece. New test `test_distortion_knob_automation`; ctest green.

ALL FIVE structural items (1-5) are now complete and consolidated on
dist-supertask. What remains: Phase-5 close-out (COMPONENT_MATRIX, PARITY,
FINDINGS_AUDIT) and, on Luke's sign-off, the fold to `main`. A whole-mix
regression on a distortion-riding song (e.g. Mutilated Species II) against a
full-rack ReBirth export is the natural end-stage check now that the rides play.
