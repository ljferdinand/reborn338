# 303 findings, the reopened supertask

The live worklist for the 303, reopened 2026-08-29 when Luke's ear on a clean
rack-off comparison said our bass does not sound like ReBirth's. Settled laws
graduate to `PARITY.md`; retired ones go to `FINDINGS_AUDIT.md`.

## Standing decisions

**Parity work happens on the native TPT voice.** Luke, 2026-08-29: Open303 stays
as close to its own source as possible and is not to be bent toward ReBirth. It
is an interesting shortcut we keep, and whether it ships as the default is a
separate and open question. Render anything measured here with
`REBORN_BASS_ENGINE=tpt`.

**ReBirth is the target, not the TB-303.** Unchanged, but worth restating here
because a 303 emulator's literature is all about the hardware and this project is
not.

**One 303 at a time in a test piece.** Luke, 2026-08-29: two simultaneous lines
cannot be told apart by ear.

## 0. How this got reopened, and one correction

The route in was `listen-null-2026-08-29/`, the first calibration pair this
project has made. Its whole-mix row scored **0.12 dB** mean octave error and Luke
still heard substantial bass differences. Both are true: Trakka Rekka's two 303s
sit about 16 dB under the drums, so they are 2.5 percent of the energy and a
band-share metric over a whole mix cannot see them.

**Correction, on the record.** Chasing that, I reported that Calm runs its 303 at
resonance 127 and that this was the known hard regime. That is wrong. The `303`
chunk says cutoff 120, resonance 127, but the chunk is the SAVE-TIME panel; the
track starts at **cutoff 0, resonance 0**, envmod 78, decay 84, accent 107, tune
25, and does not reach the chunk's values until tick 4992, which is bar 156 and
far outside the window measured. Pattern 1, the one it starts on, is a slid bass
line with no accents at all.

That is the same chunk-versus-track trap recorded for the PCF earlier the same
day, walked into again within hours. It is now recorded in
`docs/NON_OBVIOUS_BEHAVIOUR.md`.

## 1. Batch B1: the filter's static response over the cutoff x resonance plane

55 ReBirth exports, 2026-08-29. One 303, no effects, every fixture passing
`isolation_check.py --device bass1 --effect none`.

The source is a held drone, one note slid across all sixteen steps so the
instrument never retriggers, with env mod and accent at zero. Each fixture is
divided by `b1_ref`, the same song with the filter wide open and no resonance,
which cancels the oscillator, the VCA and the output stage and leaves the
filter's magnitude response. Corner is the -3 dB point below the fixture's own
passband, read off the drone's harmonic grid; peak is the largest excess over
that passband.

**Why a plane and not a sweep.** `COMPONENT_MATRIX.md` records cutoff as four
points at one note and resonance as eight, which is one line through a
two-dimensional surface. The shelved diode plan
(`PLAN_303_FILTER_TRACKB.md`) concluded that what is needed is "a RESO-and-
cutoff-dependent K_B refit", and that is a fit a one-dimensional sweep cannot
supply. Nine cutoffs by five resonances, both extremes on both axes.

### What it found: two independent defects

**The corner is too low everywhere, and the error grows with both knobs.**

| cutoff \ reso | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| 0 | -0.10 | -0.02 | -0.01 | -0.08 | -0.17 |
| 32 | -0.18 | -0.23 | -0.27 | -0.29 | -0.32 |
| 64 | -0.18 | -0.45 | -0.40 | -0.46 | **-0.65** |
| 96 | -0.38 | -0.46 | - | - | - |

Octaves, ours minus ReBirth. Mean 0.284, worst 0.655 over the 31 points where
both corners resolve.

**The resonant peak is short, and that shortfall also grows with both knobs.**

| cutoff \ reso | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| 0 | -1.0 | +0.1 | +4.4 | +3.6 | +1.9 |
| 32 | -2.6 | -0.1 | -0.5 | -1.7 | -4.9 |
| 64 | -2.7 | -0.7 | -3.9 | -5.9 | -6.0 |
| 96 | -1.4 | -3.2 | -7.4 | -10.5 | -10.4 |
| 127 | -0.0 | -5.1 | -9.2 | **-11.5** | -9.2 |

dB, ours minus ReBirth. Mean 4.20, worst 11.45.

The low-cutoff rows are the least trustworthy: with the corner near the
fundamental the "peak" measure picks the fundamental rather than a resonant one,
which is why the top-left corner reads positive.

### The cutoff law refits cleanly, and it does not touch the peak

The r = 0 column isolates the cutoff law from resonance entirely. Our law is

    baseCut = CUT_SCALE * 407 Hz * 2^(CUT_SLOPE * cutoff)

shipped at CUT_SCALE 0.78, CUT_SLOPE 2.9, both env-overridable. The correction
required grows from 0.10 octaves at the bottom of the knob to 0.36 at the top,
so it is a SLOPE error and no change of scale alone can fix it.

| | mean corner error | worst |
|---|---|---|
| shipped, 0.78 / 2.9 | 0.216 oct | 0.383 |
| 0.834 / 3.20 | 0.071 | 0.131 |
| 0.85 / 3.30 | 0.046 | 0.074 |
| **0.88 / 3.24** | **0.043** | **0.061** |
| 0.89 / 3.22 | 0.043 | 0.067 |

The residual at the best fit is a shape mismatch, low at the bottom of the knob
and high in the middle, that a two-parameter law cannot remove.

Re-measuring the WHOLE plane with 0.88 / 3.24:

| | corner, mean | corner, worst | peak, mean | peak, worst |
|---|---|---|---|---|
| shipped | 0.284 oct | 0.655 | 4.20 dB | 11.45 |
| refit | **0.077** | **0.386** | 4.39 | 11.37 |

**So the two defects are independent.** Fixing the corner improves it 3.7 fold
and leaves the resonant peak exactly where it was. That separation is the whole
reason for measuring a plane rather than a line, and it means the resonance
question can be attacked on its own without the corner confounding it.

### Why re-anchoring is safer here than it looks

`CUT_SCALE` was set in a joint refit on 2026-07-31 and the audit is full of
warnings about coupled constants. But the filter's modulation is denominated in
OCTAVES on top of the anchor, deliberately:

    fc = baseCut * 2^(envOct + resoOctEff * reso)

so `EM_OCT`, `EM_FLOOR`, the accent sweep and the resonance-to-corner coupling
all keep their meaning when the anchor moves. The code says so at the env-domain
switch, and the measurement agrees: the peak error moved by 0.19 dB across a
change that moved the corner by 0.2 octaves.

**NOT SHIPPED, AND NOW REJECTED.** The regression it was waiting on was run on
2026-08-29 after B4 and it fails comprehensively. See the next subsection.

### The regression net rejects this refit, and disagrees about the SIGN

Run with `REBORN_BASS_ENGINE=tpt` on both sides, which is the flag that makes the
net able to see a 303 constant at all:

| candidate | ALL 373 cells | better | worse |
|---|---|---|---|
| **0.88 / 3.24, the B1 refit** | **+2.094** | 45 | **327** |

Twelve of the sixteen sets have **zero** cells better. Each half fails on its own
too, so it is not an interaction between the two constants. Scanning the anchor
alone over four sets, in dB of change against the shipped 0.78 / 2.9:

| move | batchI | batchL | batchL2 | batchQ | envshape |
|---|---|---|---|---|---|
| `CUT_SCALE` 0.88, corner up 0.17 oct | +1.061 | +1.082 | +0.872 | +1.086 | +0.415 |
| `CUT_SCALE` 0.84, up 0.11 | +0.553 | +0.532 | +0.410 | +0.617 | +0.065 |
| `CUT_SLOPE` 3.24, up | +0.633 | +1.411 | +1.140 | +1.391 | +0.506 |
| `CUT_SCALE` 0.72, corner DOWN 0.12 | +0.038 | +0.272 | +0.250 | **-0.301** | +0.615 |
| `CUT_SLOPE` 2.75, DOWN | **-0.094** | +0.174 | +0.221 | **-0.276** | +0.348 |

**Every upward move is uniformly bad and the downward moves are mixed**, with
batchI and batchQ preferring a lower corner and envshape a higher one. So the net
does not merely disagree with B1 about how far the corner should move. **It
disagrees about which way**, and the shipped anchor sits close to its optimum.

That is a contradiction between two measurements, not a weak result, and it is
the next thing to settle. Do not ship either side of it on the strength of the
other.

**One hypothesis tested and NOT supported.** B1 divides every fixture by
`b1_ref`, the filter wide open, and our wide-open corner is low enough that the
denominator could have been a lowpass sitting inside the audio band while
ReBirth's was not. That would be the denominator rule again, in the one place
this round had not checked it. Measured, the two `b1_ref` spectra track each
other to within 3.5 dB at the fundamental and about 2 dB from 440 Hz up, and
both roll off steeply, so the denominators are comparable and this is not the
explanation. Recorded so the next round does not spend an afternoon on it.

### RESOLVED, and defect 3 dissolves: the corner was never wrong

The contradiction was settled by drawing the plane instead of reducing it. Every
303 measurement in this project had been two numbers per cell, and two numbers
cannot say which of two measurements is confused. `plot_response.py` draws all 45
cells with ReBirth and ours overlaid, and the answer is visible in about ten
seconds: **the curves lie almost on top of each other in corner position**, and
what differs is the balance either side of the corner.

**The bug in B1's corner statistic.** It reads "the frequency where the response
falls 3 dB below its own PASSBAND", and takes the passband from the lowest four
partials of a 55 Hz drone: 65, 129, 194 and 231 Hz. Wherever the corner comes
down near those partials the passband reading is already in the stopband, and
that is most of the low-cutoff half of the plane. At cutoff 0 resonance 0
ReBirth's response is already falling at the first partial, so its "passband"
reads -15.8 dB and every number computed from it is meaningless. It is the
denominator rule in a new disguise: a reference taken from inside the thing being
measured.

**Two passband-free measures, agreeing.** Both are in `bass303_plane.py`.

| measure | resonance | mean error | mean abs |
|---|---|---|---|
| corner as the RESONANT PEAK frequency | 96 | -0.076 oct | 0.076 |
| | 127 | +0.019 | 0.032 |
| corner as the crossing of an ABSOLUTE -20 dB | 32 | -0.068 | 0.095 |
| | 64 | -0.079 | 0.079 |
| | 96 | +0.003 | 0.091 |

**Our corner is right to within about 0.08 octaves, not 0.284 octaves low.**
Defect 3 as recorded does not exist, the refit was fixing something that was not
broken, and that is exactly why the regression net rejected it and disagreed
about the sign. `CUT_SCALE` 0.78 and `CUT_SLOPE` 2.9 stay.

### What IS wrong, in the filter's own coordinates

Ours minus ReBirth in dB, binned by octaves from ReBirth's own corner and pooled
across the cutoff knob:

| reso | -3.0 | -2.0 | -1.0 | -0.5 | **0.0** | +0.5 | +1.0 | +2.0 | +3.0 |
|---|---|---|---|---|---|---|---|---|---|
| 0 | . | 0.0 | -0.0 | -0.0 | **+2.2** | -0.0 | +4.4 | +3.0 | -2.2 |
| 32 | +1.5 | +1.6 | +0.9 | -1.3 | **-3.5** | -5.6 | -0.5 | -2.0 | -8.8 |
| 64 | +4.8 | +4.1 | +5.0 | +3.3 | **-4.8** | -4.9 | -4.2 | -3.6 | -7.9 |
| 96 | +5.0 | +4.3 | +6.0 | +6.6 | **-9.0** | -7.2 | -2.5 | -3.2 | -3.2 |
| 127 | +4.4 | +4.4 | +5.9 | +7.3 | **-9.6** | -7.4 | -3.0 | -4.1 | -4.7 |

**A see-saw about the corner.** At every resonance from 64 up we put too much
energy in the three octaves BELOW the corner and too little AT and above it. It
is a passband-to-resonance BALANCE error, not a corner position error and not
simply "the peak is short".

**THE MAGNITUDE ABOVE IS NOT CONFIRMED AND IS PROBABLY INFLATED. Correcting my
own number the same day.** The table is computed over `b1_ref`, and `b1_ref` is
cutoff 127 at resonance 0, whose corner is only about 2.2 kHz. That denominator
is a lowpass sitting inside the audio band, which is the fourth time this project
has divided by a filter and called it a reference.

The same data cut a way that needs NO reference says the effect is about a third
of the size. Taking each cell over the SAME CUTOFF at resonance 0, so the
oscillator, the VCA, the output stage and the shared reference filter all cancel
and the two sides differ in exactly one knob:

| reso | -3.0 | -2.0 | -1.0 | **0.0** | +1.0 | +2.0 |
|---|---|---|---|---|---|---|
| 32 | -3.3 | -2.4 | -2.0 | **-3.6** | -0.5 | +0.4 |
| 64 | +0.7 | +0.7 | -1.1 | **-3.2** | +0.5 | +2.1 |
| 96 | +3.3 | +0.8 | -0.4 | **-3.4** | +0.5 | +2.4 |
| 127 | +2.0 | -0.2 | -1.3 | **-1.5** | -0.5 | +1.0 |

So what resonance DOES is close: **our peak comes up about 1.5 to 3.5 dB short
and our skirts run 1 to 3 dB full**, which is a damping difference and a modest
one. The reso-32 row sitting uniformly low is a separate thing, the passband
makeup gain, which enters this ratio because the denominator is at resonance 0
where our makeup is 1.0.

The direction of the finding stands and the size does not. Batch B5 exists to
settle it with a reference that is actually open.

That single shape explains all three of the disagreements this round:

  * **The regression net.** It scores energy-normalised third-octave shape, so
    our excess low end is the dominant term. Moving the corner UP adds highs
    without touching the low excess, so it gets worse; moving it DOWN trades
    some of the excess away, which is why batchI and batchQ preferred it. The
    net was right about the sign of what it could see and it was never seeing a
    corner error.
  * **B2's brightness gain**, ours buying 0 to 25 percent more dB per octave. The
    same see-saw through a high-band over low-band ratio.
  * **The recorded "peak is 11.5 dB short"**, which was measured over the
    passband. Split into absolute peak height and passband level, the peak is
    within a few dB and moves BOTH ways across the knob, ours running up to
    9.2 dB TALLER at low cutoff and 6.5 dB shorter at high cutoff, while our
    passband is 2 to 8.6 dB loud almost everywhere.

`score303`'s fixtures are not in the workspace at all: nothing under `fixtures-*/`
matches `scratch_303_*`, and the renders it scores are stale.

### A fixture bug in this batch, and its lesson

Nine of the 55 fixtures test note-dependence at note 7, and they are **invalid**:
they were measured against `b1_ref`, which is a note-0 drone. The reference's
harmonic grid is not the fixture's, so the ratio is meaningless, and it shows as
a corner pinned at 320 Hz and a peak of 74 to 84 dB for every setting.

That is the second reference-design error of the week, after a PCF round-7
reference that had the unit on at zero depth. **A denominator must differ from
its numerator in exactly one thing.**

RE-MEASURED with a note-7 reference exported the same day. Within each note our
corner is low as before, and the deficit is somewhat larger at note 7: -0.17,
-0.38 and -0.54 octaves at cutoff 32, 64 and 96 against -0.19, -0.17 and -0.38
at note 0. **Whether the LAW is note-dependent cannot be separated from the
instrument here**, because the corner is read off the drone's harmonic grid and
note 7's partials are 97 Hz apart where note 0's are 65, so the estimator's own
resolution differs between the two. Comparing ours against ReBirth AT a note is
fair; comparing note 0 against note 7 is not. A cleaner design would drive both
notes at a fixed cutoff and compare only the ratio of ratios.

## 2. Batches B2 and B3: the envelope and the accent

36 more ReBirth exports the same day. One 303, no effects, all isolation-checked.
The source is a plucked note on every sixteenth at 120 BPM, so the filter
envelope fires each step and the harmonic grid never moves.

### Three measures were tried before one worked

Worth keeping, because two of them fail for reasons that will recur.

**The spectral centroid** needs no model, which is why it was tried first, and it
measures the source rather than the filter. A saw's residual harmonics dominate
it: the entire env-mod range moved the centroid from 284 Hz to 646 Hz while the
corner itself travels several octaves.

**Fitting a four-pole ladder to the ratio against the wide-open reference** fails
for a subtler reason. The 303's filter is never actually flat, so that ratio is
one ladder response divided by another and not a ladder response at all. It
pinned at the top of the search grid on 97 percent of frames. That is the THIRD
denominator of this kind this week, after a PCF reference with the unit on at
zero depth and B1's note-7 subset. The rule is worth stating plainly: **a
denominator differs from its numerator in exactly one thing, and a filter you
cannot switch off is not a denominator.**

**BRIGHTNESS**, the ratio of 1 to 8 kHz energy against 60 to 250 Hz, is what
works. It needs no model and no reference, because both sides play the identical
pattern. It says nothing about the corner in Hz and it answers the parity
question exactly: does our envelope go where ReBirth's goes and come back when
ReBirth's comes back.

**Its one confound, stated rather than hidden.** With env mod at zero there is no
filter envelope at all, and brightness still falls about 3 dB across a step,
because the VCA's own decay takes the high band down faster than the low. So 3 dB
of every fall below belongs to the VCA and only the excess is the filter.

### B2: our envelope is too FAST at the bottom of the decay knob

**RETIRED AND REPLACED, later the same day.** The table first published here was
read off a statistic that was measuring the wrong part of the step. It is kept
below with the correction, because the way it failed is worth more than the
numbers were.

**The bug.** `bass303_envelope.py` reported a `floor` taken from the last three
columns of the folded step. A step at 120 BPM is 125 ms and the analysis window
is 23.2 ms wide, so **every column from 36 on is a window that has already
crossed into the next note**, and the last three are dominated by its onset.
Every "floor" in the table was the next note's ONSET.

That is why it looked so strange. The published reading was "ReBirth's floor
barely depends on the decay knob at all", which would be a remarkable thing for a
decay knob to do. It is not remarkable at all once you see that the number is an
onset: an onset does not depend on a decay knob. Read on the clean columns,
ReBirth's real floor at env mod 127 runs **-53, -48, -30, -20, -18 dB** across
the decay range, which is a 35 dB span and about as decay-dependent as a floor
can be.

The tool now defines `CLEAN` as the last column whose window ends inside the
step and reads nothing past it. `fit_accent.py` was cutting three columns short
of that too; tightened, and **the shipped accent constants are unmoved and still
the minimum**, with the score improving from 1.00 to 0.78 dB rms because the
contaminated columns were only adding error. The accent refit does not rest on
this.

