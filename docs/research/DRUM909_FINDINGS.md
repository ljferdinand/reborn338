# 909 parity findings

Measured against the isolated single-voice ReBirth exports (comp off, effect rack
off), power-averaged. Harness: `tools/measure/score909.py` (one method both sides,
`--synth` for the clean-room path), `tools/measure/fit_trims909.py` (trim fitter,
run LAST), `tools/rbsgen/gen_batch909complete.py` (81 fixtures), exports in
`renders/fixtures-2026-08-25/909complete/`.

> **Current as of 2026-08-25. Supertask 3 opened on branch `909-parity`; round 1
> below is the live state. Nothing is merged.** The 909 is a SAMPLE player like the
> 808, with 22 embedded recordings. Measurement uses `samples-standard-909/`
> (converted from the `Standard Components` mod, verified byte-identical to
> `samples-original/`; measurement only, never committed).

**The measurement trap carried over from the 808 applies here too:** coherent
averaging of step-locked hits cancels non-aligned tonal and HF content and reads it
far too quiet. `score909` power-averages from the start. Every pre-2026-08-25 909
number (the trims especially) was measured through the old confounded harness and
should be treated as suspect.

---

# Round 1 (2026-08-25): phase 1 and 2, and the structural gaps closed

Phase 1 traced every control through decode (`decode909Knobs`), the device
(`drum909.cpp`), the loader (`sampleset.cpp`) and the engine. Four controls were
being dropped entirely, three of them the same decode bug the 808 had.

## The dropped controls, all now plumbed

- **MT and HT Tune and Decay were dropped.** `decode909Knobs` read only the LT
  bytes (12/13) into a single shared `tomTune`/`tomDecay`, ignoring MT (15/16) and
  HT (18/19), and the device drove all three toms from one pair. Measured, LT
  tracked ReBirth exactly (70.0/91.5/123.8 Hz) while MT and HT sat frozen at their
  default pitch against ReBirth's 86->156 and 102->178. The LT match is what proves
  the varispeed law was already right and the fault was purely decode.
- **Ride Tune was dropped, and conflated with the crash.** Header **byte 28 is the
  ride tune** (MIDI CC 91): the shipped-song header scan shows bytes 25/27 tracking
  as the crash/ride LEVELS and 26/28 as their TUNES, a symmetric `CR[L,tune]
  RD[L,tune]` layout. Byte 28 was never read and the device had one `EX_CYM_TUNE`
  for both cymbals. ReBirth's ride sweeps 5087 -> 9362 Hz across the knob, nearly an
  octave; ours was frozen at 6960.
- **SD Tone was decoded and then ignored.** No device param existed. Ours read a
  flat centroid 5007 across the knob against ReBirth's 1570 -> 4211 -> 4335.

`DrumKnobs` gained `ltDecay/mtDecay/htDecay` and `rdTune`; the device gained
independent `EX_LT/MT/HT_TUNE` and `_DECAY`, a separate `EX_RD_TUNE`, and
`EX_SD_TONE`. After the fix HT tune matches exactly, MT tracks, all three tom
decays respond independently, and the ride tune sweeps with ReBirth.

## BD and SD Tune select a variant, and the ladder was INVERTED

ReBirth's BD and SD Tune pick a recorded variant, and the variants are an
**ascending** pitch/brightness ladder: bd1..4 at 54/54/59/65 Hz (centroid
54/56/61/74), sd1..5 at 124/145/178/215/242 Hz. Max tune picks the HIGHEST body.
The loader had it backwards ("max tune picks the LOWEST body"), so the kick and
snare ran backwards up the knob: `bd_tune` 0/64/127 read 70/54/59 Hz against
ReBirth's 54/59/65, and `sd_tune` barely moved (stuck ~183 against 124/178/242).

Flipping it makes **SD tune track ReBirth's ladder exactly** and BD track its
direction. Because the ladder carries the pitch, the extra BD/SD varispeed was
removed; it double-shifted. A small BD variant-boundary offset remains at the top
of the knob.

## The snare tone/snappy mix

