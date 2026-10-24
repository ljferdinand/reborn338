# PCF resonance rewrite (branch exp/pcf-resonance)

Started 2026-09-06 off `pcf-supertask`. EXPERIMENTAL, merges nowhere without
Luke's sign-off. The shipped PCF (TPT SVF, `src/effects/pcf.cpp`) is untouched;
the new core lives behind a flag so both run in one binary for A/B.

## The one goal

Close the last PCF character gap: the DYNAMIC resonance during the fast envelope
sweep. ReBirth's PCF, on each trigger, produces a bright resonant peak that rides
the cutoff sweep down as a smooth curve ("flangy, ring-modulator, a curve on
every chirp", Luke 2026-09-06). Our zero-delay TPT SVF settles too cleanly: same
cutoff and source, but ours is duller at the attack and its brightness peaks
~200 ms late (PCF_FINDINGS 12). The static filter, the bandpass (now fixed), and
the between-harmonic content all match; only the swept-resonance dynamics differ.

This is the SAME FAMILY as the 303 round-chirp. The 303 diode branch established
(PLAN_DIODE_CHIRP): the ride/ring is a marginally-stable, loop-delayed
forward-Euler phenomenon; zero-delay / implicit / backward-Euler cores settle too
fast to ring; and fittability (a clean static response) and the ride are in
structural tension. That tension is the risk here. The PCF is a SIMPLER filter
than the 303 voice (one 2-pole resonance, a known envelope), so it may be more
tractable, but the precedent says this can be large with uncertain payoff.

## The target (validation)

- **Chirp trajectory**: `renders/pcf-chirp-2026-09-06` + `scratchpad/chirp_traj.py`.
  Match RB's peak-center and centroid curve over each trigger (bright at the
  attack, smooth descending curve). `lp_q100` RB centroid 5259 6527 5409 3797 3138
  2216 1541 965 (at 20 60 120 200 300 450 600 900 ms).
- **Static parity must be PRESERVED**: the LP shape (drone, 0.3 dB), the BP peak
  (0.3 dB across Q), the resonance-vs-Q, the corner drift, the makeup. The rewrite
  must not regress these. Guard with the drone static fixtures (round 2 / pcf-bp).
- **Level**: the ~3-4 dB dynamic makeup deficit (ours quiet when the envelope
  runs) may be the SAME root as the ring gap (a weak dynamic resonance) or
  separate; watch whether the rewrite closes it too.
- **Ear**: the listening set (`listen-pcf-2026-09-05`), re-cut, null pair first.

## Approach, in order

1. Stand up a resonance core behind a flag (`REBORN_PCF_CORE` or similar) in
   `pcf.cpp`, defaulting to the current TPT so nothing changes unflagged.
2. Reproduce RB's swept-resonance ring. Candidate cores, guided by the 303 work:
   a coupled/forward-Euler 2-pole resonator with delayed feedback at the native
   rate (rings, per the 303 rb core), possibly lightly oversampled; or a
   trapezoidal SVF with the cutoff-sweep transient preserved. Zero-delay and
   backward-Euler are known NOT to ring (303), so they are not the vehicle.
3. Fit the static response (LP shape, BP peak, resonance-vs-Q, makeup) so the
   rewrite matches the current parity while ALSO ringing on the sweep.
4. A/B the chirp trajectory vs RB; then the static guards; then the listening set.
5. If it rings AND holds static parity, it is the candidate to replace the TPT
   resonance (Luke sign-off). If ring and static parity cannot both be had (the
   303 tension), fall back: keep the fixed-BP TPT and log the dynamic ring as a
   documented residual.

## Progress

### 2026-09-06: the dig: the makeup is largely correct; the gap is dynamic brightness

