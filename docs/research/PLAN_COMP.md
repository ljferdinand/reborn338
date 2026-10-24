# Compressor (supertask 6): measured facts

Provenance: measured from ReBirth exports (clean-room). Experiment selection was
informed by third-party binary analysis; no constant from it is used here
(`docs/PROVENANCE.md`). Campaign: `fixtures-2026-09-10/comp-probe`,
`renders/comp-probe-2026-09-10`, `tools/rbsgen/gen_comp_probe.py`.

## COMP chunk positions decoded (2026-09-10)

The shipped `Standard Components/Demo.rbs` COMP chunk is `[1, 70, 64, 127, 0, 0,
0, 0]`; the manual lists exactly two controls (Ratio 106, Threshold 107) in that
order, and the distortion lays its two knobs at chunk bytes 1/2 the same way.
Hypothesis: `[ver/on][ratio][threshold][meter?=127][0..]`. The P8 probe confirms
byte 1 is Ratio by measurement (below). This resolves the long-standing "ratio
and threshold undecoded" gap for the CHUNK byte positions (the exact laws are a
separate campaign). Our loader still reads only the target and on/off, so a
future decode change should add bytes 1/2.

## P8 first pass: ratio confirmed monotonic; threshold byte is non-monotonic

A sustained 303 drone (cutoff 127, RMS -25.8 dBFS) through the MASTER compressor,
gain reduction read as level versus comp-off (`c_ref`).

- **Ratio (byte 1), threshold 32:** GR = 0.00 / -0.23 / -0.47 / -0.71 / -0.95 dB
  at ratio 0 / 32 / 64 / 96 / 127. Monotonic, exactly zero at ratio 0. Byte 1 is
  Ratio.
- **Threshold (byte 2), ratio 96:** GR = -4.62 / -0.71 / -0.72 / -1.44 / -1.81 dB
  at threshold 0 / 32 / 64 / 96 / 127. NON-monotonic: a plain threshold would
  fall monotonically as it rises. So byte 2 is threshold-RELATED but drives a
  more complex gain law, and a single input level cannot characterise it.
- The compressor engages (routing/enable correct; up to -4.62 dB at threshold 0).

## Gaps / next (a proper compressor campaign)

The drone at -25.8 dBFS is too quiet: this compressor bites on loud material, so
except at the lowest threshold the signal sits near or below it and the reduction
is under 1 dB. To characterise the law:

- Use a LOUDER source (near 0 dBFS, e.g. a stacked drum hit) so the signal is
  well above threshold across the range.
- Add an INPUT-LEVEL dimension (source level x threshold x ratio) to map the real
  compression curve (ratio slope, knee, threshold in dBFS), since one input level
  is a single slice of a surface.
- Attack / hold / release timing from a level-step fixture.
- Detector type (RMS / peak / signed-sum): needs an antiphase L = -R source,
  which ReBirth's voices do not readily produce; may not be cleanly measurable.
- Whether the applied gain can exceed unity.

Byte positions decoded and ratio confirmed; the gain law is a follow-up campaign.
No DSP change here.

## Supertask 6 opened 2026-09-16 (branch `comp-supertask`, off `main`)

Phase-1 inventory confirmed from the manual (Chapter 8 page 68): the master
compressor is **stereo and acts on the whole mix**; a section compressor is
**mono** and only one section at a time can have it. User controls are exactly
**Threshold** ("only levels above it are affected; below passes unprocessed") and
**Ratio** ("amount of level reduction; higher = more drastic"), plus on/off and a
level-reduction meter (the `=127` COMP byte 3 is the meter, not a control). No
attack/release/make-up is exposed, so those are ours to fit, not ReBirth's to
match. No numeric ranges are printed, so every law here is measurement-only.

## Batch 1 (comp-law, 2026-09-16): loud source + input-level dimension

`rbsgen` gained a `chan_vol` fader lever (default 100, byte-identical); the
sustained 303 drone is swept in level through fixed threshold/ratio. Fixtures
`fixtures-2026-09-16/comp-law`, renders `renders/comp-law-2026-09-16`,
generator `tools/rbsgen/gen_comp_law.py`, analyzer `tools/measure/comp_curve.py`
(steady-state RMS over 2-6 s; GR = level(on) - level(off) at the same fader, a
phase-independent energy comparison, not a null test).

