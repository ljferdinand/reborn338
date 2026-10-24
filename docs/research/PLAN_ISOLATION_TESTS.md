# Plan: purpose-built isolation songs, and a listening protocol

Written 2026-08-29 at Luke's request, after he pointed out that what he is being
asked to judge by ear is confounded by things we have not implemented.

He is right, and the gap is sharper than he put it. **This project has a rigorous
measurement protocol and has had no listening protocol at all.** The `ab-parity`
skill says a fixture sounds one device with no effects, that the master
compressor must be verified off, and that demo songs are a whole-mix regression
suite for the very end rather than a way to isolate anything. Every listening set
handed over so far has been a demo song with three effects running.

## What that costs, concretely

`tools/rbsgen/isolation_check.py` reads a song and reports what is actually in
the path. Pointed at Trakka Rekka, which is where the only 303 reference exports
come from:

```
FAIL Trakka Rekka.rbs
       bass2 is audible at -25 dBFS and should be silent
       808 is audible at -21 dBFS and should be silent
       909 is audible at -24 dBFS and should be silent
       the COMPRESSOR unit is ON
       a section is routed to the compressor (mixer param 1 = 1)
       the distortion unit is ON for section(s) ['bass1', 'bass2']
       echo sends are not zero: [75, 88, 78, 76]
       the mixer track has 884 timed events after tick 0
       the DELY track has 46 timed events after tick 0
       the DIST track has 38 timed events after tick 0
       the PCF track has 28 timed events after tick 0
```

That is not a marginal objection. Building the listening set for this very plan,
the first thing measured was our bass 1 against `Trakka Rekka-303#1.WAV`, and it
said our 303 was missing 25 to 39 dB in its top two octaves. It is not: that
export's bass 1 runs through the distortion, a delay send of 75, the master
compressor, and the PCF for part of the song, while ours was rendered `--no-fx`.
The comparison was worthless and would have sent us to rebuild the 303 filter.

## What we can and cannot currently compare

Measured today, whole-song, our render against ReBirth's export:

| Comparison | octave error | level | clean? |
|---|---|---|---|
| **909 alone, sample mode, rack off** | **0.13 dB** | +0.64 | **yes** |
| Whole mix, rack off, Trakka Rekka | 0.18 dB | -0.15 | yes, but the 303 is 16 dB down and masked |
| Whole mix, rack off, Calm | 3.56 (Open303) / 1.30 (TPT) | +3.10 / +0.15 | yes, and 303-dominated |
| bass 1 alone vs `303#1` export | 3.07 rack-off / 2.75 rack-on | -3.57 / -2.19 | **no**, and it cannot even identify its own provenance |

Two things follow.

**We have exactly one piece of clean single-device ground truth, and it nulls at
0.13 dB.** That is the 909 in sample mode with the rack off. It is the source
every effect test should be built on, not because drums are interesting but
because it is the only place where a difference Luke hears can only be the
effect.

**We have no clean 303 reference at all.** Every 303 export in
`rb338-exports/` went through the rack. So the honest answer to "our 303 does not
sound like ReBirth's" is that we do not currently know how far off it is, and
finding out needs its own fixture round. Calm's rack-off whole mix hints at it
(1.30 dB on the TPT voice, 3.56 on Open303) but Calm is not a fixture.

**And the default voice is not the parity voice.** `REBORN_BASS_ENGINE` defaults
to Open303 while parity was measured on the native TPT voice, so every listening
set to date used the voice parity was NOT measured on. On Calm's rack-off mix TPT
is much closer (1.30 against 3.56); on the confounded Trakka comparison Open303
looked closer. That contradiction is unresolved and it is a prerequisite, not a
detail: we cannot ask Luke which 303 sounds right until we know which one we are
asking about.

## What is not implemented, and therefore what cannot be in a test yet

From `COMPONENT_MATRIX.md`, so a test never contains one of these by accident:

| | State |
|---|---|
| **Compressor ratio, threshold** | never applied. The `COMP` chunk is not even parsed. Defaults OFF while every demo song switches ReBirth's ON |
| **Delay triplet mode** | decoded at `DELY`+2 and dropped |
| **Delay pan** | decoded at `DELY`+4 and dropped. Away from centre in 10 of 16 songs |
| **Delay ping-pong** | ours, no ReBirth control behind it, defaults fully on, 10.8 dB left bias on the wet return. BACKLOG 1 |
| **Delay tone** | ours, ReBirth exposes no such control |
| **Distortion character** | 25 to 38 dB short in the top three octaves. BACKLOG 2 |
| **`DELY`/`DIST`/`COMP` knob automation** | tick-0 dumps read, timed rides dropped. BACKLOG 5 |
| **303 vintage mode** | not decoded, not applied |

