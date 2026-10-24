# Reborn RB-338: experimental filter design: coupled nonlinear diode ladder

Clean-room re: ReBirth (NO ReBirth code, ever). Sources for THIS design are published
papers + the 303 circuit topology (published schematic) + standard diode physics.
Project-wide, MIT/free-libre 303 emulators (Open303) may now be read and adapted with
attribution (see `PROVENANCE.md`); this diode design nonetheless keeps its own provenance
to papers + schematic. Parity target stays ReBirth.

## Root cause (why every probe failed)
Our filter is a **buffered** TPT one-pole cascade with a single feedback nonlinearity.
Feedback overdrive, per-stage tanh, and post-filter distortion all failed to turn our
resonant peak into ReBirth's even harmonic comb because the comb is not an add-on. It is
produced by the 303's **coupled** diode ladder: the four stages are NOT buffered from each
other, they share ladder current and load one another, forming nested nonlinear delay-free
loops. At high resonance that coupled nonlinear system intermodulates the note's harmonics
into the dense comb (flatness ~0.16, ~135 peaks). Buffered stages with a per-stage clip
cannot reproduce it, confirmed empirically this session.

## Literature basis (clean-room inputs)
- Huovilainen 2004 (DAFx, open PDF): analyze ladder circuit -> ODE -> Euler -> cascade of
  1-pole IIR sections with EMBEDDED per-stage tanh. This is the transistor (Moog) ladder,
  buffered stages: the structural template, but not the diode coupling.
- D'Angelo & Fontana, "Modeling the EMS VCS3 VCF as a Nonlinear Filter Network" (IEEE TASLP);
  D'Angelo & Valimaki, "Generalized Moog Ladder Filter I/II" (IEEE TASLP 2014): the
  DIODE-based VCF (VCS3, same diode-ladder family as the 303) modeled as a nonlinear filter
  network with nested delay-free loops, solved by fixed-point iteration. This is the correct
  framework for the coupled diode ladder.
- 303 circuit specifics (published schematic + circuit analyses, incl. Stinchcombe):
  input HP before the VCF; VCO drives a differential pair at the bottom of the ladder
  (input nonlinearity ~ Icv/(1+exp(+/-Vin/Vt)), i.e. tanh-family); the diode ladder's OWN
  coupled nonlinearity is the dominant character (per circuit analysis, input-pair
  saturation is the minor effect); a HP in the feedback path; first pole differs from the
  other three (1+3-pole); high resonance loses passband/DC gain (needs makeup).

## Proposed experimental filter
A coupled nonlinear diode-ladder core, 4 stages, NOT buffered:
- Per-stage state; each stage's update depends on its neighbours (ladder coupling) with a
  diode/differential-pair nonlinearity (tanh-family) inside the coupling, the D'Angelo
  nonlinear-network form.
- First-cut discretization: 1-sample delay in the coupling loop + 2-4x oversampling
  (the classic, simplest-to-run method) with cutoff/resonance tuning correction. Upgrade
  path if it tunes/aliases poorly: zero-delay fixed-point (a few iterations or Newton) per
  D'Angelo.
- Keep the 303 topology bits: input HP; feedback HP (corner = fit param); first-pole ratio
  (fit param); resonance-gain makeup.
- Reuse the existing oscillator, envelopes, and the ALREADY-VALIDATED plucky VCA
  (AMP_DECAY 0.06 / SUS 0.08). Swap ONLY the filter core.

## Fittable parameters (to the ReBirth sysid measurements)
cutoff-knob->Hz map; resonance/feedback gain vs knob; diode nonlinearity scale (Vt-like);
inter-stage coupling strength; feedback-HP corner; first-pole ratio; oversampling factor;
makeup curve.

## Build / branch plan
- Experimental branch off main: exp/diode-ladder. New class (e.g. DiodeLadder) behind the
  same filter interface; bass_synth selects TPT vs diode via a compile flag or a runtime
  probe (REBORN_FILTER=diode) so both live in one binary for A/B. Current TPT filter
  untouched; main stays clean; nothing merged without Luke sign-off.
- Reuse the render + measurement harness unchanged.

## Fitting plan
- Target = ReBirth sysid_c{020,045,070,095,120} (resonance shape vs cutoff) + env_r127 /
  env_r30 (resonance vs knob) + detroit / stress (musical context).
- Loss = multi-patch spectral distance combining: resonance-vs-cutoff curve (peak height,
  center vs cutoff, bandwidth), flatness / comb density (the thing we keep missing),
  rolloff + centroid (brightness), and tail articulation.
- Optimize: coordinate-descent / grid first; optional all-in = differentiable-filter method
  (arxiv gray-box / all-pole) to autodiff the loss.
- Success = ReBirth's resonance-vs-cutoff curve + ~0.16 flatness comb + brightness +
  articulation all matched. Then hfQ retires (a broadband comb doesn't self-oscillate as a
  pure tone), which removes the chirp for free.

## Status / next
Waiting on the 5 ReBirth sysid exports. On arrival: (1) extract ReBirth's resonance-vs-cutoff
curve as the fitting target, (2) stand up DiodeLadder on the branch, (3) fit, (4) A/B vs
ReBirth on the full reference set. If it lands, fold in on sign-off; if not, we have a
precise measured target and a clean fallback (lock the brightness+articulation wins already
validated, keep the ladder as scoped R&D).
