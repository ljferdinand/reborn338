# The 303 filter: state, and what to do next

Rewritten 2026-08-02 at the end of the envelope round. This file is the entry
point for the 303 filter and `RUNBOOK.md` section 4 points at it. Two earlier
versions of it were built on findings that have since been retracted, so trust
this one and read `docs/research/FINDINGS_AUDIT.md` under the 2026-08-02 headings for the
evidence behind every number.

> **READ §0 AND §6 FIRST. Everything else in this file is older than they are.**
> §0 is the canonical answer on whether the filter needs a structural change, and
> §6 is the current worklist, assessed 2026-08-05 and agreeing with the task
> list. §1 through §5 are the state at the end of 2026-08-02 with dated
> corrections inline, kept for the reasoning trail; where they conflict with §0
> or §6, those win. §7's traps and §5's tools table still hold as written.
>
> **UPDATE 2026-08-03.** Since 2026-08-02: the "gesture gap" in §3.1 was
> traced to a specific mechanism, **our voice retriggers where ReBirth holds the
> gate on 55 of 110 Clodhopper steps, one-directionally**, which explains the
> gesture deficit, the accent brightness spikes (task 76) and a chunk of the
> broad brightness excess as ONE underlying family rather than three. The tie
> DECISION was checked against the actual audio step by step and is correct;
> the defect is downstream in the voice, in at least two confirmed-separate
> mechanisms (a pitch-glide artefact correlating 0.87 with jump size, and an
> accent-decay shape error that was tested with a real fix attempt and
> REFUTED as a net improvement). Full detail in `docs/research/FINDINGS_AUDIT.md` under
> "the tie decision itself is right" and "the accent is up to 29 dB too
> bright".

---

## 0. Filter status: topology or constants

**This is the canonical answer, and it exists because two different questions
have been answered two different ways.** Read either answer as the verdict on
the whole filter and you will send the work in the wrong direction. Both are
correct, about different axes.

**The far-field skirt SLOPE is a constants problem, not a structural one.** The
direction call on it was withdrawn 2026-07-31 after two retractions, a
band-anchoring bug in `skirt_knob.py` and a half-bin error in `find_f0()`. What
survives is a residual of 1.1 dB per octave at cutoff 110 and 1.8 at cutoff 127,
against ReBirth reading -18.0 to -19.6 dB/oct where an ideal four-pole reads
-20.7. **ReBirth is a four-pole ladder**, and a 1 to 2 dB/oct residual against a
structure now confirmed to be the same structure is a refit. Nothing about the
filter order or the stopband shape should change on that evidence. Full trail in
`OPEN_DECISIONS.md`.

**The resonant-peak PROMINENCE and its damping is a limit of the current
parameterisation, and no constant in the model fixes it.** Measured 2026-08-03 on
batch H with `tools/analyze/harm_response.py`: at resonance 127 our peak stands
3.0 dB short at cutoff 40, 4.8 at cutoff 90 and 6.4 at cutoff 127, while at low
resonance we match or slightly exceed. ReBirth concentrates its energy into a
taller narrower peak and we spread ours out. Every candidate mechanism was
eliminated by number, the 6 kHz ceiling, the HF shelf, the decay time, the drive
shaper, `RESO_OCT` and `hfQ`. Only the `K_B` feedback moves prominence at all,
and it is already at 3.90 against a self-oscillation limit of 4.0. Something must
supply Q without costing corner position and nothing in the model does. See §3.3.

**So the order is right and the DAMPING is wrong.** That is a narrower claim than
the topology question retired above, and it is not the retracted claim that the
filter ORDER was wrong. The candidate structural fix is the coupled diode ladder
in `reborn338_diode_ladder_design.md`, built clean-room from the published
papers and the schematic; `TASK_303_GESTURE_DEPTH.md` carries it as the §3.3 work
with its source list and its constraints.

---

## 1. Where the filter stands