## The protocol

### 1. Every listening batch opens with the null pair

The same fixture with no effects at all, ours against ReBirth's. Luke listens to
that **first**.

This is the piece that has never been there and it is the whole point. If the
null pair sounds identical to him, his ear is calibrated for that source and any
difference in the next file is the effect. If it does not, there is no point
testing an effect on it and the source becomes the job instead.

`listen-null-2026-08-29/` is the first one: the 909 alone at 0.13 dB, and the
rack-off whole mix at 0.12 dB.

### 2. One device, one effect, proved rather than assumed

Every fixture passes `isolation_check.py` before it is exported. It verifies, on
the ARRANGEMENT rather than the save-time panel, that only the device under test
makes sound (by rendering each device soloed and measuring, since a silent device
still carries a pattern-select and a knob header), that the compressor is off and
unrouted, that the PCF and distortion are off unless one of them is the subject,
that every echo send is zero unless the delay is the subject, and that nothing is
automated after tick 0.

### 3. Ladders, not points

Three settings minimum and five preferred across each control's range including
both extremes, per the existing protocol. A single setting cannot tell a right
law from a right value at one point, and this project has fitted the wrong curve
from two points more than once.

### 4. One bed, reused everywhere

A single reference pattern under every effect test. Familiarity is what makes an
ear sensitive: after the third batch Luke knows exactly what the bed sounds like
clean, and any colouration stands out. A different bed per effect throws that
away.

The bed: the 909 in sample mode, two bars, a real pattern rather than a test
tone, at 120 BPM, loop widened so the export runs about 40 seconds.

### 5. Order of business

Null, then each effect alone, then pairs, then the whole rack, then demo songs.
Demo songs last, which is what the protocol has always said and what we stopped
doing.

### 6. What "satisfied" means, per effect

1. A measured number on the isolated fixtures, using the metric that suits the
   effect: band error for a static one, a trajectory metric for a time-varying
   one such as the PCF's cutoff or the delay's repeat envelope.
2. Luke's ear on the isolated pair, after the null pair.
3. Only then, the same effect inside a demo song, where the number will be worse
   and we will know why.

## The fixture catalogue

All on the same bed, all `--samples`, all passing `isolation_check.py`.

| Batch | Fixtures | Varies | Answers |
|---|---|---|---|
| **N, null** | 1 | nothing | is our source indistinguishable from ReBirth's? |
| **P, PCF** | ~12 | Freq x Amt at one pattern; then pattern variety; then the decay ladder | mostly re-confirmation, since rounds 7 to 10 measured this. Its value is that Luke can finally hear it alone |
| **D, delay** | ~9 | Steps, Feedback, and the **Pan byte we currently discard** | supertask 5. The pan and ping-pong questions are audible in one pair |
| **X, distortion** | ~12 | Amount x Shape, crossed with the input FADER | supertask 7. Crossed with level because a waveshaper's behaviour depends on how hard it is driven, unlike a filter |
| **C, compressor** | ~9 | Ratio x Threshold | supertask 6, once the `COMP` chunk is parsed. Cannot be built before that |
| **S, stacking** | ~6 | dist to PCF (the documented series order), PCF plus delay, all three | Luke's "then in various combinations" from the 28th |
| **B, the 303** | ~12 | Cutoff x Resonance x Env mod on a held note and on a real line, both voices | the missing ground truth. Not an effect test |

Batch B is the one that unblocks judging any effect on a 303, and it is also the
answer to "our filters still do not sound like ReBirth's", which we currently
cannot confirm or deny.

## Why a drum bed is enough for a filter and not for a distortion

The PCF and the delay are linear. If their response and their timing match on
one source they match on every source, so testing them on the source where we
are already at null is not a compromise, it is the correct choice.

The distortion and the compressor are not linear. What they do depends on how
hard they are driven and on the input's spectrum. Their ladders therefore have to
cross the knob with the input LEVEL, and they will eventually need a 303 under
them too, which is another reason batch B comes before batch X finishes.

## What changes in how results get reported