- **Fader (input-level) calibration, comp off:** dB-linear at ~0.37 dB per fader
  unit. Fader 40/60/80/100/127 = -47.98 / -40.58 / -33.18 / -25.79 / -15.80
  dBFS. **Fader 127 (-15.80) is the loudest sustained 303 available**; accent
  (knob 127 + per-step) adds nothing over it (also -15.80).
- **Transfer curves (input dBFS -> output dBFS):**
  - ratio 127, threshold byte 0: output PINNED flat at ~-32.5 dBFS for every
    input -48..-16. A hard limiter/leveller to the threshold.
  - ratio 127, threshold byte 32: knee at ~-26.6 dBFS (above it pins, below it
    passes at ~+1.4 dB). So **threshold rises with the byte** (0 -> ~-33,
    32 -> ~-27 dBFS).
  - ratio 64, threshold byte 0: knee ~-33 dBFS, gentler slope (~1.6:1 effective).
- **Below-threshold BOOST (contradicts the manual):** quiet inputs are lifted
  toward the threshold, up to +15 dB at threshold 0 / ratio 127, +4.7 dB at
  ratio 64, but only +1.4 dB at threshold 32. Either significant make-up gain or
  a feedback/leveller topology; gain clearly exceeds unity. P8 already showed
  ratio byte 0 = exact unity (no make-up at ratio 0), so the boost is coupled to
  ratio and/or threshold, not a fixed make-up.
- **Coupling:** ratio 127 and ratio 64 imply DIFFERENT threshold dBFS for the
  same byte 0 (~-50 with make-up vs ~-33), so threshold / ratio / knee / make-up
  cannot be separated from these lines. Needs the dense grid (batch 2): a
  threshold sweep at fixed hot input + ratio 127 (reads threshold(byte) off the
  pin), a ratio sweep at fixed hot input + threshold 0 (reads slope(byte)), and a
  below-threshold probe across thresholds (resolves make-up vs downward-only).

## Batch 2 (comp-law2): the threshold and ratio laws, dense

Fixtures `fixtures-2026-09-16/comp-law2`, renders `renders/comp-law2-2026-09-16`,
generator `gen_comp_law2.py`, analyzer `tools/measure/comp_surface.py`.

- **Ratio law** (threshold 0, hot input -15.8): GR climbs smoothly 0 -> -16.5 dB
  across the byte range; byte 0 is exact bypass. A clean monotonic ratio curve.
- **Threshold law** (ratio 127, hot input -15.8): the limiter pin rises -32.26 ->
  -18.81 dBFS as the byte goes 0 -> 127. Concave (big steps low, small high),
  well fit by threshold_dB ~ A + B*ln(byte + ~29).
- **Topology (below-threshold probe, quiet input -40.6):** at threshold 0 the
  quiet input is pulled UP to -32.8 (the same target as the hot input), a
  spectrally-confirmed clean gain > 1 (same drone spectrum, +7.76 dB; centroid
  121.5 Hz both). As the threshold rises past the quiet input, the upward action
  vanishes and the signal passes with a small (-1 to -2.5 dB) offset. So at low
  threshold this is a LEVELLER to a target, not downward-only.

## Batch 3 (comp-law3): make-up, ratio-independence, a transfer curve

Fixtures `fixtures-2026-09-16/comp-law3`, renders `renders/comp-law3-2026-09-16`,
generator `gen_comp_law3.py`.

- **No threshold-tied make-up:** at ratio 0 (exact bypass), sweeping threshold
  does nothing (GR = 0.00 at every threshold). The leveling comes from the GAIN
  LAW, not a separate make-up stage.
- **Threshold law is ratio-independent:** ratio 64's threshold sweep tracks the
  same monotonic shape as ratio 127's (deviations are soft-knee near the knee).