Branch `303-env-domain` and its child `docs/audit-2026-08-04-cleanup`, 30 commits
as of 2026-08-05, **nothing merged to `main`**. Check `git log` rather than
trusting that count; it has been wrong in this file twice.

Spectral shape error against ReBirth over all 373 fixture cells we hold an
export for, measured by `tools/analyze/regress_ab.py`:

| | rms dB |
|---|---|
| start of the round | 3.962 |
| after the env mod swing and corner re-anchoring | 2.536 |
| after the accent attack and depth | **2.325** |

314 cells better, 57 worse. The only fixture set still worse than the start is
batchG at +0.201, concentrated in its decay-0 cells.

Build clean, tests pass, 18 of 18 songs round-trip.

---

## 2. What was found, in one place

**The env mod knob is BIPOLAR and SYMMETRIC.** It swings the corner equally
either side of where the cutoff knob puts it, not upward from a fixed floor.

    log2 fc = log2(centre) + (F + A k)(env - c)
    F = 0.85 oct    A = 4.45 oct/knob    c = 0.5

`c` fits free at 0.495 and forcing it to exactly 0.5 costs 0.0003 octaves. Three
parameters, twenty endpoints, rms 0.106, and held out at two cutoffs and a
resonance it degrades only by a factor of 1.3 to 2.1.

**The accent has an ATTACK.** ReBirth's accent rises to its peak at 52 ms; ours
peaked at 0 ms. `ACC_ATK` 0.010 and `ACC_SWEEP` 0.80 took in-gate trajectory
error from 3.722 dB to 0.924, improving 33 cells and worsening none.

**`CUT_SCALE` 1.05 to 0.78 is bookkeeping, not a refit.** With a symmetric swing
the constant means the envelope's centre rather than its closed corner, which is
2^(-F/2) lower. The arithmetic gives 0.7815 and a 140-cell grid search found 0.78
independently.

---

## 3. What is WRONG right now, ranked

**1. The filter moves too little, and this is the big one.** Counting upward
brightness gestures of 3 dB or more over twelve seconds of Clodhopper's 303-2:
ReBirth 64 gestures at median depth 13.5 dB, total 1007. Ours 48 at 9.9, total
504. Luke hears this as ReBirth "modulating on every beat" against ours
"audible only around step 12", and he hears it on every fixture too.

> **Refined 2026-08-03 by sweeping the threshold, and it changes what to fix.**
> The reading "a quarter of the gestures are missing and the survivors are 70
> percent as deep" makes this sound like two comparable halves. It is not. Our
> share of ReBirth's gesture COUNT collapses as the threshold rises: on 303-2,
> 75 percent at 3 dB but **44 percent at 12 dB**; on 303-1, 51 percent at 3 dB
> but **23 percent at 12 dB**. The rate half is largely an artefact of counting
> shallow wobbles at a low threshold. **The defect is DEPTH: ReBirth makes big
> filter movements and we make small ones**, and the deficit concentrates in
> exactly the gestures loud enough to hear.
>
> The two voices are also not the same problem, which the single-threshold
> number hid. 303-2 is short on both count (75 percent) and depth (73 percent).
> 303-1 is short on count (51 percent) while its median gesture is **deeper than
> ReBirth's** at 108 percent, so there the defect is purely rate, and its total
> movement gap is the larger of the two, 28 percent against 50. Score any
> candidate on BOTH voices and on TOTAL movement, never on count alone.

**2. RETRACTED 2026-08-03. There is no env-mod-0 depth shortfall.** This said
0.54 octaves against ReBirth's 0.96, and it was the instrument.

At env mod 0 the code gives `envDepthOct = EM_FLOOR` exactly and the swing is
symmetric, so our commanded travel is **exactly 0.85 octaves by construction**
and the true onset must be `floor * 2^0.85`. That is a truth we set, and it
calibrates the instrument for free. On the very cell the figure came from, the
true onset is 854 Hz; `env_traj.py` reads 876 (+0.04 oct) and the instrument
behind the 0.54 read 690 (**-0.31 oct**). Both agree on the floor to within a
hertz and differ only at the onset.

