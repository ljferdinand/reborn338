# Decisions: open forks, and settled calls kept for the trail

Choices that are not urgent, are not obvious, and should be made deliberately
rather than by whichever code happens to get written first. Each entry states
the question, what is actually at stake, and what would settle it. Nothing here
is a task; these are forks in the road.

When a fork is decided, move it down into "Resolved calls" at the foot of this
file rather than deleting it, and record the decision wherever it belongs.

---

# Open forks

## Reproduce ReBirth's defects, or improve on them? (DECIDED 2026-08-25)

**Luke's ruling, and it is a matter of PRACTICE for the whole project, not a
one-off:** where ReBirth does something measurably worse than we could,
**we defer to ReBirth.** We note the difference, and we leave a window of
opportunity for it to become an advanced or alternate setting later.

**What raised it.** ReBirth repitches its sampled drum voices by plain varispeed
with **naive linear interpolation** and no anti-aliasing (identified 2026-08-25 to
within 0.1% per band, see `docs/research/DRUM909_FINDINGS.md` round 2). Linear interpolation
has a sinc-squared roll-off, so every voice played off its natural rate loses highs;
Luke heard it on the 909 ride as a "digital attenuation" and initially preferred
ours, which at the time only sounded cleaner because its tune knob was broken and it
never resampled at all.

**Why defer.** `docs/research/GOALS.md` already ranks one-to-one parity above sounding
better, and this is exactly the case that tests it. A higher-quality interpolator
(cubic or windowed-sinc) would preserve the highs, but it would diverge from ReBirth
on EVERY repitched voice at once, at every off-centre tune setting, across both drum
machines. That is a large, silent, systematic divergence bought for a small gain,
and it is not what the project is for.

**The window.** Interpolation quality is a natural candidate for the advanced
settings surface decided below: a "sample interpolation: ReBirth (linear) /
high quality" switch, defaulting to ReBirth. Any future case of this shape gets the
same treatment, so the default build stays faithful and the alternative stays
available rather than lost.

**Scope note.** Reproducing a defect is not the same as keeping our own bugs. A
spurious envelope with no knob behind it, a dropped decode byte or an inverted
ladder is OUR error and gets fixed; this rule is only about faithfully following
ReBirth where ReBirth itself is imperfect.

## Advanced settings surface: the non-ReBirth knobs (DECIDED 2026-08-24, build pending)

**Decision (Luke, 2026-08-24).** The synth path carries four controls ReBirth's
808 does not have (`tom_decay`, `ch_decay`, `cp_spread`, `cb_tune`, the
`EX_*` extras in `src/devices/drum808.cpp`). Rather than hard-code them at their
fitted values or delete them, surface them in an **"advanced settings"** area,
reachable **both** by an on-panel switch/toggle **and** by a menu option. This
area also hosts other configuration, still to be defined.

**Still to define before building.** The rest of what "advanced settings" holds
(the "other config tbd"). The four 808 knobs are the first tenants, not the whole
list. **Second tenant, added 2026-08-25:** a sample-interpolation switch, ReBirth
(linear, the default and the parity path) versus high quality, per the
defer-to-ReBirth ruling above.

**Where it touches.** GUI only: `app/src/PluginEditor.{h,cpp}` for the toggle, the
menu entry, and the panel; the knobs already exist in the device and its param
list, so no core change. Note the standing gotcha: the GUI has zero headless test
coverage, so any `PluginEditor` change must be verified by launching
`build-app/RebornApp_artefacts/Release/Standalone/Reborn 338.exe` and confirming it
opens (a null-deref once survived a whole fork because the app was never run). Hold
the build until the rest of the config list is defined, or start with just the four
knobs on Luke's go.

## Sample sets: adopt `.rbm`, or define our own pack format?

**Raised 2026-07-26 by Luke.**

ReBirth's drum sounds come from 46 AIFF recordings embedded in `ReBirth.dll`,
and a `.rbm` mod replaces them with its own. We already read `.rbm` well enough
to pull the voices out, and songs name the mod they were written against, which
is how a 1998 song still resolves the sounds its author intended. See
`docs/FORMAT_RBM.md` and `docs/REBIRTH_CHARACTER.md`.

**The question.** Do we treat `.rbm` as the sample-set format for Reborn, or
define our own archive and treat `.rbm` as an import path?

**What is at stake.**