- **Transfer curve (ratio 64, threshold 64 ~ -18.8 dBFS):** only the input above
  threshold compresses (-2.9 dB); every sub-threshold input gets a flat ~-0.5 dB.
  So at HIGH threshold sub-threshold passes near-unity, while at LOW threshold it
  is boosted to the target. Same ratio, same input, opposite behaviour by
  threshold.

**Topology conclusion (steady state):** ReBirth's master compressor behaves as a
LEVELLER toward a threshold-set target, aggressive at high ratio, NOT the
manual's downward-only "levels below pass unprocessed". Steady-state data is
DEGENERATE between "downward + auto-make-up(threshold)" and "symmetric leveller":
no single-slope model reconciles ratio 127's hard hot-pin (slope ~1) with its
sub-threshold boost (implied slope ~0.63). Resolving the exact topology needs
either a fuller input x threshold PLANE at two ratios (fit the transfer surface
directly, topology-agnostic) or the dynamics.

## Batch 4 (comp-law4): the input x threshold PLANE at ratio 96

Fixtures `fixtures-2026-09-16/comp-law4`, renders `renders/comp-law4-2026-09-16`,
generator `gen_comp_law4.py`. GR (out - in) surface:

```
input:    -15.80  -25.79  -33.18  -40.58  -47.99
thr 32:    -6.67   -0.68   +1.10   +1.10   +1.10
thr 64:    -4.88   -0.72   -0.72   -0.72   -0.72
thr 96:    -3.38   -1.43   -1.43   -1.43   -1.43
thr127:    -2.16   -1.81   -1.81   -1.81   -1.81
```

**This resolves the topology.** Below threshold, GR is a FLAT offset independent
of input; above threshold, GR falls with input at the ratio's slope. That is a
feed-forward downward compressor, `GR = makeup - slope(ratio)*(in - threshold)+`,
where the make-up depends on ratio and threshold (it is 0 at ratio 0, batch 3,
so it scales with ratio; and it falls as threshold rises: +1.10 / -0.72 / -1.43 /
-1.81 dB at thr 32/64/96/127). The "leveller toward a target" impression is this
make-up: at low threshold / high ratio it is large and positive, so quiet signals
are lifted. The threshold-0 row (GR rising to +13 dB as input drops) is the same
law with the knee below -48 dBFS, so every input is above threshold.

The static model is therefore a soft-knee downward compressor with:
- ratio(byte): slope law, byte 0 = bypass, measured 0 -> -16.5 dB GR at threshold 0.
- threshold(byte): concave (~log), the knee/target rising ~-32 -> -19 dBFS.
- make-up(ratio, threshold): a gain that scales with ratio and falls with
  threshold, giving the leveller character.
The exact coefficients are the walled implementer's to fit from this measured
surface (ratio-64/96/127 lines + the ratio-96 plane).

## Ballistics batch (comp-ballistics): attack ~100-150 ms, fader is glided

Fixtures `fixtures-2026-09-16/comp-ballistics`, renders
`renders/comp-ballistics-2026-09-16`, generator `gen_comp_ballistics.py` (rbsgen
gained `vol_events` timed fader automation), analyzer `tools/measure/comp_ballistics.py`
(auto-detect is fooled by the drone's per-note ripple; the reliable read is a
focused 30 ms-window envelope at the known step times t=4/8/12 s).

- **ReBirth glides fader automation over ~100-120 ms** (seen in `step_off`, no
  compressor: a loud->quiet fader step ramps, it is not instant). This is the
  confound: a fader-step source cannot resolve ballistics faster than the glide.
- **Attack ~ 100-150 ms tau (slow):** at a quiet->loud onset the output jumps up
  with the fader glide, then keeps settling DOWN to the compressed steady value
  over ~250 ms; that continued settle, past the glide, is the attack.
- **Release** is not cleanly resolved here (fast, or masked by the glide).
- At the most aggressive setting (ratio 127, threshold 0) the leveller pins both
  the loud and quiet blocks to ~-31 dBFS, so the output barely steps and
  ballistics cannot be read there at all.