Kept digging the ~3-4 dB dynamic deficit (Luke's call). Findings:

- **The reliable deficit is on the 909 listening patterns** (identical samples,
  null matched 0.02 dB): ours attenuates the pattern ~3.9 dB vs the null where RB
  attenuates only ~1.1 dB, so ours is ~2.8 dB quieter. `pcf_ratio` locates it: the
  LOW bands match (delta ~0), the HIGH bands (2-11 kHz) are 3-5 dB down. So it is
  ours being DARKER at the top during the sweep, not a passband-level loss.
- **Makeup vs cutoff at resonance** (`renders/pcf-makeup-2026-09-06`, static drone,
  transfer so source divides out): at Q 0 and Q 96 ours matches RB across the
  whole cutoff range (0.1 dB). So the resonance makeup is correct where the
  patterns live (Q 90 ~ Q 96). The ~3 dB is NOT a makeup deficit.
  - One real but separate anomaly: at Q 64 RB's makeup drops ~2.5 dB as the cutoff
    rises above Freq 64 while ours stays flat (ours ~+2.5 dB louder), mid-Q only.
    Does not affect the Q90 patterns; log as a minor residual.
- **Low-base big-sweep fixtures over-open** (envelope depth ~10-15% too high): the
  ENVSC probe (default 1.0) shows ~0.88 recovers ReBirth's bright-attack descending
  chirp on Freq-30 sweeps. But the Freq-70 listening patterns CLAMP the cutoff and
  are unchanged by it, so this fix helps the low-cutoff levels, not the patterns.

CONCLUSION of the dig: the remaining gap is TWO things, neither a passband makeup:
(1) the low-base sweeps over-open (envelope depth, TRACTABLE, verified); (2) the
high-cutoff patterns read darker at the top during the sweep = the dynamic
resonance brightness / "flangy chirp", which is the 303-class ring/ride the
zero-delay filter does not reproduce. The makeup is not the culprit. So the
resonance rewrite is the honest lever for (2); (1) is a small calibration.

### 2026-09-06: the one-more-measurement: max cutoff is NOT too high; it is the ring/ride

Tested whether the pattern darkness is ours' MAX cutoff clamping too high (which
would push the resonance above the drums) rather than a true ring/ride. Bright
909-hat source (`hat_lp_f096..127`, renders/pcf-round2), which reaches higher than
the drone, ours (sample mode) vs RB, transfer:

    Freq 127 transfer at 4/5/6/8 kHz:  RB +4.6/4.7/4.8/4.9   ours +4.3/4.3/4.3/4.2

So at max cutoff both are ~fully open through 8 kHz, ours only ~0.3-0.6 dB darker
(above 8 kHz the hat has no energy, so the corner at ~14.5 kHz is not resolvable,
but the drums and the patterns carry little there anyway). A ~0.5 dB static
difference cannot make the patterns 3-5 dB dark during the sweep.

CONCLUSION: the max-cutoff / cutoff-law-top hypothesis is RULED OUT. The pattern
darkness-during-sweep is the genuine dynamic resonance ring/ride, the 303-class
structural thing (a zero-delay filter does not ring/ride like the analog; a
ringing core cannot hold static parity, per the 303 diode branch). Every
tractable alternative (makeup, cutoff-top, bandpass) is now eliminated. The honest
options are the full resonance rewrite (303-class hard, uncertain) or accepting
the ring/ride as a documented residual. Recommendation: bank the bandpass fix and
the ~0.95 Amt-depth correction, document the ring/ride residual.

### 2026-09-07: the rewrite is FALSIFIED: the "ride" is cutoff position, not resonance

Built the Chamberlin SVF (delayed one-sample resonance feedback, oversampled,
soft-clipped) behind REBORN_PCF_CORE=1 and A/B'd the chirp centroid vs RB:

    lp_q100 ms:        20   60  120  200  300  450  600  900
    RB            :  5259 6527 5409 3797 3138 2216 1541  965  (bright attack, curve)
    TPT (shipped) :  3794 4613 4463 5017 3990 2574 1883 1180  (dull, late peak)
    Chamberlin OS8:  4020 4779 4626 5049 4037 2615 1908 1205  (~identical to TPT)

