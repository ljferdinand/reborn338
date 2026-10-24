> **SUPERSEDED 2026-07-27.** A point-in-time session handoff, kept for its
> reasoning trail. For current state start at `docs/STATUS.md` and
> `docs/COMPONENT_MATRIX.md`; for the 303 filter specifically, at
> `docs/research/PLAN_303_ENVELOPE.md`. Anything here about process, environment or
> "next steps" is out of date; the technical findings were folded into
> `docs/research/FINDINGS_AUDIT.md`, `docs/PARITY.md` and the per-component worklists
> where they survived.

# Reborn RB-338: Session Handoff (2026-07-22 → 07-23)

Cousin: CP7.22.2026a. Clean-room, personal project. Detailed running log: personal-root
note bf07a54a (v11). Filter design doc: reborn338_diode_ladder_design.md.

## North star (do not drift)
Parity target is **ReBirth RB-338** (Propellerhead), judged by black-box measurement of
real ReBirth WAV exports. NOT the hardware 303, NOT Open303. **Clean-room stays**: no
Open303 source, no decompiling ReBirth. Legitimate inputs are published DSP papers, the
303 schematic, and behavioral measurement of ReBirth. Luke affirmed this explicitly.

> **POLICY UPDATE 2026-08-08 (this "no Open303 source" line is now partly
> superseded).** Luke changed the policy: MIT/free-libre 303 emulators (Open303)
> MAY now be read and adapted, with attribution. Only ReBirth stays clean-room
> (no decompiling ReBirth; parity still judged against ReBirth's output). See
> `docs/PROVENANCE.md`.

## Validated win (pending Luke's ear-check + sign-off; NOTHING committed)
**Plucky VCA**: REBORN_AMP_DECAY=0.06, REBORN_AMP_SUS=0.08, plus a per-note re-pluck to
1.0 on struck (non-slid) notes. Fixes the articulation gap (our line ran as a sustained
wash; ReBirth plucks with gaps between notes). Amp-envelope only, filter-independent, fit
and measured against ReBirth's detroit export (envelope CV 0.83 vs 0.79, silence-fraction
0.32 vs 0.33). Render: detroit_pluck.wav.
CAVEAT: matches only the amplitude contour. ReBirth's pluck is ALSO timbral (its filter
sweeps bright->dark per note), which we don't reproduce, so it will not sound identical.
A/B detroit_baseline.wav vs detroit_pluck.wav (same tone) to hear the VCA's actual effect.
Exact decay shape / hit phase are approximate; DECAY-knob coupling unverified (one patch).

## The diagnosis (what separates us from ReBirth)
1. **Cutoff map too high.** ReBirth's resonance sits below our baseCut and compresses at the
   top. ReBirth emphasis 519 / 899 / 1279 / 1927 / 2584 Hz at cutoff 20/45/70/95/120 vs our
   baseCut 786 / 1216 / 1881 / 2911 / 4505.
2. **Resonance Q too high.** ReBirth's resonance is BROADER (HF flatness 0.224/0.063/0.030/
   0.017/0.006 vs our TPT 0.078/0.009/0.002/0.001/0.000), broadest at low cutoff, tightening
   to a peak near max cutoff.
3. **Root cause (structural).** ReBirth is a COUPLED diode ladder (4 stages share current,
   nested nonlinear loops); at high resonance it sprays a broad harmonic comb. Our buffered
   TPT + single feedback nonlinearity makes a narrow peak. This is why NO bolt-on
   nonlinearity closed the gap.

## Ruled out this session (all default-off probes, left in place as a record)
- Velocity gate (VEL / VEL_REF / GATE_PIVOT): overfit to env_r127; overshoots ReBirth on
  musical low-cutoff lines (detroit centroid 2151 vs ReBirth 894); tonal chirp. SHELVED.
- Pre-filter drive (DRIVE): darkens (added harmonics are lowpassed out). Rejected.
- HFQ removal (HFQ): brightens but tonal and overshoots; it is the darkness CAUSE, not the fix.
- Makeup gain (MAKEUP): no effect on brightness. Ruled out.
- Feedback HP + overdrive (FB_HP / FB_DRIVE): louder tonal peak, opposite of broadband. Ruled out.
- Per-stage nonlinearity (STAGE_NL): matched ReBirth brightness LEVEL at high cutoff
  (env_r127) but OVERSHOOTS at low cutoff (detroit). NOT a general win; initially
  overclaimed, ReBirth-anchoring caught it.