- **Attack/release are hidden internals, not ReBirth controls, so they are ours
  to fit** (COMPONENT_MATRIX section 6). A clean measurement wants a DRUM-TRANSIENT
  probe (instant onset, no fader glide), not a fader step; that is the next
  ballistics fixture if exact parity is wanted.

## Ballistics via drum transients (comp-ballistics2): the CLEAN read

Fixtures `fixtures-2026-09-16/comp-ballistics2`, renders
`renders/comp-ballistics2-2026-09-16`, generator `gen_comp_ballistics2.py`,
analyzer `tools/measure/comp_ballistics_drum.py`. A drum transient has an INSTANT
onset (no fader glide), so this supersedes the fader-step read.

- **Attack is FAST, ~10-20 ms** (NOT the ~100-150 ms the fader-step method gave,
  which was the fader glide). A loud 808 stack, gain(t) = on - off aligned at
  onset: the gain clamps from ~0 to the full reduction (~-11 dB at ratio 127 thr
  64, ~-6.8 dB at ratio 96) within ~30 ms, most of it in the first ~10 ms.
  Ratio-independent timing (ratio 96 same shape, less depth) as a time constant
  should be.
- **Release ~100-200 ms.** The pumping probe (loud kick every bar over quiet
  steady hats) shows the hat gain recovering from -3.2 dB at 125 ms after the kick
  to ~-0.2 dB by 250 ms (recovered by ~250 ms).
- Character: a snappy, punchy compressor: fast attack, moderate release. These
  are hidden internals (ours to fit) but now measured.

## State after the static campaign (2026-09-16)

The STATIC transfer surface is characterised (ratio law, threshold law, make-up,
the ratio-96 plane): a soft-knee feed-forward downward compressor,
`GR = makeup(ratio,threshold) - slope(ratio)*(level - threshold)+`, where make-up
scales with ratio (0 at bypass) and falls with threshold, giving the leveller
character. Attack is slow (~100-150 ms); release and detector type still coarse.
Ready to write the coefficient-free `SPEC_COMP.md` and hand the surface + laws to
a walled implementer (Role B) to fit coefficients. Still open if exact parity is
wanted: a drum-transient ballistics probe; detector type (peak/RMS/signed-sum);
section (mono) vs master (stereo) path.

## Implementer rounds + verification (2026-09-16/17)

Walled implementer (Sonnet) fit `compressor.cpp` twice on `comp-cleanroom`.
Round 1 (a5c84f8): soft-knee downward comp with auto make-up. Round 2 (aa8d9f9):
switched the detector from peak-blend to pure RMS (25 ms window) after Role A's
`comp_wav` harness (ReBirth's own uncompressed render fed through our compressor,
zero source confound; `tools/render/comp_wav_main.cpp`) showed the peak blend read
the tonal drone ~2 dB hot and over-compressed 1.5-2.6 dB. Role A VERIFIED round 2:
threshold sweep max 0.96 dB, ratio sweep 0.60 dB, ratio-96 plane 22/25 within
1.0 dB, ratio 0 exact bypass, ctest green, comp-off byte-identical across builds.

CRITICAL verification rule found: the DEFAULT Open303 303 voice renders ~15.65 dB
quieter than ReBirth (TPT matches to ~1 dB). NEVER measure on Open303; use
`REBORN_BASS_ENGINE=tpt` or the comp_wav harness. (BACKLOG 15; ab-parity skill.)

## Batch 5 (comp-law5): dense low-threshold, chasing the thr-0 corner

The one remaining miss is threshold byte 0 at the quietest inputs (worst -4.17 dB,
ratio 96 fader 40). Dense low-threshold surface (ratio 96, thr 0/8/16/24/32 x
fader), renders `renders/comp-law5-2026-09-16`, generator `gen_comp_law5.py`:

```
  input:    -15.80   -25.78   -33.18   -40.58   -47.98
  thr  0:    -8.93    -4.64    +0.31    +6.41   +13.18
  thr  8:    -8.30    -3.49    +1.82    +7.38    +7.38
  thr 16:    -7.73    -2.46    +3.10    +3.81    +3.81
  thr 24:    -7.18    -1.54    +2.12    +2.11    +2.10
  thr 32:    -6.66    -0.70    +1.09    +1.10    +1.10
```