Measured properly on all four em000 cells, **our travel is within 0.05 octaves
of ReBirth's** (0.71/0.89/0.81/0.89 against 0.76/0.90/0.83/0.91). `EM_FLOOR`
0.85 delivers its nominal depth and the fitted F was right.

What is really on those cells is a near-pure OFFSET, ours low by 0.10 to 0.18
octaves at BOTH endpoints, which is the signature of a static corner error.
**That is item 3 below, and these were one defect counted twice.**

The plan's own advice to "apply the travel-gain calibration" was sound but
misaimed: swept properly, the gain is 0.55 below 0.6 octaves of travel and 0.93
to 1.05 from 0.85 up, so the operating point that matters was already in the
good region. Do not use `env_traj` travel below about 0.7 octaves.

**3. ANSWERED 2026-08-03. `CUT_SCALE` 0.78 is compensating for a resonant peak
that is 5 to 6 dB TOO SHORT, and no constant in the model fixes everything.**

Our peak stands too low above the passband, worsening with both resonance and
cutoff: at resonance 127 it is 3.0 dB short at cutoff 40, 4.8 at cutoff 90 and
**6.4 at cutoff 127**. At low resonance we match. ReBirth concentrates its energy
into a taller narrower peak and we spread ours out, so with the corner in the
right place our excess off-peak energy has nowhere to hide, and the aggregate
prefers dragging the corner down until our broad hump slides back over ReBirth's.

The corner itself wants **0.86**, agreed by two independent instruments: batch H
peak-frequency error 0.1653 at 0.78 against 0.1047 at 0.86, and on the tied
envfloor cells the mean absolute corner offset falls 0.226 to 0.056 octaves.

**Every candidate mechanism was tested. Only the feedback moves prominence.**

| | result |
|---|---|
| the 6 kHz ceiling | refuted earlier, byte-identical at 9/12/16 kHz |
| the HF shelf | **real, +4 to +6 dB above 6 kHz, untouched by every filter constant.** But NOT the cause: excluding it down to a 4 kHz band limit does not flip the verdict |
| the decay time | refuted, `ENV_DEC_MUL` 0.60 recovers a sixth of the error |
| the drive shaper | refuted, and lowering it is strongly harmful (rms 4.54 to 11.06) |
| `RESO_OCT` | refuted, worsens the corner in either direction from 0.25 |
| `hfQ` | refuted, disabling it moves prominence 3.74 to 3.57 and shape not at all |
| `K_B` feedback | **the mechanism**, 3.40 to 3.90 halves prominence error to 1.87 |

But no setting is best on more than one instrument, and `K_B` is already at 3.90
against a self-oscillation limit of 4.0. **This is a limit of the
parameterisation, not of the fit**: something must supply Q without costing
corner position, and no constant in the current model does that. The order is
right (ReBirth is a four-pole ladder, that stands) and the DAMPING is wrong.

Probably also the subjective gap. A peak 5 to 6 dB short at high resonance is
exactly the signature of the missing "snap" and onset "chirp", since the squelch
of a 303 is that peak. **Wants an ear check before being treated as settled.**

**4. The accent knob law is the wrong shape.** ReBirth reaches 41 percent of its
brightness travel by knob 16; we reach 8. The level half needs no such curve.

**5. Two confirmed sequencer probes are unapplied. CLOSED 2026-08-03: neither
should be switched on.** Re-scored individually and as a pair on both voices.
**Total movement is worse under every probe on both voices**, and the pair is the
worst or near-worst option on each. `REST_ACCENT`'s four extra gestures on 303-2
are shallow enough that the total falls 504 to 499, and on 303-1 it REMOVES
three. Above a 9 dB threshold it is identical to shipped in count and median, so
it does not touch a single deep gesture. `SLIDE_NEEDS_VOICE` costs 9 percent of
total movement on 303-2 and 29 percent on 303-1.

