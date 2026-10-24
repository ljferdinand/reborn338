# Task brief: 303 filter gesture-depth (branch `303-env-domain`)

**For:** a coding agent (Claude Code) operating inside the `reborn338` repo on
Luke's machine.
**Why this file exists:** carry the 303 plan's next steps forward *in the repo*
so the state lives in the code and the docs, not in Luke's head. Do the work,
keep the running record current, and surface decisions as small concrete
questions. Do not require Luke to recall prior context.
**How to run it:** carry out Section 4 (the first concrete step) and then STOP
and report. Do not run all of Section 5 autonomously; each later step depends on
what the step before it found.

---

## 0. Read first, in this order

The detail lives in the repo. This brief orients and constrains; it does not
replace the docs.

1. `CLAUDE.md` and `AGENTS.md` at the repo root (the project's own rules).
2. `docs/research/PLAN_303_ENVELOPE.md` is the entry point for the 303 filter. Read the
   dated update notices at the top and trust the newest headings: two earlier
   versions were built on findings since retracted.
3. `docs/research/FINDINGS_AUDIT.md` under the 2026-08-02 and 2026-08-03 headings for the
   evidence behind every number the plan cites.
4. `docs/PARITY.md`, `RUNBOOK.md` (its section 4 points at the plan), and
   `docs/REBIRTH_AUTOMATION.md` before running any export.
5. `docs/research/reborn338_diode_ladder_design.md` before touching Section 5's §3.3
   work. If any path here has moved, locate it under `docs/` or `tools/` rather
   than guessing.

---

## 1. Mission

Close the gesture-depth gap on Clodhopper's two 303 voices (plan §3.1, §6 step
1). It is the largest remaining perceptual error and the one Luke actually
hears: ReBirth "modulates on every beat" where ours is "audible only around step
12." The defect is DEPTH, not rate. ReBirth makes big filter movements and ours
makes small ones, and the deficit concentrates in exactly the gestures loud
enough to hear (ReBirth keeps 48 of 64 gestures above 12 dB on 303-2; ours keeps
21 of 48). The two voices are not the same problem: 303-2 is short on both count
and depth, 303-1 is short on rate while its median gesture is already deeper than
ReBirth's. Score every candidate on BOTH voices and on TOTAL movement, never on
count at a single threshold.

The sequencer half of this is CLOSED and was a dead end. `REST_ACCENT` and
`SLIDE_NEEDS_VOICE` both make total movement worse on both voices and neither
touches a gesture above 9 dB. Do not reopen them. They stay default-off.

---

## 2. Non-negotiable constraints

- **Clean-room, about ReBirth.** No decompiling or transcribing ReBirth, at any
  point. Reading and adapting MIT/free-libre 303 emulators (Open303) is now
  permitted under their licenses, with attribution (see `PROVENANCE.md`). For the
  ReBirth-PARITY ladder/damping work in Section 5 the legitimate inputs remain
  published DSP literature, the 303 schematic, and black-box ReBirth exports; the
  citeable sources are Huovilainen 2004 and the D'Angelo & Fontana and
  D'Angelo & Valimaki ladder papers. Open303 emulates the hardware 303, not
  ReBirth, so it is a synthesis input, never a parity input.
- **Nothing merges to `main` without Luke's explicit sign-off.** Work on
  `303-env-domain` or a child branch. The branch holds 22 commits as of
  2026-08-05, none merged, build clean, tests passing, 18 of 18 songs
  round-tripping. Keep it that way.
- **Score against real ReBirth exports, never internal metrics alone.** Use
  `tools/analyze/regress_ab.py` across the full fixture inventory (it caches
  renders on the override string; `--detail N` shows the worst cells). The
  current whole-inventory error is rms 2.325 dB over 373 cells.
- **A default-off probe must stay byte-identical to shipped** over the
  373-fixture inventory and the 32 demo-song renders. Verify with
  `tools/render/byte_neutral.py`. A NEW probe ships default-off unless it
  improves TOTAL movement on BOTH 303 voices.
- **Measure stereo.** Mono-summing has hidden real defects on this project (it
  hid the pan law). Do not collapse to mono for analysis unless the metric
  explicitly requires it.
- **ReBirth exports are only clean with the master compressor verified OFF.**
  Exports are automatable, unattended, via `tools/rbdrive/rbdrive.py`
  (Windows-only, roughly 15 s per cell). Read `docs/REBIRTH_AUTOMATION.md`
  first. Do not ask Luke to hand-export.
- **Demo songs are end-stage-only comparison,** not a fitting target.

---

## 3. Traps to avoid (plan §7, each cost real time)

- An instrument that returns a verdict must be calibrated against known truth
  before it is trusted. The retracted frequency-domain finding came from a tool
  never once asked to identify a trajectory whose domain was known.
- A fit that succeeds on its own cells has told you nothing. Hold out at other
  cutoffs and resonances. `EM_FLOOR` 0.9 scored 0.071 octaves on its own data
  and 0.542 held out.
- Measure the endpoints, not the span between them. The bipolar env-mod law was
  invisible for a week because everything was reported as travel.
- A band ratio saturates when the corner leaves the band. It cannot choose
  between two candidate corners and it makes the accent knob law unfittable on
  brightness.
- Do not drive an operating point to make an effect large. Pushing env-mod up to
  match ReBirth's travel pinned the corner against its ceiling and the tool read
  the clamp as a finding.