Using `.rbm` directly means every mod ever released works without conversion,
which is a real preservation win and squarely on-mission. It costs us a format
we do not control, that carries artwork and layout we may not use, whose sprite
payloads we still cannot decode, and that has no room for anything ReBirth did
not have.

Defining our own means we can carry what `.rbm` cannot: the variant ladders that
the drum machines actually need (the 808 snare's five brightness layers, the
909's five-step pitch ladder and its noise layers), per-voice metadata, sample
rates other than the original, stereo sources, licensing and attribution, and a
loop-point or round-robin scheme. It costs a converter, a spec to maintain, and
the risk of inventing a format nobody else writes.

**A third option worth weighing:** ship both, with `.rbm` as a first-class
import that is converted on load, and our own format as what the app writes.
That is close to what `.rbsx` already does for songs, where `.rbs` is a
supported but lossy export and `.rbsx` is native. The precedent argues for it.

**What would settle it.** Mostly the variant question. If matching ReBirth's
drum machines requires per-voice variant ladders as first-class data, and the
evidence so far says it does, then `.rbm` cannot express what we need and the
decision makes itself. Supertasks 2 and 3 will answer that as a side effect, so
this is best decided after the 808 and 909 are understood rather than before.

**Do not decide this early.** There is no cost to waiting and a real cost to
guessing wrong.

---

# Resolved calls

Kept for the reasoning trail. Both of these were live decisions on 2026-07-31
and both settled inside that same day, which is why the file that held them
(`DECISIONS_PENDING.md`) was folded in here on 2026-08-04 and removed: it was
titled for pending decisions and had none left. Neither is waiting on anyone.

The one live thread either of them left, the level-matched drive rerun, moved to
`PLAN_303_ENVELOPE.md` as an open item. Section 2's conclusion that the skirt is
constants rather than topology is now stated canonically in that file's section
0, alongside the separate question about resonant-peak prominence that it does
NOT settle.

## 1. The drive shaper: the EAR CHECK WAS RUN, and it went against the change

**Updated 2026-07-31. Luke listened to all three cases asked for below, plus a
fourth. Drive ON won all four by a substantial margin.** His words on the set:
they all sound considerably closer to ReBirth than at the start of the round, with
a "snap" in ReBirth still being reached for.

A later Clodhopper-specific test, on a single-303 export with the effects off, had
drive LOW sounding texturally more akin to ReBirth, but so much quieter that he
could not judge it reliably. That reading is confounded by level and does not
overturn the first result. **The clean follow-up is a level-matched rerun**, and
until that happens `satAsym()` stays in.

The measurement case for removal has separately been voided: it rested on a filter
fit scored with the harmonic instrument that was found to be miscounting on
2026-07-31, so the table below is not currently evidence for anything. Both sides
of this argument need regenerating before it is reopened.

What survives untouched is the linearity proof itself, because it is a ratio over
twelve harmonics and does not read above the instrument's crossover: ReBirth's 303
path IS linear to 0.03 dB. That is a real constraint on any explanation, and it
sits awkwardly with the ear result rather than being settled by it.

---

<details>
<summary>The original section 1, written 2026-07-31 before the ear check</summary>

### What the change is

`satAsym(x, bias) = tanh(x + bias) - tanh(bias)` is an asymmetric saturator
sitting between the oscillator and the filter. It was fitted in 2026-07-26
against the `detroit_303` reference to add harmonic richness. The proposal is to
take it out of the signal path, refitting the filter alongside it in the same
pass rather than after.

The interim form measured so far scales its input to 0.25 rather than removing
it. That is a probe, not the shipping form: 0.25 is where the sweep flattens
against the noise, not a claim that a quarter is correct. Removing it properly
is the follow-up either way.

### Why it looks right

ReBirth's 303 path is linear to 0.03 dB. That is not an inference. Dividing the
square oscillator's harmonic series by the saw's cancels every linear stage the
two share, leaving a function of pulse duty alone, and ReBirth fits it to 0.03 dB
over twelve harmonics at three cutoffs with the same duty every time. A
waveshaper anywhere in its path would break that cancellation.

The two instruments disagreed at a fixed filter, which is what forced the joint
fit. Lowering the drive reproduces ReBirth's linearity and monotonically wrecks
the filter fit, running 2.31 dB of linearity residual down to 0.07 while the
filter curve goes 2.54 up to 4.24. That is the signature of one parameter
standing in for another's error: the shaper's added harmonics were filling in a
filter that falls off too steeply above the corner.

Refitting the filter with the drive pinned low answers it. Coordinate descent
gives `DRIVE 0.25, CUT_SCALE 0.95, CUT_SLOPE 2.8, RESO_OCT 0.325,
RESO_OCT_CUT 0.425, K_B 2.80, HFQ_ON 4000`:

| | drive 1.0, refitted | drive 0.25, refitted | ReBirth |
|---|---|---|---|
| filter curve, 2151 points | 2.543 | **2.276** | 0 |
| peak frequency, oct rms | 0.145 | **0.139** | 0 |
| peak prominence, dB rms | 3.20 | **2.43** | 0 |
| linearity residual, dB | 2.31 | **0.09** | 0.03 |
| oscillator gain, square vs saw | 0.5393 | **0.5497** | 0.5493 |
| fitted pulse duty | 0.52422 | **0.52300** | 0.52286 |
| whistle articulation, dB | -23.4 | **-28.6** | -29.4 |

It wins on every instrument at once, which is the test it had to pass. Two of
those were not fitted for and are worth the emphasis. The oscillator now matches
ReBirth on duty, level and linearity simultaneously, which the waveform round
said would only happen if the path were linear. And the whistle guard, which has
sat about 6 dB shallower than ReBirth through every previous round and was
recorded as a mild over-resonance to watch, closes to 0.8 dB.

### Why it still needs ears

Every number above is a spectral or level statistic on a held sawtooth at note
A. None of them is a judgement about whether the thing sounds like a 303. The
shaper was originally added because Luke described ReBirth's `detroit_303` as
"buzzy, farty" and ours as "a far cry" from it, and that was an ear judgement
that no metric in this project has ever captured. Taking the shaper out could
measure better on six instruments and still lose the character it was reaching
for.

There is also a specific reason for suspicion. The old fit was made against
`detroit_303` with a recorded caveat that the distortion effect was not matched
on both sides of the comparison. If that confound inflated the case for the
shaper, removing it is right. If the shaper was independently doing something
audible, the confound does not make it wrong.

### What is needed

Listen to an A/B of the same songs at the current drive and at the refitted low
drive, and say which is closer to ReBirth. Specifically:

1. **`detroit_303`**, because it is the patch the shaper was fitted against and
   the one Luke has characterised in words.
2. **Clodhopper**, because its bassline is the max-resonance buzzy case.
3. **One clean patch** where the shaper should be doing little, as a control.

If the low-drive build sounds equal or better, the change proceeds and
`satAsym()` comes out properly rather than being scaled down. If it sounds
thinner, the finding is that ReBirth's linearity at the oscillator is real but
something else in our path is supplying the missing harmonics, and the next
question is where.

**Blocked on nothing.** The renders can be produced in about a minute now.

</details>

---

## 2. The skirt: the DIRECTION CALL IS WITHDRAWN, and needs no replacement

**Updated 2026-07-31, after the instrument was calibrated. Everything below this
box was written from measurements that are now retracted. It is kept because the
reasoning was sound given what was believed, and because the shape of how it went
wrong is worth being able to re-read.**

Luke's answer to this was "change the filter order or the stopband shape". That
answer was correct for the question as it was put, and the question was wrong.

Two retractions landed on this, in order. First the resonance dependence went:
`skirt_knob.py` anchors the analysis band to the cutoff knob rather than to a
measured peak, and the shallowing turned out to be the band walking up the
spectrum as ReBirth's peak rose. Then the magnitude went: `find_f0()` was locating
the fundamental to only half an FFT bin, the error multiplies by harmonic number
while the search window does not, and every slope measured above harmonic 15 came
back too steep on both sides. See the audit entries for both.

What is left after both. Our skirt is steeper than ReBirth's by **1.1 dB/oct at
cutoff 110 and 1.8 at cutoff 127**, not the 4.2 and 3.4 that motivated this call.
ReBirth reads -18.0 to -19.6 dB/oct over 1.7x to 4x the corner, and an ideal
4-pole reads -20.7. **ReBirth is a four-pole ladder.** In the one band at cutoff
64 that sits above the resonant peak and clear of the dither floor, the difference
between us is -0.06 dB/oct.

**There is no topology question here.** A 1 to 2 dB/oct residual against a
structure that is now confirmed to be the same structure is a constants problem,
and the constants have never been fitted with a working instrument. Nothing about
the filter order or the stopband shape should change on this evidence.

The `REBORN_SKIRT_ZERO` probe stays default-off and unfitted. It was built to
supply a resonance dependence that does not exist, and the second retraction does
not bring it back.

**What this needs from Luke: nothing.** The next step is a refit, which is
measurement work. The two alternative cores behind `REBORN_FILTER` do not need
scoring for this reason any more, though scoring them remains cheap if the refit
stalls.

---

<details>
<summary>The original section 2, written 2026-07-31 before either retraction</summary>

### What is measured

Above the corner, our filter's skirt is **steeper than ReBirth's in all sixteen
cells of the resonance sweep, without exception**. That part needs no law to be
true and no estimator to be trusted, because it is a sign, not a magnitude.

The magnitude, where it can be measured reliably, is this. At cutoff 110, where
two independently exported batches agree to 1.2 dB/oct, **ReBirth's skirt
shallows as resonance rises**, running about -34 dB/oct at resonance 60 to -29.5
at 127. Ours does not: it sits near -34 to -35 across the whole knob and if
anything steepens slightly. The difference drifts from -0.9 dB/oct at resonance
45 to -6.7 at 118, and the drift accounts for about 4.8 of that 5.8 range, so it
is a trend rather than scatter.

At cutoff 127 the same measurement does not reproduce, because the analysis band
there sits 50 dB below the passband. **Do not quote cutoff-127 slope numbers.**
An earlier round did, and it had to be retracted.

### Why this is a direction call and not a fit

A resonant ladder's far-field skirt asymptote should not depend much on
feedback. Resonance changes the peak, not the slope four octaves up. ReBirth's
does depend on it. No setting of `K_A`, `K_B`, `K_CUT`, `RESO_OCT`, `CUT_SLOPE`
or any other constant currently in the code can produce a resonance-dependent
asymptote, because none of them changes the order of the filter or the shape of
its stopband. This is a statement about topology.

It is also the same conclusion the peak-position work reached from the other
side, which is the reason to take it seriously: two independent measurements,
one on where the peak sits and one on how the stopband falls, both say the
structure is not the structure.

### What the options look like

These are sketches, not costed plans. Costing them is the first job after a
direction is chosen.

**Do nothing yet.** Finish the constants, ship the 303 with a skirt that is 1 to
5 dB/oct too steep depending on resonance, and revisit after the drum machines.
Defensible: the error is in the stopband, where there is little energy, and no
ear check has ever flagged it. The risk is that it interacts with the
distortion and PCF supertasks, which sit downstream of this filter and will be
fitted against whatever it produces.

**Change the filter order or the stopband shape.** A resonance-dependent
asymptote can come from a ladder whose stages are not identical, from a
zero moving with the feedback, or from a different core entirely. The repo
already carries two alternative cores behind `REBORN_FILTER` (`rb`, a
ReBirth-era forward-Euler ladder, and `diode`, a backward-Euler diode ladder),
neither of which has been scored against the new curve metric. Cheapest first
step in this direction is to score both, which costs an afternoon and might
answer the question without any new DSP.

**Accept a hybrid.** Keep the current core and add an explicit
resonance-dependent shelf above the corner. Honest but inelegant, and this
project has a rule against buying fit with free parameters. It would need to be
labelled as a correction term rather than a mechanism.

### What is needed

A direction, in one sentence. Everything downstream of the filter waits on it,
because the level calibration and the accent constants both get measured through
whatever the filter does.

If the answer is "score the two existing cores first", that is a direction and
it needs no further input from Luke.

</details>

---

## What neither of these is waiting on

The noise floor. **Done, twice.** Measured on `renders/batchH_floor/`, six cells
with five ReBirth exports each, ten pairs per cell, pooled the same way
`curve_fit.compare()` pools so that a floor and a fit are the same statistic. The
pooled median is **0.114 dB rms**, and the shipped config scores 4.745, so the fit
sits about 42x above its own measurement noise. The old worry that the fit might
be inside the noise is settled in the opposite direction from the one expected:
there is plenty of real signal in the residual and room to work on it.

The 1.43 figure that appeared in earlier drafts of this file was a different
statistic, a single cell rather than a pooled set, and should not be quoted.