Three things this session got wrong that this fixes.

**A listening set says which drums it used.** The 2026-08-29 set was cut with our
own drum synthesis and did not say so, which Luke had to ask about. Sets for
judging an effect use ReBirth's samples so our synthesis is out of the
comparison; sets for judging the product use ours. Both, labelled.

**A number is quoted with the condition it was measured under.** "G-Spot scores
0.57 dB" means nothing without "with the compressor, the delay and the
distortion all still open in that row".

**A whole-mix figure is never the headline for a component.** It moved the wrong
way twice today for reasons that had nothing to do with the component being
changed.

---

## Addendum, same day: Luke's ear on the null pair, and what it found

He reported that the rack-off full mix has substantial differences in the bass:
ReBirth's lines have "very little texture, more bass", ours has "some kind of
buzzy texture". He also asked that test pieces never sound two 303s at once,
because he cannot separate them, and noted the melody line differs.

**The 0.12 dB on that pair was deaf to all of it.** Trakka Rekka's two 303s sit
about 16 dB under the drums, so they are 2.5 percent of the energy and a
band-share metric over the whole mix cannot see them. This is the `ab-parity`
rule about widening what is measured, and it happened on the very pair offered
as a calibration.

**Calm's first 50 seconds turns out to be bass 1 alone**: both drum machines
digitally silent, bass 2 30 dB down, rack off on both sides. That is a clean
single-303 comparison needing no new export, and it is now
`listen-303-solo-2026-08-29/`. So the plan's claim that no clean 303 reference
exists was too strong: none exists in `rb338-exports/`, but one was hiding in
`renders/nofxexports/Calm.WAV` and nobody had looked for a window rather than a
file.

What it measures. The notes are right, with partials at 32, 43, 51 and 65 Hz on
both sides. **Our harmonics are 10 to 20 dB weaker than ReBirth's from 86 Hz
up**, and in octave-share terms we are 17 to 25 dB short at 176 and 353 Hz.
Whole-window error is 8.81 dB for Open303 and 5.43 for the native TPT voice.

That reorders the plan. **Batch B, the 303, goes first.** Three of the four
effects sit on a 303 in every shipped song, and an effect cannot be judged
through a source this far out. It also means the 303 supertask, recorded as
complete, is not: the matrix already calls cutoff, resonance, env mod and decay
"partial", and this says how much that costs on a real line.

Two constraints Luke set for batch B, both adopted: **one 303 at a time, never
two**, and the pieces should be musical enough to judge rather than drones.


---

## Status, end of 2026-08-29

**The protocol is in force.** `isolation_check.py` exists and every fixture made
since it does passes it. The rules are in the `ab-parity` skill, the
`component-deep-dive` skill and `RUNBOOK.md` section 6b, so they apply whether or
not anyone reads this file.

**The batch order changed on the first day.** Batch B, the 303, went first rather
than N and P, because Luke's ear on the null pair found that our 303 is the
source under three of the four effects and is substantially wrong. That was the
right call and it is the plan's own logic: an effect cannot be judged through a
source that is not right.

| Batch | State |
|---|---|
| **B, the 303** | B1 (55 fixtures, the filter plane), B2 (25, the envelope) and B3 (11, the accent) all measured. Five defects found, one fixed. `BASS303_FINDINGS.md` |
| **N, the null** | done as `listen-null-2026-08-29/`, on existing ground truth rather than a new fixture |
| **P, the PCF** | not cut. The PCF is measured to 0.23 octaves across all 55 patterns from its own rounds 7 to 10, so this is now a listening batch rather than a measuring one |
| **D, the delay** | not started, supertask 5 |
| **X, the distortion** | not started, supertask 7 |
| **C, the compressor** | blocked: the `COMP` chunk is not parsed, so its settings cannot even be seen |
| **S, stacking** | not started, and correctly last |

**One thing the plan got wrong.** It said flatly that no clean 303 reference
exists. None exists as a FILE; one exists as a WINDOW, in Calm's first fifty
seconds, and nobody had looked. Corrected in section 0 of `BASS303_FINDINGS.md`
and generalised into the `ab-parity` skill: look for a window before concluding
there is no reference.

**One thing it got right and should be kept.** The bed idea, one reference
pattern reused under every effect test so that familiarity does the work. Nothing
has used it yet because batch B is about a voice and not an effect, but it stands
for D, X, C and S.
