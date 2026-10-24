# Findings audit and controlled-test plan

Dated 2026-07-17. Prompted by a correct observation: the early A/B testing
compared full mixes of real ReBirth songs against Reborn renders, and Reborn was
missing effects those songs use (the PCF and compressor are unwired; distortion
and delay ignore their automation). Any full-mix conclusion is therefore
confounded and cannot be trusted as a voice-tuning target. This document
separates what survives that from what does not, and lays out controlled tests
to re-answer the open questions.

## Scope of the confound

Confounded: every conclusion drawn from comparing a real song's full mix to a
Reborn render, because the two are not rendering the same effect chain.

Not confounded, and retained:

- **Code-reading findings.** What the loader and engine do is a fact about the
  source, independent of audio. These stand.

Everything with an audio measurement is confounded, including the tests I earlier
called clean. Disabling the insert patches (Dist, PCF) on a solo or a scratch
song does NOT remove the MASTER COMPRESSOR, which sits on ReBirth's output and
was left enabled: the Clodhopper solo was exported with it on, and the scratch
sweeps did not explicitly disable it, so in every case a compressed ReBirth
signal was compared against an always-uncompressed Reborn render. A compressor
squashes peaks, so this can bias exactly the resonance measurements we care
about. The generator now bakes an explicit compressor-off snapshot into every
song, and every ReBirth export must also be verified with the master compressor
off in ReBirth, before any audio comparison is trusted.

## Classification of prior findings

### Retained (valid)

- PCF is unwired: loader never parses the `PCF ` chunk or PCF track, never sets
  `pcfEnable`; 55 preset patterns not embedded; no bandpass mode. (Code.)
- Compressor is unwired: no `COMP` parse, stubbed transparent on master. (Code.)
- Distortion and delay are wired for static settings only; their automation
  tracks are not decoded; drum-knob automation is not applied. (Code.)
- Insert chain is parallel where the manual specifies series; tempo ceiling is
  300 vs a documented higher limit; no vintage-303 mode. (Code observations.)
- Calm and Fonda render dark specifically because the PCF is unwired. This came
  from the full mix, but it is a diagnostic of a missing effect, not a
  voice-tuning claim, and it is corroborated: Calm routes a 303 to the PCF (LP,
  pattern 25). Retained as an effect-gap finding, not as a tuning target.

### Retired as confounded (do not use; re-test with controlled songs)

- The seven-song full-mix spectral table (centroid, rolloff, flatness, RMS per
  song) as any kind of voice-tuning target.
- The zero-crossing-rate comparison as a quantitative claim. The qualitative
  point that Reborn synthesises hats and cymbals while ReBirth samples them is a
  code fact and is retained; the per-song ZCR numbers are not, because the mixes
  differ by missing effects too.
- All level and balance conclusions ("no consistent master offset, it is
  per-voice balance"). Confounded, especially by the missing master compressor.
- Trakka Rekka as the closest match and as the PARITY tuning anchor. It runs its
  master compressor and Reborn does not, so the match was against an unmatched
  compressor. The anchor must be re-derived with the compressor off or wired.
- Per-song brightness or darkness read as a voice indicator (Fast Forward,
  SuperNova, G-Spot notes), except the Calm/Fonda-to-PCF diagnostic above.
- **The 303 solo and scratch results, now largely re-validated.** These were
  measured against a ReBirth signal I feared was compressed. The resonance sweep
  was re-run comp-off and, on the never-mislabeled points (reso020, reso127),
  matched the earlier numbers to within 0.1 dB, so the compressor was not
  biasing these minimal solo-303 songs after all (it did matter for the
  Clodhopper real-song solo, whose song enables the compressor). The
  resonance-response finding is therefore confirmed clean: Reborn's resonance is
  too weak at the low end of the knob and too strong at the top (crossover near
  reso 90 to 100), while ReBirth's centroid stays at or above Reborn's. The one
  piece still owed a dedicated clean check is the filter slope itself, on a
  static comp-off scratch song.

### Open (need controlled tests)

- 303 cutoff-knob-to-frequency mapping: CALIBRATED this pass (exponential law,
  rolloff matched to ~2%). Still owed: verification at low resonance and at other
  notes (the fit used reso 120, note A).
- 303 resonance DYNAMICS: RESOLVED this pass. The earlier cap froze the knob's
  top half; the feedback now rises across the whole knob and is tuned against
  clean ReBirth STATIC resonance sweeps (cutoff 90/120, note A), matching peak
  prominence to ~1.7 dB and peak positions to within ~6%, env-swept to ~1.1 dB.
  Residual (filter-shape, not a constant): at high cutoff + high resonance
  ReBirth is more spectrally concentrated (steeper tail, thinner low end) than
  this ladder. Not yet verified: env-mod depth, accent, waveform, other notes.
- 303 slope, re-confirmed on a clean static scratch song.
- 303 env-mod depth, accent behaviour, waveform (saw vs square), and a few notes
  across the register.
- 808 voices: kick fundamental/pitch/decay, snare body vs snap, hats tone/decay.
- 909 voices: same set.
- Distortion: tone tilt, output level, and curve shape.
- Delay: timing, feedback, and tone of the repeats.
- PCF, once wired: preset pattern playback, bandpass mode, cutoff/reso/env-mod.
- Compressor, once wired: threshold/ratio/makeup behaviour.
- Per-voice gain staging, from single-voice level comparisons.

## Controlled-test suite

All tests use `rbsgen` to author a minimal song exercising one thing, exported
from ReBirth in song mode (compressor off) and measured against a Reborn render
with one identical method. Generator status noted per group.

1. **303, generator ready now.** Resonance sweep (done). Cutoff sweep at HIGH
   resonance (120) with env-mod 0 so the filter rings at the cutoff and its peak
   marks the cutoff frequency (done; moderate resonance was tried first and did
   not ring, the peak stayed on the note's harmonic). Then env-mod sweep, accent on/off,
   saw vs square, and the same at two or three notes. These pin the cutoff
   mapping and the resonance response, which is planning item 5.
2. **808 solo, generator ready and tests run (rounds 7-9, 2026-08).** `rbsgen`
   authors 808 patterns and `gen_batch808complete.py` / `gen_batch808cyoh.py`
   generate the isolated single-voice sweeps; scored with `tools/measure/score808.py`
   (power averaging). The sample path is fitted and merged; see
   `docs/research/DRUM808_FINDINGS.md`.
3. **909 solo, same extension.** Same voice-by-voice tests.
4. **Distortion, needs a generator extension** to route a 303 through the
   distortion and set its amount and tone. Test: one 303 at a fixed patch through
   the distortion at a few amounts.
5. **Delay, mostly ready.** A 303 with a delay send and set DELY parameters, to
   check repeat timing, feedback, and tone.
6. **PCF and compressor, deferred** until they are wired in the loader. The
   scratch songs that exercise them become the regression suite at that point.

## Generated in this pass

The generator now bakes an explicit compressor-off snapshot into every song
(COMP track param 0 = 0). Both sweeps below were regenerated with it and need a
fresh ReBirth export, with the master compressor also verified off in ReBirth.

- 303 resonance sweep (re-run, done comp-off): `scratch_303_reso{020,060,100,127}_cut090.rbs`.
  Result: Reborn's resonance response is too steep. Peak prominence above the
  fundamental at reso 20/60/100/127 is ReBirth -18.7/-15.9/-15.3/-14.1 dB vs
  Reborn -21.0/-21.0/-13.9/-9.8; the curves cross near reso 90 to 100 (Reborn
  under-resonant at the low end, over-resonant at the top), and ReBirth's
  centroid stays at or above Reborn's. Comp-off matched the earlier comp-on to
  within 0.1 dB on the clean points, so the compressor was not biasing these.
- 303 cutoff sweep: `scratch_303_cut{030,060,090,120}_reso120.rbs` (resonance
  120, env-mod 0, note A), exported comp-off and measured against Reborn.
  **Result (first clean A/B): Reborn's cutoff opens the filter far less than
  ReBirth's for the same knob.** Robust metrics all agree: at cutoff 30/60/90/120
  ReBirth centroid runs 528/804/1252/2004 Hz and 85% rolloff 770/1238/2073/3402,
  while Reborn runs centroid 334/468/595/727 and rolloff 549/775/1098/1427; the
  gap widens with cutoff (2.4x at the top). So the cutoff-knob-to-frequency
  mapping in `bass_synth.cpp` under-opens and is a concrete calibration target.
  Caveat: measured at high resonance, so resonance interacts; the rolloff and
  above-1kHz-energy metrics point to under-opening regardless.

## Calibrated in this pass

With both clean sweeps in hand, the 303 filter in `bass_synth.cpp` was calibrated
in a closed render-measure-iterate loop (five iterations, scored against the clean
ReBirth references above).

- **Cutoff mapping: solved.** The linear `300 + 2100*knob` law was replaced with
  an exponential (log-frequency) law, `620 * 2^(3.2*knob)` (~620 Hz to ~5.7 kHz).
  This matches ReBirth's cutoff-knob-to-85%-rolloff curve to within ~2% across the
  knob (was 29 to 58% too dark, worsening toward the top). The resonance-loading
  detune coefficient was softened (0.030 to 0.012) so high-resonance cutoffs are
  no longer pulled down as far.
- **Resonance: matched on peak prominence, first pass.** The feedback law was
  changed from `3.2*reso^0.36` (which self-oscillated at the top once the cutoff
  was widened) to `min(2.45, 2.05 + reso)`: a rising feedback that saturates at a
  cap just below the sustained-oscillation onset. Peak prominence now matches
  ReBirth to ~1 dB from reso 60 to 127 (mean 1.2 dB over the sweep).
- **Diagnostic that shaped the fit.** ReBirth's cutoff sweep is strongly resonant
  statically (peak prominence -0.6 to -6.7 dB, near the fundamental), yet its
  env-swept resonance sweep reads only -14 to -18 dB. The difference is envelope
  smearing: the static sweep parks the peak at a fixed frequency, the env-swept
  sweep drags it across the spectrum. Reborn's earlier high feedback self-
  oscillated into a sustained whistle that ignored the sweep and read as a sharp
  peak, so the cap keeps it below that onset.
- **Known residuals (the next target).** reso 20 (minimum) still reads ~2.6 dB
  under, and at high resonance ReBirth's spectral centroid rises well above
  Reborn's (243/413/959/1824 vs a flat ~345/512). Both point to the same
  resonance-DYNAMICS difference: ReBirth's resonance follows the env-swept cutoff
  and adds high-frequency energy, while this ladder stays more damped. Closing it
  needs DSP work on the onset/damping plus static resonance sweeps at several
  cutoffs. The fit is to two 1-D slices at note A; squeal at low cutoffs, accented
  patches, and other notes is unverified.

## Resonance dynamics rework (2026-07-18)

The first-pass resonance cap `min(2.45, 2.05 + reso)` froze every setting above
knob ~51 to one value: on a static note the resonance control did nothing over
its top half (reso 60/100/127 rendered identically). Reworked so the feedback
RISES across the whole knob, with the env-swept self-oscillation held off by an
analog-realistic high-frequency Q loss (the ladder sheds resonance as its corner
nears fcMax, so a cutoff pinned there by env-mod does not lock into a whistle)
rather than by capping the knob. The detune reference was split off from the
feedback so the cutoff calibration did not move, and baseCut was lowered from
~620 to ~554 Hz because a correctly more-resonant reso 120 needs less cutoff
frequency to hit the same rolloff. Tuned against the clean ReBirth static sweeps:
cutoff rolloff |err| ~3%, env-swept peak |err| ~1.1 dB, static peak |err| ~1.7 dB,
static centroid |err| ~175 Hz. Stable (finite, bounded) at all extremes.

## Next

Optional 303 depth (needs more ReBirth exports): env-mod-depth sweep, accent
on/off, saw vs square, and two or three notes across the register, to confirm the
cutoff and resonance hold outside note A. The high-reso brightness residual is a
filter-shape difference and would need a topology change, not a constant, so it is
low priority. Then extend `rbsgen` with 808/909 pattern authoring (the samples are
now in hand) so the drum tests can begin.

---

## Closed 2026-07-27 (supertask 1, the 303)

Three long-open items closed, and two corrections to entries above.

**Vintage-mode audit: CLOSED, no refit needed.** `DESIGN_MEMO_2026-07-23` §161
asked which synth-decay mode every reference WAV used, since ReBirth's Edit-menu
"Vintage ReBirth Synth Sound" switch selects a shorter (v1.5) or longer (2.0)
envelope decay and a v1.5-or-earlier song enables it automatically. Settled from
the FILE FORMAT rather than by measurement: every file we own, all 16 shipped
songs and every rbsgen fixture, is form type **RB40**, the 2.0 format, and a
v1.5 file carries a different form type our loader rejects outright. No
reference could have been captured in vintage mode. The switch is still
unimplemented as a feature.

**303 resonance re-fit: CLOSED.** Was blocked because resonance and cutoff had
only ever been measured as 1-D slices, and a resonance error necessarily reads
as a cutoff error in whichever slice you view it through. A 36-cell cutoff x
resonance grid (batch H) separated them. Rolloff error 0.572 -> 0.273 octaves
rms, prominence 10.10 -> 3.92 dB rms, fitted on a subset and validated on all
36. Details in `docs/COMPONENT_MATRIX.md` §1.

**"Reduce the 303 filter to 18 dB/oct": RETIREMENT CONFIRMED, with new
evidence.** Measured slope above the corner at low resonance, where no peak
distorts it: ReBirth runs -18.8 dB/oct at cutoff 127 resonance 0 and *steepens*
to -22.3 by resonance 60, while ours stays near -18.2. ReBirth is STEEPER than
us, not shallower, so reducing our order would move away from it. Stays retired.

**Correction to "the accent is almost purely a VCA lift".** That came from a
single knob setting and a whole-note average, where accent's bright attack and
its fast decay very nearly cancel. Sliced in 15 ms windows they separate: an
accented note starts up to 1.80x brighter and is BELOW the unaccented one by
60 ms, at every knob setting including zero where there is no level lift at all.
Accent is a shorter filter envelope plus a knob-scaled depth.

**Correction to the 303 TUNE law.** The "proven 2026-07-23" asymmetric
12/64-down, 12/63-up law was fitted from the two extreme fixtures, both of which
sit inside 8-wide end plateaus where a continuous law happens to agree exactly.
Measuring all 128 knob values showed a 32-slot lookup table with a 24-wide
centre detent. The old law was wrong by up to 1.38 semitones mid-range and
mistuned 7 of the 16 shipped songs. Now matches on all 128 values to 0.000
semitones, confirmed by ear.

---

## 2026-07-27, later: the waveform round

**ReBirth's "square" is a PULSE at duty 0.52286, and it sits 5.20 dB below its
saw. PROVEN, by a method that needs no model of anything else.**

Divide the square's harmonic series by the saw's, each relative to its own
fundamental, and every linear stage they share cancels exactly, leaving

    R(k) = |sin(k.pi.d)| / sin(pi.d)

which depends only on the duty cycle. At d = 0.5 that is 1 for odd k and ZERO
for even k, so any even harmonic at all is a pulse-width offset. ReBirth's R(k)
fits d = 0.52286 to **0.03 dB rms over 12 harmonics at three cutoffs**, and the
fitted d is identical at every cutoff, which is what proves it belongs to the
oscillator and not the filter. A same-amplitude pulse would put the square's
fundamental 6.00 dB above the saw's; ReBirth measures +0.79 dB at all three
cutoffs to 0.01 dB.

Whole-file square-minus-saw, the metric the old finding was written from:

| | peak dB | rms dB |
|---|---|---|
| ReBirth | -2.33 / -2.73 / -3.03 | +0.01 / -0.25 / -0.48 |
| ours, before | +1.25 / +0.81 / +0.52 | +5.03 / +4.96 / +4.92 |
| ours, after | -2.89 / -3.39 / -3.71 | -0.05 / -0.20 / -0.29 |

**Two further things the same fit establishes, both worth more than the fix.**

**ReBirth's SAW is an ideal 1/k sawtooth.** The model assumes it and could not
have matched to 0.03 dB otherwise.

**ReBirth's 303 signal path is LINEAR at these settings**, for the same reason:
a waveshaper anywhere in it would break the cancellation. Ours is not. Our own
R(k) misses the pulse model by 2.29 dB rms and *drifts with cutoff*, and the
residual is entirely the asymmetric drive stage: rendering the same fixtures at
a quarter of the drive brings the fit to 0.07 dB, the duty estimate to 0.52304
and the level to 0.5497, all within noise of ReBirth. See the open question in
`docs/COMPONENT_MATRIX.md` §1 before touching it, because it is not a free
change.

**Our 303 output carried DC and ReBirth's does not. FIXED.**

Whole-file mean, ReBirth **+/-0.000003**, ours **-0.0055** (saw) and **-0.0140**
(square). The cause is `satAsym(x, bias) = tanh(x+bias) - tanh(bias)`: the
subtraction cancels the offset for a signal sitting at zero, not the mean of one
that swings, so an amplitude-dependent offset survived and the VCA then gated
it, putting a step under every note. A 5 Hz output DC blocker takes us to
-0.000015, and costs 0.01 dB at the 110 Hz fundamental.

This is a plain defect rather than a mis-tuning, and it had gone unseen because
every 303 metric in the library is a spectral or level ratio and none of them
looks at the mean.

**The 303's VCA was the wrong SHAPE, and the old fit shows how that happened.**

ReBirth holds the VCA flat for half a step and then releases with a 21.92 ms
time constant. Ours decayed continuously from the attack with a 60 ms constant
toward a 0.08 floor, because the gate never closed between consecutive notes:
`fireBass` called `release()` only on a rest, so a run of sixteenths was one held
note and the plucky decay did all the articulating.

The old shape was fitted on **envelope coefficient-of-variation 0.83 against
0.79, and silence fraction 0.32 against 0.33**, two summary statistics of a whole
musical patch. Both numbers were matched. Neither can distinguish a continuous
decay from a hold-then-release, and the two shapes are audibly different. This is
the same failure as the accent "almost purely a VCA lift" reading and the
two-point tune fit: a statistic that averages over the thing being measured.
Slicing one note at 1.25 ms separates them at a glance.

Fixed by giving the voice the step duration and counting the gate down, with slid
steps tying. Recovered 2 dB of the whole-file rms deficit. The filter fit is
unmoved (batch H rolloff 0.273 to 0.280 octaves rms, prominence 3.92 to 3.95 dB),
17/17 songs still round-trip, and the whistle guard improved from -22.4 to
-23.1 dB against ReBirth's -29.4.

**Open and honest about it:** every batch G fixture is at 120 BPM, so whether
that gate is half a STEP or a fixed 62 ms is not determined by any data we have.
Proportional is implemented because a step-clocked sequencer and the hardware
both work that way. Batch I1 settles it.

**The 303 gate is PROPORTIONAL to the step. CLOSED by measurement, batch I1.**

It was implemented as half a step on reasoning alone and labelled as a choice.
Five tempos settle it. Fitted gate/step at 60/90/120/150/180 BPM: 0.455 / 0.482 /
0.474 / 0.475 / 0.465, spread 0.027, while the gate in ms ranges 113.8 down to
38.8. Our own render, which uses exactly 0.500, reads 0.472 through the same
estimator, so the estimator has a -0.028 bias and ReBirth's value is 0.500. The
release is a fixed time constant: 0.39 dB/ms at every tempo, tau = 22.2 ms.

Calibrating the estimator against our own known-value render, rather than
trusting its absolute output, is what turned 0.470 into 0.500.

**A measurement trap that cost a round, and will again.** The first pass fitted
0.420 / 0.474 / 0.470 / **0.150** / 0.432 and read as "neither hypothesis". The
150 BPM cell was not bad data and not a bad export: at 150 BPM a 16th step is
*exactly 11 cycles* of 110 Hz, so every note samples the waveform at the same
phases and averaging across notes stops smoothing the ripple. Peak-per-slice
fails this way, and so does an analytic (Hilbert) envelope, because its residual
ripple also sits at multiples of f0. A boxcar of exactly one oscillator period
integrates any ripple at a multiple of f0 to its mean and cannot fail this way.
Any envelope estimator in this project should use `period_env()`.

**"Decay reaches the amp envelope": DISPROVEN, batch I3.** Eight decay settings
at env mod 0. ReBirth's gate moves 58.2 to 59.8 ms and its release 25.50 to
25.75 ms across the whole knob, i.e. nothing, while its centroid climbs a steady
342.6 to 367.3 Hz. Decay does reach something at env mod 0 and it is not the VCA.
Recorded as unexplained rather than replaced with a second guess.

**303 env mod re-fit: CLOSED at low resonance, and the residual moved to the
filter.** `EM_OCT` 1.6 to 2.2 against nine env mod settings crossed with three
decays, 0.2215 to 0.0384 octaves rms. A curvature term was fitted and rejected
for buying 0.001 octaves. At the batch G settings (cutoff 50, resonance 100) we
are still 37% short of ReBirth's centroid, up from 54% short, so depth is no
longer the explanation there and the resonance-span thread owns what is left.

---

## 2026-07-30: the instrument was the problem

This round set out to do what `docs/research/RUNBOOK.md` section 5 recommended, make the
resonance-to-corner rise depend on cutoff and refit it jointly. Before fitting
anything it audited the measurement chain, and the audit changed what the fit
was scored against. The refit follows at the end.

### The primary metric snaps to the harmonic comb

**`rolloff()` in `tools/analyze/batchH_filter.py` cannot resolve the residual it
is being used to minimise.** It is a cumulative-magnitude threshold crossing on
a 110 Hz sawtooth, and the cumulative curve climbs in steps at each harmonic, so
the answer snaps to the comb. Moving the threshold by 2 percent moves the
reported rolloff by **0.137 octaves on average and 0.356 at worst**, against a
fitted residual of **0.237 octaves rms**.

It is worst where it does the most damage. The reso-0 row is the DENOMINATOR of
the resonance-span diagnostic that the corner-rise mechanism was derived from,
and five of its six cells sit between harmonics:

| cutoff | 16 | 40 | 64 | 90 | 110 | 127 |
|---|---|---|---|---|---|---|
| reso-0 rolloff lands at harmonic | 3.03 | 3.67 | 4.11 | 5.29 | 6.75 | 7.98 |
| octaves moved by a 2% threshold change | 0.047 | 0.340 | 0.301 | 0.270 | 0.269 | 0.356 |

So every span number carries roughly 0.3 octaves of metric noise in its
denominator. This does not overturn the DIRECTION of the earlier findings, since
both sides are measured the same way and the systematic pattern is much larger
than the noise in aggregate, but it does mean the last two rounds were tuning
against a residual comparable to their instrument's own resolution.

### prominence() is not measuring the resonance in 12 of 29 cells

Its band is a fixed 800 to 6000 Hz. Measured against where ReBirth's resonant
peak actually is, **12 of the 29 cells that have a peak at all have it BELOW
800 Hz**, entirely outside the window, so there the metric reports whichever
harmonic happens to be loudest above 800 Hz. That is a different quantity that
moves for different reasons. Every cell at cutoff 16 is in this group.

This explains an otherwise alarming result: the refit below improves rolloff
from 0.237 to 0.141 octaves rms while `prominence()` worsens from 2.71 to
3.94 dB, yet the harmonic-domain prominence, which measures the real peak above
the real passband in every cell, is flat at 3.19 against 3.14 dB.

### The inversion cannot support the claim built on it

`tools/analyze/invert_filter.py` produced the branch's headline mechanism. Three
independent defects, all confirmed by re-running it:

- **Stale constant.** It hardcodes `BASE = 582.0` while `bass_synth.cpp` has
  used 407.0 since the branch. Its printed corner frequencies are 1.430x too
  high. Ratios cancel it, absolute values do not, which is the same shape of
  defect the ab-parity skill already records for `fit_trim.py`.
- **The search clips.** Its `FC_REL` grid tops out at 1.70x our corner. At
  cutoff 127 the answer needs 1.727x, so it clips, and the `k` axis pins at its
  3.92 ceiling at cutoff 127 reso 110 and 127 with fit errors of 0.39 and 1.52.
  The reported +0.95 octave rise at cutoff 127 was read off two clipped cells.
- **It is not identifiable.** Using the script's own tolerance, single cells are
  fitted equally well by up to **seven distinct corners spanning 0.90 octaves**,
  and **6 of 24 cells have no fit inside tolerance at all**, so the reported
  corner there is the least-bad point rather than a fit.

Worst of all, the rise was computed as corner(reso 127) / corner(reso 0), and
**at reso 0 there is no resonant peak, so the corner is not observable there**.
The baseline of the claim was inferred at the one setting where the filter shows
neither a peak nor a knee.

**invert_filter.py is therefore retired as a verification instrument.** The
runbook's recommended next step, re-running it to confirm the corner surface
flattened, cannot be carried out with it as written.

### The hfQ elimination is stale, and null results have operating points

`docs/research/RUNBOOK.md` section 5 lists `hfQ` as eliminated, "provably inert on this
grid". That was measured before the branch. `hfQ` ramps in from 3000 Hz, and the
branch raised the static corner at cutoff 127 from about 3055 Hz to about
3471 Hz, which puts it inside the ramp.

Measured: at the shipped `RESO_OCT` of 0.70, disabling `hfQ` moves the
cutoff-127 span only 3.57 to 3.70. At `RESO_OCT` 1.10, which is the region a
cutoff-dependent fit reaches, it moves **4.30 to 4.81**, and cutoff-110 moves
4.29 to 4.43 against ReBirth's 4.45. The first probe in this round repeated the
original mistake, measured at the shipped operating point, and read "small".

**The general lesson is the more valuable half: a null result for a parameter is
only valid at the operating point where it was measured, and in this filter the
other parameters move the operating point.** Eliminations here should record the
operating point alongside the number.

### The replacement instrument

Both sides play a sawtooth at f0 and `wave_fit.py` already established that
ReBirth's saw is an ideal 1/k series, so

    |H(k*f0)|  proportional to  sqrt(PSD at harmonic k) * k

samples the filter's magnitude response every f0 Hz with the source divided out.
It is immune to threshold quantisation by construction, and it separates what
one scalar had entangled: passband, peak frequency, peak prominence and slope.
The peak is parabola-interpolated in (log f, dB) so it does not re-acquire the
comb, and cells with no peak are excluded rather than reported as a corner.

Scoring against the whole CURVE rather than a summary also answers the runbook's
own warning that two scalars cannot distinguish two shapes: it is 2151 points
across 36 cells instead of 72 numbers.

### The refit

Fitted by coordinate descent on the curve metric, on a checkerboard of 18 cells,
scored on all 36. Defaults in the code are UNCHANGED and these are environment
overrides, so the shipped build is byte-identical until Luke decides.

    CUT_SCALE 1.05   CUT_SLOPE 2.8   RESO_OCT 0.325
    RESO_OCT_CUT 0.425   K_B 3.40   HFQ_ON 4000

| | curve dB rms | peak freq oct rms | rolloff oct rms |
|---|---|---|---|
| pre-branch, RESO_OCT 0 | 11.94 | | |
| branch 303-corner-rise | 5.26 | 0.266 | 0.237 |
| this refit | **2.54** | **0.161** | **0.141** |

Fit and held-out cells score together (2.484 against 2.543), so it is not
overfitting.

**The defect is fixed rather than relocated**, which is the check the previous
two rounds skipped. Peak-frequency error by cutoff knob went from a monotone
0.68-octave tilt to a near-flat 0.14:

| ours minus ReBirth, octaves | 16 | 40 | 64 | 90 | 110 | 127 |
|---|---|---|---|---|---|---|
| branch | +0.37 | +0.13 | +0.05 | -0.15 | -0.17 | -0.31 |
| refit | +0.20 | +0.10 | +0.08 | +0.09 | +0.07 | +0.06 |

Gates: unit tests pass, 18/18 songs round-trip, whistle guard -23.4 dB against
the branch's -23.1 and ReBirth's -29.4 (all articulate), waveform linearity
unchanged at 2.31 against 2.39 dB residual.

### A correction to the branch's mechanism story

The branch says ReBirth's corner rises with resonance and ours fell, and added a
flat `RESO_OCT` of 0.70. Measured where a peak exists on both sides, reso 60 to
127, **ReBirth rises +0.51 octaves and the branch rises +0.82**, so the branch
overshoots. Its direction is still right and its improvement is real and large
on an instrument it was not fitted to (11.94 to 5.26 dB), but the specific
constant came from an inversion whose baseline cell cannot see a corner. The
refit keeps the coupling and makes it cutoff-dependent, spanning 0.38 octaves at
cutoff 16 to 0.75 at cutoff 127.

### Also done this round

The eight `REBORN_*` probe lambdas inside the per-sample loop of
`bass_synth.cpp` were calling `getenv` about 4.2 million times per 12-second
render. Hoisting them to function-local statics took a single render from 5.40 s
to 0.42 s and a 36-fixture batch from roughly 49 s to 1.6 s. Verified
**byte-identical on all 36 fixtures**, which is a valid null test here because
our renders are deterministic even though ReBirth's are not.

### Still open

The corner rise is still about 0.23 octaves too fast over reso 60 to 127 after
the refit, and our corner sits uniformly 0.06 to 0.20 octaves high, so a small
amount of the same defect survives. The drive stage
(`docs/COMPONENT_MATRIX.md` item 2) is untouched and still misses ReBirth's
linearity by 2.31 dB against 0.03.

`invert_filter.py` had its stale base corrected to 407.0 and carries a header
recording why its output should not be trusted for the corner-rise question.
`rolloff()` and `prominence()` in `batchH_filter.py` are left ALONE on purpose:
every historical number in this document and in `COMPONENT_MATRIX.md` was
measured with them, so changing them would silently invalidate the record.
`harm_response.py` and `curve_fit.py` are added alongside as the instrument for
new work, and the two can be run against each other on any build.

Nothing here is committed. The DSP change is two default-off environment probes
(`REBORN_RESO_OCT_CUT`, `REBORN_HFQ_ON` / `REBORN_HFQ_SPAN`) plus the getenv
hoist, so the shipped build is byte-identical to the branch and the fitted
constants above are not applied. Whether to take them is Luke's call.

---

## 2026-07-30, later: the drive shaper does not survive

`docs/COMPONENT_MATRIX.md` item 2 and `docs/research/RUNBOOK.md` item 2 both say the same
thing: ReBirth's 303 path is linear to 0.03 dB, ours is not, and the residual is
`satAsym()`, but it must not simply be turned down because the shaper was fitted
against `detroit_303` and the filter constants were fitted with it in place. The
required shape was a JOINT refit. Now that a 36-fixture probe costs 1.6 s that
is cheap, so it was done.

### The two instruments disagreed, which is the whole point

`tools/analyze/drive_probe.py` sweeps the drive and reports both, with the
filter held at the 2026-07-30 fit:

| drive | 1.00 | 0.75 | 0.50 | 0.35 | 0.25 | 0.15 |
|---|---|---|---|---|---|---|
| linearity residual dB (ReBirth 0.03) | 2.31 | 1.25 | 0.45 | 0.17 | 0.07 | 0.06 |
| filter curve dB rms | 2.54 | 2.74 | 3.41 | 3.84 | 4.24 | 5.07 |

Lowering the drive reproduces ReBirth's linearity and monotonically wrecks the
filter fit. That is the signature of a parameter standing in for another one's
error: the shaper's added harmonics were filling in a filter that falls off too
steeply above the corner. Our measured slope there runs -33 to -38 dB/oct where
ReBirth runs -21 to -23.

So the drive could not be judged at a fixed filter, which is exactly what the
matrix warned.

### Refitting the filter at the low drive answers it

Coordinate descent on the curve metric with the drive pinned at 0.25:

    DRIVE 0.25   CUT_SCALE 0.95   CUT_SLOPE 2.8
    RESO_OCT 0.325   RESO_OCT_CUT 0.425   K_B 2.80   HFQ_ON 4000

| | drive 1.0, refitted | drive 0.25, refitted | ReBirth |
|---|---|---|---|
| filter curve, 2151 points | 2.543 | **2.276** | 0 |
| peak frequency, oct rms | 0.145 | **0.139** | 0 |
| peak prominence, dB rms | 3.20 | **2.43** | 0 |
| linearity residual, dB | 2.31 | **0.09** | 0.03 |
| oscillator gain, square vs saw | 0.5393 | **0.5497** | 0.5493 |
| fitted pulse duty | 0.52422 | **0.52300** | 0.52286 |
| whistle articulation, dB | -23.4 | **-28.6** | -29.4 |

**The low-drive build wins on every instrument at once**, which is the test the
change had to pass. The shaper was not buying richness, it was compensating.

Two of those deserve emphasis. The oscillator now matches ReBirth on duty, level
and linearity SIMULTANEOUSLY, which the waveform round said would only happen if
the path were linear. And the whistle guard, which has sat about 6 dB shallower
than ReBirth through every previous round and was recorded as a mild
over-resonance to watch, closes to 0.8 dB. Both were predicted by removing a
nonlinearity, and neither was fitted for.

### A trap in the new metric, found and fixed

The first reading of this said peak frequency REGRESSED 0.161 to 0.762 octaves
rms, a 4.7x loss that would have sunk the change. It was an artifact of
`harm_response.py` itself.

`has_peak` only tested that the maximum was not the first harmonic. At reso 30
the response is nearly flat and there is no resonance, so `argmax` lands on
whichever harmonic noise favours. The low-drive spectrum shifted enough to flip
four reso-30 cells from "no peak" to "peak", and comparing those meaningless
positions produced errors of 1.59, 1.79, 1.83 and 2.60 octaves that dominated
the rms. The test now requires the peak to stand at least 1.5 dB above the
passband, which is above the flat-response scatter and far below any real
resonance (reso 60 runs 3 to 5 dB, reso 127 runs 12 to 18). Both builds then
score on the same 23 cells and the ordering reverses.

Worth recording as a shape of mistake rather than an instance: this is the same
error the round had already diagnosed in `prominence()` and in
`invert_filter.py`, which is reading a corner off a cell that has no corner. It
was rebuilt into the replacement instrument on the first try.

### The cost, and what it unblocks

The low drive renders about **12 dB quieter** (passband offset -14.8 dB against
-2.5 dB), so it needs a makeup gain. That is not a hidden cost of this change so
much as the arrival of an item already on the list: `COMPONENT_MATRIX.md`
records that our absolute output runs 2.2 to 4.8 dB below ReBirth's and that
calibrating it was deliberately deferred because the cutoff-dependent part
belonged to item 1. Items 1 and 2 are now both fitted, so the output level is
unblocked and should be calibrated next, once, rather than twice.

### Status

Not committed, and not applied. `REBORN_DRIVE` remains a probe defaulting to
1.0, so the shipped build is unchanged. Removing `satAsym()` from the signal
path proper, rather than scaling its input to a quarter, is the follow-up
implementation and has not been done; the 0.25 figure is where the sweep flattens
against measurement noise, not a claim that a quarter is the true value.

---

## 2026-07-30, later still: the output level is NOT yet a scalar

`COMPONENT_MATRIX.md` records our output 2.2 to 4.8 dB below ReBirth's with the
deficit growing with cutoff, and says not to correct it with a scalar until the
filter is fitted, because the cutoff-dependent part belongs to the filter and
calibrating early means calibrating twice. With items 1 and 2 fitted, this asked
whether that condition is met. It is not.

`tools/analyze/level_ab.py` splits the offset into the constant a makeup gain
would fix and the tilt it would only absorb. ReBirth's own peak level rises
**+4.40 dB** across the cutoff knob (rms +1.49), because opening a lowpass
passes more of the source. Ours minus ReBirth, per cutoff:

| build | peak mean | peak tilt | rms tilt |
|---|---|---|---|
| branch 303-corner-rise | -3.01 | -3.05 | -1.67 |
| refit, drive 1.0 | -3.13 | -2.87 | -1.57 |
| refit, drive 0.25 | -15.99 | -2.56 | -1.77 |

The refits barely moved the tilt, from -3.05 to -2.87 and -2.56 on peak. So our
level still rises only about 1.5 to 1.8 dB across the knob where ReBirth's rises
4.40, and **a single makeup gain would be absorbing filter error**, which is the
mistake already made once with `K_CUT` and once with the drive shaper. The
matrix's instruction stands unchanged and the level stays uncalibrated.

This is a negative result and it is the useful kind: it says the level is not a
loose end to be tidied, it is a symptom with the same owner as the filter.

### A correction to this round's own commit message

The commit `303 filter and drive` states the mechanism as "we run -33 to -38
dB/oct against ReBirth's -21 to -23" above the corner. **That reading was
ReBirth's noise floor, not its filter.**

`tools/analyze/floor_check.py` tests it without a reference: a harmonic that is
really present stands above the spectrum BETWEEN harmonics, and one that has
sunk into the dither does not. At cutoff 127 resonance 127 ReBirth's harmonics
stop clearing the inter-harmonic floor by 6 dB at **7615 Hz**, while the slope
window ran to about 16 kHz. Most of the window was floor, and a floor is flat,
and flat reads as shallow.

`harm_response.py` now drops harmonics more than 55 dB below the peak before
fitting the slope, and reports the slope as nan rather than as a number when too
few real points survive. With that guard the comparison at reso 127 reads

| cutoff | 16 | 40 | 64 |
|---|---|---|---|
| ReBirth | -18.5 | -19.1 | -27.0 |
| ours | -20.9 | -21.4 | -34.0 |

so **ours is STEEPER than ReBirth and the gap widens with cutoff**. The
direction of the level-tilt argument survives, since a steeper skirt recovers
less energy as the corner opens, but the specific figures in that commit message
are wrong and should not be quoted. The cutoff-127 slopes are still not reported
with confidence because too few harmonics clear the floor there on either side.

Note this does NOT revive the retired "reduce the filter to 18 dB/oct" item. At
low resonance ReBirth still runs steeper than a 4-pole would suggest, and what
is measured here is a resonance-dependent and cutoff-dependent divergence in the
skirt, not a wrong order. The retirement stands.

### Where this leaves the 303

The remaining defects now look like one defect seen three ways: the corner rise
is about 0.23 octaves too fast over reso 60 to 127, the skirt above the corner
is a few dB per octave too steep and increasingly so with cutoff, and the output
level therefore rises too little as the filter opens. All three are the shape of
the resonant skirt rather than the position of the corner, which the peak-
frequency fit has now flattened to within 0.15 octaves.

That points at topology rather than constants, and it should be treated as its
own investigation rather than another round of fitting the same five numbers.
The accent constants stay provisional and stay untouched until it settles, per
the standing order in `COMPONENT_MATRIX.md`.

---

## 2026-07-30, batch S: the skirt, measured properly for the first time

The skirt above the corner could not be measured at high cutoff on batch H,
because ReBirth's own harmonics are in its dither floor by 6070 Hz at cutoff 127
resonance 127 while the fit window ran to 16 kHz. Batch S exists to fix the
probe rather than the filter.

### The fixture lever, tested before a batch was built on it

A 303's filter does not track the keyboard, so raising the note samples the SAME
filter with a better probe. A sawtooth harmonic at frequency f has amplitude
proportional to f0/f, so doubling f0 puts 6 dB more signal at every skirt
frequency. Three probe fixtures at the worst cell settled it:

| f0 | 110 Hz | 220 Hz | 440 Hz |
|---|---|---|---|
| last harmonic clearing the floor by 6 dB | 6070 Hz | **15857 Hz** | 14966 Hz |

220 Hz is the pick. It covers two octaves above the cutoff-127 corner, and it is
reached with the step octave flag alone, leaving `tune` at the 64 centre detent,
so it is a single-variable change from batch H. 440 Hz buys nothing because the
harmonics get too sparse.

**Chain validated at the same time, and better than ever recorded.** The
f0 = 110 probe is the same settings as batch H's `h303_cut127_reso127`, exported
in a different session. Worst band deviation over seven bands **0.02 dB**, total
power within 0.01 dB, against the 0.07 dB the automation was first validated at.
The rack and the master compressor were off and nothing drifted.

### The result, and it is not what was expected

Slopes fitted only over harmonics clearing the floor on BOTH sides, 18 to 34
usable harmonics per cell, window taken from ReBirth's peak so both sides are
measured over the same band.

| cutoff, reso 60 / 90 / 127 | ReBirth | ours |
|---|---|---|
| 64 | -24.9 / -25.9 / -28.6 | -26.4 / -28.0 / -30.6 |
| 90 | -31.0 / -31.1 / -33.1 | -31.7 / -30.8 / -29.0 |
| 110 | -33.3 / -33.0 / -31.4 | -28.8 / -25.4 / -29.8 |

**The error CROSSES OVER with cutoff:** ours is 1.85 dB/oct too steep at cutoff
64, 1.23 too shallow at 90, and 4.56 too shallow at 110. So this is not one
constant offset. ReBirth's skirt steepens monotonically as the filter opens,
-24.9 to -31.0 to -33.3 at resonance 60, and ours does not follow.

### A hypothesis of mine, killed by its own evidence

The previous entry proposed that a too-steep skirt explains the output-level
tilt, since a steeper skirt passes less of the source and so recovers less
energy as the corner opens. **That is now contradicted.** Above cutoff 90 we are
SHALLOWER than ReBirth, which passes MORE energy, so our level should rise
faster than ReBirth's there rather than slower. It rises slower.

So the level tilt is a separate defect and remains unexplained. Recorded as
unexplained rather than replaced with a second guess, which is how the decay
knob at env mod 0 is handled and is the right precedent.

Peak level is dominated by the passband and the resonant peak rather than by the
skirt, so the two were probably never the same quantity. Worth checking directly
before another mechanism is proposed for the level.

### What was actually established

Three things, none of them a tuning constant. The skirt IS now measurable at
high cutoff, and the fixture recipe for doing it is written down. ReBirth's skirt
steepens with cutoff and ours does not track that. And the level tilt does not
follow from the skirt, so the remaining residuals are at least two defects and
not one, which is the opposite of what the previous entry guessed.

### Incomplete, and why

Batch S is 12 fixtures and **10 exported**. ReBirth hung part-way through
`s303_cut127_reso060`, holding that file open and refusing `WM_CLOSE` across
several attempts, so `cut127_reso090` and `cut127_reso127` are missing and the
cutoff-127 row is absent from the table above. That row matters, because the
crossover trend predicts it is where we are most shallow. The automation doc
forbids force-killing (it discards the window geometry), so this needs a manual
close and a re-run of the last three fixtures. The generator is deterministic
and `tools/rbsgen/gen_batchS.py` regenerates the identical set.

### Batch S completed: the cutoff-127 row, and it is not a simple tilt

ReBirth exited on its own after the repeated close attempts, so the three
missing fixtures were re-exported, including `cut127_reso060` whose file had
been written while ReBirth was stuck and could not be trusted. 12 of 12.

| cutoff 127 | reso 60 | reso 90 | reso 127 |
|---|---|---|---|
| ReBirth | -30.9 (n=12) | -24.4 (n=37) | **-19.4** (n=41) |
| ours | -25.2 (n=31) | -27.6 (n=28) | -25.5 (n=31) |
| ours minus ReBirth | +5.7 | -3.2 | -6.1 |

**The completed table does not support the tilt reading in the entry above.**
By cutoff the means run: too steep by 1.85 at 64, too shallow by 1.23 at 90,
too shallow by 4.56 at 110, then too steep by 1.24 at 127. That is not a
monotone crossover, and the cutoff-127 row alone spans +5.7 to -6.1, so its
mean is not describing anything real.

What the raw numbers do show, and it is the more interesting fact:
**ReBirth's skirt gets dramatically SHALLOWER as resonance rises at high
cutoff**, running -30.9 to -24.4 to -19.4 across the resonance knob at cutoff
127, on 37 and 41 usable harmonics for the two cells that matter. Ours stays
near -25 to -27 regardless. At its maximum resonance ReBirth's skirt is close to
a three-pole rolloff.

Treat the reso-60 cell with caution: only 12 of ReBirth's harmonics cleared the
floor there against 31 of ours, so it is the weakest cell in the table and it is
also the one carrying the positive sign.

**This bears on a RETIRED finding and must not be used to revive it casually.**
The audit retired "reduce the 303 filter to 18 dB/oct" on the evidence that
ReBirth runs -18.8 dB/oct at cutoff 127 resonance 0 and steepens to -22.3 by
resonance 60 while ours stayed near -18.2, i.e. that ReBirth is steeper than us
rather than shallower. That evidence was gathered at 110 Hz without a floor
guard, which is the exact condition now known to flatten a measured slope. The
new measurement runs the other way at high resonance.

That is NOT enough to reinstate the old hypothesis, which was about the filter's
ORDER as a whole and would move every cell. What is measured here is a
resonance-dependent shallowing at high cutoff only, with cutoff 64 still
showing ours too steep. But the retirement's evidence base is now known to be
confounded, so the retirement should be re-examined rather than cited.

Recorded as an open question with the measurement attached, not as a decision.
The next step is a slope measurement across the resonance knob at fixed high
cutoff with the floor guard and the batch S probe, which is a smaller and more
targeted fixture set than either batch here.

---

## 2026-07-30: RETRACTION. The high-cutoff skirt findings were noise

The two entries above report a skirt slope at cutoff 110 and 127 and draw
conclusions from it, including that ReBirth's skirt shallows sharply with
resonance and approaches a three-pole rolloff at maximum resonance. **None of
that survives. Do not use it.**

### How it was caught

The resonance sweep and the earlier grid both contain cutoff 127 resonance 127
at f0 = 220, identical settings, exported in two different runs. They disagree:

| cell | worst band diff | slope, export A | slope, export B | disagreement |
|---|---|---|---|---|
| cut127 reso127 | 0.029 dB | -19.4 (n=41) | -28.2 (n=14) | **8.8 dB/oct** |
| cut127 reso090 | 0.013 dB | -24.4 (n=37) | -31.2 (n=14) | **6.8** |
| cut127 reso060 | 0.016 dB | -30.9 (n=12) | -25.3 (n=40) | **5.6** |

ReBirth is reproducible: band energies agree to 0.03 dB, which is the tolerance
the automation was validated at. The ESTIMATOR is not.

### Why, and it is a mistake worth naming

The usable-harmonic count `n` swings between 41 and 14 for identical settings.
Up there the harmonics sit AT the noise floor, so whether any individual one
clears a 6 dB margin is decided by that export's noise realisation. A slope
fitted through whichever points happened to survive is fitted to noise.

**Selecting points by an SNR test and then measuring a quantity that depends on
which points were selected is not a guard against the noise floor. It is a way
to launder the noise floor into a finding.** The floor guard added one entry
earlier was introduced precisely to stop a floor-contaminated slope, and it
replaced a biased estimator with an unstable one, which is worse because the
instability is invisible in a single run.

The only reason it surfaced is that a later batch happened to re-export three
cells the earlier batch already had. That was not deliberate. **Deliberately
duplicating two or three cells across every batch would have caught this
immediately and costs almost nothing.** Worth doing from now on.

### What the measurement actually supports

Margin raised to 12 dB, and the fit window capped at the last harmonic
unambiguously present on BOTH sides, so the two are compared over a band where
both have signal rather than each over its own reach:

| cutoff | 64 | 90 | 110 | 127 |
|---|---|---|---|---|
| result | ours steeper by 2.04 dB/oct | ours steeper by 0.93 | UNUSABLE | UNUSABLE |

At cutoff 110 and above **neither side clears the floor more than an octave
above the peak**, so there is no slope to measure and the tool now says so
instead of returning a number. The 16-fixture resonance sweep exported for this
question therefore answers nothing, and that is the honest outcome of it.

What survives is modest and reproducible: **ours is steeper than ReBirth by
about 1 to 2 dB/oct at cutoff 64 and 90**, consistent in sign across all five
usable cells. That is it.

### Consequences for what is on record

- "ReBirth's skirt shallows with resonance at high cutoff, reaching -19.4
  dB/oct" is RETRACTED.
- "The skirt error crosses over with cutoff" is RETRACTED. The crossover was
  produced entirely by the unusable cells; where the data is sound the sign is
  the same at both cutoffs.
- The retired "reduce the filter to 18 dB/oct" item stays retired, and the
  suggestion one entry above that its evidence base should be re-examined is
  withdrawn. It was based on the -19.4 figure, which does not exist.
- The batch S PROBE finding stands: raising f0 to 220 does move the last usable
  harmonic from 6070 Hz to 15857 Hz, and the chain revalidation at 0.02 dB
  stands. The probe is better. It is still not good enough at cutoff 110 and
  above.

### The lever that has not been tried

More averaging. The dither floor is not variance that averaging cannot touch:
for a stationary signal, averaging N Welch frames drops the noise power by N
while the harmonics stay put. These renders are 12 s. Going to 60 s is five
times the frames for about 7 dB of floor, which is the right order to make
cutoff 110 usable, and it costs nothing but render time now that a batch is
1.6 s. Try that before authoring any more fixtures.

---

## 2026-07-30: the averaging lever fails, and the reason rewrites the estimator

The retraction above proposed longer arrangements as the untried lever: for a
stationary signal, averaging N Welch frames drops noise power by N. ReBirth
exports the whole arrangement, so 2 bars at 120 BPM is a 4 second export and
about 21 frames, and 32 bars is 64 seconds and 16 times that, worth roughly
12 dB of floor.

**It changed nothing. The last usable harmonic came out at 5739 Hz on both the
4 second and the 64 second export, identical to the digit.**

That is not what a noise-limited measurement does. Averaging always moves a
noise floor at least a little, so an unchanged answer means the limit was never
noise.

### What the limit actually was

Printing the harmonic levels instead of the summary shows both spectra carry
real content all the way out. At cutoff 127 resonance 127, relative to each
file's own peak:

| kHz | 3.8 | 5.5 | 7.3 | 9.0 | 11.7 | 15.2 |
|---|---|---|---|---|---|---|
| ReBirth | -1.9 | -31.0 | -48.5 | -55.8 | -64.5 | -67.6 |
| ours | -16.9 | -40.8 | -58.2 | -66.6 | -78.1 | -77.7 |

ReBirth sits about 13 dB above its own dither at 15 kHz. The harmonics were
there the whole time.

**The "last usable harmonic" test was measuring the wrong quantity.** It
compares a harmonic against the level BETWEEN harmonics, which is a contrast
ratio, not a signal-to-noise ratio. Contrast degrades toward high frequency
because the spectrum gets denser (16 struck notes put sidebands around every
harmonic, and any nonlinearity adds intermodulation), not because the signal
sinks. So the test declared cells unusable that were perfectly measurable, and
its instability across exports came from the same place.

That is the third estimator fault in this component in one session, and all
three are the same species: **a gate whose pass condition depends on the noise
realisation, applied to data where the gate itself is near its threshold.**
First the peak test counted flat cells as peaked, then the floor test selected
harmonics by contrast, then the contrast test was mistaken for an SNR test.

### The measurement that survives all of it

Fixed band, no gating, 64 second exports, cutoff 127 resonance 127:

| | peak | slope 6-14 kHz | level at 15 kHz |
|---|---|---|---|
| ReBirth | 3531 Hz | **-23.6 dB/oct** | -67.6 |
| ours, shipped | 3311 Hz | -28.2 | -77.7 |
| ours, fitted 2026-07-30 | 4194 Hz | -27.7 | -69.9 |

**Ours is about 4 dB/oct steeper than ReBirth above the corner**, and both our
configurations agree on that within 0.5 dB/oct, which is the stability the
gated estimator never had. It is the same sign as the 1 to 2 dB/oct measured at
cutoff 64 and 90, so the skirt is too steep across the range and more so at the
top.

This is the direction the retracted claim pointed, at roughly half to a third of
the magnitude it asserted, and without the resonance law it invented. The
retraction stands: the -19.4 figure and the shallowing law were noise. What is
established now is a single cell, measured stably, and it needs the resonance
sweep re-run with the fixed-band method before it is a law.

### An open discrepancy in the refit

The fitted configuration puts the peak at 4194 Hz against ReBirth's 3531, which
is 0.25 octaves HIGH, while on batch H the same configuration matched cutoff 127
to +0.06 octaves. Batch S is the same filter an octave up in the source, so the
two should agree. They do not, and that is unexplained. Possibilities worth
separating: the batch H peak estimate at f0 = 110 is coarser (one harmonic is a
larger fraction of an octave down there), or something in the fit is
note-dependent when it should not be. Do not treat the fitted constants as
settled until this is understood.

### Method note worth keeping

Use a FIXED analysis band and report the level at its top, rather than gating
harmonics by any SNR or contrast test. If the band is contaminated the level
readout says so directly, and nothing silently changes which points were fitted.

---

## 2026-07-31: the sweep re-scored, and a law that reproduces (at cutoff 110)

The 16-fixture resonance sweep needed no new ReBirth time, only a method that
does not gate. Band taken as 1.7x to 4x ReBirth's own peak, the same
frequencies for both sides, every point used, and the level at the top of the
band printed so a contaminated read is visible.

**The method was validated before its output was believed.** The sweep and the
earlier grid share six cells, exported independently:

| cell | grid | sweep | disagreement |
|---|---|---|---|
| cut110 reso 60 / 90 / 127 | -34.0 / -31.3 / -29.5 | -33.8 / -30.1 / -29.5 | 0.2 / 1.2 / 0.0 |
| cut127 reso 60 / 90 / 127 | -30.2 / -22.9 / -17.8 | -30.6 / -26.5 / -23.6 | 0.4 / 3.6 / 5.7 |

So **cutoff 110 is reproducible and cutoff 127 above resonance 60 is not**, even
with no gating, because the analysis band there sits 50 dB below the passband.
The gated estimator's worst on these same cells was 8.8 dB/oct; this is 1.2 at
cutoff 110. Cutoff 127 remains unmeasurable for slope and its numbers should not
be quoted.

### The finding, at the cutoff where it holds

At cutoff 110, both independent exports agree that **ReBirth's skirt SHALLOWS as
resonance rises**, about -34 dB/oct at resonance 60 to -29.5 at 127, roughly
4.5 dB/oct across the knob. **Ours does not do this**: it sits near -34 to -35
across the whole knob and if anything steepens slightly.

The difference, ours minus ReBirth, over the eight sweep points at cutoff 110:
mean -4.61 dB/oct, running -0.9 at resonance 45 to -6.7 at 118, a drift of
about -5.8 per 100 of knob. The drift accounts for roughly 4.8 of the 5.8 range,
so it is a trend rather than scatter. At cutoff 127 the same difference has a
drift of -0.04, i.e. none, but that row is inside the unreliable region.

**Ours is steeper than ReBirth in all 16 cells**, without exception, which is the
part that needs no law to be true.

### Standing relative to the retraction

The retraction stands and was correct. What it killed was the specific figure
(-19.4 dB/oct at cutoff 127 resonance 127, which does not reproduce: two exports
give -17.8 and -23.6) and the claim that the shallowing was established. The
DIRECTION it pointed at is now confirmed by a method that passes a
reproducibility test, at a different cutoff, with a smaller magnitude, and only
where the measurement is sound.

This is the shape a finding should take here: it survived being measured twice
by an estimator that was itself checked against duplicate exports first.

### What this says about the filter

A resonant ladder's skirt asymptote should not depend much on feedback; the
resonance changes the peak, not the far-field slope. ReBirth's does. That is a
topology statement rather than a constant, and it is the same conclusion the
peak-position work reached from the other side. It is now measured, at one
cutoff, reproducibly.

### Correction, same day: the "open discrepancy in the refit" was not one

The entry above flags the fitted constants as unsettled because they matched
ReBirth's cutoff-127 peak to +0.06 octaves on batch H (f0 = 110) and sat 0.25
octaves high on batch S (f0 = 220), and asks whether something in the fit is
note-dependent. **It is not, and there was no disagreement between the batches.**

Measured directly on the three probe fixtures, all cutoff 127 resonance 127,
only the source pitch differing:

| | f0 110 | f0 220 | f0 440 | spread |
|---|---|---|---|---|
| ReBirth | 3531 Hz | 3531 Hz | 3510 Hz | **0.01 oct** |
| ours, shipped | 3200 | 3311 | 3071 | 0.11 oct |
| ours, fitted | 4083 | 4194 | 3949 | 0.09 oct |

ReBirth's corner does not move with the note, which confirms the assumption the
batch S probe was built on. Ours does not either, to within about 0.1 octaves,
which is roughly two harmonics of peak-estimator resolution at these
frequencies and is therefore not evidence of tracking.

The fitted configuration is 0.21 to 0.25 octaves high at cutoff 127 resonance
127 at EVERY pitch, consistently. The +0.06 figure it was being compared against
is `harm_response.py`'s per-cutoff number, which AVERAGES over the resonances at
that cutoff. An average of a row against one cell of the same row is not a
disagreement between two batches, and reading it as one was an error.

So the fitted constants are not in question on these grounds. What is true, and
was already known, is that the fit is a compromise: it flattens the
peak-position tilt across the grid while leaving about a fifth of an octave at
the single hottest cell. That is a residual, not an inconsistency.

**Worth keeping as a habit rather than a fact:** when two numbers for "the same
thing" disagree, check they are the same statistic before hypothesising about
the system. Two of this session's false alarms were an average compared against
a member of the set it averages.

---

## 2026-07-31: ReBirth has about SIX render states, not random phase

`docs/REBIRTH_AUTOMATION.md` records that exports are not sample-deterministic,
measured on Calm as a sample correlation of 0.66 with every octave band matching
within 0.07 dB, and concludes "never null-test". That is correct but incomplete,
and the missing half changes how every reference should be captured.

**Six exports of ONE fixture, identical input, produced FOUR distinct outputs.**
rep2 matched rep4 and rep3 matched rep6, byte for byte. The renders that did
differ were sample-wise uncorrelated, r between -0.16 and +0.25, as documented.

Byte-identical collisions are impossible from a continuous random phase. For N
equally likely discrete states the expected number of distinct values in six
draws is N(1-(1-1/N)^6), which is 3.99 at N = 6 against the 4 observed. So
ReBirth starts each render from a small set of discrete states, on the order of
six, rather than from an arbitrary phase.

This was found while chasing what looked like an anomaly: one of six duplicate
batch S pairs came back byte-identical. At roughly six states, one collision in
six pairs is the expected rate, not an anomaly. The thing that looked like a
bug was the measurement.

### What it changes

**Null-testing stays forbidden.** Renders differ 13 times in 15. Nothing here
loosens the ab-parity rule.

**A single-pair noise floor is unsafe.** Two exports collide about one time in
six and a colliding pair scores exactly zero, so a single-pair floor is drawn
from a distribution containing zeros and will sometimes report that an objective
is perfectly precise. That already happened: the floor first measured for
cut110 reso127 was 0.000, while the true spread over all fifteen pairs of that
same cell is a median of 1.43 and a max of 1.58. The floor table committed
earlier the same day understates any cell where it happened to draw a collision.

    harmonic_curve, 15 pairs:  min 0.000  median 1.425  max 1.577
    band_distance,  15 pairs:  min 0.000  median 0.359  max 0.795

Use the median as the working floor and the max as the pessimistic one.
`tools/optimize/metrics.py:noise_floor_many()` does this; the single-pair
`noise_floor()` is superseded.

**Averaging references is the best return in the loop.** Average the harmonic
CURVES, never the waveforms, since the states are uncorrelated and summing them
cancels signal rather than noise. Per-harmonic spread across the six states
falls as 1/sqrt(N): 0.46 dB from one export, 0.32 from two, 0.27 from three,
0.19 from six. Three exports more than halves the floor for about thirty seconds
of ReBirth time.

For any cell where precision matters, capture it three times and average.
`average_reference()` in the same module.

### Method note

Two of this session's confusions came from treating a single measurement of a
noisy quantity as the quantity. The estimator instability that forced a
retraction was one, and the 0.000 floor was the other. Where a number is used to
decide whether another number is real, measure its distribution rather than one
draw of it.

## 2026-07-31: our ladder does NOT lose the natural peak rise. Lead closed.

Proposed at the end of the skirt-zero round: the ideal ladder's resonant peak
rises 0.69 octaves from feedback alone between k = 1 and k = 3.9, and ReBirth's
measured rise over its resonance knob is 0.51, so perhaps our TPT implementation
loses that natural motion and `RESO_OCT` is a patch over the loss rather than a
law. That would have been worth a lot, because it would have meant one
implementation defect behind the corner rise, the too-steep mean skirt and
possibly the level tilt.

**It is wrong.** Measured directly: one batch H fixture at cutoff 110, k set
through `K_A` with `K_B` and `K_CUT` zeroed and `RESO_OCT` off, so the feedback
is the only thing moving.

| k | ideal oct | ours oct | ours - ideal |
|---|---|---|---|
| 2.00 | 0.00 | 0.00 | 0.00 |
| 2.80 | 0.15 | 0.19 | +0.04 |
| 3.40 | 0.23 | 0.30 | +0.08 |
| 3.90 | 0.28 | 0.34 | +0.06 |

Over the whole range ours rises **0.34 octaves against the ideal 0.28**, so our
ladder reproduces the natural peak motion and slightly exceeds it. Nothing is
being lost and `RESO_OCT` is not compensating for a lost mechanism.

What that leaves standing: over the resonance KNOB, with `K_B` 3.40, k travels
about 1.6 to 3.4, which the ideal ladder answers with roughly 0.34 octaves of
rise. ReBirth measures 0.51. So ReBirth's corner rises about 0.17 octaves MORE
than feedback alone explains, and `RESO_OCT` supplies that excess. It is a real
residual, not a patch, though it was originally fitted on evidence from the
since-retired inversion and its magnitude should still be refitted.

**A measurement note that nearly cost this result.** The first run based the
comparison at k = 1.0, where there is no resonant peak at all, so the peak finder
returned the fundamental at 110 Hz and every octave figure downstream was
nonsense (it read a 3.9 octave "rise"). The same trap the inversion fell into:
asking where the corner is at a setting where the corner is not observable. Any
peak-referenced measurement must exclude cells with no peak, and here that means
never basing a ratio at low feedback.

## 2026-07-31: half the skirt "mean offset" is band placement, and the drift is not robust

`skirt_sweep.py` scales its 1.7x..4x band to **ReBirth's** peak and applies those
same frequencies to both sides, which is the right choice for comparing like with
like when the peaks agree. At cutoff 110 they do not always agree: our peak sits
0.19 octaves LOW at resonance 45 and 0.22 octaves HIGH at 127.

Re-measured with the band scaled to EACH side's own peak:

| reso | 45 | 60 | 75 | 90 | 100 | 110 | 118 | 127 | mean | drift/100 |
|---|---|---|---|---|---|---|---|---|---|---|
| diff, shared band | -0.3 | -2.3 | -3.9 | -4.4 | -4.0 | -4.1 | -3.6 | -4.8 | **-3.42** | **-4.16** |
| diff, own band | +5.1 | +0.7 | -3.9 | -4.4 | -4.3 | -3.6 | -2.0 | -1.6 | **-1.74** | **-7.11** |
| our peak vs theirs, oct | -0.19 | -0.17 | 0.00 | 0.00 | +0.13 | +0.12 | +0.22 | +0.22 | | |

**About half the mean offset is band placement, not filter.** -3.42 becomes -1.74
once each side is measured above its own peak. The skirt is half as wrong as the
record said.

**The drift is NOT robust to band placement, and that is the more important
half.** The two placements disagree about it: shared says the gap GROWS with
resonance (-4.16 per 100 knob), own says it grows faster (-7.11) but the upper
half of the own-band row moves the other way, -3.9 at resonance 75 closing to
-1.6 at 127. The two low-resonance cells, where the peak is weakest and its
location least certain, are doing most of the disagreement.

So the drift figure that motivated the whole stopband investigation is
placement-sensitive at exactly the cells where a peak-referenced band is least
trustworthy. The DIRECTION survives, because ours is steeper than ReBirth's in
every cell under either placement, and that needs no band alignment to be true.
The MAGNITUDE and the resonance dependence do not survive as stated.

**What to do before any further skirt fitting: find a placement-independent
statistic.** Options worth testing are a band anchored to the CUTOFF KNOB rather
than to either measured peak, or an integral over a fixed absolute band with the
level difference reported alongside so a contaminated read announces itself. Do
not fit the zero to the current drift number.

This is the fourth time a peak-referenced measurement has misled here (the
inversion's reso-0 baseline, the k=1.0 peak-rise baseline, the gated skirt
estimator, and now this). The pattern is consistent enough to be a rule: **any
statistic referenced to a measured peak is only as good as the peak, and needs a
control that does not depend on the peak at all.**

## 2026-07-31: RETRACTION. ReBirth's skirt does NOT shallow with resonance.

`tools/analyze/skirt_knob.py` anchors the analysis band to the cutoff KNOB, which
is an input, identical on both sides, and cannot move with either side's
response or with resonance. Measured that way, ReBirth's own skirt slope against
its resonance knob:

| band | cutoff | reso 45 | reso 127 | drift per 100 knob |
|---|---|---|---|---|
| peak-anchored | 110 | -32.9 | -29.5 | **+5.60** (the claimed shallowing) |
| knob-anchored | 110 | -31.1 | -32.1 | **-1.23**, flat |
| knob-anchored | 127 | -27.6 | -31.0 | **-3.89**, steepening |

**The shallowing was the band walking.** As resonance rises ReBirth's peak rises,
so a band scaled to that peak climbs in frequency, and higher up the spectrum the
slope reads shallower because it is further past the steep post-peak region.
Hold the frequencies still and the dependence vanishes at cutoff 110 and reverses
sign at 127. Neither shallows.

**What this retracts.** The finding recorded 2026-07-31 as "ReBirth's skirt
SHALLOWS with resonance and this one reproduces", and with it the conclusion that
"a resonant ladder's skirt asymptote should not depend much on feedback; ReBirth's
does; that is a topology statement". It is not a topology statement. There is
nothing there to explain.

Note that the earlier round did the right thing and checked reproducibility: two
independent export batches agreed to 1.2 dB/oct on the shared cells. They agreed
because they were both measured the same wrong way. **Reproducibility is not
validity.** A systematic error reproduces perfectly.

**What survives, and it is worth having.** Ours is steeper than ReBirth's in every
cell under every band choice tried, now by a roughly CONSTANT 3 to 4 dB/oct:
mean -4.23 at cutoff 110 with drift +0.30, mean -3.35 at cutoff 127 with drift
+3.64. A constant excess steepness is a much smaller and more tractable problem
than a resonance-dependent asymptote, and it does not require any topology
change: candidates are the oversampling and decimation, the corner position, and
the stage saturators.

**Consequences for work in flight.** `REBORN_SKIRT_ZERO` must NOT be fitted or
shipped: it was built to supply a resonance dependence that does not exist, and
`skirt_model.py`'s prediction, though correct as algebra, was answering a question
about a phenomenon that is not real. Both stay in the tree, default off, because
the algebra in `skirt_model.py` also proved the thing that DOES stand: feedback
cannot change a ladder's far field, so whatever causes our constant 3 to 4 dB/oct
is not the resonance law.

The direction call Luke gave ("change the filter order or the stopband shape") was
made on the retracted evidence and should be revisited before any topology work
starts.

### The constant skirt excess: five candidates eliminated, cause still unknown

Measured with the knob-anchored band, cutoff 110, ours minus ReBirth in dB/oct.
Baseline is the fitted config at -4.23 mean.

| probe | mean | verdict |
|---|---|---|
| `TPT_OS` 2 / 4 / 8 / 16 | -3.95 / -4.23 / -4.51 / -4.29 | **not oversampling.** Flat from 2 up. Only OS=1 is worse, at -5.40 |
| `HFQ=0` | -4.23 | **inert**, identical to four figures, at this operating point |
| `DRIVE=0.25` | -4.02 | not the shaper |
| `DRIVE=0.25 BIAS=0` | -4.97 | a linear path is WORSE |
| `U_SAT=8 FB_DRIVE=1` | -5.36 | not the saturators, and removing them is worse |

The oversampling result kills a principled hypothesis worth stating so it is not
retried: a TPT one-pole has a zero at Nyquist, four of them put four zeros there,
and that steepens the upper spectrum; oversampling moves the zeros up and should
have relieved it. It does not. Between OS 2 and 16 the excess moves 0.56 dB/oct
while Nyquist moves by a factor of 8.

**Worth noting for whoever picks this up:** in the knob-anchored band at cutoff
110 (3.9 to 9.2 kHz, which is 1.7x to 4x a 2295 Hz nominal corner) an ideal
4-pole would give about -18 to -23 dB/oct. ReBirth measures -31 and we measure
-35. BOTH are far steeper than four poles there, so whatever is adding steepness
is present in both and we merely have about 4 dB/oct more of it. That reframes
the question from "what is our filter doing wrong" to "what is the extra
steepness in both, and why is ours larger". The resonant peak's own post-peak
falloff is the obvious shared candidate and has not been separated out.

Not yet tested: the corner POSITION (ours sits 0.07 octaves high at this cutoff
in the fitted config, which should make ours read shallower, not steeper, so it
is unlikely but unmeasured), and the possibility that the excess is a function of
how far the band sits above each side's own corner rather than a property of the
filter at all.

## 2026-07-31: the harmonic instrument was miscounting, and it inflated every HF slope in the project

`find_f0()` located the fundamental by taking the loudest FFT bin, which is only
accurate to half a bin. At NFFT 16384 and 44.1 kHz that is 1.35 Hz. `harmonics()`
then searched a FIXED window of plus or minus 4 bins, 10.8 Hz, around each
predicted harmonic. The f0 error multiplies by the harmonic number and the window
does not, so the two cross at about k = 15. Above that the search window has
walked off the harmonic entirely and returns the leakage skirt beside it. Because
the miss grows with k, the recovered response falls faster than the filter does.

Measured on the real fixtures: batch S has a true f0 of 220.0014 Hz and the
snapping estimator returned 220.7153, an error of 0.715 Hz, which is 14.3 Hz by
harmonic 20 against a window half-width of 10.8. The harmonic was outside the
window it was being searched in.

### How it was caught, which is the part worth keeping

Not by inspection. By feeding the estimator a signal built from a filter whose
slope is known exactly. `tools/analyze/skirt_calib.py` synthesises a 220 Hz
sawtooth, applies an ideal 4-pole per harmonic in the frequency domain, gates it
with the fixture's own VCA and writes 16-bit dual-mono, then measures it with the
same code path used on ReBirth. Over 2x to 4x the corner it reported -31.1 dB/oct
where the exact answer is -21.4, and -43.4 where the exact answer was -21.6.

That test should have existed before any skirt number was quoted. The rule from
the 2026-07-30 round, "calibrate an estimator against a known value before
trusting its absolute output", was written down and then not applied to the
primary instrument.

### The fix

`refine_f0()` finds the f0 that best aligns the WHOLE comb, scoring by
interpolating the PSD at each of the first 40 harmonics rather than taking a
windowed maximum, so the score is smooth in f0 instead of piecewise constant.
Accuracy goes from 0.7 Hz to 0.001 Hz, verified against the synthetic where the
true value is known: 220.0012 recovered against 220.0000. Residual error at
harmonic 70 is 0.08 Hz against a 10.8 Hz window.

Re-measured, the calibration returns the exact answer to 0.2 dB/oct in every band
that is not into the dither floor. `find_f0(refine=False)` reproduces the old
behaviour and exists only so a historical number can be regenerated.

Energy integration over the harmonic cluster was tested as an alternative and is
WORSE: immune to the f0 error, but it sums the noise floor over 40 bins and reads
+19 dB/oct too shallow in the gated case. The peak estimator with a correct
centre is the right instrument.

### What changes

Every absolute slope measured above harmonic 15 was wrong, on both sides. Both
sides snap to the same wrong bin, so the error is largely SHARED and differences
survived it in direction if not in magnitude. That is why the sign findings held
up and the magnitudes never did.

| statistic | with the broken instrument | re-measured |
|---|---|---|
| skirt, knob band, cutoff 110, ours minus RB | **-4.23 dB/oct** | **-1.11** |
| skirt, knob band, cutoff 127, ours minus RB | **-3.35** | **-1.78** |
| ReBirth's own skirt at cutoff 110 | -31 dB/oct | **-18.0 to -19.6** |
| ours at cutoff 110 | -35 | **-19.2 to -21.7** |
| curve fit, shipped config, batch H pooled | 2.5 to 2.8 dB rms | **4.745** (2559 pts, was 2151) |
| measurement noise floor, pooled | 0.289 dB rms | **0.114** |

**RETRACTED: "both ReBirth and we are far steeper than four poles, so there is a
shared extra steepness to explain."** There is not. An ideal 4-pole reads -20.7
over 1.7x to 4x the corner and ReBirth reads -18.0 to -19.6. ReBirth is a 4-pole
and so are we. The reframe recorded earlier the same day, which was the most
confident-sounding paragraph in this file, was entirely an artifact.

**RETRACTED: the five eliminations for the constant skirt excess.** Oversampling,
`HFQ`, the drive shaper, the linear path and the saturators were each scored by
their effect on a number that was wrong by 3 dB/oct. The effect sizes they were
being judged against, 0.3 to 1.1 dB/oct, are the same order as the corrected
excess itself. Those probes are neither confirmed nor refuted; they are unmeasured
and must be rerun if the residual is still worth chasing.

**STANDS: ours is steeper than ReBirth's above the corner.** The sign survives,
which is now the fourth finding whose direction held while its magnitude did not.
The size is 1.1 dB/oct at cutoff 110 and 1.8 at cutoff 127, not 4.2 and 3.4. In
the one band that is both above the peak and clear of the floor at cutoff 64, the
difference is -0.06 dB/oct, which is nothing.

**STANDS and is unaffected: everything measured below harmonic 15.** The tune
table, the slide law, the pulse duty and gain (a ratio over 12 harmonics, and the
wave fixtures are at f0 = 110 with the duty fit running to k = 12), output DC, the
VCA shape, gate proportionality, env mod depth. None of these reads above the
crossover. The waveform fit is the closest call and sits just under it.

### The filter residual, now that it can be seen

Scored properly, the shipped config's error is not spread over the curve. It is
entirely above harmonic 20:

| harmonic band | ours minus ReBirth, mean dB | rms |
|---|---|---|
| k 1-5 | -0.17 | 1.10 |
| k 5-10 | +0.38 | 3.09 |
| k 10-20 | +0.31 | 2.90 |
| k 20-40 | -2.41 | 4.85 |
| k 40-90 | -5.00 | 5.52 |

Below 2 kHz we match ReBirth with no mean offset. Above it we run progressively
low, reaching -5 dB by 4.4 kHz and above, and the worst cells are all at cutoff
127 while the best are at cutoff 40. This is the SAME phenomenon as the skirt
excess, not a second one: 1 to 2 dB/oct integrated over the top two octaves is 3
to 5 dB. One defect, visible two ways.

The fit now sits 42x above its own noise floor rather than 8x, so the residual is
real signal and there is room to work. The old worry that the fit might be inside
the noise is resolved in the opposite direction from the one expected.

### Consequences for work in flight

The refit that exists as environment overrides (`CUT_SCALE` 0.95, `RESO_OCT`
0.325, `RESO_OCT_CUT` 0.425, `K_B` 2.80, `HFQ_ON` 4000, with `DRIVE` 0.25) was
fitted against the broken curve and its scores are void. It is not applied, so
nothing shipped is wrong, but it must be refitted before it goes anywhere near
`main`. The same applies to the peak-frequency residuals of 0.2553 and 0.1452
octaves: peaks at high cutoff sit near harmonic 14 to 27, which straddles the
crossover, so those numbers are contaminated to an unknown degree and need
regenerating rather than adjusting.

The accent constants stay provisional for the reason already recorded, with one
more added: they were measured through this instrument.

### The refit, redone on the corrected instrument

Both configurations rendered fresh and scored with the fixed `find_f0`, so this
is like for like. Coordinate descent from the previous round's optimum, fitted on
18 checkerboard cells and scored on all 36.

`CUT_SCALE 1.05, CUT_SLOPE 2.9, RESO_OCT 0.25, RESO_OCT_CUT 0.35, K_B 3.4,
HFQ_ON 4000`, drive left at the shipped value.

| instrument | shipped default | refit |
|---|---|---|
| curve rms, 2559 points | 4.745 | **2.627** |
| fit cells vs held out | | 2.586 vs 2.627, no overfitting |
| peak frequency, octaves rms | 0.2576 | **0.0996** |
| peak-frequency tilt across the cutoff knob | +0.34 to -0.33 | **+0.07 to +0.03** |
| corner rise over reso 60 to 127, excess | +0.29 oct | **+0.16** |
| peak prominence, dB rms | **2.85** | 3.19 |
| level offset | -2.54 dB | -2.42 |

It wins on three instruments and loses on the fourth. The prominence regression is
small and real and should not be smoothed over: the refit trades about 0.34 dB of
peak-height accuracy for 1.6 octaves of peak-position accuracy.

**The systematic HF deficit is gone**, which is the part that matters:

| harmonic band | shipped | refit |
|---|---|---|
| k 1-5 | -0.17 | -0.12 |
| k 5-10 | +0.38 | -0.07 |
| k 10-20 | +0.31 | +0.24 |
| k 20-40 | -2.41 | **+0.05** |
| k 40-90 | -5.00 | **-0.14** |

No mean offset survives at any band. What remains is 2.5 to 3.1 dB of rms scatter
with no tilt, plus a residual across the cutoff knob running -1.44 dB at cutoff 16
to +0.70 at cutoff 110.

Two checks that this is not the instrument fix flattering itself. The shipped
config's peak-frequency tilt measures 0.67 octaves here against the 0.65 recorded
before the fix, so that number survives unchanged and the improvement is not
invented. And the descent converged after one pass having moved three axes by one
grid step each, meaning almost all of the gain came from the previous round's
fitted starting point rather than from this descent: **the earlier refit was
substantially right despite being fitted through a broken instrument**, which is
what a shared bias predicts.

Still true: `RESO_OCT` remains about 0.16 octaves too aggressive over resonance 60
to 127, now measured on peaked cells only rather than inverted from resonance 0.

**NOT APPLIED.** Defaults are unchanged and this exists as environment overrides.
It changes the sound, and four defects in this project have been caught by ear and
none by these metrics, so it wants a listen before it becomes the default.

### APPLIED, 2026-07-31

The refit above is now the default in `src/devices/bass_synth.cpp`. Six constants
moved: `CUT_SCALE` 1.0 to 1.05, `CUT_SLOPE` 2.5 to 2.9, `RESO_OCT` 0.70 to 0.25,
`RESO_OCT_CUT` 0.0 to 0.35, `K_B` 3.70 to 3.40, `HFQ_ON` 3000 to 4000. They were
fitted jointly and no one of them means anything on its own.

Verified: the default build renders byte-identical to the environment-override
build, tests pass, 17 of 17 songs round-trip.

Ear check, six patches with the ReBirth export sitting alongside each pair,
level-matched to 0.05 dB on four of them and 0.6 dB on the widest:
**marked improvement across the board.** That includes `drv_closed`, where the
band metric had predicted a small regression (2.30 to 2.57 dB). The metric was
wrong and the finding is withdrawn: a whole-file third-octave summary is the
species the runbook already warns cannot distinguish shapes, and it should not
have been reported as a per-patch verdict.

`K_B` moving down to 3.40 also buys self-oscillation headroom rather than
spending it: 0.60 below the k = 4.0 threshold where it used to be 0.30.

## 2026-07-31: the level offset IS a scalar, for every patch that has resonance

Measured on the applied refit, passband level (mean of the first three harmonics)
in dB, ours minus ReBirth, over the batch H grid:

| cutoff | reso 0 | reso 30 | reso 60 | reso 90 | reso 110 | reso 127 |
|---|---|---|---|---|---|---|
| 16 | -0.21 | -2.71 | -2.42 | +0.20 | -1.33 | -1.41 |
| 40 | -1.98 | -3.81 | -2.29 | -1.05 | -1.36 | -1.53 |
| 64 | -3.93 | -4.64 | -1.82 | -1.13 | -1.38 | -1.56 |
| 90 | -5.94 | -5.00 | -1.57 | -1.21 | -1.40 | -1.51 |
| 110 | -7.24 | -5.05 | -1.47 | -1.22 | -1.39 | -1.51 |
| 127 | -8.01 | -4.98 | -1.18 | -1.23 | -1.38 | -1.46 |

**At resonance 60 and above the offset is flat at -1.2 to -1.6 dB across the whole
cutoff knob.** That is a scalar, and it is the first time this has been true. The
long-standing instruction not to apply a makeup gain because it would absorb
filter error no longer applies to those cells: there is no tilt left for it to
absorb. About +1.4 dB is the indicated correction.

**At resonance 0 and 30 the two sides diverge with cutoff, up to 8 dB.** Raw
levels show which side moves: from cutoff 16 to 127 at resonance 0, ReBirth's
passband rises **+10.9 dB** and ours rises +3.1. At resonance 90 and above both
sides are flat and agree.

**Do not act on the low-resonance half yet, and do not call it a defect.** The
statistic is the mean of harmonics 1 to 3, which is only a passband measure while
the corner sits well above 330 Hz. At cutoff 16 our own corner is about 550 Hz and
ReBirth's is unknown and probably lower, so on ReBirth's side those harmonics may
be sitting on the skirt rather than in the passband, and part of that +10.9 dB
would then be the corner sweeping up past the first three harmonics rather than a
gain change at all. This is the same shape of error as the four peak-referenced
misleads already in this file: a statistic whose validity depends on where the
corner is, evaluated where the corner might not be.

**The measurement that would settle it**: low-frequency energy integrated below a
fixed fraction of each side's OWN measured corner, or simpler, a fixture at a
lower note so that harmonics 1 to 3 are unambiguously below the corner at every
cutoff. Batch H is at f0 = 110 Hz; an octave down would put them at 55, 110 and
165 Hz and remove the ambiguity entirely.

Superseded by this: "ReBirth's level rises 4.40 dB across the cutoff knob and ours
rises 1.5 to 1.8, and the tilt survives the refit." Both figures were measured
through the miscounting instrument and pooled across resonance, which averages a
flat family with a steep one and describes neither.

## 2026-07-31: batch L settles the level question at low resonance, and opens a new one

Batch L is batch H dropped an octave, f0 = 55 Hz, so harmonics 1 to 3 sit at 55,
110 and 165 Hz and are below the corner in every cell on both sides. That removes
the ambiguity that made the batch H reading unusable. 6 cutoffs x 3 resonances
plus **three duplicate control cells**, which batch H should have had and did not.

Export chain validated before anything was read from it: the two clean duplicate
pairs agree to 0.147 and 0.144 dB rms, against the 0.114 pooled floor from batch
H. The third pair, cutoff 16 resonance 0, reads 0.655 because its high harmonics
are near the dither floor, which is expected rather than alarming.

### The result

Passband level, absolute dB, and the rise across the cutoff knob 16 to 127:

| resonance | ReBirth rise | ours | offset spread across cutoff |
|---|---|---|---|
| 0 | **+6.6 dB** | +0.8 | 5.78 dB |
| 30 | +0.1 | -0.0 | 0.74 |
| 127 | -0.2 | -0.1 | **0.15** |

**About 4 dB of the batch H figure was the artifact.** The +10.9 dB measured there
becomes +6.6 once the harmonics are unambiguously in the passband. The remaining
6.6 dB is real, and it is entirely a resonance-0 phenomenon: at resonance 30 and
above both sides are flat and the offset is a scalar to within 0.15 dB at
resonance 127.

**So the makeup gain is now justified for everything except resonance 0.** The
offset is +0.6 to +0.7 dB at resonance 127 and -3.0 to -3.6 at resonance 30, both
flat across cutoff.

### What is causing the resonance-0 rise, and why it is NOT settled

Read as a ladder DC loss of 1/(1+k), the implied feedback at ReBirth's resonance
knob ZERO falls monotonically with cutoff: 1.163, 0.622, 0.318, 0.122, 0.035,
0.000 at cutoff 16/40/64/90/110/127. That would mean ReBirth's resonance knob does
not reach k = 0 at low cutoff, and our `K_A = 0` is wrong there.

Two pieces of evidence, pointing opposite ways.

**For it.** Normalising each resonance-0 curve to its own passband and reading it
at fixed multiples of the corner, the low-cutoff cells sit 3 to 4 dB shallower
near and above the corner: at 1x the corner, cutoff 16 reads -14.5 dB and cutoff
127 reads -18.5. Residual feedback does exactly that. Pure cutoff-dependent gain
would leave the normalised shape unchanged.

**Against it.** At resonance 30 ReBirth is FLAT across the cutoff knob. If the
mechanism were an additive k that decays with cutoff, resonance 30 would show the
same rise attenuated, not zero. It could be reconciled by ReBirth applying a
passband makeup that tracks total k, but then resonance 0 would be flat too, and
it is not.

**Neither story explains both rows, so neither is recorded as the finding.** The
measurement is the finding. An exponential in the cutoff knob fits the implied k
to r = -0.984 in log space but its dB predictions miss by up to 1.0 dB, so the
functional form is not established either and must not be shipped as a law.

Also note the shape evidence is CONFOUNDED: it references our own corner law, and
at resonance 0 there is no peak, so our corner position there has never been
independently verified. It is the same class of dependency that has misled this
project four times. Treat it as a lead, not a result.

### Next step for whoever picks this up

Separate gain from feedback without relying on our corner law. The cleanest
approach available is to fit each ReBirth resonance-0 cell's whole normalised
curve to `G^4/(1+kG^4)` with BOTH the corner and k free, and see whether k comes
out non-zero at low cutoff with the corner landing somewhere sensible. Two free
parameters against roughly 40 usable harmonics per cell is a well-posed fit,
unlike the retired inversion that had one cell fitted equally well by seven
corners.

## 2026-07-31: ReBirth loses low end as resonance rises, and we do not. Largest open gap.

Found by cross-checking batch L against batch H. The two batches share 110 and
220 Hz, and ReBirth agrees with itself there to **0.07 and 0.01 dB**, so the
response is a function of FREQUENCY and not of which note is played, and the two
batches can be read as one instrument.

Level of the 55 Hz harmonic relative to the 220 Hz harmonic, same cell, both
sides. A fixed low-end rolloff would be one number repeated:

| cell | ReBirth | ours | gap |
|---|---|---|---|
| cut 16, reso 0 | +3.45 | +3.53 | 0.08 |
| cut 127, reso 0 | -4.76 | +0.97 | 5.73 |
| cut 16, reso 127 | -7.00 | -1.07 | 5.93 |
| cut 127, reso 127 | -6.24 | -0.18 | 6.06 |

ReBirth spans 10.46 dB across the grid, we span 4.60. At resonance 127 ReBirth
sits at about -6.3 dB in EVERY cutoff cell while we sit at -0.2.

**A ladder cannot produce this.** At 55 and 220 Hz against a 3190 Hz corner both
frequencies are deep in the passband, where the response is flat at 1/(1+k), so
raising feedback must attenuate them equally and their RATIO cannot move. ReBirth's
ratio moves by 10 dB. Something in its path removes low frequencies in proportion
to resonance.

Our side behaves like the textbook, which is the defect. The makeup gain at
`bass_synth.cpp:1121` is `(1.0 + kEff * 0.75)`, frequency-flat and scaled by
feedback, so it restores the ladder's DC loss uniformly and holds the ratio near
zero by construction.

**Why this is not a gating artifact.** At 55 Hz a half-step gate at 120 BPM is
only about 3.4 cycles of the fundamental, so leakage is a fair worry. It is ruled
out by the resonance dependence: the gate is identical in all 18 cells and does
not know the resonance knob, yet the effect moves 6 to 10 dB with it. A gating
artifact would be resonance-independent and would appear on both sides equally.

**Why it matters more than its dB suggests.** It applies to the passband, where
the energy is, on resonant patches, which is most 303 material. On a max-resonance
bassline we carry roughly 6 dB more energy at 55 Hz than ReBirth does. That is a
plausible component of the "snap" ReBirth has that we have been reaching for by
ear: less low-end weight under a resonant sweep reads as tighter.

**Not fixed, and deliberately not guessed at.** The obvious mechanisms are a
resonance-scaled low shelf, a lowpass in the feedback path (feedback strong at low
frequency, weak above ~200 Hz, which produces exactly this sign), or a makeup gain
that is frequency-shaped rather than flat. Distinguishing them needs the whole
low-frequency curve fitted across the resonance knob, not two harmonics, and batch
L only samples 55/110/165/220 Hz. A fixture set at several notes spanning 40 to
200 Hz would resolve the shape properly.

Note this cuts against the long-standing task 44 framing, "whole-song spectral
tilt: bass-light, treble-heavy on every song". On the 303 in isolation we are
bass-HEAVY relative to ReBirth whenever resonance is up. Either task 44 predates
too many fixes to trust, or the whole-song deficit lives in the drums and the
summing rather than in the 303, and the two have been averaged together.

## 2026-07-31: the low-end loss is a ONE-POLE HIGHPASS near 100 Hz, switched on by resonance

Batch N resolves the shape that batch L could only detect. Two low notes whose
harmonic combs interleave, C1 at 32.703 Hz and E1 at 41.203, giving eighteen
points below 340 Hz from two fixtures. The notes are TIED rather than plucked, so
the gate never closes: at 32.7 Hz a plucked half-step gate at 120 BPM is 2.0
cycles of the fundamental, which is not a measurement, while tying gives about 65
cycles per bar.

### Validation, run before reading anything from the batch

| check | result |
|---|---|
| duplicate controls, ReBirth side | **0.001 and 0.003 dB rms** |
| the two combs where they nearly collide, 163.5 vs 164.8 Hz | agree within 1.5 dB in every cell, both sides |
| tied vs plucked, same cell | agree at resonance 127 (0.02 dB both sides), **disagree at resonance 0**: ReBirth -2.45 dB, ours -0.02 |

The duplicate figure is worth noting on its own. A SUSTAINED tone reproduces 100x
better than a plucked one (0.001 dB against batch L's 0.145), because ReBirth's
roughly six discrete render states differ in how a transient lands and a
continuous tone has no transient to land. **For any measurement that does not
need the envelope, tie the notes.** This is the cheapest precision available in
this project and it was sitting unused.

The tie-versus-pluck disagreement at resonance 0 is unexplained and is recorded as
an open observation, not folded into anything below. It is one-sided, which rules
out the obvious "2 cycles is not enough" explanation applying equally.

### The result

Fitting a one-pole highpass, corner and offset free, to each curve below 340 Hz:

| cutoff | resonance | ReBirth corner | residual | ours | residual |
|---|---|---|---|---|---|
| 64 | 0 | 1.0 Hz | 2.20 | 1.0 Hz | 1.06 |
| 64 | 60 | 88.1 Hz | 0.43 | 1.0 Hz | 0.47 |
| 64 | 90 | 104.8 Hz | 0.41 | 1.0 Hz | 0.47 |
| 64 | 127 | 109.8 Hz | 0.42 | 1.0 Hz | 0.49 |
| 127 | 0 | 47.4 Hz | 1.13 | 1.0 Hz | 0.78 |
| 127 | 60 | 95.3 Hz | 0.47 | 1.0 Hz | 0.51 |
| 127 | 90 | 98.1 Hz | 0.45 | 1.0 Hz | 0.51 |
| 127 | 127 | 98.8 Hz | 0.45 | 1.0 Hz | 0.52 |

**A one-pole highpass near 100 Hz describes ReBirth's low end to within 0.45 dB
once resonance is engaged.** Our own curves fit at the grid's minimum corner,
1.0 Hz, meaning no highpass at all, and they do so with the SAME residual, 0.47 to
0.52. That equality is the important part: the residual is the measurement's own
structure, roughly 0.5 dB of comb-to-comb scatter, and not a failure of the model.
The model is as good as the data allows.

**It saturates by resonance 60.** ReBirth's curves at resonance 60, 90 and 127 are
identical to within 0.25 dB across the whole comb: at 32.7 Hz they read -8.86,
-9.05 and -9.10 dB. This rules out any mechanism proportional to feedback, because
the resonant peak provably keeps growing past resonance 60 while this stops.
Whatever it is, it is switched on rather than scaled.

**At resonance 0 the model does not apply.** The fits there are poor (residual
1.13 and 2.20 against 0.42 for the resonant cells) and the corner is unstable
between cutoffs. Resonance 0 has its own separate open question already on file.

### What to do with it

The observable is clean and this project fits ReBirth's OUTPUT as a black box, so
implementing the observable is legitimate even though the internal cause is not
known. Candidate: a one-pole highpass on the 303's output whose corner rises with
the resonance knob and saturates near 100 Hz by knob 60.

One mechanism is ruled out for whoever tries to reason it through. A highpass on
the RESONANCE FEEDBACK path, which the `rb` core already implements and which is
the obvious guess, produces the WRONG SIGN. With the feedback highpassed, low
frequencies see no feedback and therefore no 1/(1+k) loss, so they come out
LOUDER than the midband, not quieter. A lowpass on the feedback gives the right
sign but would flatten the resonant peak, which is at high frequency and provably
intact. A makeup gain applied through a highpass, so that the 1/(1+k) loss is
restored above 100 Hz but not below, fits both the sign and the saturation.

### The highpass implemented as a probe, and cross-validated

`REBORN_LF_HP` (default **0.0**, off) adds a one-pole highpass before the VCA
whose corner follows the fitted saturating law
`fc = LF_HP * r / (r + LF_HP_KNEE)` with `r` the resonance knob and
`LF_HP_KNEE` 0.141. Applied before the VCA rather than at the output because a
sustained tone cannot separate the two, and that is the choice which does not
highpass the envelope's own transients and thin the pluck.

Swept on batch N, scored on the six resonant cells as the 32.7 Hz gap:

| `LF_HP` | mean abs gap | max |
|---|---|---|
| off | 10.10 dB | 10.80 |
| 100 | 1.76 | 2.09 |
| **119.9** | **0.50** | **0.84** |
| 140 | 0.58 | 1.08 |
| 165 | 1.70 | 2.21 |

0.50 dB is the measurement's own scatter, and 119.9 came from the independent
corner fit rather than from this sweep, which is why the agreement is worth
something.

**Cross-validated on batch L**, a different fixture set at a different note that
the law never saw. The statistic is the 55 Hz harmonic minus the 220 Hz harmonic,
over the twelve cells with resonance:

| | mean abs gap | max |
|---|---|---|
| probe off | 5.35 dB | 6.12 |
| probe on | **0.80 dB** | 1.88 |

At resonance 127 the gap collapses from +5.93/+6.12 to +0.14/+0.34 in every
cutoff cell.

**The residual is at resonance 30 and is a real limitation.** ReBirth's low-end
loss there runs -2.36 dB at cutoff 16 to -6.03 at cutoff 127, so below the knee
it depends on CUTOFF, and our law does not. Batch N sampled resonance 0, 60, 90
and 127, where the effect had already saturated and was cutoff-independent, so
this was invisible when the law was fitted. Resolving it needs resonance points
between 0 and 60 crossed with cutoff.

**BATCH H CANNOT JUDGE THIS CHANGE, and the reason is structural.**
`curve_fit.curve()` normalises every curve to the mean of its first three
harmonics. At batch H's f0 = 110 Hz those are 110, 220 and 330 Hz, which is
exactly where this highpass acts, so the metric divides out the thing being
measured. Its low band, the part it can still see, improves (k1-5 rms 1.01 to
0.56 and mean -0.12 to +0.08) while the pooled figure drifts 2.627 to 2.749
through the shifted reference. Do not read that pooled number as a regression,
and do not tune this constant against it.

That is a general caution worth carrying: **a metric normalised to the passband
is blind to changes in the passband.** It has been the right metric all month
because everything under investigation was above the corner.

Verified: tests 1/1, 17 of 17 songs round-trip, and the default build renders
byte-identical to before the change, so nothing ships until this is judged.

## 2026-07-31: ear check on the low-end highpass, and what it retracts

Six patches, ReBirth alongside two of our builds, all three matched on 300 Hz to
5 kHz so the comparison is timbral rather than a loudness test. Luke's results:

| observation | patches |
|---|---|
| **current and lowend sound IDENTICAL** | all six |
| indistinguishable from ReBirth | `drv_closed`, `drv_open` |
| ReBirth has more of a **"chirp"** | all Clodhopper, `drv_acid`, `detroit_303` |
| ReBirth's filter sweep more pronounced, "almost wahwah-like, the bass smoothly swooping". Present in ours, less so | `clodhopper_303-2` |
| our version plays **more notes near the end of the loop** | `clodhopper_303-1` and `-2` |

### RETRACTED: that the low-end gap is part of the "snap"

Recorded earlier the same day as "a plausible component of the 'snap' ReBirth has
that we have been reaching for by ear". **It is not.** A change that closes 5 to
10 dB of low-end error is completely inaudible to Luke on this material. Whatever
the snap is, it is somewhere else, and the chirp is now the better candidate
because it is the thing he can actually hear missing.

This is a useful failure to have on file. The reasoning was that low-end weight
under a resonant sweep reads as untight, which is plausible acoustics and was
still wrong. **A mechanism that would explain a perception is not evidence that it
does.** The only thing that settles which measured defect corresponds to a heard
one is asking, which took one round and should have come before the speculation
rather than after it.

### What the inaudibility does NOT do

It does not argue against applying the highpass. Parity with ReBirth's output is
the project's goal ranked above all others, the measurement is unambiguous (batch
N 10.10 dB to 0.50, cross-validated on batch L at 5.35 to 0.80), and there is no
ear objection in either direction. It moves the decision to measurement alone.
Tracked as task 56, blocked on task 57.

### The chirp, and where it is not

Absent from `drv_closed` and `drv_open`, which Luke cannot tell from ReBirth at
all. Those are the filter-closed and low-resonance cases, so the chirp needs
resonance or an envelope sweep to appear. That points at the filter envelope's
ATTACK rather than at the oscillator or the VCA, and it may be the same defect as
the under-pronounced sweep on `clodhopper_303-2`. Check whether one fix moves both
before treating them as two problems.

Worth connecting to an older unexplained item: ReBirth's centroid climbs 0.10
octaves across the decay knob while its gate and release provably do not move, so
decay reaches something besides the filter envelope and the VCA and we never found
what. That may be this, seen statically instead of heard.

### The rhythm defect is the priority

Our Clodhopper plays more notes than ReBirth near the end of the loop, on both
303s. `docs/research/GOALS.md` ranks sound AND RHYTHM, and a note-count difference is the
more severe class. It is also the third recurrence of one family, after the tied
rest treated as a note-off and the accent row read on a rest, both of which were
interactions between two flags that no one-control-at-a-time fixture could reach
and both of which Luke found by ear. Tracked as task 53 and it goes first.

## 2026-07-31: the "extra notes" are our DC BLOCKER ringing, not the sequencer

Luke reported our Clodhopper playing more notes than ReBirth near the end of the
loop, on both 303s. It is not a note-count difference. Both sides trigger exactly
the same steps. **Our notes do not stop**, and a tail sitting at -26 to -50 dB
through a step where ReBirth is at digital zero reads as extra notes.

That is the third time his ear has been right about magnitude and different about
mechanism, after "glide" turning out to be the gate. The pattern is now reliable
enough to plan around: **take the location and the size seriously, re-derive the
cause from scratch.**

### The evidence

`tools/analyze/onset_ab.py` is new and permanent, because this defect family has
surfaced three times and no metric has ever caught it. It compares, per 16th step,
the ratio between the loudest point early in the step and the quietest point at
the end of the previous one, with the alignment measured rather than assumed.

Decoded from the fixture, the relevant steps of the 16-step pattern are:

| step | content |
|---|---|
| 7 | G0, SLIDE |
| 8 | REST, ACCENT |
| 9 | REST, SLIDE |
| 10 | G0, ACCENT |

Both sides correctly hold through step 8, because step 7 slides into it, and both
release at about 175 ms. **The sequencer and the tie handling are correct.** What
differs is only the decay rate after the release:

| | at 210 ms | at 300 ms | implied tau |
|---|---|---|---|
| ReBirth | -21.3 dB | -75.8 | 14.5 ms |
| ours | -23.5 | -45.3 | 34.6 ms |

ReBirth's file is 3.6% exact digital zeros. Ours is 0.2%.

### What it is not

| probe | tail tau | verdict |
|---|---|---|
| `AMP_REL` 21.92 ms to 2 ms | 30.9 | **not the VCA release**, an 11x faster release moved it 3.7 ms |
| `K_B` 3.4 to 2.0 | 35.2 | not the resonance feedback |
| `K_B` 3.4 to 0.8 | 36.1 | not the resonance feedback, and lower is slightly worse |

The VCA result is the one that matters: the release constant is 21.92 ms fitted
across six fixtures with 0.40 ms spread, so it was never the suspect, and making
it 11x faster barely moved the tail. Whatever sustains the note is not the VCA.

### What it is

**The DC blocker.** `REBORN_DC_HP` defaults to 5 Hz, and a one-pole at 5 Hz has a
time constant of 1/(2*pi*5) = 32 ms. When the voice stops, the filter's stored
state discharges over that time constant and puts a decaying tail on the output.

| build | tail tau | level at 300 ms |
|---|---|---|
| ReBirth | 14.5 ms | -75.8 dB |
| ours, shipped (5 Hz) | 34.6 | -45.3 |
| **DC blocker OFF** | **19.3** | **-65.6** |
| 20 Hz | 19.4 | -65.0 |
| 40 Hz | 19.3 | -65.0 |

Removing it accounts for **20 dB of the gap** and takes the tail from 34.6 ms to
19.3, against ReBirth's 14.5. The remaining 5 ms is a separate and much smaller
question.

The irony is worth recording. The DC blocker was added for a real, measured
defect: we emitted -0.0055 (saw) and -0.0140 (square) of DC where ReBirth sits at
+/-0.000003. It fixed that and introduced an audible one, and no spectral metric
could see it because a 32 ms tail 40 dB down changes almost nothing about a
spectrum averaged over twelve seconds.

### The fix is NOT simply raising the corner

Raising `DC_HP` to 20 Hz works acoustically but costs low end: a 20 Hz one-pole
takes 1.4 dB off C1 at 32.7 Hz, and batch L shows ReBirth is essentially flat down
there at resonance 0, so its own DC blocking must sit well below 32 Hz. Raising
ours would buy the tail at the price of a low-end error, in the same octave the
highpass work is currently trying to get right.

**The better candidate is to move the DC blocker BEFORE the VCA.** The DC it
exists to remove comes from `satAsym()`, which is pre-filter, so blocking it
pre-VCA is arguably the more correct placement, and the VCA then gates the
blocker's own ringing to silence instead of passing it. That is a signal-flow
change rather than a constant, so it needs measuring rather than assuming.

**Deliberately not applied.** It interacts with the resonance-engaged highpass
still being fitted (task 57), and the two are in the same octave. They should be
settled together rather than one at a time.

## 2026-08-01: batch P, and why a TIED fixture cannot measure ReBirth at low resonance

Batch P was built to answer task 57: how the low-end highpass behaves below the
resonance knee, crossed with cutoff. 27 fixtures, tied low notes as in batch N,
resonance 0/15/30/45/60 by cutoff 16/40/90/127.

**The capture chain is perfect and that is not the problem.** Two cells duplicate
batch N exactly and, re-exported a day later, came back **bit-identical**, rms
0.000 dB. A sustained tone removes ReBirth's roughly six render states entirely,
because those states differ in how a TRANSIENT lands and a held tone has none.

### RETRACTED before it was ever recorded as a finding

Fitting a one-pole highpass times a resonant ladder to batch P gave ReBirth a
resonance-0 corner of 357/454/849/1189 Hz at cutoff 16/40/90/127 against our
524/771/1667/2971, i.e. **ours too high by 0.55 to 1.32 octaves**. Derived
constants followed: `CUT_SCALE` 0.775, `CUT_SLOPE` 2.034, and a resonance coupling
sweeping 1.90 octaves instead of 0.60.

Scored on batch H, which it was not fitted on, that config takes the curve rms
from **2.627 to 8.886** and swings the resonance-0 mid-band error from +5.2 dB too
bright to 4.8 dB too dark. The reading is wrong.

**The cause is the tie.** Recorded as an open observation on 2026-07-31 and not
taken seriously enough: tied and plucked notes agree at resonance 127 on both
sides (0.02 dB) but at resonance 0 **ReBirth differs by 2.45 dB where our engine
differs by 0.02**. ReBirth does something on a held note at low resonance that we
do not. Cross-checking the same panel setting at three notes makes it visible: the
two PLUCKED batches (f0 55 and f0 110) agree with each other to 0.12 dB at every
shared frequency, and the tied C1 curve sits 1.3 to 1.6 dB below both above 500 Hz.

So the tied fixture measures ReBirth in a state that does not correspond to normal
playing, and any filter constant extracted from it at low resonance is measuring
that state rather than the filter. **Batch P is unusable for its intended purpose**
and task 57 needs plucked fixtures.

Batch P also dropped the plucked control cells that batch N carried. Batch N's
controls are what made this diagnosable at all. **Every batch keeps its controls,
including the ones that seem to have already done their job.**

### A calibration that was too kind

The two-corner estimator was calibrated on synthetic NOISELESS curves and recovered
a 3187 Hz corner from a band ending at 327 Hz to within 0.4%. On real data with
about 0.5 dB of ripple the same fit over the same band returned 1158 Hz for a known
3190. **A calibration without realistic noise measures the algebra, not the
instrument.** Widening the band to 1300 Hz, where the rolloff is actually visible,
brought our own known corners back to within 5 to 7%.

### What DOES stand, from the plucked batch H

Fitting a pure 4-pole to the resonance-0 cells, where k = 0 so the model is exact:

| knob | ReBirth | ours | ours minus ReBirth |
|---|---|---|---|
| 16 | 607 Hz | 541 | **-0.17 oct** |
| 40 | 856 | 763 | -0.17 |
| 64 | 1162 | 1119 | -0.06 |
| 90 | 1579 | 1672 | +0.08 |
| 110 | 2063 | 2314 | +0.17 |
| 127 | 2596 | 3025 | **+0.22 oct** |

**Our resonance-0 corner is within +/-0.22 octaves, and the error is a SLOPE
error rather than an offset**: too low at the bottom of the knob, too high at the
top, crossing near knob 64. ReBirth's implied law there is about
`522 * 2^(2.365 * knob/127)` against our `427 * 2^(2.9 * knob/127)`.

That fully explains the resonance-0 brightness error already on file. For a
4-pole, +0.22 octaves of corner is +5.3 dB above the corner, and batch H measures
+5.20 dB of mid-band excess at cutoff 127 resonance 0. Two independent statistics,
one number.

**Do not change `CUT_SLOPE` on this alone.** It moves every resonance, and the
current value was fitted jointly against cells where peaks exist. The resonance-0
corner is a new CONSTRAINT for that joint fit, not a separate law, and adding it
is the next step.

## 2026-08-01: ReBirth's filter envelope fires at ENV MOD ZERO. This is the chirp.

Found by following the tie control in batch L2, which was added only to explain
why batch P failed. It explains four separate open items at once.

### The measurement

Cutoff 64, resonance 45, **env mod 0**, decay swept. Brightness is the energy at
880-2200 Hz relative to the 220 Hz harmonic, so level is divided out and this is
pure spectral shape.

| decay knob | ReBirth | ours |
|---|---|---|
| 0 | -18.32 dB | -12.32 |
| 32 | -16.36 | -12.32 |
| 64 | -14.45 | -12.32 |
| 96 | -12.99 | -12.32 |
| 127 | -12.50 | -12.32 |

**ReBirth's decay knob moves brightness 5.8 dB with env mod at zero. Ours does not
move at all**, identical to two decimals across the entire knob.

Slicing a single 125 ms note into eight and averaging over 56 notes shows what it
is:

| | 0 ms | 31 ms | 62 ms | 93 ms |
|---|---|---|---|---|
| ReBirth, decay 0 | -12.79 | -16.67 | -21.55 | **-22.61** |
| ReBirth, decay 127 | -11.68 | -11.61 | -13.72 | -12.94 |
| ours, decay 0 | -7.88 | -7.20 | -8.27 | -8.02 |
| ours, decay 127 | -7.88 | -7.20 | -8.27 | -8.02 |

ReBirth's notes **start bright and darken by 9.8 dB within the note** at decay 0,
and stay bright at decay 127. The two start within 1.1 dB of each other, which is
the signature of ONE envelope with the same depth and a decay time set by the
knob, rather than two different depths. For a 4-pole, 9.8 dB above the corner is
about 0.41 octaves of corner movement, so the floor depth is roughly 0.4 octaves
and possibly more, since at decay 0 the note ends before the envelope finishes.

Ours is flat within the note and inert across the knob.

### What this explains

**The chirp, task 54.** A per-note brightness burst that decays is exactly a
chirp. Luke has confirmed the chirp is the same quality as the "snap" he has been
describing since the first ear check, so this is the largest known subjective gap
in the 303 and it now has a mechanism and a number.

**Why the chirp is absent on `drv_closed` and `drv_open`**, the two patches he
cannot distinguish from ReBirth. Those are the filter-closed and low-resonance
cases, where a 0.4-octave corner movement has little to act on.

**The under-pronounced filter sweep, task 55.** If our envelope is missing a fixed
floor, every sweep is weaker than ReBirth's by that floor, which is "present in
ours, just not as pronounced" exactly.

**The old unexplained item**, recorded weeks ago and never solved: "ReBirth's
centroid climbs a steady 0.10 octaves across the decay knob while its gate and
release provably do not move, so decay reaches something besides the filter
envelope and the VCA and we do not know what." It reaches the filter envelope,
which is not gated to zero by the env mod knob. The 0.10-octave centroid figure
was a summary statistic badly understating a 5.8 dB effect.

**Why batch P failed.** ReBirth's tied and plucked curves differ because a tied
note fires the envelope once over four bars while a plucked pattern fires it 32
times. Our engine has no envelope to fire, so our tied and plucked curves are
identical to 0.04 dB. The tie effect was never a fixture artifact; it was this
finding showing through.

### The one thing it is NOT

The VCA attack. Swept from 1.2 ms to 20 microseconds, our tied-minus-plucked
difference stays at 0.02 dB. `CLAUDE.md` records that we deliberately softened
that attack to kill a broadband HF shelf, and it was reasonable to suspect we had
over-corrected. We had not. The attack is inert here and that hypothesis is dead.

### Also visible, and separate

Our brightness at this cell is about 5 dB HIGH before any of this: -7.9 against
ReBirth's -12.8 at note onset. That is consistent with the resonance-0 corner
finding of the same day and belongs to task 58, not here.

### The env mod floor implemented, and the three-way tension it exposes

`REBORN_EM_FLOOR`, default **0.0** (off). The sign convention is not the obvious
one and the first attempt had it backwards.

**Adding the floor as the envelope OPENS made every cell worse**, taking the decay
probe from 3.39 dB rms to 7.30 at a depth of 0.4. The measurement says why: our
flat brightness of -12.32 dB already matches ReBirth's **decay-127** value of
-12.50, the setting where its envelope barely decays within a note. Our corner is
not sitting at ReBirth's resting position waiting to be lifted. It is already
sitting where ReBirth's is with the envelope OPEN.

So the envelope SUBTRACTS as it closes: `envOct += EM_FLOOR * (envShaped - 1)`. At
the note onset the corner is unchanged, which preserves every existing fit at the
point they were anchored, and it falls by up to `EM_FLOOR` octaves as the envelope
decays.

Swept against the decay probe, plucked, env mod 0:

| `EM_FLOOR` | plucked rms | tied rms |
|---|---|---|
| 0.0 (shipped) | 3.39 dB | 12.56 |
| 0.50 | 0.74 | 4.96 |
| **0.60** | **0.46** | 3.57 |
| 0.70 | 0.68 | 2.48 |
| 0.75 | 0.89 | 2.20 |

**0.60 octaves takes the decay-knob error from 3.39 dB to 0.46**, a 7x
improvement, and the fitted depth agrees with the 0.41 octaves estimated
independently from the within-note trajectory to the accuracy that estimate
deserves.

The tied column never gets below 2.19 at any depth, and its optimum sits at 0.75
rather than 0.60. A tied note exposes the envelope's full decay over four bars
where a plucked pattern only ever exposes its first 125 ms, so this is a
longer-timescale SHAPE mismatch rather than a depth error. Recorded, not fitted:
plucked is what music does and what every other fit in this project uses.

### The three-way tension, which is the real state of this

| config | batch H curve | decay probe |
|---|---|---|
| shipped, no floor | **2.627** | 3.39 dB |
| floor 0.60 alone | 2.946 | **0.46** |
| floor 0.60 + `RESO_OCT` 0.45 | **2.590** | 1.45 |

The floor lowers the resting corner at every resonance, which compounds a
resonance rise that was already too weak: batch H's mid-band error at resonance
127 goes from -0.80 to -3.46 dB at cutoff 16. Raising `RESO_OCT` from 0.25 to 0.45
takes batch H back to 2.590, slightly BETTER than the current default, but pushes
the decay probe from 0.46 to 1.45 because it brightens resonance 45, which is
where the probe sits.

**Neither fixture constrains this alone and that is the finding.** It needs a
joint fit over `EM_FLOOR`, `RESO_OCT`, `RESO_OCT_CUT`, `CUT_SCALE` and `CUT_SLOPE`
scored on both at once, together with the resonance-0 corner constraint from task
58, which is a third pull on the same constants.

### A fixture gap worth naming

**Batch H has decay = 64 in every one of its 36 cells.** Every filter fit this
project has ever done is therefore blind to the decay axis, which is precisely the
axis this finding lives on. That is why a 5.8 dB effect survived every round of
fitting: the primary metric could not see it, and the one statistic that did touch
it, "centroid climbs 0.10 octaves across the decay knob", was a summary too
compressed to alarm anyone. Any future grid should cross decay, not fix it.

## 2026-08-01: we re-fire the accent envelope on every accented step in a tied run

Luke, on the chirp ear check: "the filter/envelope feels like it is oscillating
more slowly across the loop than it does on chirp or current, like in our version
it is being applied to each note". Localised by measurement, and he is exactly
right.

Per-step over 7 bars of `clodhopper_303-2`, ABSOLUTE energy so level and
brightness are separated. Low band 100-400 Hz spreads 10.4 dB in ReBirth and 10.1
in ours, so **level matches**. High band 1-4 kHz spreads **33.0 dB in ReBirth and
16.8 in ours**.

Steps 13 to 16 are one TIED RUN starting on an accent, and 13, 14 and 15 carry
identical flags (note, accent, slide):

| step | flags | ReBirth | current | chirp, `EM_FLOOR` 0.6 |
|---|---|---|---|---|
| 13 | note ACC sl | **+12.1** | +10.4 | +9.3 |
| 14 | note ACC sl | **-9.9** | +10.3 | +5.7 |
| 15 | note ACC sl | **-17.4** | +10.3 | +2.6 |
| 16 | note | **-20.9** | -3.9 | -9.9 |

Three steps with identical flags, and ReBirth spans 30 dB monotonically across
them. Flags cannot drive that. It is one envelope firing at the retrigger and
decaying through the run. Ours is FLAT at +10.3 and then steps down at 16 only
because the accent flag stops, which is the signature of reading the flag per step
rather than running an envelope. Slide means DO NOT RETRIGGER.

Suspect: `holdStep()` re-arms `accAttacking_` on every step carrying the accent
flag. That came from a real finding, that ReBirth reads the accent row on a rest
and it lands on whatever is sounding through it, but it looks over-applied to
tied NOTES. **Do not simply delete the re-arm**; the rest-accent behaviour is
measured and real, and removing it re-breaks a defect Luke caught by ear. The
distinction to establish is accent-on-a-REST versus accent-on-a-SLID-NOTE, and
batch G cannot separate them by construction because no rest in it is preceded by
a slid step.

### The size of the effect does not transfer, and the reason is worth keeping

Luke pointed out that Clodhopper runs resonance at 125 of 127 with the cutoff knob
at 45. At that resonance the filter is near self-oscillation and the corner sits
near 800 Hz, so the 1-4 kHz band being measured is well ABOVE the resonant peak,
on a very steep skirt. A modest corner movement is hugely amplified there: 0.6
octaves against a 30-plus dB/oct skirt accounts for most of the 30 dB swing by
itself.

So the SHAPE is the finding (monotonic decay across identical flags versus flat)
and the MAGNITUDE is patch-specific. **This patch is a magnifier: excellent for
hearing, dangerous for fitting.** Fit the accent envelope somewhere the skirt is
not doing the amplifying, and never quote a dB figure from here as a depth.

### Consequence for the env mod floor

`EM_FLOOR` 0.6 decays 6.7 dB across that run where ReBirth decays 30. Fitting the
floor against musical material like this would drive it far too deep, compensating
for the wrong mechanism. **Settle the accent retrigger before fitting the floor on
anything but the static probe.**

### The env mod floor validated by ear on an ACCENT-FREE patch

`drv_closed` runs cutoff 25, resonance 30, env mod 10 and **accent 0**. It is the
only patch in the ear-check set with no accent, so it is the only one whose
verdict is not confounded by the accent-retrigger defect found the same day.

Luke, unprompted: ReBirth has "a kind of curve around each note moreso than the
others", and "a little more stomp when each note lands, which chirp more closely
approximates than current". He ranked chirp between current and ReBirth.

**"Stomp" is patch-specific and must not be read as a name for the mechanism.**
He clarified afterwards that the word belongs to the closed-filter tracks, which
are dark and bass-heavy, and that he would not reach for it or necessarily even
notice the same thing on a brighter patch. The mechanism is a corner falling
across the note; in this register that reads as stomp, at high resonance it reads
as the chirp, and on a sustained bright patch it may not announce itself at all.
Do not go looking for "stomp" on a bright patch and conclude the finding failed.

Measured, brightness at the note ONSET (700-3000 Hz against 100-400 Hz):

| | onset brightness | distance from ReBirth |
|---|---|---|
| ReBirth | -24.06 dB | |
| current | -21.72 | 2.34 dB |
| chirp, `EM_FLOOR` 0.6 | -23.65 | **0.41 dB** |

**The floor closes 82% of the onset gap**, and the ear ranking and the measurement
agree without either being told the other's answer. On the one patch where the
floor can be judged alone, it is right.

**A caution on the statistic.** Read across the whole note this ratio RISES rather
than falls, because the low band decays faster than the high band once the VCA
releases. Any "drop across the note" computed from it is meaningless. Use the
onset and the gate; the release belongs to the tail, and the divergence there
(ReBirth -9.49 dB by 104 ms against our -16 to -19) is the DC blocker of task 53,
not this.

`drv_open` behaves the same way by ear, less strongly, which is expected: it
carries accent 60, so part of its verdict is confounded, and its measured change
was only 0.31 dB.

**Consequence.** The floor is cleared to be fitted. It should be fitted against
the static decay probe and `drv_closed`, both accent-free, and NOT against
accented musical material until task 60 is settled.

## 2026-08-01: the accent law, measured on a purpose-built probe

`fixtures-2026-08-01/accentprobe`, nine fixtures. Steps 1 to 8 are a run followed
by eight rests; slide sits on the step you slide FROM so steps 1-7 carry it,
making 1-8 a single continuous note that fires once. Cutoff 64, resonance 60, env
mod 0, accent knob 96. **Deliberately not Clodhopper's 45/125**: near
self-oscillation the measurement band sits on a very steep skirt and a modest
corner move reads as 30 dB, which is superb for hearing and useless for fitting.

Duplicate controls: `acc_none` bit-identical, `acc_mid` 0.261 dB rms. The
difference is expected, since `acc_mid` contains an accent transient and ReBirth's
render states differ precisely on transients.

### The law, high band 1500-4000 Hz

| | ReBirth | ours |
|---|---|---|
| accent depth on a RETRIGGERED note | **+22.4 dB** | +10.9 |
| accent bump on a SLID step | **+2.8 dB** | +10.2 |
| accent bump on a REST | **+2.8 dB** | fires fully |
| decay across a tied note, no accent | **14.1 dB** | 1.6 |

**ReBirth gives an accent on any NON-RETRIGGERING step the same small +2.8 dB
nudge, whether that step is a rest or a slid note, against +22.4 dB on a real
retrigger.** The two non-retriggering cases agreeing to the decimal is what makes
this a law rather than two coincidences.

### This corrects the hypothesis it was built to test

Task 60 was written as "we re-fire the accent envelope where ReBirth does not".
That is wrong in kind. **ReBirth does respond on a tied step, by about an eighth
as much.** Our defect is that we barely distinguish the two cases at all, +10.2
against +10.9, where ReBirth separates them by 19.6 dB.

So the fix is NOT to suppress accents on non-retriggered steps. `holdStep()`
re-arming the accent is right in principle and was established by measurement; it
is the DEPTH that is wrong. Deleting it would have replaced one error with
another, which is why the fixture was built before the patch.

### A second, separate defect in the same data

**Our accent is less than half as deep as ReBirth's even when it fires correctly**:
+10.9 dB against +22.4 on a plain retriggered note, measured on `retrig_all`
against `retrig_none` where both sides are flat across all eight steps and no tie
or envelope subtlety is involved. That is `ACC_SWEEP`, and it is a clean
single-parameter target. It may also be part of task 55, the under-pronounced
sweep, since accent contributes to the movement Luke hears as weak.

### And a third independent confirmation of the env mod floor

`acc_none` is a tied note at env mod 0 with no accent anywhere. ReBirth's high
band falls **14.1 dB across it** while ours falls 1.6. That is the filter envelope
firing at env mod zero, now confirmed on a third unrelated fixture set after the
decay probe and `drv_closed`.

### Fitting the accent: one parameter lands, the other splits the fixtures

`REBORN_ACC_TIE` (default 1.0 = unchanged) scales the accent ceiling on a step
that does NOT retrigger. `fireAccent()` only ever raises the envelope toward its
own target, so a small tie-accent arriving while a big struck one is still
decaying leaves it alone rather than dragging it down.

**`ACC_TIE` 0.05 reproduces ReBirth exactly, on both cases at once:**

| | ReBirth | shipped | `ACC_TIE` 0.05 |
|---|---|---|---|
| bump on a SLID step | +2.8 dB | +10.2 | **+2.8** |
| bump on a REST | +2.8 dB | fires fully | **+2.8** |

One parameter hitting both is the real check, because the two cases being
identical is what made this a law rather than two coincidences.

Cross-checked on batch G, which turns out to contain non-retriggering accented
steps (all 54 fixtures change): centroid-ratio rms 0.215 to 0.222 and level-lift
rms 0.55 to 0.51 dB. **Neutral**, within noise both ways. So it fixes the tied-run
defect without disturbing the fixture set the accent constants were fitted on.

### `ACC_SWEEP` does NOT ship, and the reason is instructive

The accentprobe says our accent depth on a proper retrigger is +10.9 dB against
ReBirth's +22.4, and fitting it there gives `ACC_SWEEP` 1.30, taking the probe
error from 11.35 dB rms to 1.65 across three bands.

Scored on batch G the same value takes the centroid ratio from 0.215 to **0.347**,
substantially worse:

| accent knob | ReBirth | shipped | `ACC_SWEEP` 1.30 |
|---|---|---|---|
| 64 | 1.016 | 1.028 | 1.368 |
| 96 | 1.250 | 1.092 | 1.676 |
| 127 | 1.507 | 1.160 | 1.900 |

**Why the probe misleads.** It runs env mod 0, so `ACC_SWEEP` is the ONLY accent
mechanism active in it and gets forced to account for the entire accent effect.
Batch G has env mod up, where `ACC_SHORT` (accent shortening the filter envelope)
contributes as well, so the same `ACC_SWEEP` lands on top of a mechanism the probe
could not see. A fixture that isolates a parameter can also over-attribute to it.

**A statistic that saturated, caught in passing.** The first scoring band, 1500 to
4000 Hz, stops discriminating once the accented corner rises past it: `ACC_SWEEP`
1.70 to 1.90 bought only +0.6 dB there where earlier steps bought +2.1. Fitted on
that band alone the answer would have been about 1.9. Scoring three bands showed
1.9 overshooting the 6-12 kHz octave by 11 dB. **Check that a fit statistic is
still moving before trusting where it flattens.**

### What batch G says the accent defect really is

Not depth alone. ReBirth's centroid ratio spans **0.662 at accent knob 0 to 1.507
at 127**, a factor of 2.3. Ours spans 0.917 to 1.160, a factor of 1.26. Our accent
brightness range is far too narrow at BOTH ends, and the low end is the more
interesting half: at knob 0 ReBirth makes accented notes *darker*, which is
`ACC_SHORT` acting on the filter envelope, and we barely reproduce it.

That points the same way as everything else today. The accent constants cannot be
fitted before the env mod floor, because the floor is what `ACC_SHORT` acts on
when env mod is low, and with the floor default-off there is nothing there for it
to shorten.

## 2026-08-01: WE PLAY THE WRONG NOTE. Flag 0x10 is not a rest.

Luke, ear-checking the accent change: "in all of the new renders the slide to D is
for all intents and purposes not present to these ears", and he transcribed the
pattern off ReBirth's own GUI, listing NOTES on all sixteen steps.

Pitch-tracked on `clodhopper_303-2`, dominant low partial per step:

| step | ReBirth | ours |
|---|---|---|
| 1 | 60.6 Hz | 60.6 |
| 2 | 96.9 | 96.9 |
| **3** | **72.7 (D)** | **98.2 (G)** |
| 4 | 73.3 (D) | 73.6 (D) |
| 5 | 98.2 | 98.2 |

**ReBirth plays the D at step 3. We hold the previous G and do not reach D until
step 4.** Steps 1, 2, 4 and 5 agree exactly, so this is not a timing offset. It is
a wrong pitch, on a shipped song, and it has been there the whole time: the
current build does it too, so nothing today caused it.

Step 3's bytes are note = 2 (D), flags `0x13` = SLIDE | ACCENT | `0x10`. We decode
`0x10` as REST and hold. Luke's GUI reading matches the note bytes step for step,
including every step carrying `0x10` (3, 6, 8, 9, 12).

### The hypothesis, and it reconciles evidence that looked contradictory

**`0x10` does not mean silence. It means NO GATE / NO RETRIGGER, a tie, and the
pitch still updates.**

This is not a reason to discard `docs/FORMAT_RBS.md`'s evidence, which is real and
was properly obtained: the shipped "Silent Default Song" carries `0x10` on every
step and its own Read Me calls it "completely silent patterns", while the all-zero
default song plays every step. Under the tie reading both facts hold at once. A
pattern of nothing but ties never strikes a note, so it never sounds.

The musical statistics point the same way. Across the shipped demo songs,
restricted to patterns containing at least one struck step, **19.2% of steps carry
`0x10`** (882 patterns, 13,749 steps). A fifth of every acid line being ties is
ordinary. A fifth being silence is not.

**Not proven, and it must not be applied on this evidence.** It needs a controlled
fixture: a `0x10` step at a DIFFERENT pitch from its predecessor, exported from
ReBirth, testing "pitch updates on a `0x10` step" directly instead of inferring it
from one step of one song. Test with and without the slide flag too, since step 3
carries both and they may interact.

### Why nothing caught this

**The `.rbs` round-trip passes 17 of 17 and is structurally incapable of catching
it.** It proves our writer and our reader agree with each other, not that either
agrees with ReBirth. This is the second semantic decode error to hide behind a
green round-trip, after the drum switch bytes.

Every parity fixture generated by `rbsgen` writes patterns using OUR meaning of
the flag, so ReBirth renders them the way we intended and the error cancels. It
can only appear on a song ReBirth authored, which is why months of fixture work
never touched it and one demo song exposed it immediately.

Recorded as task 61. It outranks every open filter item: a wrong pitch is worse
than any amount of spectral mismatch, and `docs/research/GOALS.md` ranks rhythm alongside
sound.

### Also noted while looking

`docs/FORMAT_RBS.md` still documents `0x01` as accent and `0x02` as slide.
`tools/rbsgen/rbsgen.py` has carried the opposite since 2026-07-24 with a comment
saying the two were previously swapped and were corrected by controlled export.
The doc is stale and should be fixed alongside whatever `0x10` turns out to be.

### CORRECTION, same day: `0x10` IS the rest flag. The bug is narrower and it is fixed.

The hypothesis above, that `0x10` means tie rather than rest, is **wrong** and is
retracted. Luke settled it from the interface: Clodhopper uses a mod skin that
relabels the 16th note and rest controls as "on and off", and reading it back he
confirmed steps 3, 6, 8, 9 and 12 carry a **16th REST**, which is exactly the set
carrying `0x10`. The format doc was right and I should not have doubted it on
circumstantial evidence.

**The real defect is smaller and stranger. ReBirth's oscillator pitch follows
every step's note byte, whether or not that step gates.** The rest flag controls
the GATE and nothing else. We were discarding the note value on rest steps
entirely.

Proven on `fixtures-2026-08-01/restpitch`, four cells, pitch during step 2:

| cell | ReBirth | before | after |
|---|---|---|---|
| C slides into a REST whose note is G | **98.2 Hz (G)** | 65.3 (C) | **98.2** |
| same, NOT slid into | 100.3 | 59.2 (tail) | 98.2 |
| control, rest note = C | 65.3 | 65.3 | 65.3 |
| control, plain slide C to a G NOTE | 98.2 | 98.2 | 98.2 |

The two controls confirm our plain slide was already correct, so the fault was
isolated to reading the note on a non-gating step.

On the shipped song, `clodhopper_303-2` steps 2 to 5:

| | 2 | 3 | 4 | 5 |
|---|---|---|---|---|
| ReBirth | 96.9 | **72.7** | 72.7 | 98.2 |
| before | 96.9 | **98.2** | 74.0 | 98.2 |
| after | 96.9 | **74.0** | 72.7 | 98.2 |

### The fix

`Instrument::setStepNote()`, called for EVERY step before the gate decision. It
moves the glide TARGET only, so a struck note still jumps (trigger does that) and
a tied or rested one glides. Three lines of routing; the gate logic is untouched.

Tests 1/1, 17 of 17 songs round-trip, and the decay probe renders byte-identical.

### What this says about how the two of us find things

Luke heard "the slide to D is not present". That was correct, specific, and
pointed at a wrong PITCH, which no spectral metric in this project measures at
all: every instrument here compares magnitude spectra between two files playing
what we assume is the same note. A wrong note is invisible to all of them.

And my hypothesis was wrong in the direction that flatters a fresh idea over an
established one. The format doc's evidence was good, obtained the same careful
way as everything else, and I proposed overturning it from one step of one song.
The interface settled it in one message. **When a decode reading conflicts with
the program's own UI, the UI wins, and it is cheap to ask.**

### A note for whoever authors fixtures next

`rbsgen` writes note = 0 on a rest step, where ReBirth writes whatever pitch the
step displays. That is now observable behaviour rather than a don't-care, because
the pitch follows it. Both sides read the same bytes the same way so existing
comparisons stay valid, but a fixture whose rests are meant to hold a pitch must
now say so explicitly.

### Ear check on the pitch fix, and `ACC_TIE` applied

Luke, on the pitch fix alone: "closer, but the last Gs still read as individual
16th notes rather than a single contiguous note that slides down to low G on step
15."

Two things in that, and only one of them was the pitch.

**The octave drop was already correct.** Step 15 reads 48.4 Hz in both ReBirth and
the fixed build, so the slide down to low G lands where it should. Nothing to fix.

**The articulation was the accent.** Steps 13, 14 and 15 all carry it and we
re-fired the accent envelope at full depth on each, articulating every step of
what should be one held note. That is the defect measured earlier the same day
and fitted to `ACC_TIE` 0.05, which had been left switched off so the pitch fix
could be judged in isolation.

| | within-step dip, steps 13-15 | decay across the run |
|---|---|---|
| ReBirth | 2.69 dB | 7.5 dB |
| pitch fix only | 4.01 | 5.2 |
| pitch fix + `ACC_TIE` 0.05 | **2.21** | **7.2** |

Luke on the combined render: "perfetto, we are back in business."

**`ACC_TIE` is therefore applied at 0.05**, on measurement plus ears. Tests 1/1,
17 of 17 songs round-trip.

Note the dip slightly overshoots, 2.21 against ReBirth's 2.69, so we are now a
touch smoother through a tied accented run than ReBirth is. Small, and in the
opposite direction from the defect, but it should not be tuned away on this
evidence: the remaining accent work is entangled with `ACC_SWEEP` and the env mod
floor and belongs in the joint fit.

### A process note worth keeping

Isolating one change is right for MEASUREMENT and can be wrong for an EAR CHECK.
The pitch fix was given alone so its effect could be attributed cleanly, which was
correct science and cost Luke a listening pass on a build that still had a known
defect in it. When the question is "does this sound correct", present the best
candidate; when it is "what did this change do", isolate.

---

## 2026-08-02: the re-measurement debt was mostly imaginary, and the pitch fix moved nothing

End of day on 2026-08-01 recorded a debt: batch L2 and the accent probe both
contain rests, the pitch fix changed what pitch a rest sounds, so their numbers
needed regenerating before reuse. That was reasoning, not measurement. Measured
this morning it is wrong in the first case and right for the wrong reason in the
second.

### What was actually done

Re-rendered three batches with the current build and byte-compared against the
stored `_ours` renders, all of which predate the four commits of 2026-08-01
evening.

| batch | renders differing | why |
|---|---|---|
| `batchL2` | **0 of 23** | it has no rests. Every step is a note |
| `decayprobe` | **0 of 7** | likewise |
| `accentprobe` | 9 of 9 | but see below |

`gen_batchL.py` and `gen_batchL2.py` both build their pattern as sixteen
`step(note=NOTE_A, octave=-1)`, so `_record_303` never pads and no rest is ever
written. The debt on those two never existed.

### The accent probe changed, and it was NOT the pitch fix

Re-rendered a third time with `REBORN_ACC_TIE=1.0`, which is exactly the default
that `b5b518b` replaced, everything else current. That reproduces the stored
renders to within 0.05 dB on every fixture:

| fixture | ACC_TIE held at 1.0, vs stored | current default, vs ACC_TIE 1.0 |
|---|---|---|
| `acc_all` | +0.008 | **-3.893** |
| `acc_mid` | -0.011 | **-1.467** |
| `acc_mid_dup` | -0.011 | **-1.467** |
| `rest_acc` | -0.051 | **-1.445** |
| `acc_none`, `acc_first`, `retrig_*` | +0.000 to +0.018 | +0.000 |

So the whole difference is `ACC_TIE`, which is an applied, intended,
ear-confirmed change, and **the pitch fix contributed nothing measurable to the
run region of any of the three batches.** The reason is structural rather than
lucky: batch L2 and the decay probe have no rests at all, and the accent probe's
rests all sit after a step that does not slide, so they are Releases whose pitch
moves only during an already-decaying tail.

The duplicate control worked exactly as intended here: `acc_mid` and
`acc_mid_dup` agree to three decimals in both columns.

### What this changes

Task 57's fit is **not blocked** and never was. Batch L2 is exported, validated
and its renders are current.

Task 54's evidence, the decay probe, is **intact** and needs no regeneration.

The accent probe's stored numbers were measured at `ACC_TIE` 1.0 and the build
is now 0.05, so they do need regenerating, for that reason and not the recorded
one. Fresh renders are in `renders/accentprobe_ours`; the superseded set is kept
as `renders/accentprobe_ours_preACCTIE`, the same way
`curve_fit_cache_preF0FIX.json` was kept.

### The lesson, which is an old one arriving again

A debt was recorded from a plausible mechanism without checking whether the
mechanism fired. It cost nothing this time because the check was two minutes of
rendering, and it would have cost a round if the conclusion had been the other
way and batch L2's fit had been redone for no reason. **When a change is
suspected of invalidating a measurement, re-render and diff rather than
reasoning about which fixtures contain the trigger.** The render loop is 1.6 s
for 36 fixtures precisely so that this is never worth arguing about.

---

## 2026-08-02: the low-end highpass is CONFIRMED at high resonance and unidentifiable at low, and the cutoff dependence is not what was recorded

Task 57, read on the merged batch L + L2 grid: six cutoffs by six resonances,
plucked, with `tools/analyze/lowend_grid.py`. This supersedes the framing in
RUNBOOK section 4 item 2.

### The controls, which come first

Dual-mono confirmed at -240 dBFS peak L-minus-R, so `psd()` averaging the
channels is free SNR here rather than a blind spot. Said out loud because the
rule is to measure stereo or check and say that you checked.

Duplicates, six cells exported twice: **max delta 0.068 dB, mean 0.026.** That is
this statistic's own noise floor and every number below is read against it.

Tie versus pluck, the control batch P dropped and batch L2 restored, and it
produced a finding on its own. ReBirth's tie-minus-pluck runs **+2.21 dB at
resonance 15 and +0.74 at resonance 45**, against a known +2.45 at resonance 0,
while ours is +0.02 in every case. So the tie effect is not a step that switches
off past the knee, it is a **monotone decay that is still 30 percent alive at
resonance 45**. Batch P was even less usable than recorded.

### CONFIRMED, and this is the strongest result of the round

At resonance 127 the gap between ReBirth and us IS a one-pole highpass, and the
six-point shape says so rather than a single ratio being inverted to assume it:

| cell | 55 | 110 | 165 | 220 | 275 | 330 | fitted fc | rms |
|---|---|---|---|---|---|---|---|---|
| cut 127 reso 127 | -6.06 | -2.08 | -0.63 | 0 | +0.34 | +0.61 | 110.8 | **0.038** |
| cut 110 reso 127 | -6.12 | -2.13 | -0.62 | 0 | +0.31 | +0.57 | 112.1 | **0.027** |
| cut 64 reso 127 | -6.10 | -2.14 | -0.64 | 0 | +0.32 | +0.49 | 111.7 | **0.027** |

Residuals of 0.027 to 0.038 dB against a duplicate floor of 0.026. The corner is
**111.5 Hz plus or minus 0.7 and independent of cutoff**, which is what batch N
claimed from two tied cells and is now established on plucked fixtures across
three cutoffs.

### The shipped law works, on a grid it never saw

Rendered batch L and L2 under `REBORN_LF_HP=119.9 REBORN_LF_HP_KNEE=0.141`:

| | mean abs gap | rms gap | max |
|---|---|---|---|
| highpass OFF, the shipped default | 5.047 | 5.232 | 6.124 |
| highpass ON at the batch N law | **1.380** | **1.976** | 5.732 |

Cross-validation in the proper sense: batch N was two TIED notes at two cutoffs,
this is 30 PLUCKED cells at six. The law more than halves the error on data it
was not fitted to.

### The remaining error is one column, and the recorded framing was imprecise

With the law on, the residual is concentrated almost entirely at resonance 0.
Rows are cutoff, columns are resonance 0 / 15 / 30 / 45 / 60 / 127, dB:

    cut  40    -0.46   +0.31   +0.49   +0.30   +0.06   -0.25
    cut  64    -1.91   -1.10   -0.63   -0.48   -0.46   -0.31
    cut  90    -3.72   -2.34   -1.39   -0.95   -0.72   -0.32
    cut 110    -4.96   -2.94   -1.72   -1.16   -0.82   -0.34
    cut 127    -5.73   -3.28   -1.88   -1.23   -0.89   -0.27

Resonance 127 is closed to within 0.34 dB at every cutoff. The law gives fc = 0
at resonance 0 and ReBirth has up to 5.7 dB of loss there.

RUNBOOK section 4 said "below the knee ReBirth's loss depends on CUTOFF and our
law does not". True, but the sharper and more useful statement is: **the
resonance-0 BASELINE is nonzero and grows with cutoff**, from -0.46 dB at cutoff
40 to -5.73 at cutoff 127. Everything else the law already handles.

### REFUTED: the low-resonance departure is NOT task 58 leaking in

The hypothesis was reasonable. The resonance-0 curve droops at 275 and 330 Hz
where a one-pole rises, a droop relative to the 220 Hz reference is what a
lowpass corner difference looks like locally, and task 58 has independently
measured our resonance-0 corner as 0.22 octaves HIGH at cutoff 127, which is the
right sign.

Fitting a one-pole plus a free tilt refutes it. If the tilt were task 58 it would
track CUTOFF and be flat in resonance. It does the opposite: the tilt varies by
1.5 to 3.4 dB/oct down each cutoff row and only 1.2 dB/oct across rows. Resonance
dominates. The hypothesis does not survive.

### And the fixture cannot identify what it IS

Three models, five effective points after referencing to 220 Hz, 2.6 octaves of
span. Residual rms in dB:

| cell | 1-pole | 1-pole + tilt | 2-pole + tilt |
|---|---|---|---|
| cut 127 reso 0 | 0.670 | 0.576 | **0.087** |
| cut 110 reso 0 | 0.801 | 0.641 | **0.042** |
| cut 64 reso 0 | 1.130 | 0.605 | **0.089** |
| cut 127 reso 127 | **0.037** | 0.015 | 0.082 |
| cut 64 reso 127 | **0.027** | 0.017 | 0.126 |

A 2-pole plus tilt collapses resonance 0 to near the noise floor and makes
resonance 127 worse. That looks like a mechanism until the parameters are read:
the fitted corners wander 70 to 183 Hz and the tilts run -3.6 to -5.7 dB/oct,
and adding a free tilt to the resonance-127 cells drags a corner the shape check
independently pinned at 111 Hz up to 147. **That is degeneracy, not a better
model.** It is the same non-identifiability that retired `invert_filter.py`,
where single cells fitted equally well by seven corners spanning 0.90 octaves.

So: a one-pole at 111.5 Hz is established at high resonance, the resonance-0
baseline is established as real and cutoff-dependent, and its SHAPE is not
identifiable from six harmonics.

### What would identify it, and why nothing existing does

More points below 340 Hz. Batch N got eighteen by crossing C1 at 32.703 Hz with
E1 at 41.203 so the combs interleave, but batch N is TIED and the control above
shows the tie effect is still 30 percent alive at resonance 45, so it cannot be
used where the question lives.

The fixture that does not exist is **batch N's note plan with batch L's plucked
articulation**. The reason nobody built it is recorded in `gen_batchP.py`: at
32.7 Hz a plucked half-step gate at 120 BPM is 2.0 cycles of the fundamental,
which is not a measurement.

The way through is tempo, and it appears not to have been tried. The VCA gates
for half a step and the gate is confirmed proportional across 60 to 180 BPM, so
**at 60 BPM the same plucked gate is 4.1 cycles at 32.7 Hz and 6.9 at 55**,
against 3.4 at 55 Hz today. That buys the resolution without touching the
articulation, and 60 BPM is inside the range the gate law was validated on rather
than an extrapolation.

Such a batch must carry a **120 BPM control cell** duplicating an existing batch L
plucked cell exactly, because the tempo change is itself a new variable and the
rule is to validate the chain before trusting a new batch.

### Two notes for whoever fits this next

The first version of the two-term fit grid-searched the tilt over a bounded range
and **railed at -2.00 dB/oct in nine cells**. A railed parameter is not a fit and
those numbers were discarded. The model is linear in the tilt at fixed corner, so
the tilt is now solved in closed form and railing is impossible. Check for this
whenever a fitted parameter sits exactly on a bound.

The cutoff-16 row is reported separately everywhere and never pooled. At that
setting our corner is about 550 Hz, so the 220 Hz reference sits only 2.5x below
it and the lowpass is tilting the band the highpass is measured in. Read that row
as a lower bound, not a measurement.

---

## 2026-08-02: the low-end highpass is APPLIED, on sign-off

Task 56, closed. `REBORN_LF_HP` defaults to 119.9 Hz with knee 0.141 instead of
0. This is the second default change since `d80e1a5`, after the filter refit.

The evidence it had to clear was not the fit that produced it. Batch N fitted
this on two TIED notes at two cutoffs. It is applied because it then held up on
data it never saw and did not cost anything anywhere else:

| instrument | off | on | ReBirth |
|---|---|---|---|
| low-end gap, 30 plucked cells, 6 cutoffs | 5.232 | **1.976** | 0 |
| batch H rolloff, oct rms | 0.1584 | **0.1504** | 0 |
| batch H prominence, dB rms | 3.83 | **2.65** | 0 |
| whole-curve peak frequency, oct rms | 0.0996 | 0.0997 | 0 |
| whole-curve prominence, dB rms | 3.19 | **2.32** | 0 |
| corner-rise excess, oct | +0.16 | **+0.14** | 0 |
| whistle articulation, dB | -23.3 | **-30.7** | -29.4 |

Every instrument improves or holds and none regresses, which is the same test
the drive refit had to pass. Batch H and the whole-curve metrics matter most
here because the highpass was not fitted on either, and prominence improving by
about a quarter on both is the strongest single piece of it.

The resonance span row does not move. That is expected rather than
disappointing: this filter works below the corner and the span is measured
above it.

Verified after applying: build clean, tests 1/1, 17 of 17 songs round-trip, and
the shipped default reproduces the scored configuration exactly, 0.1504 and 2.65
against 0.1504 and 2.65.

### Shipped knowingly incomplete at resonance 0

`fc(0) = 0` while ReBirth still loses up to 5.7 dB there, and the miss grows
with cutoff: -0.46 dB at cutoff 40 to -5.73 at 127. Resonance 127 is closed to
0.34 dB at every cutoff. Applying a law that is right over most of the knob and
absent at one end is better than leaving the whole thing off, and the
resonance-0 shape is not identifiable from the fixtures that exist. Task 62
builds the one that could. **Do not fit that column from batch L alone.**

### A baseline task 53 must re-measure

Gap articulation moves from -23.3 dB to -30.7 with this on, because the highpass
removes low-frequency energy that was filling the gaps between notes. That is
the same observable the DC blocker's 32 ms ring was diagnosed on, so task 53's
recorded numbers are now stale and must be regenerated rather than reused. The
two changes act on the same symptom from different ends, and 53 may be smaller
than it looked.

---

## 2026-08-02: batch Q. The resonance-0 gap is NOT a highpass, and more points proved it rather than fitting it

Task 62, run. 43 fixtures, C1 at 32.703 Hz crossed with E1 at 41.203 so their
combs interleave into 18 points below 340 Hz, plucked, at 60 BPM. Three times
batch L's resolution over the range where the unexplained loss lives.

**The prediction was wrong, and usefully so.** Task 62 was written on the theory
that the resonance-0 shape was UNIDENTIFIABLE from six points, and that tripling
the point count would resolve the fit. It did not. With 18 points no model in the
highpass family fits at all, at 10 to 25 times the noise floor. The problem was
never resolution. It was the model family, and the extra points are what made
that visible.

### The fixture worked, and here is the evidence rather than the assertion

**The interleaved comb is valid.** C1 and E1 are separate fixtures, separately
exported, and where their harmonics nearly collide they land on the same curve:

| | C1 | E1 | agreement |
|---|---|---|---|
| near 164 Hz | +0.58 | +0.66 | 0.08 dB |
| near 291 Hz | +0.03 | +0.01 | 0.02 dB |
| near 328 Hz | -0.29 | -0.38 | 0.09 dB |

**Per-point noise, which is NOT the pooled figure.** The 0.026 dB quoted for
batch L was one pooled statistic. Per harmonic, from the duplicate pairs: 0.024
dB rms at E1 cutoff 90 resonance 15, and 0.000 at C1 cutoff 127 resonance 30,
which came back bit-identical. At C1 cutoff 16 resonance 0 it is 0.190 dB rms
with a 0.435 dB worst point, so **the cutoff-16 column is excluded from every
number below**, as it was in batch L and for the same reason.

**Chain check: PASS.** Two cells duplicate batch L exactly at 120 BPM and
reproduce its exports to 0.072 dB worst, against batch L's own duplicate spread
of 0.068. The capture chain is intact across sessions and across a CD remount.

**Tempo is NOT neutral, and that is why the control existed.** The same note and
knobs at 60 BPM against 120 read +0.307 dB at cutoff 40 and +0.213 at cutoff 127,
which is four to twelve times the per-point noise. Consistent in sign. Two
readings are open and this batch cannot separate them: either the slower gate
resolves a 55 Hz fundamental better, in which case 60 BPM is the more accurate
measurement, or ReBirth genuinely behaves differently. **Do not merge batch Q's
numbers with batch L's without accounting for roughly 0.26 dB.**

### The finding: the gap has an interior maximum

ReBirth minus ours, referenced to 230-340 Hz, at resonance 0:

| Hz | cutoff 40 | cutoff 90 | cutoff 127 |
|---|---|---|---|
| 32.7 | -2.03 | -5.87 | -8.81 |
| 41.2 | -0.65 | -4.23 | -6.97 |
| 65.4 | +2.20 | -0.38 | -2.97 |
| 98.1 | **+3.12** | +1.60 | -0.50 |
| 130.8 | +2.62 | **+1.98** | +0.30 |
| 164.8 | +1.97 | +1.81 | **+0.66** |
| 228.9 | +0.77 | +1.06 | +0.54 |
| 294.3 | -0.02 | +0.01 | +0.03 |
| 327.0 | -0.34 | -0.51 | -0.29 |

A highpass is monotone increasing in frequency. **This is not.** It falls steeply
below about 65 Hz, rises to a maximum of +0.66 to +3.12 dB somewhere between 98
and 165 Hz, and declines again above. That interior maximum is the whole reason
every model failed:

| cell | 1-pole | 1-pole+tilt | 2-pole+tilt | 3-pole+tilt |
|---|---|---|---|---|
| cut 40 reso 0 | 1.699 | 1.118 | 0.932 | 0.899 |
| cut 90 reso 0 | 1.421 | 1.196 | 0.888 | 0.861 |
| cut 127 reso 0 | 0.756 | 0.716 | 0.399 | 0.459 |

Against a per-point floor of 0.02 to 0.05 dB. Adding poles barely helps, because
no highpass of any order has a maximum in its passband. Several fits also ran
away to 500 to 900 Hz with tilts of -10 to -17 dB/oct, which is the parameter
railing at the search boundary rather than a mechanism.

### What the shape suggests, held loosely

The peak sits near 100 to 165 Hz and **migrates upward with cutoff**: about 98 Hz
at cutoff 40, 131 at cutoff 90, 165 at cutoff 127. That is close to the 111.5 Hz
one-pole corner independently confirmed at resonance 127, which raises the
possibility of ONE mechanism near 110 Hz whose character changes across the
resonance knob, rather than a highpass that switches on.

That is a hypothesis and nothing here tests it. It is written down so it can be
tested, not so it can be assumed. The last two rounds each lost time to a
plausible mechanism adopted before it was checked.

### What must not happen next

Do not fit a bump. A resonant highpass, a peaking shelf or a band emphasis would
all reduce the residual, and this project has a standing rule against buying fit
with free parameters. The prior round already showed that adding one free tilt
dragged an independently pinned 111 Hz corner to 147. The next step is to find
what in ReBirth's signal path could produce an interior maximum near the filter's
own low-frequency region, and to test that against a fixture that discriminates,
not to add terms until the residual falls.

---

## 2026-08-02: our accent envelope fires ONCE PER BAR on Clodhopper. It should be about four times.

Luke, on the highpass ear check: ReBirth's chirp or wah "hits every quarter note
in rebirth, and it sounds like its hitting every bar in ours". He flagged it as
possibly contradicting the previous day's accent work. It does not contradict it.
It is the same finding arriving from the other side, and it says `ACC_TIE` 0.05
is being applied to cases it was never measured on.

### First, an instrument that failed, recorded so it is not trusted

`tools/analyze/mod_rate.py` measures where the TIMBRE moves: a level-independent
brightness trajectory, then the spectrum of that trajectory, so the rate of
filter movement is read directly. Nothing else in this project can see a rate
error, because `harm_response`, `curve_fit`, `batchH_filter` and the `skirt_*`
family all average a whole render into one spectrum, and even `onset_ab` asks
which steps RETRIGGER rather than how often the timbre moves.

It self-tests to within 1% on synthetic sweeps at 0.583, 2.333 and 4.667 Hz.
**And on the real renders it disagreed with Luke**, reporting ReBirth's dominant
modulation at the BAR rate on both 303s. That is the opposite of what he hears.

The self-test is why, and it is the trap the runbook already names: a clean
synthetic sweep proves the FFT works, not that the instrument can separate filter
movement from note structure in a real bassline with onsets every sixteenth,
rests, slides and seven bars of pattern change. **Calibrate with realistic noise,
not noiseless synthetics.** The tool is kept because the idea is right and the
diagnosis below vindicates it, but its numbers on real material are not evidence
until it is recalibrated on a fixture whose accent rate is known because we set
it.

### The pattern, decoded

Clodhopper's two 303s carry the SAME 16-step pattern and differ only in knobs.
Accent flags sit on steps **3, 8, 10, 12, 13, 14, 15**, seven of sixteen.

### What our engine actually does with them

Tracing `decideBassStep()` plus `if (accent) fireAccent(!slide ? 1.0 : ACC_TIE)`:

| step | beat | event | slideIn | accent flag | depth fired |
|---|---|---|---|---|---|
| 3 | 1.3 | Hold | | yes | 0.05 |
| 8 | 2.4 | Hold | | yes | 0.05 |
| 10 | 3.2 | Strike | yes | yes | 0.05 |
| 12 | 3.4 | **Release** | | yes | **discarded** |
| 13 | 4.1 | Strike | no | yes | **1.00** |
| 14 | 4.2 | Strike | yes | yes | 0.05 |
| 15 | 4.3 | Strike | yes | yes | 0.05 |

**Exactly one full-depth accent per bar, at step 13.** One per bar is 0.583 Hz.
A quarter note at 140 BPM is 2.333 Hz. Luke's factor of four is exactly the
factor in the trace.

The history is coherent rather than contradictory:

| build | full accents per bar | what Luke said |
|---|---|---|
| before `ACC_TIE` | 6 | the chirps are "so pronounced", applied to each note |
| `ACC_TIE` 0.05 | **1** | "sounds like its hitting every bar" |
| ReBirth | about 4 | "hits every quarter note" |

We overshot from one side of the target to the other.

### Two cases the accent probe never contained

`ACC_TIE` was fitted on two cases and reproduces both exactly: an accent on a
slid step, and an accent on a rest that is tied into, ReBirth giving +2.8 dB on
each against +22.4 for a real retrigger. Neither of those is what steps 10 and
12 are.

**Step 10 is slid into from a rest that was itself RELEASED.** Step 9 is a rest
whose own predecessor does not tie, so our engine releases the voice, and then
step 9's slide flag makes step 10 a Strike with `slideIn = true`. So we glide
into a note **from silence** and suppress both its retrigger and its accent.
There is nothing to glide from. ReBirth has no sounding note to tie to either,
so a fresh pluck with a full accent is the plausible behaviour and we do the
opposite.

**Step 12 is an accent on a plain rest while a note is still ringing.** Step 11
gates and does not tie, so at step 12 our `Release` path runs and `release()`
takes no accent argument: the flag is dropped entirely. The established finding
is that ReBirth reads the accent row on a rest and lands it on whatever is
sounding through, and something IS sounding through here.

Both are interactions between two flags, which is the species that has produced
every ear-caught defect in this component so far, and both are absent from
`gen_accentprobe.py` by construction: its `acc_mid` is a slid step inside a
sounding run, and its `rest_acc` is a rest tied into. Neither is a slide out of
silence, and neither is an accented rest that is not tied into.

### This is the batch N failure mode again

A law fitted where it was measurable, then applied where it was not. The
low-end highpass was fitted at resonances where it had saturated and was wrong
below the knee. `ACC_TIE` was fitted on two non-retriggering cases and is being
applied to a third and fourth case that are not non-retriggering at all.

### What must NOT happen next

Do not tune `ACC_TIE` upward to get the rate back. The rate is wrong because the
wrong steps are being suppressed, not because the suppression depth is wrong, and
raising it would re-break the tied run that 0.05 fixed on measurement and on
Luke's ear. Fix which steps suppress, then re-measure the depth.

The fixture that settles it needs both missing cases, plus the two `ACC_TIE`
already reproduces as controls so a fix cannot silently undo them.

---

## 2026-08-02: both accent-rate hypotheses CONFIRMED. We suppress two accents ReBirth fires.

Task 66, measured on `fixtures-2026-08-02/accentrate`, 13 fixtures built for
exactly these two cases. The statistic is brightness, a 1-5 kHz over 100-400 Hz
ratio in dB over the 60 ms following the event, averaged across four bars. It is
a RATIO so it reads the filter and not the amplitude envelope, which matters
because an accent changes both.

Duplicates agree to **0.02 dB** (-20.90 / -20.90, -9.58 / -9.56, -9.27 / -9.27),
so everything below sits far above the measurement's own noise.

### The accent LIFT, accented minus its matched control

| case | ReBirth | ours | |
|---|---|---|---|
| **A. slide out of SILENCE into an accented note** | **+5.89 dB** | **+0.17 dB** | we suppress it |
| **B. accent on a PLAIN rest over a ringing note** | **+10.61 dB** | **+0.00 dB** | we discard it |
| reference: a real retrigger with an accent | +5.95 dB | | |

Both hypotheses hold, and case B is the starker of the two: our Release path
drops the accent flag entirely, so the lift is exactly zero to the decimal.

### The sharpest number in the round

**Case A's lift is +5.89 dB and a genuine retrigger's is +5.95 dB.** They agree
to 0.06 dB. So ReBirth treats a slide out of silence as **exactly a fresh
retrigger**, which is the hypothesis stated in task 66 and is now measured rather
than argued. There is nothing to glide from, and ReBirth behaves accordingly.

That gives the fix its shape without any fitting: `slideIn` should suppress the
retrigger only when the voice is actually SOUNDING. It is a state question, not a
constant.

### Where the accent lands, when it lands

ReBirth's absolute brightness after an accented event:

| event | dB |
|---|---|
| accented retrigger | -9.27 |
| accent on a rest that IS tied into | -9.33 |
| accent on a PLAIN rest over a ringing note | -9.58 |

Within 0.31 dB of each other. When ReBirth's accent fires it reaches the same
place regardless of how the step is articulated. That is a constraint on any
depth law and it is worth more than another fitted constant.

### `ACC_TIE` 0.05 is NOT overturned

It was fitted on an accent on a slid step where the voice IS sounding, and on a
rest that is tied into, and it reproduces both. Nothing here touches those. What
is overturned is applying that suppression to two cases that are not
non-retriggering at all. Same failure mode as the batch N highpass law: fitted
where it was measurable, applied where it was not.

**Do not raise `ACC_TIE` to recover the rate.** Fix which steps suppress. The
depth question stays entangled with `ACC_SWEEP` and the env mod floor and belongs
in task 59's joint fit.

### A gap in my own fixture design, recorded so the next round closes it

`r303_A_control_sounding` has no matched no-accent twin, so the sounding-slide
case has an absolute number (-22.45 dB) and no lift. It is also probed at a
different step from the others, so it cannot be compared against them directly.
The two decisive cases each got a proper control and the confirmatory case did
not. Add `r303_A_control_sounding_noaccent` before quoting anything about that
cell.

### Both fixes implemented as default-off probes, and scored

`REBORN_SLIDE_NEEDS_VOICE` and `REBORN_REST_ACCENT`, in `BassRules`. Both live
in the tested boundary rather than in the DSP, because both are sequencer-STATE
questions rather than constants: the boundary now threads whether the previous
step left a note running.

**Byte-neutral with both off**, verified rather than assumed: Clodhopper's two
303s and eight demo-song tracks render identically to the pre-change build. The
two new unit tests were each deliberately inverted and the build confirmed to
fail on them.

| accent lift, dB | ReBirth | before | after | error before | error after |
|---|---|---|---|---|---|
| A. slide out of silence | +5.89 | +0.17 | **+3.03** | -5.72 | **-2.86** |
| B. accent on a plain rest | +10.61 | +0.00 | **+3.32** | -10.61 | **-7.28** |
| reference: a real retrigger | +5.95 | +3.03 | +3.03 | -2.92 | -2.92 |

**Every control moved by exactly 0.00 dB**, including both cells `ACC_TIE` was
fitted on. The probes are surgical.

**Case A is now structurally correct and its residual is a DIFFERENT problem.**
After the probe it lifts +3.03, which is exactly what our own plain retrigger
lifts, so it behaves as a retrigger just as ReBirth's does. Its remaining error
is -2.86 dB and the plain retrigger's is -2.92 dB, **agreeing to 0.06 dB**. That
is not a coincidence: it is task 60, our accent DEPTH being about half ReBirth's,
showing up identically in both cells. Case A is closed as a structural question.

**Case B is not fully explained.** -7.28 dB remains, well beyond the depth
deficit. Part of the +10.61 is a baseline artifact, since the unaccented control
there is only a decaying tail and therefore dark, but that does not account for
all of it. Left open rather than tuned.

### The rate, traced over Clodhopper

| build | full-depth accents per bar | |
|---|---|---|
| shipped default | 1 | 0.58 Hz |
| `slideNeedsVoice` only | 2 | 1.17 Hz |
| `restAccent` only | 2 | 1.17 Hz |
| **both** | **3** | **1.75 Hz** |
| ReBirth, by ear | about 4 | 2.33 Hz |

Three of the missing four recovered, from two changes each independently
measured on a purpose-built fixture rather than fitted to the rate. The rate was
the SYMPTOM Luke heard; neither probe was tuned against it, which is why the
agreement is worth something.

**Neither is applied.** Both want an ear check, and the honest framing for it is
that the rate should be closer but the accent DEPTH is still about half
ReBirth's on every cell, so the character will not be right yet.

---

## 2026-08-02: the MIDI map cross-check found a control we decode and throw away

Task 63. The runbook says the 808 section was never cross-checked against
Appendix C; that is stale, the matrix records it was done and that it corrected
three rows. What had genuinely never been checked is the **effects**, and that is
where the finding is.

### The delay drops PAN, on ten of the sixteen shipped songs

ReBirth's delay has exactly four MIDI-addressable controls, page 198: **Steps
100, Triplet 101, Pan 102, Feedback 103.**

`src/io/rbs.cpp` documents the chunk in its own comment as
`DELY (8): [ver][steps][unit][feedback][pan]` and then reads only `p[1]` and
`p[3]`. So `unit` and `pan` are decoded, named, and discarded.

Scanning the sixteen shipped songs:

| field | values seen | verdict |
|---|---|---|
| `unit` (triplet) | **1 in all sixteen** | unimplemented, but costs nothing on the catalogue |
| `pan` | **50, 60, 64, 79, 82, 88, 92, 103, 108, 127** | away from centre in **ten of sixteen** |

So the delay's stereo placement has been wrong on most of the shipped catalogue
for the whole project. This was found at a desk in a few minutes by comparing two
lists, and no amount of spectral measurement would have surfaced it, because
every metric that could see it was mono-summing.

Worth connecting to the pan law already recorded as about 7x too wide (task 45)
and to the whole-song spectral tilt (task 44). A dropped delay pan is exactly the
species of defect that survives a mono-summed check, which is the blindness
CLAUDE.md already names.

### The compressor has TWO controls, not five

Page 198 lists **Ratio 106 and Threshold 107 and nothing else.** Ours exposes
attack, release and makeup as well. Those are not necessarily wrong, since a
compressor has an attack and a release internally whether or not a user can reach
them, but they are **ours to fit rather than ReBirth's to match**. The matrix row
previously read "threshold, ratio, makeup behaviour" as though makeup were a
ReBirth control to be measured. Supertask 6 has two free user controls and three
hidden constants, which is a different problem from five free ones.

### The distortion control is SHAPE, and we call it tone

Page 198: **Shape 104, Amount 105.** We model the first as a tone tilt. "Shape"
suggests a waveshaping curve selector and "tone" suggests a filter, and no fit
reconciles those. Same class as the 808 snare's Tone-versus-Tune, where chapter
15's prose was wrong and the MIDI map was right. Measure what the control does
before fitting it.

### What has no MIDI address at all

Page 130 says Standard Mapping reaches "practically all" controls, so the
absences are informative. Tempo has no CC, only keyboard. Neither does pattern
LENGTH, the per-pattern shuffle flag, the vintage 303 switch, or mod selection.
That is a constraint on task 64: a MIDI-driven fixture could not sweep tempo.

---

## 2026-08-02: the accent's problem is the FILTER, not the VCA, and the law is the wrong SHAPE

Task 60, on `fixtures-2026-08-02/accentdepth`: nine knob values for the law,
three with the accent flag OFF, five at a second operating point, three
duplicates. Two statistics, because the manual is explicit that accent "does not
only affect volume, but also the shape and character of the sound" (p49): a
level-independent brightness ratio for the filter, and level for the VCA.

Duplicates agree to **0.01 to 0.05 dB**.

### The knob IS inert without the flag

With the accent flag off, knob 0, 64 and 127 give **identical** brightness and
level on both sides, to 0.02 dB. So the 303's accent knob is a per-step scaler
exactly as the 808's is, and it does nothing on its own. That was never checked
before and it fixes the SHAPE the law has to have.

### Level is nearly right. Brightness is not.

Lift against knob 0, at cutoff 64 resonance 60:

| knob | ReBirth bright | ours | error | ReBirth level | ours | error |
|---|---|---|---|---|---|---|
| 16 | +4.37 | +0.65 | **-3.72** | +1.64 | +2.17 | +0.54 |
| 32 | +7.18 | +1.26 | **-5.93** | +3.11 | +3.90 | +0.80 |
| 64 | +9.50 | +2.28 | **-7.22** | +5.56 | +6.54 | +0.98 |
| 96 | +10.27 | +3.03 | **-7.25** | +7.49 | +8.50 | +1.01 |
| 127 | +10.65 | +3.52 | **-7.14** | +8.96 | +9.98 | +1.02 |

At cutoff 100 resonance 30 the level error is smaller still, +0.10 to +0.42 dB,
and the brightness error runs -2.08 to -5.20.

**The VCA half of the accent is close**, about 1 dB heavy at one operating point
and under half a dB at the other. **The filter half is missing most of its
depth.** Task 60 was recorded as "ours is half ReBirth's", which is true of the
one point it was measured at and understates the rest.

### It is a SHAPE error, so no scalar fixes it

**ReBirth's brightness lift saturates hard**: it reaches +4.37 dB by knob 16,
which is 41 percent of its full travel in the first eighth of the knob, and
+9.50 by knob 64. **Ours is close to linear**: +0.65, +1.26, +2.28, +3.52.

Multiplying our depth by three would land knob 127 about right and leave knob 16
at +1.95 against ReBirth's +4.37. The curve is wrong, not just its gain.

And the error depends on the operating point, -7.14 dB at cutoff 64 resonance 60
against -5.20 at cutoff 100 resonance 30, so a single constant cannot carry it
even after the shape is fixed.

### A mechanism worth testing, not adopting

Saturation sits well with the other accent result from today: when ReBirth's
accent fires it reaches the same brightness whether the step is a retrigger, a
tied rest or a plain rest, within 0.31 dB. A knob that saturates and an outcome
that is articulation-independent both point at the accent driving the filter
envelope toward a **ceiling** rather than adding a proportional amount.

That is a hypothesis. It is written down so it can be tested against a fixture
that discriminates, not fitted. The standing rule applies: a saturating term
would reduce the residual on this data whether or not it is the mechanism.

### Where this sits

The accent modulates the same filter envelope that env mod and decay drive, so
the dependency order in COMPONENT_MATRIX still holds and this belongs in task
59's joint fit rather than being fitted alone. What has changed is that the
target is now a measured nine-point CURVE at two operating points rather than a
single point at knob 96, and the VCA half is known to be nearly right and can be
held while the filter half moves.

---

## 2026-08-02: ReBirth's filter CLOSES through every note and ours does not. This is the chirp, the snap, the stomp and the roundness, and they are one defect.

Luke, on the accent-rate ear check: "there's a roundness to each time the accent
hits, that wah / filtered sound. it's almost as if on the rebirth they're a
shape that's CLOSING, but on ours they're a shape that's OPENING."

That is a time-domain claim, and every accent measurement in this project so far
reduced the event to one number over one window. A filter that reaches the right
place by the wrong route scores identically to one that is correct.

### He is literally right, including the sign

Brightness through the note, onset to 110 ms, while the note is still audible.
Negative means the filter CLOSES. **Env mod is 0 in every one of these
fixtures**, so a non-accented note should in principle not sweep at all.

| | ReBirth | ours |
|---|---|---|
| accented, knob 127 | **-9.80** | -2.72 |
| accented, knob 32 | **-15.93** | -1.28 |
| **not accented** | **-5.70** | **+0.14** |

The non-accented row is the one to look at twice. ReBirth closes 5.7 dB through
a note with no accent and env mod at zero. Ours **opens** 0.14 dB. "A shape
that's closing" against "a shape that's opening" is not a metaphor, it is the
sign of the measurement.

And the two cross. ReBirth starts 3.5 dB BRIGHTER at the onset and ends 4.0 dB
DARKER at 110 ms, crossing near 100 ms, which is exactly what a pair of opposite
gestures sounds like.

### One defect, not four

This is the same thing Luke has been reaching for since the first ear check under
four different words. The "snap" on the transient, the "stomp" on the dark
closed-filter patches, the "chirp" that task 54 named, the under-pronounced wah
of task 55, and now the "roundness" of an accent. All of them are a filter
envelope that decays through the note. ReBirth always has one. We effectively do
not.

It also explains why the earlier accent measurements looked like a simple depth
deficit. A 60 ms window straddles ReBirth's decay, so measuring a moving target
with a static ruler returns a number that depends on the window rather than a
property of the system. The "-7.14 dB short" figure is window-dependent and
should not be quoted as a constant.

### The probe for it already existed, fitted, switched off

> **PARTLY RETRACTED. Read the CORRECTION two subsections below before using any
> number in the table that follows.** It was scored over 0 to 110 ms, which
> straddles the 62.5 ms gate close and is dominated by the release tail. Measured
> inside the gate, `EM_FLOOR` OVERSHOOTS at high accent rather than nearly
> closing the error. The qualitative finding above is unaffected.

`REBORN_EM_FLOOR=0.60` was fitted on 2026-08-01 against the decay probe, taking
the decay error 3.39 dB to 0.46, and left default off pending task 60. Applied
to today's fixture, which it has never seen, and scored on a statistic it was
not fitted on:

| | ReBirth | ours now | with EM_FLOOR | error now | error with |
|---|---|---|---|---|---|
| accented, knob 127 | -9.80 | -2.72 | **-9.48** | +7.09 | **+0.32** |
| accented, knob 32 | -15.93 | -1.28 | -8.05 | +14.65 | +7.87 |
| not accented | -5.70 | +0.14 | -3.68 | +5.84 | +2.01 |
| second operating point | -6.17 | -0.70 | -3.08 | +5.47 | +3.09 |

That is genuine cross-validation: a different fixture, a different statistic, a
value fitted the day before. The strongest cell goes from 7.09 dB of error to
**0.32**.

### It is not the whole answer, and the residual has structure

Knob 32 remains +7.87 dB out, worse than knob 127 at +0.32, and ReBirth sweeps
MORE at the smaller accent (-15.93 against -9.80) which is the opposite of the
naive expectation. A plausible reading is that a large accent holds the filter
open longer so it has fallen less by 110 ms, which would mean the accent has its
own decay rather than being a static offset. Not tested. Written down to be
tested.

The second operating point is also only half closed, +3.09 dB, so the law has a
cutoff or resonance dependence that 0.60 alone does not carry.

### What this changes about the plan

Task 59's joint fit now has measured targets for all three of its entangled
terms rather than one: `EM_FLOOR` validated on two independent fixtures, the
accent depth as a nine-point curve at two operating points, and the corner. And
the sweep-through-the-note is the statistic the joint fit should be scored on,
because it is the one that distinguishes the right route from the right
destination.

### CORRECTION, same day: the EM_FLOOR score above was measured through the gate

The table above scored the sweep from 0 to 110 ms. **The gate closes at 62.5 ms**
(half a step at 120 BPM), so that window straddles it and is dominated by the
release tail, where a resonant filter rings. This project has been bitten there
before: the VCA release had to be fitted around exactly that ring.

Re-measured wholly INSIDE the gate, 0 to 39 ms, which is the last reading a 23 ms
analysis window can take without crossing 62.5:

| | ReBirth | ours | with EM_FLOOR | error now | error with |
|---|---|---|---|---|---|
| accented, knob 127 | -0.82 | -1.12 | -3.49 | **-0.30** | **-2.67** |
| accented, knob 32 | -5.47 | -0.76 | -3.82 | +4.71 | +1.65 |
| not accented | -2.14 | +0.04 | -1.49 | +2.18 | +0.65 |
| second operating point | -2.14 | -0.24 | -1.07 | +1.90 | +1.06 |

**At knob 127 our current build is already nearly right, and EM_FLOOR makes it
worse.** Error -0.30 dB becomes -2.67, an overshoot. The claim that the strongest
cell went from 7.09 dB of error to 0.32 was measured through a contaminated
window and **is retracted**.

Pooled over the four cells, EM_FLOOR still helps: rms error 2.83 dB down to 1.70.
It is an improvement, not the near-solution the first table implied, and it
overshoots at high accent.

### Both windows measure something real, and they should be reported separately

This is not a case of one window being right. Level at 110 ms sits 27 to 31 dB
below the onset, which is quiet but well above any floor, so the tail is real
signal and it is audible.

- **Inside the gate** is the filter moving while the note is held.
- **After the gate** is the filter moving through the release, which for a
  resonant filter includes the ring, and is a large part of what a "wah tail"
  is.

ReBirth closes in BOTH and we close in neither, so the qualitative finding
survives untouched. What does not survive is a single number for "the sweep".
Any future score has to say which window, and the joint fit should carry both.

### The non-monotonic anomaly is NOT a tail artifact

Inside the gate ReBirth sweeps -2.14 dB with no accent, **-5.47 at knob 32**, and
only -0.82 at knob 127. Non-monotonic, and more pronounced inside the gate than
across the tail, so it is a property of the system rather than of the window.

A large accent holding the filter open long enough that little sweep remains
within 39 ms, while a medium one decays inside the window, would produce exactly
this. That is a hypothesis about the accent having its own decay. It is not
tested and must not be fitted.

---

## 2026-08-02: the env-mod-0 filter envelope, measured properly. EM_FLOOR 0.60 was two errors cancelling.

The decay-crossing grid RUNBOOK section 4 has asked for since 2026-07-27, built
and exported as `fixtures-2026-08-02/envshape`: 24 fixtures, decay swept 0 to 127,
plucked at env mod 0 and 64, **TIED at env mod 0**, plus a second operating point
and three duplicates. No accent anywhere, because accent is downstream.

The tied group is what made this work. A plucked note's gate closes at half a
step, so everything after that is tangled with the VCA release and the filter's
own ring, which is exactly what contaminated this morning's window. A tied note
holds the gate open and shows the envelope alone.

### ReBirth has a full filter envelope at env mod ZERO. We have none.

Tied, env mod 0, no accent. Total fall over 8 s, and the time to 63% of it:

| decay knob | ReBirth depth | ReBirth tau | ours depth | ours tau |
|---|---|---|---|---|
| 0 | 14.6 dB | 0.07 s | **0.0 dB** | none |
| 16 | 15.6 dB | 0.09 s | **0.0 dB** | none |
| 32 | 16.5 dB | 0.14 s | **0.0 dB** | none |
| 64 | 17.9 dB | 0.28 s | **0.0 dB** | none |
| 96 | 18.7 dB | 0.81 s | **0.0 dB** | none |
| 127 | 18.5 dB | 2.14 s | **0.0 dB** | none |

**A depth of 15 to 19 dB with a time constant the decay knob sets across a 30x
range**, and our shipped build does not move at all. This is the complete
characterisation of what task 54 named "the chirp", and of what Luke has called
snap, stomp, roundness and closing. It is a law now, not a floor constant.

### Our decay-to-tau mapping was already right

With `REBORN_EM_FLOOR` on, our time constants come out 0.05 / 0.07 / 0.09 / 0.28
/ 0.79 / 2.14 s against ReBirth's 0.07 / 0.09 / 0.14 / 0.28 / 0.81 / 2.14. The
top three agree to 0.02 s. The decay law itself was never the problem; only the
depth, which was zero.

### EM_FLOOR 0.60 fitted two errors against each other

Scored on two statistics across a sweep of the value, rms error in dB:

| EM_FLOOR | TIED depth | PLUCKED sweep, 0-39 ms |
|---|---|---|
| 0.60 | 7.38 | **0.40** |
| 0.80 | 4.32 | 1.30 |
| **1.00** | **1.70** | 2.65 |
| 1.30 | 4.52 | 4.73 |
| 1.60 | 10.05 | 6.90 |

They pick different answers, which means a scalar cannot satisfy both and the
SHAPE is wrong rather than the gain. The raw curves say why. Expressed as a
fraction of each curve's own total fall:

| | at 50 ms | at 150 ms | at 500 ms |
|---|---|---|---|
| ReBirth | 31.6% | 68.8% | 98.2% |
| ours | 41.0% | 79.5% | 99.6% |

**Our envelope falls too fast early.** The 39 ms plucked window sits exactly in
that region, so at 0.60 the missing depth and the over-fast early fall cancel and
produce a flattering 0.40 dB score. That is the "one parameter standing in for
another's error" signature this project has a standing rule about, and it is why
0.60 looked good on the decay probe it was fitted against.

### What the value should be, and what is left

On the tied trajectory, which sees the whole envelope rather than 39 ms of it,
**EM_FLOOR near 1.0** tracks ReBirth closely: at decay 32 it reads -6.49, -12.42
and -15.22 dB at 46, 139 and 325 ms against ReBirth's -5.21, -11.33 and -15.37,
settling 0.87 dB short of ReBirth's -16.49.

So the recommendation is 1.0 rather than 0.60, and **task 54's fitted value is
superseded** by a better-conditioned measurement. Not applied: this changes the
sound on every patch and wants Luke's ear, and it should go in with the joint fit
rather than alone.

What remains after it is the early shape, nine percentage points of fall too much
in the first 50 ms. That is a real residual and it must not be absorbed by
detuning the depth again.

### The frequency-versus-octaves question, and why envshape cannot answer it

Our envelope moves the corner in OCTAVES: the envelope is added to `log2(fc)`.
If ReBirth's instead adds to `fc` in Hz, the trajectory differs in a specific
way, because `d(log fc)/de = B/(fc0 + Be)` is small while the envelope is large
and grows as it decays. That predicts a dB fall that starts slowly and
accelerates, which is the sign of the residual measured above: ReBirth is 31.6
percent through its fall at 50 ms where we are 41.0.

**Tested, and the fixture cannot decide it.** Tracking the interpolated resonant
peak over time on the slow decays, then fitting an exponential once in Hz and
once in log2(Hz), residual as a percent of the trajectory's own span:

| | decay | peak travel | fit in Hz | fit in log2 |
|---|---|---|---|---|
| ReBirth | 96 | 987 to 522 Hz | 3.6% | 3.5% |
| ours, EM_FLOOR 1.0 | 96 | 878 to 431 Hz | 6.2% | 5.4% |
| ReBirth | 127 | 1002 to 535 Hz | 6.3% | 6.7% |
| ours, EM_FLOOR 1.0 | 127 | 895 to 447 Hz | 8.4% | 7.7% |

The two models differ by well under one percentage point everywhere, far below
the scatter. **This is an identifiability limit rather than a measurement
failure**: the corner travels only about 0.9 octaves at env mod 0, and over less
than an octave an exponential in Hz and one in log2(Hz) are very nearly the same
curve. No care with the estimator changes that.

An earlier attempt using the 85% spectral rolloff gave 8 to 29 percent residuals
and was even less able to discriminate, for the reason RUNBOOK already records
about `rolloff()`: it snaps to the harmonic comb. The interpolated peak is the
right instrument and it still cannot separate these two.

`fixtures-2026-08-02/envdomain` is the fixture that can: TIED so the gate never
closes, at env mod 32 to 127 so the corner sweeps several octaves, at decays 96
and 127 so a 93 ms frame is short against the time constant, plus two
low-cutoff cells where the travel is largest.

### A separate observation from the same trajectories, worth its own line

With `EM_FLOOR` at 1.0 our resonant peak sits **0.17 octaves below ReBirth's at
the start of the note and 0.28 below at the end**, and travels 0.11 octaves
FURTHER over the decay (1.03 against 0.92). So the envelope's endpoints are both
low and its excursion slightly too large. That is a calibration target for the
joint fit and it is at cutoff 64 resonance 60, which is a different cell from
task 58's resonance-0 measurement and must not be pooled with it.

---

## 2026-08-02: ReBirth's filter envelope is exponential in FREQUENCY. Ours is in octaves.

> **RETRACTED the same day, later round. Do not read this section standalone.**
> Re-measured with a calibrated instrument, ReBirth's envelope is exponential in
> OCTAVES, which is what ours already does. The travel table below is also wrong,
> reading our own env mod travel as 2.81 octaves where the code commands exactly
> 2.2. See "RETRACTION: the envelope domain" further down. What survives is that
> the travel GAP is real, and larger than this table says.

`fixtures-2026-08-02/envdomain`, 12 fixtures built specifically because the
envshape grid could not answer this: TIED so the gate never closes, env mod 32 to
127 so the corner sweeps far more than an octave, slow decays so a 93 ms frame is
short against the time constant, and two low-cutoff cells where the travel is
largest.

Fitting the interpolated resonant-peak trajectory as an exponential decay, once
in Hz and once in log2(Hz). Residual as a percent of the trajectory's own span,
so lower is the domain the envelope actually acts in.

| cell | ReBirth travel | fit in Hz | fit in log2 |
|---|---|---|---|
| env mod 32 | 1.84 oct | **2.7%** | 3.9% |
| env mod 64 | 2.77 oct | **2.0%** | 3.7% |
| env mod 96 | 4.06 oct | **2.2%** | 3.1% |
| env mod 127 | 4.83 oct | **1.8%** | 4.2% |
| env mod 127, cutoff 16 | 4.32 oct | **1.4%** | 7.1% |
| env mod 127, cutoff 16, decay 96 | 4.24 oct | **0.9%** | 4.0% |

**Hz wins in every cell, and by a factor of two to five where the excursion is
largest.** At env mod 127 cutoff 16 the Hz fit is 1.4 percent and the octave fit
is 7.1. That is not a close call, and it only became visible once the corner
swept four octaves instead of one: the same test on the env-mod-0 grid gave 3.6
against 3.5 and could not decide.

The peak is a proxy for the corner, and the test rests on the two being
proportional, which is the standard behaviour of a resonant ladder. Under that
assumption an exponential in the corner is an exponential in the peak, scaled.

### This explains the residual shape directly

An envelope adding to `fc` in Hz has `d(log fc)/de = B/(fc0 + Be)`, which is
small while the envelope is large and grows as it decays. So the dB trajectory
falls slowly at first and accelerates. That is exactly the residual the envshape
grid measured: as a fraction of its own total fall ReBirth is 31.6 percent done
at 50 ms where we are 41.0. Our envelope is added to `log2(fc)`, which makes the
dB fall a pure exponential and therefore too fast at the start.

So the remaining shape error after `EM_FLOOR` is not a constant to be tuned. It
is the domain the envelope is applied in, and that is a structural change.

### A second finding from the same trajectories: our env mod travel is short

| env mod | ReBirth travel | ours | ratio |
|---|---|---|---|
| 32 | 1.84 oct | 1.23 | 0.67 |
| 64 | 2.77 | 1.72 | 0.62 |
| 96 | 4.06 | 2.34 | 0.58 |
| 127 | 4.83 | 2.81 | 0.58 |

**Our corner travels 58 to 70 percent of ReBirth's.** The recorded env mod fit
(0.2215 to 0.0384 octaves rms, nine points crossed with three decays) was scored
on peak POSITION at a moment, not on the excursion of a held note, so it can be
right where it was measured and short across the sweep. Same species as
everything else found today.

### Held loosely

Our own trajectories fit better in Hz than in log2 in three cells, which should
not happen if our envelope is purely in octaves. Either the peak-to-corner
proportionality breaks down over a wide sweep, or our envelope is not a clean
exponential in its own domain. Not chased. Recorded so that whoever changes the
domain checks our side afterwards rather than assuming it became correct.

---

## 2026-08-02: batch H carries the envelope confound, so the envelope has to land BEFORE the filter constants

Checked while working out where the frequency-domain change belongs in the
order. **Batch H is PLUCKED, env mod 0, decay 64.** Every note therefore
retriggers ReBirth's env-mod-0 envelope, whose time constant at decay 64 is 0.28
s against a 125 ms step, so it never settles between notes. Ours has no envelope
at all.

Brightness change from note onset to 39 ms, averaged over 32 notes:

| cell | ReBirth | ours | difference |
|---|---|---|---|
| cut 40 reso 127 | **-4.12** | -0.11 | -4.01 |
| cut 40 reso 60 | -2.40 | -0.03 | -2.37 |
| cut 16 reso 127 | -2.48 | -0.04 | -2.44 |
| cut 64 reso 60 | -2.20 | -0.03 | -2.17 |
| cut 90 reso 0 | -0.75 | -0.02 | -0.73 |
| cut 127 reso 60 | -0.09 | -0.01 | -0.08 |

**ReBirth's every note sweeps and ours does not.** The confound is up to 4 dB at
low cutoff with high resonance and under 1 dB at high cutoff, so it is not
uniform, and it is largest exactly where the harmonic instrument has the most
signal to fit.

### What this does and does not mean

The filter refit of 2026-07-31 was fitted on batch H with a static model against
a target that was moving. Its constants may therefore have absorbed envelope
error, which is this project's most repeated failure mode.

It does **not** mean the refit was wrong. It improved three independent
instruments at once, held on a fixture set it never saw, and was confirmed by
ear on six patches. The residual it left, 2.627 dB rms, is the same order as the
confound found here, which is suggestive rather than conclusive.

What it means is an ORDERING constraint. The static filter constants cannot be
finally trusted until they are refitted with the envelope present on both sides,
because until then they are being asked to explain a sweep they have no term
for.

### The order that follows

1. Implement the envelope in the FREQUENCY domain, default off. Small.
2. Refit `EM_FLOOR` depth in the new domain, on the envshape tied trajectories.
3. Refit the env mod depth, currently travelling 58 to 70 percent of ReBirth's.
4. **Then** refit the static filter constants on batch H, now that both sides
   have an envelope.
5. **Then** the accent depth curve, which sits on top of all of it.

That is task 59's joint fit, and steps 1 to 3 are its real prerequisites rather
than the decay grid, which is now done.

---

## 2026-08-02: the envelope ear check was staged on material where the envelope cannot be heard

Luke, on the envelope ear check: none, `EM_FLOOR` 0.60 and 1.00 "all do not
meaningfully improve our version", and did not sound markedly different from the
previous batch. The files were verified to be three genuinely different renders,
so the probes took effect. **He was right and the fault is in the fixture
choice.**

The patches were selected for having ENV MOD 0, which is the condition the
envelope exists under, without checking DECAY against note length:

| patch | decay | envelope tau | gate | fall WITHIN a note |
|---|---|---|---|---|
| Clodhopper 303-2 | 115 | 1.63 s | 54 ms | **3%**, 0.6 dB of a 17 dB envelope |
| drv_closed | 60 | 0.26 s | 62 ms | 21%, 3.6 dB |
| detroit_303 | 80 | 0.55 s | 62 ms | 11%, and env mod 95 already dominates |

Clodhopper's envelope moves three percent inside a note. It is a static
brightness offset there, not a gesture, and the whole-file rms match removes part
of even that. He was asked to hear a shape in material where the shape cannot
occur.

**The lesson generalises past this instance.** An ear check has an operating
point exactly as a measurement does, and choosing one by the parameter under test
alone is not enough: the effect has to be able to EXPRESS itself in the material.
For an envelope that means decay against gate length, not just env mod.

### The listening fixtures, and what they show

`fixtures-2026-08-02/envlisten`, built to make the effect maximally audible and
labelled LISTENING ONLY because an exaggerated operating point flatters whatever
is judged on it. Short decay against a long gate, resonance 115, accent 0, env
mod 0, 90 BPM so the gate is 83 ms rather than 54.

Whole-file brightness, dB:

| fixture | ReBirth | none | EM_FLOOR 1.0 | gap closed |
|---|---|---|---|---|
| plucked, decay 24, cutoff 64 | -5.7 | -4.0 | **-12.9** | **-5.4** worse |
| plucked, decay 48, cutoff 64 | -2.6 | -4.0 | **-7.5** | **-3.5** worse |
| plucked, decay 24, cutoff 40 | -22.7 | -17.0 | -28.1 | +0.4 |
| **tied, decay 80, cutoff 64** | -19.0 | -4.2 | -24.2 | **+9.7** better |

**EM_FLOOR 1.0 overshoots badly on plucked notes with a short decay** and closes
most of the gap on a held one. It was fitted on the tied depth, and it behaves
like a value fitted there: right where it was fitted, wrong elsewhere.

That is the third time today the same shape has appeared. `ACC_TIE` fitted on two
cases and over-applied to two others. The batch N highpass fitted where it had
saturated. `EM_FLOOR` 0.60 fitted on a 39 ms window. Each time a value was right
at its own operating point and wrong away from it, and each time the underlying
reason was a model whose SHAPE could not span the range.

### What this does to the recommendation

**Neither 0.60 nor 1.0 should ship.** 0.60 fits the short window and is 7.4 dB
short on held-note depth; 1.0 fits the depth and is 3.5 to 5.4 dB too dark on
short plucked notes. There is no value between them that fixes both, because the
error is the octave domain making the fall too fast at the start, which is
exactly what task 67 measured.

So the envelope should go in WITH the frequency-domain change rather than before
it. That is a change of plan: the earlier note said implement the envelope first
and treat the domain as a refinement worth about a tenth as much. On the tied
fixture the domain is worth a tenth; on a short plucked note it is the difference
between closing the gap and overshooting it by 5 dB.

---

## 2026-08-02: the resonant peak is audible as a PITCH, and Luke read the corner error off it to within half a semitone

Luke, on `v303_pluck_dec024_c040`: ReBirth has "an audible harmonic that sounds
like an E", ours at `EM_FLOOR` 1.0 has "a C# harmonic", and ours "sounds more
like ReBirth, but not like ReBirth".

He was describing the resonant peak. Measured, on the same files, with the
interpolated harmonic peak:

| | peak | as a pitch |
|---|---|---|
| ReBirth | 701.2 Hz | F5 +7c |
| ours, no envelope | 863.9 Hz | A5 -32c |
| ours, `EM_FLOOR` 1.0 | 576.5 Hz | D5 -32c |

**He named the interval C# to E, which is three semitones. The measurement is
3.39 semitones.** His absolute naming sits a semitone low, which is ordinary for
picking a resonant peak out of a complex tone, and the INTERVAL, which is the
part that carries the information, is right to within half a semitone.

### Two errors, cleanly separated, and they straddle

| configuration | peak against ReBirth |
|---|---|
| no envelope | **+0.301 octaves**, too high |
| `EM_FLOOR` 1.0 | **-0.282 octaves**, too low |

ReBirth sits almost exactly between our two configurations. So this one fixture
separates two independent errors: the STATIC corner is about 0.3 octaves high at
this operating point, and the envelope at 1.0 then overshoots downward by about
the same amount.

Interpolating, a value near **0.52** would put the peak on ReBirth's here, which
is close to the originally fitted 0.60 and is the third statistic today to pick a
different value. That is the same story as everywhere else: with the shape wrong,
each statistic picks the value that suits it.

**Independent confirmation of an earlier number.** The envdomain trajectories put
our peak 0.17 octaves below ReBirth's at a note's start and 0.28 below at its
end, at `EM_FLOOR` 1.0. This fixture, a different patch measured a different way,
gives -0.282. Those agree.

### A discrepancy to check, not to resolve here

Task 58 records our corner as 0.17 octaves LOW at cutoff knob 16 and 0.22 HIGH at
knob 127, crossing near 64. This fixture is cutoff 40, below that crossover, so
task 58 predicts we should be LOW and the measurement says +0.30 HIGH. The cells
are not the same: task 58 was measured at resonance 0 and this is resonance 120,
and by a different method. Flagged rather than reconciled.

### A method note worth keeping

**For a resonant filter the peak position is audible as a PITCH, and a musician
can name the interval.** That is a far finer discrimination than "brighter" or
"darker", and it converts a corner error directly into semitones. Deliberately
staging a high-resonance fixture so the peak sings, and asking for the interval
rather than for a judgement, is a measurement channel this project has not been
using and should.

---

## 2026-08-02, later round: RETRACTION, the envelope domain

**ReBirth's filter envelope is exponential in OCTAVES, which is what ours already
does. The finding earlier today that it is exponential in FREQUENCY does not
survive re-measurement and is retracted.** Nothing was shipped on it. The plan it
produced, `docs/research/PLAN_303_ENVELOPE.md`, is rewritten.

### What went wrong is not the fixture, it is that the instrument was never calibrated

The `envdomain` fixture is sound and is reused unchanged here. What did not exist
was any check that the tool reading it could tell the two domains apart. The tool
itself lived in a scratchpad and did not survive the session, so the exact fault
cannot be reconstructed. What can be shown is that its conclusion does not
reproduce, and that it failed a check it could have run at the time.

**The check it failed.** Our own env mod travel is a known number: `EM_OCT` is
2.2, so at env mod 127 the code commands the corner to travel exactly 2.2
octaves. The retracted table reports our travel as **2.81 octaves**. A peak-based
estimator can under-read the corner's excursion, and this one does, but it cannot
exceed the command by 28 percent. That single line is enough to condemn the
measurement, and it was sitting in the table.

### The instrument, and its calibration

`tools/analyze/env_traj.py`, now in the repo rather than in a scratchpad. It
tracks the resonant peak by sampling the transfer function at the note's
harmonics and interpolating a parabola in log frequency, then fits

    octaves     log2 f(t) = a + b exp(-t/tau)
    frequency        f(t) = A + B exp(-t/tau)

**Both minimising the same objective, rms residual in octaves.** Fitting each in
its own units hands each model a home-ground advantage; that alone changes the
answer, and is the likeliest single cause of the retracted result (scored in own
units the frequency model's apparent residual drops by about a third).

It is calibrated against trajectories whose domain is KNOWN, because our engine's
domain is switchable. `tools/rbsgen/gen_envcal.py` builds 24 tied cells and
`REBORN_ENV_DOMAIN` renders them twice:

| known truth | reads hz | reads oct | reads tie |
|---|---|---|---|
| octave-domain renders, 24 | 0 | 10 | 14 |
| frequency-domain renders, 24 | 6 | 0 | 18 |

**No crossed verdict in 48 renders.** The tool is insensitive, returning "tie"
more often than not, and that is the correct failure mode for a verdict: it
declines rather than guesses.

### What it says about ReBirth

| analysis band | hz | oct | tie |
|---|---|---|---|
| default, 198 Hz to 5.2 kHz | **0** | 8 | 4 |
| ceiling at 4 kHz | **0** | 8 | 4 |
| ceiling at 3 kHz | **0** | 8 | 4 |
| ceiling at 2.2 kHz | **0** | 6 | 6 |
| floor at 2.5 x f0 | **0** | 6 | 6 |
| floor at 3.5 x f0 | **0** | 0 | 12 |
| frame 2048 | 1 | 3 | 8 |
| frame 8192 | **0** | 4 | 8 |

Narrowing the band converts oct verdicts to ties, which is what less data should
do. It never produces a frequency verdict. One cell of twelve crosses at the
shortest frame; the calibration set does not cross at any frame size.

ReBirth's travel is 2.9 to 4.4 octaves in these cells, and the calibration shows
the tool detects the frequency domain reliably at 3 octaves. If ReBirth's
envelope were in Hz, this would have said so.

### The failure that produced the calibration, worth keeping

The first calibration attempt used the `envdomain` fixtures with the env mod
depth pushed up to match ReBirth's travel, and it FAILED: given octave-domain
renders it answered "hz" on a third of them and "oct" on none. The cause was our
own filter's 6 kHz ceiling. Driven that hard the corner pins at the top and only
then decays, and flat-then-falling is exactly the frequency-domain signature. The
instrument was reading a clamp as a finding.

**A calibration that fails is worth more than one that passes.** This one named
the confound, and the band guard that answers it is now in the tool. The lesson
generalises past this measurement: an operating point chosen to make an effect
large can push the thing being measured into a regime where it no longer means
what it did.

### What survives, and it is the substantive part

**The travel gap is real and LARGER than the retracted table said.** Measured
with the calibrated tool, decay 96, tied, cutoff 64:

| env mod | ReBirth | ours | note |
|---|---|---|---|
| 32 | 1.91 | 0.26 | |
| 64 | 2.93 | 0.80 | |
| 96 | 4.01 | 1.50 | ReBirth truncated, lower bound |
| 127 | 4.44 | 1.99 | ReBirth truncated, lower bound |

ReBirth's high cells are LOWER BOUNDS: its corner is still falling when it leaves
the measurable band, so the true travel is larger. Ours are complete.

The tool's gain on this quantity is calibrated too, by rendering a known
commanded travel and measuring it:

| commanded | measured | ratio |
|---|---|---|
| 1.0 oct | 0.85 | 0.85 |
| 2.0 | 1.71 | 0.86 |
| 3.0 | 2.34 | 0.78 |
| 4.0 | 3.28 | 0.82 |

So it under-reads by 15 to 20 percent, consistently, and never over-reads. Our
shipped 2.2 commanded reading as 1.99 is exactly right for that gain.

**`EM_OCT` is short by at least a factor of two, not the 1.5 previously recorded.**
Correcting ReBirth's 4.44 by the same gain implies a commanded travel above 5
octaves against our 2.2, and that is still a lower bound. The correction assumes
ReBirth's peak sits relative to its corner the way ours does, which is not
measured; the uncorrected comparison, 4.44 against 1.99 by the same instrument,
needs no such assumption and is already more than a factor of two.

### The switch stays, default off, and here is why it earns its place

`REBORN_ENV_DOMAIN=hz|oct` is implemented and byte-neutral at its default,
verified over 32 renders of the sixteen demo songs. Its value is no longer that
we might ship it. **It is the only way to produce a trajectory whose domain is
known, so it is what calibrates the instrument.** Delete it and `env_traj.py`
becomes unfalsifiable again.

---

## 2026-08-02, later round: the env mod knob is BIPOLAR, and that is the missing shape

The retraction above left the travel gap standing and I set out to fit it as two
scalars, `EM_FLOOR` for the intercept and `EM_OCT` for the slope. That fit
succeeded on its own data and **failed held-out validation**, which is what
turned up the real law. Both are recorded, because the failure is the useful part.

### Measure the two ENDPOINTS, not the travel between them

Tied note, cutoff 64, resonance 60, decay 96, `tools/analyze/env_traj.py`:

| env mod | ReBirth onset | ReBirth floor | ours onset | ours floor |
|---|---|---|---|---|
| 0 | 987 Hz | 517 Hz | 903 Hz | 902 Hz |
| 32 | 1498 | 400 | 1081 | 903 |
| 64 | 2164 | 283 | 1576 | 903 |
| 96 | 3180 | (guard) | 2549 | 903 |
| 127 | 4763 | (guard) | 3585 | 903 |

**ReBirth's floor falls as the env mod knob rises. Ours does not move at all.**
That is not a gain error, it is a missing degree of freedom, and no value of any
scalar in our model can produce it: our env mod term is anchored at the closed
end and can only ever open the filter.

Fitted, with the two guard-limited floors excluded:

    onset rises  +2.25 octaves per unit knob   (5 points)
    floor falls  -1.72 octaves per unit knob   (3 untruncated points)

So the knob's total depth is 3.97 octaves, of which **57 percent goes up and 43
percent goes down**, about a centre that does not move.

### The law

    log2 fc = log2(685 Hz) + (F + A k) (env - c)

with `k` the env mod knob 0 to 1, `env` the envelope 1 to 0, `F` = 0.93 octaves
(the depth at env mod zero, which is the thing our engine lacks entirely),
`A` = 3.97 octaves per unit knob, `c` = 0.434.

Predicted against measured, all eight usable endpoints:

| env mod | onset pred | meas | err | floor pred | meas | err |
|---|---|---|---|---|---|---|
| 0 | 987 | 987 | 0.0% | 517 | 517 | 0.0% |
| 32 | 1461 | 1498 | -2.4% | 383 | 400 | -4.2% |
| 64 | 2164 | 2164 | 0.0% | 283 | 283 | 0.0% |
| 96 | 3204 | 3180 | +0.8% | 210 | guard | |
| 127 | 4687 | 4763 | -1.6% | 157 | guard | |

Three parameters over eight endpoints, worst error 4.2 percent and most under 2.

### The scalar fit that failed, and why the failure was the finding

Fitting `EM_FLOOR` and `EM_OCT` on the six cells where neither side truncates
gave **0.9 and 4.8, rms 0.071 octaves**, which looks excellent. Held out:

| held-out set | result |
|---|---|
| decay 0, 16, 32, 64 at env mod 0 | short by 0.07 to 0.35 octaves |
| cutoff 16, env mod 127 | over by 0.80 to 0.96 octaves |
| env mod 96 and 127, one-sided | short by 0.86 to 1.43 octaves |

Held-out rms 0.542 against a fit rms of 0.071, a factor of eight. **Do not ship
`EM_FLOOR` 0.9 with `EM_OCT` 4.8.** The reason it cannot hold is now plain: with
the floor pinned, all of ReBirth's downward swing has to be bought by pushing the
onset higher, which overshoots at the top and pins against our 6 kHz ceiling. The
cutoff-16 cells over-run by exactly the amount the missing downward swing would
have supplied.

One caveat on my own validation, stated because it was a real error in it: the
cutoff-16 rows are ReBirth lower bounds too, since its floor there is also below
the guard, so scoring them two-sided was wrong. They are weaker evidence than
they look. The env mod 96 and 127 rows are the sound ones.

### This is very likely the same defect as the EM_FLOOR statistic conflict

Three statistics each picked a different `EM_FLOOR` (0.60, 1.00, ~0.52) and that
was read as a domain error. The domain is refuted. A missing bipolar swing
explains it at least as well and without a structural change: with the model only
able to open the filter, each statistic picks whatever single value best hides
the missing downward half at its own operating point. That is a hypothesis with a
clean test, which is to implement the swing and re-run all three.

### What the next fixture must do differently

The floor is unobservable above env mod 64 because the band guard sits at
1.8 x f0 = 198 Hz and ReBirth's corner goes under it. **The fix is a lower note**,
not a wider guard: at 55 Hz the observable band starts near 99 Hz and the whole
sweep fits. Every envelope fixture so far uses note 9 at 110 Hz, which is why
this has never been visible.

---

## 2026-08-02, later round: the env mod swing is SYMMETRIC, measured an octave down

`fixtures-2026-08-02/envfloor`, 21 fixtures, 21 ReBirth exports, 0 failed. The
bipolar law above was fitted on three floor points because above env mod 64
ReBirth's corner sweeps under the analysis band. **Dropping the note an octave to
55 Hz halves the band's floor to 99 Hz and all five points become visible.**

Duplicates agree to 0.015 octaves at the onset and 0.000 at the floor, so the
noise floor of the whole chain from ReBirth's exporter to a fitted endpoint is
negligible against everything below.

### The law

    log2 fc = log2(centre) + (F + A k)(env - c)
    F = 0.85 oct    A = 4.45 oct/knob    c = 0.5

**`c` fits free at 0.495, and forcing it to exactly 0.5 costs 0.0003 octaves.**
The env mod knob spreads the corner symmetrically either side of where the panel
says the cutoff is. That is a designer's number, not a fitted one, and it is
worth more than a fourth parameter would have been.

Fit: ten cells, twenty endpoints, rms 0.106 octaves.

### Held out, which is the only part that counts

| held-out group | fitted centre | rms, free c | rms, c = 0.5 |
|---|---|---|---|
| cutoff 40 | 500 Hz | 0.136 | 0.142 |
| cutoff 90 | 1162 Hz | 0.224 | 0.221 |
| resonance 90 | 937 Hz | 0.195 | 0.198 |

`F`, `A` and `c` are held at their fit-set values and only the centre is
refitted, because the centre IS the cutoff knob's own corner and must move with
it. Degradation is a factor of 1.3 to 2.1. **The scalar fit this replaces
degraded by a factor of eight.** Constraining `c` to 0.5 is neutral held out,
which is the test that matters for a simplification.

The resonance-90 group is the weakest evidence of the three, because the
instrument tracks the resonant PEAK and the peak-to-corner offset itself grows
with resonance. That it holds at all is reassuring; the number should not be
mined for a resonance law.

### Applied to our engine

`EM_CENTRE` 0.5, `EM_FLOOR` 0.85, `EM_OCT` 4.45, floor error against ReBirth
across the env mod sweep at cutoff 64 resonance 60:

| env mod | shipped | with the law |
|---|---|---|
| 0 | +0.74 oct | +0.13 |
| 32 | +1.13 | +0.10 |
| 64 | +1.55 | +0.02 |
| 96 | +2.36 | +0.18 |
| 127 | +2.70 | +0.04 |

**A monotone error that grew to 2.70 octaves becomes a flat 0.09 with a 0.16
spread.** The onset offset is -0.09 over the same cells, so what remains is very
nearly a pure offset in the opposite direction at each end, which is a static
corner error rather than an envelope one.

The onset residual widens to -0.31 octaves at env mod 127. That is our own
peak-to-corner relation compressing as the corner climbs, not necessarily a law
error: the commanded corner there is 5610 Hz and the peak reads 4052. Do not fit
it out without separating the two.

### A cutoff-law measurement falls out of the same fit

The per-group centre is ReBirth's corner at that cutoff knob, measured through a
fixture built for something else entirely:

| cutoff knob | ReBirth centre |
|---|---|
| 40 | 500 Hz |
| 64 | 781 |
| 90 | 1162 |

A straight line of **3.09 octaves per unit knob to within 0.040 octaves**,
against our `CUT_SLOPE` of 2.90. Our cutoff law is about 6 percent shallow. That
is an independent line on task 58, from a different fixture and a different
method, and it is a much smaller finding than the swing.

### Status

**Measured, not applied.** Changing `EM_FLOOR`, `EM_OCT` and `EM_CENTRE` from
their defaults changes the shipped sound on every 303 patch with env mod up,
which is most of them. Needs Luke.

---

## 2026-08-02: the env mod swing APPLIED, and the regression that came with it

Applied on Luke's sign-off. `EM_CENTRE` 0.5, `EM_FLOOR` 0.85, `EM_OCT` 2.2 to
4.45, `CUT_SCALE` 1.05 to 0.78.

### Applying the envelope ALONE was a regression, and a large one

`tools/analyze/regress_ab.py`, new, scores every fixture set we hold a ReBirth
export for. The metric is third-octave spectral shape, each side normalised by
its own energy so level cancels, rms dB, lower better.

| | before | envelope only |
|---|---|---|
| envfloor, envdomain, batchP, batchN (tied) | 5.8 / 5.0 / 8.3 / 8.1 | 3.4 / 3.2 / 4.8 / 5.1 |
| batchH, batchI, batchQ, batchL (plucked, static) | 1.9 / 2.3 / 3.2 / 2.2 | 5.0 / 4.6 / 5.2 / 5.1 |
| **all 373 cells** | **3.962** | **4.698** |

Every tied fixture improved sharply and every static grid got worse, for 251 of
373 cells worse overall. This is exactly what step 5 of the plan warned about and
it is worth stating plainly: **our cutoff, resonance and feedback constants were
all fitted through the old, wrong envelope.** Moving a note's onset corner by
2.65 octaves invalidates every one of them at once.

### The fix was bookkeeping, not a refit

`CUT_SCALE` 1.05 to 0.78. **The corner did not move.** With a symmetric swing the
constant stopped meaning "the corner with the envelope closed" and started
meaning "the centre the envelope swings about", which sits half the env-mod-0
depth lower: 2^(-0.85/2) = 2^-0.425, and 1.05 x 2^-0.425 = 0.7815.

A 140-cell grid search over `CUT_SCALE` crossed with `CUT_SLOPE`, pooled across
four differently-designed grids, landed on 0.78 independently. **It agrees with
the arithmetic to one percent, so no degree of freedom was spent.** And
`CUT_SLOPE` did not move, which is the same argument from the other side:
re-anchoring shifts a curve and cannot tilt it.

### The result, on all 373 cells

| fixture set | n | before | after | change |
|---|---|---|---|---|
| batchN | 20 | 8.112 | 1.831 | **-6.281** |
| batchP | 27 | 8.286 | 2.160 | **-6.126** |
| envfloor | 21 | 5.837 | 2.277 | -3.560 |
| envdomain | 12 | 5.033 | 2.884 | -2.149 |
| decayprobe | 7 | 3.704 | 1.624 | -2.080 |
| envshape | 24 | 3.908 | 2.106 | -1.802 |
| batchQ | 43 | 3.203 | 2.184 | -1.018 |
| batchL2 | 23 | 2.589 | 1.705 | -0.883 |
| accentprobe | 9 | 6.570 | 5.917 | -0.654 |
| batchI | 40 | 2.344 | 1.787 | -0.557 |
| accentrate | 13 | 3.995 | 3.445 | -0.550 |
| batchL | 21 | 2.204 | 1.746 | -0.458 |
| batchH | 36 | 1.943 | 1.730 | -0.213 |
| batchW | 3 | 5.140 | 5.217 | +0.077 |
| **batchG** | 54 | 2.641 | 3.086 | **+0.445** |
| **accentdepth** | 20 | 5.651 | 6.142 | **+0.490** |
| **ALL** | **373** | **3.962** | **2.536** | **-1.426** |

292 cells better, 79 worse. Only 12 of the 16 sets were used at any point in the
scan and the four largest improvements are all sets it never saw.

### The two sets that got WORSE, and why they are next

`batchG` and `accentdepth` both carry ACCENT. The accent sweep is still added in
the old one-sided anchoring, and its depth was fitted against the old envelope,
so it is now the only part of the filter path still living in the previous world.
That is step 6 of the plan and these two numbers are its acceptance test.

`batchW` at +0.077 over 3 cells is noise.

### Safety

32 demo-song 303 renders, before and after: peak 0.290 to 0.289, no clipped
files, mean rms -68.47 to -68.50 dB, no non-finite samples. Longest run of
single-peak-dominated spectrum 0.19 s to 0.37 s, which is a resonant note rather
than a self-oscillation lock. Build clean, tests pass, 18 of 18 songs round-trip.

---

## 2026-08-02: the accent has an ATTACK, and a decay refit that was rejected

### The accent rises to its peak at 52 ms. Ours peaks instantly.

Differencing an accented note against its matched no-accent control, so the
note's own filter envelope cancels. Accent knob 127, cutoff 64, resonance 60,
the accent's own contribution to brightness in dB:

| ms | 0 | 20 | 41 | 61 | 81 | 102 | 122 | 142 |
|---|---|---|---|---|---|---|---|---|
| ReBirth | 5.77 | 6.56 | 7.14 | **7.25** | 6.08 | 3.09 | -5.97 | -7.53 |
| ours | **4.45** | 3.55 | 1.53 | 0.03 | -1.26 | -2.44 | -3.32 | -4.39 |

Read the first four columns only: the gate closes at 62.5 ms and past that the
difference mixes the accent with the VCA release and with `ACC_SHORT`, and cannot
be attributed to either.

Inside the gate ReBirth climbs from 5.77 to 7.25 dB while we fall from 4.45 to
0.03. **The accent has a real attack of tens of milliseconds, not the 2 ms
placeholder**, and this is the same defect as the main envelope in a different
place: a shape that opens through a note, previously read as a level.

`ACC_ATK` 0.002 to 0.010 and `ACC_SWEEP` 0.35 to 0.80, fitted jointly on four
accent knobs, in-gate trajectory error 3.722 to 0.924 dB rms. The attack has a
clean interior minimum, 0.010 beating both 0.002 and 0.020.

**Across the whole inventory this improved 33 cells and made ZERO worse**, since
every fixture without an accent is untouched: accentdepth 6.142 to 3.988,
accentprobe 5.917 to 4.432, accentrate 3.445 to 2.738, batchG 3.086 to 2.842.

### The accent knob law is still the wrong shape, and must not be fitted here

| knob | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|
| ReBirth brightness lift | 4.37 | 7.18 | 8.66 | 9.50 | 9.98 | 10.27 | 10.50 | 10.65 |
| ours, at `ACC_SWEEP` 0.35 | 0.84 | 1.64 | 2.40 | 3.09 | 3.72 | 4.28 | 4.76 | 5.14 |

ReBirth is at 41 percent of its travel by knob 16 and 89 percent by knob 64; ours
is near linear. That is why knob 16 is the worst cell at every depth tried and no
scalar fixes it. The LEVEL half needs no such curve, being within about 1 dB
across the whole knob, so the curvature belongs to the filter half alone.

**Do not fit that curve on this table.** Brightness here is a band ratio, 1 to
5 kHz over 100 to 400, so a corner climbing past the upper band stops registering
and the measurement saturates whether or not the knob does. Separating the two
needs the corner itself, at a frame short enough to resolve a 10 ms attack.

### REJECTED: the quadratic decay law

Our decay-to-tau map is a straight line in log tau, right at the top of the knob
and 2.3x too fast at the bottom (0.03 s against ReBirth's 0.07 at knob 0). That
was a second-order residual for a week and stopped being second-order the moment
the envelope got deep: after the swing landed, the two worst cells in batchG were
`em127_dec000` and `em064_dec000`, both at decay 0.

A quadratic fitted through knobs 0, 64 and 127 predicts the three it was not
fitted on to 0.09 / 0.13 / 0.73 s against 0.09 / 0.14 / 0.81, which looks like a
good law. Scored across the inventory it is **not shippable**:

| | change |
|---|---|
| batchG | -0.184 |
| envshape | -0.231 |
| envfloor | **+0.146** |
| envdomain | **+0.133** |
| batchQ | **+0.036** |
| ALL 373 | -0.042, with **143 better and 160 WORSE** |

A 0.042 dB gain bought by making more cells worse than better is the "two errors
cancelling" signature again. And the reason is identifiable rather than
mysterious: **the tau table it is fitted against was measured under the OLD
envelope**, so those taus are confounded by the very thing that just changed. The
probe ships default-off (`REBORN_DEC_A`, `_B`, `_C`, verified byte-identical over
all 373 fixtures) and the tau table wants re-measuring before anyone fits this.

### Where the 303 filter stands after the whole round

| | rms dB against ReBirth |
|---|---|
| start of session | 3.962 |
| after the env mod swing plus corner re-anchoring | 2.536 |
| after the accent attack and depth | **2.325** |

314 of 373 cells better, 57 worse. The only set still worse than the session
start is batchG at +0.201, whose regression is now concentrated in the decay-0
cells above.

---

## 2026-08-02: CUT_SCALE 0.78 is a suspected COMPENSATION, flagged not resolved

Found while re-staging the ear checks, which is worth noting on its own: the
staging is a measurement and it caught something the fitting did not.

Two direct measurements of the same fixtures disagree about the same constant.

**The corner, measured by `env_traj.py`** on the envfloor tied cells at cutoff 64
resonance 60 decay 96, our peak against ReBirth's in octaves:

| env mod | onset, `CUT_SCALE` 0.78 | floor, 0.78 |
|---|---|---|
| 0 | -0.18 | -0.16 |
| 32 | -0.48 | -0.29 |
| 64 | -0.44 | -0.54 |
| 96 | -0.58 | -0.23 |
| 127 | -0.57 | -0.44 |

With `CUT_SCALE` left at 1.05 the same cells sit within 0.09 octaves at both
ends. **So by the corner measurement, 1.05 is right and 0.78 is 0.4 octaves low.**

**The spectrum, measured by `regress_ab.py`** on the same fixtures, prefers 0.78
by a wide margin: envfloor 2.277 against 3.356, batchP 2.160 against 4.787,
envshape 2.106 against 4.140, and the 373-cell aggregate is not close.

A corner that is measurably 0.4 octaves wrong cannot score better on spectral
shape unless it is CANCELLING something. That is the two-errors-cancelling
signature, and this time it was caught before it was believed rather than after.

### What it is not

A missing attack on the main filter envelope was the obvious candidate, since the
accent turned out to need exactly that on the same day. **Refuted**: with
`CUT_SCALE` back at 1.05, `REBORN_ENV_ATK` at 0.002, 0.010, 0.020 and 0.040 all
score 4.3 to 4.9 against the shipped 2.012 over six pooled sets. The attack does
not recover it at any value.

Still open as candidates: the flat HF shelf of task 40, the filter's slope above
the corner, and the resonance-to-corner coupling.

### Status

`CUT_SCALE` 0.78 SHIPS, because across 373 cells it is a large net improvement
and reverting it would be worse by every aggregate measure available. It ships
FLAGGED. Do not treat it as a measured corner, do not build another fit on top of
it, and re-open it when the cancelled error is identified.

`renders/EARCHECK_5_corner` stages the disagreement directly: three tied cells,
ReBirth against the shipped build against `CUT_SCALE` 1.05, level matched on
40 to 120 Hz so the match cannot touch the corner. The resonant peak sings on
these, and the useful question is the INTERVAL rather than a verdict.

---

## 2026-08-02, ear check: the movement is under-pronounced, and the ceiling is NOT why

Luke's listening on the five restaged sets, reported verbatim in substance: the
filter movement is more pronounced on ReBirth than on ours at every setting
tried, on tied notes, on plucked notes and with accents. On Clodhopper the
envelope "sounds like it is modulating on every beat" where ours is "largely
audible only around the 4th beat, roughly step 12".

### The rate half is measurable and real

Counting upward brightness gestures of 3 dB or more over twelve seconds of
Clodhopper's 303-2, against ReBirth's own solo export:

| | gestures | median depth | total |
|---|---|---|---|
| ReBirth | 64 | 13.5 dB | 1007 |
| ours, shipped | 48 | 9.9 | 504 |
| ours, session start | 43 | 5.2 | 322 |
| ours, `+REST_ACCENT` | 52 | | |
| ours, `+SLIDE_NEEDS_VOICE` | 41 | | |

**A quarter of ReBirth's gestures are missing from ours and those present are
about 70 percent as deep.** The round improved both (322 to 504 total movement)
without closing either. `REST_ACCENT`, still unapplied, recovers four of the
sixteen missing gestures; `SLIDE_NEEDS_VOICE` REMOVES seven, which is worth
knowing before it is ever switched on.

### REFUTED: our 6 kHz filter ceiling

The obvious explanation for `CUT_SCALE` 0.78 beating 1.05 was that with a 5.3
octave symmetric swing, 1.05 drives the corner into our ceiling and pins it, and
a pinned filter does not move. It looked strong: 1.05 halves the gesture count on
Clodhopper, 25 against 48, and total movement 181 against 504.

`REBORN_FC_MAX` was added to test it. **Raising the ceiling to 9, 12 and 16 kHz
changes NOTHING**, byte-for-byte identical gesture counts, depths and totals at
every value. The corner never reaches 6 kHz on that patch and the clamp was never
engaging. The probe ships default-off, verified byte-identical over all 373
fixtures, and is kept because the question will be asked again.

### So why DOES 1.05 halve the movement, and a caution about the number

Unknown. The leading suspicion is that the measurement is at fault rather than
the filter: the gesture metric is a band ratio over 800 Hz to 5 kHz, so if 1.05
puts the corner ABOVE that band, real movement stops registering. That is exactly
the artefact that made the accent knob law unfittable on brightness, in the same
session.

**So this table must not be used to choose between 0.78 and 1.05.** It measures
movement inside a band, not the corner. The corner measurement (`env_traj.py`,
which tracks the resonant peak and has no band) still says 1.05 is right at
cutoff 64 on the envfloor fixtures. Reconciling those needs the corner tracked on
Clodhopper itself, which is a musical pattern rather than a held note and will
need the estimator adapting.

`CUT_SCALE` 0.78 continues to ship, still flagged, and the cancelled error is
still unidentified. One candidate is now closed.

### The em000 settle time, and a second calibration of Luke's ear

He reported ReBirth's env-mod-0 filter "slowly closing and settling after about
1.5 seconds", with ours closing much faster. Measured on that exact cell,
envfloor tied, cutoff 64 resonance 60 decay 96:

| | tau | time to 90 percent | depth | onset | floor |
|---|---|---|---|---|---|
| ReBirth | 0.64 s | **1.48 s** | 0.96 oct | 1040 Hz | 533 Hz |
| ours, shipped | 0.84 s | 1.93 s | **0.54 oct** | 690 Hz | 473 Hz |
| ours, session start | 0.05 s | 0.12 s | 0.00 | 893 | 893 |

**"About 1.5 seconds" against a measured 1.48.** That is the second time in two
days his ear has read a physical quantity to better than the tolerance of the
instrument, after naming a corner interval as three semitones against a measured
3.39. Treat these as measurements.

And the diagnosis is not the one the words suggest. **Our time constant is
SLOWER than ReBirth's, not faster.** What he heard is depth: our envelope at env
mod zero travels 0.54 octaves where ReBirth's travels 0.96, so it arrives sooner
because it has less distance to cover. A 0.42 octave shortfall, concentrated at
the onset end (0.59 octaves low there against 0.17 at the floor).

**This does not match the fit.** `EM_FLOOR` ships at 0.85 and the joint fit put
ReBirth's F at 0.85 too, yet ReBirth's measured depth on this cell is 0.96 and
ours is 0.54. Two candidates, both already recorded elsewhere and neither
resolved: the joint fit trades across env mod cells so F is pulled by the high
ones, and `env_traj.py`'s gain on TRAVEL falls off badly for small travels (0.85
at 1.0 octave commanded, but 0.47 at 0.55 commanded). Either way the fitted F is
not delivering its nominal depth at knob zero and that is where the fit is
weakest. Re-open it with the travel-gain calibration applied.

### The rest of the ear check, recorded so it is not lost

At env mod 96 on the same fixture, ReBirth "starts more pronounced then
normalizes around the same time as ours, with a little more chirp". At cutoff 90
env mod 64 the two are "more or less indistinguishable", which is the first
unqualified pass on a filter fixture in this project and is worth keeping as the
operating point where we are already right.

On the tied set the under-pronunciation is present "at every env mod setting",
and on the plucked and accent sets "across all settings".

**Still unanswered**: within `EARCHECK_5_corner`, the direct comparison of
`2shipped` against `3altcorner`. He compared each against ReBirth rather than
against each other, so the corner question that check was built for is still open.

---

## 2026-08-03: the gesture gap is a DEPTH defect, and the two sequencer probes do not help

`PLAN_303_ENVELOPE.md` ranks the gesture gap as the largest remaining 303 error
and makes step 1 "re-score `REST_ACCENT` and `SLIDE_NEEDS_VOICE` against the new
envelope, since their original residuals predate it". Done. Both re-scored,
individually and as a PAIR for the first time, on both 303 voices.

Setup validated first: a fresh render of Clodhopper 303-2 at current defaults
reproduces the recorded 48 gestures, median 9.9, total 504 exactly.

### Neither probe helps, and the pair is worse than either

Threshold 3 dB, twelve seconds, `--solo` per voice, effects off.

| 303-2 | count | median | total |
|---|---|---|---|
| ReBirth | 64 | 13.5 | 1007 |
| shipped | 48 | 9.9 | **504** |
| `+REST_ACCENT` | 52 | 8.6 | 499 |
| `+SLIDE_NEEDS_VOICE` | 41 | 8.7 | 461 |
| `+both` | 45 | 8.6 | 456 |

| 303-1 | count | median | total |
|---|---|---|---|
| ReBirth | 63 | 5.4 | 789 |
| shipped | 32 | 5.9 | **222** |
| `+REST_ACCENT` | 29 | 8.4 | 210 |
| `+SLIDE_NEEDS_VOICE` | 25 | 7.1 | 157 |
| `+both` | 23 | 8.0 | 150 |

**Total movement, which is the statistic that multiplies count and depth, is
worse under every probe on both voices.** `REST_ACCENT` does add four gestures
on 303-2, as recorded, but they are shallow enough that the total falls 504 to
499. On 303-1 it goes the other way and REMOVES three. So the earlier note that
it "recovers four of the sixteen missing gestures" is true only of the count on
one voice, and buys nothing.

`SLIDE_NEEDS_VOICE` is clearly harmful here, minus 9 percent of total movement on
303-2 and minus 29 percent on 303-1. The pair is the worst or near-worst option
on both. **Neither should be switched on for this reason**, which settles the
open warning that they must not be paired without re-scoring.

### What the threshold sweep shows, and it reframes the defect

`REST_ACCENT` against shipped, at rising thresholds:

| | thresh 3 | 6 | 9 | 12 |
|---|---|---|---|---|
| 303-2 shipped | 48 / 9.9 | 38 / 12.1 | 27 / 13.6 | 21 / 14.2 |
| 303-2 `+REST_ACCENT` | 52 / 8.6 | 40 / 12.1 | **27 / 13.6** | **21 / 14.2** |
| 303-1 shipped | 32 / 5.9 | 16 / 9.9 | 13 / 11.7 | 6 / 12.1 |
| 303-1 `+REST_ACCENT` | 29 / 8.4 | 16 / 9.9 | **13 / 11.7** | **6 / 12.1** |

Above 9 dB the two are identical, count and median both. **`REST_ACCENT` does not
touch a single deep gesture.** It only adds or removes shallow wobbles near the
3 dB threshold, which is why it moves the count without moving the movement.

The more important half. Our share of ReBirth's gesture COUNT collapses as the
threshold rises:

| our count as a share of ReBirth's | thresh 3 | 6 | 9 | 12 |
|---|---|---|---|---|
| 303-2 | 75% | 64% | 49% | **44%** |
| 303-1 | 51% | 52% | 46% | **23%** |

ReBirth keeps 48 of its 64 gestures above 12 dB on 303-2. We keep 21 of 48.

**So this is not "a quarter of the gestures are missing and the survivors are 70
percent as deep", which reads as two comparable halves. The rate half is largely
an artefact of counting shallow wobbles at a 3 dB threshold. The defect is that
ReBirth's filter makes big movements and ours makes small ones**, and the deficit
concentrates in exactly the gestures that are loud enough to hear.

That also explains why the two sequencer probes cannot help by construction:
they change WHICH steps retrigger, and the deficit is in HOW FAR the filter
travels when it does.

### A useful decomposition the single-threshold number hid

At threshold 3 the two voices look like the same problem. They are not.

    303-2   count 75% of ReBirth, depth 73%   both halves broken
    303-1   count 51% of ReBirth, depth 108%  our gestures are DEEPER, and far rarer

On 303-1 our median gesture is already deeper than ReBirth's. Whatever is wrong
there is purely rate, and it is a much larger gap than 303-2 in total movement,
28 percent against 50. A fix aimed at depth alone will not close 303-1, and a
fix aimed at rate alone will not close 303-2. **Score any candidate on both, and
on total movement rather than count.**

### Method note

The threshold is the discriminator and it should have been swept from the start.
A gesture count at one threshold cannot separate "added a shallow wobble" from
"deepened a real movement", and those have opposite implications for what to fix
next. This is the same shape as the summary-statistic trap in `RUNBOOK.md`
section 7: one number could not distinguish two mechanisms, and sweeping the
parameter the number depends on separated them immediately.

---

## 2026-08-03: RETRACTED, the env-mod-0 depth shortfall. It was the instrument

`PLAN_303_ENVELOPE.md` ranked "the env-mod-0 depth is 0.42 octaves short" as the
second-largest remaining 303 error: 0.54 octaves measured against ReBirth's 0.96
on the envfloor tied cell, even though `EM_FLOOR` ships at 0.85. **It does not
reproduce, and the arithmetic says which instrument was wrong.**

### The known-truth check that settles it

At env mod 0 the code gives `envDepthOct = EM_FLOOR` exactly, and with
`EM_CENTRE` at 0.5 the swing is symmetric about the cutoff-knob corner. **So our
commanded travel at env mod 0 is exactly 0.85 octaves, by construction, and the
true onset must be `floor * 2^0.85`.** That is a truth we set, so it calibrates
the instrument rather than being measured by it.

On the exact cell the shortfall was recorded from, `g303_c064_em000_dec096_r060`:

| | our onset |
|---|---|
| true, from the commanded law | 854 Hz |
| `env_traj.py` | 876 Hz, **+0.04 oct** |
| the instrument behind the 0.54 figure | 690 Hz, **-0.31 oct** |

Both instruments agree on the FLOOR to within a hertz (473 against 474). They
disagree only at the ONSET, and only the 0.54 figure is inconsistent with a
travel we set ourselves. Across all four em000 cells `env_traj` recovers the
known onset to +0.04, -0.04, +0.04 and -0.14 octaves.

### What the travel actually is

`env_traj.py` on the four em000 cells, fresh renders at current defaults:

| cell | ReBirth | ours | gap |
|---|---|---|---|
| c040 dec096 r060 | 0.76 | 0.71 | 0.05 |
| c064 dec096 r060 | 0.90 | 0.89 | 0.02 |
| c064 dec096 r090 | 0.83 | 0.81 | 0.01 |
| c064 dec127 r060 | 0.90 | 0.89 | 0.02 |

**Our env-mod-0 depth is within 0.05 octaves of ReBirth's on every cell, not
0.42 short.** `EM_FLOOR` 0.85 is delivering very close to its nominal depth and
ReBirth's own depth is close to the same number, so the fitted F was right.

### What is really there, and it is already on the list

A near-pure OFFSET. Ours sits below ReBirth at BOTH endpoints by a consistent
amount:

| cell | onset | floor |
|---|---|---|
| c040 dec096 r060 | 0.18 | 0.13 |
| c064 dec096 r060 | 0.18 | 0.16 |
| c064 dec096 r090 | 0.11 | 0.10 |
| c064 dec127 r060 | 0.18 | 0.16 |

Equal at both ends is the signature of a static corner error, not an envelope
error. **That is plan item 3, `CUT_SCALE` 0.78, and the code comment at the
`EM_CENTRE` site already said so**: "what is left after it is a near-pure OFFSET
... which is a static corner error and belongs to `CUT_SCALE` rather than to
this". So items 2 and 3 were one defect counted twice, and only item 3 is real.

### Why the instrument gain warning was itself misleading

The plan said to "apply `env_traj.py`'s travel-gain calibration before refitting
F", citing a gain of 0.47 at 0.55 octaves commanded. Swept properly by moving
`EM_FLOOR` at env mod 0, where commanded travel is known exactly:

| commanded | 0.40 | 0.60 | 0.85 | 1.10 | 1.40 |
|---|---|---|---|---|---|
| measured | 0.22 | 0.33 | 0.89 | 1.08 | 1.30 |
| gain | 0.55 | 0.55 | **1.05** | 0.98 | 0.93 |

The gain IS bad below about 0.6 octaves of travel and it is fine from 0.85 up.
**The operating point that matters sits in the good region**, so the warning was
real but did not apply where it was being applied. Do not use `env_traj` travel
below about 0.7 octaves; above that it is accurate to about 5 percent.

### The general lesson, and it is the third instance this week

A quantity we COMMAND is a free calibration of any instrument that measures it,
and it costs one render. The 0.54 figure survived because nobody asked the
instrument to report a number whose answer was already known. The same omission
produced the retracted skirt findings (`find_f0` off by half a bin, never
checked against a known slope) and the false noise floor (a single pair drawn
from a distribution containing zeros).

`RUNBOOK.md` section 7 already says "calibrate an estimator against a known value
before trusting its absolute output" and "when you build a new estimator, build
the thing that checks it in the same sitting". This is what it costs when the
rule is written down and not applied: item 2 of the plan, ranked second, was an
artefact for a day.

---

## 2026-08-03: what `CUT_SCALE` 0.78 cancels, and a flat HF shelf nothing touches

Plan item 3, and the top live item once item 2 was retracted. The corner
instrument says 0.78 puts our corner 0.10 to 0.18 octaves LOW, yet every
aggregate spectral measure prefers it. An aggregate rms says "better" without
saying WHERE, which is exactly how one error pays for another, so this splits
the residual by frequency. `tools/analyze/cutscale_where.py`.

Third-octave residual, ours minus ReBirth, each render normalised to its own
total first so this is SHAPE. Cell `g303_c064_em000_dec096_r060`, whose corner
sweeps 992 to 531 Hz on ReBirth.

| Hz | `CUT_SCALE` 0.78 | 1.00 |
|---|---|---|
| 640 | -1.28 | +1.60 |
| 1016 | -1.61 | +4.21 |
| 1613 | +0.75 | +7.36 |
| 2560 | +0.88 | **+8.10** |
| 4064 | -0.58 | +6.15 |
| 6451 | **+3.80** | +4.62 |
| 8127 | **+4.61** | +4.75 |
| 10240 | **+5.69** | +6.45 |

### Two separate defects, and only one of them is what 0.78 is paying for

**What 0.78 cancels is a mid-band excess**, 640 Hz to 5 kHz, peaking at +8.1 dB
about two octaves above the corner. Darkening everything by 0.22 octaves flattens
it, at the price of putting the corner in the wrong place. That is the
compensation.

**What 0.78 does NOT cancel is a flat shelf above 6 kHz of +3.7 to +6.5 dB**, and
this is the more useful half. It is present at every configuration tried:

| above 6 kHz | 0.78 | 1.00 | `K_B` 1.5 | `RESO_OCT` 0 | drive 0.5 |
|---|---|---|---|---|---|
| 6451 | 3.80 | 4.62 | 4.59 | 3.75 | 8.33 |
| 8127 | 4.61 | 4.75 | 5.05 | 4.74 | 9.17 |
| 10240 | 5.69 | 6.45 | 5.92 | 6.39 | 12.18 |

**No filter constant moves it.** Cutoff scale, resonance feedback, the
resonance-to-corner coupling and the drive were all swept and it sits at +4 to
+6 dB throughout. That is task 40's flat HF shelf, now quantified and shown to be
outside the filter's constants. It cannot be fitted away and it should stop being
counted against any filter fit.

### Two candidates eliminated, with numbers

**The decay time is NOT the cause.** Our tau is 0.84 s against ReBirth's 0.64, so
we linger longer at the bright end and a whole-file spectrum should show excess
HF. Plausible, and wrong: `ENV_DEC_MUL` 0.60 moves the 2560 Hz excess only 8.10
to 6.79 and the total rms 4.54 to 3.89. It recovers about a sixth of the error.

**The drive shaper is NOT the cause, and lowering it is strongly harmful here.**
`REBORN_DRIVE` 1.0 to 0.1 takes the total rms 4.54 to **11.06** and the 6451 Hz
band 4.62 to 21.86. The direction is the opposite of what a waveshaper's added
harmonics would predict, and the reason is already in the code: the input and the
resonance feedback compete inside the same saturating stack, so a hot oscillator
DUCKS the resonance. Cutting the drive un-ducks it and the filter rings harder.
**This is independent evidence against the drive-removal case**, from a direction
that has nothing to do with the ear check or with the retracted linearity fit.

### What is left, partially

The resonance-to-corner coupling is the best single explanation of the mid-band
bump: `RESO_OCT` and `RESO_OCT_CUT` at zero take the rms from 4.54 to 2.65 and
halve the 2560 Hz excess, 8.10 to 4.10. Reducing the feedback helps less, `K_B`
1.5 giving 3.44. Neither closes it and both would move the corner, which is the
quantity the fit was anchored on, so **this is not a licence to zero them**. It
does say the coupling is where the remaining mid-band error lives.

### Status of `CUT_SCALE`

Unchanged, still shipping at 0.78, still flagged. The question "what is it
cancelling" now has a two-part answer: a mid-band excess that belongs to the
resonance coupling, and a flat HF shelf that belongs to task 40 and to neither.
Correcting the coupling is what would let the corner go back where the corner
instrument wants it.

### Same day, continued: the corner wants 0.86, the shape wants 0.78, and the shelf is NOT why

Having located what `CUT_SCALE` cancels, the obvious next step was to fit the
corner directly, since `harm_response.py` and `env_traj.py` both measure the peak
and are immune to the HF shelf.

**Both corner instruments agree, and they agree on a value that is not 0.78.**
On batch H, peak-frequency error by `CUT_SCALE`: 0.1653 at 0.78, 0.1463 at 0.84,
0.1460 at 0.90, 0.2646 at 1.00. And the shipped corner error is a near-flat
OFFSET, -0.06 to -0.18 octaves across the cutoff knob, not a tilt. An offset of
about 0.14 octaves implies 0.78 * 2^0.14 = 0.86, which lands inside batch H's
own optimum.

Verified independently on the tied envfloor cells, corner offset in octaves,
ours below ReBirth:

| cell | 0.78 onset / floor | 0.86 onset / floor |
|---|---|---|
| c040 em000 | 0.18 / 0.13 | 0.08 / -0.02 |
| c064 em000 r060 | 0.18 / 0.16 | 0.08 / 0.06 |
| c064 em000 r090 | 0.11 / 0.10 | 0.00 / -0.03 |
| c064 em000 dec127 | 0.18 / 0.16 | 0.09 / 0.05 |
| **c090 em000** | **0.52 / 0.54** | **0.09 / 0.06** |

Mean absolute offset **0.226 to 0.056 octaves**, a fourfold improvement, and the
cutoff-90 cell falls from over half an octave to under a tenth.

### And it still loses on the aggregate, so the tension is real

`regress_ab.py` over all 373 cells: **2.325 to 2.585, worse**, 105 cells better
and 265 worse. The split is informative rather than uniform. The sets where the
corner is the thing being measured improve (envfloor -0.265, envdomain -0.449,
accentdepth -0.517, batchG -0.176) and the broad spectral sets worsen (batchQ
+0.831, batchL +0.771, batchL2 +0.600, batchN +0.581, batchI +0.558).

**The hypothesis that the HF shelf explains this is REFUTED.** If the aggregate
were choosing a darker corner to hide an unfittable shelf, excluding the shelf
should flip the verdict. It does not. Batch H third-octave rms, 0.78 against
0.86, at four band limits:

| band limit | 0.78 | 0.86 |
|---|---|---|
| 12 kHz | 1.723 | 2.226 |
| 8 kHz | 1.608 | 2.178 |
| 5 kHz | 1.543 | 2.064 |
| 4 kHz | 1.487 | 1.993 |

0.78 wins at every limit, by a nearly constant margin, with the shelf entirely
excluded. So the shelf is a real and separate defect, and it is NOT what makes
the aggregate prefer a low corner.

### What that leaves, stated plainly

Two instruments that should agree do not, and neither is now suspect: the corner
measurement is calibrated against a commanded travel, and the aggregate survives
removing the one contaminant that could have biased it.

**Our filter can have ReBirth's corner or ReBirth's spectral shape, not both.**
That is a statement about the shape of the response curve rather than about any
constant in it, and it is the same conclusion the peak-versus-aggregate conflict
reached from the other side. It is NOT the retracted topology claim: ReBirth is a
four-pole ladder and that stands. It is narrower, that our four-pole with its
current resonance coupling cannot put the peak where ReBirth puts it while also
distributing energy the way ReBirth does.

`CUT_SCALE` stays at 0.78. Do not ship 0.86 on the corner evidence alone: it is
better on the quantity it was fitted to and worse on 265 of 373 cells, which is
the trade this project has repeatedly refused. What has changed is that the
tension is now measured from both sides and the cheap explanation is dead.

**The next experiment**, and it is one experiment rather than a fit: hold the
corner at 0.86 and ask what single additional change recovers the aggregate. The
resonance-to-corner coupling is the leading candidate on the earlier evidence
(zeroing it took the envfloor mid-band rms 4.54 to 2.65), and it is exactly the
term that sets peak position against energy distribution.

### Same day, the answer: our resonant peak is too SHORT, and no constant fixes all three

Holding the corner at 0.86 and asking what single change recovers the aggregate.
The resonance-to-corner coupling was the leading candidate and it is **refuted**:
lowering `RESO_OCT` makes the corner WORSE at both cutoff scales (0.1653 to
0.2315 to 0.2865 at 0.78; 0.1047 to 0.1358 to 0.1643 at 0.86), so 0.25 is right
for the corner and the coupling is not the recovery term.

**What is actually wrong is peak PROMINENCE, and it is the opposite of what a
too-broad filter would give.** Our peak stands too LOW above the passband, and
the shortfall grows with both resonance and cutoff:

| prominence dB, ReBirth / ours | reso 90 | reso 110 | reso 127 |
|---|---|---|---|
| cutoff 40 | 8.6 / 7.3 | 12.7 / 10.6 | 16.1 / 13.1 |
| cutoff 90 | 11.6 / 7.7 | 16.1 / 10.4 | 19.4 / 14.6 |
| cutoff 127 | 12.8 / 8.1 | 18.3 / 11.1 | **20.8 / 14.4** |

Up to **6.4 dB short**. At low resonance we match or slightly exceed. So ReBirth
concentrates its energy into a taller, narrower resonant peak and we spread ours
out. That is why raising the corner to the right place makes the aggregate worse:
with the corner correct, our excess off-peak energy has nowhere to hide, and
dragging the corner down with `CUT_SCALE` 0.78 slides our broad hump back over
ReBirth's.

### `hfQ` refuted, and the feedback confirmed as the mechanism

`hfQ` was the obvious suspect since it damps Q above 4 kHz and the shortfall
grows with cutoff. **It is not the cause**: disabling it entirely
(`HFQ_ON` 20000) moves prominence only 3.74 to 3.57 and leaves the shape
unchanged to three decimals. It does not engage on this grid, which is the same
conclusion an earlier round reached and which now holds at the current corner
too. `KHI_CEIL` 4.0 on top of it changes nothing further.

The feedback is the mechanism. `K_B` 3.40 to 3.90 takes prominence **3.74 to
1.87**, halving it.

### And that is where it stops, because the three instruments do not co-operate

| config | corner oct | prominence dB | shape dB |
|---|---|---|---|
| shipped, 0.78 | 0.1653 | 3.73 | **1.723** |
| 0.86 | **0.1047** | 3.74 | 2.226 |
| 0.86, hfQ off | 0.1067 | 3.57 | 2.226 |
| 0.86, `K_B` 3.9 | 0.1486 | **1.87** | 2.639 |

**No setting of the existing constants is best on more than one.** `CUT_SCALE`
trades corner against shape. `K_B` buys prominence and pays in both. `RESO_OCT`
worsens the corner in either direction from 0.25. And `K_B` is already at 3.90
against a self-oscillation limit of 4.0, so the prominence cannot be bought much
further by feedback at all.

**The conclusion is about the parameterisation, not the fit.** Our four-pole
ladder, with the Q mechanism it currently has, cannot simultaneously place the
peak where ReBirth places it, make it as tall as ReBirth makes it, and
distribute the remaining energy as ReBirth does. Something has to supply Q
without costing corner position, and no constant in the current model does that.

This is NOT the retracted topology claim, which said the filter ORDER was wrong
and rested on a slope figure that did not exist. ReBirth remains a four-pole
ladder. This is narrower and better evidenced: the ORDER is right and the
DAMPING is wrong, in a way that a scalar feedback term cannot express because it
is bounded by self-oscillation before it delivers enough peak.

### Why this is probably what Luke has been hearing

He has described a missing "snap" and a missing onset "chirp" since the first ear
check, and the chirp is recorded as absent on exactly the two patches he cannot
tell from ReBirth. A resonant peak 5 to 6 dB short at high resonance and high
cutoff is precisely the audible signature of both: the squelch of a 303 is that
peak. This is the first measurement that predicts his subjective report from a
number rather than the other way round, and it should be checked by ear before it
is treated as settled.

Nothing shipped. `CUT_SCALE` remains 0.78, `K_B` 3.40, `hfQ` on.

---

## 2026-08-03, ear check: the accent is up to 29 dB too bright, and Luke found it

The trio was staged to test whether raising `K_B` closes the resonant-peak
shortfall. **It answered a different and much larger question**, because Luke
reported that on all of it he could hear nothing on the accented notes "beyond
that hard accented note around the 12th step", and that it was much more
pronounced than in previous ear-check renders.

That is a direct read of a number. Excess in the 1.2 to 5 kHz band, ours minus
ReBirth, on Clodhopper 303-2 at 140 BPM where a 16th step is 107 ms:

| step | 0 | 4 | 8 | 11 | **12** | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|
| bar 1 | +4.6 | +5.2 | +14.8 | +6.9 | **+29.2** | +18.0 | +17.9 | +5.1 |
| bar 2 | +4.9 | +4.1 | +10.4 | +27.8 | **+17.9** | +18.1 | +18.4 | +16.6 |

Median across all frames +5.4 dB, maximum **+29.4 dB**, and 7 percent of frames
above +20 dB. **He named step 12 without being told what to listen for, and step
12 is the single hottest frame in the file.** Third time his ear has read a
physical quantity to inside the instrument's tolerance, after the corner interval
of three semitones against 3.39 and the settle time of "about 1.5 seconds"
against 1.48.

### What this changes

The whole-patch numbers hid it. Our render is +7.06 dB over ReBirth at 80 to
400 Hz and **+25.50 dB at 1.2 to 5 kHz**, and a shape error of 14.31 dB in the
800 Hz to 5 kHz band. Those aggregates read as a broad brightness problem. Sliced
per step they are nothing of the kind: **most steps sit near +5 dB and a handful
of accented ones spike to +18 to +29.** The defect is the ACCENT, not the filter's
general brightness.

That is a much larger error than anything measured on the static fixtures this
session, and it is on the patch that gets listened to. It also explains the
listening result directly: level-matching a file whose loudest event is 29 dB hot
turns everything else down until only that event is audible, which is exactly what
he described.

### Consequences for the round

**The `K_B` question was not answered and the trio should not be re-used.** With
one event 29 dB out, a 5 to 6 dB prominence difference is not audible on this
patch. The prominence finding stands as measurement; its ear check needs a patch
without a dominant accent, or the accent fixed first.

**This outranks plan items 1 and 3.** The gesture work counted upward brightness
gestures and found ours shallower on average; this says our accented gestures are
enormously DEEPER than ReBirth's while the rest are mildly hot. Those are
consistent only if ReBirth's accent is far gentler than ours, which is the
opposite of the direction item 4 assumed when it recorded that "ReBirth reaches 41
percent of its brightness travel by knob 16 where we reach 8".

Suspects, in order, all in the accent path and all currently shipping:
`ACC_SWEEP` at 0.80, raised from 0.35 during the envelope round; `ACC_RESO` at
0.1, which adds resonance on top of the sweep; and `ACC_ATK` at 0.010. None has
been measured against ReBirth per STEP, only in aggregate.

### Method note

**Slice by step before trusting a whole-file spectrum on a musical patch.** The
aggregate said "broadly too bright" and the per-step view said "five steps are
catastrophically too bright and the rest are nearly right". Those call for
completely different fixes. This is the same failure mode as the gesture count at
a single threshold, one file earlier in this document, and the same as the
summary-statistic trap in `RUNBOOK.md` section 7.

### Same day: `ACC_SWEEP` 0.80 owns 11 dB of the spike, and 18 dB is something else

Sweeping the accent terms against the per-step excess in 1.2 to 5 kHz on
Clodhopper 303-2, ours minus ReBirth:

| config | median | p90 | max |
|---|---|---|---|
| shipped | 5.4 | 18.4 | **29.4** |
| `ACC_SWEEP` 0.35, its pre-envelope-round value | 5.4 | 17.7 | **19.0** |
| `ACC_SWEEP` 0 | 5.0 | 17.5 | 18.1 |
| `ACC_RESO` 0 | 5.4 | 18.4 | **28.7** |
| both zero | 5.0 | 17.5 | 18.1 |
| `ACC_LVL` 0 | 5.4 | 15.8 | 20.7 |

**`ACC_SWEEP` is the dominant term**: 0.80 back to 0.35 takes the peak from 29.4
to 19.0, so it owns about 11 dB of the spike on its own. **`ACC_RESO` is
innocent**, moving the peak 0.7 dB, and should not be touched on this evidence.

**But zeroing the accent entirely still leaves +18.1 dB at the peak and +17.5 at
p90, with about a tenth of all frames above +17 dB.** So roughly 18 of the 29 dB
is NOT the accent. Something else makes specific steps far too bright and it is
currently unidentified. Do not treat reverting `ACC_SWEEP` as a fix for what Luke
heard; it is a fix for a third of it.

### How a value that was fitted on measurement got this wrong

`ACC_SWEEP` 0.80 was applied on 2026-08-02 with `ACC_ATK`, on the grounds that
together they "took in-gate trajectory error from 3.722 dB to 0.924, improving 33
cells and worsening none". That is a real improvement on a real metric, and the
metric could not see this: it scores the corner's trajectory WITHIN the gate on
fixture cells, averaged, and it has no notion of how much 1.2 to 5 kHz energy one
accented step puts out relative to its neighbours on a musical pattern.

This is the same shape as the two other traps found today, and it is worth
stating as a rule rather than a third anecdote. **A constant fitted on an
averaged metric can be badly wrong on the extremes that metric averages over,
and the accent is by construction an extreme.** Any accent constant should be
scored per step against ReBirth, on a pattern that actually uses accents, before
it ships.

### Status

Nothing changed. `ACC_SWEEP` still ships at 0.80. Reverting it to 0.35 is not
proposed yet, because it was applied on evidence that has not been re-examined
and because two thirds of the defect lies elsewhere. The next question is what
the residual +18 dB is: it is present with every accent term zeroed, so it is
the envelope, the sequencer, or a note-dependent effect, and identifying which
comes before any constant moves.

### Same day, and it reframes the round: Clodhopper runs at ENV MOD ZERO

Chasing the residual, `REBORN_EM_OCT` was swept from 4.45 to 2.2 to 0 on
Clodhopper 303-2 and the per-step excess did not move by a single decimal:
median 5.0, p90 17.5, max 18.1 at every value. Read from the fixture bytes,
that is exactly right:

    303-1: tune=127 cutoff=45 reso=125 ENVMOD=0 decay=125 accent=96
    303-2: tune=65  cutoff=29 reso=127 ENVMOD=0 decay=115 accent=96

**Both 303 voices on Clodhopper have the env mod knob at ZERO.** They are
high-resonance (125 and 127), high-accent (96), low-cutoff (45 and 29) patches
with no env mod at all.

### What that means for the envelope round

`EM_OCT` 4.45 is the headline constant of the 2026-08-02 envelope round, fitted
as the `A` term of the bipolar swing law. **It has no effect whatsoever on
Clodhopper**, and Clodhopper is the patch nearly every ear check has been run on
and the one Luke's "modulating on every beat" report came from.

So the envelope-knob law can be neither confirmed nor refuted by any Clodhopper
listening, and the filter movement Luke hears ReBirth make on it comes from the
env-mod-ZERO path: `EM_FLOOR` at 0.85, plus the accent, into a filter at
resonance 127. The gesture gap of 48 against ReBirth's 64 was measured under
those conditions too.

This does not retract anything. The bipolar law was fitted on envfloor and
envdomain fixtures that do sweep the knob, and it stands on those. What it means
is that **the constants that matter for the patch under the microscope are
`EM_FLOOR`, the accent terms, `K_B` and `CUT_SCALE`, and not `EM_OCT` or
`EM_CENTRE`.** Two of those, `EM_FLOOR` and `ACC_SWEEP`, were fitted during the
envelope round on metrics that average over exactly the extremes this patch
consists of.

### The residual, characterised but not identified

With the accent zeroed, what still moves the excess:

| | median | p90 | max |
|---|---|---|---|
| accent off | 5.0 | 17.5 | 18.1 |
| `EM_OCT` 0 | 5.0 | 17.5 | 18.1 |
| `EM_FLOOR` 0 | -0.6 | 14.9 | 16.7 |
| `K_B` 2.4 | 3.5 | 15.6 | 16.4 |
| `CUT_SCALE` 0.55 | **-4.3** | **7.2** | **8.5** |

`EM_FLOOR` accounts for most of the MEDIAN offset, the resonance for a little,
and only a large darkening reaches the peaks. That `CUT_SCALE` 0.55 is needed to
tame it, against 0.78 shipping and 0.86 from the corner instrument, says the
patch wants a filter far darker than either fixture-derived value. **A third
disagreement, and this one on the patch that gets listened to.**

Not resolved. The most likely explanations to separate next, and they are
cheaply separable: that resonance 127 with our Q law is simply wrong in a way
the batch H grid understates because batch H's own resonance-127 column is one
of six rather than the whole patch; or that something in the accent-plus-tie
interaction on this specific pattern retriggers where ReBirth does not, which
is the class of defect that has already shipped three times here and which
`onset_ab.py` exists to catch.

---

## 2026-08-03: we retrigger on 55 of 110 steps where ReBirth holds

`onset_ab.py` on Clodhopper 303-2 against ReBirth's own export, 140 BPM:

    retrigger strength dB, high = the gate closed and reopened
    mean ReBirth 34.6, ours 39.3

    55 of 110 steps differ by 4 dB or more
    ours retriggers where ReBirth holds: 55
    ReBirth retriggers where ours holds:  0

**Half the steps, and the asymmetry is total.** There is not one step in seven
bars where ReBirth articulates and we do not. The pattern is identical in every
bar, on steps 2, 4, 6, 14 and 16, with the largest differences at step 2
(+15.6 dB) and step 16 (+10.8).

Neither existing sequencer probe addresses it and both make it marginally worse:
`SLIDE_NEEDS_VOICE` gives 57 and `REST_ACCENT` 59, against 55 shipped. So this is
a new defect of the family the runbook already names as the most expensive one
here, where three have shipped and no metric other than this one caught any.

### It explains all three of the day's Clodhopper observations at once

This is the reason to believe it rather than the measurement itself.

**The gesture deficit inverts into a consequence.** We count 48 upward brightness
gestures against ReBirth's 64, which reads as "our filter moves less". But a
filter that re-fires its envelope on steps where ReBirth holds never lets
brightness fall between those notes, so the excursions merge into a wash and
FEWER distinct upward gestures are counted. Retriggering more produces fewer
gestures, not more.

**The +5 dB median excess** in 1.2 to 5 kHz follows directly: an envelope
restarted twice as often spends far more time near its peak.

**The +29 dB spikes** are where a spurious retrigger lands on an accented step,
which on this patch is most of them, since accent is 96 on both voices.

One defect, three symptoms, and the two that were being chased as separate
problems are downstream of the third.

### What it does not explain, and the caution that goes with it

It does not explain the resonant peak being 5 to 6 dB short on the static batch H
grid, which has no sequencing in it at all. That remains a separate finding.

And this must not become the explanation for everything by default. What is
established is the retrigger count and its one-directionality. **The mechanism is
not identified**: it could be the gate length, the tie or slide decision, the
accent row being read on a step that should hold, or the envelope's re-fire
condition. `step_events.h` carries the decision table and its measurements and is
where to start, since it exists precisely because three defects of this kind
lived in four inline lines.

### Why it was not found sooner

Every metric in the project except `onset_ab.py` averages over time. A spurious
retrigger is a timing event; it shows up in a spectrum only as slightly more
brightness, which is exactly how it has been read all week, as a filter that is
too bright and needs darkening. **`CUT_SCALE` 0.55 was needed to tame this patch
against 0.78 shipping, and that is what fitting a spectral constant to hide a
rhythmic defect looks like.** The third disagreement about `CUT_SCALE` recorded
earlier today is very likely this.

### Research, 2026-08-03: what the hardware does, and where our retrigger defect must live

Published sources only, per the clean-room rule. No Open303 source was read; the
constraint is recorded in `docs/research/reborn338_HANDOFF_2026-07-23.md` and Luke
reaffirmed it when asking for this.

**The 303 has TWO envelope generators, not one.** A Main Envelope Generator
(MEG) drives the filter cutoff and a separate VCA Envelope Generator (VEG)
drives the amplifier. Our code already reflects this split (`mainEnv_` and
`ampEnv_`), which is worth noting because a single-envelope model would make the
accent findings unexplainable.

**Gate length is the key architectural fact.** A normal 16th-note gate "extends
less than a full step's worth of clock pulses, specifically three pulses and the
positive portion of the fourth", which is roughly 0.81 of a step and is
described as the source of "the 303's staccato note feel". **Extended gates,
used by slides and ties, "extend through the step on which they are placed and
into the next step"**, so the envelope is not retriggered across a slide.

**Accent** shortens the MEG decay, raises the cutoff and lifts the VCA, and does
so through a "sweep circuit that smooths the control voltage driving the filter
cutoff". Published descriptions say this ACCUMULATES across consecutive accented
notes, giving a rising floor.

### Where that leaves our 55 spurious retriggers

It rules out the obvious explanation. **Our slide handling is already correct**:
`trigger()` does not reset `mainEnv_` when `slide` is true and says so in the
comment. So we are not retriggering the filter envelope across slides.

It also does not implicate the gate fraction, despite the hardware's 0.81 sitting
well above our 0.500. That 0.500 is not a guess: it was measured against ReBirth
at five tempos from 60 to 180 BPM and cross-checked against our own known-value
render to remove a -0.028 estimator bias. **ReBirth departs from the hardware
here and ReBirth is the target**, which is the fourth control where that has been
true after the glide, the leaky VCA and the tune table.

**So the defect is in WHICH steps are treated as tied, not in what happens once
one is.** `onset_ab.py` says ReBirth holds the gate across 55 steps where we
close it, one-directionally. If ReBirth ties on steps our decision does not, we
close a gate it keeps open, and every one of those closures is a retrigger it
does not perform. That is precisely the family `step_events.h` exists for: three
defects have already shipped in that decision and all three were found by ear.

Next step is to compare the tie decision step by step rather than the audio:
`decideBassStep` against the pattern bytes, on the steps `onset_ab.py` names
(2, 4, 6, 14, 16 in every bar), and ask what ReBirth is doing there that we are
not.

**A conflict worth recording rather than resolving.** Published sources say the
hardware accent accumulates across consecutive accents. Our own measurement of
ReBirth says it does not: four consecutive accents stay within 0.23 dB, with a
slight droop rather than a build. Both can be true, since ReBirth is an
emulation and an imperfect one, and the project rule is that ReBirth wins. Noted
so the hardware description is not later mistaken for a target.

### The tie decision, read: where the 55 retriggers are NOT coming from

`decideBassStep` in `include/reborn/step_events.h` was read against the defect.
Three things it already does correctly, so they are eliminated:

**Slides tie.** `ev.tie = st.slide`, and `setStepTiming` gives a tied step a gate
of 1.5 steps against 0.5 for a struck one, so a slid step holds past its own end
and the next step picks the gate up unbroken. This matches the hardware's
extended gate.

**A tied-into rest is not a note-off.** The `Hold` branch exists and carries the
measurement that produced it, on Clodhopper's 303a steps 2, 5 and 7.

**The filter envelope does not retrigger on a slide.** `trigger()` leaves
`mainEnv_` alone when `slide` is true.

So the mechanism is none of the three defects already found in this decision. It
is something else on steps 2, 4, 6, 14 and 16 of Clodhopper 303-2, where ReBirth
shows a retrigger strength of 9 to 31 dB (holding) against our 15 to 34.

**What this needs, and it is a small tool rather than more reading.** Every check
so far has inferred the decision from audio. What is missing is a dump of what
`decideBassStep` actually returns per step for a real song, printed next to
`onset_ab.py`'s per-step verdict. `decideBassSteps` already exists and is
designed for exactly this, being the form the tests use because "every defect in
this code was a SEQUENCE property". Nobody has pointed it at Clodhopper and
compared row by row.

That is the next step, and it should come before any constant moves: the three
prior defects in this decision were all found by ear and none was visible in a
spectrum, so guessing at a fourth from spectra would be repeating the mistake.

---

## 2026-08-03: the tie decision itself is right. The 55 spurious retriggers are NINE separate boundary shapes, not one bug

Task 75's question: is `decideBassStep` making the wrong call on the 55 of 110
Clodhopper steps where `onset_ab.py` says we retrigger and ReBirth holds.

### The trace, at the real call site

`REBORN_TRACE_BASS=1|2|3` (bass1, bass2, or both), printed to stderr from inside
`fireBass()` in `engine.cpp`, so it cannot diverge from what the render actually
did: it prints the pattern's raw flags, `decideBassStep`'s verdict, and the
global step index in the same numbering `onset_ab.py` reports, at the exact call
site that drives playback. Default off, verified byte-identical over the 32
demo-song renders.

Joined against a fresh `onset_ab.py` run (111 steps, 55 disagreeing, reproduced
exactly), every one of the 55 was traced back to its decided event.

### The tie decision is NOT the mechanism

Every disagreeing step's flags were checked against `decideBassStep`'s own logic
by hand as well as by trace, and every verdict is what the code should produce
given those flags: `ev.tie = st.slide` is applied consistently, Holds fire
exactly where the previous step's slide is true and this step has no gate,
Strikes fire exactly where this step gates. **The sequencer boundary is not
where this defect lives.** That refutes the working theory from the research
pass; it does not refute the research itself, which correctly narrowed the
search to this decision and away from gate length and the accent circuit.

### It resolves into nine recurring boundary shapes, by position in the bar

| step in bar | n | diff range | decided | notable |
|---|---|---|---|---|
| 2 | 7 | 15.2-15.6 dB | Strike, slideIn=1 | correlates 0.87 with pitch-jump size |
| 4 | 7 | 5.6-8.1 dB | Strike, slideIn=1 | follows an accented Hold |
| 6 | 7 | 6.0-6.1 dB | Hold | accentLatched_ verified false here |
| 7 | 5 | 4.2-6.6 dB | Strike, tie=1 | |
| 10 | 7 | 6.2-6.3 dB | Strike, slideIn=1, accent=1 | |
| 12 | 2 | 4.5-5.5 dB | Release, accent=1 | only bars 6-7 |
| 13 | 7 | 6.8-7.1 dB | Strike, tie=1, accent=1 | |
| 14 | 7 | 4.5-5.6 dB | Strike, slideIn=1, accent=1 | |
| 16 | 6 | 10.7-10.8 dB | Strike, slideIn=1 | second largest |

Every disagreement is on a glide-in Strike, a Hold, or a Release; not one is a
plain fresh unaccented pluck, which is consistent with the earlier finding that
the deficit is concentrated where the filter is asked to CONTINUE rather than
restart.

### Bucket 2, the largest, correlates with pitch jump and is not accent-related

Fitting |semitone jump during the glide| against the diff over all 34 glide-in
Strikes: **correlation 0.87**. The seven occurrences of a 12-semitone jump (an
octave, this pattern's step 1 to step 2) average 15.4 dB; the 27 zero-jump
glide-ins average 6.6 dB. Both raw accent flags are 0 at this boundary, so this
is not the accent leaking; it is either a genuine acoustic effect of the glide
sweeping the oscillator's harmonics across a fixed resonant peak, or a defect in
how the pitch glide itself is executed. Not chased further today: it needs a
controlled glide fixture (jump size swept in isolation), not more Clodhopper.

### Bucket 4 tested a real mechanism and REFUTED the naive fix

`accentLatched_` (`bass_synth.cpp`) is a one-way latch: a struck unaccented note
clears it, but a slide or Hold with no accent of its own leaves it exactly as it
was. It gates a sustained VCA level term and a faster filter-decay time constant.
Bucket 4's steps directly follow an accented Hold (step 3 of the bar), so the
latch is genuinely true going into them and genuinely wrong to still be true,
since the step itself carries no accent.

`REBORN_ACC_LATCH_EXPIRE`, new probe, clears the latch on the first unaccented
slide or Hold instead of leaving it. Verified byte-identical at its default.
Tested on Clodhopper 303-2:

| | shipped | +LATCH_EXPIRE |
|---|---|---|
| steps disagreeing | 55 | **64** |
| bucket-4 gap (7 occurrences) | 5.6-8.1 dB | 4.5-6.3 dB, two dropped below threshold |
| steps newly disagreeing | | **11**, all step-in-bar 1 or 5 |

**Net worse.** The mechanism is real, confirmed by the partial local improvement,
but clearing it as a hard binary event moves the error to the NEXT boundary
rather than removing it: the filter decays at the wrong rate one step sooner,
which throws off the step that follows. This is the same shape of mistake as
`ACC_SWEEP` and the rejected quadratic decay law: a quantity that actually
decays continuously (`accEnv_` already does, on a ~65 ms time constant) was
being gated by a binary flag with no decay of its own. The fix, if there is one,
is probably to drop the flat `ACC_SUS` floor term entirely and let `accEnv_`
alone carry the sustain, rather than adding a second expiry rule to the latch.
Not attempted today; the probe stays as a documented negative result and a
correctly-shaped next attempt should score bucket 4 in isolation before
touching the shipped path.

### Buckets 6, 7, 10, 12, 13, 14 remain uncharacterised

Bucket 6 is confirmed NOT the accent latch (traced false there). None of the
other five were investigated past the table above. All are open.

### Status

Nothing shipped. Both new tools are default-off and byte-identical: the trace
(`REBORN_TRACE_BASS`) and the probe (`REBORN_ACC_LATCH_EXPIRE`, tested and
refuted as a wholesale fix, kept for its diagnostic value on bucket 4 alone).

The retrigger-count framing from the research pass ("the tie decision") is
retired: the decision is correct. What is wrong is downstream, in the VOICE, in
at least two independent mechanisms (a pitch-glide artefact and an accent-decay
shape error) with at least five more boundary shapes not yet looked at.

---

## 2026-08-05: task triage. Three things read as blocked and are not, and one "discrepancy" never existed

A priority review of the pending 303 task list, asking which of the newer tasks
supersede or make redundant the older ones. Partial: six investigations were
launched and one returned before the rest were lost to a usage limit. What is
below is that one plus direct verification. **The other five clusters are not
done and should be re-run**, namely the DC blocker against the retrigger defect,
the accent residual, the EM_FLOOR statistics, the measurement ordering, and the
cross-component question.

### The cutoff-law "discrepancy" was an apples-to-oranges comparison, and it was mine

`PLAN_303_ENVELOPE.md` §8 and the `CUT_SCALE` comment in `bass_synth.cpp` both
recorded that ReBirth's fitted centres give 3.09 octaves per cutoff knob against
our 2.90, called it a 6 percent shortfall, and noted as unexplained that setting
3.09 scored worse at every scale tried.

**Our slope is not a constant.** `RESO_OCT_CUT` adds slope with resonance:

    d(log2 fc)/d(cutoff knob) = CUT_SLOPE + RESO_OCT_CUT * reso

At resonance 60 that is 2.90 + 0.35 * (60/127) = **3.065**. ReBirth's 3.09 was
measured on the envfloor grid, which is resonance 60 throughout
(`gen_envfloor.py`, `reso=60` on groups A, B and C). The two agree to 0.025
octaves per knob. The recorded line compared a resonance-60 measurement against a
resonance-0 constant.

It also dissolves its own puzzle. `CUT_SLOPE` 3.09 scored worse because it would
put the effective slope at resonance 60 at 3.255, overshooting by 0.165.

Both sites corrected. This is the same failure the project keeps hitting, a
number compared against another number measured under different conditions, and
it survived a documentation audit because the audit checked whether docs agreed
with each other rather than whether they were right.

### Task 58's slope error does not reproduce

Refitting ReBirth's own batch H resonance-0 exports with a calibrated instrument,
`G^4/(1+kG^4)` against harmonics from `harm_response.py`. Calibrated first
against two renders whose slope is known exactly because the coupling term
vanishes at resonance 0: `renders/bh_ship` at the shipped 2.9 recovered 2.911
with a line residual of 0.003 octaves, and `renders/batchH_ours` at the pre-refit
2.5 recovered 2.522.

ReBirth then reads 2.832 to 2.988 across every band and floor choice, bracketing
our 2.900. What is actually there is a **flat offset of about 0.1 octaves low**,
by cutoff knob 16/40/64/90/110/127: -0.128, -0.085, -0.044, -0.048, -0.072,
-0.121. Flat, no crossing. Task 58 recorded -0.17 to +0.22 **with** a crossover
near 64, measured before 2026-08-02 at `CUT_SCALE` 1.05 with no env-mod-0
envelope on our side.

The offset's sign and size match the peaked-cell finding that wants `CUT_SCALE`
0.86. Task 58 should be rewritten as a resonance-0 corner target rather than a
slope error, because that is the region every task-74 instrument is blind to.

### Task 74 never ran a joint refit, and its conclusion survives anyway

Commits `0f429ba`, `198fed3` and `cb6930e` are one-parameter-at-a-time sweeps,
not a joint fit. `EM_FLOOR`, `EM_OCT`, `EM_CENTRE` and `CUT_SLOPE` appear in
none of them, and `RESO_OCT_CUT` was only ever zeroed alongside `RESO_OCT`, never
raised.

The conclusion holds regardless, because it is a **boundedness argument rather
than a fit failure**: prominence has exactly one lever, `K_B`, already at 3.90
against self-oscillation at 4.0 and still 1.87 dB short. No joint fit finds a
value a parameter does not have.

Two gaps carried forward rather than closed. Task 74's own text asks for an ear
check before the prominence finding is treated as settled, and it has not run.
And every instrument behind it is blind at resonance 0 and 30 by construction;
`harm_response.py` says so in its own code, and the prominence table covers
resonance 90, 110 and 127 only.

### Three items read as blocked and are not

**`COMPONENT_MATRIX.md` item 1**, the one that says "That measurement is the next
job and gates the rest": it claims the filter constants have never been checked
against their own noise floor and that batch H has no duplicate exports.
`renders/batchH_floor/` holds 30 files, six cells at five ReBirth exports each,
and the check was done twice, pooled median **0.114 dB rms** against a shipped
config scoring 4.745, so the fit sits about 42x above its own noise. Nothing is
gated by it and has not been since 2026-07-31.

**`COMPONENT_MATRIX.md` item 2**, "the drive shaper does not survive a joint
refit, blocked on an ear check": the ear check ran 2026-07-31 and drive ON won
all four cases. Settled, `satAsym()` stays. Only the level-matched rerun is live,
and it is in `PLAN_303_ENVELOPE.md` §8.

**Task 53** is marked blocked by task 57, which is complete.

All three fail in the same direction, reading as blocked when nothing blocks
them, which is the failure mode most likely to stall work that could proceed.

## 2026-08-06: the DC blocker does NOT confound the onset_ab COUNT, and inflates task 77's ratio by 2.7 dB through the FLOOR alone

Task 53, the priority-1 gate on the rhythm track. The question it exists to
answer: does `REBORN_DC_HP` inflate the floor that `onset_ab.py` divides by, so
that the 55-of-110 Clodhopper 303-2 disagreements, and tasks 77 and 78 behind
them, are partly a measurement artefact?

**The one render, three ways.** `renders/Clodhopper-mod.rbs`, `--solo bass2
--no-fx`, 12 s (7 bars at 140), against `renders/clod_rb/Clodhopper-303-2-bars1-7.wav`.
`REBORN_DC_HP` at 5 (shipped), 0 (disabled) and 20 (corner up). Same binary,
same command, same GT. The DC_HP=5 render reproduces the documented count
exactly, 55 of 110, which anchors the recipe.

**The count is blocker-robust.** 55 at DC_HP=5, 51 disabled, 56 corner-up. The
four steps that move (idx 13, 29, 86, 91) all sit at +4.2 to +4.6 dB at DC_HP=5,
right on the 4 dB threshold, and fall just under it when the blocker is removed.
They are threshold-crossers, not substance. No step appears on removal. So the
set of disagreements is not a blocker artefact.

**But the glide-jump bucket's MAGNITUDE is 2.7 dB blocker.** The seven large
disagreements sit at idx 1, 17, 33, 49, 65, 81, 97, which is step 2 of every one
of the seven bars, and they are task 77's "7 twelve-semitone jumps averaging
15.4 dB" exactly. They respond to the blocker: 15.4 dB at DC_HP=5, 12.8 disabled,
12.6 corner-up. The count did not move because they stay far above 4 dB either
way, but ~18 percent of task 77's headline number is the blocker.

**The mechanism is the FLOOR, not a ringing tail, and the direction is opposite
to the task's hypothesis.** Breaking the ratio into floor and peak (onset_ab's
own 3 ms envelope, per-file alignment):

| idx | ReBirth floor/peak | dc5 | dc0 | dc20 |
|----:|:---:|:---:|:---:|:---:|
| 1  | -54.6/-39.0 | -63.0/-31.7 | -60.3/-31.7 | -60.1/-31.7 |
| 17 | -54.6/-38.9 | -63.0/-31.8 | -60.3/-31.8 | -60.1/-31.8 |
| 97 | -54.6/-38.8 | -62.8/-31.8 | -60.2/-31.8 | -60.1/-31.8 |

The **peak is byte-identical across all three blocker settings** (-31.7 to -31.8
everywhere). Only the **floor** moves, from -63.0 at DC_HP=5 to -60.3 disabled.
The task supposed the blocker's 32 ms discharge tail would RAISE the floor and
mask retriggers. Measurement shows the reverse: a 5 Hz highpass barely rings in a
27 ms window, and its real effect here is to REMOVE sub-bass from the quiet
pre-step window, LOWERING our floor by 2.7 dB and so widening the ratio. The
blocker inflates the disagreement rather than masking it.

**What task 77 actually owns, and what it does not.** Decomposing the 15.4 dB:
ReBirth's own ratio at these steps is -39.0 - (-54.6) = 15.6 dB; ours at DC_HP=5
is -31.7 - (-63.0) = 31.3 dB. The gap splits into a **peak gap of ~7.3 dB**
(ours re-attacks to -31.7 where ReBirth reaches only -39.0 at the twelve-semitone
jump) which is completely blocker-invariant and is the real defect, and a
**floor gap** (ours -60 to -63 vs ReBirth -54.6) of which 2.7 dB is the blocker
and the remaining ~5.7 dB is a genuine difference: ReBirth sustains MORE low-end
through the gap than we do even with our blocker off. So task 77 must measure the
PEAK directly, or render blocker-off, or the onset_ab ratio will carry 2.7 dB of
blocker confound into the fit.

**Task 78 is untouched.** None of the accent-latch steps are among the movers.

**Verdict, gating three tasks.** The blocker does not confound the count and does
not create the disagreements. Tasks 77 and 78 are real. The single caveat handed
forward: task 77's controlled fixture must read peak, not the onset_ab ratio, and
should render with `REBORN_DC_HP=0` so the floor cannot add 2.7 dB.

**A note that contradicts the 2026-07-31 "notes do not stop, 20 dB gap"
diagnosis.** At these glide-in steps our floor reaches -63 dB with the blocker on
and ReBirth's is HIGHER at -54.6, so it is ReBirth, not us, that carries more
energy through this gap. The "ours rings on where ReBirth is silent" reading, if
it is real, is on note-OFF boundaries and a different patch, not on the steps
driving the onset_ab disagreements, and it should be re-confirmed on those
specific steps before any blocker change is made. No code was changed here.

## 2026-08-06: the accent flat-latch leak is real, decayonly fixes it, and onset_ab had the sign backwards

Task 78. The VCA accent lift is `ACC_LVL * accKnob * (ACC_SUS*latched + 0.90*accEnv_)`
at bass_synth.cpp ~1540. The flat `ACC_SUS*latched` term holds 0.10 of lift for
as long as the one-way `accentLatched_` stays set, which is an unbounded run of
ties, so it leaks forward. `REBORN_ACC_DECAYONLY` (added today, default off,
verified byte-identical to the shipped render) drops that term and lets `accEnv_`
carry the whole lift at coefficient 1.0. At the attack, where `accEnv_` sits at
target, `1.0*accEnv_` equals the old `0.10 + 0.90*accEnv_`, so the struck level is
unchanged and only the tail differs, with nothing flat left to leak.

**onset_ab says it is worse, and onset_ab is wrong here.** Clodhopper 303-2: 55
disagreements at default, 64 with decayonly, the same 55->64 the 2026-08-03
`REBORN_ACC_LATCH_EXPIRE` probe produced. Decomposing the 9 steps that newly
cross the 4 dB threshold (idx 16, 32, 36, 42, 48, 52, 64, 68, 84): every one has
its PEAK unchanged to 0.0 dB and its FLOOR dropped ~2 dB. They are floor
artefacts, not gate closures. This is precisely the floor sensitivity task 53
characterised this morning: onset_ab divides a peak by a floor, so any change
that lowers sustained level lowers the floor and inflates the apparent retrigger
count. onset_ab cannot judge a level change, and the 2026-08-03 "latch-expire
made it worse" verdict is the same artefact with the sign backwards.

**The floor drop is TOWARD ReBirth.** At those 9 gap steps ReBirth's own floor is
LOWER than ours: mean GT -74.8 vs default -70.4 dBFS. decayonly moves us from
-70.4 to -72.4, nearer ReBirth on 9 of 9. So the flat latch was holding our
between-note level ~4.4 dB too high, and decayonly closes about 2 dB of that,
roughly half the gap. The residual ~2.4 dB of excess sustain is separate (VCA
release, the DC-blocker floor of task 53, or `accEnv_`'s decay rate) and is not
chased here.

**This reconciles with task 53 rather than contradicting it.** Where notes
CONNECT, the step-2 glide/tie of task 77, ReBirth sustains MORE than us (GT floor
-54.6 above ours). Where a REST precedes the onset, these step-1 and step-5 gaps,
ReBirth sustains LESS (GT floor -74.8 below ours). Opposite signs, different step
types, both consistent with our note tail decaying wrong.

**regress_ab confirms it by a level-cancelled metric the floor cannot fool.**
Across all 373 fixture cells the third-octave spectral-shape rms falls from 2.325
to 2.274, 29 cells better and 1 worse. Every non-accent set is +0.000, so the
change touches only accented patches, as intended. accentprobe improves 4.432 ->
2.729, a 1.70 dB drop; accentdepth and accentrate both improve in the mean. The
single regression is accentrate `r303_B_control_tied_rest`, +0.279, a tied-rest
control, which points at the same residual-tail question as Clodhopper's gaps.
The glide-jump bucket (task 77) is byte-identical default vs decayonly, so the
change is cleanly isolated to accent behaviour.

**VERDICT.** decayonly is a real accent-parity improvement: byte-neutral off,
directionally correct on the between-note floor (toward ReBirth on 9 of 9),
spectrally better on 29 of 30 affected cells. onset_ab's 55->64 is a floor
artefact and must not be used to judge a level change. RECOMMEND making
`REBORN_ACC_DECAYONLY` the default on sign-off (drop the flat `ACC_SUS*latched`
term, `accEnv_` at coefficient 1.0). NOT applied to the default yet. The ~2.4 dB
residual sustain and the one tied-rest regression are a separate tail-decay
question, shared with task 53's non-blocker floor gap.

## 2026-08-06: task 77, our engine has NO glide artefact in isolation; the Clodhopper excess looks like the reso-127 filter mismatch (export INCOMPLETE, blocked)

Task 77, isolating the glide-jump bucket out of Clodhopper. Two solid results and
one blocked half.

**Clodhopper's hot step, decoded.** bass2 runs cutoff 29, reso 127, envmod 0,
decay 115, accent 96, saw. Its 32 pattern records were decoded from
`renders/Clodhopper-mod.rbs`. The onset_ab hot step (idx 1, step index 1) is, in
the looping patterns 0 and 1, a +12 semitone SLIDE (a tied glide up an octave)
carrying NO accent flag. The prior bar's step 15 is a plain unaccented struck
note, which clears the one-way accent latch, so accentLatched_ is false at the
slide. That is exactly why task 78's decayonly does not move this bucket: the
accent leak is not active here. So the bucket is neither the accent leak nor,
below, the glide.

**Our engine produces no glide artefact in isolation, across a wide sweep.** A
tied glide (struck base, then a slid tie to base+jump, no VCA re-attack) was
rendered with REBORN_DC_HP=0 across cutoff {29,60}, reso {0,60,110,120,127},
envmod {0,96} and jump {0,3,7,12}. In EVERY cell the slid step's peak is at or
below the struck step's, monotonically more so for bigger jumps, never a surge.
Measured with tools/analyze/glide_peak.py (step-locked peak, not the floor-
confounded onset_ab ratio, per task 53). Code review agrees: on a slide,
trigger() does not retrigger the filter envelope or re-pluck the VCA, only the
pitch glides (bass_synth.cpp ~172), so candidate (c), a filter-env re-fire, is
not what the engine does, and candidate (b), a freqCur_ glitch, has no code path
to produce a surge.

**The one export we got points at a STATIC mismatch, not the jump.** The export
batch wedged after one fixture (see below), leaving a single clean ReBirth
export: g303_j00_r110, the PIVOTAL CONTROL, jump 0 at reso 110. Compared to ours
by third-octave shape, each side self-normalised (never sample-correlated, since
ReBirth exports are not sample-deterministic): shape rms 1.68 dB with NO glide
present at all. Ours carries a resonant bump at 400-500 Hz where ReBirth's sits
higher at 635-1270 Hz, plus more 6-10 kHz and less 3-5 kHz. Ours is 3.0 dB
QUIETER overall, not hotter, at this reso 110. This is a static filter-response
mismatch at zero jump, which is the shape of the reframe: the Clodhopper "glide-
jump" bucket is most likely the known high-resonance filter mismatch (tasks
39/40/74/76) surfacing at the loudest steps, with the 0.87 jump correlation a
confound of where the pattern places its notes. NOT yet proven: it needs the
jump sweep and, above all, reso 127 (Clodhopper's value; reso 110 behaves
differently), where task 53 found ours' peak HOTTER than ReBirth.

**Why it is blocked.** `rbdrive batch` exported g303_j00_r110 cleanly (1.21 MB),
then g303_j03_r110 rendered truncated (8236 bytes, dead partway), then ReBirth
hung (Responding=False) and would not close after three attempts. loopMIDI is not
installed on this machine and was not running; REBIRTH_AUTOMATION.md names a
missing MIDI port as the suspected trigger for exactly this wedge. The CD was
mounted (drive E, RB338V20), so the established CD-wedge is ruled out. ReBirth was
left hung and NOT force-killed, per the standing rule that Stop-Process discards
its window geometry; clearing it is Luke's call. The truncated
g303_j03_r110.wav is locked by the hung process and will delete once it closes.

To finish: install/start loopMIDI (or confirm it is not the cause), then re-run
`python tools/rbdrive/rbdrive.py batch fixtures-2026-08-06/glidejump/glidejump.json
--out-dir renders/glidejump_rb --no-midi-check`. The fixtures and the ours-side
renders already exist; only ReBirth's side is missing.

## 2026-08-06 (cont.): task 77 RESOLVED. No glide artefact; the gap is the static reso-127 filter response

The blocked export completed once loopMIDI was up: all 10 fixtures clean, 0
failed. That is one more data point for REBIRTH_AUTOMATION.md's suspected loopMIDI
wedge (down = wedge, up = clean), moving it from suspected toward established. The
full jump x reso sweep, ours vs ReBirth, settles task 77.

**Glide is matched.** Comparing each render's slid step to its OWN struck step
(the struck base note is identical across all jumps, so this cancels the 3 dB
level offset), ours and ReBirth agree:

    reso  jump   ours g/s   RB g/s    diff
    127     0      +0.00    -0.05    +0.05
    127     3      +0.00    -0.11    +0.11
    127     7      -0.17    -0.33    +0.16
    127    12      -0.28    -0.35    +0.07
    110    12      -0.25    -0.84    +0.59

At Clodhopper's reso 127 the glide/struck ratios differ by at most 0.16 dB across
the whole jump sweep. Where they differ more, at reso 110, ReBirth's upward glide
loses MORE energy than ours (RB -0.84 vs ours -0.25 at jump 12), the OPPOSITE of
ours carrying excess onset energy at the glide. The +12 pitch jump contributes
essentially nothing to the Clodhopper disagreement.

**The real gap is static.** The third-octave spectral-shape rms between ours and
ReBirth is 1.7 to 2.0 dB at EVERY cell, jump 0 included, and does not grow with
jump (jump 0: 1.69 / 1.85; jump 12: 1.74 / 1.82). The export noise floor, a dup
against its original, is 0.00 to 0.09 dB, so this is ~20x the scatter and real.
Ours' resonant peak sits near 400-500 Hz where ReBirth's is higher at 635-1270
Hz, with more 6-10 kHz and less 3-5 kHz, and ours is 3.0 to 3.5 dB QUIETER
overall. This is the high-resonance filter-response mismatch (tasks 39/40/74),
sampled here at cutoff 29 and reso 110-127.

**Reconciled with task 53.** Task 53 found ours' PEAK 7.3 dB HOTTER than ReBirth
at Clodhopper's hot step, while here ours is 3 dB QUIETER overall. Both are the
same static filter-curve mismatch seen at different pitches: a misplaced resonant
peak is hotter than ReBirth at some frequencies and quieter at others, and
Clodhopper's hot step lands where ours is stronger. The 0.87 onset_ab correlation
with jump size was a confound of where the pattern places its notes, not a glide
law.

**VERDICT.** Task 77 is not a distinct defect. It folds into the reso-127
filter-response work: task 76's brightness residual, tasks 58 and 59, and the
diode-ladder topology fork. No code change. The controlled fixture
(fixtures-2026-08-06/glidejump), the ReBirth exports (renders/glidejump_rb) and
tools/analyze/glide_peak.py remain as the isolation and are reusable when the
filter response is refit. Our engine's glide DSP is confirmed correct against
ReBirth and needs no further work.

## 2026-08-06: task 76, the "18 dB unexplained" is stale; step-12 brightness is the accent corner push, the residual is an onset-level shape

Task 76 found on 2026-08-03 that Clodhopper 303-2 step 12 runs up to 29 dB too
bright in 1.2-5 kHz, that ACC_SWEEP owned about a third, and that ~18 dB survived
zeroing the accent, pointing at an unexplained filter residual. Re-measured today
on the current build (after the 2026-08-05 env swing and today's decayonly), with
step-locked windows and band SHARE normalised per the ab-parity rule, that
decomposition no longer holds.

**The steady-state filter at reso 127 matches.** On accent-0 held notes at
Clodhopper's cutoff 29 / reso 127, across abs 0/3/7/12, the 1.2-5 kHz band share
is within +/-1 dB of ReBirth and the resonant peak frequency is identical (651,
700, 684 Hz on both sides). Ours is 3 dB quieter overall. So there is no
steady-state filter-response defect at reso 127; the topology is not implicated
in steady state (this refines the 2026-08-06 task-77 note, which read the
whole-file 1.7-2.0 dB shape rms as filter response; most of that is transient).

**The step-12 spike is the accent corner push.** Step 12 (a SLIDE+ACC, accKnob
96) reads +19.5 dB share excess at default. Reverting ACC_SWEEP 0.80 -> 0.35
drops it to +1.9; removing the accent's filter effects entirely (ACC_SWEEP,
ACC_RESO, ACC_LVL all 0) drops it to -9.6, i.e. ours goes DARKER than ReBirth. So
the accent corner push owns essentially all of the share excess, not a third.
accOct scales as ACC_SWEEP*(1+reso), which DOUBLES at reso 127, so 0.80 overshoots
hardest exactly where Clodhopper sits. That is task 72's accent-corner-law fit,
not a separate residual.

**The "18 dB residual" was raw, not normalised.** With the accent fully off, the
RAW 1.2-5 kHz excess is still median +4.5, max +15.3, the shape of the original
+18. But that is a LEVEL effect: in the 45 ms onset windows ours is about +5.7 dB
louder broadband than ReBirth, while over the whole file ours is 3 dB quieter. So
ours has HOTTER ATTACKS and QUIETER SUSTAINS than ReBirth, and the raw band
excess rides that. Normalised to each file's own onset energy, the accent-off
residual is median -1.2, max +8.8, no 18 dB anywhere. Per the ab-parity rule that
level must not masquerade as a spectral error, the normalised number is the real
one and the residual is modest.

**VERDICT.** Task 76's unexplained residual is retired. The step-12 brightness is
the accent corner push (ACC_SWEEP 0.80 too aggressive at high reso, task 72), not
a filter-topology residual, and the steady-state reso-127 filter matches ReBirth.
A SEPARATE thread surfaced: ours' onsets are ~5.7 dB hotter and its sustains ~3
dB quieter than ReBirth, an attack-envelope shape difference, which is the honest
form of the "brightness" once level is normalised. NEXT: re-fit the accent corner
law per-step against ReBirth at multiple resonances (task 72); do NOT snap
ACC_SWEEP, since 0.80 was fitted for accent depth and the coupling is exactly
what the ab-parity skill warns of. Open the onset-vs-sustain level shape as its
own item. Measured on the Clodhopper render and the glidejump exports via
REBORN_ACC_SWEEP / ACC_RESO / ACC_LVL probes.

## 2026-08-06: task 72, the accent corner law's reso-scaling is wrong-signed; candidate fix improves the accent net 1.2 dB

Measured the accent corner push (env_traj resonant-peak at the note attack) versus
accent knob and resonance, ReBirth vs ours, from accentdepth (reso 60, existing)
plus a new accentreso export (reso 90 and 110, all 12 clean). Push in octaves
above the knob-0 baseline, at knob 127:

    reso    ReBirth   ours
    60      +1.34     +0.71
    90      +0.90     +0.88
    110     +0.78     +0.87

ReBirth's accent push DECREASES with resonance; ours INCREASES. Our law
accOct = accKnob*accEnv_*ACC_SWEEP*(1+reso) grows with reso, the wrong sign: we
under-push at reso 60 (half of ReBirth) and over-push by reso 110-127. That is
the reso-127 over-push task 76 saw and the reso-60 under-push task 60 saw, the
same defect with opposite sign, which is exactly what a wrong reso-scaling makes.

**Candidate.** Parameterised the reso factor as ACC_SWEEP*(1 + ACC_RESO_SLOPE*reso)
behind REBORN_ACC_RESO_SLOPE (default 1.0 reproduces 1+reso byte-for-byte).
Fitting the three knob-127 points gives ACC_SWEEP 3.30, ACC_RESO_SLOPE -0.708, so
the factor DECREASES with reso: 3.30 at reso 0, 2.20 at reso 60, 0.96 at reso 127,
replacing 0.80 -> 1.60. It boosts mid-reso push to match ReBirth's stronger accent
and cuts the high-reso over-push.

**Validated on the regression net.** Accent sets improve 1.22 dB rms: accentprobe
2.729 -> 1.931, accentdepth 3.970 -> 2.094, accentrate 2.488 -> 1.972, 21 cells
better and 2 worse. Full 373-cell net 2.274 -> 2.129, and EVERY non-accent set is
+0.000, so the change touches only accented steps. The 2 worse cells are low-knob
(knob 16, reso 60), a knob-curvature detail. On Clodhopper, step 12's 1.2-5 kHz
onset share excess falls from +19.5 to +13.8 dB (the rest is task 82's onset-level
shape). The reso-30 cells in accentdepth did NOT regress despite a ~2.8x push
boost, which empirically supports ReBirth pushing strongly at low reso rather
than this being a bare extrapolation.

**VERDICT.** Candidate ACC_SWEEP 3.30, ACC_RESO_SLOPE -0.708 is a strong, net-safe
accent-parity improvement. Held as a probe, default unchanged, pending the
Clodhopper ear check for sign-off. Open refinements: the low-knob curvature (2
cells) and the exact low-reso shape, since there is no clean reso<60 corner data
(the peak is too weak to track). Fixture tools/rbsgen/gen_accentreso.py; measured
via env_traj + regress_ab.

## 2026-08-06: task 82 RETIRED, the "hotter attacks, quieter sustains" is a Clodhopper alignment artefact

Task 82 was opened from the task-76 reconciliation, where Clodhopper's accent-off
onset windows read ~+5.7 dB louder broadband than ReBirth while the whole file
read ~3 dB quieter, suggesting a front-loaded amp envelope. It does not reproduce
on controlled fixtures.

- Plain struck note, cutoff 64 reso 60 (accentdepth d303_acc000_off), amp
  envelope normalised to peak: ours and ReBirth match in shape and both peak near
  gate-close (57 vs 60 ms); ours' 0-250 ms envelope area is if anything slightly
  LARGER (0.0509 vs 0.0483), the opposite of quieter sustains.
- Held note, cutoff 29 reso 110 and 127 (glidejump), onset-to-sustain ratio: ours
  is LOWER than ReBirth by 0.2 to 1.3 dB, i.e. ours is LESS peaky, not more.
- glidejump struck-onset level: ours ~3-4 dB quieter than ReBirth, matching the
  whole-file offset, so no relative onset hotness.

All three clean measurements agree: our amp envelope matches ReBirth's shape, with
a uniform level offset, and shows no peaky-onset defect. The Clodhopper +5.7 dB was
a sub-window timing artefact: aligning a complex pattern by bar-level cross-
correlation leaves ReBirth's per-note attack a few ms off inside the 45 ms window,
which deflates ReBirth's measured onset and inflates ours-minus-ReBirth. On
generated fixtures with known step timing it vanishes.

RETIRED. No envelope-shape code change. One real residual noted: at Clodhopper's
cutoff 29 / reso 127 patch ours sits ~3 dB quieter than ReBirth overall, a level
offset (not a shape error), most likely the resonance makeup-gain handling at
extreme reso, which belongs with the Track B high-resonance filter work.

## 2026-08-06: Luke's ear check on the shipped accent work surfaces the accent-rest resonance distribution

After task 72 shipped, Luke's ear on the Clodhopper ear check: the accented hits
now sound much more like ReBirth, but ReBirth still "resonates on each beat" while
ours lands "one loud accent around step 12". Measured, this is exact.

Per-step 1.2-5 kHz resonant activity, each side relative to its OWN loudest step
(step 12), averaged over 7 bars: ReBirth spreads it across the bar (steps 2 -7.2,
7 -5.5, 12 0.0 all within ~7 dB, most steps -15 to -18), while ours collapses to
step 12 with everything else 30 to 60 dB down. The largest gaps are the ACCENTED
RESTS: step 2 ReBirth -7.2 vs ours -34.3, step 7 -5.5 vs -33.0. Ours produces
almost no resonance where ReBirth fires a bright chirp.

**Mechanism confirmed.** Those rest steps fire the accent through holdStep at
fireAccent(ACC_TIE_TARGET()), and ACC_TIE is 0.05, so the corner push (accOct,
which is driven by accEnv_) is suppressed to near nothing. Rendering with
REBORN_ACC_TIE raised recovers them: at 1.0, step 2 goes -34.3 -> -8.3 (ReBirth
-7.2) and step 7 -33.0 -> -3.5 (ReBirth -5.5), nearly matching. So the resonant
chirp ReBirth fires on an accented rest is exactly the corner push our ACC_TIE
suppresses. This agrees with the accentrate finding that ReBirth's accent lands at
the same place however the step is articulated (accentOnly already fires 1.0),
while holdStep's rest path does not.

**Why it is not a one-line fix.** ACC_TIE 0.05 was fitted by task 60/75 to hold
the tied-slide RUN together (steps 12-14), where raising it re-breaks the run.
And accEnv_ drives BOTH the VCA level (accentLift) and the corner push (accOct),
so suppressing accEnv_ kills both, whereas ReBirth wants a modest LEVEL rise
(+2.8 dB, which ACC_TIE was fitted on) together with a STRONG corner push (the
bright chirp). The level and the brightness are coupled in our accent and
decoupled in ReBirth's, the same level-vs-brightness split task 76 found. The fix
is to give the accent-on-rest its own strong corner push while leaving the level
and the tied-slide suppression alone, not to raise ACC_TIE globally. Opened as a
task. Measured on the Clodhopper render via REBORN_ACC_TIE.

## 2026-08-06: task 83, the accent-corner decoupling works but the per-beat resonance is the reso-127 filter SUSTAIN (Track B)

Implemented the decoupling behind REBORN_ACC_CORNER_SPLIT (default off, byte-
neutral): a tied/rest accent fires the CORNER to a strong target while the VCA
level stays at ACC_TIE_TARGET, since accEnv_ drives both and accLevelScale_ scales
only the level. It behaves as designed. Unlike raising ACC_TIE, which coupled the
level and broke the tied run (onset 60 -> 88), the split raises the corner while
the onset count stays 58 to 67, so the run holds.

**But a flat corner target cannot reproduce ReBirth's per-beat distribution.** At
c=0.3 the controlled accent fixtures are neutral (accent net 1.955 -> 1.945, 5
better 4 worse) but Clodhopper's rest steps barely move (step 2 stays near -31
relative to the peak against ReBirth's -7). At c=0.6 Clodhopper improves more but
the reso-60 tied/rest fixtures regress (accent net +0.109, 8 cells worse). No
single value threads both, because the split brightens ALL tied/rest accents
together, the quiet rests (2,7,11) AND the loud slides (12,13,14), so the RELATIVE
spread barely changes while the absolute brightness rises and over-cooks the
reso-60 cells.

**The real driver is the RESONANCE SUSTAIN.** The distribution measurement showed
ReBirth holds 1.2-5 kHz activity across the whole bar (every step within ~18 dB of
the peak) while ours collapses to the loud accent with the rests 30 to 60 dB down.
That is the self-oscillating filter at reso 127 continuing to RING between notes,
which our TPT ladder does not sustain. The accent corner push fires a brief chirp
at each accented step; it cannot manufacture a resonant floor that rings through
the rests. So Luke's "ReBirth resonates on each beat" is a filter-topology matter,
the reso-127 self-oscillation sustain, and it joins tasks 76 and 77 as an
independent pointer to the diode-ladder fork (Track B).

**VERDICT.** The decoupling mechanism is valid and kept as a probe (default off)
for whenever the filter is addressed; at c~0.3 it is a small, fixture-neutral
improvement to the accent-rest chirp. It is NOT the fix for the per-beat
character, which is the reso-127 filter sustain, so it does not ship as a default
on its own. The per-beat resonance is Track B. Probes REBORN_ACC_CORNER_SPLIT and
REBORN_ACC_CORNER_TIE.

## 2026-08-06: Track B stage 0, ReBirth's prominence is BRACKETED by K_B at a clean high-reso patch (tuning here, not structural)

Ran task 80's prominence ear check, the Track B stage-0 gate, now that the accent
defect that blocked it is fixed (task 72). Patch: low A (55 Hz), resonance 120,
cutoff 100, gentle env sweep, no accent, the clean high-reso and high-cutoff point
where the prominence shortfall was said to be worst. Fresh ReBirth export at
renders/prominence_rb. Level-matched on the fundamental (40-90 Hz), so the
resonance above it is what is judged.

ReBirth's resonance is BRACKETED by ours on BOTH axes:
- Resonance energy above the fundamental: ReBirth +2.4 dB, ours shipped (K_B 3.40)
  -0.8, ours max-Q (K_B 3.90, the most before it whistles) +6.0. ReBirth sits
  between; a K_B near 3.6 to 3.7 would land it.
- Comb density (spectral flatness 800-6000 Hz, higher is denser): ReBirth 0.072,
  ours shipped 0.079 (slightly denser than ReBirth), ours max-Q 0.038 (sharper).
  ReBirth is between here too.

So at this clean patch our TPT reaches ReBirth's resonance from both sides by K_B
alone. The shipped 3.40 is 3.2 dB short here, and it is short because K_B is capped
globally by the reso-127 self-oscillation whistle, which leaves prominence on the
table at reso 120. That points at a conservative, reso-INDEPENDENT K_B, not a
structural wall.

CAVEAT: this is one clean saw patch with no distortion. The 5-6 dB shortfall and
0.16 comb the diode-ladder fork was premised on were cited at a more extreme point
(reso 127, or Clodhopper's distorted mix), so this does not fully close the fork.
But if even this deliberately-exposed patch reads as tuning, the disciplined move
is to fit a RESO-DEPENDENT K_B (open the resonance where there is headroom, hold it
down only near reso 127) BEFORE committing to a new filter core. Trio staged at
renders/EARCHECK_7_prominence; Luke's ear is the arbiter and the verdict is pending.

## 2026-08-06: Track B stage-0 VERDICT, the diode fork is shelved; the prominence gap is a K_B refit, not a filter core

Luke's ear on the staged trio (EARCHECK_7): ReBirth sounds "very much" between our
shipped and our pushed-Q, "a pushed hard version of the ReBirth version,
considerably more chirp", with NO graininess or density that neither of ours has.
So the prominence gap is TUNING, not the structural comb the diode-ladder fork was
premised on. Measurement confirms it at both operating points:
- reso 120, cutoff 100: ReBirth's resonance energy (+2.4 dB above the fundamental)
  and comb flatness (0.072) are BRACKETED by ours shipped (-0.8 / 0.079) and
  ours max-Q (+6.0 / 0.038).
- reso 127, cutoff 100 (the extreme the fork's premise came from): ReBirth (+3.3 /
  0.070) is MATCHED by ours at K_B 3.60 (+2.9 / 0.061), whistle-safe even at reso
  127 AND cutoff 127 (crest ~19.6, fundamental dominant, static and swept).

So the coupled diode-ladder core (REBORN_FILTER=diode) is NOT needed for
prominence. The gap was a conservative, resonance-independent K_B (3.40), capped
for a self-oscillation margin that does not bite until well above 3.60. Track B's
diode fork is SHELVED as scoped R&D behind its probe; the plan pivots to a K_B
refit.

BUT a flat K_B bump is not the clean answer. regress_ab K_B 3.40 -> 3.60 across 373
cells: net BETTER (2.129 -> 2.062, -0.067, 192 better, 115 worse), with big wins on
batchG (-0.176, 48 better), batchH (-0.142), batchN (-0.195), envfloor (-0.114),
envdomain (-0.091), but real regressions on batchI (+0.026, 29 worse), batchQ
(+0.027, 21 worse) and accentdepth (+0.058, 16 worse). A flat bump opens resonance
everywhere, which is right where ours was short (the high-reso batches) and wrong
where ours was already matched. So the fix is a RESO-and-cutoff-DEPENDENT K_B (open
the resonance where there is headroom, hold it near 3.40 where a bump
over-resonates), a refit of the task-43 resonance law, NOT a flat constant and NOT
a new filter core.

NEXT: refit K_B as a curve over (reso, cutoff) against the resonance-vs-cutoff
target, verifying the regressing sets (batchI, batchQ, accentdepth) hold. Then ear
check and sign-off. The diode core stays behind its probe as a fallback if the
curve refit cannot reach the extreme, but the ear verdict and the reso-127 match
say it will.

## 2026-08-06: task 85, a reso-dependent K_B closes the prominence gap cleanly where a flat bump could not

Stage 0 said the prominence gap is a conservative K_B, not topology, and to fit K_B
as a reso-and-cutoff-dependent curve. Fitted the reso curve from the batchN reso
sweep (0/60/90/127) at cutoff 64 and 127 against ReBirth's resonance-above-
fundamental:
- cutoff 64: matched by K_B ~3.40 at reso 60, ~3.55 at 90, ~3.65 at 127.
- cutoff 127: ~3.40 at reso 60, >3.80 at reso 90, ~3.70 at reso 127.

The reso-60 anchor is 3.40 (the shipped value) at BOTH cutoffs, which is why a flat
bump to 3.60 over-resonated there and regressed accentdepth.

Implemented K_B as a hinge, kBeff = K_B + K_B_SLOPE*max(0, reso - K_B_PIVOT): flat
at 3.40 up to the pivot then rising, behind REBORN_K_B_SLOPE / REBORN_K_B_PIVOT
(default slope 0 is byte-identical, verified). Candidate slope 0.47, pivot 0.47
lands 3.40 / 3.51 / 3.65 across reso 60 / 90 / 127.

VALIDATED on the full net, and CLEANER than the flat bump. regress_ab 373 cells:
2.129 -> 2.087, -0.042, 78 better and 9 worse, against the flat K_B 3.60's -0.067
but 115 worse. The curve holds every set the flat bump regressed: batchI +0.000
(was +0.026), batchQ +0.000 (was +0.027), accentdepth +0.001 (was +0.058), because
the pivot keeps reso 60 at 3.40. The high-reso wins are kept: batchG -0.119,
batchH -0.113, batchN -0.080, batchL -0.135, accentrate -0.053. The only cells
worse are tiny (envshape +0.009, a handful of singles).

OPEN: the cutoff dependence (reso 90 at cutoff 127 wants more K_B than at cutoff
64) is not yet captured; the reso hinge alone under-opens that one corner. A K_CUT
term (currently 0) is the follow-up if it matters after an ear check. NOT shipped
as default: the candidate is held behind the probe pending an ear check and Luke's
sign-off, per the task-72 pattern. On branch 303-kb-reso-refit.

## 2026-08-07: task 85 cutoff follow-up, K_CUT closes the high-cutoff prominence gap once it is gated at BOTH ends of the reso range

The reso hinge's open corner, resolved. Measured the cutoff dependence directly on
the batchH cutoff-by-reso grid (36 cells, env mod 0, static filter) with
harm_response.py, ReBirth ground truth in renders/batchH_rb. The hinge cuts
peak-prominence rms from 3.73 to 2.71 dB but leaves a residual that is near zero at
cutoff 16 and grows with cutoff, concentrated in the reso 90 to 110 band: still 4
to 5 dB short at cut 90/110/127. That is the audible gap, and it is what a
cutoff-scaled feedback term is shaped to add.

THE NAIVE TERM IS A TRAP, and two metrics disagreeing is how it was caught. A flat
K_CUT (k = reso*(kBeff + K_CUT*cutoff)) drops peak-prominence rms all the way to
1.26 at K_CUT 0.60, reading as a clear win on that metric. The spectral-shape net
says the opposite: batchN regresses +0.938, batchH goes neutral. Per-cell detail
localises the ENTIRE regression to the reso-127 corner (n303_C1_cut127_reso127
+2.62 rms at K_CUT 0.10 alone), because our k=4.0 self-oscillation cap already
OVER-resonates that corner versus ReBirth's controlled ceiling (23.4 dB / crest
13.8 against RB 20.8 / crest 15.9), and K_CUT drives it straight into the cap. The
prominence metric could not see this: it scores peaked cells only, and the failure
is where the resonance runs away, not where a peak is missing.

FIRST GATE, the top taper. cutGate ramps the boost to zero from reso 0.87 up to
1.0, leaving the self-oscillation corner at the safe hinge level. That fixed batchN
(the reso-127 blow-up became -0.18) and roughly doubled the grid win, but the full
373-cell net then exposed a SECOND regression, this time at LOW reso: batchI,
batchQ and accentdepth regressed WORSE than the flat K_B bump this whole task
rejected (accentdepth +0.08 to +0.11). Cell detail put every worst cell at reso
~030 and high cutoff, where ReBirth is a near-flat lowpass with no peak at all, so
K_CUT was injecting resonance that does not belong there. The batchH prominence
view was blind to it for the same reason as before: reso 030 has no peak.

SECOND GATE, the low end. Weighting the boost by resoAbove = max(0, reso - pivot)
zeroes it below the hinge's pivot, so it fires only in the reso 60-110 band where a
peak exists and there is headroom below self-oscillation. This holds the protected
trio STRUCTURALLY: batchI/batchQ stay +0.000 and accentdepth +0.002 at every K_CUT
value, because those sets' cells live below the pivot. No new free parameter, the
low gate reuses the hinge's pivot.

VALIDATED on the full net, both endpoints of the useful range clean. k = reso*kBeff
+ resoAbove*K_CUT*cutoff*cutGate, with slope 0.47, pivot 0.47, K_CUT_TOP 0.87:
- K_CUT 1.0: net -0.071, batchH -0.19, 82 better / 12 worse, NO set regressed.
- K_CUT 1.5: net -0.075, batchH -0.144, 90 better / 13 worse, NO set regressed.
Both beat the hinge alone (-0.042); K_CUT adds about -0.033 on top. Biggest wins
batchG -0.259, batchN -0.168, batchH -0.144, batchL -0.135, accentrate -0.073, all
held-out or fit, so the gain is not batchH/batchN overfit. batchH caps the top:
its improvement gives back monotonically as K_CUT rises (-0.19 at 1.0, -0.144 at
1.5, -0.046 at 2.0, +0.034 at 2.5), because mid-band reso 90-110 starts to
over-resonate past ReBirth. So 1.5 is the aggregate best and 1.0 the conservative
fallback; the choice between them is the live ear-check question.

RESIDUAL, by design. The reso-127 corner is left at the hinge level, still about 2
dB short at high cutoff, because cutGate deliberately spares it. The deeper reason
is that our TPT ladder's maximum resonance at the k=4.0 cap EXCEEDS ReBirth's most
extreme cell (23.4 vs 20.8 dB, and crest 13.8 vs 15.9, so ours is more whistle-like
there). ReBirth's resonance ceiling sits BELOW our self-oscillation cap. Closing
that last corner is a separate sub-fix (lower the effective ceiling to match RB, or
shape the very top of the reso curve), not part of this increment.

Byte-neutral CERTIFIED with tools/render/byte_neutral.py: HEAD default and the base
commit both pool to 50d00a6a across all 16 songs x 2 soloes, probe-on (K_CUT 1.5)
differs (2488cb21), so the probe is wired up and the shipped build is unchanged.
Behind REBORN_K_CUT (default 0) and REBORN_K_CUT_TOP (default 0.87). NOT shipped as
default: held behind the probe pending Luke's ear check on the batchH-band and
high-reso material, and his sign-off, per the task-72 pattern. On branch
303-kb-reso-refit.

## 2026-08-08: the 303 ear check HELD k10; the remaining resonance gap is CHARACTER (corner position + slope), not level and not grain

Luke's ear on EARCHECK_8 (K_CUT quartet, rebirth/shipped/k10/k15 on batchH cells
and the p303 patch): he preferred k10 over k15, but held it rather than shipping,
because once the LEVEL matches the resonance still does not sound like ReBirth.
Verbatim: k10 is darker at cut127/reso090 and cut110/reso090 with the envelope more
suppressed; at cut127/reso110 the chirp is more present, a ghostly harmonic that
reads as G in ReBirth is a more prominent, squeely A in ours; the p303 moving patch
is close but a little thinner with the chirp more present. The k10 preference was
only answering a forced choice, so the decision is HOLD and keep iterating on the
character. K_CUT stays committed but default-off on 303-kb-reso-refit.

MEASURED the character on the batchH cells (note A 110 Hz, env mod 0, static),
harm_response.py plus a fine-spectrum inharmonicity probe, three threads:

1. GRAININESS REFUTED. Trough-to-peak energy ratio in the resonance band (energy
   midway between harmonics vs at the harmonics) is 0.001 to 0.003 for ReBirth and
   the same or higher for ours (k10/A2 was 0.010). ReBirth's resonance is a CLEAN
   harmonic peak, not a grainy inharmonic one, so it is not carrying between-the-
   harmonics texture our linear ladder cannot make. This is the structural-comb
   premise Track B was built on, refuted a second time and now by a direct
   inharmonic measurement rather than by ear (the first was the EARCHECK_7 verdict).

2. CORNER POSITION, ours systematically LOW and reso-dependent. Peak frequency RB
   vs ours across the grid: at reso 60 to 110 ours trails RB by roughly 0.15 to
   0.25 octaves (reso 90 / cut 127: 2710 vs 3207 Hz), then at reso 127 ours runs
   HIGH (cut 127: 3724 vs 3585). The sign flip means the corner-RISE-with-resonance
   law (RESO_OCT) has a shape error, rising too slowly through the middle of the
   knob and too fast at the top, not a constant offset. A low corner reads as a
   flatter, ghostlier pitch (the "G") and as a darker tone. Fixing prominence with
   feedback (K_CUT) also raises the corner, and the coupling does not match RB: at
   reso 110 the k that matches prominence overshoots the corner (k10 3622 vs RB
   3402); at reso 90 both stay short.

3. SLOPE above the corner, ours too STEEP. Ours runs about -21 dB/oct against RB's
   -18.5, consistently across the resonant cells (RB is shallower than an ideal
   four-pole -20.7; ours is at it). RB keeps more upper-harmonic energy above the
   peak, which is the "grainy / rich" texture; our steeper rolloff collapses to a
   purer, squeely single tone. This is the far-field skirt residual PLAN_303 s0
   calls a constants refit, now tied to an audible character difference.

So the resonance character gap is CORNER POSITION (a reso-dependent shape error in
the corner-rise law) and SLOPE (ours too steep), both tuning axes, NOT inharmonic
grain and NOT level. They are entangled with prominence through the single feedback
k: ReBirth reaches a (corner, prominence, slope) triple that our ladder does not hit
by k alone. Whether our existing knobs (CUT_SCALE, the RESO_OCT corner-rise law, the
skirt/oversampling) can be refit to reach that triple, or whether the coupling is a
structural limit, is the open question and the next 303 step. NOT yet attempted; no
code change from this measurement. On branch 303-kb-reso-refit.

## 2026-08-08: forward-model verdict, the corner and prominence are TUNABLE but the resonance SLOPE is STRUCTURAL

Ran the forward model at two cells (cut90/reso90 and cut127/reso110, batchH,
env mod 0), sweeping each axis independently and reading back corner, prominence
and slope against the ReBirth export. cut90/reso90 is the clean read (its
resonance band is well below any ceiling or dither); RB there is corner 1685 Hz,
prominence 11.6 dB, slope -17.5 dB/oct.

- CORNER is tunable. CUT_SCALE 0.78 -> 1.05 moved our corner 1580 -> 2205 Hz;
  RESO_OCT moves it the other way. So CUT_SCALE / RESO_OCT place the corner where
  ReBirth's is, and the reso-dependent corner error is a corner-rise-law refit.
- PROMINENCE is tunable. Feedback k (K_CUT / K_B) moved prominence 7.6 -> 10.2 dB
  toward RB's 11.6 at cut90/reso90.
- SLOPE is NOT tunable and is the structural residual. Across every variant at
  cut90/reso90 the slope stayed about -21 dB/oct: shipped -21.5, corner-shifted
  (cs105) -20.7, gentler rise (ro015) -21.0, high feedback -21.2. RB is -17.5,
  about 3.5 dB/oct shallower, and nothing reached it. Worse at high cutoff:
  cut127/reso110 feedback STEEPENS our slope (shipped -19.4, k10 -22.1, khigh
  -24.3), so matching RB's prominence there drives the slope further FROM RB's
  shallow -17.3. DRIVE ruled out too: REBORN_DRIVE 1 -> 2 left the slope at -21.1
  while collapsing prominence 8.1 -> 2.8, and DRIVE 4 destroyed the note. So more
  saturation does not buy RB's rich tail, it kills the resonance.

CONCLUSION. After the comb and the graininess were both refuted, ONE real
structural difference survives: ReBirth's resonant filter rolls off ~3 dB/oct
SHALLOWER above the corner than our 4-pole TPT ladder, keeping a richer
upper-harmonic tail while staying prominent, and our ladder cannot produce that
combination by any tuning knob (corner, feedback, drive). This is the "grainy /
rich vs squeely / pure" character Luke's ear kept flagging, and it is the resonance
ROLLOFF SHAPE, not the inharmonic comb Track B was premised on. It reopens a
structural motivation, but a narrow and specific one: a topology that rolls off
shallower while staying resonant (a lower-order pole-tap mixed into the ladder
output, or the diode core measured for its transition slope), NOT a new core for a
comb that is not there. Corner is a tuning refit; slope is the structural item.
Next step is Luke's direction. On branch 303-kb-reso-refit.

## 2026-08-08: the real 303 resonance-character gap is an ONSET GLIDE caused by feedback; ReBirth reaches the same prominence with a flat resonance and ours cannot

Widened to the TIME domain (onset spectrograms, level-matched, batchH cut127/reso110
and cut127/reso090, env mod 0) after three static-spectral fixes (tap, oversampling)
each matched ReBirth on its own metric and were inaudible. That was the tell: the
character lives in the transient, not the steady spectrum. The spectrograms show it
plainly. ReBirth's resonance is a FLAT horizontal band from the attack. OURS starts
high (~4000 Hz) and glides DOWN to ~3300 over the first ~100 ms, re-firing the glide
on every note onset. That single difference is what Luke's ear tracked all along:
the "A vs G" is the glide launching sharp and settling, the "chirp vs snap" is the
same motion (ReBirth a steady pitched ring, ours a downward zip), and it is why the
tap and the 6x oversampling were both nulls, neither touches the onset glide.

CAUSE ISOLATED. Rendered A2 at env mod 0 varying one thing at a time: low DRIVE
(0.3) still glides, the FC_MAX ceiling raised to 12k still glides, but SHIPPED (lower
feedback) is FLAT. A feedback sweep confirms it scales monotonically: shipped (k~2.4)
flat, hinge (k~2.5) barely tips, k10 (k~2.9) clear glide, K_CUT 2.0 (k~3.2) strong
glide. So the onset glide is the FEEDBACK: at high feedback the resonance rings up
large, the nonlinear feedback saturation (satTanh) compresses it, and the effective
corner drifts down as it settles. Drive and the ceiling are ruled out.

THE REFRAME. This is the same lever as the prominence fix. K_CUT and the hinge raise
feedback to reach ReBirth's peak height, and that raised feedback is exactly what
makes the glide. Prominence and the glide are one knob pulling two ways, which is why
k10 measured right and never satisfied the ear: the prominence bought the glide.
ReBirth reaches the same peak height with a flat, glide-free resonance, so its
resonance is made and limited differently from our feedback ladder. This is the first
structural difference that survives measurement (comb refuted, graininess was
aliasing, slope was tappable but inaudible), and it is NOT the comb Track B was
shelved over. It is the resonance mechanism: how the peak is made and kept from
running away.

NEXT, in order. (1) Tunability first: can the feedback saturation be reshaped so the
corner stops drifting with amplitude while the peak stays tall (glide off, prominence
kept)? If yes it was tuning. (2) If it will not separate, this is the genuine measured
reason to reopen the diode core, with a specific yes/no test: does a diode ladder stay
flat at a loud onset. Not yet attempted. Bears on the K_CUT/hinge prominence work,
which may be buying a glide; do not ship k10 as-is until this resolves. On branch
303-kb-reso-refit.

## 2026-08-08: the pole-tap reaches the slope but is a NULL by ear; the real character gap is ALIASING, cleared by oversampling

Turned on the 2-pole tap (an existing default-off probe, REBORN_TAP=2). It
reaches ReBirth's slope: at cut127/reso090 tap gain 0.40 gives -18.1 dB/oct vs RB
-18.0, at cut127/reso110 -17.3 vs -17.3, at under 1 dB prominence cost. The needed
gain scales with cutoff (blend fraction ~0.09 low, ~0.20 at cut 127), the same
cutoff-scaled/reso-gated shape as K_CUT. So the slope is NOT a structural wall,
it is a tap. BUT the ear check (EARCHECK_9_tap, rebirth/k10/k10+tap on the character
cells) came back a NULL: Luke heard the tap as not hollow or phasey but barely
distinct from k10, slightly more MUTED on A2/A3, and said ReBirth's harmonics are
clearer than either of ours. The slope was reachable but it was not the audible
character. Do not fit the tap.

The reframe his ear forced, and it agrees with the earlier inharmonicity number:
the character gap is HARMONIC CLARITY. Measured the energy BETWEEN the harmonics
(mud, lower = clearer) at cut127/reso110: ReBirth 0.0027, ours 0.0104, about 4x
muddier. Cause is ALIASING, confirmed by oversampling: TPT_OS 2/4(default)/6/8
gives mud 0.079 / 0.0104 / 0.0031 / 0.0025. Our high-Q resonance throws harmonics
past Nyquist that fold back to non-harmonic frequencies; the current 4x is not
enough at high reso and cutoff. 6x lands on ReBirth's 0.0027 (0.0031); 8x is
marginally better (0.0025). STAGE_NL ruled out (already 0, off). So the "grainy /
muddy vs clear" Luke kept hearing is aliasing, a clean tunable (raise TPT_OS to 6),
not the slope, not the tap, not a new core.

CANDIDATE: TPT_OS 6, staged for ear at EARCHECK_10_os (rebirth/k10/k10+os6). Cost
is 1.5x the ladder vs the current 4x. NOT committed and NOT validated on the net
yet: pending Luke's ear that the clarity matches, a regress_ab pass that 6x does
not shift the broader fit, and a decision on always-on 6x vs engaging it only at
high resonance where the aliasing bites. On branch 303-kb-reso-refit.

## 2026-08-08 CORRECTION and HANDOFF: the "onset glide" finding above OVERSTATED it; the glide is SHARED, the residual is at the floor, and the 303 resonance CHARACTER work is parked

Correcting my own record. The entry above titled "the real 303 resonance-character
gap is an ONSET GLIDE" was read off an eyeballed spectrogram and it is wrong as
stated. Plotting the resonant-peak frequency TRACK properly (tools/analyze/
onset_resonance.py; renders/303_resonance_character/peak_track.png) shows ReBirth's
peak ALSO glides down about 0.3 octaves per onset (3700 -> 3050 Hz). Ours glides
3850 -> 3050, a hair more and starting a hair higher. THE GLIDE IS SHARED, not the
gap. The spectrogram fooled me because a sharp bright line (ours) against a broad
diffuse band (ReBirth) reads as "glide vs flat" when both are gliding.

What the peak track DOES show is a peak-WIDTH difference, and it is metric-dependent
so treat it with care: frame by frame ours is a SHARPER peak (~120 Hz) than ReBirth
(~300-500), but the TIME-AVERAGED PSD inverts it, ours reads BROADER (~436 vs ~334
Hz) because our sharp peak glides and smears in the average. Both are true. Which
one the ear weights is unresolved, and I will not name it as THE gap after being
wrong three times.

THE CHEAP SWING (Luke asked: reshape the feedback saturation to take the glide off
without losing prominence) came back empty. satTanh is tanh; U_SAT is the input
soft-clip headroom (tanh(x/U_SAT)*U_SAT, default 1.0) and FB_DRIVE scales into the
feedback tanh (default 1.0). U_SAT 2/4/8 does not move the glide or the width.
FB_DRIVE 0.6 broadens the peak but drags its frequency and level down and makes it
erratic. No clean width-or-glide knob in the feedback.

THE DIODE, one look: REBORN_FILTER=diode raw at this cell renders peak 2089 Hz,
prominence 1.5 dB, i.e. its resonance is not engaged. It is UNCALIBRATED (its
corner and D_K resonance law were never fitted), so a fair diode test is a scoped
calibration project, not a quick probe. That is the honest cost of the diode
rematch.

THE PATTERN IS THE FINDING. Chasing the resonance character I successively named
the gap as the rolloff SLOPE (the 2-pole tap reached ReBirth's -18 dB/oct, INAUDIBLE
by ear), then ALIASING mud (TPT_OS 6 matched ReBirth's inter-harmonic energy,
INAUDIBLE), then the onset GLIDE (SHARED with ReBirth, an overread). Each "gap"
either vanished at Luke's ear when fixed or turned out not to be a gap. His own
verdict across the ear checks was "practically inaudible." So the 303 resonance is
close enough that the character residual sits at the floor of both what these meters
can cleanly separate and what the ear reports.

WHAT IS ACTUALLY IN HAND. The LEVEL fix, REBORN_K_CUT (see the two task-85 entries),
a reso-and-cutoff-gated resonance boost validated on the full net (K_CUT 1.0 net
-0.071, 1.5 -0.075, both beat the hinge -0.042, protected sets held, no set
regressed), byte-neutral, HELD behind its probe. Luke held it because on the static
test tones it did not satisfy his ear; the open question for it is an ear check on
MUSICAL material (Clodhopper etc.), not held A notes, to decide k10 (K_CUT 1.0, his
preferred of the two) on its own merits.

RECOMMENDATION for whoever picks this up, and Luke is deciding how to proceed:
1. Ear-check k10 (REBORN_K_B_SLOPE=0.47 REBORN_K_B_PIVOT=0.47 REBORN_K_CUT=1.0) on
   musical material. If it holds up, it is a real level-parity win to ship on
   sign-off. If not, drop it too.
2. Treat the resonance CHARACTER (sharp-vs-broad peak; both glide) as a
   characterized, parked residual. Its one possible structural fix is the diode
   core, which would first need its corner and resonance law calibrated. Do NOT
   reopen it on a whim; it is a project, and the payoff is practically inaudible.
3. Do NOT re-derive the slope or the aliasing as "the gap"; both are matched-on-
   the-meter, inaudible, and recorded here. The corner-position refit (RESO_OCT
   shape error, ours low at mid reso) is the one remaining TUNING lever that is
   both measurable and was never a null; it is the honest next parity step if the
   303 filter gets more budget.
All 303 character work is on branch 303-kb-reso-refit; nothing is on main and
nothing ships without Luke's sign-off. Evidence images in
renders/303_resonance_character/, method in tools/analyze/onset_resonance.py.

## 2026-08-08 POLICY CHANGE: MIT/free-libre 303 emulators are now permitted; this supersedes the "no Open303 source" constraint recorded above

Luke changed the clean-room policy. The rule was always really two rules bundled
into one sentence; they are now split:
- **ReBirth clean-room STANDS.** No decompiling or transcribing ReBirth, ever. No
  ReBirth-derived bytes ship (samples, songs, mods, artwork). Parity is still
  judged only against ReBirth's OUTPUT as a black box. Both non-negotiable rules
  (this, and "nothing merges to main without Luke's sign-off") are unchanged.
- **The Open303 bar is LIFTED.** MIT-licensed and free/libre 303 emulators
  (Open303, RobinSchmidt/rosic) may now be read, adapted, and shipped, under their
  own licenses and with attribution. GPL forks (jc303) are still NOT bundled into
  a shipped binary (linking would force GPL), though they may be hosted as
  separate plugins. Canonical statement: `docs/PROVENANCE.md` ("Third-party
  open-source code").

This SUPERSEDES the 2026-08-03 note above ("No Open303 source was read... the
constraint is recorded in reborn338_HANDOFF_2026-07-23.md and Luke reaffirmed
it"): that constraint no longer holds. It also RESOLVES the provenance half of
task 84: the =rb probe's Open303-seeded b0 constants are now permitted with an
in-code MIT attribution (added at bass_synth.cpp), not a purge refit. Note Open303
emulates the hardware 303, not ReBirth, so it is a synthesis/product input and
never a ReBirth-parity input; the native TPT engine (whose constants are fitted
from ReBirth exports) remains the parity path.

This is the documentation half of the modular-303-voices fork (branch
303-modular-voices): the 303 voice becomes a pluggable slot with an Open303-derived
engine as the default, the native TPT kept selectable as the parity voice, and a
host harness for third-party 303 plugins. Plan and phasing in the approved plan
file; PLUGINS.md reframed accordingly. Docs only; nothing on main.

---

## Retired 2026-08-26: the 909 accent depth of 3.967

**Retired**, and replaced by `1 + 7.934 * knob`.

The 2026-08-25 sweep was clean in every way it knew to check: accented and
unaccented hits inside one render, the knob crossed against the row, a control
proving the knob alone does nothing. It measured the wrong cell. `rbsgen` wrote
the accent column as value 1 for both machines; a 909 writes 2 there, in all
1442 accent cells across the shipped songs, and ReBirth scales the accent depth
by that value. So the fixture measured a cell no real 909 part contains, and
came out at exactly half depth.

Consequence while it stood: every 909 accent in every song played at half
strength, which on Trakka Rekka was most of the 4 dB the whole 909 was down.

The lesson is not "cross more variables". It is that a **generated fixture can
differ from real song data in a way the crossing never touches**, and the check
that would have caught it is cheap: compare the fixture's own bytes against the
same bytes in a shipped song before trusting a law fitted on it. See also the
2026-08-25 cell-value correction, which is the same class of error one column
over.

## Retired 2026-08-26: three mixer laws read as linear

The master fader, the channel fader and the pan law were all implemented as
linear readings of their bytes, none of them ever measured. All three are wrong;
see PARITY.md for the measurements. They were invisible to every fixture because
rbsgen writes 100 into both faders and 64 into pan, which is precisely where
each law is anchored.

**Standing rule this suggests**: a control whose fixture value never moves has
not been measured, however many fixtures use it.

## Retired 2026-08-26: the 808 accent depth of 3.95

**Retired**, replaced by 4.02. The batch-E fit was measured through the device's
own `tanh`, which compressed the accented hits it was measuring. Re-measured on
a linear bus the law is `1.0121 + 4.0233 * knob` with residuals under 0.0015.
Unlike the 909's accent law, the fixture itself was fine: an 808 really does
write 1 in its accent column. Only the saturator had to go.

## Standing rule added 2026-08-26: a saturator is a parity surface

Three nonlinearities in our chain had never been measured against ReBirth: a
`tanh` on each drum device bus, `softClipUnity` on the master, and a compressor
that ran on every render whatever the song said. All three were added for
musical safety and all three were wrong, because ReBirth is linear in all three
places. They hid because they only bite on loud material and **no fixture drives
anything hard**: isolated voices at moderate levels sit 30 dB below the
ceiling.

If we have a nonlinearity and cannot point at the measurement that put it there,
it is a bug. The test that finds them is cheap: stack every voice at maximum
level and maximum accent, lower the master so the export keeps headroom, and
check that the gain law is still the one measured on a single quiet voice.

---

## Corrected 2026-08-27: "the 909 synthesis path was never calibrated"

**Overstated, and corrected in DRUM909_FINDINGS.md.** The claim appears in the
2026-08-26 synthesis-pass write-up and in the merged commit messages for that
work. The git history contradicts it: those voices were written 2026-07-15
(`7a9ffe8`) and tuned against ReBirth references the next day (`e881b8e`
"reference-driven tuning round 1", `b9eaf15` "per-device A/B against original
exports"), which is where the "Reference-tuned" comments in `drum909.cpp` come
from.

What is defensible is narrower: **calibrated in July, then left behind.** The
July rounds predate the isolated-fixture discipline and the coherent-averaging
discovery; supertask 3 explicitly did only the sample path; and one trim table
served both paths, so the synthesis balance was the sample path's numbers. The
synth voice shapes were untouched from 2026-07-15 until 2026-08-26.

Worth recording as a habit, not just a fact. The overstatement was not a
measurement error (every number in that pass stands); it was a claim about
**history** made from what the code looked like rather than from what the history
says. A `git log -S` on the constants in question takes seconds and would have
caught it. When a write-up asserts that something was never done, check the log
before writing it: the repo is the durable record, and "nobody ever tried this"
is exactly the kind of line a future reader will take at face value.


---

## Accepted residuals 2026-08-27: the PCF, at close of supertask 4

Stated with magnitudes, as the deep-dive close-out requires. None of these is
"close enough".

~~**Resonance byte 96 is 2.15 dB rms out.**~~ RESOLVED 2026-08-28. The corner
drift was real, not a fit artefact: it survives the same estimator applied to
both sides on a denser source, and modelling it as a quadratic in the resonance
knob takes that row from 2.15 dB to 0.52.

~~**Five parameter bytes are extrapolated, not measured.**~~ RESOLVED
2026-08-28, and the two halves came out differently. A drone an octave lower
measures cutoff bytes 0 to 48 directly and confirms the extrapolation to within
0.53 dB, so that law needed no change. The resonance end did: bytes 112, 120 and
127 are bit-identical, so the knob SATURATES at 112, where the law had been
extrapolating on to Q = 9.5.

**The resonance makeup gain plateaus at -4.1 dB from Q byte 80 up** while Q
keeps climbing. Reproduced in both fixture rounds across four settings, so it is
recorded as measured rather than explained.

**Pattern length is the weakest column of the preset table.** It is inferred
from the smallest shift under which the trigger set repeats over four bars. The
manual says most patterns are one or two bars, so the entries reading 96
thirty-seconds are suspect and may be 64 or 128 that missed the tolerance.

**The envelope sits at 0.40 octaves rms** against ReBirth, worst on the densest
thirty-second patterns, where back-to-back triggers into a 22 ms decay are most
sensitive to trigger timing.

## Standing rule added 2026-08-27: change the SOURCE before the statistics

Round 1 of the PCF learned that a 303 drone constrains a filter only between its
fundamental and where its harmonics sink into the export's dither. Round 5
forgot it and spent three estimators on the same limitation: a per-frame
spectral fit, a calibrated centroid, and noise subtraction with one-sided
trimming. All three failed differently and none could have worked, because the
drone's 64.6 Hz period forces an analysis window longer than the decay being
measured.

A broadband probe (kick, crash and closed hat on every sixteenth, 43 Hz to
18 kHz) settled it immediately: at an 11.6 ms window pattern 0 reads exactly
three triggers with identical velocities.

**When a measurement will not settle, ask what the source cannot show you before
reaching for a better estimator.** A source limit looks exactly like estimator
noise, and no estimator recovers information the fixture never carried.

## Accepted residuals 2026-09-07: the PCF swept resonance

Supertask 4's swept-resonance investigation (branch `exp/pcf-resonance`,
`PLAN_PCF_RESONANCE.md`, `PCF_FINDINGS.md` §12-13). Luke's ear on the
bandpass-fixed listening set: ReBirth is "more flangy, ring-modulator, a curve on
every chirp". Fixed along the way: the bandpass level law (peak grows with Q, was
flattened; 18 dB -> 0.3 dB) and the envelope depth (Amt opens the cutoff at 0.95,
not 1:1). Two residuals accepted, each after its candidate mechanisms were
measured and ruled out:

- **Swept-resonance brightness (mid/high cutoffs).** ReBirth's resonant peak is
  bright at the attack and rides the sweep down as a curve; ours is duller. PROVEN
  to be a cutoff-POSITION difference, not resonance dynamics: ReBirth's cutoff
  sits in the source band at the attack (~6.5 kHz on the Freq-0 chirp) while ours
  over-opens above it (~14.5 kHz), and a marginally-stable resonance-core REWRITE
  (Chamberlin, delayed feedback, oversampled) behaved identically to the TPT
  because a resonance core sits AT the cutoff and cannot move it. The low-cutoff
  over-opening is real (~0.073 cparam, ~1.7x) but no single law fixes it without
  disturbing the static Amt; makeup, cutoff-law top, depth-scale and depth-offset
  all ruled out or non-converging. Magnitude: 3-5 dB in the 2-11 kHz bands during
  the sweep on high-Amt low-base fixtures.
- **Pattern level deficit vs trigger density.** On the 909 listening patterns ours
  runs 1-3 dB quieter, correlating with trigger density (one-trigger pat00 +0.96
  dB; dense sixteenth/32nd pat05/34/40 -2.8 dB) while pat05's centroid matches
  ReBirth. A retrigger/attack effect, not the sweep; no clean lever (the
  retrigger-is-a-reset behaviour is measured to match ReBirth).

Both are subtle and resisted every model and clean measurement; the bandpass fix
closed the gross difference. The zero-delay TPT filter and the analog resonance
ring/ride are in the same structural tension the 303 round-chirp hit. Reopen only
with a new mechanism, not by re-running these fits.

## Supertasks complete: distortion and compressor proven (2026-09-18)

This supersedes the "Open (need controlled tests)" entries above for the effects.
All seven components (303, 808, 909, PCF, delay, compressor, distortion) are now
measured against ReBirth exports, fitted, and folded to main.

**Distortion (supertask 7), PROVEN.**
- It is a WAVEFOLDER, not a tone-tilt clip. Proven by mechanism elimination on the
  drone: only foldback reaches ReBirth's 4-8% HF (a plain hard clip tops out near
  0.8%); clip/asym/rectify/bitcrush all top <1%.
- Byte map corrected: DIST byte 1 = Shape, byte 2 = Amount (the loader had them
  swapped). Amount 0 = exact bypass.
- The "Shape as a tilt EQ" model is RETIRED. Measured, Shape drives the fold
  threshold and the quantizer step count (non-monotonic in brightness).
- The "vocoder" texture is inharmonic/aliased content (`dist_inharm.py`), matched
  to ReBirth's 38-69% on the dense grid; character ear-signed off.
- Output gain law fitted from the measured level surface (`dist_gain.py`), median
  level gap across the driven grid 4.6 -> 2.0 dB, replacing the `kOutTrim` constant
  that was up to 13 dB too loud at high drive.
- The `level` param was phantom and is RETIRED. Per-section topology and the DIST
  Amount/Shape/on-off timed automation are decoded and dispatched.
- Whole-mix regression (A Taste of Haste) within -1.5 dB and ~1.5 dB per band.

**Compressor (supertask 6), PROVEN.** Soft-knee downward with automatic make-up
(the "leveller" character), RMS detector, fitted within ~1 dB across the surface;
one accepted model-inherent residual at the threshold-0 corner (-4.17 dB). Full
record in `docs/research/PLAN_COMP.md` / `SPEC_COMP.md`.

**A verification rule that cost a confounded pass and is now enforced:** NEVER
measure on the Open303 voice (15.65 dB quiet); always `REBORN_BASS_ENGINE=tpt` or
a `*_wav` through-effect harness (BACKLOG 15).

**Still open (small):** the DELY and COMP knob-ride decode (BACKLOG 5); the
section (mono) compressor path is unverified (only the master path was measured).

## Proven 2026-09-26: the delay plays its DELY track in Song mode, not its chunk

Review E3. Approved by Luke 2026-09-26 and fixed in `57c7cfb`. This closes the
DELY half of the "still open" line above; the COMP rides stay carried and unplayed
pending Luke's call (BACKLOG 5).

**Condition.** Four isolated fixtures (`fixtures-2026-09-26/delay-chunk-vs-track`):
one 909 rimshot per downbeat through the delay, 120 BPM, echo send 100, nothing
else in the path. The DELY chunk was patched to disagree with the DELY track's
tick-0 dump on steps, feedback and pan at once, in both directions, plus a copy
naming the MSM2 mod. Exported from ReBirth in Song mode
(`renders/delay-chunk-vs-track-2026-09-26`) and read per channel at each
candidate echo time relative to the dry hit, no mono sum.

**Result.** ReBirth played the TRACK in every case, to 0.1 dB, and the mod copy
loaded MSM2's rimshot and still played the track. Ours played the CHUNK in both
disagreeing directions. After the fix our renders of all four show ReBirth's echo
times, L/R balance (centred within 0.1 dB, or the far side cut to silence where
ReBirth's is 46.6 dB down) and per-repeat ratio (-2.4 dB on both sides). Full
tables in `PARITY.md`, last section.

**What moved.** Full-rack 90 s renders, TPT voice, before and after:

| song | first change | L RMS | R RMS | L-R before | L-R after | why |
|---|---|---|---|---|---|---|
| A Taste of Haste | 0.32 s | -0.01 | +0.01 | +0.05 | +0.04 | start: steps, feedback, pan; 10 rides from 48.0 s |
| Alloy Cowboy Resurrection | 0.79 s | -0.87 | -0.44 | +3.30 | +2.87 | start: steps, feedback, pan; 36 rides from 25.3 s |
| Calm | 8.08 s | -0.01 | -0.01 | 0.00 | 0.00 | start: feedback; its one send (bass 2) is muted until 7.4 s |
| G-Spot | 0.60 s | -0.07 | -0.06 | +0.01 | 0.00 | start: feedback; 460 rides from 5.9 s |
| Groove It Up a 2nd Time! | 58.19 s | +0.03 | +0.13 | -0.17 | -0.26 | 17 feedback rides from 57.8 s |
| Metallicon City | 64.38 s | 0.00 | -0.06 | +0.03 | +0.09 | one pan ride at 64.0 s |
| Multiball Challenge | 43.98 s | 0.00 | 0.00 | 0.00 | 0.00 | start: steps, feedback; its two sends (the 303s, ridden up from 5.3 s) carry nothing until both 303s enter at 43.6 s |
| Mutilated Species II | 2.04 s | +0.75 | -0.56 | +0.90 | +2.21 | 326 rides from 1.9 s |
| SuperNova | 0.31 s | +1.66 | 0.00 | -4.70 | -3.03 | start: pan 127 to 85 |
| TGV's KiloMix '98 | 54.02 s | 0.00 | +0.03 | -0.48 | -0.50 | one feedback ride at 53.7 s |
| Trakka Rekka | 0.43 s | +0.44 | +0.16 | -1.26 | -0.99 | start: feedback 0 to 36, pan 127 to 63; 13 rides from 53.5 s |
| Transfusion | 0.31 s | -0.05 | -0.01 | +0.27 | +0.23 | start: feedback, pan |

RMS columns are after minus before, dB; ride counts are those inside the 90 s.
Clodhopper, Fast Forward, Fonda Workout and RSI are byte-identical: chunk and
track agree and no ride falls inside 90 s. The changed set is exactly what the
files predict. `--no-fx` renders are byte-identical before and after, and the
byte-neutral pooled hash holds.

**Carried forward.** The ring's behaviour while the unit is off, the echoes in
flight when Steps or the grid is ridden, and a 0.9 dB echo-to-dry excess on the
repeating fixtures that predates this change (`PARITY.md`). The first is measured,
and the third moved from the delay to ReBirth's hit timing, in the 2026-09-27
entry below; the second is still unmeasured.

## Proven 2026-09-27: the delay's rides and unit switch, and the 0.9 dB is hit timing

**Condition.** Five isolated fixtures, `fixtures-2026-09-27/delay-rides` (its
`MANIFEST.md`): one 909 rimshot, echo send 100, 120 BPM, 4 bars, delay pan 64 at
tick 0, everything else off. `isolation_check.py` fails each file for exactly one
reason, the timed DELY event under test. Exported in Song mode to
`renders/delay-rides-2026-09-27` and read per channel (no mono sum), 30 ms windows,
each relative to its own channel's bar-0 dry hit. Ours with `reborn_render`, the
standard 909 samples, full rack, 8 s. Tables in `PARITY.md`, delay section.

**PROVEN, and matched.**
- *Timed rides play.* dr_even (pan ridden to 127 at bar 2): the echo is centred
  in bars 0 and 1 and hard right in bars 2 and 3, -10.0 dB near side, in ReBirth
  and in ours.
- *Switching the unit off (DELY param 0) CLEARS the delay.* dr_p0 (steps 4,
  feedback 96, off at bar 1, on at bar 2): ReBirth plays no echo in bar 1, and bar
  2's hit echoes at -10.0 and -12.4 dB, exactly a fresh hit's, with nothing left
  from before the switch-off and nothing from input sent while it was off. Ours
  froze the ring and replayed bar 0's tail in bar 2 (+1.4, -6.9, -9.3); since
  `f7c7930` it empties the ring at the switch-off and reads ReBirth's figures.

**PROVEN, not yet matched.**
- *Rides land at their 1/32 tick and glide.* dr_odd40, 41 and 42 ride the pan to
  127 at ticks 40, 41 and 42 around an echo leaving at 2.583 s, between ticks 41
  and 42. ReBirth's far side reads -52.4, -16.4 and -10.0 dB: the tick-41 ride
  reaches the echo 21 ms later, only 6.4 dB down, and the tick-40 one is still
  4.3 dB short of its settled -56.7 after 83 ms. Ours applies rides at the next
  16th and at once (silent, -10.0, -10.0). BACKLOG 18.
- *The far side at pan 127 is -46.8 dB, not silent* (dct_mirror and dr_even,
  least-squares gain of L on R, correlation 0.99). Ours is digital zero. BACKLOG
  19; `SPEC_DELAY.md` and `PLAN_DELAY.md` corrected.

**ASSUMED, not measured.** That Steps, grid and feedback rides behave like the
pan ride. No fixture rides them.

**MOVED, NOT RETIRED: the 0.9 dB echo-to-dry excess** on the repeating E3
fixtures. This entry first retired it as the reader; adversarial verification the
same day showed that was wrong. The DELAY is matched: against bar 0's hit, which
nothing precedes, the least-squares gain of each echo is -9.98 and -12.42 dB in
ReBirth and -9.98 and -12.41 in ours (dct_ref and dct_track), and -9.98 on both for
dct_mirror's one echo, a match to 0.01 dB. But the rendered audio really differs
wherever an echo lands on a later hit, by more than 0.9 dB, and the cause is
ReBirth's hit timing (below), not the reader. `echoes.py` averages bars 1 and 2
against a dry reference taken from those same downbeats, which hid the size. Per
bar, dct_ref, 30 ms windows at the downbeat and the two echo times, each file
against its own bar-0 hit (per channel; L and R read alike, the fixture being
centred, and the model below is built on L):

| bar | ReBirth | ours | gap (dB) |
|---|---|---|---|
| 1 | -0.3, -10.1, -12.6 | +1.4, -6.9, -9.3 | 1.7, 3.2, 3.3 |
| 2 | +1.0, -7.6, -10.1 | +2.0, -5.8, -8.3 | 1.0, 1.8, 1.8 |
| 3 | +1.1, -7.2, -9.6 | +2.2, -5.4, -7.8 | 1.1, 1.8, 1.8 |

ReBirth's bar-0 hit is at sample 64 and bar 1's at 88,192, so bar 0's third echo
(64 + 3 x 29,400 = 88,264) lands 72 samples after bar 1's hit and partly cancels
it; ours lands on it at lag 0 and adds. A model settles the attribution: one
hit-plus-echo train built from ReBirth's own clean bar-0 hit and the echo gains
measured on it, one train per bar, reproduces all twelve of ReBirth's windows for
bars 0 to 3, exactly at the reading's 0.1 dB resolution, when placed at ReBirth's
onsets, and all twelve of ours when placed on the exact grid. dr_p0's bar 3 is the
same effect, 0.4, 0.8 and 0.8 dB (ReBirth +1.0, -7.7, -10.1; ours +1.4, -6.9,
-9.3): its echoes land on bar 2's fourth and fifth, and ReBirth's bar 3 sits 8
samples less than a bar after bar 2 (first written here as 7; the extra sample was
cross-correlation jitter). The same model at that spacing reads +0.9, -7.7 and
-10.1, and at a whole bar +1.4, -6.9 and -9.3, exactly ours. So the finding belongs
to hit timing, BACKLOG 20, at this size.

**MEASURED on the exports in hand, confirmation fixture to come: ReBirth's hits
sit on a 64-sample block grid.** Every onset, less the rimshot's own 3-sample
attack, is a multiple of 64: dl_ref 64, 88,192, 176,384, 264,576; dl_t090 (90 BPM)
128, 117,568, 235,136, 352,768; dl_t144 64, 73,472, 146,944, 220,480; dss_t123 64,
86,016, 172,096, 258,112; dss_t100 128, 105,792, 211,648, 317,504; dr_p0 as dl_ref
(its bar 3 reads 264,577, one sample of cross-correlation jitter). Bars 1 and later
sit at floor(k x bar / 64) x 64, 0 to 63 samples early against the exact grid: 17
of these 18 bars, the exception dl_t090's bar 2, whose exact time (235,200) is
itself a multiple of 64 and which lands one block earlier, likely a floating-point
floor. Bar 0 alone is LATE, by one block at 120, 123 and 144 BPM and by two at 90
and 100. The "66 to 192 samples early, not linear in the bar number" first recorded
here measured each bar against that late bar-0 hit, so it mixed the start delay
into the drift; the 120 BPM row it gave, -72, -80, -88, is exactly linear. Ours
puts every hit on the exact grid, first onset at sample 3. Not a delay property; a
rhythm-parity question for the transport work. BACKLOG 20.

## Proven 2026-10-01: the compressor sign-off proved the DSP, not its place in the engine

**Condition.** `fixtures-2026-10-01/comp-rides` (its `MANIFEST.md`): one 303
drone, channel fader 127, the compressor the only effect, 32 fixtures, exported
in Song mode to `renders/comp-rides-2026-10-01` and read per channel with
`tools/measure/comp_rides.py`. Ours with `REBORN_BASS_ENGINE=tpt`. Tables in
`PARITY.md`, "The compressor's rides and topology".

**What the 2026-09-17 sign-off proved, and what it could not.** Its verification
ran `comp_wav`: ReBirth's own uncompressed render fed straight through our
compressor DSP. That removes every source confound and proves the DSP, and it is
still right: on today's drone `comp_wav` gives -32.86 / -19.33 dBFS against
ReBirth's -32.21 / -18.75. But `comp_wav` feeds the DSP the OUTPUT level, with L
equal to R, on the master; it never runs the engine. So it could not see three
integration defects, each of which this probe found in the engine path:
- *A 2.85 dB master offset.* `renderSlice` ran the master compressor before the
  fixed 0.72 headroom, so it saw the bus 2.85 dB hotter than the output and the
  engine cut 2.99 dB more than `comp_wav` (-35.85 / -22.22).
- *A ~12.5 dB section miss.* The section compressor ran after the channel fader
  and at the bus level, where ReBirth's runs before the fader at the fader-127
  output level as one channel of its L+R detector: 12.54 dB low at ratio 127
  threshold 0, 7.08 dB at threshold 127, and the fader changed how hard it
  compressed. `BACKLOG.md` 16 had recorded the section path as never measured.
- *A pan-blind detector.* Ours took the louder channel squared; ReBirth's master
  detector sums L and R, reading a hard-left drone 5.73 to 5.92 dB lower than the
  same drone centred. Every calibration fixture was centred, and `comp_wav` feeds
  L = R, so nothing could tell the two apart.
And the COMP track's 156 rides were carried and not played (BACKLOG 5); ReBirth
plays them, each within 0.03 dB of its end setting's static export.

**Fixed** in four commits on `review/2026-09` (the rides played; the master
compressor fed bus x 0.72 with the fader's own gain after it; the detector
((l + r) / 2)^2, bit-identical for l == r; the section compressor run at the
fader-127 level before the fader), and a fifth that keeps the section
compressor running at fader 0 and on mute (it ran on silence there and the
section came back as a clipped burst). Every compressed cell is now within 0.66
dB of ReBirth but st_r064_t000 (+1.33 dB raw, +0.66 once `comp_wav` takes the
TPT drone's +0.94 dB level residual out of both sides). The calibration is untouched: no DSP constant moved, and every
centred render is byte-identical across the detector change.

**UNMEASURED.** The detector's signed sum against |l| + |r| (needs an antiphase
source). The channel fader's place relative to the distortion (BACKLOG 21).
ReBirth's ride glide (BACKLOG 18; its rides reach 90 percent in 15 to 25 ms,
ours 15 ms adding cut and 85 to 95 ms removing it).

**The rule this adds.** A `*_wav` harness proves an effect's DSP and nothing about
where the engine puts it, at what level, or what it does to a panned or ridden
signal. Verify a component through `reborn_render` on fixtures that vary its
neighbours (a fader, a pan, a ride) as well as through its harness. The
distortion was verified the same way (`dist_wav`), and its order against the
channel fader is the open question in BACKLOG 21.