**What the clean measurement says**, on the trajectory rather than any single
statistic. Our envelope's TIMING is already right across most of the grid and
badly wrong at the bottom of the decay knob. Timing error by cell, in dB rms
after each cell's own brightness-per-octave gain is divided out (section 5 says
why that division belongs there):

| env mod \ decay | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| 0 | 0.64 | 0.36 | 0.28 | 0.36 | 0.32 |
| 32 | 2.14 | 0.39 | 0.29 | 0.27 | 0.21 |
| 64 | 4.48 | 0.73 | 0.23 | 0.16 | 0.17 |
| 96 | 6.00 | 1.59 | 0.14 | 0.13 | 0.16 |
| 127 | **8.54** | 2.26 | 0.28 | 0.15 | 0.17 |

Everything at decay 64 and above is between 0.13 and 0.36 dB, which is as good
as this measure resolves. The whole defect is the decay-0 column and the top
half of the decay-32 column, and it grows with env mod because a timing error
only shows where the envelope has somewhere to travel.

### B3: the accent is short at the top, and it ACCUMULATES when it should not

Accent depth, as the brightness of an accented step over its plain neighbour:

| accent knob | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| ReBirth | -0.73 | +0.35 | +1.62 | +2.17 | +1.94 |
| ours | -0.18 | +0.45 | +1.30 | +1.36 | **+0.91** |

We track it to knob 64 and then fall away. At the top of the knob ReBirth is
+1.94 dB and we are +0.91, less than half.

**And the accumulation question, which the matrix records as never tested.**
Runs of one, two, three and four consecutive accented steps, each step's
brightness relative to a never-accented step in the same fixture:

| | step 0 | 1 | 2 | 3 |
|---|---|---|---|---|
| ReBirth, one accent | **+1.97** | -0.16 | -0.06 | +0.03 |
| ReBirth, two | +2.14 | +2.01 | -0.05 | +0.03 |
| ReBirth, three | +2.14 | +2.01 | +2.12 | +0.03 |
| ReBirth, four | +1.97 | +2.05 | +2.09 | +1.88 |
| ours, one accent | **+2.95** | **+2.05** | +0.11 | +0.40 |
| ours, two | +2.95 | +3.06 | +1.26 | +0.43 |
| ours, three | +2.95 | +3.23 | +3.17 | +1.32 |
| ours, four | +2.95 | +3.22 | +3.16 | +3.20 |

**ReBirth's accent does not accumulate and does not leak.** Every accented step
gets the same 1.9 to 2.1 dB whether it is the first of a run or the fourth, and
the step immediately after an accent is back to baseline within 0.2 dB.

**Ours does both.** A single accent reads +2.95 on its own step, half again as
deep as ReBirth's, and still +2.05 on the FOLLOWING step, which ReBirth leaves
untouched. In a run the second accent lands higher than the first and the excess
carries one step further each time.

That is consistent with the 2026-08 accent round's aggregate finding that "our
accented gestures are enormously deeper than ReBirth's while the rest are mildly
hot", and it identifies the mechanism the aggregate could not: a per-accent state
that adds instead of replacing, plus a release long enough to cross a step.

## 3. Where the 303 stands after B1 to B3

Five defects were measured. One is fixed.

| | defect | state |
|---|---|---|
| 1 | the accent leaks and accumulates | **FIXED and shipped on the branch**, section 4 |
| 2 | the filter envelope is 2.3x too fast at the bottom of the decay knob | **FIXED and shipped on the branch**, section 5 |
| 3 | the cutoff corner is too low, 0.284 octaves mean | **RETIRED, the defect does not exist.** Two passband-free measures put the corner within 0.08 octaves. Section 1 |
| 4 | accent depth falls away above knob 96 | open, improved by section 4 but still -1.56 dB at 127 |
| 5 | the level rises 10.8 dB across the cutoff knob at resonance 0 in ReBirth and 2.4 in ours | **FIXED and shipped on the branch**, section 7. The tilt is gone; a flat 3 dB offset remains and is now a one-number calibration |

Defect 2 was stated on 2026-08-29 morning as "the envelope falls up to 8 dB too
far at low decay". That reading came from the window bug in section 2 and from
scoring an uncalibrated brightness raw. Measured properly it is a TIMING defect
and not a depth one, and section 5 has it.

## 4. The accent, refitted 2026-08-29

The first of the five fixed. Three constants moved and none of them is new; what
is new is a target that could see the whole step.

### Why the 2026-08-02 fit could not have found this

It scored only the first 62 ms of a note, and said so: "past 62 ms the difference
mixes the accent with the VCA release and with ACC_SHORT, and cannot be
attributed to either." Everything wrong with the accent lives after 62 ms.

B3 removes that limit by differencing an accented pattern against the SAME
pattern with no accented steps at all, rather than against a matched control
note. The VCA, the note and the filter's own envelope cancel exactly, so the
whole step is readable and so is the step after it.

### The target, which is a shape and not a number

`b3_run1` minus `b3_ref_plain`, brightness in dB, twelve milliseconds a column:

| | 0 | 12 | 24 | 36 | 48 | 60 | 72 | 84 | 96 | 108 |
|---|---|---|---|---|---|---|---|---|---|---|
| ReBirth, step 0 | +2.7 | +1.5 | +1.0 | +1.3 | -0.7 | -3.9 | -9.2 | -13.6 | -16.3 | **-16.7** |
| ReBirth, step 1 | -0.2 | +0.2 | +0.2 | 0.0 | +0.2 | -0.1 | +0.1 | +0.3 | -0.3 | +0.2 |
| ours, before | +3.1 | +3.5 | +3.6 | +3.9 | +4.3 | +4.0 | +1.4 | -1.7 | -3.0 | **-5.0** |
| ours, before, step 1 | +1.2 | +1.3 | +1.5 | +1.6 | +1.7 | +1.8 | +1.8 | +1.5 | +1.2 | +1.2 |

Three things in it, and only the first had ever been fitted. **The accent is
bright immediately and that brightness is gone inside 40 ms.** It then drives the
note far DARKER than an unaccented one for the rest of the step. And it leaves
nothing behind at all.

Ours peaked late at 48 ms, reached only a third of the squelch, and left 1.5 dB
on the following step.

### The refit

| | shipped | refit |
|---|---|---|
| `ACC_ATK` | 0.010 s | **0.008** |
| `ACC_DEC` | 0.065 s | **0.040** |
| `ACC_SHORT` | 0.45 | **0.34** |
| trajectory error over both steps | 5.74 dB rms | **1.00** |

`ACC_SWEEP` was included in the search and came out unchanged at 3.30, so the
accent's DEPTH was already right and only its times were wrong. That is worth
noting because the 2026-08 round suspected the depth.

After the refit, ours reads +3.0, +2.7, +3.1, +1.3, +0.4, -3.8, -9.8, -13.6,
-14.9, -14.7 on step 0 against ReBirth's -16.7 floor, and +0.4 flat on step 1.
The accumulation runs go from +2.95, +3.22, +3.16, +3.20 to +2.80, +3.00, +2.84,
+2.67 against ReBirth's +1.97, +2.05, +2.09, +1.88: the CLIMB is gone and about
0.8 dB of uniform excess remains.

### It improves the old regression too, at every point

`accentdepth`, 2026-08-02, twenty fixtures, both of its statistics:

| accent knob | 32 | 64 | 96 | 127 |
|---|---|---|---|---|
| brightness error, shipped | +0.45 | +0.04 | -0.86 | -1.93 |
| brightness error, refit | **+0.37** | +0.04 | **-0.66** | **-1.56** |
| level error, shipped | +0.60 | +0.90 | +0.87 | +0.69 |
| level error, refit | **+0.21** | **+0.34** | **+0.24** | **+0.02** |

Better everywhere on both, so it is not a trade. **Shipped on the branch.**

### What is still wrong with the accent

The uniform 0.8 dB of extra depth on accented steps, and a residual +0.4 dB on
the step after where ReBirth gives +0.06. Both are small beside where this
started, and neither is explained by the three constants fitted here: `ACC_SWEEP`
is already optimal, so the excess is somewhere else in the accent path,
`ACC_RESO` and `ACC_LVL` being the untested candidates.

The brightness law also still falls away at the top of the knob, -1.56 dB at 127
against -1.93 before. That is the same shape B3 saw directly and it is now the
largest remaining accent error.

## 5. The filter envelope, refitted 2026-08-29 (batch B4)

The second of the five fixed, and the smallest change in the file: three
constants that had been sitting in the source as a probe since 2026-08-02 with
the old law still the default.

    DEC_A  0.030 -> 0.070      tau = DEC_A * 10^(DEC_B k + DEC_C k^2)
    DEC_B  1.87  -> 0.901      k is the decay knob, 0 to 1
    DEC_C  0.0   -> 0.584

**The decay knob's law is a quadratic in log tau, not a line.** The line was
right at the top of the knob and 2.3 times too fast at the bottom, and that had
been a known second-order residual for a month. It stopped being second-order
when the env-mod swing got deep: with five octaves of travel instead of two, a
time-constant error at the fast end drags the corner through several octaves
inside a single note.

### The correction that made the fit possible

**Brightness is monotone in the corner, and it is not CALIBRATED.** How many dB
of high-over-low energy one octave of corner movement buys depends on the slope
and the skirt of the filter doing the moving, and B1 has already shown ours is
not ReBirth's there. So a raw dB residual cannot tell "our envelope travels too
many octaves" from "our octaves are worth too many dB".

Scored raw, B2 asked for the envelope depth to come down hard, `EM_OCT` 4.45 to
about 3.45. **The regression net rejects that flatly**: batchI +0.908, envshape
+0.579, envfloor +1.368, all much worse, and the only set that liked it was
batchQ, which runs at env mod 0 and cannot see `EM_OCT` at all. The depth was
being asked to pay for a slope error.

`fit_envelope.py` now divides out a per-cell least-squares gain before scoring,
so what is left is TIMING, which is what a decay law is. The gain is printed
rather than hidden, and it is the second finding of this batch:

| env mod \ decay | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| 0 | 0.73 | 0.76 | 0.94 | 0.66 | 0.72 |
| 32 | 0.79 | 0.87 | 1.01 | 1.05 | 1.19 |
| 64 | 0.82 | 0.88 | 0.96 | 0.84 | 1.17 |
| 96 | 0.81 | 0.86 | 0.89 | 0.79 | 1.02 |
| 127 | 0.82 | 0.84 | 0.88 | 0.93 | 0.99 |

**Our filter's brightness-per-octave is 0 to 25 percent higher than ReBirth's.**
That is a slope and skirt measurement, it belongs to defects 3 and 5, and it is
new evidence for both.

### Three sources agree, and only one of them was fitted on

1. The taus measured from the `decayprobe` exports in 2026-08: 0.07, 0.09, 0.14,
   0.28, 0.81, 2.14 s at knobs 0, 16, 32, 64, 96, 127. This is what the quadratic
   was originally fitted to.
2. **B2's trajectory grid**, which never saw those. Timing error over all 25
   cells falls **2.267 to 0.425 dB rms**, and the decay-0 column, which carried
   all of it, falls from **8.54 dB to 1.30** at env mod 127. Scanning the knob-0
   constant alone gives a clean interior minimum at 0.070, with 0.060 and 0.080
   both worse, so it is a fitted time and not the shoulder of something else.
3. **A time constant fitted directly to ReBirth's own B2 trajectories**, which
   needs no model of ours at all: 0.080 s at knob 0 and 0.129 at knob 32, against
   the law's 0.070 and 0.129. Past knob 32 that fit goes degenerate, and it says
   so, because a time constant longer than a step cannot be read from one step.
   That is exactly the half of the knob where the old straight line was already
   right.

Everything at decay 64 and above was already inside 0.36 dB and stays there. The
change moves only the end of the knob that was wrong.

### What it did to the regression net, including the part that got worse

Net over all 373 cells: **-0.036 dB, better**. It is not uniform and the pattern
is the interesting part.

| improved | | regressed | |
|---|---|---|---|
| batchG | -0.179 | envfloor | +0.146 |
| envshape | -0.232 | envdomain | +0.132 |
| batchI | -0.116 | batchQ | +0.035 |
| batchH | -0.016 | accentdepth | +0.033 |

**The sets that test envelope shape improve most and the sets that regress all
run at or near env mod 0**, where `EM_FLOOR` is the entire envelope. They regress
for the reason the gain table just gave: our octaves are worth too many dB, so
holding the corner up for the RIGHT length of time costs more spectral error
than holding it up too briefly did. Bending the decay law to hide that is the
exact trade the audit is full of, so it is not bent. The corner and the skirt are
the next two fits and this is more evidence for them, not against this one.

### One trap this batch did NOT walk into, recorded because it was close

`regress_ab.py` renders with the SHIPPED default 303 voice, which is **Open303**,
not the native TPT voice every constant in this file belongs to. Run without
`REBORN_BASS_ENGINE=tpt` on both sides it reports **+0.000 and zero cells moved**
for any TPT constant, which reads exactly like "structurally unaffected". A
change of `EM_OCT` 4.45 to 3.0 and `EM_FLOOR` 0.85 to 0.5, which really costs
batchI +2.214 and 30 of its 40 cells, scores +0.000 without the flag. Its own
baseline gives it away once you look: batchI scores 7.646 on Open303 and 1.789 on
TPT.

**Every regress_ab invocation for a 303 constant must carry
`REBORN_BASS_ENGINE=tpt` in both `--before` and `--after`.** Some `+0.000`
entries in `FINDINGS_AUDIT.md` may be this artifact rather than a result, and
should not be relied on without a rerun.

## 6. Batch B5: the dense grid, and what B1's reference was hiding

Six pitches a whole tone apart, each a drone that slides on every step so the
303 never retriggers. Six harmonic series interleave and the union is dense
exactly where one series is sparsest. Sixty fixtures, all proved by
`isolation_check.py`, exported unattended in about fifteen minutes.

### The reference was the problem, and now it is measured

`b5_ref_open` drives the corner above the band with the ENVELOPE rather than the
cutoff knob: cutoff 127 at env mod 127 and decay 127, read in the first quarter
second while the envelope is still open. Against B1's old reference, per side:

| Hz | 89 | 141 | 224 | 356 | 566 | 898 | 1425 | 2263 |
|---|---|---|---|---|---|---|---|---|
| ReBirth, open over flat | 0.7 | 1.7 | 3.5 | 6.0 | 9.2 | 12.3 | 15.8 | 21.2 |
| ours, open over flat | 0.0 | 0.1 | 0.3 | 0.7 | 1.8 | 4.0 | 8.5 | 15.8 |

**B1's `b1_ref` is 3 to 8 dB more filtered on ReBirth's side than on ours across
224 Hz to 2.3 kHz.** It was never a comparable denominator, and every B1 number
computed over it inherited that as a bias, in the direction of making our
response look darker than it is. That is the whole explanation of the "5 dB too
much below the corner and 5 to 10 dB too little at it" figure, which is hereby
retired. Cut with no reference at all, what resonance DOES matches to about
2.5 dB, and the rolloff slopes match to within a few dB per octave.

### What the dense grid actually finds, at RESONANCE 0

The one place B1 could never look, because at resonance 0 there is no peak to
anchor on and its passband statistic had run out of partials. Rolloff in dB
relative to each side's own 55 to 70 Hz bin:

| cell | 78 | 98 | 123 | 156 | 196 | 247 | 311 | 392 | 494 | 622 | 784 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| c032 ReBirth | -1.1 | -2.8 | -4.6 | -6.5 | -8.6 | -10.6 | -13.7 | -16.8 | -21.2 | -26.2 | -32.2 |
| c032 ours | -0.2 | -0.6 | -1.3 | -2.3 | -3.7 | -5.4 | -8.6 | -11.8 | -16.3 | -21.6 | -28.6 |
| c064 ReBirth | -0.8 | -2.0 | -3.5 | -5.0 | -6.8 | -8.4 | -10.6 | -12.4 | -14.9 | -18.0 | -22.4 |
| c064 ours | -0.1 | -0.2 | -0.5 | -0.9 | -1.4 | -2.2 | -3.7 | -5.3 | -7.8 | -11.2 | -16.1 |
| c112 ReBirth | -0.3 | -0.8 | -1.4 | -2.3 | -3.4 | -4.5 | -6.2 | -7.5 | -9.2 | -10.9 | -12.8 |
| c112 ours | -0.0 | -0.0 | -0.1 | -0.2 | -0.3 | -0.5 | -0.8 | -1.3 | -2.0 | -3.1 | -4.9 |

**ReBirth's filter at resonance 0 starts drooping from the very bottom and ours
does not.** Ours has a properly flat passband and then a clean corner; ReBirth's
is already 2.3 dB down at 156 Hz with the cutoff knob at 112. The correction our
filter needs, ours minus ReBirth, is a shelf rising from zero at 62 Hz to a
plateau of **5.2 dB at c032, 7.1 dB at c064 and 8.0 or more at c112**, with the
knee climbing from roughly 150 Hz to 350 Hz as the cutoff knob opens.

**It is a shape difference and not a corner shift**, which is what the octave
error at three different depths says: our resonance-0 corner reads +1.05 octaves
high at the 3 dB point, +0.63 at 10 dB and +0.25 at 20 dB. A shift would give the
same number at all three. The curves converge as they descend.

### This is a known gap, and B5 is the fixture that was asked for

`bass_synth.cpp` has carried this comment since 2026-08-02, at the
resonance-engaged low-end highpass:

> KNOWN INCOMPLETE AT RESONANCE 0, and deliberately shipped that way. fc(0) = 0
> while ReBirth still loses up to 5.7 dB there, and the miss grows with cutoff,
> -0.46 dB at cutoff 40 to -5.73 at 127. The resonance-0 SHAPE is not
> identifiable from six harmonics over 2.6 octaves; task 62 builds the fixture
> that could identify it. Do not fit that column from batch L alone.

B5 is that fixture, arrived at independently, and it agrees with the recorded
size of the miss and with the recorded fact that it grows with the cutoff knob.
The shape is now identified. **At resonance 96 the low end already matches to
within 0.5 dB**, so the resonance-engaged half of the law is right and only the
resonance-0 end of it is missing.

### Luke's ear settled the anchor, and the answer is a LEVEL LAW