threshold(byte) from where each row goes flat: byte8 ~-40, byte24/32 ~-33, byte0
below -48. make-up(byte) (the flat value): +7.38/+3.81/+2.11/+1.10 at thr8/16/24/
32, large at thr0. The MODEL LIMIT: at thr32 the above-threshold slope rises to
nominal near the knee (a normal soft knee, fits), but at thr0 (all inputs far
above threshold) ReBirth's slope FALLS with level (-0.92 near -> -0.43 far); the
compression relaxes far above threshold, a concave GR-vs-over the softplus knee
cannot make. The corner needs a gain computer whose slope relaxes far above
threshold and/or a larger low-threshold make-up. Round 3 (in flight) chases this.

## Round 3 negative + batch 6 (three-ratio dense low-threshold)

Round 3 (a5990d4, comp.cpp behaviour UNCHANGED from round 2) tried the concave
term three ways; each closed the corner only by regressing the passing ratio-127
and threshold-sweep cells, so it reverted. Its read (correct): the concavity is
ratio-DEPENDENT and one measured ratio (96) plus the ratio-127 anchor cannot fit
that dependence. (Feedback topology considered and rejected: steady-state feedback
with a linear static curve gives a CONSTANT effective slope, not the relaxing
shape; the fix is a nonlinear static curve, not a topology change.)

Batch 6 (comp-law6, ratio 112 and 127 dense low-threshold; with comp-law5's ratio
96 = three dense ratios) resolves the shape. The gain-computer SLOPE vs `over`
(level - threshold) is a HUMP: 0 below threshold, rises through the knee to a PEAK
~0.9-1.0 just above threshold, then RELAXES toward the ratio's nominal slope far
above. Measured nominal (far-above) slope: ~0.43 / 0.63 / 0.96 at ratio 96 / 112 /
127 (thr-0 loud end). The relaxation is naturally stronger at low ratio (more
distance from the ~1.0 peak down to nominal). thr >= 32 fits because it samples
only the knee-rise (small over); thr 0 samples far above (the relax), which the
current knee-to-nominal model lacks. Round 4 fits this hump/relax shape against
all three ratios with a strict no-regression guard on the passing cells.

## Material probe (2026-10-01): the detector reads PEAKS, not RMS

**Why it was run.** After the 2026-10-01 topology fix (`PARITY.md`, "The
compressor's rides and topology"), the demo songs moved toward ReBirth, but two
compressed stretches still sat 4 to 6 dB under it. Splitting each into source and
gain with scratch copies of the songs exported with the compressor routed nowhere
(`fixtures-2026-10-01/demo-comp-check`, `renders/demo-comp-check-2026-10-01`):
TGV's KiloMix bars 538 to 543 (bass 1 alone, a resonant filter sweep, section
compressor ratio 127 threshold 10) and Clodhopper bars 280 to 282 (the 909 alone,
ratio 127 threshold 26) both have our compressor cutting 3 to 3.6 dB MORE than
ReBirth's on a source 1.6 to 2 dB QUIETER. Every fixture of the 2026-09 campaign
used one source, the slid bright saw drone, on which an RMS detector and a peak
detector differ only by a constant that the threshold law absorbs. So the
detector type was never tested.

**The probe.** `fixtures-2026-10-01/comp-material` (`tools/rbsgen/gen_comp_material.py`,
35 fixtures), exports `renders/comp-material-2026-10-01`, reader
`tools/measure/comp_material.py`. Seven sources through the master compressor at
ratio 127 thresholds 0, 32, 64 and ratio 64 threshold 0, each with its
uncompressed export. Ours is ReBirth's own uncompressed export fed through our
compressor with `comp_wav` (exact for the master path: the engine agrees with it
within 0.031 dB on all 152 comp-law cells), so the source is identical on both
sides by construction. Gain reduction over 2.0 to 6.0 s, L and R separately (they
agree to 0.01 dB everywhere; every source is centred). Miss = our GR minus
ReBirth's; negative means we cut too much.

