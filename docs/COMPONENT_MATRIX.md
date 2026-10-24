# Component matrix

## Where we are

**Supertask 1 (the 303) is SIGNED OFF as of 2026-09-05 (Luke: "it's done").**
The 2026-08-29 reopening (below) ran to completion. The five defects from batches
B1 to B3 were resolved or accepted: accent fixed and shipped; envelope, cutoff
corner and accent-depth refits landed; the resonant-peak/round-chirp character
closed with FB_SEP shipped for the held chirp and the retriggered mid-reso chirp
accepted as a documented residual (item 8 below, `research/BASS303_FINDINGS.md` §31, the
concluded `exp/diode-ladder-chirp` branch). The 303 work (with the
delay and the PCF) was folded into `main` on 2026-09-16 (Luke's sign-off); the final
HONEST whole-track assessment is still deferred, by Luke's call, until the rest of the
effects path (compressor, distortion) is far enough along to judge the whole against a
full ReBirth track. Parity work
targeted the NATIVE TPT voice; Open303 stays faithful to its own source and is not
bent toward ReBirth. The reopening record follows for history:

**Supertask 1 (the 303) was REOPENED 2026-08-29.** It had been parked at a
"near-inaudible filter-character residual"; on a clean single-303 comparison it
is not near-inaudible. Calm's first 50 seconds is bass 1 alone with the rack off
on both sides, and there our harmonics run 10 to 20 dB weaker than ReBirth's from
86 Hz up. Batches B1 to B3, 91 ReBirth exports, then measured five distinct
defects. See `docs/research/BASS303_FINDINGS.md`, which is the live worklist. Parity work
targets the NATIVE TPT voice: Luke's call the same day is that Open303 stays
faithful to its own source and is not to be bent toward ReBirth. **Supertask 3 (the 909) is complete and merged to main on BOTH paths.** The
sample path was ear-approved 2026-08-25, every control on the page-197 map measured
and fitted, flam included. The **synthesis path**, which is what ships when no mod
supplies samples, was fitted 2026-08-26/27 and signed off ("lock it, I'm happy with
the synth drums"), merge `c46a03c`. It now sits within about 1 dB per isolated
voice and within 0.04 dB across a whole song at 0.975 envelope correlation. See
`docs/research/DRUM909_FINDINGS.md` and section 3. What remains for the 909 is the same
drum-channel master-effects work the 808 is waiting on.

**Supertask 2 (the 808) is complete and merged to main:** the **sample path** in
rounds 7-9, and the **shippable synth path** frozen on Luke's sign-off 2026-08-24
(he was happy with the results). See `docs/research/DRUM808_FINDINGS.md` and section 2. The
synth pass re-fit every voice under power averaging (the round-3 constants having
been fitted through the coherent-averaging bug) and then reshaped five voices on
Luke's listening; every voice lands within ~0.1 dB of its export at the default
knob. The three drum-channel master effects are the remaining 808 work. **Supertask 4 (the PCF) is SIGNED OFF as of 2026-09-16 (Luke: "this is it,
congratulations").** Beyond the static parity (chunk, routing, cutoff, resonance,
corner drift, level law, decay table and all 55 preset patterns, cutoff tracking
ReBirth to 0.23 octave), the clean-room re-derivation closed the character: a
re-derived banded resonator (high-cutoff prominence), a DEPTH-CAPPED envelope-to-
cutoff reach (the swept chirp descends instead of plateauing, low base no longer
over-opens), an S-shaped Naka-Rushton velocity curve (quiet steps return the filter
low, the "click-and-clack", mid steps fill the sweep), and an attack ramp that
glides to the peak then decays (the pendulous wave, not a per-step sawtooth). All
measurement-sourced, ear-confirmed on pat16. See `docs/research/PCF_FINDINGS.md` §14-26 and
section 4 below. A/B toggles kept: `REBORN_PCF_RESO=0` / `REBORN_PCF_ENVLAW=old`
reproduce the shipped behaviour byte-identical.

**Supertask 5 (the delay) is complete and folded in (2026-09-10, merge `c42ec9c`).**
Clean-room measured and re-derived: length `floor(Steps*Fs*60/(bpm*D))` (D=3 triplet
/ 4 straight), feedback = byte/127 to unity, static return via the mixer pan law (no
ping-pong), no echo-path filter, send = the exponential mixer fader. See
`docs/research/PLAN_DELAY.md` / `specs/SPEC_DELAY.md`; two minor gaps (DELY automation track,
bypass/tail) were left open. Both are closed in code as of 2026-09-26 (review
E3): in Song mode the delay plays its DELY track, tick-0 dump and rides, not its
chunk, and the track's param 0 switches the unit. Measured 2026-09-27: switching
the unit off clears the delay, which ours now does too, and a ride lands at its
1/32 tick and glides (duration and shape unmeasured), where ours waits for the
next 16th and steps (BACKLOG 18, engine-wide). Section 5 has the split.

**Supertask 6 (the compressor) is SIGNED OFF (2026-09-17) and FOLDED TO MAIN
(2026-09-18, fold `e8a3e84`).** Fully measured
(6 export batches, `research/PLAN_COMP.md`) and fitted clean-room by a walled implementer.
MEASURED: ReBirth boosts below-threshold signals (the "leveller" character,
contra the manual), the boost scales with ratio and falls with threshold, full
reduction arrives within ~30 ms and recovery by ~250 ms on drum transients, and
ratio 0 is an EXACT bypass. The round-2 MODEL signed off then (a fit, not a
measurement, and superseded) was a soft-knee feed-forward downward compressor
with automatic make-up, an RMS detector and attack ~10-20 ms / release ~100-200
ms constants. **Round 5 (2026-10-01, branch `comp-detector-cleanroom`, merged on
Luke's sign-off at the close-out, 2026-10-23) replaces that model** with a peak follower on the lowpassed (l + r) /
2 (hold, dB-linear release, no gain smoother) and a LINEAR gain law, `g = (1 -
k) + k * T / max(env, F)`: `specs/SPEC_COMP.md`, "Round 5 result". COMP chunk decoded ([on][ratio]
[threshold][meter]) and the Ratio/Threshold bytes now feed the DSP. Verified by
Role A against ReBirth's own renders (the `comp_wav` harness: ReBirth's
uncompressed drone through our compressor, no source confound) to within ~1 dB
across the threshold/ratio/plane surface. ONE accepted residual: the threshold-0 /
quietest-input corner (-4.17 dB), PROVEN unclosable in the round-2 model (input-
independent make-up is pinned by the ratio-127 loud cell; closing it needs an
input-dependent make-up / structural rewrite). Round 5's linear law is that
rewrite and brings the corner to -0.03 dB. Defaults OFF; inert when off
(comp-off render byte-identical). **2026-10-01:** the COMP rides play, and the
engine integration that `comp_wav` could not see was measured and fixed: the
master compressor's input level, its L+R detector, and the section path (section
6 below; `PARITY.md`, "The compressor's rides and topology"). A verification rule surfaced: NEVER measure on
the Open303 voice (15.65 dB quiet), always `REBORN_BASS_ENGINE=tpt` (BACKLOG 15).
**Supertask 7 (the distortion) is SIGNED OFF and FOLDED TO MAIN (2026-09-18, fold
`7fb54dc`).** The character was re-derived clean-room as a fitted WAVEFOLDER (not
the old tanh): pre-gain(Amount) -> triangle wavefold(Shape) -> level-quantize ->
post low-pass, with the inharmonic "vocoder" texture matched and ear-signed off
2026-09-18. All five structural items done: per-section chains with distortion
before the PCF and pan applied once (were already true after the PCF-supertask
rewrite), the phantom `level` param retired, the output GAIN LAW fitted
(`clamp(kOutBase*exp(kOutAmt*amount + kOutShp*shape))`, median level gap across the
grid 4.6->2.0 dB), and the DIST timed Amount/Shape/on-off automation decoded
(`AutoTarget::Dist`; Mutilated Species II rides Amount 1241 times, and songs that
start at Amount 0 on the track used to render distortion-off entirely). Whole-mix
regression (A Taste of Haste): our full mix tracks ReBirth within -1.5 dB and
~1.5 dB per band, the soloed distorted bass lifts 2-16 kHz +15.4 dB. See
`docs/research/PLAN_DIST.md`, `docs/specs/SPEC_DIST.md`, `docs/specs/SPEC_DIST_GAIN.md`. The
control-inventory audit mapped what these decode and drop; see
`docs/research/CONTROL_INVENTORY_AUDIT.md` and `docs/BACKLOG.md`. Order is fixed; a
component is not finished until Luke says so.

**With that, all seven supertasks (303, 808, 909, PCF, delay, compressor,
distortion) are folded to main.** origin/main push and any final whole-mix demo
regression pass are the remaining housekeeping.

**Every fixture from 2026-08-29 onward is isolation-checked.** One device, one
instance of it, one effect or none, verified by
`tools/rbsgen/isolation_check.py` before export. See `docs/research/PLAN_ISOLATION_TESTS.md`.

**A parallel modular-voices fork is complete (P1 to P7); see `docs/PLUGINS.md`.**
It made the 303 a pluggable voice slot: an Open303-derived engine became the
default best-of-breed voice, with the native TPT engine selectable as the
ReBirth-PARITY voice (since 2026-09-18 the TPT voice is the default and Open303
the selectable one), and either 303 slot can host a third-party CLAP or VST3
synth (jc303 and our own Open303 plugin verified). This does NOT change the
parity target or this matrix: parity is still measured against the native TPT
voice, which is unmoved (byte-neutral pooled hash 50d00a6a with
REBORN_BASS_ENGINE=tpt). Open303 emulates the hardware 303, not ReBirth, so it is
a synthesis/product input, never a parity input.

**For the filter, start at `docs/research/PLAN_303_ENVELOPE.md`.** It is the maintained
entry point and it leads with what is wrong rather than what was found, because
two earlier versions of it were built on findings since retracted. Its §0 is the
canonical answer on whether the filter needs a structural change, and **its §6 is
the current 303 worklist**, assessed 2026-08-05 into three tracks whose order is
forced by measurement contamination rather than by importance. The same ordering
is encoded in the task list as `blockedBy` edges. The list below is the
definition of DONE; §6 is the order of WORK.

> **Corrected 2026-08-03.** The paragraph that stood here from 2026-07-31 said
> work lived on branch `303-corner-rise` and that **nothing was merged and no
> default had changed since `d80e1a5`**. Both were false within a day of being
> written and stayed in the file. Two default changes have shipped on Luke's
> sign-off since: the refitted filter constants on 2026-07-31, and the low-end
> highpass on 2026-08-02. It also pointed at `research/RUNBOOK.md` section 5, which had
> gone stale in the same way and is now cut back.
>
> **Do not state the live branch or the merge status in prose here.** It goes
> stale in days, and a stale claim that nothing has changed is how a superseded
> constant gets treated as current. Read it from `git` and from the defaults in
> `src/devices/bass_synth.cpp`. When you change a default, grep the docs for the
> old value in the same commit.

The tables below are the per-control inventory and the definition of done. Where
a row's status predates 2026-07-31 and rests on a slope, a peak frequency at high
cutoff, or a curve rms, treat it as suspect: the primary harmonic instrument was
found to be miscounting on that date. `docs/research/FINDINGS_AUDIT.md` carries what was
retracted and what it re-measured to.

### Settled and verified against ReBirth

| | |
|---|---|
| **tune** | a 32-slot table; matches on all 128 knob values to 0.000 semitones, ear-confirmed. Fixed 7 of 16 shipped songs that were up to 1.38 semitones out |
| **slide** | time constant was 3x too slow; now -0.4 ms mean error over five intervals |
| **register, octave** | correct, within 0.09 semitones. Nothing outside note A had ever been checked |
| **vintage mode** | settled from the file format; no reference was ever in vintage mode, so no refit was needed |
| **waveform** | ReBirth's square is a **pulse at duty 0.52286**, 5.20 dB below its saw. Fitted to 0.03 dB rms with the filter cancelled out |
| **output DC** | we had -0.0055 (saw) and -0.0140 (square) against ReBirth's +/-0.000003. A plain defect, not a mis-tuning. Fixed |
| **VCA shape** | ReBirth holds flat for **half a step** then releases with **tau 22.2 ms**. Gate confirmed proportional at five tempos, 60 to 180 BPM |
| **env mod depth** | refitted against a nine-point sweep crossed with three decays: 0.2215 to **0.0384 octaves rms** at resonance 20. Batch B2 tried to cut it to 3.45 octaves and three regression sets rejected that; the apparent excess is filter SLOPE, not depth |
| **filter env decay law** | quadratic in the knob, **not a line**. Refitted 2026-08-29 on B2's 25-cell trajectory grid: timing error 2.267 to **0.425 dB rms**, and the decay-0 cell that carried it 8.54 to 1.30. Confirmed by three independent sources |
| **brightness per octave** | **ours is 0 to 25 percent HIGHER than ReBirth's**, measured as the per-cell gain in B2. A slope and skirt finding, evidence for defects 3 and 5 |
| **level against the cutoff knob** | ReBirth's 303 gains **10.8 dB** across the knob at resonance 0 and ours gained 2.4. Fixed 2026-08-29 as a resonance-gated tilt: batch H's cutoff tilt goes -1.38 dB to **-0.03**, so the remaining 3 dB deficit is now a one-number calibration and no longer filter error |
| **accent** | mechanism established (shorter filter envelope plus knob-scaled depth). Constants **provisional**, see below |

### The filter, and a correction to what this file used to say

The 2026-07-27 version of this section said the resonance-span mismatch, the
passband level and the high-resonance env-mod shortfall were "three independent
measurements pointing at one defect", and that the mechanism was a corner that
rises with resonance in ReBirth and fell in ours.

**The direction survives. The magnitudes recorded here did not.** They came from
`tools/analyze/invert_filter.py`, since retired: it carried a stale base constant
making every absolute frequency 1.43x too high, its search clipped exactly at
cutoff 127, single cells were fitted equally well by up to seven corners spanning
0.90 octaves with 6 of 24 having no fit at all, and the rise was computed against
resonance 0 where there is no resonant peak and the corner is not observable.

Measured with `harm_response.py` on cells that have a peak, ReBirth's corner rise
over resonance 60 to 127 is **+0.51 octaves on average and roughly flat across
cutoff** (0.42 to 0.49 for cutoffs 40 through 127), not the 0.26 to 1.71 spread
recorded before. Two of the primary metrics used for every earlier 303 filter
number are also now known to be flawed and are **left flawed on purpose** so the
historical record stays comparable; see the runbook's tool notes.

### Remaining for the 303, in dependency order

1. **The filter constants: fitted, NOT validated, NOT applied.** A refit takes
   peak-frequency error 0.2553 to 0.1452 octaves rms and flattens the cutoff
   tilt. **The noise-floor blocker recorded here is DISCHARGED, corrected
   2026-08-05:** this said batch H had no duplicate exports and that the check
   "gates the rest". `renders/batchH_floor/` holds 30 files, six cells at five
   ReBirth exports each, and the check was run twice, pooled median 0.114 dB rms
   against a shipped config scoring 4.745, so the fit sits about 42x above its
   own noise. Nothing below is gated by it and has not been since 2026-07-31.
   What is still true is that the refit is not applied.
2. **The drive shaper does not survive a joint refit. NOT blocked, corrected
   2026-08-05:** the ear check this said it was waiting on was run 2026-07-31 and
   drive ON won all four cases, so `satAsym()` stays. The only live remnant is a
   level-matched rerun, carried in `research/PLAN_303_ENVELOPE.md` §8. The measurement
   case for removal is separately void, having rested on the harmonic instrument
   found to be miscounting. See `research/OPEN_DECISIONS.md` "Resolved calls" §1.
3. **The skirt is too steep in all 16 sweep cells** and ReBirth's shallows with
   resonance where ours does not. **Retired 2026-07-31 as a topology question:**
   both halves of that reading were retracted (a band-anchoring bug in
   `skirt_knob.py`, then a half-bin error in `find_f0()`), and what survives is a
   1 to 2 dB/oct residual against a confirmed four-pole ladder, which is a refit.
   The genuine structural question is the resonant-peak PROMINENCE, a different
   axis. See `research/PLAN_303_ENVELOPE.md` §0 for the canonical statement of which is
   which. **RESOLVED 2026-08-06 as NOT structural.** Task 80's prominence ear
   check (Luke's ear plus measurement at reso 120 and 127) found ReBirth's
   prominence is bracketed by our own K_B: it is a conservative, reso-independent
   K_B, not a topology limit. The diode fork is SHELVED; the fix is a
   reso-and-cutoff-dependent K_B refit (task 85). See research/FINDINGS_AUDIT.md
   2026-08-06.
4. **The output level is not a scalar yet.** Do not apply a makeup gain; it would
   absorb filter error.
5. Env mod at high resonance, which belongs to the filter thread.
6. Decay at env mod 0: ReBirth moves 0.10 octaves while provably not touching the
   VCA. Unexplained, and recorded as unexplained.
7. The accent constants, last, once 1 to 3 stop distorting them.
8. **The round chirp: HELD shipped, RETRIGGERED accepted as a residual
   (2026-09-05).** FB_SEP (separated resonance feedback) ships the HELD/slid
   chirp and the max-reso tail (`be0970e`, default on, Luke ear-approved). The
   RETRIGGERED per-note chirp at moderate resonance stays flat and is now a
   permanent, documented residual: the coupled-ladder branch proved the ride is a
   marginally-stable forward-Euler / loop-delay phenomenon that any fittable
   (stable, decoupled, zero-delay) core removes, so it is not reachable by a
   single-file core swap. See `research/BASS303_FINDINGS.md` §31 and, for the full
   evidence, `research/PLAN_DIODE_CHIRP.md` on `exp/diode-ladder-chirp`.

Also outstanding, not blocking: implement the vintage switch, and settle the two
undecoded bytes at the head of every device chunk.

**Retracted, do not resurrect:** the -19.4 dB/oct skirt figure at cutoff 127
resonance 127 (two exports give -17.8 and -23.6), the claim that the skirt error
crosses over with cutoff, and any re-examination of the retired "reduce the
filter to 18 dB/oct" item, which rested on that figure.

Getting exports was cheap (an unpublished tool drove ReBirth unattended) and a render is now about
0.25 s, so "never measured" is not an acceptable state for any control.

---

The definition of done. One section per component, in supertask order. The
ReBirth column comes from the v2.01 manual, chapter 15 Reference unless noted,
and is authoritative about what exists. The status columns describe *our* code
and are only trustworthy where marked confirmed.

Status key: **done** implemented and A/B measured across the range;
**partial** implemented but unmeasured or measured at too few points;
**gap** missing or known wrong; **?** not yet audited, do not trust.

Phase 1 of each supertask is to replace every `?` in that section with a fact.

---

## 1. Bass synth (303), x2

**Phase 1 inventory done 2026-07-26**, from manual chapter 6 (pp47-49) and the
MIDI map (Appendix C p195). Panel: WAVEFORM (sawtooth or square), TUNE, CUTOFF,
RESO, ENV.MOD, DECAY, ACCENT. Per step: note, octave up and down, accent,
slide, rest. Per section: pattern, bank, length, shuffle.

| Control | CC | Decoded | Applied | UI | Measured | Status |
|---|---|---|---|---|---|---|
| Waveform, saw or square | 23/30 | yes | yes | yes | **12 harmonics x 3 cutoffs, both waveforms** | **done.** ReBirth's square is a pulse at duty 0.52286 sitting 5.20 dB below its saw; see below |
| Tune, **±1 octave in semitone steps** (p48) | 24/31 | yes | yes | yes | **all 128 knob values** | **done.** A 32-slot table, not a continuous law; see `tuneSemitonesFromByte()`. Our render matches ReBirth on every knob value to 0.000 semitones, and Luke confirmed the corrected pitch by ear on Calm 2026-07-27 |
| Cutoff | 25/32 | yes | yes | yes | 4-point sweep, note A only | partial |
| Resonance | 26/33 | yes | yes | yes | 8 static + 4 env-swept, note A | partial, law needs re-fit after the VCA change |
| Env mod | 27/34 | yes | yes | yes | 4-point | partial |
| Decay | 28/35 | yes | yes | yes | 3-point | partial |
| Accent knob | 29/36 | yes | yes | yes | on/off only | gap, no depth sweep |
| Accent accumulation across consecutive accents | | n/a | no | n/a | never tested | **gap, open question** |
| Slide | | yes | yes | yes | **5 intervals** | **done.** Time constant was 3x too slow; see below |
| Octave up/down | | yes | yes | yes | law fitted | partial |
| Rest / gate | | yes | yes | yes | not measured | ? |
| Pattern length, banks, shuffle | | yes | yes | yes | round-trips 18/18 songs | done |
| **Vintage ReBirth Synth Sound** | | **no** | **no** | **no** | see below | **gap** |

The manual is explicit that **accent "does not only affect volume, but also the
shape and character of the sound"** (p49), so a level-only accent model is
wrong by construction.

### Fit these in dependency order, and not before

> **HISTORICAL FROM HERE TO THE END OF SECTION 1, as of 2026-07-31.** The
> numbered list below and the resonance-span analysis under it were written
> 2026-07-27 and are kept because they are the record of how the laws were
> reached and because several still-live conclusions (the waveform, the VCA, env
> mod, accent) were derived here. But the filter numbers in it were measured with
> `rolloff()` and `prominence()`, both since shown flawed, and the corner-rise
> magnitudes came from `invert_filter.py`, since retired. **The current filter
> position is the corrected one at the top of this file.** Read the dependency
> ordering and the mechanisms here; do not take the filter figures as current.

Measured 2026-07-27 against batch G. Accent modulates the same filter envelope
that env-mod and decay drive, so fitting accent first bakes their error into it.
That is the 808 snappy-exponent mistake, which was fitted while the snare body
was still wrong and had to be redone. Work down this list:

1. **The RESONANCE law is wrong, in floor and in range. The cutoff law is
   fine.** Batch H crossed cutoff {16,40,64,90,110,127} against resonance
   {0,30,60,90,110,127} with env mod 0 and a held note, so the filter is static.
   The error is a function of resonance and barely of cutoff:

   | resonance | 0 | 30 | 60 | 90 | 110 | 127 |
   |---|---|---|---|---|---|---|
   | our 85% rolloff / theirs | **2.1** | 1.4 | **1.0** | 0.85 | 0.79 | 0.79 |

   Averaged by cutoff instead, the ratio only moves 1.30 to 1.04, so this is not
   a cutoff-mapping error and the 1.45x from batch G was this seen through a
   single resonance slice.

   Two things are wrong. **The floor:** at resonance 0 ReBirth is essentially a
   non-resonant lowpass (peak prominence -43.8 to -24.3 dB) while ours sits 9 to
   24 dB more prominent, so our feedback never reaches zero. **The range:** at
   cutoff 127 ReBirth's rolloff climbs 4.4x as resonance goes 0 to 127 (877 to
   3903 Hz), because the resonant peak adds high-frequency energy and tracks the
   corner; ours climbs 1.15x (2008 to 2314). Our resonance is a large constant
   with almost no travel, theirs starts at nothing and opens the filter hugely.

   This is the same thing `research/FINDINGS_AUDIT.md` recorded as "component B, a
   reso-DEPENDENT HF slope that flips sign with resonance", now measured on a
   clean 2-D grid instead of two 1-D slices, and it supersedes the open
   resonance re-fit item.

   **Refitted 2026-07-27.** `K_A` 2.8 to 0.0, `K_B` 1.1 to 3.4, base cutoff 554
   to 582 Hz, fitted jointly on 15 cells and validated on all 36. Peak
   prominence error fell from **10.10 to 3.59 dB rms** and rolloff from 0.572 to
   0.409 octaves rms. The floor is fixed: the resonance-0 ratio came from 2.09
   to 1.13.

   **The residual is now a CUTOFF-dependent darkness at high resonance**, which
   is a different defect from the one just fixed. Rolloff ratio, ours/theirs:

   | cut \ reso | 0 | 30 | 60 | 90 | 110 | 127 |
   |---|---|---|---|---|---|---|
   | 16 | 1.02 | 1.13 | 1.15 | 1.01 | 1.08 | 1.03 |
   | 40 | 1.09 | 1.02 | 0.99 | 0.98 | 0.88 | 0.96 |
   | 64 | 1.21 | 0.98 | 0.81 | 0.82 | 0.82 | 0.82 |
   | 90 | 1.16 | 0.89 | 0.76 | 0.71 | 0.72 | 0.73 |
   | 110 | 1.17 | 0.82 | 0.66 | 0.63 | 0.64 | 0.64 |
   | 127 | 1.13 | 0.74 | 0.58 | 0.56 | 0.57 | 0.59 |

   Low cutoff is now good across the whole resonance knob (0.88 to 1.15). The
   error grows with CUTOFF and saturates in resonance: at cutoff 127 we are
   0.56x, far too dark, and it barely moves from resonance 60 upward. Suspect
   the interaction between the cutoff span (`CUT_SLOPE` 2.2) and the
   high-frequency Q loss (`hfQ` starts damping above 3 kHz toward
   `kHiCeil` 2.45), which together cap how far our resonant peak can travel. At
   cutoff 127 ReBirth's rolloff reaches 3903 Hz; ours reaches about 2300.

   **Round 2, same day.** `hfQ` was ruled out on arithmetic before spending a
   measurement on it: at cutoff 127 with env mod 0 the corner sits near 2673 Hz
   and `hfQ` only engages above 3000, so it is inactive in every batch H cell.

   Added a cutoff term to the feedback (`K_CUT`) and refitted with `CUT_SLOPE`,
   again fitting on 9 cells and validating on all 36. Shipped `K_B` 3.0,
   `K_CUT` 0.6, `CUT_SLOPE` 2.2 to 2.5.

   | | rolloff oct rms | prominence dB rms |
   |---|---|---|
   | start of day | 0.572 | 10.10 |
   | round 1 | 0.409 | 3.59 |
   | round 2 | **0.273** | 3.92 |

   The targeted residual largely closed: the ratio by cutoff went from
   1.07 to 0.70 across the knob, down to 1.06 to 0.86. Round 2 cost 0.33 dB of
   prominence to buy 0.14 octaves of rolloff.

   **`K_CUT` was NOT the mechanism, and this is the open thread.** It barely
   moved the diagnostic it was built for: ReBirth's resonance span by cutoff
   runs 1.92 / 2.24 / 2.99 / 3.59 / 3.96 / 4.45 and ours runs
   1.91 / 1.98 / 2.10 / 2.28 / 2.34 / 2.41. Nearly all of round 2's gain came
   from `CUT_SLOPE`. Do not read `K_CUT = 0.6` as a measured law; it is a small
   term that survived a fit.

   New evidence for whoever takes this next: **the filter slope above the
   corner**, measured at low resonance where no peak distorts it.

   | cell | band | ReBirth | ours |
   |---|---|---|---|
   | cut127 reso0 | 3-11 kHz | -18.8 | -17.7 |
   | cut 90 reso0 | 1.8-7 kHz | -20.3 | -19.5 |
   | cut127 reso30 | 3.5-11 kHz | **-22.2** | -19.2 |
   | cut127 reso60 | 4-12 kHz | **-22.3** | -18.2 |

   Slopes nearly match at resonance 0 and diverge as resonance rises: ReBirth
   *steepens* to -22 dB/oct while ours stays near -18. Steeper yet carrying more
   total HF energy means ReBirth's corner sits higher than ours, so the cutoff
   span may still be short even at 2.5. Note this cuts AGAINST the retired
   "reduce the filter to 18 dB/oct" hypothesis in `research/FINDINGS_AUDIT.md`: ReBirth
   is steeper than us, not shallower. That retirement stands.

   Safety: `tools/analyze/whistle_guard.py` checks the change did not push the
   ladder into self-oscillation. On 16 repeated notes at max cutoff and
   resonance, ReBirth's articulation dip is -29.4 dB and ours is -22.4 dB. Both
   articulate clearly, but ours is 7 dB shallower, which is a mild
   over-resonance at that extreme and worth watching if feedback rises again.
   **Round 3 evidence, 2026-07-27: the mismatch shows up in LEVEL too, which no
   spectral-shape metric could have told you.** Across the wave fixtures
   (resonance 20, env mod 0) ReBirth's peak level *rises* 3.54 dB as cutoff goes
   32 to 96, because opening a lowpass passes more of the source. Ours rises
   0.98 dB. Same direction as the resonance-span gap and measured with a
   completely different instrument, so the two corroborate: ReBirth's filter
   opens much further than ours, in bandwidth AND in passband level, and the
   error grows with cutoff. Our saw sits 2.3 dB low at cutoff 32 and 4.9 dB low
   at cutoff 96.
2. **The drive stage is a nonlinearity ReBirth does not have.** New and large.
   `satAsym()` was fitted in round 2 of `research/DESIGN_MEMO_303_HARMONICS.md` against
   the `detroit_303` reference, a high-resonance musical patch, on a
   harmonic-ladder metric.

   The waveform round measures the question directly and cleanly. Dividing the
   square's harmonic series by the saw's cancels every linear stage they share,
   so what is left tests whether the path is linear at all. **ReBirth's fits a
   pure pulse model to 0.03 dB rms over 36 points and its fitted duty is
   identical at all three cutoffs.** A waveshaper anywhere in ReBirth's path
   would break that. Ours misses by 2.29 dB rms and drifts with cutoff.

   | drive | R(k) residual | fitted duty | square/saw osc gain |
   |---|---|---|---|
   | ReBirth | 0.03 dB | 0.52286 | 0.5493 |
   | ours, shipped | 2.29 dB | 0.52418 | 0.5377 |
   | ours, `REBORN_DRIVE=0.5` | 0.43 dB | 0.52316 | 0.5420 |
   | ours, `REBORN_DRIVE=0.25` | **0.07 dB** | **0.52304** | **0.5497** |

   At a quarter of the drive we reproduce ReBirth to within measurement noise on
   all three numbers at once.

   **Do not just turn it down.** Three things have to be settled first. The
   `detroit_303` fit is suspect but not disproven: a nonlinearity's effect scales
   with level, and detroit is a hot high-resonance patch where our ladder output
   is far larger than in these fixtures. The memo also records a caveat that the
   detroit comparison was not run with distortion matched on both sides, which
   is exactly the confound the rules in `MEASUREMENT_PROTOCOL.md` exist to prevent. And the whole
   batch H filter fit was made with the current drive in place, so changing it
   invalidates `K_A`, `K_B`, `K_CUT`, `CUT_SLOPE` and the base cutoff together.

   The right shape for this round: re-measure `detroit_303` with the effect rack
   verified off on both sides, decide whether the asymmetric shaper survives,
   then refit the filter and the drive jointly rather than one after the other.
   That is the same coupled-fit discipline that item 1 needed.
3. **Env mod: REFITTED against batch I2, 0.2215 to 0.0384 octaves rms.**
   `EM_OCT` 1.6 to 2.2, over nine env mod settings crossed with three decays,
   at cutoff 32 and resonance 20 so the filter is not ringing on top of it.

   ReBirth's centroid rise above its own env-mod-0 fixture fits
   `octaves = 0.968 k + 0.324 k^2` (k = knob/127, decay 64) to better than
   0.005 octaves at all nine points. The quadratic term is a quarter of the
   total, and a straight line through 0 and 127 misses the middle by 0.08
   octaves, which is exactly what three points report as "a gain that is too
   small".

   A curvature term was fitted and then NOT shipped: 2.2/0.00 scores 0.0384 and
   2.1/0.15 scores 0.0374, a tie, and buying 0.001 octaves with a free parameter
   is how the accent constants ended up provisional. The reason none is needed is
   worth keeping: the measured quantity is centroid, not corner frequency, and
   our cutoff-to-centroid mapping is already expansive enough to produce
   ReBirth's curve from a linear cutoff law. Re-check it if item 1 moves the
   cutoff law, because that is what supplies the curvature.

   **The residual is a resonance interaction, so it belongs to item 1.** At the
   batch I settings (reso 20) we match to 0.04 octaves. At the batch G settings
   (cutoff 50, reso 100) our envmod-127 centroid is 1658 against ReBirth's 2624,
   up from 1217 but still 37% short. Depth is no longer the explanation.
4. **The VCA shape: FIXED. Decay at envmod 0: still open.**

   The shape was the bigger half and it is done. Sliced per 1.25 ms instead of
   averaged, ReBirth's envelope is flat to within +/-1 dB out to 62 ms and starts
   falling at 63 to 65 ms. A step at 120 BPM is 125 ms, so **the gate is 0.500 of
   a step**, and with it pinned there **the release fits tau = 21.92 ms** across
   all six wave fixtures, spread 0.40 ms. Per-5 ms peak, dB below the note max:

   | ms | 0 | 20 | 40 | 60 | 70 | 80 | 90 | 100 | 110 | 120 |
   |---|---|---|---|---|---|---|---|---|---|---|
   | ReBirth | -2.5 | -0.7 | -0.4 | -0.5 | -4.2 | -8.4 | -11.6 | -16.3 | -19.8 | -23.6 |
   | ours, before | -4.4 | -1.9 | -4.1 | -7.2 | -7.8 | -10.1 | -10.0 | -11.6 | -13.2 | -13.0 |
   | ours, after | -7.7 | -0.6 | -0.2 | -0.7 | -3.8 | -8.8 | -11.5 | -14.9 | -18.2 | -20.2 |

   Ours decayed continuously from the attack because the gate never closed
   between consecutive notes: `fireBass` only called `release()` on a REST, so a
   run of sixteenths was one long held note and a 60 ms plucky decay did all the
   articulating. ReBirth releases at half a step whether or not the next step is
   a note. Fixed by giving the voice the step duration (`setStepTiming`) and
   counting the gate down; a slid step ties by holding past its own end.

   That plucky decay was fitted on **envelope coefficient-of-variation 0.83
   against 0.79 and silence fraction 0.32 against 0.33**, two summary statistics
   of a whole musical patch. Two scalars cannot tell a decay from a
   hold-then-release and both shapes hit them. Worth remembering as a shape of
   mistake, not just an instance of one.

   Recovered 2 dB of the whole-file rms deficit.

   **The knob: DECAY does not touch the VCA. Disproven, batch I3.** The earlier
   note here guessed that "decay reaches the amp envelope"; eight settings say
   otherwise. Across decay 0 to 127 at env mod 0, ReBirth's fitted gate moves
   58.2 to 59.8 ms and its release moves 25.50 to 25.75 ms, i.e. nothing, while
   its centroid still climbs 342.6 to 367.3 Hz, a steady monotonic **0.10
   octaves**. So the decay knob does reach something at env mod 0, and it is not
   the amplitude envelope. The likeliest remaining candidate is that ReBirth's
   filter envelope is never fully disconnected at env mod 0, either a floor on
   the depth or a knob that does not reach zero, and a longer decay then holds
   that residual sweep open slightly longer. Small, but it is a real 7% and it is
   unexplained. Ours is inert to six figures (416.5 Hz at every setting).

   **The gate is PROPORTIONAL. Settled by batch I1, five tempos.** Fitted
   gate/step at 60/90/120/150/180 BPM: 0.455 / 0.482 / 0.474 / 0.475 / 0.465,
   spread 0.027, against a gate in ms that ranges 113.8 down to 38.8. Our own
   render, which uses exactly 0.500, reads 0.472 through the same estimator, so
   the estimator carries a -0.028 bias and **ReBirth's true value is 0.500**.
   Measured against a known ground truth rather than assumed.

   The release is a fixed time constant, as it should be. Measured directly as a
   slope rather than fitted, it is 0.39 dB/ms at every one of the five tempos,
   i.e. **tau = 22.2 ms**, against the 21.92 ms shipped from batch G.

   **Estimator trap worth keeping.** The first pass at this fitted 0.420 / 0.474
   / 0.470 / 0.150 / 0.432 and read as "neither". The 150 BPM cell was not bad
   data: at 150 BPM a 16th step is *exactly 11 cycles* of 110 Hz, so every note
   samples the waveform at the same phases and averaging across notes stops
   smoothing the ripple. Peak-per-slice and an analytic (Hilbert) envelope both
   fail this way. A boxcar of exactly one oscillator period integrates any ripple
   at a multiple of f0 to its mean and is immune by construction. `period_env()`
   in `tools/analyze/vca_ab.py` and `batchI_env.py`.
5. **Then accent.** Its mechanism is settled and implemented (below); only its
   four constants are provisional.

### Waveform: ReBirth's square is a pulse, and it is quieter than the saw

Settled 2026-07-27. The method matters more than the answer, because it is
reusable: dividing the square's harmonic series by the saw's, each relative to
its own fundamental, cancels every linear stage the two share, filter included.
What is left is

    R(k) = |sin(k.pi.d)| / sin(pi.d)

a function of the duty cycle alone. At d = 0.5 it is 1 for odd k and ZERO for
even k, so **any even harmonic at all is a pulse-width offset**, and no model of
the filter, the envelope or the level is needed to see it.

ReBirth fits **d = 0.52286 to 0.03 dB rms** over 12 harmonics at three cutoffs,
with the same d at every cutoff, which is what places it in the oscillator. Its
square's fundamental sits **+0.79 dB** above its saw's at all three cutoffs to
0.01 dB, against the +6.00 dB a same-amplitude pulse would give, so the square
oscillator runs **5.20 dB** below the saw. Shipped as `REBORN_SQR_DUTY` 0.52286
and `REBORN_SQR_GAIN` 0.5493, applied at the oscillator ahead of the drive.

0.52286 and 0.47714 are spectrally identical, so this is the width and not the
phase. The pulse is AC-coupled at source: a 52.286% pulse carries DC of 0.0457
and ReBirth's output has none.

| square minus saw, cut 32 / 64 / 96 | peak dB | rms dB |
|---|---|---|
| ReBirth | -2.33 / -2.73 / -3.03 | +0.01 / -0.25 / -0.48 |
| ours, before | +1.25 / +0.81 / +0.52 | +5.03 / +4.96 / +4.92 |
| ours, after | -2.89 / -3.39 / -3.71 | -0.05 / -0.20 / -0.29 |

The rms relationship is now exact to 0.2 dB. The 0.6 dB left on peak, and the
square/saw centroid ratio still reading 1.045 against ReBirth's 0.964, are both
the drive stage in item 2 above and not the oscillator.

Blast radius: **5 of the 32 shipped 303 voices select the square** (Alloy Cowboy
Resurrection on both, Calm 303a, Groove It Up a 2nd Time! 303b, Multiball
Challenge 303b), plus anything that switches it from automation. On Calm's 303a
the change costs 4.5 dB of rms, which is the size of the error that was there.

### Our output carried DC and ReBirth's does not

Whole-file mean: ReBirth **+/-0.000003**, ours **-0.0055** (saw) and **-0.0140**
(square). `satAsym(x, bias) = tanh(x+bias) - tanh(bias)` cancels the offset for a
signal sitting at zero, not the mean of one that swings, so an
amplitude-dependent offset survived, and the VCA gated it into a step under every
note. A 5 Hz output DC blocker (`REBORN_DC_HP`) takes us to -0.000015 and costs
0.01 dB at the fundamental.

Worth noting how it hid: every 303 metric in the library is a spectral or level
ratio, and not one of them looks at the mean.

### The accent mechanism (settled), and its constants (provisional)

Accent is **a shorter filter envelope plus a knob-scaled depth**, not a level
boost with a brightness side effect. Slicing one note in 15 ms windows separates
two effects that very nearly cancel when averaged over the whole note, which is
why an earlier single-knob measurement concluded "centroid only 1.041x brighter"
and led to the accent's cutoff contribution being capped at 0.02 octaves:

| accent knob | 0 ms | 30 ms | 60 ms | 105 ms |
|---|---|---|---|---|
| 0 | 0.90 | 0.67 | 0.54 | 0.55 |
| 64 | 1.18 | 0.90 | 0.67 | 0.56 |
| 127 | 1.80 | 1.38 | 0.89 | 0.66 |

An accented note starts brighter and decays faster at *every* setting including
zero, so there is a **knob-independent** envelope shortening (about 1.85x) plus
a **knob-scaled** depth. The level lift decays too, from +10.3 dB at the attack
to +2.3 dB by 105 ms at knob 127, so it is not a flat boost.

Whole-note targets, accented steps against the same steps unaccented:

| knob | 0 | 32 | 64 | 96 | 127 |
|---|---|---|---|---|---|
| level dB | −0.12 | +4.14 | +7.15 | +9.51 | +11.37 |
| centroid ratio | 0.662 | 0.822 | 1.016 | 1.250 | 1.507 |

The knob alone does nothing with no accented steps (peak within 0.01 dB,
centroid within 17 Hz), same as the 808's. Accent does **not** accumulate across
a run: four consecutive accents stay within 0.23 dB, with a slight droop rather
than the build a real 303's charge-storing circuit gives. There is a one-step
carryover of +0.9 to +1.1 dB onto the note after a run.

**Constants are provisional.** `REBORN_ACC_SHORT` 0.45, `REBORN_ACC_SWEEP` 0.35,
`REBORN_ACC_LVL` 3.2, `REBORN_ACC_SUS` 0.10, from a 54-point grid search that
landed on the grid EDGE for two of them. Best achieved is 0.64 dB rms on level
and 0.20 on the centroid ratio, against a target spread of 0.845 where we manage
only 0.404. That shortfall is the envelope errors above, not the accent model,
so **refit all four after items 1 to 4 and do not tune them before then**.

### Slide: 3x too slow, and two traps worth remembering

ReBirth reaches **63% of the interval in 17 to 22 ms**, measured across +1, +2,
+4, +9 and -5 semitones. Interval-independent, as a real time constant should
be. Ours ran 44 to 59 ms. After correcting the constant to 0.018 s the mean
error across all five went from +33 ms to **-0.4 ms**, with three of five exact
and the other two inside the estimator's one-cycle resolution.

The old value came from the **hardware**, whose glide is about 60 ms. ReBirth is
far faster, and ReBirth is the target. That is the second control where
following Roland rather than ReBirth was the bug, after the leaky VCA that
ReBirth takes to exact digital zero.

**Trap 1: the slide flag sits on the note you slide FROM.** A flag on the
destination does nothing at all: fixtures with slide on the destination are
byte-for-byte distinct yet render identically to no slide, envelope re-attack
and all. Our engine already had this right (`fireBass` passes the *previous*
step's flag), but the batch G slide fixtures were built the wrong way round and
measured nothing. Verified by exporting all four combinations.

**Trap 2: the note field is 0..12, and out-of-range input is already handled,
differently from ReBirth.** The batch G fixtures asked for `note = 9 + 7 = 16`.
`rbs.cpp` maps anything above 12 to **0**, i.e. C, so sliding from A landed on a
C below it: that is the downward glide to ~65 Hz we measured, and it is the
decoder behaving as written rather than a bug. ReBirth instead renders note 16
as a musical E.

Checked before drawing any conclusion: across all 16 shipped songs, both synths
and all 32 patterns, **every note byte is within 0..12**. So this is a
difference in handling malformed input that no real song can reach, and it needs
no fix. It cost a measurement round only because the fixture was invalid.

### Vintage ReBirth Synth Sound

A switch on ReBirth's Edit menu selecting between two envelope decay lengths:
**on gives a shorter decay** (how v1.5 and earlier always behaved), **off gives
a slightly longer one** (the 2.0 default). New songs default to off, and the
manual states that opening a song saved in v1.5 or earlier turns it on
automatically so the song plays as written (p49).

**This closes the "vintage-mode audit" item open since `DESIGN_MEMO_2026-07-23`
(§161).** Every file we own, all 16 shipped songs and every rbsgen fixture, is
form type **RB40** with byte-identical HEAD bytes, and RB40 is the 2.0 format.
A v1.5 file would carry a different form type, which our loader rejects outright.
So no reference recording we have ever measured against could have been in
vintage mode, every 303 fit to date was made in the one consistent v2.0 mode,
and **no refit is needed**.

It still has to be implemented, because a user can switch it and a v1.5-era song
from the wild would arrive with it on. What it is worth in milliseconds is not
yet measured.

### Two undecoded bytes at the head of every device chunk

`rbs.cpp` labels the first two bytes of the `303`/`808`/`909` chunks `[ver, ?]`
and ignores both. They are not a version:

- **byte 0** is 0 or 1 and differs *per chunk within one song* (Trakka Rekka has
  303a=1, 303b=0, 808=0, 909=0), so it is a per-section flag of some kind.
- **byte 1** ranges 0 to 29 across the shipped songs, consistent with a pattern
  index in 0..31.

Both survive our writer's round trip, so nothing is broken today, but a control
that is stored and not understood cannot be called done. Settle it with a
controlled experiment (change one thing in ReBirth, save, diff) rather than by
inspecting more songs.

---

## 2. TR-808

**Read `docs/REBIRTH_CHARACTER.md` first.** ReBirth's 808 is a sample player
with 24 embedded recordings, not a synthesis model. The parity target for
supertask 2 is the SAMPLE path to its ceiling; the synthesis path is a
best-effort approximation Luke judges by ear (set 2026-08-17).

**Phase 1 done 2026-08-17.** Control map confirmed three ways: the Appendix C
page-196 MIDI map, the RB-338 panel, and Luke's own reading. An earlier note
that the 808 list still needed the page-196 cross-check was stale; it has it.

Measurement harness: Luke supplied the default sample set (`Standard
Components/`, an unpacked mod, 24 named `TR808*.aif` at 16-bit mono 44.1 kHz).
Converted once to canonical `808_*.wav` in the workspace `samples-standard-808/`
(measurement only, never committed). AIFF decode already exists in
`src/io/rbm.cpp`. Our sample-path render loads and plays them.

### Control map (which control exists on which voice)

Every voice has a Level. Beyond that, per the panel and page 196:

| Voice | Level | Tone | Decay | Snappy | Tune | Shared column |
|---|:-:|:-:|:-:|:-:|:-:|---|
| BD | 38 | 39 | 40 | | | |
| SD | 41 | 42 | | 43 | | |
| LT / MT / HT | 44/47/50 | | | | 45/48/51 | LC / MC / HC (conga) |
| RS | 53 | | | | | CL (clave) |
| CP | 55 | | | | | MA (maraca) |
| CB | 57 | | | | | |
| CY | 58 | 59 | 60 | | | |
| OH | 61 | | 62 | | | |
| CH | 63 | | | | | |
| AC (accent) | 37 | | | | | |

The tom Tune is shared by the tom and its conga (one knob per column, three in
total). Each shared column is one sequencer row plus a per-column Switch (CC
46/49/52/54/56) that picks tom-vs-conga (etc.); a step cannot sound both. There
are 11 Level faders, one per column, not 16; the alternate voice inherits its
column's fader. `DrumKnobs.level[11]` is therefore correct (an earlier note
calling this a gap was wrong).

### Worklist for phases 3-4 (measured against the sample path)

Round 7 (2026-08-17) fitted the tom tunes and the BD and SD voices. Round 8
(2026-08-18) finished the sample-path voices and corrected a measurement bug that
had mis-set every trim. Round 9 (2026-08-24) polished the residuals, re-fit the SD
(round-7 was confounded), and pinned the BD tone-minimum on Luke's ear. The whole
sample path is merged to main. Details and measured laws in
`docs/research/DRUM808_FINDINGS.md`.

| Item | State | Action |
|---|---|---|
| AC Level global depth 3.95, knob inert alone | done (round 3) | regression-cover |
| Shared-column switches +11/+14/+17/+19/+21, 0 = alternate | done (round 3) | regression-cover |
| **Tom Tune: three independent tunes** | **FITTED r7 (32ef222)** | within one FFT bin; dropped a ~2.1x varispeed |
| **BD Tone (pitch holds) + Decay + tone-min quirk** | **FITTED r7/r9** | pitch flat, decay re-fit; tone-min quirk = level -4 dB + ~100 Hz hi-cut + a ~200 ms gate, all below tone 16 only (byte-verified) |
| **SD real noise + tone/snappy** | **FITTED r7, RE-FIT r9** | round-7 fit was confounded (coherent averaging); re-fit under power, snare no longer ~5x too dark |
| SD Tone/snappy latched at load | residual | de-latch for automation (per-hit variant), like the 909 |
| **CY Tone: moving-pivot tilt on the SAMPLE** | **FITTED r8** | was synth-only/inert; one-pole tilt, fc 1.5->8.5 kHz, 1.2 dB RMS |
| **CY Decay: exponential** | **FITTED r8** | `0.0400 * 33.00^decay` (re-fit with the brightness lowpass in place); mid ~20% long, top ~8% short residual |
| **OH Decay: a GATE, not exponential** | **FITTED r8** | hold `min(0.018+0.721*decay,0.585)` s then 16 ms release; within a few ms |
| **Spurious tom/CH decay envelopes** | **REMOVED r8** | no knob behind them; tom tails now within 0.2 dB |
| **CH + OH on one step = short open hat, p149** | **FITTED r8** | keep OH, 28 ms choke, suppress CH; was playing the closed hat |
| **All 16 trims re-fit by POWER averaging** | **FITTED r8** | coherent averaging cancelled tonal/HF voices and mis-set them 4-16 dB; score808 corrected |
| Metallic ~11 kHz resonance | **RETIRED r8** | was a coherent-averaging artifact; under power averaging CH and OH match ReBirth exactly, samples are byte-identical to `samples-original`. No filter. |
| **Cymbal brightness (CY only)** | **FITTED r8** | a gentle ~3 kHz one-pole lowpass on the cymbal (CH/OH untouched); centroid 8475 vs 8449, rolloff 12242 vs 12155; trim and decay re-fit around it |
| Pattern length, banks, shuffle, accent row | done | regression-cover |

---

## 3. TR-909

Same architecture note. 22 embedded recordings.

The control list below is the **MIDI map from Appendix C, page 197**, which
enumerates every externally addressable parameter and is therefore an
independent completeness check on chapter 15. It is richer than our `DrumKnobs`
struct, which was shaped before this was read.

| Control | CC | ReBirth | Ours | Status |
|---|---|---|---|---|
| AC Level | 65 | global accent | **FITTED**: gain = 0.773 + 3.070*knob, exactly linear; the knob alone does nothing | done |
| BD Level, Tune, Attack, Decay | 66-69 | 4 variants on an ASCENDING ladder; Attack softens the onset transient | **FITTED**: ladder un-inverted (equal quarters), decay is the reciprocal-remainder law, Attack is an opening lowpass | done |
| `909_bd0`, the fifth recording | | 1.918 s, 21.5 Hz dominant | never loaded; measured NOT to be the long-decay kick, role still unexplained | open question, not a gap |
| SD Level, Tune, **Tone**, Snappy | 70-73 | Tune is a 5-step ladder; Snappy sets noise level and steps 3 layers; **Tone sets the noise tail's LENGTH** | **FITTED and ear-approved**: ladder exact, gated noise, body gain constant | done |
| LT Level, Tune, Decay | 74-76 | independent per tom | **FITTED**: bytes 12/13 decoded, own tune and decay | done |
| MT Level, Tune, Decay | 77-79 | independent | **FITTED**: bytes 15/16 were dropped, now decoded | done |
| HT Level, Tune, Decay | 80-82 | independent | **FITTED**: bytes 18/19 were dropped, now decoded | done |
| Hi-hat Level, **one control for CH and OH** | 83 | shared level | correct, both read byte 22 | done |
| RS Level, CP Level | 84, 85 | one sample each | spectra already matched, trims fitted | done |
| CH Decay, OH Decay | 86, 87 | yes; same-step CH+OH plays the FULL open hat (p35) | **FITTED**: both on the shared decay law; same-step behaviour already matched | done |
| CC Level, CC Tune | 88, 89 | crash; NO decay control | **FITTED**: tune span 10.56 st; the spurious decay envelope removed | done |
| RC Level, RC Tune | 90, 91 | ride; NO decay control | **FITTED**: ride tune is header byte 28, was never read; spurious envelope removed | done |
| **Flam amount** | 92 | header byte 29; only affects steps whose cell value is 4; sets the GAP, 0 to one 32nd | **IMPLEMENTED** 2026-08-25; level tracks ReBirth within 0.3 dB across the knob | done |
| **Instrument Selection** | 93 | selects the edited voice | n/a for audio | n/a |

Read the 808 MIDI page the same way at the start of supertask 2. The 808 section
above was built from chapter 15 alone and has not had this cross-check.

### Phase-1 audit (2026-08-25, supertask 3 opened, branch `909-parity`)

Traced every control through decode (`decode909Knobs`), the device
(`drum909.cpp`), and the sample loader (`sampleset.cpp`). The 909 is a sample
player like the 808; measurement uses `samples-standard-909/` (byte-identical to
the mod). Harness: `tools/measure/score909.py`, `tools/rbsgen/gen_batch909complete.py`
(81 fixtures). The gaps, all found in code and being measured against the export
batch:

- **Header byte 28 is the RIDE TUNE (MIDI CC 91).** The shipped-song header scan
  shows bytes 25/27 tracking as the crash/ride LEVELS and 26/28 as their TUNES, a
  symmetric `CR[L,tune] RD[L,tune]` layout. `decode909Knobs` reads byte 26 (crash
  tune) but not byte 28, and the device conflates both cymbals onto one
  `EX_CYM_TUNE`, so **the ride tune has no path at all**. `rbsgen` now writes byte
  28; the `d909_rdt` sweep confirms it against ReBirth.
- **MT and HT Tune and Decay are dropped.** `decode909Knobs` reads only the LT
  bytes (12/13) into shared `tomTune`/`tomDecay`; header bytes 15/16 (MT) and
  18/19 (HT) are ignored, and the device drives all three toms from one
  `EX_TOM_TUNE`/`EX_TOM_DECAY`. Same class of bug the 808 had. The `mtt/htt/mtd/htd`
  sweeps pin it. `DrumKnobs` has no `mtDecay`/`htDecay`/`rdTune` fields yet.
- **`909_bd0` is the long kick.** Measured: bd0 is 1.918 s and very low (21 Hz
  dominant, centroid 15 Hz), while bd1-4 are all ~0.5 s and form a brightness
  ladder (centroid 55/61/71/90 Hz) that `bdTune` selects among. bd0 is never
  loaded. Hypothesis: it is the kick at LONG DECAY, and our envelope-on-a-0.5s-
  sample cannot ring 1.9 s. The `d909_bdd_127` export tests it.
- **909 hi-hat same-step rule DIFFERS from the 808** (manual p35): on the 909,
  CH+OH on one step plays the FULL open hat, not the 808's shortened hat. Our 909
  code chokes the same-step OH (wrong). `d909_choh_same` measures it.
- **Flam (CC 92)** is a sequencer timing feature (a knob sets the flam interval),
  unimplemented. Deferred to phase 4; not a voice-timbre control.

Still to verify against the exports: AC level law, SD Tone (unaudited), the
BD/SD variant selection vs varispeed split, and the hat/crash/ride levels.

---

## 4. PCF

Supertask 4, measured end to end on 2026-08-27 across 124 ReBirth exports in six
rounds. Every control, the envelope, all 55 patterns and the routing topology.
Settled laws are in `PARITY.md`, accepted residuals in `research/FINDINGS_AUDIT.md`, and
the full working in `docs/research/PCF_FINDINGS.md`.

**Not signed off, but the 2026-08-28 "dull drums" defect is RESOLVED and the
decay law is confirmed.** The 11.5 dB deficit Luke's ear caught on G-Spot was
ROUTING, not the filter: songs PERFORM the PCF's target and we held it at its
opening value, so ours ran over the whole song where ReBirth's runs over part
(`research/PCF_FINDINGS.md` §9, resolved 2026-08-29). The decay-law suspicion was retired:
rounds 7 and 8 (2026-08-29) refit it from the whole trajectory to a QUANTISED
16-step table (`tau = 88.6 ms / rate`, applied in `pcf.cpp`), and an independent
trajectory fit on the round-7 ReBirth renders on 2026-09-05 re-confirmed it, ours
against ReBirth, to under 1% on 12 of 13 resolvable bytes. Round 11 (2026-09-05)
then closed the two static-filter gaps: the corner drift with resonance is REAL
and ours already models it, the top two resonance bytes saturate at byte 112
(nothing above it to measure), and the bottom cutoff bytes are source-limited
(below the ~40 Hz drum-probe floor, no law departure). The apparent high-Q
residual chased next turned out to be a MEASUREMENT ARTIFACT (the broadband drum
probe biases the static transfer; the 2-pole fit absorbs it as a phantom corner
offset): measured cleanly on the drone harmonic-by-harmonic on the current build,
ours matches ReBirth to 0.14 / 0.24 / 0.38 dB rms at Freq 64 Q 0 / 64 / 96 and
the resonant peak matches, so the static filter is at ~0.3 dB across cutoff and
resonance including high Q (`research/PCF_FINDINGS.md` §11 resolution; BACKLOG 12 retired).

**Swept-resonance investigation concluded 2026-09-07 (`research/PCF_FINDINGS.md` §13,
`research/PLAN_PCF_RESONANCE.md`, branch `exp/pcf-resonance`).** Luke's ear on the BP-fixed
set found the PCF "more flangy / ring-modulator / a curve on every chirp". Chased
it exhaustively: the bandpass was fixed (§12), the Amt depth corrected to 0.95
(RB opens ~5% less than 1:1), and two accepted residuals remain, both without a
clean fittable law: (A) swept-resonance brightness across mid/high cutoffs (the
ride is cutoff-position, not resonance dynamics; a resonance-core rewrite was
built and falsified; the low-cutoff over-opening is real but has no law that also
holds the static Amt), and (B) a pattern level deficit that correlates with
trigger density (dense sixteenth/32nd patterns ~2.8 dB quiet, a retrigger effect,
character otherwise matching). Both in FINDINGS_AUDIT / BACKLOG. The -4.1 dB
resonance-makeup plateau is also unexplained but small.

**Update 2026-09-11 (clean-room re-derivation, `research/PCF_FINDINGS.md` §15-20).** Broadband
probes (which the drone could not resolve above 8 kHz) split residual A into two:
(1) a STATIC high-cutoff UNDER-RESONANCE (our resonator was 10-15 dB less prominent
than ReBirth at high cutoff/high Q), now FIXED by a re-derived banded, cutoff-
dependent resonator on branch `pcf-resonator-cleanroom` (validated: prominence
matches across all 8 bands, corner ~3%, flag-gated `REBORN_PCF_RESO`, off =
byte-identical), HELD on its branch, not merged, because PCF still does not close
by ear; and (2) the swept cutoff TRAJECTORY (over-open + waver, resonant pitch ~a
minor third off), the remaining audible gap and the next PCF investigation. Also
found and banked: resonance is 8 quantised bands (not continuous), the decay top
band holds (no decay, was fitted 2.2 s), and the cutoff law top is more open than
the drone fit (top-bend, shipped §14).

**Update 2026-09-16: the PCF is CLOSED by ear and FOLDED IN (`research/PCF_FINDINGS.md`
§21-26, merge `03ff15c` on `pcf-supertask`).** The held resonator plus the whole
envelope re-derivation are merged (the resonator's hold is lifted). The swept
TRAJECTORY residual was the cutoff CLAMP: ReBirth's envelope-to-cutoff reach is
DEPTH-CAPPED `g=min(gcap(amt), D*(1-base))`, no hard clamp, so the cutoff descends
immediately and low base does not over-open (§21-22). The pattern CHARACTER took
three more measured pieces, each ear-driven: a compressive S-shaped velocity curve
(Naka-Rushton) so quiet steps stay quiet and the filter returns low between beats
(the "click-and-clack"), the mid steps boosted to fill the sweep (§23, 26); and an
attack RAMP that glides to the trigger target and reaches the peak then decays,
turning the per-step sawtooth into ReBirth's pendulous wave (§24-25). Luke's ear
confirmed pat16 on 2026-09-16 (chirp + low return + brightness match). A/B toggles
kept: `REBORN_PCF_RESO=0` and `REBORN_PCF_ENVLAW=old` are the shipped behaviour,
byte-identical. Open, both minor and separate: pat05 (dense full velocity) runs a
touch dark in the sample-mode groove (a full-velocity residual, not the envelope);
and pmo_p16's trough is ~19% above ReBirth's (the dynamic range is 4.1x vs 4.8x),
close but not exact. Not yet on `main` (release sign-off pending).

| Feature | ReBirth | Ours | Status |
|---|---|---|---|
| `PCF ` chunk decode | 12 bytes | decoded, all six controls | done |
| PCF automation track | TRAK 7, param N = byte N | tick-0 dump and timed rides | done |
| Per-section distortion rides | mixer TRAK `base+3`, timed | decoded, not dispatched | partial, belongs to supertask 7 |
| On/off | button | a bypass: routing, pan and send survive it | done |
| Routing target | `MIXR` +2, exclusive | `pcfTarget`, exclusive | done |
| FREQ (cutoff) | 0..127 | `fc = 9.01 * 2^(0.08388 * byte)`, plus a quadratic top-bend on the base Freq above byte ~106 | **measured across the whole range**, bytes 0 to 48 confirmed 2026-08-28; **top refit 2026-09-10** (`research/PCF_FINDINGS.md` §14): a broadband corner measurement showed the extrapolated top was too low, RB is more open (byte 120: 11.3 vs 9.7 kHz, byte 127 open); `kTopBendGain`/`kTopBendKnee` match RB to ~2%, zero below the knee so patterns and the chirp are byte-identical |
| Q (resonance) | 0..127 | `1/Q = 0.1055 + 1.3378 * (1-r)^1.77`, knob saturates at byte 112 | **measured**; byte-112 saturation confirmed 2026-09-05; ours matches RB to 0.38 dB rms at Q 96 (drone, current build), peak height and position matched |
| Corner drift with resonance | the corner rises with Q, ~13-16% of fc | quadratic in the knob, anchored at Q = 0 | **done**, confirmed real 2026-09-05 and ours models it; no residual (the apparent high-Q offset was a measurement artifact, BACKLOG 12 retired) |
| MODE switch, lowpass or bandpass | 2-pole, 12 dB/oct, p63 | both taps, byte 0 = BP and 1 = LP | **fixed 2026-09-06**: BP peak grows with Q like RB (was flattened by k-scaling, -19 dB at Q112); now 0.3 dB peak / 0.3 dB rms curve across Q |
| Filter makeup gain | +4.33 dB open, falling with Q | measured table, interpolated | **measured** |
| Bypass costs nothing | 0.00 dB | 0.00 dB | **measured** |
| AMT (envelope amount) | yes | sums into the cutoff parameter at **0.95**, clamped | **corrected 2026-09-07** (was 1:1; RB opens ~5% less, `kEnvDepth`) |
| DEC (envelope decay) | yes | quantised 16 steps, `tau = 88.6 ms / rate`, rate a 1-2-3-4-5-7 series, in seconds; **top band (120-127) HOLDS (no decay)** | **done**, trajectory fit 2026-08-29, re-confirmed 2026-09-05; top-band hold measured and applied 2026-09-11 (PCF_FINDINGS 15, was a wrong 2.2 s) |
| 55 preset patterns, 0 to 54 | p62, diagrams pp206-220 | all 55 measured, `pcf_presets.inc` | **measured** |
| Per-step trigger velocity | bar height in the diagrams, p206 | measured per trigger | **measured** |
| Per-pattern length and rate | length varies and sets the repeat | thirty-second grid, own clock | **partial**, lengths are the least certain column |
| **Series after the distortion** | p64 | series, with its own distortion instance for its section | done |

The 55 patterns are fully specified in Appendix D as step positions and
velocities. Transcribing them is mechanical work, not archaeology.

Measured 2026-08-27 against 124 ReBirth exports across six rounds. The static
filter sits at **0.49 dB mean rms over eleven settings**, worst 2.15 dB at
resonance 96 where ReBirth's corner drifts upward with resonance in a way we do
not model. The envelope tracks ReBirth's cutoff to **0.40 octaves rms with no
systematic offset**, over six patterns spanning whole notes to consecutive
thirty-seconds.

**Whole-mix baseline, 2026-08-27.** Six demo songs exported from ReBirth at full
length with the rack on sit at 2.06 dB mean octave-band error and 2.01 dB mean
level error against our renders. That is a baseline, not a parity figure: all
six switch the master compressor ON and ours defaults OFF because its ratio and
threshold are undecoded, all six use the delay, and four distort sections whose
bus still skips the pan law. Re-measure after the compressor, the delay and the
distortion, and expect most of it to come from them.

**Found here, belongs to the delay: a 3.3 dB whole-mix stereo skew, diagnosed.**
Our render of *Trakka Rekka* tilts 3.13 dB left where ReBirth's own export sits
at -0.62. The cause is the delay's ping-pong, which defaults fully on, is never
set by anything, and feeds the input into the left line only; its wet return
measures 10.81 dB left-biased. Turning it off lands the whole mix at -0.18 dB.
ReBirth has no ping-pong at all, only an output pan we decode and discard.
Recorded in `BACKLOG.md` entry 1 with the isolation steps and the fix, and left
for supertask 5.

**Found here, and fixed because it was blocking:** our distortion carried a
constant making it a 19 dB attenuator, which is what made Calm's 303 inaudible.
Refitted against a seventeen-point ReBirth sweep. Its gain LAW is still
unmeasured (residual spans 7 dB at the knob extremes) and belongs to supertask 7.

**Found here, belongs to another component.** The distortion bus has the same
defect the PCF bus had: it sums channels without the pan law that the dry path
applies, so a distorted section is 3.01 dB hot at centre pan. The PCF's copy is
fixed and measured; the distortion's is left for supertask 7, since its gain
staging has never been measured and changing it now would be a guess.

---

## 5. Delay

Cross-checked against the MIDI map, Appendix C page 198, on 2026-08-02. ReBirth's
delay has exactly four controls: **Steps 100, Triplet 101, Pan 102, Feedback
103.** That check found two we decode and then throw away.

**Clean-room re-derivation, 2026-09-10 (merged into `pcf-supertask`; `main`
sign-off pending).** The delay was measured end to end from ReBirth exports
(`research/PLAN_DELAY.md`, `renders/delay-taps-2026-09-10`) and reimplemented from a
coefficient-free spec by a walled implementer, then Role A verified against the
measured reference. The table below is the post-rewrite state on that branch.

| Control | CC | Ours | Status |
|---|---|---|---|
| Steps | 100 | measured integer length law `floor(Steps*Fs*60/(bpm*D))`, matched exactly incl. truncation | **matched [delay-cleanroom]** |
| Triplet mode | 101 | decoded (DELY+2) and applied: byte 0 = triplet grid (D=3), else straight (D=4) | **applied [delay-cleanroom]** |
| Pan | 102 | decoded (DELY+4) and applied as the mixer pan law on the wet return | **applied [delay-cleanroom]** |
| Feedback | 103 | `= byte/127`, uncapped, exactly 1.0 at 127, matched | **matched [delay-cleanroom]** |
| `DELY` track, tick 0 | | the Song-mode start over the chunk, put back by `play()` in Song mode; measured on the four E3 fixtures | **matched [E3, 2026-09-26]** |
| `DELY` track, pan rides | | played (`AutoTarget::Delay`); a ride moves the delay, measured (dr_even). ReBirth applies it at its 1/32 tick with a glide (duration unmeasured), ours at the next 16th and at once (dr_odd40/41/42; BACKLOG 18) | **applied; timing and glide not matched** |
| `DELY` track, Steps, grid and feedback rides | | played the same way; ASSUMED to behave like the pan ride, no fixture rides them. Echoes in flight when Steps or the grid moves: unmeasured | **applied, unmeasured** |
| Unit on/off (track param 0, no chunk byte) | none | off skips the delay and its return and CLEARS the ring, as ReBirth does (dr_p0, 2026-09-27); ON at tick 0 in all 16 songs, never ridden | **matched [2026-09-27]** |
| Return pan at the extremes | | ReBirth's far side at pan 127 is -46.8 dB, ours silent (BACKLOG 19) | **not matched, inaudible; pan-law sign-off** |
| Per-channel send levels | | applied via the exponential mixer fader on the echo-send byte | **matched [delay-cleanroom]** |
| Our `time` division picker | none | REMOVED (ReBirth uses steps plus the grid byte) | removed [delay-cleanroom] |
| Our `mix` | none | REMOVED (send bus returns wet-only) | removed [delay-cleanroom] |
| Our `tone`, a repeat high-cut | **none** | REMOVED (echo centroid = dry; ReBirth has no such control) | removed [delay-cleanroom] |
| Our ping-pong | none | REMOVED (echoes stay on one side; static return pan) | removed [delay-cleanroom] |

**The delay plays its TRACK, not its chunk (review E3, 2026-09-26, `57c7cfb`).**
In Song mode ReBirth plays the DELY track's tick-0 dump and its rides; the `DELY`
chunk is the Pattern-mode panel, saved as it stood. Measured on four isolated
fixtures whose chunk and track disagree on steps, feedback and pan: ReBirth played
the track to 0.1 dB, a copy naming a mod included. We played the chunk until then,
wrong in 8 of the 16 demo songs, and ignored all 4,435 timed rides. Now our
renders of the four fixtures match ReBirth's echo times, balance and per-repeat
ratio, and the echo gain matches to 0.01 dB against a clean dry reference. The
"0.9 dB excess" once recorded is not the delay: it is ReBirth's hit timing, up to
3.3 dB where an echo lands on a later hit (BACKLOG 20). Full record in `PARITY.md`.

**The ride fixtures (2026-09-27, `fixtures-2026-09-27/delay-rides`) split the
track's behaviour three ways**, as the table above marks it. MEASURED: the tick-0
dump is the start (E3), a timed pan ride moves the delay (dr_even), and a param-0
switch-off clears it (dr_p0); ours matches all three, the last since `f7c7930`.
MEASURED, NOT YET MATCHED: ReBirth applies a ride at its 1/32 tick and glides it
(dr_odd40/41/42), where ours applies it at the next 16th, up to 62.5 ms late at
120 BPM, and at once (BACKLOG 18, engine-wide). ASSUMED: Steps, grid and feedback
rides behave like the pan ride.

**History of the grid and pan bytes.** Until the delay supertask the loader read
only `p[1]` and `p[3]`, and its comment called `+2` "unit": the grid byte and the
pan were decoded and dropped. Both are applied since 2026-09-10 (the rows above).
Scanning the sixteen shipped songs, the chunk's pan byte takes ten distinct values
and is away from centre in ten of them, and the grid byte is 1 (straight) in all
sixteen chunks. What plays is the track's, which differs from the chunk's pan in
five songs: SuperNova 127 against 85, Trakka Rekka 127 against 63, A Taste of
Haste 60 against 90, Transfusion 64 against 90, Alloy Cowboy Resurrection 79
against 75.

---

## 6. Compressor

Cross-checked against the MIDI map, Appendix C page 198, on 2026-08-02.
**ReBirth's compressor has exactly TWO controls: Ratio 106 and Threshold 107.**
Nothing else is addressable.

SIGNED OFF 2026-09-17 (round 2, `comp-cleanroom` folded to `comp-supertask`).
Measured + fitted clean-room; verified by Role A to within ~1 dB across the
surface. See `research/PLAN_COMP.md` (the full campaign) and `specs/SPEC_COMP.md` (the brief).
Round 5 (2026-10-01, branch `comp-detector-cleanroom`, merged 2026-10-23) refit
the detector and the gain law; the rows below describe round 5, and
`specs/SPEC_COMP.md`, "Round 5 result", has the tables.

| Control | CC | Ours | Status |
|---|---|---|---|
| Ratio | 106 | `ratio`, decoded + applied | **fitted, verified** |
| Threshold | 107 | `threshold`, decoded + applied | **fitted, verified** |
| `COMP` chunk decode | | Ratio (byte1) + Threshold (byte2), byte/127 | **done** |
| Applied on the master | | linear levelling law `g = (1 - k) + k * T / max(env, F)` (round 5) | **fitted** to 146 static cells (in-sample, worst 0.023 dB) |
| COMP track rides | | params 0 (on/off), 1 Ratio, 2 Threshold played; 3 (meter) ignored | **played, verified 2026-10-01** |
| Master input level | | bus x 0.72 headroom before it, the master fader's own gain after it | **verified 2026-10-01** |
| Detector | | peak follower on (l + r) / 2 through two one-pole lowpasses (-3 dB at about 4.45 kHz at 44.1 kHz) | **L+R sum verified 2026-10-01**; signed vs abs unmeasured, signed used; peak reading fit 2026-10-01 (round 5) |
| Section path | | before the channel fader and pan | **order verified 2026-10-01** |
| Section input scale | | fader 127 x centre pan x headroom x 0.5; runs on at fader 0 / mute | **fitted 2026-10-01** (mute placement assumed) |
| our `attack` | **none** | 8.94 ms one-pole on the envelope | fit (hidden internal, round 5) |
| our `release` | **none** | 26.29 ms hold, then 150.9 dB/s, floored at F; no gain smoother | fit (hidden internal, round 5) |
| below-threshold boost | **none** | the linear law's ceiling at env = F, `(1 - k) + k * T / F` (+29.7 dB at ratio 127 threshold 0) | fit (the leveller character, round 5) |

What is MEASURED: ReBirth's compressor is a LEVELLER, not the manual's
downward-only unit: below-threshold signals are boosted, which is why the
manual's "levels below pass unprocessed" is wrong (measurement wins). Both
channels get the same gain (master is stereo). Its attack and release are hidden
internals, ours to fit, measured from drum transients.

The round-2 MODEL (a fit, superseded by round 5, never a measurement): `GR =
makeup(ratio,threshold) - slope(ratio)*softKnee(level - threshold)` with an RMS
detector. The round-5 model, which the soft-knee form could not express: `g =
(1 - k) + k * T / max(env, F)` in linear gain, read by a peak follower
(`specs/SPEC_COMP.md`, "Round 5 result"; `src/effects/compressor.cpp`).

**Accepted residual (round 2):** the threshold-byte-0 / quietest-input corner,
-4.17 dB; round 5 brings it to -0.03 dB. PROVEN unclosable in the round-2 model: the defect is under-boost, fixable only by a
larger low-threshold make-up, which (being input-independent) is pinned by the
ratio-127 loud cell, so satisfying one contradicts the other. Round 2 judged the
structural rewrite that would close it not worth it for one extreme cell; round
5's linear gain law is that rewrite, and closed it. See `research/PLAN_COMP.md` (batches
5-6, rounds 3-4) and `specs/SPEC_COMP.md` "Round 5 result".

Every ReBirth reference export before this was made with the compressor OFF; the
comp-on campaign exports (comp-law*, comp-ballistics*) are now the regression set.
The section (mono) compressor path, the last open completeness item, was
measured and fixed on 2026-10-01 (`fixtures-2026-10-01/comp-rides`): every
compressed section cell within 0.53 dB of ReBirth but r064_t000 (+1.33 raw,
+0.66 once the TPT drone's +0.94 dB level residual is taken out with `comp_wav`;
the uncompressed and ratio-0 section cells carry that residual whole). Those are
round-2 numbers. Round 5's peak detector puts every compressed cell within 0.07
dB corrected, but raw +0.93 to +2.33 dB hot, because the TPT drone's peak is
1.4 dB low against ReBirth's while its RMS is 0.94 dB high (BACKLOG 22). Verification MUST use `REBORN_BASS_ENGINE=tpt`
(BACKLOG 15: Open303 is 15.65 dB quiet), and `comp_wav` alone is not enough: it
proves the DSP and cannot see the engine's input level, pan or fader order
(`research/FINDINGS_AUDIT.md`, 2026-10-01).

---

## 7. Distortion: SIGNED OFF, folded to main 2026-09-18

Cross-checked against the MIDI map, Appendix C page 198. ReBirth's distortion has
two controls, **Shape 104 and Amount 105**, and is a WAVEFOLDER (measurement-
proven: only foldback reaches its 4-8% HF; clip/asym/rectify/bitcrush all top
<1%), not the amp-style clip the manual's prose suggests.

| Control | CC | Ours | Status |
|---|---|---|---|
| Amount | 105 | `amount` (DIST byte 2) = wavefolder drive; tick-0 + timed rides decoded | done |
| Shape | 104 | `tone` (DIST byte 1) = fold threshold (bi-Gaussian bell) + quantizer step count; tick-0 + timed rides decoded | done |
| our `level` | none | RETIRED 2026-09-18 (phantom, nothing set it) | removed |
| `DIST` automation track | | `AutoTarget::Dist`: Amount/Shape/on-off rides decoded and dispatched | done |
| Routing per section | | per-section chains, four instances mirroring the UI unit | done |
| Curve shape and output level | | fitted wavefolder (character ear-signed off) + fitted output gain law | done |
| Position in the chain, before the PCF | | per-section chain, distortion -> PCF -> compressor, panned once | done |

**The naming question is resolved.** Shape (`tone` in code) is NOT a tone tilt:
measured, it drives the fold threshold and the quantizer step count (a character
control, non-monotonic in brightness), exactly as the SHAPE label implied and the
old tone-tilt model did not. The name `tone` is kept only because the engine, the
.rbs loader/writer and the plugin all address the param by that string; its
behaviour is the wavefolder character. See `docs/research/PLAN_DIST.md`, `specs/SPEC_DIST.md`,
`specs/SPEC_DIST_GAIN.md`.

---

## 8. Cross-cutting, after the components

Not part of any single component and deliberately deferred until the sources are
right.

| Item | Status |
|---|---|
| Pan law, ours is roughly 7x too wide | FIXED 2026-07-26 (near-side-unity law, `panGains`) |
| Whole-song spectral tilt, bass-light and treble-heavy | CLOSED 2026-09-18: re-measured `--no-fx`, every band within ~2 dB on Calm/G-Spot/Trakka (was 5-15 dB). See PARITY.md. |
| MIXR volume law | ? |
| Master output chain and soft clip | ? |
| Tempo ceiling, 300 here against a higher documented limit | **gap** |
| Effect automation | PCF, DIST, DELY (2026-09-26, review E3), COMP (2026-10-01) and the compressor/PCF routing decode and dispatch. Rides still land on the 16th grid and step (BACKLOG 18) |
| `.rbm` mod sprite decode, blocks the classic skin | blocked, sprites are not decodable JPEGs |