Luke on `listen-303-filter-2026-08-29/`, 2026-08-29: **"ReBirth's are louder and
by my ear fuller underneath. 04 and 05 really demonstrate the oomf through their
plucking. Harmonics on 2 and 3 are spot on though."**

That picks the anchor, and it picks the one the tables above did not use. Read
with no normalisation of any kind, ours runs **2 to 6 dB quieter** than ReBirth
across the batch, and the gap grows with the cutoff knob. His "spot on" on 02 and
03, the two most resonant cells, agrees with the reference-free resonance
measurement to the letter: the resonant character is right and the weight is not.

**The measurement that needs no calibration at all.** Output level against the
cutoff knob, each side relative to ITS OWN cutoff-0 cell, so gain staging, mixer
levels and export calibration cannot enter. Batch B1, dB:

| reso | cut 32 | cut 64 | cut 96 | cut 127 |
|---|---|---|---|---|
| 0 | 4.09 / **1.21** | 7.25 / **1.84** | 9.41 / **2.19** | 10.76 / **2.38** |
| 32 | 1.73 / 0.57 | 2.62 / 0.93 | 2.97 / 1.15 | 2.97 / 1.28 |
| 64 | -0.12 / 0.06 | -0.87 / 0.06 | -1.89 / 0.05 | -1.68 / 0.04 |
| 96 | -1.09 / -0.46 | -0.79 / -0.89 | -0.82 / -1.18 | -0.93 / -1.34 |
| 127 | 2.39 / -0.80 | 2.38 / -1.55 | 2.35 / -2.26 | 1.86 / -2.68 |

ReBirth first, ours second. **At resonance 0 ReBirth's 303 gets 10.76 dB louder
as the cutoff knob opens and ours gets 2.38.** B5 reproduces it independently at
7.86 against 1.68 over its own narrower span.

**The defect lives at LOW resonance and nowhere else.** The gap is 8.4 dB at
resonance 0, 1.7 at 32, essentially nil at 64 and 96, and reopens to 4.5 at 127.
That is exactly the shape of the existing law's hole: `LF_HP` is engaged BY
RESONANCE, `fc(0) = 0`, and resonance 0 is where our level law is most wrong.

**So it is one law, not the two things the earlier sections were circling.** What
looked like a passband droop, a see-saw and a low-end shelf are all the same
fact seen through different anchors: ReBirth's 303 is much quieter at low cutoff
than ours, and the difference goes away as the knob opens. Fit the LEVEL against
the cutoff knob at resonance 0, then re-measure the shape; do not fit the shape
first.

### One caution for whoever fits it

Levels are not comparable between the two sides on this batch, so every table
above is anchored somewhere, and **the anchor changes the answer**. Anchored at
300 to 600 Hz the same data appears to say ReBirth has MORE low end than us;
anchored at 55 to 70 Hz it says the opposite. Both cannot be right and the
disagreement is not noise, it is a tilt being read from two ends. The tables
above anchor at 55 to 70 Hz, which is the honest choice at resonance 0 because
that is the only region where both sides are demonstrably flat. Whoever fits
this should decide, and write down, whether the correction is a droop we lack in
the midband or a lift we lack at the bottom, because the spectral measurement as
it stands cannot tell those apart and they are not the same fix. **The level
measurement above CAN**, because it is within-side, and together with Luke's ear
it says the level law is the thing to fit first.

## 7. The level law, fitted and shipped 2026-08-29 (batch B6)

    LVL_CUT    0 -> 8.4      dB of tilt across the cutoff knob at resonance 0
    LVL_RGATE  -    0.10     how fast resonance takes the tilt away
    LVL_PIVOT  -    0.65     where on the knob the tilt pivots

    dB = LVL_CUT * (cutoff - LVL_PIVOT) * exp(-reso / LVL_RGATE)

A gain, applied with the passband makeup, ahead of the VCA.

### It is a gain and not a corner, which took two wrong attempts to establish

A lowpass only loses 10.8 dB of a saw when its corner drops below the
fundamental, so the obvious reading is that our cutoff knob is too narrow.

**Attempt one, widen the knob.** `CUT_SLOPE` 2.9 to 4.25 with `CUT_SCALE` 0.78
to 0.306, holding the top of the knob fixed, takes the level surface from 6.47
to 1.40 dB rms with a clean interior minimum. It also drags the resonance-96
corner **0.79 octaves low**, from 0.065 to 0.793.

**Attempt two, widen it and put the corner back with resonance.** Adding
`RESO_OCT` 1.60 and `RESO_OCT_CUT` -1.05 recovers the corner to 0.102 octaves and
holds the level at 1.40. It still fails: batch B2's envelope trajectory goes from
**0.425 to 2.800 dB rms** and its onset **11.35 dB dark**.

Three independent measurements say the corner is where it should be. Only the
level is wrong, so only the level moves.

### A normalisation mistake of my own, which inflated the defect

The first version of `fit_level.py` expressed each side's surface relative to its
own `b1_c000_r000` cell, and read the error as **6.47 dB rms**. That cell is the
single most anomalous one in the set: ReBirth's filter is nearly shut there and
ours is not, so anchoring on it added that one cell's error to all 45. Normalised
by the MEAN over the plane, which no single cell can move, the real disagreement
is **1.84 dB rms**. Same family as anchoring a spectrum on a bin that is not in
the passband, and the third time this week a reference chosen from inside the
data has misled a fit.

### What it does, and what it deliberately does not