They cannot help by construction: they change WHICH steps retrigger, and the
deficit is in HOW FAR the filter travels when it does. Both stay default-off.

---

## 4. Every probe, and whether it ships

| probe | default | status |
|---|---|---|
| `REBORN_EM_CENTRE` | 0.5 | **SHIPS.** -1 restores the pre-2026-08-02 anchoring |
| `REBORN_EM_FLOOR` | 0.85 | **SHIPS.** F in the law |
| `REBORN_EM_OCT` | 4.45 | **SHIPS.** A in the law |
| `REBORN_CUT_SCALE` | 0.78 | **SHIPS, FLAGGED.** Suspected compensation, §3.3 |
| `REBORN_ACC_ATK` | 0.010 | **SHIPS** |
| `REBORN_ACC_SWEEP` | 0.80 | **SHIPS** |
| `REBORN_ENV_DOMAIN` | oct | off. Keep: it is what calibrates `env_traj.py` |
| `REBORN_DEC_A/_B/_C` | 0.030 / 1.87 / 0.0 | off. A quadratic decay law was fitted and REJECTED |
| `REBORN_FC_MAX` | 6000 | off. The ceiling was tested as the §3.3 culprit and refuted |
| `REBORN_SLIDE_NEEDS_VOICE` | off | confirmed, unapplied, and it makes §3.1 worse |
| `REBORN_REST_ACCENT` | off | confirmed, unapplied, helps §3.1 a little |

Every "off" default is verified byte-identical over the 373-fixture inventory or
over 32 demo-song renders.

---

## 5. Tools built this round

| | |
|---|---|
| `tools/analyze/env_traj.py` | the corner trajectory, by resonant-peak tracking. **Calibrated**: 0 crossed verdicts on 48 known-truth renders. Its travel gain is 0.78 to 0.86 and it never over-reads |
| `tools/analyze/regress_ab.py` | scores a change across every fixture set with a ReBirth export, caches renders on the override string, `--detail N` shows worst cells |
| `tools/render/byte_neutral.py` | the "this probe changes nothing" claim, checked the same way every time |
| `tools/render/stage_trio.py` | level-matched listening trios. The match band is a required argument |
| `tools/analyze/gesture_rate.py` | how often and how far the filter opens, per voice, counted and summed. Added 2026-08-03 and used by §6; count and depth are reported separately because they are separate defects that multiply. It is a BAND RATIO, so a corner sitting above the band under-reports and it cannot choose between two candidate corners |
| `tools/rbsgen/gen_envcal.py` | known-domain calibration set, ours only, no ReBirth export needed |
| `tools/rbsgen/gen_envfloor.py` | the tied grid at 55 Hz, so the envelope floor stays inside the analysis band |

---

## 6. The worklist, in three tracks

