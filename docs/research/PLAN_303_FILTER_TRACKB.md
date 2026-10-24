# Track B: the 303 diode-ladder filter core, execution plan

> STAGE 0 RESOLVED 2026-08-06: the diode fork is SHELVED. Luke's ear plus
> measurement at reso 120 AND 127 show ReBirth's prominence is a conservative
> K_B (tuning), not a structural comb. The work is now a RESO-and-cutoff-
> dependent K_B refit (a flat bump to 3.60 is net better but regresses batchI/
> batchQ/accentdepth), NOT the filter-core rewrite below. The stages below are
> retained as the plan we would follow IF the K_B refit cannot reach the
> extreme. See FINDINGS_AUDIT.md 2026-08-06.

Drawn up 2026-08-06. This is the HOW and the gates. The technical design is
`reborn338_diode_ladder_design.md` and is unchanged by this. Nothing here merges
without Luke's sign-off, and stage 0 is a cheap gate that can stop the whole
thing before any fitting starts.

## Why this is the next 303 work

Three independent findings from the 2026-08 accent round all terminate at the
same place, the resonance behaviour at high Q that the shipped TPT ladder cannot
reach:

- Task 76: the accent brightness at reso 127 is the filter response, not the
  accent. Once the accent was correct, the reso-127 excess stayed.
- Task 77: the glide-jump excess is the static reso-127 filter, not the glide.
  Our glide DSP matches ReBirth to 0.16 dB.
- Task 83: "ReBirth resonates on each beat" is the reso-127 self-oscillation
  SUSTAIN, the filter ringing between notes, which our ladder does not hold. No
  accent change reproduces it.

And the design doc's root cause: ReBirth's dense even-harmonic comb (flatness
~0.16, ~135 peaks) is produced by the COUPLED diode ladder loading its own
stages. A buffered TPT cascade with a per-stage clip structurally cannot make it,
which was confirmed empirically across a round of feedback-overdrive and
post-filter-distortion probes.

## Where we already are, and why this is a fit, not a build

The coupled core EXISTS. `bass_synth.cpp` carries three filter cores in one
binary, selected by `REBORN_FILTER`:

- default (TPT): the shipped 4-pole zero-delay ladder with saturating feedback,
  oversampled. This is what parity rests on today and what stays if the fork
  fails.
- `=rb`: a coupled forward-Euler "ReBirth-era" ladder, one clipper and a feedback
  HP, from the 2026-07-23 design memo.
- `=diode`: a COUPLED NONLINEAR diode ladder. Backward-Euler fixed-point solve per
  oversampled substep (unconditionally stable, so the corner can open without the
  forward-Euler blow-up), a differential-pair tanh in every coupling, the first
  pole different from the other three (the 303's 1+3 topology), and a feedback HP.
  About seven fittable parameters (D_VT, D_G, D_K, D_OS, D_ITER, D_P1, D_FBHP)
  plus the corner and resonance maps.

So Track B is a FIT-AND-VALIDATE effort on an existing, compiling, stable core,
not a from-scratch build. That is the large de-risk. The `=diode` core has never
been fit to ReBirth's targets; it runs, and that is all we know.

## The targets: "done" means all of these, measured against ReBirth, not the hardware

1. Resonance-vs-cutoff curve: peak height, centre-vs-cutoff, bandwidth across the
   cutoff sweep.
2. Resonant prominence at high reso and cutoff: currently 5 to 6 dB short, and the
   TPT's only lever (K_B) is capped at 3.90 against 4.0 self-oscillation (tasks
   80, 74). A structural change is the only thing that can add prominence the
   TPT cannot.
3. The reso-0 corner: about 0.1 octaves low, flat by cutoff knob (task 58).
4. The harmonic comb: flatness ~0.16, the dense even comb the TPT cannot make.
5. The reso-127 SUSTAIN: the resonance ringing between notes (task 83), which is
   what Luke heard as "resonates on each beat".
6. Brightness (rolloff, centroid) and tail articulation, which the TPT already
   gets close and which must not regress.

## Stages