ReBirth's Tone is a **body-versus-noise balance** and it SATURATES: at tone 0 the
body dominates (70% of the energy in 100-300 Hz), by tone 64 the noise does (40% in
3-8 kHz), and 64 -> 127 barely moves. Snappy sets the noise LEVEL and reaches a
**true zero** (ReBirth's snappy-0 snare is pure body, centroid 127 Hz); our 0.35
noise floor left it always noisy.

The loader pre-mix now crossfades body against noise on a saturating `sqrt(tone)`
curve with a true-zero snappy law. The snappy sweep lands centroid 126/2440/4919/6424
against ReBirth's 127/1966/4211/5951, snappy-0 exact.

## Trims

Re-fit by power averaging after the shape work. The old values carried the
coherent-averaging error in the same shape as the 808's: kick 13.1 dB quiet, crash
10.7 dB hot, ride 8.2 dB quiet. **Every voice now within 0.12 dB** of its export.

## Settled by measurement, needing no change

- **`909_bd0` is NOT the long-decay kick.** It is 1.918 s and very low (21.5 Hz
  dominant, centroid 15), against bd1-4 at ~0.5 s. The hypothesis was that it was
  the kick at maximum decay, but ReBirth's max-decay kick stays at 53.8 Hz, not
  21.5. Its role is still unexplained; it remains unloaded, which now looks correct.
- **The 909 same-step hi-hat already matches.** Manual p35: on the 909 (unlike the
  808) CH+OH on one step plays the FULL open hat. `d909_choh_same` measures dominant
  8166.5 on both sides, so our behaviour is right despite the code choking OH; worth
  a regression test rather than a change.
- RS, CP and CH needed only trims; their spectra already matched.

---

# Round 2 (2026-08-25): three ear corrections from Luke, and the ride explained

Each was found by WIDENING the measurement after the summary centroid/rolloff had
already "passed", which is the measurement-protocol rule and the third time in this project it
has paid. `tools/measure/banddiff.py` was added for it: per-band ratios plus the
decay envelope, both power-averaged.

## BD: ours sounded "clearer", ReBirth "high cut"

Ours decayed **about twice as fast** (-18.8 dB at 100 ms against ReBirth's -9.7),
and truncating a decay broadens the spectrum upward, so on the SAME sample we
carried +3 dB at 100-200 Hz and +5 dB above 3.5 kHz. That extra top is what read as
"clearer" and made ReBirth sound high-cut by comparison.

ReBirth's observed tau runs 0.0545/0.0614/0.0699/0.0801/0.0881 s across the decay
knob, and **at the top it equals the raw sample's own natural tau** (0.0882), so max
decay applies no envelope at all. Dividing the sample's own decay out leaves the
envelope's reciprocal remainder as a clean power law:

    1 / tau_env = 7.139 * (1 - decay)^1.287

which reproduces all five points to within 0.4 ms. This is the same
reciprocal-remainder structure the 808 kick needed, and the same lesson: **fit the
decay before the trim.** Every significant band is now within 0.4 dB.

The BD variant boundaries are also equal quarters (tune 96 already sits on bd4),
which rounding got wrong; the tune ladder is now exact at all five knob points.

## SD: tone 0 sounded muted and rang too long

Rather than fit by eye again, each export was decomposed onto the raw body and raw
noise recordings by **non-negative least squares** in the power domain, which gives
the gains ReBirth actually uses. That overturned the round-1 model:

    noise/body vs TONE   (snappy 64): .165 .369 .419 .427 .431
    noise/body vs SNAPPY (tone 64)  : .000 .203 .419 .464 .621
    body gain: CONSTANT at 0.253 everywhere

**Tone is not a body/noise crossfade.** It is a second noise-level control, and the
body must not be attenuated by it. Snappy reaches a true zero and also steps the
noise variant; Tone saturates, most of its travel done by knob 32. The raw body
carries **no high frequency at all** (0.00% above 1.2 kHz), so every bit of the
snare's brightness is the noise layer. A x1.38 term compensates the device's tanh,
which compresses the mix and lowers the effective noise-to-body ratio.

Tone now tracks 1361/3688/4104/4215 against 1570/3775/4211/4335, snappy
126/2319/4104/5681 against 127/1966/4211/5951.

## The ride "digital attenuation": ReBirth resamples with linear interpolation

Luke heard an artifact on all ReBirth rides and guessed a time stretch. It is a
**varispeed resample, not a time stretch**: across the tune knob the product of
sounding length and dominant frequency is constant (6346 / 6346 / 6353), which is
the signature of a tape-style speed change. Tune 64 is the natural rate.

Comparing ReBirth's render against three resamplers of the raw recording at the
same ratio identifies the method exactly. ReBirth matches **naive linear
interpolation** to within 0.1% in every band at both extremes, and is clearly
distinct from an ideal band-limited resample (which keeps more highs: 41.99% vs
36.75% at 6-10 kHz pitching down) and from drop-sample (which litters 18-22 kHz).
Linear interpolation has a sinc-squared roll-off and no anti-aliasing, and that
progressive high-frequency loss is the "digital attenuation" Luke hears.

Luke initially preferred our ride, and it is worth recording precisely why: the ride
he compared was the BROKEN one, whose tune never reached the device, so it played at
natural rate and never resampled at all. Now that the ride tune works, our playback
resamples with linear interpolation too (`renderSample`), so it carries the same
character and lands within ~1.8 dB of ReBirth per band.

**DECIDED 2026-08-25: we defer to ReBirth, as a matter of practice for the whole
project.** We keep linear interpolation, note the difference, and leave the window
open for a high-quality interpolator as an advanced/alternate setting. A better
interpolator would diverge from ReBirth on every repitched voice at every off-centre
tune setting across both drum machines, which is a large systematic divergence for a
small gain. See `docs/research/OPEN_DECISIONS.md`, which records this as the general rule for
any future case of the same shape.

## Crash and ride carried a spurious decay envelope

Following the ride investigation: the 909 panel has **no crash or ride decay
control** (MIDI map p197 lists CC 88/89 as CC Level/Tune and 90/91 as RC Level/Tune,
and the song header carries no decay byte for either), yet the sample path applied
an envelope to both. ReBirth plays the recordings to their natural length: its ride
ends at 0.916 s, exactly the raw sample's own 0.916 s. Ours truncated the ride to
0.548 s and the crash to 0.789 s.

Removed, and both now land within 1 ms of ReBirth (0.915 vs 0.916, 0.906 vs 0.907).
This is the third instance of the same defect class in this project, after the 808's
spurious tom and closed-hat envelopes: **an envelope with no knob behind it.** Worth
checking for by default on any remaining voice. The synth path keeps its envelopes,
since a synthesised voice needs one to end at all.

The same investigation gave the exact cymbal tune span: crash and ride both sweep
-5.34 to +5.22 semitones (10.56 total, identical for both), against the 9.0 we had.

---

# Round 3 (2026-08-25): the remaining controls, all measured

## One law behind every decay knob

Measuring the leftover decay sweeps turned up a single structure. **At its maximum
every 909 decay knob applies NO envelope**: at knob 127 ReBirth's observed tau equals
the raw sample's own natural tau to within 0.1 ms on all of CH (.0390/.0390), OH
(.1296/.1297), LT (.1491/.1492), MT (.0899/.0900) and HT (.0984/.0984), exactly as
the kick already did. Below that, dividing the sample's own decay out leaves an
envelope whose reciprocal is a power law in (1 - knob):

    1 / tau_env = A * (1 - knob)^p

The laws this replaced were plain exponentials, all far too fast at the bottom (our
CH ran 0.0053 s against ReBirth's 0.0120, our OH 0.0104 against 0.0678) and none
reaching natural length at the top. All six knobs now agree within about 3 ms.

Two notes. The theoretical reciprocal sum under-predicts slightly because the
samples' own envelopes are not pure exponentials, so each constant was corrected
against the RENDERED result rather than left at its offline value. And the three toms
share one knob law: fitted separately they give A 6.72/6.59/6.84 from only three
sweep points each, so one shared law is better supported than three overfitted ones,
their different lengths coming from their own samples.

## BD Attack was dead on the sample path, and is a lowpass

Only the synthesis branch read `bdAttack_`, so on the parity path CC 69 did nothing.

Working out what it does took two wrong models. A normalised band mix shows nothing
(the 1-4 kHz share reads 0.000% at every setting) because the transient carries
almost no energy against the long low body, the same trap as the 808 kick tone.
Absolute onset energy shows 1-8 kHz rising 9.6 dB across the knob, which suggests an
added click, but the raw recording's own onset already carries that content.
Comparing the 1-8 kHz band against the 100-1000 band, so the reading is
level-independent, settles it: the raw sample's onset ratio is -13.2 dB, ReBirth at
attack 127 reads -13.1, and at attack 0 it reads -21.7. **The knob SOFTENS the
existing transient rather than adding one.**

A pure amplitude fade-in cannot reproduce that: it attenuates every frequency equally
at each instant, so it moved the peak correctly and barely touched the ratio. A
one-pole lowpass whose corner opens with the knob does both, 700 Hz at attack 0
rising by a factor of 30 to passthrough at the top. Ratio now within 1.4 dB and peak
within 0.45 dB.

## Accent is exactly linear, and the knob alone does nothing

Fixtures put accented and unaccented hits inside the same render so their ratio needs
no level matching. ReBirth's lift is -2.24 / +3.78 / +7.31 / +9.81 / +11.69 dB across
the knob, which as a linear gain is 0.773 / 1.545 / 2.320 / 3.094 / 3.841, successive
differences 0.773, 0.775, 0.774, 0.748. So:

    accented gain = 0.773 + 3.070 * knob        (unaccented = 1.0, never moves)

reproducing all five points to three decimals. **At the bottom of the knob an
accented cell is 2.24 dB QUIETER than a normal one**, so accent is a separate
velocity level rather than "normal plus a boost", overtaking normal only around knob
9. And the knob alone does nothing: with the row clear every step reads -29.84 dB at
both extremes. The law this replaced was three times too shallow. Now within 0.31 dB.

## Flam: byte 29 is the amount, cell value 4 is the marker

The last unmeasured 909 control, and it took two rounds to pin because the obvious
test fails. **Sweeping header byte 29 on its own changes nothing at all**: five
exports across its full range are identical to 0.01 dB in whole-file RMS, on-step
energy and pre-step energy. That refuted the byte-29 hypothesis as stated, and is
the reason it was worth exporting rather than writing the correlation into the
decoder.

The accent result measured the same day says why. Accent's knob also does nothing
alone; it only scales steps whose row is marked. Flam behaves identically. The drum
cell values are 1 soft, 2 normal, 3 accent, and **4 is flam**: patching the kick's
cells from 2 to 4 makes byte 29 live, moving whole-file RMS 2.7 dB between the knob
extremes while cells of value 2 ignore it completely.

What the flam actually is, from the averaged onset envelope over the 16 step-locked
hits: the first 50 ms are bit-for-bit the shape of a normal hit, and then a SECOND
hit appears. At knob 127 it peaks **63.7 ms after the step, which is 1.018 of a
32nd note** at this tempo, so the grace note lands a 32nd after the main hit rather
than before it. **CORRECTED by a full sweep.** The first reading, from knob 0 and 127 alone, said
the knob set the grace note's LEVEL at a fixed 32nd-note spacing. Exporting the
intermediate values shows the opposite: the knob sets the GAP, which grows
monotonically 15 / 20 / 28 / 29 / 51 / 66 ms across knobs 16 / 32 / 48 / 64 / 96 /
127, fitting gap = 5.2 + 59.3 * knob ms and reaching one 32nd note at the top. The
level reading was an artifact of measuring |x| on a 54 Hz kick, whose own carrier
ripples every 9 ms and swamped the envelope; smoothing over the carrier period
makes the trend obvious. Two lessons already on the record repeated here: two
points cannot distinguish two laws, and the metric has to outlast the material.

So the model is: **a flam step fires the voice twice, the second after gap =
knob x one 32nd note.** Because this is a sample player the second trigger restarts
the voice, which is why a small gap sounds like one thickened hit rather than two,
and why the total energy rises with the knob as the hits separate.

IMPLEMENTED 2026-08-25. `DrumStep` carries a flam bitmask, `DrumKnobs` the amount;
the engine schedules the repeat on its own pending slot, separate from the shuffle
delay because a step can be both shuffled and flammed and only the flam voices
repeat. Our whole-file level tracks ReBirth within 0.3 dB across the knob, which is
the telling check because energy is gap-sensitive.

Two bugs found implementing it. The decoder's accent test was `v >= 3`, so a flam
cell read as an accent too. And the WRITER never wrote the mid or hi tom Tune and
Decay, the ride Tune, or the flam amount, and flattened a flam cell back to a plain
accent, so a save silently discarded four controls the loader read correctly. A
round-trip test now pins all of them, verified to fail against the old writer. Byte 30, for the record, is not a control at all: it mirrors bytes 25
and 27 (the crash and ride levels) exactly in every one of the 16 shipped songs.

## CORRECTION: what the 909 cell values actually mean

Luke's ear on the flam song caught this: ReBirth's KICK sounded doubled where ours
sounded like nothing was applied, even though the kick carried no flam marker in
the pattern. Measured, ReBirth's kick had a transient 47 ms after the step, which
is exactly the flam gap at that song's knob setting. So ReBirth was flamming a cell
we thought was an accent.

Isolating each factor against ReBirth, one variable at a time, settles it:

| column 0 | cell | level vs normal | second transient |
|---|---|---|---|
| 0 | 2 | +0.00 | no |
| **1** | 2 | **+10.07** | no |
| 0 | **3** | -1.66 | **yes, at the flam gap** |
| 0 | 4 | +1.11 | yes |
| 1 | 3 | +8.41 | yes |

So **ACCENT is column 0 on BOTH machines**, exactly as on the 808, and the 909's
CELL VALUE is a separate axis: **1 soft, 2 normal, 3 and up FLAM**. The earlier
reading, that 3 was accent and 4 was flam, was wrong in both halves.

Two consequences, both material.

**The accent law was confounded and is re-measured.** The accent fixtures set the
accent row AND cell value 3 together, so every "accented" hit was also flammed and
the flam's retrigger was folded into the law. That is what produced the nonsense of
an accented hit reading QUIETER than a normal one at knob 0. With the cells left at
normal, the measured gains are 0.999 / 1.999 / 2.999 / 3.998 / 4.967 across the
knob: exactly linear through 1.0 at knob 0, as it must be.

    accented gain = 1 + 3.967 * knob        (was 0.773 + 3.070 * knob)

The 808's independently measured depth is 3.95, so **both machines share one accent
law**, which the manual's "equal effect on all sounds" already implied and which is
a good cross-check that this reading is the right one.

**Fourteen of the sixteen shipped songs were affected.** Their 909 parts carry 2423
cells of value 1 and 281 of value 3. We were playing every soft hit at full velocity
and every flam as an accent. Soft now plays at 0.40 (measured -7.96 dB, ours -7.94)
and a flam cell at 0.637 per hit (measured -1.66 dB total, ours -1.63).

Accent now tracks ReBirth within 0.5 dB across the knob. Cell value 4 remains a
louder flam variant we decode but do not emit; no shipped song uses it, and ours
reads 2.7 dB under ReBirth's there, an accepted residual.

## Still open

- Flam is **IMPLEMENTED** (2026-08-25). See the flam section above.
- SD tone is latched into the pre-mix at load and so cannot vary per hit, the same
  latching residual the 808 has.
- The closed hat's mid-knob decay runs about 3 ms long, its sample envelope not being
  exponential enough for any single-exponential fit to follow exactly.
- `909_bd0`, the 1.918 s very low kick recording, is still never loaded and its role
  is still unexplained. It is NOT the long-decay kick: ReBirth's max-decay kick stays
  at 53.8 Hz rather than bd0's 21.5 Hz.
- **Nothing has been checked by ear yet.** That is the real gate.

---

## The synthesis pass (2026-08-26)

The 909 supertask calibrated the SAMPLE path and stopped. But a clean install
has no ReBirth mods to load samples from, so the synthesis model is what ships,
and on the existing 81 fixture exports it was 4 to 17 dB out on every voice.

**A correction, added 2026-08-27.** An earlier draft of this section said the
synthesis path "had never been measured against anything", and that is wrong.
The git history says otherwise: these voices were written on 2026-07-15
(`7a9ffe8`) and tuned against references the next day, in `e881b8e`
"Sample playback + reference-driven tuning round 1" and `b9eaf15`
"Solo-reference tuning round 2: per-device A/B against original exports". The
"Reference-tuned" comments scattered through `drum909.cpp` date from exactly
there.

What is true is narrower, and is the same story the 808 went through: it was
calibrated in July and then **left behind**. Three things happened after.

- **The method changed underneath it.** Those rounds predate the isolated-fixture
  discipline, which begins 2026-07-23. They were solo and whole-mix comparisons,
  which FINDINGS_AUDIT.md records as confounded because the master compressor was
  on and the effects were unwired. They also predate the coherent-averaging
  discovery, which mis-set every 808 trim.
- **Supertask 3 went to the sample path only**, as its own merge title says
  (`82e4ebf` "Merge 909-parity: the TR-909 sample path"). Every fixture-based
  finding of that round (the decay laws, the un-inverted tune ladders, the
  accent law, the four dropped controls) landed there. `score909.py` carried a
  `--synth` flag from the day it was written and defaults to samples; nothing in
  the supertask used it.
- **One trim table served both paths.** `kTrim909` was shared, so the synthesis
  path's per-voice balance was literally the sample path's numbers. The 808 has
  two tables because it got its own synth pass in August; the 909 did not.

The mechanical confirmation: `git log -S` on the synth tom bases (95/140/200) and
on the shared metallic bank returns exactly two commits, 2026-07-15 and the pass
below. **Frozen for six weeks while everything around them moved.** The merged
commit messages for this work carry the same loose "never calibrated" phrasing;
they are left as they stand rather than rewriting a merge that has been signed
off, so this note is the correction of record.

### Pitch needs instantaneous frequency, not a windowed FFT

At 90 Hz a 40 ms FFT window holds under four cycles and its peak wanders by
hundreds of cents, which is why the tom and kick pitch laws had never been
fitted. `tools/measure/pitch909.py` band-passes around the partial, takes the
analytic signal and differentiates the unwrapped phase, which stays stable down
to about two cycles.

One trap it introduces: the estimate is an average over its own window, so a
fast sweep reads far shallower than it is. Fitting the kick's sweep to the
smoothed numbers as if they were instantaneous underestimated its onset
threefold. The fit has to model the window.

**Toms.** All three span 10.1 semitones with knob 64 exactly halfway, settled
pitches fitting `base * 2^((knob-0.5)*span/12)` to within 7 cents for LT and MT:

| tom | knob 0 | knob 64 | knob 127 | base | span |
|---|---|---|---|---|---|
| LT | 66.28 | 85.46 | 111.15 | 85.65 | 8.95 st |
| MT | 83.00 | 109.30 | 142.96 | 108.98 | 9.41 st |
| HT | 95.10 | 119.93 | 159.55 | 122.00 | 8.96 st |

The sweep is 245 cents deep, not the 556 we had.

**Kick: the tune knob does not change the note.** ReBirth's kick rests at 54 Hz
at every setting; tune sets how far above that the sweep starts and how long it
takes to arrive. It also steps in four, because the loader picks one of four
recorded variants, and knobs 96 and 127 measure byte-identical. Fitted: one
shared depth of 3.396 with tau stepping 7.4 / 15.6 / 23.1 / 37.6 ms.

### The metallic bank could not have worked

Every metallic voice ran one shared six-square bank at 245 to 337 Hz. ReBirth's
partials sit at 3.5 to 4.8 kHz and differ per voice:

    CH  4770 5507 5620 6481 7617 8166      CR  3521 4027 5012 5905 6804 8909
    OH  3779 3838 5566 5663 6864 7709      RD  3704 6928 6944 7424 9044 9458

Nothing in the old bank landed near any of them; what reached the top was the
SQUARES' odd harmonics, which is why the crash's centroid was 12.5 kHz against
ReBirth's 6.2. Replaced with per-voice sine partial banks at the measured
frequencies plus a broadband wash, band-limited to ReBirth's own range. Six
sines alone are six lines with silence between them, and a real cymbal is those
partials sitting in a wash, so the noise term is not optional.

The rim was the same error one step down: it resonated at 480 and 1030 Hz,
ReBirth's second and third partials, with the 221 Hz fundamental missing.

### And a third saturator

The synth branch ran every voice through `softClipUnity(o * s.vel)`, which the
sample branch does not have and ReBirth does not have anywhere. The isolated
fixtures run at accent 0, where it costs 0.6 dB, and the trims had absorbed
that. On real material it cost 4.65 dB, because Trakka Rekka accents nearly
every kick and drives `s.vel` to 1.76.

Accent no longer brightens a voice either. Measured on ReBirth: comparing the
band profile of accented against unaccented hits inside one render, every band
from 20 Hz to 8 kHz moves under 0.6 dB and by the same amount at knob 0, 64 and
127. It has to be pure gain, because ReBirth is playing one recording louder and
a recording cannot brighten.

### Result

Every isolated voice within about 1 dB with centroid and rolloff matching, and
`kTrimSyn909` converges within 0.01 dB. On Trakka Rekka's 909, synthesis only:
level -4.28 dB to +1.21, envelope correlation 0.893 to 0.984.

**Open**: in the mix the synthesis carries about 9 points too much 100-300 Hz
where the sample path carries 1.6, even though every isolated voice is within
2 dB on that song. The excess is not the kick, which matches its own export
within 1.7 dB per band. Suspect the voices not isolated by the one-voice
fixtures (rim, clap, hats) at the song's own levels.

### Second pass: punch and tuning were the same bug (2026-08-26)

The listening note after the first synth pass was that ReBirth's voices are
punchier and ours sound slightly out of tune. Those are one fault.

**Punch is crest factor**, peak against RMS inside a hit. Ours was flat on every
voice, worst on the snare (8.2 against ReBirth's 16.4) and the toms (5.9 to 7.1
against 13.4 to 13.8). The cause: the synthesis decay laws ran 2 to 5 times too
long. (An earlier draft called them "never fitted, only invented". Not quite: the
kick's decay law was set during the July reference round, `e881b8e`, and the tom
expression was edited on 2026-08-25 when the dropped MT and HT decay controls
were wired. What is accurate is that none of them had ever been fitted against a
MEASURED decay from an isolated export, which is what this pass did.) The trims were then fitted to total
energy, which pays for a long decay with a low peak. The project's own rule is
**fit decay before trim**, and this pass had gone straight to trims.

**It is also the tuning complaint.** A long decay puts a drum's loudness after
its pitch sweep rather than during it, so the loudness-weighted pitch reads flat
even when the settled pitch is right. The toms measured 75 to 272 cents flat
while settling within 60. Fixing the decays moved them to mostly inside 90 cents
with no change to the pitch law at all.

Measured decay laws, from ReBirth's own observed amplitude tau:

| voice | law | what it replaced |
|---|---|---|
| BD | 0.0546·e^(0.526k) | 0.012 + k²·0.22, 4.5× too short at k=0 |
| LT | 0.0687·e^(0.663k) | one shared invented law, |
| MT | 0.0544·e^(0.501k) | 2.3 to 4.3× too long |
| HT | 0.0562·e^(0.487k) | |
| CH | 0.0116·e^(1.324k) | already close, hence its healthy crest |
| OH | 0.0666·e^(0.707k) | up to 2.5× too long |
| CP | 0.072 | 0.130 |
| CR | 0.33 | 1.1 to 2.1 s |
| RD | 0.23 | 0.55 to 1.45 s |

Unlike the sample path, the three toms do **not** share one law. The samples
carry their own lengths; a synthesised tom has to be told.

**The snare's two knobs were driving the wrong things.** The 2×2×2 corner set
separates them: tau is 0.033 s at every corner except tone and snappy both up,
where it reaches 0.079, and tune does nothing (0.0331 against 0.0333). So snappy
sets the noise LEVEL and tone sets its LENGTH, an interaction and not a sum: with
no noise there is nothing for tone to lengthen. We had the body driven by tune,
which has no effect, and the noise tail driven by snappy at up to 255 ms against
a measured 85. Tone was not read on this path at all.

**The kick's saturation was too heavy.** Against ReBirth's harmonic profile,
measured relative to its own 40-80 Hz fundamental band so a level offset cannot
hide in it, a drive of 1.55 put the third harmonic 14 dB over ReBirth's at tune 0.
That lands at 162 Hz, which is why the kick's loudness-weighted pitch read about
250 cents sharp. Fitted to 0.85 plus a little even-order content, because a
saturated sine makes odd harmonics only and ReBirth's kick carries a strong second.

New tool `tools/measure/decay909.py`: envelope from the analytic signal,
power-averaged across hits, fitted between −6 and −26 dB below peak. A 5 ms power
window aliases against a 91 Hz tom and yields a bouncing envelope that is not
real; this is the same class of trap as coherent averaging.

**Residual, honest.** Our kick carries about 2 dB less 20-100 Hz and 1.5 dB more
100-300 than ReBirth's. The kick is nearly the whole mix in Trakka Rekka, so the
band-share table renders that as 19 percentage points, which overstates it: a
share near 50% amplifies a small dB difference. A saturated sine plus a click is
not a recording of a drum, and closing the last few dB wants a richer body model
rather than another constant.

### Third pass: the tuning was mostly the snare (2026-08-26)

Punch judged good after the second pass; the remaining note was that the tuning
still sounded off. One voice carried nearly all of it.

**The snare was four to five semitones sharp.** Its body is a fundamental with
one partial above it, and the measurement is unambiguous across the tune sweep:

| tune | f1 | f2/f1 | f2 level |
|---|---|---|---|
| 0 | 124 | 1.347 | −7.7 dB |
| 32 | 145 | 1.372 | −6.9 |
| 64 | 178 | 1.360 | −7.2 |
| 96 | 215 | 1.353 | −8.1 |
| 127 | 242 | 1.355 | −8.9 |

So a partial at **1.358×, 7.8 dB down**, over a fundamental spanning **11.97
semitones** from a base of 175.22 Hz. Ours had the partial at 1.784× and only
1.7 dB down, which is a loud tone a tritone above the fundamental, and it dragged
the perceived pitch with it: 662 cents sharp at tune 0, 455 at tune 64. The tune
range was 7.2 semitones, so the error closed to nothing at the top of the knob
and was worst at the bottom.

One subtlety worth keeping: setting the pre-saturation weights to the measured
−7.8 dB left the partial 2.3 dB light in the output, because the body's `tanh`
eats more of the weaker tone. The weight is set to −5.7 dB so the result measures
right. **Fit what comes out, not what goes in.**

**The other two were stale fits.** The mid and hi tom ran 30 to 50 cents sharp
and the kick lingered in its pitch sweep, 73 to 90 cents sharp through the middle
of it. Both had been fitted while the decay laws were still 2 to 4 times too
long, which weights a pitch measurement toward the settled tail rather than the
part that is actually loud. **A pitch fit is only as good as the envelope
underneath it**, so pitch has to be refitted after decay changes, not before.

Every voice's dominant now matches ReBirth's, the snare's exactly at tune 0, 32
and 64. On whole songs, 909 only: Trakka Rekka within 0.04 dB at 0.975 envelope
correlation, G-Spot within 0.49 dB at 0.953.

**Open, and worth an ear rather than a metric.** Our peaks run hotter than
ReBirth's on both songs, 2.4 dB on Trakka Rekka and 5.0 on G-Spot, while the
isolated voices' crest factors match within 2 dB. The likely cause is that our
synthesised voices have deterministic phase, so simultaneous hits sum coherently
where ReBirth's samples do not. It would show up as the kit sounding harder in a
mix than it does one voice at a time.
