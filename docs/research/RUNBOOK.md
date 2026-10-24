# Runbook: orchestrating work on Reborn 338

Written 2026-07-27 as a handoff. You are picking up a clean-room reimplementation
of Propellerhead ReBirth RB-338. This file is the operating manual: what the job
is, how to do it here, where the work stands, and what to do next. It assumes you
have read nothing else yet.

Read this end to end before touching anything. The traps in section 7 have each
cost a full measurement round at least once.

---

## 1. The job, in one paragraph

One-to-one parity with ReBirth's **sound and rhythm**, per component, ranked
above every other consideration including code elegance, feature additions, and
sounding "better". ReBirth's audio output is the ground truth, treated as a black
box. The Roland hardware is a prior, not a target, and three times now the bug
was that we followed Roland instead of ReBirth. Not for sale, not for profit;
this is software preservation. `docs/research/GOALS.md` is the charter.

**Two rules that are not negotiable.** Nothing derived from ReBirth ships: no
samples, songs, mod contents, artwork or bytes. Measuring its audio is fine; this
project does not disassemble or decompile it, and nothing recovered from its
binary enters the codebase. A third-party decompilation now exists outside the
repo and is used only to choose which measurements to run, behind a
measurement-only wall (`docs/PROVENANCE.md`). And nothing merges to `main`
without Luke's explicit sign-off.

---

## 2. Where things are