The ringing/delayed-feedback core behaves like the zero-delay TPT: same dull
attack, same 200 ms-late peak. It does NOT produce RB's bright-attack descending
curve. The reason is logical and settles the whole thread: RB's cutoff at the
attack sits ~6.5 kHz (in the drums' rich band, so the resonance there is bright);
ours sits ~14.5 kHz (above the drums, so dull). A resonance core sits AT the
cutoff and cannot move it. So the "ride/curve" is a cutoff-POSITION difference
(ours over-opens the cutoff ~1 octave too high at the attack), not a
resonance-dynamics difference. No resonance rewrite can fix a cutoff-position
error. The resonance rewrite is FALSIFIED (as the 303 zero-delay build was).

The real lever is the envelope/cutoff over-opening (ENVSC ~0.86-0.95 pulls it
back; it fixes the un-clamping low-cutoff sweeps). The clamping Freq-70 patterns
are mostly at parity already (pat05 centroid 5194 vs RB 5235); the ones that lag
are the fast/32nd patterns (pat34 3888 vs 4466), a smaller retrigger-shape thing.
The Chamberlin core stays behind the flag (default off) as the evidence.

CONCLUSION: abandon the resonance rewrite. The tractable levers are the BP fix
(done) and an envelope-depth correction (~0.95, matching RB's measured static
Amt); remaining big-sweep and fast-pattern differences are small residuals.

### 2026-09-07: the cutoff-dependent envelope-depth fit does NOT converge

Exported a Freq sweep of resonant chirps (`renders/pcf-fsweep-2026-09-07`, Q100
Amt110 pattern0) and fit ENVSC(Freq) by matching ours' attack-centroid trajectory
to RB at each base cutoff. Result: the best-fit ENVSC is noisy and non-monotonic
(F0..100: 0.90 0.90 0.90 0.70 0.60 0.50 0.80) and, crucially, the centroid-
trajectory residual stays HIGH at the best depth (0.16 to 0.45 octaves). So a
Freq-dependent envelope depth does not reproduce RB's swept trajectory: RB's
difference is in the SHAPE (peak time, descent), not a depth scale.

CONCLUSION of the whole PCF resonance investigation: every tractable model has
been tried and either ruled out or failed to fit: makeup (matches Q0/Q96),
cutoff-law top (matches at Q0 through 8 kHz), resonance core rewrite (Chamberlin
behaves like the TPT; the ride is cutoff-position not resonance), and now
cutoff-dependent envelope depth (does not converge). The remaining swept-resonance
brightness ("flangy chirp") is a diffuse difference in the envelope/cutoff
trajectory shape that does not reduce to a clean fittable law. The one clean,
principled fix in hand is the uniform ~0.95 Amt-depth correction (RB's measured
static Amt, PCF_FINDINGS 5). Recommendation: apply that, bank the bandpass fix,
and log the swept-resonance brightness as a documented residual (as the 303
round-chirp was), PCF measurement-complete.

### 2026-09-07 (cont.): ENVOFF fixes low-cutoff; the pattern deficit is TRIGGER DENSITY

Tested a constant envelope-cutoff-parameter offset (REBORN_PCF_ENVOFF, subtract
from the envelope contribution, floored at 0). It fits the low-cutoff over-opening
cleanly (F0 centroid residual 0.41 -> 0.11 at ENVOFF 0.05) AND at Amt-max gives
1.0 - 0.05 = 0.95, i.e. ReBirth's measured static Amt depth (PCF_FINDINGS 5). So
ENVOFF 0.05 is a single principled fix for BOTH the static Amt and the low-cutoff
swept over-opening. But it does NOT change the Freq-70 listening patterns (they
clamp regardless of the offset), and the mid-high-cutoff swept residuals stay high
(0.4-0.5) at every model, so the swept-brightness across cutoffs has no clean law.

NEW, distinct finding: the ~2-3 dB pattern-level deficit CORRELATES WITH TRIGGER
DENSITY, not with the sweep. Whole-file level ours-RB on the 909 listening set:
pat00 (1 trigger) +0.96; sparse pat12 -0.26; dense sixteenth/32nd pat05 -2.76,
pat34 -2.74, pat40 -2.82. And pat05's CENTROID matches RB (same character), so it
is purely quieter. So the dominant thing Luke hears on the patterns is a
RETRIGGER/ATTACK effect (more retriggers -> more level lost vs RB), NOT the
swept-resonance brightness. That is a fresh, specific lead, separate from the
(exhausted) swept-brightness modeling and likely more tractable.

STATE: BP fixed. ENVOFF 0.05 = a real win (static Amt + low-cutoff sweeps), apply
it. Swept-resonance brightness across mid-high cutoffs = documented residual (no
clean model). Pattern-level deficit = trigger-density/retrigger-attack, a new lead
to chase or accept.