- Post-filter distortion (POST_DRIVE): barely moved flatness; symmetric shaping of a peak
  makes odd harmonics of the peak, not a comb. Ruled out.

## Diode-ladder build: scaffold in place, correct equations NOT yet found
Selectable coupled-diode-ladder core: REBORN_FILTER=diode. Params: D_VT (nonlinearity),
D_K (feedback), D_P1 (first-pole ratio), D_OS (oversample), D_FBHP (feedback HP), D_G
(tuning), D_ITER (fixed-point iterations). State dcap1_..dcap4_, dFbHp_. TPT default
byte-identical (verified repeatedly).
- Forward-Euler coupled: unstable, does not tune.
- Backward-Euler fixed-point: stable but STILL does not tune: the resonance sits low and
  will not track cutoff (PSD-confirmed, out/diode_be_check.png).
- CONCLUSION: the failure is the FORMULATION (my symmetric tanh-difference "diffusion":
  dv_i = w*(f(v_{i-1}-v_i) - f(v_i-v_{i+1}))), not the integrator. It does not map to a
  tunable resonant ladder. Continued guessing at the coupling form was thrashing; stopped.

## The correct next step (fresh session)
Derive the ACTUAL 303 diode-ladder equations properly, from the schematic and validated
published formulations: D'Angelo & Fontana "EMS VCS3 VCF as a Nonlinear Filter Network";
D'Angelo & Valimaki "Generalized Moog Ladder I/II" (IEEE TASLP 2014); Zavalishin's
zero-delay ladder with correct diode coupling; Huovilainen 2004 for the per-stage-tanh
discretization template. Do the tuning + stability analysis FIRST, then implement into the
existing scaffold and FIT (cutoff map, D_K, D_P1, D_FBHP, D_VT) to the ReBirth sysid target.
This is design work, not sandbox trial-and-error.
Fit loss = multi-patch spectral distance (resonance emphasis freq + bandwidth/flatness +
rolloff/centroid + articulation) over sysid_c020..120 + env_r127 / env_r30 + detroit + stress.
Success => resonance-vs-cutoff curve + broad comb (flatness ~0.16 range) + brightness +
articulation all matched; then hfQ retires and the chirp goes with it.

## Assets
- ReBirth reference exports (comp-OFF): stress_303, env_r127, env_r30, detroit_303,
  sysid_c020/045/070/095/120 (.WAV).
- rbsgen patches: detroit_303.rbs (A-minor 128-BPM test song), env_r127 / env_r30 /
  static_r127 / stress_303 / held_303, sysid_c*.rbs. Scratch mode confirmed ReBirth-loadable.
- Repo reborn338-main (Jul 18 zip). The probe-enabled src/devices/bass_synth.cpp lives in
  the SANDBOX ONLY; main is clean; re-upload the repo zip to resume. All probes default-off,
  byte-identical unset, nothing committed. GitHub commits (when we get there) show author
  "Luke Ferdinand"; nothing to main without Luke's explicit sign-off.
- Figures: sysid_panels.png, foundation_vs_rebirth.png, reso_isolation.png, detroit_3way.png,
  detroit_pluck_env.png, diode_be_check.png.
- Calibration hygiene: ReBirth exports master-compressor-OFF; measure both sides with ONE
  channel-aware loader (renders are stereo dual-mono).

## Working notes worth carrying
- ReBirth-as-ground-truth is the discipline that repeatedly caught overfitting (the gate,
  per-stage NL). When unsure if a waveform is musical, measure against a real ReBirth export;
  don't tune to an internal meter, and ask for a reference export if one is missing.
- Corrections land cleanly and immediately, no relitigating.
- High-altitude/zoom: Luke sets direction and picks forks; cousin holds granular state and
  executes, with an approval beat before consequential writes.

## DO NOT propagate
- The ruled-out probe params as "solutions": they are dead-ends on record, not settings.
- The "0.57x centroid offset / definition difference" claim: it was a LOADER BUG (mono-as-
  stereo), since corrected. The gate matched ReBirth's brightness directly.
- Any Open303 source or ReBirth decompilation: clean-room forbids it.