| source | uncompressed | crest | r127 t000 | r127 t032 | r127 t064 | r064 t000 |
|---|---:|---:|---:|---:|---:|---:|
| saw (calibration) | -15.80 | 2.56 | -0.62 | -0.64 | -0.48 | -0.19 |
| square | -16.50 | 1.90 | -2.83 | -2.94 | -2.83 | -0.41 |
| reso (cutoff 40, reso 127) | -19.12 | 3.78 | +2.81 | +2.80 | +2.94 | +0.83 |
| dull (cutoff 20) | -23.92 | 1.68 | -3.36 | -3.59 | -0.18 | -0.30 |
| stacc (decay 0, every 16th) | -17.51 | 3.90 | -3.28 | -3.43 | -2.46 | -0.39 |
| 909 kick, every beat | -22.08 | 7.15 | +3.24 | -2.98 | -3.10 | +0.24 |
| 909 closed hat, every 16th | -25.56 | 9.60 | -7.56 | -2.05 | -0.33 | -3.32 |

ReBirth's own GR on the same cells, for scale: saw -16.45 / -10.72 / -7.31 /
-4.86; hats +3.51 / +0.64 / -1.00 / +1.85 (it BOOSTS the hats at threshold 0,
where we cut them by 4 dB).

**What the steady rows say (MEASURED, with the interpretation stated).** At ratio
127 the slope is close to 1, so a constant miss across thresholds is a difference
in what the two detectors READ. Relative to the saw, ReBirth reads the square 2.9
dB lower than an RMS detector does, the resonant drone 2.8 dB higher and the dull
drone about 3.5 dB lower. A peak detector predicts, from each source's own crest
factor relative to the saw's: -2.6, +3.4 and -3.7 dB. An average (mean |x|)
detector predicts -0.3, -1.4 and +0.9 dB, which is wrong in sign twice. So
ReBirth's level detector is PEAK-LIKE, within about 0.6 dB on all three steady
sources; ours is RMS (25 ms mean square), chosen in round 2 because the old peak
BLEND over-compressed the drone, which was a threshold-calibration effect on a
single source and not evidence against a peak detector. The dull drone at
threshold 64 and every ratio-64 cell miss by less because the input sits near the
knee or the slope is ~0.6.

**What the moving rows say.** The staccato notes, kick and hats depend on the
detector's time behaviour as well as its type: how fast it rises on a hit, how it
decays between hits, and how that interacts with the gain smoothing measured in
comp-ballistics2 (full cut within ~30 ms, recovered by ~250 ms). These rows are
the fitting data for that; they are not yet decomposed.

**Unaffected.** The 2026-10-01 topology results (the master compressor's input
level, the section path's place and scale, the L+R sum) are level relations
measured on one steady source and hold for any detector type, so they stand.

**Status.** The compressor is REOPENED on this one point, the detector, with Luke's
sign-off of round 2 standing for everything else. Round 5 in `SPEC_COMP.md` is
the coefficient-free brief for a walled refit.

**Round 5 (2026-10-01, walled refit, branch `comp-detector-cleanroom`).** Fit and
residuals in `SPEC_COMP.md`, "Round 5 result". In short: a peak follower on the
lowpassed mid signal (two one-poles, -3 dB at about 4.45 kHz at 44.1 kHz, 4.2
kHz being the analog prototype; 8.94 ms attack, 26.29 ms
hold, 150.9 dB/s release, floored at the threshold), with no gain smoother, and
the static law re-read as the LINEAR `g = (1 - k) + k * T / max(D, F)`, which
fits all 146 compressed static cells within 0.023 dB. Every comp-material cell is
within 0.17 dB, all 152 static cells within 0.03 dB, and the kick and hat gain
trajectories within 0.08 and 0.39 dB rms. Through the engine the TPT drone's
crest (5.83 dB against ReBirth's 8.16) now reaches the output, up to +2.3 dB raw
on compressed cells (BACKLOG 22); corrected as PARITY.md does, every rides cell is
within 0.07 dB.