| Path | What it is |
|---|---|
| `reborn/` | the repository, the only thing under version control |
| (elsewhere on the author's machine) | the ReBirth 2.0.1 install, with `Demo Songs/` and `Mods/` |
| `manualPNGs/` | the v2.01 manual as page images. Read these, not the PDF |
| `fixtures-2026-07-23/`, `fixtures-2026-07-27/` | generated `.rbs` test songs, batches A to I |
| `renders/batch*_rb` | ReBirth exports, ground truth |
| `renders/batch*_ours` | our renders of the same fixtures |
| `fixtures-2026-07-30/batchS` | the skirt fixtures: probe (3), grid (12), sweep (16), long (4) |
| `renders/batchS_rb`, `batchSW_rb` | ReBirth, grid and sweep. **Six shared cells**, the duplicate controls |
| `fixtures-2026-07-31/batchL` | **batch H an octave down, f0 = 55 Hz.** 6 cutoffs x 3 resonances, plus 3 duplicate controls. The only fixtures that see below 110 Hz |
| `fixtures-2026-07-31/drivecheck` | the 4 ear-check patches, plus Luke's 7-bar Clodhopper |
| `renders/refit_earcheck/` | ReBirth / shipped / refit trios for six patches, sorted to play together |
| `renders/batchLG_rb` | 64-second exports, 4 cells |
| `renders/determinism/` | six exports of ONE fixture, the render-state evidence. Keep these |
| `renders/batchH_floor/` | six cells, five ReBirth exports each. The noise floor, 0.114 dB rms pooled |
| `fixtures-2026-07-31/batchN` | two TIED low notes, C1 32.703 Hz and E1 41.203. Duplicates reproduce to 0.001 dB. **Tied: unusable below resonance 60, see section 7** |
| `fixtures-2026-08-01/batchL2` | batch L extended to resonance 15/45/60, PLUCKED. Merges with batch L into 6 resonances x 6 cutoffs. Exported and validated, **not yet fitted** |
| `fixtures-2026-08-01/batchP` | 27 tied fixtures, **UNUSABLE**, kept only as the evidence for the tied-fixture trap |
| `fixtures-2026-08-01/decayprobe` | 7 fixtures, decay swept at env mod 0. **The env-mod-floor evidence** |
| `renders/lowend_earcheck/` | ReBirth / current / lowend trios, band-matched on 300 Hz-5 kHz |

Load the `ab-parity` skill before any measurement, the `component-deep-dive`
skill when starting or resuming a component, and `rebirth-manual` to find things
in the manual images.

The durable knowledge lives in the repo, in this order of authority:
`docs/research/GOALS.md` (charter), `docs/COMPONENT_MATRIX.md` (the live worklist and the
definition of done), `docs/research/FINDINGS_AUDIT.md` (what is proven, what was retired
as confounded, and why). **Retired findings stay retired.** Read the audit before
resurrecting an old number as a tuning target; several have come back wrong.

---

## 3. The loop

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

**Render our side of a fixture batch.** Passes `REBORN_*` environment variables
through, so any constant can be swept without rebuilding.

```bash
python tools/render/render_batch.py ../fixtures-2026-07-27/batchH ../renders/batchH_ours --seconds 12
```

**Export ReBirth's side yourself. Do not ask Luke to do it.** `tools/rbdrive`
launches ReBirth with a fixture, clears its dialogs, drives File > Export and
closes cleanly, unattended, about 15 s per fixture.

```bash
python tools/rbdrive/rbdrive.py batch spec.json --out-dir ../renders/batchX_rb
```

It is still the real GUI application, so it takes the foreground while it runs.
Say so before starting a long batch. Windows only. Read
`docs/REBIRTH_AUTOMATION.md` first; most of the odd-looking code in there is
load-bearing and the obvious alternative has usually been tried and failed.
**Validate the chain before trusting a new batch**: re-export a fixture whose
settings match an older capture and confirm it comes back identical. That is what
proves the effect rack and the master compressor were off and that nothing
drifted between sessions.

**Generate fixtures** with `tools/rbsgen/`. See `gen_batchG.py` for the panel
sweep pattern, `gen_batchH.py` for a 2-D cross, `gen_batchI.py` for envelope and
tempo work, and **`gen_batchL.py` for the current template**: it is the only one
that bakes in duplicate control cells, and every new batch should copy that.

### The measurement tools, and what each is for

| Tool | Answers |
|---|---|
| `tools/analyze/batchH_filter.py <rb> <ours>` | the 2-D cutoff x resonance grid: rolloff, centroid, prominence |
| `tools/analyze/span_probe.py --tag X REBORN_A=v ...` | render batch H under overrides and score it in one command |
| `tools/analyze/joint_fit.py [--refine]` | grid-fit the three coupled filter constants |
| `tools/analyze/invert_filter.py [cutoff ...]` | recover ReBirth's implied corner and feedback per cell |
| `tools/analyze/wave_fit.py <dir>` | oscillator pulse width and level, filter cancelled out |
| `tools/analyze/vca_ab.py <rb> <ours>` | amplitude envelope as a shape: gate length and release |
| `tools/analyze/batchI_env.py <rb> <ours>` | gate-vs-tempo, env mod depth law, decay at env mod 0 |
| `tools/analyze/batchG_303.py <dir> [group]` | the 303 panel: accent, tune, slide, gate, wave, emdec |
| `tools/analyze/whistle_guard.py <rb> <ours>` | did a change push the ladder toward self-oscillation |
| `tools/analyze/harm_response.py [ours-dir]` | **the filter as a transfer function sampled at the harmonics.** The primary instrument |
| `tools/analyze/curve_fit.py [--stage N]` | joint fit scored on the whole curve, 2559 points, held-out validated |
| `tools/analyze/skirt_sweep.py` | skirt slope against resonance, fixed band, no gating |
| `tools/analyze/skirt_knob.py` | skirt over a band anchored to the cutoff KNOB, immune to peak drift |
| `tools/analyze/skirt_bands.py` | slope in SUCCESSIVE octave bands, so "is it the filter or the band" is answerable |
| `tools/analyze/skirt_calib.py` | **feeds the whole pipeline a filter of known slope. Run this before quoting any absolute slope** |
| `tools/analyze/drive_probe.py` | drive swept against BOTH linearity and filter error |
| `tools/analyze/level_ab.py` | is the level offset a scalar yet, or does it still tilt |
| `tools/analyze/floor_check.py` | where a spectrum stops standing above its own floor |
| `tools/analyze/lowend_ab.py` | the low-frequency SHAPE, with the duplicate / comb-collision / tie-vs-pluck checks built in |
| `tools/analyze/onset_ab.py <rb> <ours> --bpm N` | **which STEPS retrigger. The rhythm instrument.** Three defects of this family have shipped and no other metric caught any of them |
| `tools/optimize/` | the optimiser driver, with `harmonic_curve` and `noise_floor_many` |

`harm_response.py` reads EXISTING renders and does not render anything itself, so
to score a configuration you must `render_batch.py` under those overrides first
and then point it at the output directory. Setting `REBORN_*` on the analyzer
alone silently measures the previous build.

**RETIRED as a verification instrument: `invert_filter.py`.** It carries a header
saying why. Use `harm_response.py`.

**LEFT FLAWED ON PURPOSE: `rolloff()` and `prominence()` in `batchH_filter.py`.**
`rolloff` snaps to the harmonic comb (a 2% threshold change moves it 0.137
octaves on average, 0.356 at worst, against a fitted residual of 0.237) and
`prominence` cannot see the peak in 12 of 29 cells because its band starts at
800 Hz. They are NOT fixed, because every historical number in the audit and the
matrix was measured with them and changing them would silently invalidate the
record. The new tools sit alongside.

---

## 4. Where the 303 stands

> **REOPENED 2026-08-29.** Everything below predates that and describes the 303
> as parked at a near-inaudible residual. It is not: on a clean single-303
> comparison our harmonics are 10 to 20 dB weaker than ReBirth's from 86 Hz up,
> and batches B1 to B3 have since measured five distinct defects, one of which is
> now fixed. `docs/research/BASS303_FINDINGS.md` is the live worklist and supersedes this
> section for anything about the current state. What is still useful here is the
> history of what was tried and why.

Supertask 1, this section. Supertask 2 (the 808) has since had its **sample path
taken to parity and merged to main** (rounds 7-9, `docs/research/DRUM808_FINDINGS.md`); its
shippable synth path and the drum-channel master effects are still to do. Order is
**303, 808, 909, PCF, delay, compressor, distortion**, and a component is not
finished until Luke says so.

### Settled and measured

| | |
|---|---|
| **tune** | a 32-slot table with a 24-wide centre detent, not a continuous law. Matches on all 128 knob values to 0.000 semitones, ear-confirmed. Fixed 7 of 16 shipped songs that were up to 1.38 semitones out |
| **slide** | 63% of the interval in 17 to 22 ms, interval-independent. Ours ran 3x slow. Now -0.4 ms mean error over five intervals |
| **register, octave** | within 0.09 semitones. Nothing outside note A had ever been checked before |
| **waveform** | ReBirth's square is a **pulse at duty 0.52286**, 5.20 dB below its saw. Fitted to 0.03 dB rms over 12 harmonics at three cutoffs |
| **output DC** | we emitted -0.0055 (saw) and -0.0140 (square); ReBirth is at +/-0.000003. Fixed with a 5 Hz blocker |
| **VCA shape** | flat for **half a step**, then release with **tau 22.2 ms**. Gate confirmed proportional at five tempos, 60 to 180 BPM |
| **env mod depth** | nine-point sweep crossed with three decays, 0.2215 to 0.0384 octaves rms |
| **vintage mode** | settled from the file format. Every file we own is RB40, so no reference was ever in vintage mode and no refit was needed. Still unimplemented as a feature |
| **pattern length, banks, shuffle** | round-trips 17/17 songs |
| **filter constants** | refitted and **APPLIED** 2026-07-31, the first default change since `d80e1a5`. Curve rms 4.745 to 2.627 on 2559 points, peak frequency 0.2576 to 0.0996 octaves, cutoff tilt from a 0.67-octave swing to 0.04. Held-out validated, ear-checked on six patches |
| **the measurement floor** | 0.114 dB rms pooled, from six cells with five ReBirth exports each. The fit sits 42x above it, so the residual is real signal |
| **the harmonic instrument** | calibrated against a filter of known slope for the first time, and it was wrong. See section 7 |

> **START HERE AS OF 2026-08-02: `docs/research/PLAN_303_ENVELOPE.md`.** The list below is
> still accurate but is no longer the whole picture. Five of its items turned out
> to be one defect: ReBirth's filter envelope fires on every note even at env mod
> ZERO, is exponential in FREQUENCY where ours is in octaves, has a depth of 15 to
> 19 dB, and has a time constant the decay knob sets from 0.07 to 2.14 s. Ours
> does not fire at all. That plan carries the ordering, the numbers, and the
> reason the static filter constants cannot be finally trusted until the envelope
> is in.

### Open, in dependency order

Rewritten 2026-07-31 at end of day. The granular items now live in the task list
(tasks 53 to 57); this is the map and the reasoning behind the ordering.

**1. The DC blocker rings for 32 ms and reads as extra notes. Task 53.**
DIAGNOSED, not fixed. `REBORN_DC_HP` is 5 Hz and a one-pole at 5 Hz has a 32 ms
time constant, so its stored state discharges into the output every time the
voice stops. ReBirth's gaps fall with tau 14.5 ms to digital zero; ours fall with
34.6 ms and stall at -45 dB. Turning the blocker off recovers 20 dB of that and
takes the tail to 19.3 ms. Ruled out first, with numbers: the VCA release (11x
faster moved it 3.7 ms) and the resonance feedback (nothing at K_B 2.0 or 0.8).
The fix is a placement question rather than a constant, and it is deliberately
held behind item 2 because both live in the same octave.

**1b. The resonance-0 corner is a SLOPE error, and it is a new constraint on the
joint fit. Task 58.** Newly measurable as of 2026-08-01. Our corner runs 0.17
octaves LOW at cutoff knob 16 and 0.22 octaves HIGH at knob 127, crossing near 64,
which is a slope error rather than an offset and fully explains the +5.2 dB of
mid-band brightness at cutoff 127 resonance 0 already on file. This region was
never observable before, because resonance 0 has no peak to locate, which is
exactly what the retired `invert_filter.py` got wrong by using it as a baseline.
**Do not act on it alone**: a fit of the resonance-0 law by itself took batch H
from 2.627 to 8.886 dB rms.

**2 and 3. The low-end highpass. Tasks 57 and 56, both CLOSED 2026-08-02.**
APPLIED at 119.9 Hz with knee 0.141, on Luke's sign-off, and it is the second
default change since `d80e1a5`. Established on the merged batch L + L2 grid,
six cutoffs by six resonances, plucked: at resonance 127 the gap between ReBirth
and us IS a one-pole highpass at **111.5 Hz plus or minus 0.7**, cutoff-
independent, fitting the six-point SHAPE to 0.027 dB against a 0.026 dB
duplicate floor. Every other instrument improved or held when it was switched on,
including batch H prominence 3.83 to 2.65 and the whistle guard -23.3 to -30.7
against ReBirth's -29.4.

**What is left of it is one column, and it has its own task.** `fc(0) = 0` while
ReBirth loses up to 5.7 dB at resonance 0, growing with cutoff. The shape there
is NOT identifiable from six harmonics over 2.6 octaves: a 1-pole, a 1-pole plus
tilt and a 2-pole plus tilt all fit with corners wandering 70 to 183 Hz. **Task
62** builds the fixture that could identify it, batch N's interleaved C1/E1 comb
with batch L's plucked articulation at 60 BPM. Do not fit that column from batch
L alone.

Two corrections this round made to the older framing, both recorded in the audit.
The cutoff dependence "below the knee" is better stated as a nonzero
resonance-0 BASELINE that grows with cutoff. And the low-resonance departure from
one-pole shape is NOT task 58 leaking in; that hypothesis was tested and refuted.

**4. The chirp. Task 54.** ReBirth has an onset chirp we lack, on every patch with
resonance or an envelope sweep, and absent on exactly the two patches Luke cannot
tell from ReBirth at all. **He has confirmed this is the same quality as the
"snap" he has been describing since the first ear check**, with the caveat that
they are not always identical but sound like the same instrument or filter. That
makes it the largest known SUBJECTIVE gap in the 303. Points at the filter
envelope's attack rather than the oscillator or the VCA.

**5. The under-pronounced filter sweep. Task 55.** Likely the same defect as item
4 seen a different way. Check whether one fix moves both before treating them as
two problems.

**6. What is left of the filter residual.** 2.5 to 3.1 dB of rms scatter with no
mean offset at any harmonic band, plus a residual running -1.44 dB at cutoff 16 to
+0.70 at cutoff 110, and `RESO_OCT` about 0.16 octaves too aggressive over
resonance 60 to 127. The scatter is the target now, not the tilt.

**7. The drive shaper.** The ear check was run and went AGAINST removal: drive ON
won all four cases. Luke has since flagged that the quiet A/B probably hid the top
end, which is where a saturator works, so treat it as unsettled and rerun it
level-matched. Note the measurement case for removal rested on a filter fit that
has since been voided.

**8. Resonance 0.** ReBirth's passband rises 6.6 dB across the cutoff knob there
and ours rises 0.8. Read as a ladder DC loss the implied feedback at ReBirth's
resonance ZERO falls 1.16 to 0.00 as cutoff opens, which would mean our `K_A = 0`
is wrong at low cutoff. Evidence splits and neither story explains both rows. Next
step is a two-parameter fit of each resonance-0 curve with corner and feedback
both free.

**9. Env mod at high resonance**, and **10. decay at env mod 0**, unchanged and
still unexplained. Item 10 may turn out to be item 4 seen statically.

**11. Accent constants**, provisional, and now for a second reason: they were
measured through the instrument that was found to be miscounting.

### Also outstanding, not blocking

Implement the vintage switch. Settle the two undecoded bytes at the head of every
device chunk (byte 0 is 0/1 and differs per chunk within one song; byte 1 ranges
0 to 29). Our absolute output runs 2.2 to 4.8 dB below ReBirth's and the deficit
grows with cutoff, deliberately not corrected with a scalar because the
cutoff-dependent part is item 1 and calibrating now means calibrating twice.

---

## 5. State of the branch

> **THIS SECTION WAS STALE FROM 2026-07-31 TO 2026-08-03 AND STATED RETRACTED
> FINDINGS AS CURRENT FACT.** It was a point-in-time handoff that later rounds
> overtook without rewriting it, while section 4 above was kept current, so the
> two contradicted each other on whether the filter constants had shipped. That
> is exactly the route by which a retired finding comes back as a tuning target,
> which this project has been bitten by more than once. It is now cut back to
> the facts that are checkable, and the reasoning that used to live here has
> moved to the files that are maintained.
>
> **The entry point for the 303 filter is `docs/research/PLAN_303_ENVELOPE.md`.** The two
> calls that were Luke's are in `docs/research/OPEN_DECISIONS.md` under "Resolved calls"
> and **neither is waiting on him**: the skirt direction call is withdrawn, and
> the drive ear check was run and went against the change.

Check the branch and what has shipped from the code and from `git`, not from
prose. Prose about which branch is live goes stale within days here.

**As of 2026-08-29:** on branch `pcf-supertask`, NOT merged. `main`
has not moved. The current handoff is `docs/research/STATUS_2026-08-29.md`; the two
`STATUS_2026-08-27*.md` and `STATUS_2026-08-28.md` are marked superseded.

Supertask 4 is NOT finished, but the defect Luke's ear found on the 28th is.
Songs PERFORM the PCF's routing and we held it wherever it started, so our
filter ran over whole songs where ReBirth's runs over part of them. Fixing that,
and everything it uncovered, took G-Spot's 909 from 5.3 dB of octave-band error
with the filter in the path to 0.57, against 0.47 with it out. All 55 preset
patterns are re-measured and the decay law is re-derived. What is left is Luke's
ear on `listen-pcf-2026-08-29/`.

The `byte_neutral` baseline moved to pooled hash `b318273e` and the old one will
not match. That is deliberate: the PCF now runs in songs where it never did, and
the distortion routing was corrected in fifteen of the sixteen demo songs.

The 303 text below is from 2026-08-03 and is kept for the filter constants it
records. As of 2026-08-03: branch `303-env-domain`, `main` at `ab8b5aa` and well
ahead of `origin/main`, and **defaults HAVE changed**. `CUT_SCALE` 0.78, `CUT_SLOPE` 2.9,
`RESO_OCT` 0.25, `RESO_OCT_CUT` 0.35, `EM_FLOOR` 0.85, `ACC_SWEEP` 0.80,
`ACC_ATK` 0.010 and the low-end highpass at `LF_HP_KNEE` 0.141 all ship.
`REBORN_DRIVE` still ships at 1.0.

### What this section used to claim, and what became of it

| claim | status |
|---|---|
| "Nothing is merged, no default changed since `d80e1a5`" | **false since 2026-07-31.** Two default changes have shipped on Luke's sign-off |
| "The skirt is too steep, and that is a topology statement" | **retracted twice.** `skirt_knob.py` anchored its band to the knob rather than a measured peak, and `find_f0()` was half a bin out. What is left is 1 to 2 dB/oct, and **ReBirth is a four-pole ladder**. No topology question |
| "The constants have never been checked against their own noise floor" | **settled, in the opposite direction.** Pooled floor 0.114 dB rms over six cells with five exports each; the fit sat 42x above it. The 1.43 I quoted was a single cell, not a pooled set |
| "The drive shaper does not survive a joint refit" | **the ear check went the other way.** Drive ON won all four cases. The measurement case was separately voided with the instrument that produced it. A level-matched rerun is the open follow-up |
| "The corner rise is 0.23 octaves too fast" | measured through the pre-fix instrument. Regenerate before quoting |

### What survives from it unchanged

**The render loop is roughly 20x faster**, from hoisting eight `getenv` calls out
of the per-sample loop. Verified byte-identical on all 36 fixtures at the time.

**ReBirth has about six discrete render states.** Six exports of one fixture gave
four distinct outputs. Null-testing stays forbidden, and a single-pair noise
floor is unsafe because a colliding pair scores zero.

**`invert_filter.py` is retired as a verification instrument**, for the four
reasons in section 3.

**The output level is not a scalar yet**, and a makeup gain would absorb filter
error.

### The lesson this section is itself an example of

A handoff written to be read once becomes a hazard the moment it is left in a
file that is presented as current. If a section is a snapshot, date it and say
what supersedes it in the same edit that writes it. The cheap version of this
rule: **when you change a default, grep the docs for the old value.**

**Read this before any number anywhere in this file.** On 2026-07-31 the primary
harmonic instrument was found to be miscounting: `find_f0()` snapped to the
nearest FFT bin, and from about harmonic 15 the fixed search window had walked
off the harmonic entirely, reading the leakage skirt beside it. Every absolute
slope measured above that crossover was too steep, on both sides. It is fixed and
the fix is calibrated against a filter of known slope. In short: ReBirth IS a
four-pole ladder, our skirt excess is 1 to 2 dB/oct rather than 3 to 4, the noise
floor is 0.114 dB rms rather than 0.289, and the shipped config scored 4.745 dB
rms rather than the 2.5 that was on record. **Any number predating that fix which
quotes a slope, a peak frequency at high cutoff, or a curve rms is suspect until
regenerated.** Run `skirt_calib.py` before quoting an absolute slope.

The granular worklist is section 4. The 303 filter entry point is
`docs/research/PLAN_303_ENVELOPE.md`.

## 6. How to run this work

**Measure, never guess.** Every large defect found so far was invisible to the
metric in use at the time. When something is wrong, **widen what is measured
rather than refining the current metric**: sweeps not pairs, cross the axes, fit
coupled things in dependency order, the modulated before the modulator. Most of
the biggest finds this month came from changing the instrument, not the fit.

**Kill hypotheses cheaply and write down the corpses.** The filter round
eliminated four candidates in one pass of `span_probe.py`, each with numbers, and
that is why the fifth was worth trusting. A negative result with a number
attached is a real deliverable.

**Do not buy small gains with free parameters.** A curvature term for env mod
scored 0.0374 against 0.0384 for no term at all, and was dropped. Buying 0.001
octaves with a parameter is how the accent constants ended up provisional.

**Fixtures, never demo songs, for voice tuning.** The demos have multiple tracks
with individual effects plus effects on the main bus. They are an end-stage
comparison tool only, for a near-final build with everything wired.

**State uncertainty plainly.** Never call a tuning final without saying so. If
the honest answer is "nothing survived and the next step is a different
experiment", that is the answer.

**Style.** Prose over bullets in chat. No em-dashes anywhere, including code
comments and documents. Avoid "genuinely", "honestly", "actually".

**Ask Luke only for what is his alone**: ear checks, direction, sign-off. Not
exports, and not anything the tooling already does.

---

## 6b. ISOLATION: the rule that governs every fixture and every listening test

Added 2026-08-29 after Luke pointed out that what he was being asked to judge by
ear was confounded by things we have not implemented. The measurement side of
this project has always been rigorous; the listening side had no protocol at all.

**One device. One INSTANCE of it. One effect or none. Proved, not assumed.**

- One 303, never two. One drum machine, never both. Two simultaneous lines cannot
  be separated by ear and a band metric cannot separate them either.
- Exactly one effect in the path, on exactly one device, with everything else off
  and unrouted and nothing automated after tick 0.
- `python tools/rbsgen/isolation_check.py <fixture> --device 909 --effect pcf`
  before every export. It reads the ARRANGEMENT rather than the save-time panel,
  and it tests device silence by RENDERING each device soloed, because a silent
  device still carries a pattern-select event and a knob header.
- Build effect tests on the source we are closest on. As of 2026-08-29 that is
  the **909 in sample mode with the rack off, at 0.13 dB mean octave error**,
  which is the only clean single-device null this project has.
- A denominator differs from its numerator in EXACTLY ONE THING. Three rounds in
  one week were lost to references that did not.

**Every listening batch opens with the null pair**, the same fixture with no
effects at all, ours against ReBirth's, so Luke hears what "the same" sounds like
before being asked whether something differs. Then each effect alone, then pairs,
then the rack, then demo songs last.

`docs/research/PLAN_ISOLATION_TESTS.md` is the protocol and the fixture catalogue. The
`ab-parity` skill carries the rules in full.

## 7. Traps that have each cost a round

**A chunk is the SAVE-TIME panel; the track is what plays.** Cost time twice in
one day on two components. G-Spot's `PCF ` chunk says Freq 70, Pattern 24, Decay
90 while its track starts at Freq 53, Pattern 32, Decay 67. Calm's `303` chunk
says cutoff 120, resonance 127 while its track starts at cutoff 0, resonance 0
and does not reach the chunk until bar 156. Never quote a chunk value as the
setting a measurement ran at.

**A summary statistic that cannot see what an ear can is blind, not strict.** A
whole-mix octave error of 0.12 dB missed two 303s sitting 16 dB under the drums.
A per-note accent statistic scored over the first 62 ms missed a defect that
lives entirely after 62 ms. Both times the fix was a different measurement, not
a tighter threshold on the same one.

**Measure a plane, not a line, where two controls interact.** The 303's cutoff
was four points at one resonance; nine cutoffs by five resonances showed two
independent defects that a line had been reporting as one.


**Peak-per-slice envelopes phase-lock.** At 150 BPM a 16th step is exactly 11
cycles of 110 Hz, so every note samples the waveform at the same phases and
averaging across notes stops smoothing the ripple. Two of five tempo fits
collapsed and the result read as "neither hypothesis". An analytic (Hilbert)
envelope fails the same way. Use `period_env()`, a boxcar of exactly one
oscillator period, which integrates any ripple at a multiple of f0 to its mean.

**Four faults in one round were a single species: a gate whose pass condition
depends on the noise realisation, applied where the gate sits near its own
threshold.** A peak test counted flat cells as peaked; a floor test selected
harmonics by contrast; a contrast test was mistaken for an SNR test; and a
single-pair noise floor drew a collision and read zero. One reached a commit and
had to be retracted. The rules that came out of it, in order of value:

1. Where a number decides whether another number is real, measure its
   distribution, not one draw of it.
2. Use a FIXED analysis band and report the level at its top, so a contaminated
   read is visible. Never gate points by an SNR or contrast test.
3. Duplicate two or three cells in every fixture batch as a control. The
   retraction was caught only because a later batch happened to re-export three
   cells by accident.
4. When two numbers for "the same thing" disagree, check they are the same
   statistic before hypothesising about the system. Two false alarms were an
   average compared against a member of its own set.

**A null result has an operating point.** `hfQ` was recorded as "provably inert"
on the batch H grid. That was measured before the branch raised the static corner
into the range where `hfQ` engages; at the settings a later fit reached, it moves
the cutoff-127 span from 4.30 to 4.81. Eliminations here must record the
operating point alongside the number.

**Calibrate an estimator against a known value before trusting its absolute
output.** The gate fit read 0.470 for ReBirth and 0.472 for our own render, which
uses exactly 0.500. That -0.028 bias is what turned "about 0.47" into "exactly a
half step".

**And when you do not, this is what it costs.** That rule was written down on
2026-07-30 and not applied to the primary instrument, which was wrong the whole
time. `find_f0()` located the fundamental to the nearest FFT bin, half a bin of
error, 1.35 Hz. `harmonics()` searched a FIXED plus or minus 4 bins around each
predicted harmonic. The error multiplies by harmonic number and the window does
not, so from about k = 15 the window has walked off the harmonic and reads the
leakage skirt beside it. Every slope above that came back too steep. Fed a
synthetic sawtooth through an EXACTLY known 4-pole, the pipeline reported -31.1
dB/oct where the truth is -21.4. A whole round of skirt work, a topology
hypothesis and five candidate eliminations were built on it. **Run
`skirt_calib.py` before quoting an absolute slope, and when you build a new
estimator, build the thing that checks it in the same sitting.**

**A shared bias protects differences and destroys absolutes.** Both sides of an
A/B snapped to the same wrong bin, which is exactly why the sign findings kept
surviving while the magnitudes kept being retracted. If a direction reproduces but
its size keeps moving, suspect the instrument before the system.

**A TIED note does not measure the same instrument as a plucked one, at low
resonance.** Batch N and batch P used sustained notes because the precision is
extraordinary: ReBirth's roughly six render states differ in how a TRANSIENT
lands, so a held tone has none and duplicates reproduce to 0.001 dB, with two
cells coming back BIT-IDENTICAL across a day. That precision is real and it is
still the right choice for anything above resonance 60. But at resonance 0
ReBirth's tied and plucked curves differ by 2.45 dB where ours differ by 0.02, so
ReBirth does something on a held note that we do not, and the fixture measures
that instead of the filter. Batch P's whole corner reading was wrong because of
it, confidently and self-consistently. **Below the knee, pluck.**

**Reproducibility is not validity, second and more expensive example.** Batch P
reproduced to 0.000 dB, bit for bit, across sessions, and gave an answer that made
an independent batch three times worse. Perfect precision on a fixture that is
asking the wrong question buys nothing. The check that caught it was scoring the
derived constants against a batch they were NOT fitted on, which is the only test
that has ever caught this class.

**Calibrate estimators with REALISTIC NOISE, not noiseless synthetics.** The
two-corner fit was calibrated on clean synthetic curves and recovered a 3187 Hz
corner from a band ending at 327 Hz to within 0.4%, which looked like proof it was
well conditioned. On real data carrying about 0.5 dB of ripple, the same fit over
the same band returned 1158 Hz for a corner known to be 3190. A noiseless
calibration measures the algebra, not the instrument. Add ripple at the level the
real measurement carries, or calibrate on a real render whose answer is known
because we set it.

**Every batch keeps its control cells, including the ones that seem to have
already done their job.** Batch N carried plucked controls alongside its tied
cells and they are the only reason the tie problem was diagnosable. Batch P
dropped them as redundant, one day later, and spent a full export round on
fixtures that could not answer the question.

**Summary statistics cannot distinguish shapes.** The old plucky VCA matched
ReBirth on envelope coefficient-of-variation and silence fraction, and was still
the wrong shape entirely. Slice the thing you are measuring.

**Derived ratios hide the error.** Two filter refits chased the "resonance span"
and moved the rms numbers without moving it. The raw surfaces showed the
mechanism immediately.

**Mono-summing hid a pan law seven times too wide.** Measure stereo, or check
that L minus R is zero and say that you checked.

**ReBirth's Export runs in song mode**, so the arrangement plays, not the panel
knobs. Fixtures must bake settings into the arrangement.

**A ReBirth export is only clean with the master compressor verified off.** This
has silently confounded a whole round.

**Never null-test or sample-difference against ReBirth.** Its exports are not
sample-deterministic. Level, spectrum and envelope only.

**DO null-test our own renders against each other.** Our voice resets phase, so two
renders of the same settings are bit-identical and shipped-minus-candidate is
exactly what a change did, isolated from the rest of the signal. `null_diff.py`
does it (and verifies the determinism first). Read the residual: band-limited in
the region you meant to move is a clean change, spread to Nyquist is a click, and
near-zero in a regime you did not mean to touch proves the change is gated there.
Full guidance in the `ab-parity` skill.

**A behaviour that needs two flags to COINCIDE needs a fixture where they
coincide.** Two real bugs shipped for months behind this. Batch G's gate group
has rests, and its slide group has slides, but no rest in it is preceded by a
slid step, so the tie-into-a-rest path never ran once. Clodhopper has three such
rests per bar and Luke heard both defects immediately: tied rests were being
treated as note-offs (a 32 dB hole), and the accent row is read on a rest and
lands on whatever is sounding through it. Neither is visible in a
one-control-at-a-time sweep BY CONSTRUCTION, because each is an interaction. When
designing a batch, include cells where flags collide, not only cells where one
varies.

**Luke's ear has now caught four defects that no metric flagged**, including both
of the above. When he says something sounds different the magnitude is usually
right even when the diagnosis is not: he guessed glide for the held-note problem
and it was the gate, but he was pointing at the right note.

**The 303 slide flag sits on the note you slide FROM.** A flag on the destination
does nothing; fixtures built the wrong way round measure nothing at all.

**Non-obvious code that looks like a bug and is not**: drum automation param 0
mutes when the value is ZERO; the 808 has five shared columns selected by switch
bytes at `808` chunk offsets +11/+14/+17/+19/+21; there are two 808 trim tables,
sample and synthesis; the 303 VCA re-plucks with a 1.2 ms attack, not a step,
because a step was a broadband click that survived four wrong hypotheses.

---

## 8. Beyond the 303

`docs/COMPONENT_MATRIX.md` carries the full per-control inventory for every
component. Two things to know before starting supertask 2 or 3.

**ReBirth's drum machines are sample players**, 24 embedded recordings in the 808
and 22 in the 909, not synthesis models. Read `docs/REBIRTH_CHARACTER.md` first.
Our synthesis path is the fallback for when no samples are present, and it is what
a clean install actually hears, since nothing supplies samples until a mod is
found.

**This section used to say the synthesis path "cannot reach parity by design".
That is too strong and was written before either machine's synth pass.** Both have
since been fitted against the same isolated exports as their sample paths: every
909 synth voice now lands within about 1 dB with its dominant frequency, centroid
and rolloff matching, and a whole solo-909 song comes out within 0.04 dB at 0.975
envelope correlation. What does remain is real but narrow, and worth naming so it
is not rediscovered as a mystery: our synthesised kick carries roughly 2 dB less
20-100 Hz and 1.5 dB more 100-300 than a recording of one, and because our voices
have deterministic phase, simultaneous hits sum more coherently than ReBirth's
samples do, so our peaks run 2 to 5 dB hotter in a mix than one voice at a time
would predict.

**Cross-check the control list against the MIDI map in Appendix C** before
trusting chapter 15. Doing that for the 909 corrected three rows and found a
control we had not implemented at all. The 808 section was built from chapter 15
alone and has not had this cross-check.

Cross-cutting items. **Three of these were closed on 2026-08-26 and the entry is
kept because the reasoning is instructive**: the pan law and the MIXR volume law
were both measured (the pan sweep this section asked for is what settled it), and
the answers were not small corrections but different laws. Pan is not equal-power
at all: the near side holds unity and only the far side falls, so centre sits
3.01 dB above a hard pan. Both faders are exponential at about 0.370 dB per byte,
not linear, which had Clodhopper 8.0 dB too loud. See PARITY.md.

**The lesson in that**: all three were invisible to every fixture, because rbsgen
writes 100 into both faders and 64 into pan, which is exactly where each law is
anchored. A control whose fixture value never moves has not been measured, however
many fixtures use it.

Still open: the whole-song spectral tilt, effect-track automation (each effect
track's `param 0` on/off is now decoded for the COMPRESSOR only; the rest of the
four tracks still do not decode), and the `.rbm` sprite decode that blocks the
classic skin.

---

## Appendix: preserved reference numbers, 2026-07-18

Hand-captured, still valid measurements of ReBirth, superseded in coverage by
batch G (54 fixtures, the full 303 panel) and batch H (a 36-cell grid). Note the
whole set is at note A, which is exactly the narrowness batch G was built to fix.
Use only as a cross-check.

Cutoff sweep (resonance 120, env-mod 0, note A), cutoff 30/60/90/120:
85% rolloff 770 / 1238 / 2073 / 3402 Hz; centroid 528 / 804 / 1252 / 2004 Hz.

Env-swept resonance (cutoff 90, env-mod 70), resonance 20/60/100/127:
peak prominence -18.7 / -15.9 / -15.3 / -14.1 dB; centroid 243 / 413 / 959 / 1824 Hz.

Static resonance (env-mod 0): cutoff 90, resonance 20/60/100/127 gives peak
-22.3 / -15.4 / -8.4 / -2.8 dB and centroid 191 / 313 / 635 / 1060 Hz; cutoff 120
gives peak -19.8 / -15.9 / -11.4 / -6.2 dB and centroid 223 / 377 / 838 / 1519 Hz.

---
