> **SUPERSEDED 2026-08-04.** A point-in-time diagnosis, kept for its reasoning
> trail. For current state start at `docs/research/PLAN_303_ENVELOPE.md`. Its section 3.2
> explicitly RETRACTS the env-mod-0 reasoning this diagnosis is built on: the
> "env mod 0 is flat" reading was an artefact of the instrument, whose travel
> gain falls to 0.55 below about 0.6 octaves, and measured properly our envelope
> depth at env mod 0 is within 0.05 octaves of ReBirth's. The probe vocabulary
> here (`REBORN_ENV_ATK/OVER/SHAPE/DEC_MUL`, `REBORN_K_EMTRADE`,
> `REBORN_FILTER=rb`) predates the plan's section 4 probe table and should not be
> used as a guide to what exists. What survives is the shape of the question and
> the resonance-versus-sweep framing, which the plan's section 0 now answers.

# 303 note-trajectory shape: diagnosis (2026-07-24)

## The defect

Per-note centroid trajectory, measured in windows after each onset
(w0 = 0-10 ms, w1 = 10-40 ms, w2 = 40-90 ms; w3 = 90-170 ms is dropped because
at 120 BPM it bleeds into the next note on both sides). ReBirth falls
monotonically within a note (the classic pluck); ours rises. Baseline, Hz:

| fixture         | ReBirth w0/w1/w2 | Reborn w0/w1/w2 |
|-----------------|------------------|-----------------|
| t303_envmod000  | 353 336 320      | 357 342 342  (OK, static) |
| t303_envmod040  | 468 432 394      | 412 382 391  |
| t303_envmod080  | 613 548 476      | 467 502 598  |
| t303_envmod127  | 790 713 585      | 808 1049 845 |
| t303_emdecay020 | 646 501 352      | 371 375 578  |

## What it is NOT (ruled out by probe sweeps)

- **Filter-env attack** (REBORN_ENV_ATK). 0.2 ms changed nothing; 20 ms
  flattened envmod080 but wrecked envmod127. Refuted.
- **VCA pluck, directly** (REBORN_AMP_DECAY). The VCA is POST-filter, so it
  scales HF and LF equally and cannot move the centroid. Disabling it only
  broke the onset measurement (notes sustain). Refuted as a direct cause.
- **Envelope-to-cutoff shape** (REBORN_ENV_SHAPE). Exponent > 1 made the rise
  worse. Refuted.
- **A simple ring-up.** envmod000 (static cutoff) is FLAT (no rise), so the
  resonance is not just building up over time in isolation. The rise appears
  ONLY when the cutoff is sweeping.

## What it IS

A **resonance x sweep interaction**. Two independent probes confirm resonance
is the source but also show it cannot be tuned out:

- Lowering resonance globally (REBORN_K_A/K_B down) cuts the rise
  (rise-penalty 1.54 -> 0.35) but collapses the whole centroid, because that
  same resonance provides the passband brightness we fitted.
- Trading resonance against env-mod (REBORN_K_EMTRADE, k *= 1 - trade*envmod,
  which preserves envmod000 by construction) also cuts the rise
  (1.54 -> 0.37) and also craters brightness: envmod127 falls to 154 Hz vs
  ReBirth 790.

The sharper reading: at envmod127 our **w0 (808) already matches** ReBirth
(790); we do not lack attack brightness. The note GAINS brightness after the
attack (808 -> 1049). The resonance rings up at the swept-high attack cutoff
over the first ~40 ms instead of being saturated at onset, so the note
brightens where ReBirth's cutoff sweep makes it darken.

Faster envelope decay (REBORN_ENV_DEC_MUL=0.3) turns the EARLY trajectory from
rising to falling (score 7.39 -> 6.26) because the cutoff drop starts to
outrun the ring-up, but it globally rescales the decay knob, so it is a
diagnostic, not a fix.

## Conclusion

The trajectory shape is NOT fixable by the available scalar parameters:
resonance is simultaneously the brightness source and the rise cause, along the
same axis. ReBirth achieves bright-AND-falling, which this TPT-ladder topology
does not by parameter tuning. The fix is structural:

1. **Faster resonance ring-up** so the peak is saturated at w0 and the cutoff
   sweep then dominates (falling). Needs the ladder to settle its resonance in
   a few ms rather than ~40 ms at the swept-high cutoff.
2. **Evaluate the RB forward-Euler core** (REBORN_FILTER=rb) whose sweep
   dynamics differ; it was shelved for being dark on musical patches but its
   trajectory behaviour under a swept cutoff has not been measured.

Both are best attacked with the parallel diagnostic workflow (the probe
scaffolding in bass_synth.cpp is set up for it) once the session limit resets.

## Update 2026-07-25: the RB core falls, but does not generalise

Direction 2 was measured. The RB forward-Euler core (REBORN_FILTER=rb) has the
CORRECT trajectory shape for free: on the fixtures its rise-penalty is 0.00 and
it falls monotonically on every one (envmod127 290/273/263 vs ReBirth
790/713/585 in shape). This is the structural property the TPT ladder cannot
produce. Its defect is darkness, which responds sharply to the resonance loop
gain (REBORN_RB_KGAIN) WITHOUT disturbing the fall.

A brightness fit (RB_A=650, KGAIN=37, KMAX=45, EM_OCT=2.2) scored 4.53 on the
trajectory metric vs the TPT's 7.39, a real improvement on exactly the
env-mod-heavy fixtures the TPT gets worst. BUT two problems make it unshippable
as-is:

1. **Mid-scoop.** Even when bright, the RB peak is too narrow: at envmod080 the
   250/500 Hz bands read -15/-17 dB vs ReBirth's -9/-10. The resonance piles
   energy at the peak instead of filling the spectrum as it sweeps. Opening the
   filter (higher RB_A, lower gain) fills nothing and kills the env-mod
   response. The scoop is inherent to the narrow peak, un-tunable, the mirror
   image of the TPT's un-tunable rise.
2. **Does not generalise / near-self-osc instability.** The fit was to fixtures
   at cutoff 40 / reso 90. On the musical detroit patch (cutoff 55 / reso 115)
   the same settings OVERSHOOT wildly: centroid 2781 Hz vs ReBirth 894, bands
   scooped to -26/-27 dB. Detroit's higher resonance drives the RB core into
   its twitchy near-self-oscillation regime. The TPT default is far more robust
   here (1116 vs 894).

So both cores have complementary, un-tunable structural limits: TPT is bright,
band-filled and ROBUST but its trajectory rises; RB falls correctly but
mid-scoops and destabilises at the resonance real songs use. Matching ReBirth
needs a resonance that is simultaneously PROMINENT (sweeps the centroid down),
BROAD (fills the mid-band as it sweeps) and STABLE at high reso, a genuine
topology design, not a parameter fit of either existing core.

DECISION: ship the calibrated TPT (robust across the operating range); the
ideal-resonance topology is a scoped future research item; the audibility of
the trajectory rise on musical material is the open question for an ear check.
Ear-check files rendered: renders/detroit_TPT_v2.wav (shipping) vs
renders/detroit_RBfit.wav (RB core) vs renders/detroit_303.WAV (ReBirth).

## Probe scaffolding added (all inert at default -> engine behaviour unchanged)

REBORN_ENV_ATK (0.002) REBORN_ENV_OVER (1.10) REBORN_ENV_SHAPE (1.0)
REBORN_ENV_DEC_MUL (1.0) REBORN_K_ENVFLOOR (1.0) REBORN_K_EMTRADE (0.0)
plus tools/analyze/envmod_ab.py (trajectory metric) and the traj.py grid harness.