- Name the analysis window. The 303 gate closes at half a step.
- Luke's ear is an instrument. Ask for intervals and durations ("how many
  semitones," "how many seconds"), not verdicts. He named a corner interval as
  three semitones against a measured 3.39 and a settle time as about 1.5 s
  against a measured 1.48.

---

## 4. First concrete step: map the env-mod-travel gap

This is the one measurement the plan says nobody has run. On the tied envfloor
grid (55 Hz, generated by `tools/rbsgen/gen_envfloor.py`) ReBirth reads
0.90 / 1.88 / 2.81 octaves of travel at env-mod 0 / 32 / 64 and ours reads
0.89 / 1.69 / 2.90. The gap opens at em32 and closes again by em64, and no single
constant explains that shape. It was only ever sampled at three points.

Do this:

1. Extend `gen_envfloor.py` (or add a sibling generator) to a finer env-mod
   sweep on the same 55 Hz tied grid: env-mod at 0, 16, 32, 48, 64, 80, 96, 112,
   127, at two resonances (one low near knob 40 and one high near knob 110),
   with the cutoff held at the envfloor anchor. That is eighteen cells. Keep the
   grid otherwise identical to `gen_envfloor.py` so it stays inside the analysis
   band and remains comparable to the existing three-point data.
2. Generate the `.rbs`, render the Reborn side with `reborn_render` (stereo,
   master compressor confirmed off), and export the ReBirth side through
   `rbdrive` (eighteen cells, unattended).
3. Read the corner trajectory at each env-mod with `tools/analyze/env_traj.py`.
   It is calibrated (0 crossed verdicts on 48 known-truth renders) but do not
   trust its travel below about 0.7 octaves.
4. Report the SHAPE of the gap: where it opens, where it closes, and its
   magnitude at each em, with the numbers. Do not convert it to a verdict yet.

Then stop and report before proposing a mechanism.

---

## 5. After that, in order (plan §6). Do not start these unprompted.

- **Settle the depth question the sweep exposes,** then re-run
  `tools/analyze/gesture_rate.py` on BOTH 303 voices, on TOTAL movement,
  sweeping the threshold, and re-count the deep (>=12 dB) gestures. A count at a
  single threshold cannot separate a shallow wobble added from a real movement
  deepened. `gesture_rate.py` exists as of 2026-08-03 and reports count and
  depth separately, because they are separate defects that multiply; its
  docstring carries the warning that it is a band ratio, so a corner sitting
  above the band under-reports.
- **Identify what `REBORN_CUT_SCALE` 0.78 cancels (§3.3).** The evidence points
  at the resonant peak standing 5 to 6 dB too short at high resonance and
  cutoff, worst at cutoff 127. Of every candidate tested, only the `K_B`
  feedback moved prominence, and it is already at 3.90 against a
  self-oscillation limit of 4.0. That is a limit of the parameterisation, not of
  the fit: something must supply Q without moving the corner, and no constant in
  the current model does. The corner itself wants 0.86, agreed by two
  independent instruments. This is where `docs/research/reborn338_diode_ladder_design.md`
  comes in: the coupled diode-ladder redesign is the candidate structural fix,
  built clean-room from the cited papers and the schematic. Treat any change to
  peak height as needing an ear check before it is called settled.
- **The accent knob curvature (§3.4),** fitted on the corner and not on
  brightness, because the band ratio saturates. ReBirth reaches 41 percent of
  its brightness travel by knob 16; ours reaches 8.
- **Only then, the static filter constants on batch H (§5 last).**

---

## 6. Interaction protocol

Luke sets direction and picks the architectural forks; he has limited executive
bandwidth to hold granular state, so the agent holds it.

- **Keep the record in the repo, not in Luke's head.** As findings land, append
  them to `docs/research/FINDINGS_AUDIT.md` under a dated heading and update
  `docs/research/PLAN_303_ENVELOPE.md` sections 1, 3 and 6 when a finding changes the
  picture. Follow the existing house style so the next session starts from the
  repo alone.
- **Report in short, self-contained updates:** what was measured, the numbers,
  what they mean, and the single next decision. Never assume Luke remembers the
  last exchange.
- **When you need his judgment, give him an instrument, not a quiz.** Ask for an
  interval, a duration, or an A/B listen on a level-matched pair
  (`tools/render/stage_trio.py`; the match band is a required argument). Never
  ask him whether a render "sounds right."
- **Do not merge to `main`.** Do not switch a default-off probe on unless it
  improves total movement on both voices. Surface any consequential change as a
  concrete proposal and wait for his sign-off. The pattern is: propose the
  payload, wait for his OK, then act.
- **House style for anything you write:** no em-dashes, no filler words, prose
  over bullets, and every claim carries the fixture and the tool that produced
  it.

---

## 7. Definition of done for this pass

- The env-mod-travel gap characterised with data across the finer sweep, not
  inferred from three points.
- `gesture_rate.py` re-run on both voices with the threshold swept, and a clear
  statement of whether whatever fix the sweep motivates closed the deep-gesture
  deficit on both voices.
- Any candidate mechanism validated held-out, at cutoffs and resonances it was
  not fitted on, and scored on total movement on both voices before it is
  proposed as a change.
- `docs/research/FINDINGS_AUDIT.md` and `docs/research/PLAN_303_ENVELOPE.md` updated so the next
  session can start from the repo with no outside context.
- Nothing merged. Work left on `303-env-domain` or a child branch, clean and
  ready for Luke's review.