| measurement | before | after |
|---|---|---|
| B1 level surface, 45 cells | 1.844 dB rms | **1.228** |
| B1 level surface, the resonance-0 row alone | 3.08 | **1.41** |
| B1 corner, resonant peak at reso 96 and 127 | 0.065 oct | 0.065 |
| B2 envelope trajectory (B4's own score) | 0.425 dB rms | 0.442 |
| B3 accent trajectory | 0.78 dB rms | 0.84 |

Every other resonance row of the level surface moves by less than 0.05 dB. It
does one thing.

### The independent check is batch H, and it is the one that matters

`tools/analyze/level_ab.py` on batch H, a different fixture set measured a
different way. `COMPONENT_MATRIX.md` has recorded for weeks that our output runs
2.2 to 4.8 dB under ReBirth's **with the deficit growing with cutoff**, and has
said plainly not to correct it with a scalar, because the cutoff-dependent part
is filter error and a gain would only absorb it.

| | tilt across the knob | spread |
|---|---|---|
| before, peak | -1.38 dB | 0.47 |
| before, rms | -1.59 | 0.57 |
| **after, peak** | **-0.03** | **0.11** |
| **after, rms** | **-0.25** | **0.19** |

**The tilt is gone.** What remains is a flat offset of about -3.1 dB peak and
-3.6 rms, which is exactly the condition `level_ab.py` was written to test for:

> A flat offset means the level is now a one-number calibration. A surviving tilt
> means the gain would be absorbing filter error again.

That condition is now met for the first time. **Whether to take the remaining
3 dB with a single makeup gain is Luke's call and is not in this change**,
because it moves the level of every render in the project. His ear on
`listen-303-filter-2026-08-29/` already says ReBirth is louder, so the direction
is not in doubt; the size and the sign-off are.

### The remaining 3 dB is NOT cosmetic, which was a surprise

I expected `regress_ab.py` to score this change at `+0.000`, because it
normalises every render by its own total band energy and a constant gain cannot
move that. It scored **+0.066, worse**, with batchQ +0.226 and batchI +0.103.

The reason is `softClipUnity` at the end of the voice. **Our output stage is
mildly nonlinear, so running 3 dB under ReBirth means driving it 3 dB more
gently than ReBirth drives its own.** Tested directly: a pure gain of +2.3 dB,
with no other change, takes batchI **-0.281** and batchQ **-0.291**, both better.
A pure gain is not supposed to be able to do that, and the fact that it can is
the measurement.

So the level deficit has been costing SPECTRAL parity as well as level, and the
tilt on its own costs +0.066 precisely because it changes the drive without
correcting the deficit. **The tilt and the flat trim belong together.**

`REBORN_LVL_TRIM` is **APPLIED at 1.46 on Luke's sign-off, 2026-08-29**, with his instruction to take the accent regression it exposes and fix it rather than back it out.
At 1.46 it puts batch H's level at +0.11 dB peak and -0.39 rms against ReBirth,
with the cutoff tilt at -0.10 and the spread at 0.11, on a set it was not fitted
on. Three of the four sets scanned improve by 0.14 to 0.27 dB with it in.

**Do not push it past the level.** The spectral score keeps improving as the gain
rises past 1.46, because a louder signal drives the clipper closer to ReBirth's
and then past it. 1.46 is the value that MATCHES THE LEVEL, measured; anything
beyond it is buying spectral score by overshooting a level that is now correct,
which is the trade this project exists to avoid.

### The full net on the pair, including the part that gets worse

Tilt plus `LVL_TRIM` 1.46 against the pre-2026-08-29 baseline, all 373 cells:
**2.075 to 1.950, -0.125 dB better**, 191 cells better and 166 worse.

| improved | | regressed | |
|---|---|---|---|
| batchP | -0.512 | accentdepth | +0.213 |
| decayprobe | -0.487 | accentprobe | +0.171 |
| batchI | -0.314 | batchG | +0.118 |
| batchL2 | -0.291 | accentrate | +0.058 |
| batchQ | -0.252 | batchW | +0.025 |
| batchN | -0.190 | | |
| envfloor | -0.141 | | |
| batchL | -0.126 | | |

**The losses are concentrated in the ACCENT sets, and that is not a coincidence.**
An accented note is the loudest thing this voice produces, so it drives
`softClipUnity` hardest, and raising the level by 3.3 dB pushes accented notes
further into it than unaccented ones. Section 4 already records an open accent
defect of about 0.8 dB of uniform excess depth. A level rise compounds with a
hot accent exactly here.

So the honest reading is: the trim is right about the LEVEL, which is measured to
a tenth of a dB on a set it was not fitted on, and it exposes the open accent
defect rather than causing one. **The trim is in and the accent is now the live task.** Luke, 2026-08-29: "turn it on, deal with the accent issues until they're fixed."

## 8. The accent refit that was REJECTED, and the batch it forced

Luke turned the output trim on with the instruction to fix the accent regression
it exposed rather than back the trim out. Fitting the accent across the whole
knob, which `fit_accent.py` cannot do because it scores one setting, found what
looked like a clean answer and it does not survive.

### What the knob measurement said

Accent depth on steps 4 and 12 against `b3_ref_plain`, the same pattern with no
accented steps, so the pattern, the note, the filter envelope and the VCA cancel:

| knob | brightness R / ours | level R / ours |
|---|---|---|
| 0 | -15.97 / **-11.37** | 0.45 / -0.10 |
| 32 | -11.34 / -8.12 | 3.09 / 2.46 |
| 64 | -7.63 / -5.80 | 4.96 / 4.29 |
| 96 | -4.67 / -4.13 | 6.44 / 5.72 |
| 127 | -2.36 / -2.95 | 7.63 / 6.83 |

**ReBirth's accent moves brightness 13.6 dB across the knob and ours moves 8.4**,
with the knob-independent squelch at knob 0 running 4.6 dB shallow and the level
uniformly 0.7 dB shy. `ACC_SHORT` 0.34 to 0.26 with `ACC_SWEEP` 3.30 to 5.10 and
`ACC_LVL` 3.2 to 3.6 takes the brightness law from 2.663 to **0.687** dB rms and
the level from 0.681 to 0.394, with a sharp interior minimum: 0.23 and 0.29 both
score worse than 0.26, and the two constants are genuinely coupled, either alone
being worse than shipped. The time constants were re-scanned at the new pair and
are unchanged.

### And the regression net rejected it outright

accentdepth **+1.827**, accentprobe **+1.656**, accentrate **+0.733**, net +0.170.
On the sets it was meant to fix.

Measured directly on accentdepth's own patch, the same differential way, the two
disagree about the size of the very same quantity:

| | ReBirth span | ours span | deficit |
|---|---|---|---|
| B3, cutoff 48 resonance 48 | 13.6 dB | 8.4 | **5.2** |
| accentdepth, cutoff 64 resonance 60 | 11.0 | 9.3 | **1.7** |

A factor of three. Six candidate triples were scored against both laws and none
satisfies them together: the refit takes B3 to 0.687 and accentdepth from 1.157
to 1.796, and every intermediate trades one for the other along a straight line.

**NOT SHIPPED.** `ACC_SHORT` stays 0.34, `ACC_SWEEP` 3.30, `ACC_LVL` 3.2.

### Why this is a fixture problem and not a fitting problem

**The accent's knob law is not the same at every operating point**, and the
constant that would carry that, `ACC_RESO_SLOPE` at -0.708, is the least tested
thing in the accent path. B3 and accentdepth differ in BOTH cutoff and
resonance, so neither can attribute the difference to either one.

The measurement protocol (now `docs/MEASUREMENT_PROTOCOL.md`) already says what to
do, from the round-2 accent finding that is in it for exactly this reason: **cross the sweep with anything it might
interact with rather than varying two things at once.** Batch B7 does that, 30
fixtures, the accent knob at five points crossed against resonance at one cutoff
and against cutoff at one resonance, every cell with its own no-accent control
and accents on the same steps as B3 so the two compare directly.

### B7's answer: it is CUTOFF, and the law has no term for it

Span of the accent's brightness effect across the knob, ReBirth minus ours:

| axis | | | |
|---|---|---|---|
| resonance, at cutoff 64 | r16: **4.02** | r60: 4.54 | r100: **5.25** |
| cutoff, at resonance 60 | c32: **5.69** | c64: 4.54 | c96: **1.34** |

**Resonance moves the deficit 1.2 dB across its whole range and cutoff moves it
4.4.** So the suspicion recorded above, that `ACC_RESO_SLOPE` was the untested
culprit, is WRONG, and it is worth leaving that wrong guess on the page next to
the measurement that killed it.

As a RATIO rather than a difference, which is what an octave-denominated law
cares about, our sweep is 0.64 of ReBirth's at cutoff 32, 0.53 at 64 and 0.42 at
96. So the correction needed GROWS with cutoff, 1.57x to 2.36x, where the dB
deficit shrinks. `ACC_SWEEP` has no cutoff term at all.

Gridding the two constants that do exist confirms there is nothing to be had
without one. Every move of `ACC_SWEEP` or `ACC_RESO_SLOPE` makes B7's 25-cell
surface worse than shipped, in both directions:

| | sweep 3.30 | 4.00 | 4.70 | 5.40 |
|---|---|---|---|---|
| reso slope -0.708 | **2.153** | 2.290 | 2.532 | 2.823 |
| -0.50 | 2.222 | 2.463 | 2.782 | 3.125 |
| -0.30 | 2.351 | 2.672 | 3.043 | 3.422 |

Shipped is the best the current law can do. **The accent's corner push needs a
cutoff term it does not have.**

### And the reason accentdepth and B3 could disagree at all

**`accentdepth` is ONE NOTE PER BAR. B3 and B7 are continuous sixteenths.**
Counted from the files: `d303_acc064_on_c064_r060` has 1 note and 15 rests;
`b3_k064` and `b7_c064_r060_k064` have 16 notes with 2 accented.

So they do not measure the same thing. accentdepth measures an accent on an
ISOLATED note, where nothing carries in and the filter envelope starts from its
floor. B3 and B7 measure an accent inside a STREAM, where the previous note's
envelope, the accent latch and the shortened decay are all live. The 2026-08-02
accent constants were fitted on isolated notes; everything measured today is on
streams.

That is not a flaw in either fixture, and both cases occur in real music. It does
mean **no fit is finished until it is scored on both**, and it explains why a
refit that improved the streamed law by four times was rejected by a net whose
accent sets are all isolated notes.

### DONE, and the cutoff term went on the wrong constant first

Adding a cutoff term to `ACC_SWEEP` and gridding it against the sweep came back
**monotone worse in every direction**, sixteen candidates with shipped best. A
boundary rather than a minimum, which means looking where the sweep cannot reach.

**B7's KNOB-0 COLUMN.** At accent knob 0 the corner push is multiplied by zero,
so what remains is the knob-independent shortening and nothing else:

| cell | ReBirth | ours | ours minus RB |
|---|---|---|---|
| cutoff 32, reso 60 | -18.94 | -12.97 | **+5.97** |
| cutoff 64, reso 60 | -11.78 | -9.15 | +2.63 |
| cutoff 96, reso 60 | -2.69 | -2.72 | **-0.03** |
| cutoff 64, reso 16 | -10.77 | -8.72 | +2.05 |
| cutoff 64, reso 100 | -9.67 | -7.05 | +2.62 |

**Our squelch is 6 dB too weak at low cutoff, exactly right at high cutoff, and
flat in resonance.** `ACC_SHORT` is the constant with no cutoff law, and the
sweep can never fix an error that is already present at knob zero.

### The fit

    ACC_SHORT      0.34 -> 0.24
    ACC_SHORT_CUT  0    -> 0.60      effective = ACC_SHORT * (1 + CUT * cutoff)
    ACC_ATK        0.008 -> 0.010
    ACC_DEC        0.040 -> 0.044

The effective shortening runs 0.276 at cutoff 32, 0.313 at 64, 0.349 at 96: it
reproduces the old constant where that constant was right and shortens where it
was not.

| | shipped | fitted |
|---|---|---|
| B7, streamed sixteenths, 25 cells | 2.153 dB rms | **1.292** |
| accentdepth, isolated notes | 2.115 | 2.143 |
| B3 trajectory at knob 96 | 0.77 | **0.74** |
| regression net, 373 cells | 1.950 | 1.949 |

Two of three improve and the third moves by 0.03. The net is flat with
accentdepth -0.014 and accentrate -0.038, so nothing else in the instrument
moves. **This is the first accent candidate today that does not trade one design
against the other**, which is what the earlier one did and why the net threw it
out.

### The time constants moved with it, and that is the tell

`ACC_ATK` and `ACC_DEC` were fitted in August against a squelch that was too weak
at low cutoff, so they had absorbed part of that error. Correcting it with a LAW
rather than a constant freed them to sit where the trajectory actually wants
them, which is why the single-point shape score came out **better** than shipped
rather than merely recovered.

**A constant that was absorbing someone else's error moves when that error is
fixed. If it does not move, suspect the fix.** That is the cleanest positive
evidence this project has produced that a refit found a mechanism rather than a
compensation, and it is worth looking for deliberately next time.

## 9. Calm found a hole in the level law within a minute

Luke asked to hear the day's work on a song. `listen_song.py` cuts a three-way
from a window of one, rack OFF on both sides so the compressor we do not
implement and the half-fitted delay and distortion are out of the picture.

Calm's first 48 seconds is bass 1 ALONE, verified on ReBirth's own export: the
4 to 10 kHz share is 0.00 percent and the drums enter at 50 s. That window is as
close to a fixture as a song gets.

**The first cut showed it getting WORSE.** Octave error 7.26 to 8.84 dB rms, and
our level going from 4.09 dB under ReBirth to 6.22 under.

### The cause, to a hundredth of a dB

Calm opens at cutoff knob 0, resonance 0 and **env mod 78**.

    tilt at cutoff 0, resonance 0    -5.46 dB
    flat trim                        +3.29
    net                              -2.17      against 2.13 measured

**The level tilt keys off the cutoff KNOB, and every fixture in B1 runs env mod
0**, so there the knob and the corner the filter actually sits at agree and
nothing could tell them apart. On music the knob is shut and the filter is not,
and the law took five and a half dB out of a voice that was already open.

### The fix

`LVL_EM` weights env mod into the tilt's argument, which is what "how open is the
filter" actually means, clamped at the top of the range because that is the only
range the tilt was measured over. Applied at 1.0.

| | before | after |
|---|---|---|
| Calm, bass 1 alone, octave error | 8.81 dB rms | **6.81** |
| Calm, bass 1 alone, level | -6.22 dB | **-1.12** |
| B1's level surface | 1.232 | 1.232 |
| regression net, 373 cells | 1.949 | 1.948 |

**B1 scores identically with it in or out**, which is exactly right: that set is
env mod 0 throughout and cannot see the constant. On the net every set reads
0.000 except batchI, the only one with env mod movement, which improves by 0.006.
A constant that moves the set that can see it and nothing else is behaving.

1.0 and not the 1.2 that would zero the level error on that window: one 28 second
window of one song is not something to fit to three digits, the whole-mix window
mildly prefers less, and 1.0 sits inside the flat part of the scan.

### The lesson, which is about fixture design and not about this constant

**A demo song found a hole in a measured, validated, regression-checked change
within a minute of being asked for.** Not because the fit was careless, but
because every fixture that could have caught it held env mod at zero, and a
constant fitted where two things always agree cannot know which of them it
depends on.

The measurement protocol's rule about crossing a sweep with what it might interact
with applies to the SETTINGS A FIXTURE HOLDS CONSTANT, not only to the ones it
varies. B1 holds env mod at 0 for a good reason, to keep the filter static, and
that reason is what hid this. When a law is fitted on a set that pins something,
check it somewhere that does not.

## 10. Calm, per voice, and three corrections to section 9

Luke, hearing the Calm set: "our bass is substantially quieter... the rebirth
version has so much more character than ours." He was right about the direction
and the section-9 numbers were wrong about the size. Three separate mistakes,
all mine, all worth writing down.

### 1. The reference was not what its directory name says

`renders/nofxexports/Calm.WAV` carries about **10 dB more energy at 100 to
400 Hz than the SUM of ReBirth's own two solo exports of the same song**. It
cannot be a plain rack-off render of this arrangement. Section 9's "15 to 19 dB
hole" was measured against it.

Against exports made here today, one 303 at a time, the deficit is **2 to 10 dB**.

| | 63 | 100 | 158 | 250 | 396 | 632 | 1000 | 1581 | rms |
|---|---|---|---|---|---|---|---|---|---|
| bass 1, ours minus ReBirth | -1.9 | **-8.0** | **-9.8** | -5.4 | -3.2 | -2.7 | -4.1 | -5.3 | -1.0 |
| bass 2, ours minus ReBirth | -4.0 | -3.8 | -3.0 | -2.9 | -2.6 | -3.3 | -5.4 | **-8.4** | -5.3 |

Both voices are short. Bass 1 worst through the 100 to 160 Hz body, bass 2
worst at the top and 5.3 dB down overall. **Do not use `nofxexports/` until each
file in it has been checked against a fresh export.**

### 2. The window was never bass 1 alone

Section 9 says Calm's opening is bass 1 alone and cites 0.00 percent of
ReBirth's energy above 4 kHz. That proves the DRUMS are silent. **Bass 2 plays
throughout it**, at cutoff 8 and resonance 127, and supplies most of the energy
above 158 Hz. The check was read as more than it establishes.

### 3. Soloing a saved song means the TRACK, not the chunk

The first attempt patched the channel on/off bytes in the `MIXR` chunk. All
three variants exported IDENTICAL, which is the only reason it was caught. **A
chunk is the save-time panel and the track is what plays**, which this project
has now hit on the PCF's routing, on the 303's knobs and here. Soloing works by
zeroing the other channels' volume events in the mixer TRACK, and the fixtures
are `fixtures-2026-08-29/calm-solo/Calm_t_bass1only.rbs` and `..._bass2only.rbs`.

### What the per-voice numbers rule out

None of it is the day's work: opening the filter four octaves, moving the
envelope's closed end, and changing the square's duty all leave the deficit
within a dB. It is not the corner, the envelope or the waveform. Both voices
are simply quiet, bass 1 in its body and bass 2 everywhere, which is a
different defect from any of the five this file opened with.

## 11. Luke found it: the gap on Calm is the DISTORTION, not the 303

Luke, 2026-08-30, after testing in ReBirth himself: "the bass 2 tracks have
drastically different textures and it seems that the reason is that distortion is
still being applied to bass 2 in that test where it isn't present in ours."

He is right, and it is worse than that. Read off Calm's mixer TRACK:

    bass 1   delay send 0    DIST switch 1 at tick 0
    bass 2   delay send 95, rising to 107    DIST switch 1 at tick 0
    808/909  delay send 0    DIST switch 0

**Section 10's per-voice exports had the distortion and a heavy delay send live
on ReBirth's side and nothing on ours.** The earlier attempt to turn the rack off
patched the `DIST` and `DELY` CHUNKS, which does nothing, because the per-section
switches are in the mixer track. The chunk-versus-track rule, for the fourth time.

### With the rack genuinely off on both sides, the 303 is essentially there

Absolute band energy, ours minus ReBirth, dB, on Calm's own bass lines:

| | 63 | 100 | 158 | 250 | 396 | 632 | 1000 | 1581 |
|---|---|---|---|---|---|---|---|---|
| bass 1 | +1.0 | -0.5 | +0.3 | +0.3 | +0.8 | +1.2 | -0.1 | +0.1 |
| bass 2 | +0.1 | +0.2 | +0.4 | +0.6 | +2.0 | +0.7 | -0.0 | +0.1 |

**Every band within about a dB, on real music, on both voices.** Section 10's
"both voices are short by 2 to 10 dB" is retired with section 9's "15 to 19".
There was never a 303 deficit on Calm at all.

### And with the distortion on, it is the whole gap

Delay and compressor off on both sides, distortion on, one voice at a time:

| | 63 | 100 | 158 | 250 | 396 | 632 | 1000 | 1581 | 2828 | 5656 |
|---|---|---|---|---|---|---|---|---|---|---|
| bass 1 | -7.1 | -11.3 | -11.9 | -4.7 | -1.4 | +1.2 | -0.0 | 0.0 | -1.1 | 0.0 |
| bass 2 | -6.7 | -6.4 | -5.7 | -4.8 | -4.5 | -4.9 | -7.1 | -10.1 | **-19.9** | **-47.9** |

Two distinct failures.

**The sign is wrong at the bottom.** ReBirth's distortion ADDS 2.8 dB at 63 Hz on
bass 1 and ours SUBTRACTS 5.2.

**And it generates almost no upper spectrum.** On bass 2 ReBirth's distortion
produces a whole band above 2 kHz that ours does not produce at all: 19.9 dB
short at 2.8 kHz and **47.9 dB short at 5.7 kHz**. That upper-octave content is
the "character" Luke has been describing since the first listening set.

### What this means for the plan

The 303 supertask spent 2026-08-29 chasing filter, envelope and accent
differences of a few dB. Those fits are real and they hold. But **the audible gap
on real music was a 48 dB hole in the distortion**, which is supertask 7 and
barely started, and every Calm comparison before this one had the distortion live
on ReBirth's side and silent on ours, so it was measuring the distortion and
calling it the 303.

Listening set: `listen-calm-dist-2026-08-30/`, the same two voices with the
effects off and then with the distortion on, so the difference between the two
pairs IS the defect.

## 12. Songs, not fixtures, and the first thing they found

Luke, 2026-08-30: "write a song and output it to RBS format, one 303, no 808, no
909... the only goal is for me to test our 303 against ReBirth using only the
filters and knobs that are assigned to the ReBirth 303 engine before they hit
the fx chain. It should have all effects disabled."

`tools/rbsgen/gen_303_song.py` writes four, in `fixtures-2026-08-30/303-songs/`.
`rbsgen` gained two things it lacked: a multi-pattern BANK, so a song can have
an arrangement, and TIMED KNOB EVENTS on the 303 track, so a knob can be ridden
rather than parked. Both default to None and the fixture generators are
byte-identical.

  * **groove**, 16 bars at one static patch, saw and square, nothing moving.
  * **tour**, 32 bars walking one knob at a time across its whole range while
    the line keeps playing: cutoff, resonance, env mod, decay, accent, then
    everything up. Sweeps ride on the sixteenth so they are heard as gestures.

All four pass `isolation_check.py --device bass1 --effect none`.

### They round-trip cleanly, which validates the format

Brightness traced every two bars, ReBirth against ours: **shape correlation
0.968 to 0.999** on all four. The arrangement, the pattern changes and every
sweep survive into ReBirth exactly as written, which is the first time this
project has put timed automation into a generated song and had it play back.

### And the first thing they found: THE SAW

| | our brightness against ReBirth's |
|---|---|
| groove, saw | **-3.6 dB** |
| groove, square | -0.5 |
| tour, saw | **-2.2 dB** |
| tour, square | +0.7 |

**Our saw runs 2 to 4 dB darker than ReBirth's and our square is within half a
dB.** A waveform-specific difference, constant across every sweep in the tour,
which is why the correlation stays near 1 while the offset does not close.

None of 2026-08-29's four fits could have found it. **Every 303 fixture in the
set runs the saw**, so the square was never a control, and a difference that
lives in one waveform and not the other is invisible when only one is measured.
That is the same shape of mistake as the env-mod-0 batch and the isolated-note
accent set: a defect hiding in what every fixture holds constant.

Listening set `listen-303-songs-2026-08-30/`, with the `.rbs` files copied in so
they can be opened in ReBirth directly.

## 13. Batch B8: our oscillator is IDEAL and ReBirth's is not. RETIRED, see 14.

The listening songs found the saw 2 to 4 dB darker than ReBirth's while the
square was within half a dB. B8 chases that: both waveforms, six pitches, the
filter driven out of the way by the envelope (cutoff 127, env mod 127, decay
127, read from 50 to 300 ms) so only the oscillator shapes the spectrum below a
couple of kHz.

### The measurement

Harmonics relative to each side's own fundamental, pooled over six pitches:

| | h2 | h3 | h4 | h5 | h6 | h8 | h10 | h12 |
|---|---|---|---|---|---|---|---|---|
| ReBirth saw | -3.7 | -6.6 | -8.9 | -10.9 | -12.6 | -15.3 | -17.7 | -19.7 |
| ours saw | -7.2 | -11.1 | -13.7 | -15.7 | -17.4 | -20.0 | -22.1 | -23.8 |
| **an ideal saw** | **-6.0** | **-9.5** | **-12.0** | **-14.0** | **-15.6** | **-18.1** | **-20.0** | **-21.6** |

**ReBirth's saw sits +2.6 dB above an ideal sawtooth and ours sits 1.8 dB below,
a gap of 4.45 dB.** With the drive backed off ours is an ideal saw to 0.1 dB, so
1.7 dB of the gap is our own `satAsym` rounding the ramp toward a sine and the
remaining 2.7 is ReBirth's.

Harmonics stronger than 1/k relative to the fundamental means a WEAK
FUNDAMENTAL, which is a highpass with its corner near the note. The ratio to
ideal grows 1.29 at h2 to 1.44 at h8, which fits a one-pole highpass at about
the fundamental, and it agrees with a measurement made a day earlier for a
different reason: ReBirth's fundamental gains 2.2 dB from 65 Hz to 118 Hz where
ours is flat.

**The measurement survives its own audit.** It was first taken by zero-padding
before windowing, which windows the padding rather than the signal. Redone
correctly the gap is 4.45 dB against 4.09, so the error was 0.36 dB and the
finding is not an artefact of it.

### Two dormant constants fix it, and the regression net rejects them

`REBORN_A_HP`, present since August and defaulted off, is described in the source
as a "fundamental-thinning highpass". At 90 Hz with `DRIVE` 0.3 it takes B8's
saw error from 4.41 to **0.29** and the square from 2.76 to **0.41**.

The net says no, and not marginally:

| candidate | ALL 373 | worse |
|---|---|---|
| `A_HP` 90 | **+0.776** | 286 |
| `A_HP` 90 with `DRIVE` 0.3 | **+2.723** | 332 |
| `DRIVE` 0.3 | **+1.704** | 314 |

### Placement was the obvious suspect and it is not the answer either

`A_HP` sits at the very output, after the ladder and the VCA. A highpass BEFORE
the ladder is shaped by the ladder afterwards and one after it is not, and those
coincide only when the ladder is wide open, which is exactly B8's condition and
no other fixture's. `REBORN_OSC_HP` was added to test that.

| | B8 saw | B8 square |
|---|---|---|
| shipped | 4.47 | 2.82 |
| `A_HP` 90, output side | 1.56 | **0.37** |
| `OSC_HP` 120, before the ladder | **0.75** | 3.94 |

**Neither placement satisfies both waveforms.** Before the ladder fixes the saw
and wrecks the square; after it fixes both on B8 and wrecks the net.

### NOTHING SHIPPED, and what would resolve it

`A_HP`, `OSC_HP` and `DRIVE` all remain at their defaults, which are off, off and
1.0. The build is unchanged.

Two measurements are each sound and they disagree, which on this project has
always meant they are covering different regimes. **B8 is the only fixture in the
set that measures with the filter driven open**, and the net is entirely fixtures
with the filter engaged. The next step is to measure the harmonic structure with
the filter ENGAGED at a known setting on both sides. If the 4.45 dB gap survives
that, the oscillator finding is real and the fix is being applied wrongly. If it
vanishes, then B8's open state is not the same open state on both sides and the
finding belongs to the envelope at its extreme rather than to the oscillator.

Do not fit anything here until that is answered.

## 14. RESOLVED: it is not the oscillator, and section 13 is retired

Section 13 said ReBirth's saw carries 2.6 dB more harmonic energy than an ideal
sawtooth while ours sits 1.8 dB below, and proposed that a missing highpass near
the fundamental explains it. The regression net rejected the fix at +0.776 with
286 of 373 cells worse. **The net was right and section 13 was wrong.**

### The test that settled it

B8's open cells drive the filter out of the way, and they are the only fixtures
in this project that do. Adding cells with the filter ENGAGED, statically, at
cutoff 64 and resonance 16, asks the same question in the regime every other set
uses. Mean harmonic gap over h2 to h12, ours minus ReBirth:

| | saw | square |
|---|---|---|
| filter OPEN (resonance 0) | **-4.45** | -2.80 |
| filter ENGAGED (resonance 16) | **+1.30** | +3.58 |
| engaged, with our own `LF_HP` switched off | **-0.22** | +2.03 |

**The gap flips sign.** An oscillator that was 4.45 dB short would be short in
both conditions, because a lowpass attenuates both sides alike at a given
frequency. It is not short in both. **With the filter engaged our saw matches
ReBirth's to a fifth of a dB.**

### What the open cells were really showing

They run resonance 0. Our low-end highpass is `fc(r) = LF_HP_MAX * r / (r +
LF_HP_KNEE)`, which is **exactly zero at resonance 0**, so in those cells we have
no low-end highpass at all while ReBirth evidently has something. In the engaged
cells at resonance 16 ours is alive at 57 Hz and the gap closes; switch it off
there and the gap reopens by 1.5 dB.

So the whole "our oscillator is an ideal saw and ReBirth's is not" reading is the
**resonance-0 hole in `LF_HP`**, seen at the one resonance where nothing fills it.
That hole is not news. `bass_synth.cpp` has carried this since 2026-08-02:

> KNOWN INCOMPLETE AT RESONANCE 0, and deliberately shipped that way. fc(0) = 0
> while ReBirth still loses up to 5.7 dB there.

Section 6 found it again from a different direction on 2026-08-29. This is the
third time it has been rediscovered wearing a different hat.

### A floor does not fix it, and the reason is worth keeping

`LF_HP_MIN` adds a resonance-0 floor, and it helps the open cells and hurts the
engaged ones in the same move:

| floor | open saw | open square | engaged saw | engaged square |
|---|---|---|---|---|
| 0, shipped | -4.46 | -2.82 | +1.30 | +3.58 |
| 50 Hz | -3.33 | -1.67 | +2.51 | +4.81 |
| 90 Hz | -1.58 | +0.08 | +3.47 | +5.77 |

A floor raises the corner at EVERY resonance, and the engaged cells say ours is
already slightly too strong at resonance 16. What the two conditions want
together is a corner that is HIGH at resonance 0 and LOWER by resonance 16,
which is a law that DECREASES with resonance where the shipped one increases.
That contradicts the 2026-08-02 fit, so it is not a tweak and not something to
decide at the end of a long session.

### Nothing shipped, and what stands

`A_HP`, `OSC_HP`, `DRIVE` and `LF_HP_MIN` are all at their defaults: off, off,
1.0 and 0. The build is unchanged.

What stands: **the 303's oscillator is not the problem.** With the filter engaged
our saw is within 0.22 dB of ReBirth's harmonic structure and our square within
2.03. The saw-versus-square asymmetry the songs found is real, but it lives at
the resonance-0 end of the low-end highpass and not in the waveform generator,
and section 13's proposed fix was fitting a filter defect with an oscillator
change.

## 15. The waveform SHAPE says 102 Hz highpass. The net says no. SETTLED: no.

**Resolved 2026-08-31: do not add the highpass. The net was right.** The section
below is the original unsettled writeup; this is the resolution.

The tie-breaker was to score the net's OWN metric (own-energy-normalised
third-octave band shape) on the LISTENING SONGS rather than the static fixtures,
ours against ReBirth, with `OSC_HP` off and at 102. On real music it moves the
full spectrum AWAY from ReBirth on every song, saw ones included:

| song | shipped | OSC_HP 102 | change |
|---|---|---|---|
| groove saw | 3.68 | 4.43 | **+0.75** |
| tour saw | 4.00 | 4.61 | **+0.61** |
| groove square | 2.02 | 3.40 | +1.38 |
| tour square | 2.74 | 3.50 | +0.76 |

The highpass improves the two-number BRIGHTNESS ratio (groove saw -3.63 to -2.69
dB against ReBirth, tour saw -2.17 to -1.15) but only by cutting the 60 to 250 Hz
denominator, and the 23-band SHAPE worsens at the same time. It also over-brightens
the SQUARE, which the songs found already correct to 0.5 dB and which OSC_HP
pushes 1.5 to 1.7 dB too bright. So the net's fixture rejection (+0.815, 361 of
373 worse) is not an operating-point artifact; the full spectrum genuinely moves
away, on musical material, on the net's own terms.

**The waveform observation stands and its INTERPRETATION was wrong.** ReBirth's
saw really is a step decaying to a plateau, and that really is a highpass
somewhere in its path at the filter-open condition. But "add a one-pole highpass
before our ladder" is the wrong fix, because our saw's darkness is not a weak
fundamental to be cut. The B8 harmonics read "harmonics strong relative to the
fundamental", which has two causes, a weak fundamental (cut lows) or strong
harmonics (a brighter, edgier oscillator), and the cycle shape pointed at the
first while the full-band test on music points at the second: cutting our lows
worsens the match, so the missing energy is in the HIGHS, not an excess in the
lows.

**What is still open**, and it is a fresh investigation, not a tuning: our saw is
2 to 4 dB too dark on real music and the fix is high-harmonic content at the
oscillator, not a filter stage. Testing it needs a source-level highs probe
independent of the resonance-scaled B shelf, which does not exist yet. That is
the next 303 waveform step. `OSC_HP`, `A_HP` and `DRIVE` all stay at their
defaults; nothing shipped.

---

### The original unsettled writeup follows.

Luke asked what it would take to recreate ReBirth's saw identically. Answering
that needed the one thing this project had never looked at: the WAVEFORM.

### ReBirth's saw is not a ramp

Pitch-synchronously averaged over twelve cycles, ReBirth's saw is a sharp step
that decays exponentially to a plateau inside one cycle. That is what a ramp
looks like after a highpass, and it is not what a lowpass does to anything.

The settle time constant, read straight off the waveform, across six pitches
spanning an octave:

| note | f0 | ReBirth tau | as a corner | ours tau | as a corner |
|---|---|---|---|---|---|
| 0 | 65.4 Hz | **1.59 ms** | 100 Hz | 4.30 ms | 37 Hz |
| 4 | 82.4 | **1.57** | 101 | 3.49 | 46 |
| 8 | 103.8 | **1.54** | 103 | 2.81 | 57 |
| 10 | 116.5 | **1.52** | 105 | 2.52 | 63 |

**ReBirth's tau is constant to 4 percent across an octave.** A fixed time
constant under changing pitch is a fixed-FREQUENCY filter and cannot be a
property of the waveform. Ours is a constant 0.29 of a CYCLE whatever the pitch,
which is a waveform property and not a filter.

So the answer to the question as asked: **an ideal ramp through a one-pole
highpass at 100 to 105 Hz, before the ladder.** `OSC_HP` 102 takes the
waveform-shape error from 0.163 to **0.054** and beats the same filter placed at
the output (0.087). The drive should stay at 1.0; reducing it makes the shape
worse, which is the opposite of what the magnitude fit wanted.

### And the net rejects it as hard as it has rejected anything

| | ALL 373 | better | worse |
|---|---|---|---|
| `OSC_HP` 102 | 1.948 to **2.763** | 12 | **361** |

Not one of the sixteen sets improves.

### Why they can disagree, which is the finding under the finding

**Every metric this project has used throws PHASE away.** Harmonic magnitudes,
third-octave band energy, brightness ratios: all of them. Two signals with
identical magnitude spectra can be entirely different waveforms.

That is not hypothetical here. Section 14 concluded from harmonic MAGNITUDES
that our filter-engaged saw matches ReBirth's "to a fifth of a dB". The two
cycles from that same pair of renders do not overlay at all. **The magnitude
metric was blind to the difference the shape metric measures**, and section 14's
"it is not the oscillator" rests on that blind metric.

### Status: UNSETTLED, nothing shipped

`OSC_HP` stays at 0 with everything else. Section 14 is not retired, because its
reasoning about the resonance-0 `LF_HP` hole is independent and still stands, but
its headline claim that our saw matches ReBirth's is now known to be a
magnitude-only statement.

Two ways forward and they are not equivalent:

  * **Luke's ear**, on `listen-303-hp-2026-08-30/`: our shipped build and our
    highpassed build against ReBirth, on the songs. If ReBirth sounds thinner
    than both, the highpass is right and the net is measuring something else. If
    ReBirth sounds fuller than the highpassed version, the highpass is wrong
    however good the waveform match is. Fast, and it is what the songs are for.
  * **A PHASE-AWARE regression net.** Slower and the better long-term answer,
    because the project currently cannot see this class of difference anywhere.
    The cycle-shape measure built here is one candidate and it is a day old and
    unvalidated.

**Do not ship the highpass on the strength of the shape metric alone.** It is
new, it was built to answer this one question, and a change that makes 361 of
373 cells worse deserves better evidence than one afternoon's instrument.

## 16. The plan from here

Superseded by `STATUS_2026-08-30.md`, which carries the ordered plan, the one
blocking decision and the week's learnings. Kept here so a reader of this file
alone is not left without it:

1. **Settle the 102 Hz highpass**, by Luke's ear on `listen-303-hp-2026-08-30/`
   or by building a phase-aware regression net. Blocking. Section 15.
2. **The resonance-0 hole in `LF_HP`**, structural, found three times, largest
   known-open item in the 303. Sections 6 and 14.
3. **Luke's ear on `listen-303-songs-2026-08-30/`**, especially the env-mod
   sweep on the all-slides pattern and the accent sweep on a line with rests.
4. **Supertask 7, the distortion**, where the audible gap on real music actually
   is: 47.9 dB short at 5.7 kHz on Calm's bass 2. Section 11.

### The old plan, for the record

In the order they should be taken, with the reason for the order.

**B4, the envelope (defect 2). DONE 2026-08-29**, section 5. It was not what
this plan said it was: a timing defect, not a depth one, and the "8 dB too far"
came from a window bug and an uncalibrated measure. Both are written up.

**The resonance-0 passband droop, section 6.** The corner is right to 0.08
octaves, defect 3 is retired, and B5 has now shown that the see-saw in section 1
was mostly B1's reference rather than our filter. What survives is one thing:
**at resonance 0, ReBirth's filter droops from the very bottom of the band and
ours does not**, by 5.2 dB at cutoff 32, 7.1 at 64 and 8 or more at 112. It is
the column `bass_synth.cpp` has flagged as KNOWN INCOMPLETE since 2026-08-02,
and B5 is the fixture that comment says was needed to identify its shape.

Fit it on B5's dense grid with `bass303_grid.py`, not on B1, and check every
step against `regress_ab.py` with the engine flag rather than only at the end.
Read section 6's caution about the anchor first: the measurement cannot yet tell
a droop we lack in the midband from a lift we lack at the bottom, and those are
not the same fix. Settling that is the first job, and Luke's ear on
`listen-303-filter-2026-08-29/` may settle it faster than another fixture will.

The mechanism is already in the code and both halves are env-overridable. The
passband makeup gain, `(1 + k * 0.75) * REBORN_MAKEUP`, restores what a resonant
ladder loses at DC, and ReBirth evidently restores less of it. The resonance law
`K_B` with its `K_B_SLOPE`, `K_B_PIVOT`, `K_CUT` and `K_CUT_TOP` terms is the
other half. **Note that a broadband makeup change alone CANNOT fix a see-saw**,
because it moves the whole curve together; the peak has to rise relative to the
passband, which means more k with less compensation, and our ladder self-
oscillates at k = 4.0, which bounds how far that can go. Whether the bound bites
is the thing to find out first, because if it does the diode-ladder fork comes
back onto the table with a real measurement behind it for the first time.

`score303`'s fixtures are not in the workspace at all, not merely four of them:
nothing under `fixtures-*/` matches `scratch_303_*` and its renders are stale, so
regenerate them before leaning on that harness. `regress_ab`, `batchI`, `batchQ`
and `accentdepth` all work today, with the engine flag.

**B4 went before the cutoff refit, and the order paid twice.** Both touch the
same corner, and doing the envelope first meant its constants were fitted against
the anchor they will keep. It paid a second way that was not foreseen: taking the
envelope's timing out of the picture is what made the leftover error legible as a
slope error, and it is what got the regression net run with the engine flag,
which is how the refit turned out to be unshippable before it shipped rather than
after.

**B5, the accent's remaining depth (defect 4).** About 0.8 dB of uniform excess
on accented steps and a brightness law that falls away above knob 96. `ACC_SWEEP`
is already optimal, so it is elsewhere in the accent path; `ACC_RESO` (0.1) and
`ACC_LVL` (3.2) are the untested candidates.

**B6, the resonant peak (defect 5).** Last because it is the hardest and the most
structural. B1 has now measured the surface the shelved diode-ladder plan was
shelved for want of: the shortfall grows monotonically with BOTH cutoff and
resonance, reaching 11.5 dB, which is exactly the "RESO-and-cutoff-dependent K_B
refit" that plan said was needed and could not fit from a one-dimensional sweep.
Try the refit before reopening the diode fork.

**B7, the listening set.** Once B4 and the cutoff refit are in, cut a 303 set per
the listening protocol: the null pair, then one 303 alone at three or four
characteristic patches, ours against ReBirth, on Calm's rack-off window or a
purpose-built piece. One 303, never two.

**Then the voice question.** Whether Open303 or the native TPT voice ships as the
default is open and is Luke's call. It should be asked with his ear on a fair
comparison, which means after the TPT voice has had B4 and the cutoff refit, not
before. `listen-303-solo-2026-08-29/` is the current three-way and it predates
both.

### What NOT to do

**Do not touch Open303.** Luke, 2026-08-29: it stays as close to its own source
as possible. It is a reference and an alternative voice, not a parity target.

**Do not fit two of these at once.** Defects 2, 3 and 5 all move the corner and
the audit is full of joint refits that were right in aggregate and wrong in every
part. B1's whole value was showing that 3 and 5 are independent; fitting them
together would throw that away.

**Do not measure the 303 on a demo song again without checking the window.** Every
303 export in `rb338-exports/` runs through the rack. Calm's opening 50 seconds is
the one clean single-303 reference we have and it was found by looking for a
window rather than a file.

## 17. The source-level highs probe: built, tested, and the deficit is REGIME-dependent

Section 15 left the next 303 waveform step as "build a source-level highs probe
independent of the resonance-scaled B shelf, then score the net's own metric on
the listening songs." Done 2026-09-03. The probe is `REBORN_OSC_HS_GAIN` (dB) and
`REBORN_OSC_HS_FC` (Hz) in `bass_synth.cpp`: a one-pole high shelf on the RAW
oscillator, before the drive and the ladder, so the ladder shapes the added highs
afterward. It is self-gating by how open the filter is, which is the property
section 15 asked for and the thing the output B shelf does not have. Default off,
shipped output bit-identical.

### First, the metric was reproduced, and it caught a scope error in section 15

`tools/analyze/song_highs.py` re-renders our side from the song `.rbs` with the
TPT voice and scores `regress_ab`'s `band_shape` against the ReBirth export, per
band. It reproduces section 15 only over the FULL song, not the net's own 8 s
window: groove saw 3.69 against 3.68 and tour saw 3.95 against 4.00 full-length,
where the 8 s window gives the tour 1.20 because its first 8 s is only the bright
reference riff. So section 15's tour number was a full-song score, and the tour's
hard regimes (env mod on slides, accent, everything-up) all sit past the 8 s the
net normally reads. The static groove is unaffected by this because its patch
never changes.

### The deficit is a treble shortfall that GROWS WITH FILTER DRIVE

`tools/analyze/tour_sections.py` windows the tour's eight four-bar sections. The
shortfall is small where the filter is driven gently (reference riff rms 1.19,
and we are even brighter than ReBirth at 5.7 kHz there) and grows with env mod,
accent and resonance: env-mod-on-slides 3.76, accent 4.04, everything-up 4.96,
all worst at 5 to 7 kHz where we run 10 to 15 dB dark. The groove, a single
resonant patch (cutoff 52, reso 88, env mod 78), is different again: a NOTCH at
4.5 kHz (-13 dB) with an EXCESS at 7 kHz (+4), which is a resonant-peak placement
difference, not a rolloff. So there are at least two things: a broadband treble
shortfall under filter drive, and a resonant-notch difference in the static
resonant patch.

### The probe helps the songs and the net REJECTS it, the same shape as OSC_HP

A shelf with the corner up at 2.5 kHz, so it touches only the treble-deficit
region and not the mids where the square is already right, improves three of the
four songs on the full-song metric: at gain 3, fc 2500, groove saw 3.69 to 3.58,
tour saw 3.95 to 3.47, tour square 2.70 to 2.34, groove square neutral at +0.03.
Band by band on the tour it fills the deficit cleanly, every band from 1.1 to
7 kHz moving toward ReBirth. It does NOT fix the groove's 4.5 kHz notch, which a
monotonic shelf cannot, and slightly worsens the groove's 7 kHz excess.

But the fixture net rejects it, +0.058 over 373 cells with 213 worse. Milder than
OSC_HP's +0.815 and 361 worse, and the same KIND of failure: it splits by regime.
It HELPS the envelope and sweep sets (batchG -0.101, envshape -0.053, envdomain
-0.267, envfloor -0.225) and HURTS the static and accent sets (batchQ +0.272,
accentdepth +0.263, batchI +0.194, accentprobe +0.180, decayprobe +0.150). The
same split the songs show: the swept tour is helped, the static groove is not.

### What stands

A CONSTANT source high-shelf is not shippable, for the reason a constant highpass
was not: the treble deficit is regime-dependent, present where the filter is
driven or open and absent or reversed where it is static, so a constant treble
term fills it in one regime and over-brightens the other and the aggregate net
goes the wrong way. Section 15's "high-harmonic content at the oscillator" is
partly right in that adding treble helps the driven regime, and wrong in that the
lever is not a static source property. The probe stays in as a dormant default-off
control alongside `A_HP`, `OSC_HP`, `B_GAIN` and `DRIVE`; nothing shipped.