**Stage 0, the cheap gate, do first.** Run task 80's prominence ear check on a
patch WITHOUT a dominant accent. The accent defect that blocked it before is now
fixed (task 72 shipped), so it can finally run. If Luke cannot hear the 5 to 6 dB
prominence gap, the structural fork drops in priority and stages 1 to 5 wait
behind cheaper 303 work. In the same pass, inventory which ReBirth exports we
already hold for the fitting target and which the fit still needs, so stage 1 is
not blocked on an export batch discovered late.

**Stage 1, the target and a calibrated harness.** Export ReBirth's
resonance-vs-cutoff sweep and the reso-vs-knob patches if not already held, and
extract the six target curves above. Stand up a filter-only scoring tool, modelled
on `tools/measure/score303.py`, that reads peak height, centre, bandwidth, comb
flatness, corner and centroid, and calibrate it against a known result before any
fit so a number is believable. A fit against an uncalibrated instrument is what
this project has retired findings over.

**Stage 2, fit the core one axis at a time.** Coordinate descent, never a joint
fit, per the ab-parity discipline. Order: the corner and knob-to-Hz map first,
then the diode scale (D_VT) and coupling (D_G, D_K) for the resonance curve and
the comb, then the first-pole ratio and feedback HP for the shape, then
oversampling for aliasing and tuning. Reuse the validated oscillator, envelopes
and plucky VCA unchanged; swap ONLY the filter core.

**Stage 3, A/B against both ReBirth and the current TPT.** Score the fitted core
across the full 373-cell fixture set (regress_ab), the reso-127 patches, and
Clodhopper. The gate: it must reach the comb, the sustain and the prominence the
TPT cannot, WITHOUT regressing the static match and everything already fit
downstream. A core that wins the comb but loses the static match is not a win.

**Stage 4, ear check.** Clodhopper and a prominence patch, ReBirth against the
diode core against the current TPT. Luke's verdict, the same gate that shipped
tasks 71, 72 and 78.

**Stage 5, ship or shelve.** If it lands, flip the `REBORN_FILTER` default to the
diode core on sign-off, then re-verify the full 303 fixture set and the 18
demo-song round-trip. If it does not, lock the TPT and the wins already
validated, keep the diode core as scoped R&D behind its probe, and we still hold
a precise measured target for a later attempt.

## The real risks

- **Downstream refit is the largest hidden cost.** The envelope, corner and
  accent constants (EM_FLOOR, EM_OCT, EM_CENTRE, CUT_SCALE, ACC_SWEEP and the
  task-72 reso law) were all fit against the TPT's behaviour. Swapping the filter
  core changes what they see, so a chunk of that work will need re-fitting against
  the new core. This is precisely why the filter fork was deferred behind the
  accent round rather than done first: the accent and envelope laws are cheaper to
  fit once the core beneath them is settled, but if the core changes they move.
- **CPU.** Backward-Euler fixed-point times oversample times iterations per sample
  is expensive. Set a cost budget (a maximum D_OS times D_ITER) and measure it
  against the TPT before fitting to it, so the fit does not converge on a core too
  heavy to ship.
- **Fit convergence.** ReBirth is not the hardware 303 and not Open303. The diode
  STRUCTURE is the right family, but the constants are ReBirth's and must be fit
  from ReBirth exports only. The fit may not reach ReBirth; stage 3 is where that
  is decided, not assumed.
- **Provenance.** MIT/free-libre 303 emulators (Open303) are now permitted with
  attribution (see `PROVENANCE.md`), which RESOLVES the provenance half of task
  84: the `=rb` path's "Open303 form" constants are fine as long as they carry an
  MIT attribution (added). The diode design itself still sources only papers +
  schematic. Attribute any third-party constant under its licence.

## Effort

Supertask-sized. Stage 0 is hours. Stage 1 is a few days including any export
batch. Stage 2 is the bulk, one to two weeks of iterative fitting. Stages 3 to 5
are days. The downstream refit is real and cannot be budgeted until stage 3 shows
what moved.

## The one cheap thing to do before committing to any of it

Stage 0, the prominence ear check (task 80). It is the gate that could change the
whole ranking, it is already scoped in `stage_trio.py`, and it needs only a patch
choice and an interval from Luke rather than a verdict up front. Run it first, and
let the result decide whether stages 1 to 5 start now or wait.