**UPDATED 2026-08-06. Track A is CLEARED and Track B's diode fork is SHELVED; the
reasoning below is the 2026-08-05 assessment, kept for the trail, but the state has
moved past it. Track A tasks 53, 77, 78, 76 are all resolved: the DC blocker does
not confound the count (53), the glide-jump excess is the static reso-127 filter
not the glide (77), the accent-latch leak is fixed and shipped as decayonly (78),
and task 76's "18 dB" was the accent corner push, refit and shipped as the accent
reso law (72, ACC_SWEEP 3.30 / ACC_RESO_SLOPE -0.708). Task 82 retired as an
alignment artefact. Task 83 (accent-rest resonance, from Luke's ear) traced to the
reso-127 filter SUSTAIN, not the accent. Track B stage 0 (task 80's prominence ear
check) SHELVED the diode-ladder fork: Luke's ear plus measurement at reso 120 and
127 show the prominence gap is a conservative K_B (tuning), not a structural comb.
THE NEXT 303-PARITY WORK IS TASK 85: refit K_B as a reso-and-cutoff-dependent curve
(a flat bump to 3.60 is net-better on regress_ab but regresses batchI/batchQ/
accentdepth). The diode core stays behind its probe as a fallback; see
`PLAN_303_FILTER_TRACKB.md`. Provenance (task 84 RESOLVED 2026-08-08): the =rb probe's
Open303-seeded b0 constants are now permitted with MIT attribution (added in code); no
purge refit is needed. Trail in FINDINGS_AUDIT.md 2026-08-06 and the 2026-08-08 policy
change (see `PROVENANCE.md`).**

**Assessed 2026-08-05. This section IS the 303 worklist and it agrees with the
task list, where the same ordering is encoded as `blockedBy` edges so a
contaminated task shows as blocked rather than relying on anyone remembering
why.** Task numbers below are that list. If the two ever disagree, the task list
is authoritative for state and this section for reasoning.

**The order is forced by measurement contamination, not by importance.** Three
defects corrupt the measurement of others, so they go first regardless of how
interesting they are. That is the lesson of the envelope round restated: the
filter constants were fitted through a wrong envelope, and the envelope was
fitted through wrong constants, and untangling that cost a week.

### Track A: the rhythm defect, which is what Luke actually hears

`53` -> `77` and `78` -> `76` -> `72`, with `44` and `80` hanging off `76`.

**Task 53 leads, and not because the DC blocker is the biggest problem.** It
leads because ONE render tells us whether it is inflating the floor that
`onset_ab.py` divides by. Task 53 says our notes do not STOP: a tail at -26 to
-50 dB through a step where ReBirth is at digital zero, tau 34.6 ms against
ReBirth's 14.5. The 2026-08-03 finding says we STOP AND RESTART on 55 of 110
Clodhopper steps where ReBirth holds. `onset_ab.py` measures the peak after a
step boundary over the QUIETEST point before it, so a ringing tail raises the
denominator. Those two readings have to be reconciled before either is built on,
and if the 55 count moves when the blocker moves then tasks 77 and 78 are
chasing an artefact.

Then the two characterised boundary shapes, `77` (a pitch-glide artefact
correlating 0.87 with jump size) and `78` (the accent latch, whose naive fix was
tested and refuted because clearing a continuously-decaying quantity with a
binary toggle moves the error to the next boundary). Seven of the nine shapes
remain uncharacterised.

Then `76`, which should collapse from an investigation into a verification: its
"unexplained 18 dB" was attributed to the retrigger defect landing on accented
steps, and attribution is not measurement.

`72` is LAST in this track on purpose. Its band-ratio measurement of the accent
knob law is corrupted by spurious retriggers landing on accented steps, and
accent is 96 on both Clodhopper voices.

### Track B: the static filter, which runs in parallel

`58` and `80` -> the diode-ladder fork -> `59`.

**Not contaminated by Track A**, because it lives on tied and static fixtures
rather than musical patterns. It can proceed alongside.

`58` is rewritten. Its slope error does not reproduce: ReBirth reads 2.832 to
2.988 at resonance 0 and brackets our 2.900. What is there is a flat 0.1 octave
offset matching the peaked-cell finding that wants `CUT_SCALE` 0.86. It becomes
ReBirth's resonance-0 corner as a target, which is the region every instrument
behind §3.3 is blind to by construction.

`80` is the ear check §3.3 asks for and never got. It needs a patch WITHOUT a
dominant accent, because Clodhopper has one event 29 dB out and a 5 to 6 dB
prominence difference is not audible next to that.

`59` is mostly moot. The literal joint refit never ran, but each leg is answered
by something better than a fit, and the prominence conclusion is a boundedness
argument that no joint fit escapes. One cell is genuinely untested:
`RESO_OCT_CUT` raised rather than zeroed. Do not run it before the fork is
decided, since none of those constants survives a core change.

### Track C: independent, available in any gap

`73` the decay-to-tau table, which any decay refit needs first and which is
uncontaminated because it is measured on tied fixtures. `65` the low-end
interior maximum at resonance 0, where the standing instruction is not to fit a
bump. `64` MIDI knob capture, which is higher leverage than its number suggests:
it is the only pending task that would make fixture capture cheaper for EVERY
remaining supertask rather than one.

`70` is likely moot and is parked behind `81`.

### What is still unknown

`81` carries five investigation clusters lost to a usage limit: the DC blocker
against the retrigger defect, the accent residual, the EM_FLOOR statistics, the
full contamination graph, and the cross-component question. The one cluster that
did return produced the `CUT_SLOPE` correction and the `58` rewrite, so the
other five are worth re-running.

### Two things that read as blocked and are not

Corrected in `COMPONENT_MATRIX.md` on 2026-08-05 and repeated here because they
gate nothing and said they gated everything. Item 1's noise floor was measured
twice on `renders/batchH_floor/`, pooled median 0.114 dB rms against a config
scoring 4.745. Item 2's ear check ran on 2026-07-31 and drive ON won all four
cases.

---

## 7. Traps, each of which cost something real

**An instrument that returns a verdict must be calibrated against known truth.**
The retracted frequency-domain finding came from a tool never once asked to
identify a trajectory whose domain was known.

**A fit that succeeds on its own cells has told you nothing yet.** `EM_FLOOR` 0.9
with `EM_OCT` 4.8 scored 0.071 octaves on its data and 0.542 held out.

**Measure the endpoints, not the span between them.** The bipolar law was
invisible for a week because everything was reported as travel.

**A band ratio saturates when the corner leaves the band.** This bit twice in one
day: it makes the accent knob law unfittable on brightness, and it is why the
Clodhopper gesture table cannot choose between the two candidate corners.

**An operating point chosen to make an effect large can break the measurement.**
Driving env mod up to match ReBirth's travel pinned our corner against its
ceiling, and the tool read the clamp as a finding.

**Name the window.** The gate closes at half a step.

**Luke's ear is an instrument and should be used as one.** He named a corner
interval as three semitones against a measured 3.39, and a settle time as "about
1.5 seconds" against a measured 1.48. Ask for intervals and durations, not
verdicts.

---

## 8. Threads open, off the critical path

| | |
|---|---|
| **task 65** | the resonance-0 low-end gap has an interior maximum near 110 Hz. No highpass fits. Do not fit a bump |
| **task 53** | the DC blocker's numbers went stale when the highpass shipped |
| **the delay drops PAN** | decoded at `DELY+4`, discarded, off centre on ten of sixteen shipped songs |
| **compressor and distortion** | ReBirth's compressor has two controls, ours exposes five. Its distortion control is SHAPE |
| **task 58** | **CLOSED 2026-08-05, it was never a discrepancy.** Our cutoff slope is not a constant: `RESO_OCT_CUT` adds slope with resonance, so the effective slope is `CUT_SLOPE + RESO_OCT_CUT * reso`, which at resonance 60 is 2.90 + 0.35 * (60/127) = 3.065. ReBirth's 3.09 was measured on the envfloor grid, which is resonance 60 throughout, so the two agree to 0.025 octaves per knob. The old line compared a resonance-60 measurement against a resonance-0 constant, and that is also why setting `CUT_SLOPE` to 3.09 scored worse at every scale: it would put the effective slope at 3.255 |
| **the round-trip count** | was reported as 17 for months over a set of 18. Fixed; it now counts skips and fails on them |
| **the level-matched drive rerun** | moved here 2026-08-04 from the retired `DECISIONS_PENDING.md`. The drive-shaper ear check ran 2026-07-31 and drive ON won all four cases, so `satAsym()` stays in. A later Clodhopper-only test had drive LOW sounding texturally closer but so much quieter that it could not be judged, and that reading is confounded by level. The clean follow-up is the same A/B level-matched with `tools/render/stage_trio.py`. The measurement case for removal is separately void, having rested on the harmonic instrument found to be miscounting on 2026-07-31, so both sides need regenerating before it is reopened. Full trail in `OPEN_DECISIONS.md` |