The redirection this measures: the deficit lives in how much treble the filter
delivers when the envelope drives the corner up, which is the same shape as the
skirt-topology residual the audit flagged 2026-07-30 (our stopband does not
steepen with cutoff the way ReBirth's does) and is adjacent to the resonance-0
`LF_HP` hole in section 14. A regime-aware term (gated to the envelope or the
corner) or the structural skirt work are the candidates, and which to take is
Luke's call. The groove's 4.5 kHz resonant notch is a separate, static defect
that the static fixtures should be able to localize and that a shelf does not
touch.

## 18. The filter direction: it is a REFIT, not a topology change, and the song darkness is mostly DYNAMIC

Luke's call 2026-09-03: treat the treble deficit, the skirt residual and the
resonance-0 `LF_HP` hole as one filter investigation. Before touching the filter I
re-read the audit, and two things there change the shape of the work.

**It is not a topology change.** The 2026-07-30 batch S reading I leaned on above
(ReBirth's skirt steepens with cutoff, ours does not) was RETRACTED 2026-08-06 as
a band-anchoring bug: the measurement band walked with the rising peak. What
survives, on the corrected instrument (`harm_response.refine_f0`), is that both
ReBirth and ours are 4-pole, and our skirt is only 1 to 2 dB/oct too steep at high
cutoff and about nil at mid cutoff. The audit already demonstrated once that a
refit of the corner and the resonance-to-corner coupling (`RESO_OCT`,
`RESO_OCT_CUT`, `HFQ`) drove the static HF deficit to zero (k20-40 -2.41 to +0.05).
`RESO_OCT`/`RESO_OCT_CUT` at 0.25/0.35 are that refit's values and are already
shipped, so that half was applied. The diode and rb forks stay shelved; this is a
constants problem inside the 4-pole ladder.

**The current static filter is only mildly deficient, and the song darkness is
mostly dynamic.** Measured on the current shipped TPT voice (`REBORN_BASS_ENGINE=
tpt`, batchH, corrected instrument, `kband.py`):

| band | k1-5 | k5-10 | k10-20 | k20-40 | k40-90 |
|---|---|---|---|---|---|
| mean, ours minus ReBirth dB | +0.07 | +0.12 | -0.11 | **-1.14** | **-1.57** |

`curve_fit` puts the whole static curve at 2.83 dB rms, near the best the
2026-07-31 refit reached (2.63) and far from the pre-refit 4.75. So the August
reworks reintroduced part of the static HF deficit, about 1.5 dB at the top, and
that part is a clean refit target. But it is small next to the songs, where the
driven sections run 10 dB or more dark at 5 to 7 kHz. The static batchH is env mod
0, so it never exercises the regime the songs are dark in. The dominant part of
the darkness is therefore DYNAMIC, it appears when the envelope and accent drive
the corner, and it will not be found or fixed on a static grid.

**Where this leaves the filter work.** Two targets, and the small one is not the
one that matters. The static HF deficit (about 1.5 dB at k20-40 and above) is a
refit of the corner and the reso coupling, gated on `regress_ab` and the songs.
The dynamic treble deficit is the larger one and needs a time-resolved measurement
of how the corner and the treble evolve through a driven note, ours against
ReBirth (the `env_traj.py` class of instrument), before any constant is fitted,
because fitting a static lever to a dynamic defect is the trade the audit is full
of. Nothing shipped.

## 19. The dynamic deficit is the ENV-MOD CORNER running 0.5 octaves short

Measured 2026-09-03 on the `envdomain` tied-note fixtures (resonance 60, cutoff
64, env mod 32 to 127, tied so one envelope is visible onset to floor), TPT voice,
against ReBirth's exports. Two instruments agree: the validated `env_traj.py`
(resonant-peak trajectory) and a new `treble_traj.py` (per-frame treble energy in
2 to 7 kHz relative to the passband, so a level offset cancels).

**The corner does not open far enough under env mod.** At env mod 127 decay 127
ReBirth's corner reaches 5203 Hz at the attack and ours reaches 3585, a peak that
is 0.54 octaves short. The peak is the robust number here, measured well inside
the tracking band; the low end of both traces clamps at the tool's 1.8*f0 = 198 Hz
band edge, so "the floor matches" is a measurement artifact, not established, and
the depth-versus-anchor split must be settled by the fit rather than read off this.
What is solid is the peak, and its shortfall is roughly CONSTANT across the whole
env-mod range rather than growing with it:

| env mod | ReBirth peak Hz | ours peak Hz | shortfall oct |
|---|---|---|---|
| 32  | 1498 / 1537 | 1084 / 1082 | -0.47 / -0.51 |
| 64  | 2164 / 2443 | 1583 / 1577 | -0.45 / -0.63 |
| 96  | 3180 / 3597 | 2123 / 2552 | -0.58 / -0.50 |
| 127 | 4763 / 5203 | 3144 / 3585 | -0.60 / -0.54 |

(the two columns are decay 96 / decay 127.)

**The treble deficit tracks the corner gap directly.** `treble_traj` on em127
dec127: the worst treble deficit is -12.4 dB and it lands where the corner is 0.83
octaves low; the deficit shrinks to zero as the corner decays past the treble band
on both sides. So the "saw too dark" is the corner not reaching high enough while
the envelope holds it up, exactly the driven regime section 18 pointed to, and it
is not the oscillator.

**A secondary decay-rate difference.** Our octave-domain decay tau runs about 10
to 15 percent fast at decay 127 (2.34 s against ReBirth's 2.71 at em127), so ours
also falls off the corner sooner. Smaller than the peak shortfall but the same
sign, and it should be checked when the depth is refit rather than fitted at once
with it.

**How this squares with the static corner and the 2026-08-02 EM_OCT work.** The
static (env mod 0) corner matches ReBirth to within about 0.1 to 0.2 octaves on
batchH (reso 60 cutoff 64 reads -0.20). Turning env mod ON adds the rest of the
shortfall, and it adds it as a near-CONSTANT ~0.3 octaves from env mod 32 to 127
rather than growing with the knob. That is the key to which lever: `EM_OCT`
multiplies the env-mod knob, so raising it would over-open at env mod 127 and
under-open at env mod 32 and could not produce a constant offset. The right lever
is one of the env-mod-INDEPENDENT corner terms, `EM_FLOOR`, `EM_CENTRE`, or the
reso-to-corner coupling at reso 60, not `EM_OCT`. The 2026-08-02 raise of `EM_OCT`
(2.2 to 4.45) was scored on trajectory SHAPE and the octaves-vs-frequency domain,
not on the absolute peak height against ReBirth, which is the coverage gap this
measures.

**The fit, when it is done.** Raise the env-mod peak corner about half an octave
using a term that is constant in the env-mod knob (candidates above), verify it
against the per-env-mod peak-corner table rather than one cell, and check the decay
tau (ours ~10 to 15 percent fast). Gate every step on `regress_ab` (the static
net), on `song_highs.py` (the songs) and finally on Luke's ear, since four defects
in this project were caught by ear and none by these metrics. The reso dependence
is not yet mapped: `envdomain` is reso 60 only, while batchH shows the static
corner shortfall is worst at mid reso (reso 60 -0.20) and near zero at high reso
(reso 110 to 127), so the refit must be checked across resonance before it ships,
not fitted at one resonance. Nothing shipped.

## 20. The env-mod corner is capped by the FC_MAX ceiling, and a global open fails the net

Fit attempt 2026-09-03. Two things came out of it: the mechanism is confirmed and
larger than a constant, and the naive fix is rejected by the net for the reason
section 19 warned of.

**The ceiling is the cap.** `FC_MAX` (default 6000 Hz, `bass_synth.cpp:1658`)
clamps the corner, and its own comment already flagged this: ReBirth's corner is
5830 Hz at cutoff 90 env mod 127, "already at this clamp," and `CUT_SCALE` 0.78 is
"very likely compensating for a ceiling that is too low." Confirmed by measurement.
Raising `FC_MAX` alone does nothing (shipped's sweep does not reach 6000: em127
peak stays 3585). But `FC_MAX=9000` with an env-mod lever lets the corner reach
ReBirth's em127: `EM_CENTRE=0.40` gives em127 5112 against ReBirth's 5203 and em032
1466 against 1537, with the mid env mods (em064, em096) still about 0.2 oct short,
a curvature (`EM_CURVE`) residual.

**On the songs it is the biggest win yet, on the net it is rejected.**
`FC_MAX=9000 REBORN_EM_CENTRE=0.40`, full-song `song_highs`:

| song | shipped | candidate | change |
|---|---|---|---|
| tour saw | 3.95 | **1.90** | -2.04 |
| tour square | 2.70 | **1.04** | -1.66 |
| groove saw | 3.69 | 3.22 | -0.47 |
| groove square | 2.04 | 2.58 | +0.53 |

The driven regime, which is the whole "saw too dark" complaint, more than halves.
But `regress_ab` goes +0.268 over 373 cells, 274 worse, with batchI (env mod x
decay, PLUCKED) +1.696 all-40-worse, batchQ +0.522, accentdepth +0.400, while the
tied and deep-env-mod sets improve (envdomain -0.33, envfloor -0.53, batchG -0.29,
batchW -2.76). The split is exact: everything driven and tied is helped, everything
plucked, static, or accent is hurt.

**Why a global lever cannot win here.** `EM_CENTRE`, `CUT_SCALE` and `FC_MAX` all
raise the corner in regimes that ALREADY match ReBirth, not only in the mid-reso
deep-env-mod regime where it is short. `EM_CENTRE` additionally lifts the whole
trajectory including the decay tail, so a note whose peak now matches is too bright
through its decay. batchH says the static corner is short at mid reso (60, -0.20)
and matches at high reso (110 to 127, near 0), so the shortfall is not uniform in
resonance and a lever uniform in resonance must over-correct somewhere. This is the
same shape of rejection as `OSC_HP` and the source shelf: a regime-specific deficit
does not take a global fix.

**Where this leaves it.** The mechanism is now understood end to end: the songs are
dark because the env-mod corner is short at mid reso, and it is short because the
6 kHz ceiling caps the sweep and `CUT_SCALE` was pulled down to live under it. A
shippable fix is a COUPLED, resonance-aware corner refit: raise `FC_MAX`, restore
`CUT_SCALE` toward 1.05, and shape the resonance-to-corner coupling so the corner
rises at mid reso without moving high reso, with `K_HFQ` shedding Q up top so the
raised ceiling does not whistle. That is a multi-parameter fit across the full
cutoff x reso x env-mod space gated on the net, the songs and the ear, not a single
constant. The dormant probes (`FC_MAX`, `EM_CENTRE`, `CUT_SCALE`, `EM_OCT`,
`EM_CURVE`, `RESO_OCT`, `K_HFQ`) are the levers; nothing shipped.

## 21. Luke's ear on the candidate: direction confirmed, two residuals localized

Listening set `listen-303-emcorner-2026-09-03/`, three-way, cut 2026-09-03. Luke's
verdict: these are the closest to ReBirth he has heard, and the candidate
(`FC_MAX=9000 EM_CENTRE=0.40`) moves closer still. So the corner-reach direction is
confirmed by ear, not only by the band metric. He caught one residual by ear: in
the later tour saw, from about 0:30 on, ours has a slightly different color and a
bit less "chirp" in the sweeps.

That 0:30-on region is the decay sweep, the accent sweep, and the two high-reso
payoff sections. `tour_sections` on the candidate splits his observation into two
distinct measured features:

- **The "different color" is the decay sweep over-brightening up top.** With the
  candidate the decay-sweep section runs +3.2 at 5.7 kHz, +9.0 at 7.2 kHz, +8.0 at
  9 kHz, where shipped was near flat there. It is the velocity gate (`bass_synth`
  around line 1726) deliberately relaxing the HF Q-loss while the cutoff sweeps so
  the resonance rings bright, and with the ceiling raised that ring now reaches
  7 kHz. It is NOT the static Q-shed: `HFQ_ON`/`HFQ_SPAN` from 4000/2500 to
  2200/1400 leave the decay-sweep row byte-identical, so `HFQ_ON` does not reach
  it. The velocity gate is the lever, and this is a coupled interaction, not a
  one-constant tame.
- **The "less chirp" is the accent sweep still dark at 5-7 kHz.** The candidate
  more than halves that section overall (rms 4.04 to 2.17) but still runs -5.0 at
  5.7 kHz and -4.2 at 7.2 kHz, where the accent should be firing a bright corner
  chirp. That is the accent path (`ACC_SWEEP`, the accent's corner push), not the
  env-mod corner, so it belongs to the accent step, not this refit.

  Luke caught this a second way, from the WAVEFORM of the tour saw at about 0:36:
  ReBirth's slope through the accent sweep differs from ours. Measured 2026-09-03,
  per-250 ms level and spectral centroid over 34 to 45 s: the LEVEL tracks ReBirth
  within about a dB, so it is not amplitude; the difference is BRIGHTNESS. As the
  accent knob ramps 0 to 127 ReBirth's centroid climbs to ~2600 to 2800 Hz at the
  top of the sweep (43 to 44 s) where the candidate reaches ~2100 to 2400, so
  ReBirth's accent brightens the note more and the RATE of that rise, the visible
  slope, is steeper. The candidate is a touch brighter than shipped but still about
  0.2 to 0.3 octaves short at peak accent. `ACC_SWEEP` is the lever; the accent
  step owns it.

Neither is a reason the candidate is wrong; both are what the coupled refit and the
later accent work have to get right. Nothing shipped.

## 22. The refit search, and the corner fix APPLIED 2026-09-03

**APPLIED 2026-09-03**: `EM_CENTRE` 0.5 -> 0.45 and `FC_MAX` 6000 -> 9000 are now
the defaults in `bass_synth.cpp`. Suite green, and the new default scores -0.037 on
the 373-cell net against the old (1.948 -> 1.911). The mean improves; it is a
redistribution, 145 cells better and 217 slightly worse, with the gain concentrated
in the driven and deep-env-mod sets and the songs, and small nudges on the static
cells. Luke approved the lock-in after hearing the env-mod chunk match ReBirth, on
the finding (below) that the remaining mutedness he hears is a separate resonance
and accent character, not the corner. The original writeup follows.


A workflow scanned ten lever combinations (`score_candidate.py`, reduced net plus
songs, then the best on the full net). Two clear the full 373-cell net, both far
better than the `EM_CENTRE=0.40` version Luke first heard:

| candidate | full net change | tour saw | tour sq | groove saw | groove sq |
|---|---|---|---|---|---|
| `FC_MAX=9000 EM_CENTRE=0.45` | **-0.037** | -1.32 | -1.92 | -0.51 | -0.37 |
| `FC_MAX=9000 EM_OCT=5.6` | +0.036 | -1.67 | -1.90 | -0.60 | -0.10 |
| `FC_MAX=9000 EM_CENTRE=0.40` (first heard) | +0.268 | -2.04 | -1.66 | -0.47 | +0.53 |

`EM_CENTRE=0.45`, half the rejected anchor move, is the standout: it IMPROVES the
static net (-0.037) while closing all four songs, and it halves the decay-sweep
over-bright (7 kHz +9 down to +4). It is the first corner-opening candidate that
costs nothing on the fixtures. `EM_OCT=5.6` closes the tour saw slightly more but
keeps the ring, so it is the aggressive alternative, not the pick.

**A phase-invert null corroborates it with mechanism** (`null_diff.py`). Our TPT
renders are bit-identical across two renders, so the sample-domain null of shipped
minus candidate is exactly what the fix changed (a null against ReBirth is invalid
and was not done: ReBirth's phase is not reset per export). The residual is clean
and band-limited to the corner region (224 Hz to 3.6 kHz, nothing below 140 or
above 5.7 kHz, so no click or broadband artifact), and by tour section it acts only
where the envelope drives the corner (riff and cutoff sweep -22 dB, i.e. barely
touched; envmod/decay/accent/octaves/all-up carry the change), which confirms the
self-gating the approach depends on. Against ReBirth in magnitude, the candidate
moves toward ReBirth across the whole tour treble (gain +0.9 to +5.0 over 3.6 to
9 kHz, none past ReBirth). The one place it overshoots is the STATIC resonant
groove at 7 to 9 kHz (candidate +6 where ReBirth is the reference). That was first
read as the resonant peak reaching too high once the ceiling is up, but probing it
2026-09-03 shows otherwise: the groove 7 to 9 kHz overshoot is INVARIANT to both
`FC_MAX` (9000 / 7500 / 6500) and the HF Q-shed (`HFQ_ON`/`HFQ_SPAN` down to
1600/1200). So it is neither the ceiling nor the resonance Q. Shipped already sits
+3.7 at 7 kHz on the groove (brighter than ReBirth with no corner change at all),
and the corner opening amplifies that to +6. It is a PRE-EXISTING high-resonance
brightness character of the reso-88 static patch, separate from the corner refit
and not reachable by the corner or top-end levers, so it does not gate the corner
fix. It goes on the list as its own item (the reso-88 static top character), next
to the accent-sweep brightness ramp. Listening sets `listen-303-corner-v2-2026-09-03/`
(full songs) and `listen-chunks-2026-09-03/` (short A/B windows). Nothing shipped,
pending Luke's ear.

## 23. The muted chirp is the RESONANT PEAK PROMINENCE, defect 5, and a safe partial fix

With the corner locked in, Luke's ear on the short chunks found three of the four
still slightly muted against ReBirth, the envelope "chirp" reading more clearly on
ReBirth, while the env-mod chunk matched. Measured 2026-09-03 on batchH with the
corrected instrument (`harm_response`, new default), the cause is direct: our
resonant peak stands 4 to 6 dB LESS above the passband than ReBirth's at high
resonance and high cutoff, and the gap grows with both.

| cutoff | reso 90 RB/ours | reso 110 RB/ours | reso 127 RB/ours |
|---|---|---|---|
| 90  | 11.6 / 7.7 | 16.1 / 11.0 | 19.4 / 13.9 |
| 127 | 12.8 / 7.6 | 18.3 / 11.0 | 20.8 / 15.1 |

This is defect 5, the resonant-peak prominence, which the audit called the hardest
and most structural 303 item and resolved 2026-08-06 as a reso-and-cutoff-dependent
`K_B` refit, not a topology change, with `K_B_SLOPE` (default 0) the lever.

**A partial fix is safe and net-positive; a full one is bounded by self-oscillation.**
`K_B_SLOPE` raises the feedback above the resonance pivot, so it adds most at reso
127 and less at 90. At `K_B_SLOPE=0.9` the reso-127 prominence matches ReBirth
(19.3 to 21.9 against 19.4 to 20.8) but reso 90 to 110 is still 3 to 4 dB short and
reso 127 sits at the 4-pole self-oscillation cap (kBase ~3.85 of 4.0), which is the
whistle edge. The shortfall is roughly UNIFORM across reso 90 to 127 while the term
is reso-proportional, so no single `K_B_SLOPE` closes it evenly, and closing reso
90 to 110 the same amount would drive reso 127 past the cap. That is the structural
wall the diode-ladder fork was held for.

`K_B_SLOPE=0.5` is the safe partial: the reduced net IMPROVES (-0.023) with no set
regressing (no whistle blowup on the high-reso cells), the songs move a little
(tour saw -0.19, groove saw -0.07), and the reso-127 prominence lifts toward
ReBirth while reso 90 to 110 stays short. It reduces the muted chirp without closing
it. Chunks with it on the muted patches: `listen-chirp-2026-09-03/`. The decision is
Luke's: bank the safe partial `K_B_SLOPE` and accept the reso 90 to 110 residual, or
reopen the diode fork for a full prominence match. Nothing shipped for this item.

## 24. The comb-vs-peak test: NO comb, so the diode fork's premise does not hold

Luke's call 2026-09-03: settle whether ReBirth's high-resonance character is a comb
the buffered ladder cannot make (diode fork needed) or a peak we can approach (K_B),
before committing to the diode path. Measured on isolated high-resonance fixtures,
static (batchH reso 127) and driven (`fixtures-2026-09-03/combtest`: reso 127, env
mod 127, tied, cutoffs 64/90/110, exported from ReBirth). Tools
`combtest.py` and `combtest2.py`.

**No dense comb, on either side, static or driven.** Spectral flatness in 1-8 kHz
stays 0.000 to 0.028 on both, nowhere near the design doc's claimed 0.16, and a
discrete-inter-harmonic-peak count returns zero on both. ReBirth's high-resonance
sound is a peaked harmonic spectrum, not the dense intermodulation comb
`reborn338_diode_ladder_design.md` was built to reproduce. That earlier
"flatness 0.16, ~135 peaks, confirmed empirically" is not reproduced here and was
likely a different fixture or regime; it is consistent with the 2026-08-06
resolution that shelved the fork as "not structural."

**What IS real: ReBirth carries 10 to 40 dB more DIFFUSE inter-harmonic energy.**
Inter-harmonic fill (median PSD halfway between harmonics, relative to the peaks)
runs -31 to -34 dB on ReBirth against -42 to -73 on ours across the driven sweeps.
Our oversampled ladder is very clean between harmonics; ReBirth's resonance is
broader and hashier. This is diffuse richness, not discrete comb peaks, and its
source (a broader resonance, nonlinear intermodulation, or less oversampling) is
not identified. It sits alongside the 4 to 6 dB prominence gap as the measured
high-resonance difference.

**Consequence for the diode path.** The fork's central premise, the comb, is
disproven, so the strong structural case for a full diode-ladder rewrite is gone.
What remains, the prominence (partly `K_B`-tunable) and the diffuse richness, is
smaller and does not obviously require the coupled topology. The pragmatic reading
is to bank the safe partial `K_B_SLOPE`, accept the reso 90-110 prominence and the
inter-harmonic richness as documented residuals, and keep the diode fork as a
future option rather than a committed supertask. Luke decides; nothing shipped.

### The richness source, measured: it is ALIASING, not topology

Measured 2026-09-03 on the combtest fixtures, ours against ReBirth's -31 to -34 dB
fill, by rendering ours under each candidate source:

| ours under | static c127 fill | sweep fills | reading |
|---|---|---|---|
| OS4 (shipped) | -59 | -42 to -73 | too clean |
| OS2 | -58 | -23 to -42 | closer |
| **OS1 (no oversampling)** | **-28** | **-7 to -16** | OVERSHOOTS ReBirth |
| DRIVE x3 | -62 | -49 to -64 | inert, as physics requires |
| FILTER=diode | -55 | -27 to -59 | inconsistent, does not tune |

Lowering the oversampling reproduces and then overshoots ReBirth's inter-harmonic
energy, so ReBirth's richness is ALIASING foldback and it sits around an OS1-to-2
equivalent; our 4x oversampling is cleaner than ReBirth and removes it. Heavy drive
is inert, confirming a memoryless nonlinearity cannot make inter-harmonic energy
from a harmonic saw (intermodulation lands on integer multiples of f0). The diode
core adds some but inconsistently. So the richness is a tunable oversampling
CHARACTER, not the coupled topology.

The catch: `TPT_OS` was raised to 4 to fix the crest factor (the per-cycle
resonance transient rounded off by host-rate aliasing, 4.82 against ReBirth's 6.99,
recovered to 6.65 at 4x). So matching ReBirth's inter-harmonic aliasing by lowering
`TPT_OS` trades against the crest factor 4x was raised for. It is a real trade on
the current core, not a rewrite, and the honest target is ReBirth's specific
aliasing character (its fill is a uniform -32 while ours at low OS is uneven), which
may want controlled aliasing rather than a blunt OS cut.

**A blunt OS cut is confirmed not shippable, measured 2026-09-03 after Luke heard
tour-ALLUP as less chirpy than ReBirth across the track (per-beat push comparable).**
On the ALLUP window `TPT_OS=2` adds most of the mid grit (2 to 4 kHz from about -3
to -4 dB up to -0.3 against ReBirth) at almost no crest cost on that musical section
(20.3 vs 20.6). But globally it fails the net at +0.092: it helps 219 cells and
wrecks the low-note sets (batchL +0.79, batchN +0.93, batchW +0.63), where aliasing
folds the harmonics badly, and on a held high-res note it softens the transient
(crest 12.9 against ReBirth's 14.1, where 4x matches at 14.3). So the grit and the
transient pull against each other on the OS knob, and ReBirth has BOTH, which a
single global OS cannot. The grit needs a TARGETED source, controlled aliasing or an
aliased-oscillator path gated to the high-res regime, that adds the hash without
folding low notes or rounding the resonance transient. That is a follow-up DSP
investigation, not a knob, and it is a subtle residual next to the open structural
items. Chunks: `listen-grit-2026-09-03/`.

**The aliased-oscillator path was tried and it is the wrong target, for a
principled reason.** A `REBORN_OSC_NAIVE` probe generated the saw naively at the
host rate (aliased) and held it across the oversampled ladder substeps, the idea
being oscillator grit with an oversampled-ladder transient. It moved nothing:
identical to OS4 on the ALLUP bands, crest 14.37 against 14.29. The oscillator's
aliasing folds up near 22 kHz and the ladder (corner under 5 kHz on this material)
filters it out before it reaches the audible band. So does the drive saturation's,
for the same reason (its extra harmonics are integer multiples of f0). Luke's ear
confirms it independently: OS1 sounds like an extreme ReBirth while OS2 sounds like
shipped, so the audible grit is at OS1's heavy level, and OS1 differs from OS4 only
in that the whole feedback loop runs at 1x. **The grit is the RESONANCE-TRANSIENT
aliasing**, the sharp per-cycle spike the feedback makes, folding into the audible
band at low OS. That is coupled to the transient sharpness through the very same
oversampling: at 1x the transient aliases (grit) but is rounded (crest 9.8), at 4x
it is sharp (crest 14.3) but clean. The probe is reverted. Getting ReBirth's grit
AND its sharp transient at once is a real limitation of the buffered TPT ladder,
and it is the one narrow, character-only reason the coupled diode ladder could
still matter, distinct from the comb that was disproven. It stays a documented
residual; the structural work is not justified by a subtle high-res hash alone.

**Diode fork: not needed.** All three high-resonance differences, the comb, the
prominence, and the richness, have non-diode explanations (disproven, `K_B`,
aliasing). The fork can be closed as a committed path and left as a documented
historical option. The remaining 303 high-resonance work is on the current core:
the partial `K_B_SLOPE`, and the oversampling-versus-crest trade for the richness.

## 25. Resonance-0 is CLOSED by the level law; the open level defect is HIGH resonance

Opened the resonance-0 `LF_HP` hole 2026-09-03, which STATUS and section 14 called
the largest open structural item. Re-read the record first (sections 6, 7, 14) and
re-measured on the current shipped default, and it is stale: the reso-0 hole is
largely closed and the open level defect has moved to high resonance.

Section 6 established that the reso-0 "passband droop / low-end shelf" is one thing
seen through different anchors, a LEVEL tilt: ReBirth's 303 is much quieter at low
cutoff and gains 10.76 dB across the cutoff knob at reso 0 where ours gained 2.38,
an 8.4 dB gap. Section 7's `LVL_CUT` law (8.4 dB, gate 0.10, pivot 0.65) shipped to
fix exactly that. Measured now on b1, each side relative to its own cutoff-0 cell:

| reso | ReBirth gain across knob | ours | gap |
|---|---|---|---|
| 0   | +10.72 | +10.62 | -0.1 to -1.2 |
| 32  | +2.95  | +1.89  | -1.0 to -1.4 |
| 64  | -1.90  | +0.04  | +0.2 to +1.9 |
| 127 | +1.76  | **-3.56** | **-3.3 to -5.7** |

**So the reso-0 tilt is closed to about a dB by `LVL_CUT`, and section 14's "still
open, largest structural item" framing is out of date.** What is open is the HIGH
end: at reso 127 ReBirth GAINS about 2 dB of level as the cutoff opens while ours
DROOPS about 3.5 dB, a 5.7 dB gap, with a smaller wiggle at reso 32 (ours ~1 dB low)
and reso 64 (ours ~1 to 2 dB high). This is a level tilt, invisible to the
band-shape net which normalises level, which is why it survived unmeasured.

It is PRE-EXISTING, not from this session: at reso 127 `K_B_SLOPE=0` droops -3.29
against the shipped 0.5's -3.56, so the partial prominence fix added only about
0.3 dB. `LVL_CUT` turns its boost off by reso 64 (gate 0.10), so it does nothing at
reso 127; the high-reso level is set by the ladder DC loss `1/(1+k)` and the makeup
`(1+k*0.75)`, and the makeup evidently under-compensates as the corner opens at
high k. The fix is a high-resonance, cutoff-dependent level term (the mirror of
`LVL_CUT`, which is reso-0), or a makeup that grows with cutoff at high k. Nothing
shipped; this is the real open level item, not reso-0.

**Fitted as `LVL_HI`, and NOT banked, on Luke's ear 2026-09-04.** Added a
high-resonance cutoff-tilt term (`LVL_HI`, `LVL_HI_PIVOT`, `LVL_HI_ON`), the mirror
of `LVL_CUT`, gated above reso ~108 so reso 64/96 are byte-identical. At `LVL_HI=6`
the reso-127 tilt goes from the -5.7 dB gap toward zero (roughly -1.9/-1.9/-1.4/0
across the cutoff knob; a linear tilt under-fills the low cutoffs, which stay ~2 dB
short). But it is a pure LEVEL law, invisible to the band-shape net and song_highs,
so its only checks were the b1 tilt and the ear. Luke's ear on
`listen-hireso-level-2026-09-03/`: "2 and 3 are about the same", no audible
difference on the songs. The distinction from `LVL_CUT`, which shipped on a like
measurement, is that `LVL_CUT` was 8 dB AND ear-confirmed ("ReBirth's are louder,
fuller underneath"); `LVL_HI` has the measurement but not the audible confirmation,
so it stays a default-off documented probe rather than a shipped default. The
measured tilt is real on held max-resonance notes and the probe is there if that
use case ever makes it audible; it is not worth banking a constant the ear cannot
distinguish.

**Prominence (`K_B_SLOPE=0.5`, banked) confirmed by ear 2026-09-04.** On
`listen-chirp-2026-09-03/` Luke: the candidate sounds MORE like ReBirth, with no
whistle anywhere ReBirth is clean, but the chirp is "still a way off" on ALLUP and
ACCENT. So the safe partial prominence fix is a real, audible improvement and
stays banked; the remaining ALLUP/ACCENT chirp gap is the already-documented
residuals, the aliasing grit (flagged revisit) and the accent's own chirp
(`ACC_SWEEP`, the accent step), not more resonance, which the self-oscillation cap
bounds.

## 26. The ACCENT chirp gap is NOT the accent knob, it is the env-mod corner at high env-mod

Sections 21 and 23 attributed Luke's "ReBirth's chirp is more pronounced on
ALLUP and ACCENT" to the accent's own brightness ramp, `ACC_SWEEP`, and named it
the accent step's job. That attribution came from the TOUR song, which cannot
isolate anything. Batch B9 (2026-09-04) is the isolated fixture that tests it, and
it disproves the accent-knob attribution.

THE FIXTURE. B7 (section 6) crossed the accent knob with cutoff and resonance but
held ENV MOD at 64, and found `ACC_SWEEP` 3.30 optimal on its surface. The tour's
ACCENT sweep does not run at env mod 64. Tracing the tour knob state into bars 20
to 24 (the ACCENT section, ~36.9 to 44.3 s at 130 BPM, which is exactly the "34 to
45 s" window section 21 measured), the knobs carry in at cutoff 60, resonance 85,
**env mod 90**, decay 55, with the accent sweeping 0 to 127. So the accent's
corner push is stacking on a HIGH env-mod push, the one axis B7 held fixed. B9
crosses the accent knob (0/32/64/96/127) against env mod (64/90/127) at that fixed
corner, saw, each env-mod level with its own no-accent control, accents on steps 4
and 12 as in B7 and B3. Rendered ours with `REBORN_BASS_ENGINE=tpt`; measured with
`tools/analyze/accent_centroid.py`, differential spectral centroid and 85% rolloff
of the accented steps minus the same steps of the no-accent control, one method
both sides.

ALIGNMENT GOTCHA, worth not rediscovering. A peak-relative onset threshold trips
late on a loud accented file (the quiet first note crosses 2% of the accented
peak later than 2% of a plain peak), which shifted the accented window ~5 ms late
on the k127 files and inflated an apparent "ours over-pushes the accent" reading
by 150 to 200 Hz. Detecting the onset ONCE per side from the no-accent control and
reusing it across knobs removes it. The tool does this.

THE RESULT, two metrics agreeing.

Plain note (no accent), ours minus ReBirth, the env-mod corner alone:

| env mod | centroid gap | rolloff85 gap |
|---|---|---|
| 64 | -88 Hz | -121 Hz |
| 90 | -139 Hz | -172 Hz |
| 127 | -246 Hz | -298 Hz |

Accent LIFT at peak knob (accented centroid minus the no-accent control),
ReBirth vs ours:

| env mod | ReBirth lift | our lift | gap |
|---|---|---|---|
| 64 | +478 | +522 | +44 |
| 90 | +434 | +478 | +45 |
| 127 | +314 | +299 | -15 |

Two things, and only the second is the accent path:

1. **The env-mod corner is still short at high env mod, and the shortfall grows
   with env mod.** On the plain note ours runs darker by 88, 139 and 246 Hz of
   centroid at env mod 64, 90 and 127. The corner fix of section 22 (`EM_CENTRE`
   0.45, `FC_MAX` 9000) was fitted around the mid range and leaves a residual that
   widens as env mod climbs past it. It is not `FC_MAX` clamping: our env-mod-127
   plain rolloff is 2878 Hz, far below the 9000 ceiling, so the env-mod law itself
   (`EM_OCT`, `EM_CURVE`) is producing too low a corner at high env mod.

2. **The accent's own brightness push tracks ReBirth at peak accent** (lift gap
   +44, +45, -15 Hz across env mod). `ACC_SWEEP` is not the deficit. ReBirth's
   accent lift SHRINKS as env mod fills the corner (478, 434, 314) and ours does
   the same (522, 478, 299), so the accent and env-mod pushes are sub-additive on
   both sides and match. A bigger `ACC_SWEEP` would over-brighten the accent,
   which is why B7 rejected every increase.

Reconciling with the tour. At the tour's env mod 90 the plain corner is 139 Hz
(centroid) or 172 Hz (rolloff) dark, and the matched accent push only partly
closes it, so ours stays ~95 to 139 Hz darker across the WHOLE accent sweep
including the peak. That is what Luke heard. The tour's larger absolute figure
(~200 to 400 Hz, section 21) is this corner deficit scaled up by the octave-jump
notes and slides in the tour's accent pattern, which B9's note-0 line does not
have. The direction and the bulk of the magnitude match.

A small secondary, same sign, in the accent path: at low accent knob our
shortening darkens the accented note less than ReBirth's, and that gap also grows
with env mod (+47 at env mod 64 to +168 at env mod 127). It is the accent-shorten
interacting with env mod, minor next to the plain-corner deficit, noted not
chased.

STATUS. Measured, not fitted, nothing shipped. The lever is the env-mod corner
law at high env mod (`EM_OCT` / `EM_CURVE`, the `envDepthOct` curve's steepness),
the same family as the applied corner fix, NOT the accent path. Sections 21 and 23
are superseded on the attribution: the ALLUP/ACCENT chirp gap is the env-mod
corner at high env mod, and the accent step's `ACC_SWEEP` is off the hook. Whether
to reopen the env-mod corner fit now is a decision, because that curve is scored
by the regression net and the corner fix already moved it 1.948 to 1.911.
Fixtures `fixtures-2026-09-04/b9-303-accent-corner`, renders
`renders/b9-303-accent-corner-2026-09-04`.

## 27. The global env-mod corner refit is REJECTED by the net; the deficit is a reso x envmod cross term

Section 26 named the env-mod corner at high env mod as the lever and left the fit
as a decision. The fit was run 2026-09-04 and the net rejects it. The search
gridded `EM_OCT` (4.45 to 5.25) and `EM_CURVE` (0.0 to 0.8), scoring each on the
B9 corner, the env-mod net subset and the tour songs
(`tools/analyze/score_emcurve.py`). The result:

| override | B9 deficit em90+127 | full net | worst sets | tour_saw | groove_square |
|---|---|---|---|---|---|
| shipped (EM_OCT 4.45) | 192 Hz | 0.000 | none | 0.00 | 0.00 |
| EM_CURVE 0.8 | -4 Hz | +0.275 | batchI +0.81, envdomain +0.88 | -0.61 | +... |
| EM_OCT 4.85 / CURVE 0.4 | -15 Hz | +0.211 | envdomain +1.00, batchI +0.98 | -0.70 | +0.84 |
| EM_OCT 5.25 | -26 Hz | +0.266 | batchI +1.15, envdomain +1.13 | -0.80 | +1.12 |

The B9 corner CAN be closed: `EM_OCT` 5.25 takes the em90+em127 deficit from
192 Hz to below zero. But every candidate that closes it regresses the FULL net
by +0.21 to +0.27, six to seven times the size of the corner fix's -0.037, and
the regression is concentrated in the env-mod fixtures (`batchI`, `envdomain`,
`envfloor`, `envshape`, `batchG`) and the square songs (`groove_square` +0.84 to
+1.12, `tour_square` +0.38 to +0.52), even while `tour_saw` improves -0.7 to
-0.8. It is a hard trade, not a free win.

WHY, and it is the reso axis. The env-mod net fixtures run at LOW to MID
resonance: `batchI`'s env-mod sweep is at resonance 20, `envfloor`, `envdomain`
and `envshape` at 60. B9 is at resonance 85. Our env-mod law is correct where the
net measures it and short only at high resonance, and the B9 deficit GROWS with
env mod at fixed resonance 85 (-88 Hz at env mod 64 to -246 at 127). A shortfall
that grows with env mod, and only at high resonance, is a **reso x envmod
interaction**: ReBirth opens the env-mod corner MORE at high resonance than we do.
Our exponent is `envOct + resoOctEff*reso`, purely additive with no cross term, so
`envDepthOct` does not know the resonance. A global `EM_OCT`/`EM_CURVE` change
moves the corner at every resonance and breaks the low-reso fixtures that were
right. Even `EM_CURVE` alone, which top-weights the high env-mod end, regresses
`batchI` (+0.81), because `batchI` is itself an env-mod sweep to 127 at resonance
20, so no env-mod-only term can spare it. Only a reso-gated term can.

CONCLUSION. The simple refit is REJECTED and not shipped. The env-mod corner law
is right at the resonance the net covers. The structural fix for the B9 corner is
a reso-dependent env-mod depth (a `reso x envmod` cross term, absent today), which
would open the corner at resonance 85 while leaving resonance 20 to 60 alone. That
is a new degree of freedom in the law, not a retune of the existing constants, and
a decision in its own right. The residual is the accepted state until then: at the
tour's env mod 90 ours runs ~139 Hz (centroid) darker than ReBirth on the saw at
high resonance, present across the accent sweep, which is what Luke heard.
Harness `tools/analyze/score_emcurve.py`; search driver kept in the session
scratch, results in `scratchpad/emc/SUMMARY.json`.

## 28. The reso x envmod term works, but the real conflict is WAVEFORM-dependent

Section 27 named a reso-gated env-mod depth as the structural fix. It was added as
`EM_RESO` (probe, default 0.0 = off), deepening the env-mod swing in proportion to
resonance: `envDepthOct = EM_FLOOR + envmod*(EM_OCT + EM_CURVE*envmod)*(1 +
EM_RESO*reso)`. It is zero-effect at reso 0 and preserves the fitted env-mod
shape. Built, suite green, and it closes the B9 saw corner as designed: `EM_RESO`
0.25 takes the em64/90/127 deficit from -88/-139/-246 Hz to -17/-7/+26. The
sweep, on the full 16-set net:

| EM_RESO | B9 em64/90/127 | full net | tour_saw | tour_square | groove_square |
|---|---|---|---|---|---|
| 0.0 ship | -88/-139/-246 | 0.000 | 0 | 0 | 0 |
| 0.10 | -61/-88/-144 | +0.019 | -0.37 | -0.00 | +0.33 |
| 0.15 | -46/-62/-89 | +0.047 | -0.52 | +0.13 | +0.55 |
| 0.20 | -32/-35/-33 | +0.084 | -0.66 | +0.32 | +0.79 |
| 0.25 | -17/-7/+26 | +0.126 | -0.77 | +0.50 | +1.07 |

It is 2 to 3 times gentler on the net than the global lift of section 27 (which
cost +0.27 for the same closure) because the reso gate spares reso 20. But it
still regresses, and the sweep exposes WHY, and it is not the reso axis: it is the
WAVEFORM. Every step that closes the saw (B9 saw, tour_saw down monotonically)
opens the square where the square does not want it: `groove_square` (reso 88, env
mod 78, square) regresses monotonically from +0.33 to +1.07, and it was CLOSER to
ReBirth at shipped than the term makes it. `tour_square` the same. So our env-mod
corner is right for the square at high resonance and short for the saw, and a
reso-only term is blind to that and over-opens the square.

This aligns with the whole thread's origin: "the saw is too dark" was always
SAW-specific, and the square was never flagged. So the deficit is a saw-only,
high-resonance env-mod shortfall. A reso-only `EM_RESO` cannot win it; it needs to
also be gated to the waveform (engage on saw, spare square), which is a second
degree of freedom on the term.

STATUS. `EM_RESO` stays a probe, default 0.0, nothing shipped. Three shapes are
now on the table and it is a decision: (a) add a waveform gate to `EM_RESO` so it
engages on saw and leaves the square, the principled fix the evidence points to,
another search; (b) ship a small waveform-blind `EM_RESO` ~0.10 as a partial, net
+0.019 and a real saw improvement (tour_saw -0.37) against a moderate square
regression (groove_square +0.33), a K_B_SLOPE-style partial subject to Luke's ear;
(c) bank the finding and accept the residual. Search results in
`scratchpad/emc2/SUMMARY.json`.

### EAR, 2026-09-04: EM_RESO is BANKED, and the deficit is not the corner position

Luke's ear on the level-matched chunk set (`listen-303-emreso-2026-09-04`,
ReBirth vs shipped vs `EM_RESO` 0.10 and 0.20):

- **The square cost is not audible.** Both groove and tour square sound very close
  to ReBirth at every setting, if anything 0.20 is closest. The net's square
  regression (groove_square +0.33 to +1.07) does not read as worse to the ear. So
  the waveform-blind worry that drove option (a) is not an audible blocker.
- **The saw win does not land.** Even at 0.20, where the accent-sweep centroid
  matches ReBirth (1816 vs 1828 Hz on the chunk), the candidates are not clearly
  better than shipped on the saw. What ReBirth has and ours does not, shipped and
  both candidates alike, is a **"roundness" around each held note as it heads into
  the chirp, a high-end round chirp** that is not hitting here.

So the corner POSITION was matched and the thing Luke hears is still missing. The
"saw too dark / less chirp" is not (or not only) where the env-mod corner sits; it
is the RESONANT CHARACTER of the sweep, the peak riding up as the filter opens.
That is defect 5 territory (the resonant peak prominence, partially fixed by
`K_B_SLOPE` and bounded by the k=4.0 self-oscillation cap, section 23) and the
high-resonance grit/richness character (sections 24, flagged for revisit), NOT the
corner law. `EM_RESO` moves centroid, not that character, which is why matching
centroid did not deliver it.

DECISION: `EM_RESO` is BANKED as a probe (default 0.0), not shipped, because it
does not achieve the audible goal and it moves the net the wrong way. The env-mod
corner refit thread (sections 26 to 28) is closed: the corner is as right as the
net allows and the residual brightness gap is not what the ear is chasing. The
open item is relocated to the resonant sweep character, the "round chirp", which
is the standing high-resonance-character question and the grit-revisit trigger.

## 29. The round chirp CHARACTERIZED: the resonant peak fails to ride the sweep at reso 90 to 110

Batch B10 (`fixtures-2026-09-04/b10-303-chirp`, exports
`renders/b10-303-chirp-2026-09-04`) is a big per-note filter sweep, cutoff 20,
env mod 115, decay 95, saw, crossed against resonance, with a slid (held-note)
and a square variant. `tools/analyze/chirp_peak.py` tracks the resonant peak in
[180, 9000] Hz over the note. The round chirp is the peak riding UP as the filter
opens. Measured, ours against ReBirth, the peak-center trajectory (Hz) over ms
after onset:

    reso 100 retriggered   20   60  100  140  180  220  280 ms
      ReBirth              215  215  425  482  291  218  215   arcs up to ~480 and back
      ours                 215  215  215  215  215  226  215   FLAT, never rides

    reso 100 slid (held)   20   60  100  140  180  220  280 ms
      ReBirth              215  215  320  393  441  476  522   climbs across the held note
      ours                 215  215  215  215  215  218  261   barely moves

    reso 127               20   60  100  140  180  220  280 ms
      ReBirth             2417 2347 2024 1750 1540 1292  215   rides (both do)
      ours                2775 2046 1741 1661 1408  221  215

THE FINDING. At resonance 90 to 110, ReBirth's resonant peak dominates the
spectrum and RIDES the filter sweep up (an arc to ~480 Hz retriggered, a climb to
~520 Hz across the held note): that ride IS the round chirp Luke hears. Ours does
not ride at all there. Our resonant peak is too weak to beat the low harmonic, so
the dominant peak stays pinned at 215 Hz and the chirp arc is flat. At resonance
50 to 75 (low, peak naturally low) and at resonance 127 (near self-oscillation,
where our resonance is finally strong) ours matches ReBirth. The gap is a band,
resonance 90 to 110, exactly where section 23 measured the peak prominence 3 to 4
dB short and bounded by the k=4.0 self-oscillation cap. This is that residual seen
in the time domain, and it is the reason matching the corner centroid (EM_RESO,
section 28) did nothing for the chirp: **the chirp is resonance STRENGTH, not
corner position.** The peak has to be strong enough to dominate and ride; ours is
not, in the 90 to 110 band, because the buffered 4-pole ladder self-oscillates
into a pure-tone whistle at k=4.0 and we cap below that.

WHY THIS IS THE DIODE LADDER'S REAL JUSTIFICATION, and not the disproven comb.
Section 24 closed the comb premise. This is different and it is measured: ReBirth
sustains a strong, riding resonant peak at resonance 100 WITHOUT a pure-tone
whistle, where our buffered ladder cannot (it either whistles at k>=4 or is too
weak below it). The coupled diode ladder self-limits its resonance differently
from a buffered cascade, which is precisely the mechanism that could give a strong
riding peak at 90 to 110 without the whistle. The diode-ladder revisit now has a
PRECISE, MEASURED TARGET: reproduce the peak-center ride at resonance 100 (arc to
~480 Hz retriggered, climb to ~520 Hz held) with the prominence ReBirth has and no
pure tone. `chirp_peak.py` is the scorer for it. Nothing shipped; this is a
characterization, and whether to open the diode-ladder R&D branch for it is a
decision.

### The buffered-ladder probe, 2026-09-04: no clean path, which scopes the diode branch

Before opening a structural branch, the existing resonance levers were probed as
env vars (no rebuild) against the reso-100 chirp ride: `K_B_SLOPE` (raise k in the
band), `REBORN_HFQ` (the HF Q-loss), `K_A` (floor). Peak-center trajectory at
reso 100, target is ReBirth's ride (retriggered 215..482..215, held climb to 522):

- **Retriggered note: not reachable.** Shipped, `K_B_SLOPE` to 2.0, and HFQ off
  all leave the peak pinned FLAT at 215, no ride. Only `K_A=0.6` moves it, and it
  overshoots to ~2035 Hz (gross over-resonance, wrong magnitude). The peak is
  bistable under the natural levers: pinned low, or blown up, nothing at ~480.
- **Held (slid) note: partially reachable, but only at unshippable settings.**
  `K_B_SLOPE` climbs the peak progressively (1.5 to 431, 2.5 to 568), approaching
  ReBirth's trajectory, but at 4 to 5x the shipped 0.5, which drives reso 127 past
  the k=4.0 cap in real music and would need reso-windowing it does not have.
- No whistle in these plucked fixtures (tail energy ratio 0.02 to 0.03).

VERDICT: the buffered 4-pole ladder has no clean path to the reso-100 chirp ride.
The retriggered case cannot be reached at all without gross over-resonance, and
the held case needs `K_B_SLOPE` far above what reso 127 tolerates. The resonance
either fails to dominate (pinned) or blows up, with no controllable middle at
reso 100, which is the buffered cascade's structural limit: at moderate resonance
its peak does not ring up and dominate the way ReBirth's coupled ladder does. This
is the measured green light for the diode-ladder R&D branch, scored by
`chirp_peak.py` against the B10 ride target. Probe driver in
`scratchpad/probe_reso_ride.py`.

## 30. The coupled cores: `rb` RIDES but its band-shape cannot be fit; `diode` does not ride

Diode session, 2026-09-04. Both experimental coupled cores in the code were tested
against the B10 ride (`chirp_peak.py`) and the full net (`regress_ab`).

- **`REBORN_FILTER=rb` (forward-Euler ReBirth-era ladder) HAS the ride.** At reso
  100 its peak rides (retriggered arc, held climb) where the buffered TPT stays
  flat; at reso 127 its trajectory closely tracks ReBirth (2369..1206 vs
  2417..1292). This proves a coupled ladder produces the round chirp the buffered
  cascade cannot. Its resonance is over-cooked (reso-127 prominence 85 dB vs RB
  59, reso-100 peak rides too high), but `RB_RESP` ~2.0 shapes the reso-100 ride
  toward RB with reso 127 untouched, so the resonance is tunable.
- **But the `rb` core's BAND-SHAPE cannot be fit.** Full net 1.896 -> 5.081
  (+3.19), worst on the cutoff-sweep fixtures (batchW +12.9, batchL +7.6, batchN
  +7.5). The 2026-07-23 fit only ever targeted the resonant emphasis POSITION, not
  the band shape. Diagnostic at cutoff 64 reso 0 (pure brightness, no resonance):
  the `rb` core is 6 to 9 dB too dark above ~300 Hz with too much low end, a
  broadband spectral TILT where TPT tracks ReBirth within +/-3 dB. A corner-law
  search (RB_A, RB_SLOPE over a wide grid) moves the cutoff-heavy subset only from
  +5.1 to +3.8, nowhere near TPT's floor: the tilt is structural to the
  forward-Euler native-rate rolloff, not a corner-position error, and it is
  entangled with the aliasing/warp that the native rate exists to produce.
- **`REBORN_FILTER=diode` (backward-Euler) does not ride at all.** Peak flat at
  215 Hz at every resonance including 127. The scaffold does not currently form a
  resonant peak that sweeps; it would need to be made to resonate before any
  tuning.

CONCLUSION, measured. The round chirp is achievable in principle (the `rb` core
rides), but neither existing coupled core is adoptable: `rb` rides yet its
band-shape cannot be fit to ReBirth, and `diode` does not resonate. Getting the
chirp into a net-parity voice therefore needs filter core DESIGN, not tuning:
either a hybrid (TPT's fitted stage rolloff with a coupled, riding resonance path
in place of the buffered feedback) or a correctly-discretized coupled ladder that
rides AND has the right rolloff. A concrete hybrid hypothesis to test first: the
TPT ladder's PER-STAGE saturation (`sNL` on each stage) damps the resonance
build-up, where the `rb` core uses a single clipper on the resonance tap and rings
freely; moving TPT to a single-tap resonance clipper (probe, default off) may let
the net-good TPT core ride. The buffered TPT stays the shipped voice, net 1.896,
correct on everything but this reso-90-110 ride. This is scoped R&D, not a session
tune.

### The hybrid probe, 2026-09-04: FB_SEP makes TPT ride, and the net cannot credit it

The hybrid hypothesis (per-stage saturation damps the ring) was wrong: `STAGE_NL`
defaults to 0, TPT's stages are already linear. The real mechanism is the SHARED
clipper: shipped, input and resonance feedback are summed then clipped together
(`satTanh((xin - k*fb)/U_SAT)*U_SAT`), which ducks the resonance when the
oscillator is hot. Two levers relieve it:

- `U_SAT` (widen the shared clipper): rides the HELD chirp (reso-100 slid to 474)
  and fixes the reso-127 tail, but not the retriggered case, and costs the net
  +0.83 at U_SAT 8 because it softens the fitted drive saturation.
- **`FB_SEP` (new probe, default off): clip the input alone, subtract the
  already-saturated feedback after, so the resonance rings freely like the rb
  core.** This RIDES: held (reso-100 slid to 557 vs RB 522), retriggered with a
  touch of `K_B_SLOPE` 1.0 (458 at 140 ms vs RB 482), and the reso-127 tail
  matches RB (1254 vs 1292) where shipped dropped to 221. The buffered TPT is NOT
  structurally incapable after all; it needed the feedback separated so the ring
  is not ducked.

THE COST, and it is the same wall every chirp-adding change hits. `FB_SEP=1` costs
the net +0.737, broadly (batchL +1.49, batchN +1.46, decayprobe +1.10,
accentdepth +0.86, envshape +0.67), because separating the feedback raises the
effective resonance on EVERY note. Dropping `K_B` to 2.5 recovers the net to
+0.313 but kills the ride (reso 127 collapses to flat); the ride sits on a
knife-edge between K_B 2.5 (dead) and 3.0 (rings), which is the buffered
structure's tell: freed, its resonance is dead or over-rings, with no smooth
controllable region, where ReBirth's coupled ladder rides smoothly. Threading it
would need a reso-windowed (bottom AND top gated, not cutoff-gated) resonance
boost, a further term.

THE REAL POINT. Across this whole session every method of adding the chirp ride
(EM_RESO, U_SAT, FB_SEP, K_B_SLOPE) costs the net +0.7 to +1.4, because the
band-shape net CANNOT credit a time-domain peak ride, the same blindness that hid
the env-mod corner and the level tilts (sections 6, 25, 28). So the net is not the
arbiter for the chirp; the EAR is, as it was for the corner and the level laws.
`FB_SEP` gives a real, working ride mechanism inside the net-good TPT core, so the
decision is whether its ride sounds like ReBirth's round chirp and whether its
band-shape change is audible. That is an ear check, built as
`listen-303-fbsep-2026-09-04`. Nothing shipped; `FB_SEP` is a probe, default off.

### Tuning, 2026-09-04: the shipped resonance params ARE the balance

Luke's ear on `listen-303-fbsep-2026-09-04`: FB_SEP delivers the round chirp,
"closer than we've ever been", hold the purpose-built ladder, tune FB_SEP. The
tune resolved to: leave the resonance params where they are.

A chirp-only search (match the B10 peak-center ride) picked `K_B_SLOPE` 5.0 with a
high pivot, but that was a MIS-TUNE: `chirp_peak` scores the peak CENTER, not its
LEVEL, so it did not see that `K_B_SLOPE` 5.0 slams reso 127 into the k=4.0 cap and
over-resonates the STATIC high-reso fixtures (batchL +4.3, batchN +4.6, far worse
than raw FB_SEP's +1.5). A balanced re-tune, scoring the held ride AGAINST the
static high-reso net (batchN/L/G/H), shows a clean tradeoff and no free lunch:

| resonance | held ride @220ms (RB ~476) | static-high-reso net |
|---|---|---|
| K_B 2.9, slope 1.0 | 221 (barely rides) | +0.35 |
| K_B 3.1, pivot .55, slope .8 | 275 | +0.42 |
| **K_B 3.4, slope 0.5 (shipped)** | **501 (matches RB)** | **+0.83** |
| K_B 3.4, pivot .70, slope 1.5 | 501 | +1.57 |

The shipped resonance (K_B 3.4, pivot 0.50, slope 0.5) with FB_SEP=1 gives the best
held ride at the least static over-resonance; lowering resonance to cut the
over-resonance kills the ride. So the ship candidate is `FB_SEP=1` with the
resonance UNCHANGED. It rides the HELD chirp and the reso-127 tail; the
RETRIGGERED chirp stays weak (it needs slope, which over-resonates statics) and is
the accepted buffered-ladder limit. No sustained whistle on held high-reso notes
(resonance concentration at or below shipped). The +0.74 net is partly genuine
static high-reso over-resonance, not only the uncreditable ride, so the live
question is whether that extra ring is audible on a plain sustained voice
(the groove-reso-88 ear-check track). SHIPPED 2026-09-04 (`be0970e`), `FB_SEP`
default 1.0, on Luke's ear sign-off ("remarkably close to ReBirth"), resonance
params unchanged. See section 31 for the conclusion of the coupled-ladder
investigation and the retriggered-curve residual.

## 31. The retriggered "round chirp" is a documented residual; the coupled-ladder investigation is concluded

The one 303 character item FB_SEP did not close is the RETRIGGERED per-note round
chirp at moderate resonance (reso ~90-110): on a fast pluck ReBirth's resonant
peak RINGS UP over ~100 ms (b10_r100_saw peak 215 -> 482 by 140 ms), where the
shipped buffered TPT voice stays flat. FB_SEP rides the HELD (slid) chirp and the
max-reso tail, not this. The `exp/diode-ladder-chirp` branch pursued a coupled
core to close it, and concluded 2026-09-05. The full evidence is in
`docs/research/PLAN_DIODE_CHIRP.md` on that branch; the settled result:

- **The ride is a marginally-stable, loop-delayed forward-Euler phenomenon.** The
  forward-Euler `rb` coupled core rides it (r100 988 -> 1308 -> fall, RB's arc);
  its slow ~100 ms resonance build is the near-instability of an explicit
  native-rate loop.
- **Any core stable and decoupled enough to be FITTABLE removes the ride.** The
  TPT-style zero-delay coupled diode ladder was BUILT (`REBORN_FILTER=zd`, corner
  exactly at fc, decoupled resonance/knee) and it does NOT ride: its peak is
  present at 20 ms and tracks the corner down, no ring-up. A/B of the integrator
  (trapezoidal vs backward-Euler) and the feedback (implicit vs delayed) pins it:
  implicit/zero-delay feedback never rings up; only the explicit delayed loop
  does, and even then not on r100. Fittability and the ride are in STRUCTURAL
  tension.
- **The one riding core (`rb`) is genuinely darker** than the shipped voice
  (~2x on an energy-weighted third-octave band error over the sysid sweep), so
  its darkness is audible, not a flat-net artifact.

DECISION (Luke, 2026-09-05): keep the shipped FB_SEP voice; log the mid-reso
retriggered round chirp as a permanent, documented residual of the 303 filter.
The coupled-ladder branch merges nowhere and stands as the record. Reopening it
would mean either un-darkening the `rb` forward-Euler core (option 1) or a full
D'Angelo nonlinear-network build (option 3); neither is planned.
