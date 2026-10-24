# PCF envelope-to-cutoff: behavioural specification for the implementer

You are re-deriving how the PCF envelope moves the cutoff, to match measured
ReBirth behaviour on the swept ("chirp") patterns across the WHOLE Freq range.
Work from THIS document, `docs/research/PCF_FINDINGS.md` (sections 21 and 22 especially,
and 14/16/17 for the surrounding laws), the measured audio under
`renders/pcf-envsweep-2026-09-13/` (the dense base sweep) and
`renders/pcf-sweep-2026-09-11/` (the original Amt sweep), the analyzer
`tools/measure/pcf_trajectory.py`, and the existing `reborn/` source
(`src/effects/pcf.cpp`). Those are your only inputs. Every number is measured from
ReBirth's audio output; fit your OWN coefficients to the measured trajectories.

This is a small, targeted change to ONE expression. Do not touch anything else.

## History on this branch

A FIRST attempt on this branch implemented a headroom-multiplicative law
(`cparam = base + D*Asat(amt)*env*(1-base)`, behind `REBORN_PCF_ENVLAW=old` for
A/B). It fixed the plateau for base >= 30 but OVER-OPENED at very low base, because
ReBirth's reach rolls off there (FINDINGS 21 caveat). A dense base sweep then
showed the true law is DEPTH-CAPPED, not headroom (FINDINGS 22). REFIT the law to
the depth-cap form below. Keep the `REBORN_PCF_ENVLAW=old` A/B toggle for the
shipped additive+clamp law.

## What is already correct and MUST NOT regress

- The AD envelope itself: instant attack to the step velocity on a trigger, then
  exponential decay; a retrigger REPLACES the level. Keep it.
- The measured decay law (`kDecayRate`, the 1-2-3-4-5-7 quantised table, seconds)
  and the top-band hold. Keep it.
- The cutoff law `fc = 9.01 * 2^(cparam*10.653 + bendOct) * fcScale`, including the
  base-Freq top-bend and the resonance corner ride. Keep it. The bend is driven by
  the BASE Freq, not the swept cparam, and stays that way.
- The resonator work on this branch (flag `REBORN_PCF_RESO`, on by default). Do not
  change it.
- Non-PCF renders byte-identical; PCF renders at Amt <= ~90 unchanged.

## The defect and the measured law (FINDINGS 22)

Today's cutoff parameter is `cparam = clamp(base + 0.95*amt*env, 0, 1)`, hard-clamped
at 1.0. On a high-Amt or high-base sweep the sum exceeds 1.0, so the cutoff PINS at
the ceiling (~15.7 kHz) and HOLDS until `env` decays under the clamp. That plateau
is the audible residual ("over-open and wavery, does not sustain the chirp").

ReBirth never plateaus and never over-opens. Its envelope contribution to the
cutoff parameter is DEPTH-CAPPED. A single law collapses the measured base sweep
(base 5..80 at Amt 110) to RMS 0.008 in cparam:

    cparam = base + g * env
    g = min( gcap(amt), D * (1 - base) )          # NO hard clamp

- `base` is the de-zippered Freq knob, `amt` the de-zippered Amt knob (both 0..1),
  `env` the AD level.
- `D * (1 - base)` is the headroom limit: at high base there is little room to the
  ceiling, so the reach is small and `base + g*env` stays below 1.0, so the cutoff
  descends immediately (no plateau). `D ~ 0.97`.
- `gcap(amt)` is the additive depth cap that binds at LOW base (lots of headroom):
  there the reach is `~gcap*env`, additive, so it does NOT over-open. At Amt 110
  `gcap ~ 0.79`. Fit `gcap(amt)` yourself.
- Because `g` is the MIN of the two, `base + g*env` is always below the ceiling, so
  there is no clamp and the trajectory descends immediately for every base and Amt.

Two measured facts pin the shape:

1. **Depth is capped at low base, headroom-limited at high base.** At Amt 110 the
   per-base descent depth `g` is ~flat (~0.78) for base <= 25 and falls as
   `D*(1-base)` for base >= 30. The `min` of a constant cap and a headroom line
   reproduces this to RMS 0.008. (The headroom-only law over-opened low base; the
   additive+clamp law over-opened and plateaued high base.)

2. **The Amt knob does NOT saturate.** At base 15 the reach grows Amt 110 -> 8.2 kHz,
   Amt 127 -> 14.3 kHz. The base-30 `a110 == a127` is the headroom limit, not Amt
   saturation. `gcap(amt)` keeps rising through 127 (mildly compressive at the top:
   descent depth ~0.67 / 0.79 / 0.85 at Amt 90 / 110 / 127). Fit a monotonic
   `gcap(amt)`; do NOT saturate it at byte 108.

Far from the ceiling this reduces to about the current additive map (the shipped
`0.95*amt` and the depth-cap agree at the fixtures' mid settings), so the low/mid
patterns that already match must not move.

## Acceptance tests

Rebuild on this branch (resonator on), re-render the fixtures with your law, and
compare with `tools/measure/pcf_trajectory.py` (same tool, both sides) against the
ReBirth renders:

- **Base sweep** (`renders/pcf-envsweep-2026-09-13/esw_b005..esw_b110` vs `esw_ref`):
  your peak-cutoff(t) tracks ReBirth within ~10-15% at every base 5..80 and every
  time point, and descends immediately (NO plateau) at every base. This is the test
  the headroom law failed at low base.
- **Amt sweep** (`renders/pcf-sweep-2026-09-11/psw_a040..psw_a127` and
  `renders/pcf-envsweep-2026-09-13/esw_lb015_a040..a127`): reach grows with Amt
  through 127 (no saturation); the high-Amt clamp fixtures (`psw_a110/a127/f050/f070`)
  descend immediately and match within ~15%.
- **Low/mid preserved**: `psw_a040..psw_a090` unchanged from before your change
  (measure before AND after).
- **Resonator preserved**: the prominence surface (`tools/measure/pcf_surface.py` on
  `renders/pcf-surface-2026-09-11`) unchanged from the branch baseline.
- `ctest --test-dir build -C Release` green; a non-PCF render byte-identical; a PCF
  render at Amt <= 90 essentially unchanged.

## Non-goals / constraints

- Not bit-exact (ReBirth exports are not sample-deterministic). Behavioural parity
  to the tolerances above.
- Fit your own `gcap(amt)` and `D` to the measured trajectories; seed nothing from
  elsewhere. The FINDINGS anchors are Role A's cross-check, not values to copy.
- Change ONLY the envelope->cutoff expression (the `cparam` line and, if needed, a
  small `gcap(amt)` helper). Do not touch the AD envelope, the decay law, the cutoff
  law, the resonator, the clock, or the presets.
- Keep the `REBORN_PCF_ENVLAW=old` A/B toggle for the shipped additive+clamp law.
- Work on `pcf-envelope-cleanroom` (this branch), engine building throughout.
  Commit early.

## Deliverable

The code, plus a report: the envelope->cutoff law and the `gcap(amt)` and `D` you
fit; a peak-cutoff trajectory table of yours vs ReBirth across the base sweep (5..80)
AND the clamp fixtures with pass/fail; confirmation low base no longer over-opens,
the low/mid and the resonator surface are unchanged, and the `ctest` result. Do not
overstate; if something does not fully close, say so and quantify the gap.

## ADDENDUM 2026-09-14: the compressive velocity curve (FINDINGS 23)

After the depth-cap refit, the single-trigger and full-velocity patterns match, but
LOW-velocity patterns render as a series of small hits where ReBirth is one wide
continuous sweep. Measured cause (FINDINGS 23, `renders/pcf-patternmotion-2026-09-14`):
ReBirth applies a COMPRESSIVE velocity->envelope curve. The pattern step sets the
envelope level through `env0 = f(velocity)` with f boosting the quiet steps:

| velocity | ReBirth env |
|---|---|
| 1.025 | ~1.00 |
| 0.929 | 0.911 |
| 0.652 | 0.796 |

Add this curve where the pattern sets the envelope level (today `env_ = velocity` on
a trigger; make it `env_ = f(velocity)`). Constraints:

- `f(v) ~ v` near full velocity (f(1.0) approx 1.0), so the single-trigger sweeps,
  pat0 and pat5 (all full velocity) and every prior acceptance test are UNCHANGED.
- f is compressive: it lifts low velocities (env > velocity below ~0.9), monotonic,
  f(0) = 0.
- Fit f yourself to the pattern-motion renders. `pmo_p19` (pattern 19) gives ten
  near-isolated triggers spanning velocity 0.29..0.93 (spaced 6/32, so at decay 64
  each mostly decays before the next): track its peak reach per trigger, convert to
  env via the inverse cutoff law, and fit f to (velocity, env). Cross-check pat16.
- This is applied to the trigger velocity only; keep the retrigger-REPLACES rule
  (section 11), the depth-cap reach (g = min(gcap(amt), D(1-base))), and the decay.

Acceptance (add to the existing set):
- `pmo_p16` (pattern 16, F70 Q90 A90 decay 64): your peak cutoff reaches ReBirth's
  ~7.5 kHz (within ~15%) and the sweep is continuous, not a staircase of resets.
- `pmo_p05`, `pmo_p00` (full velocity): UNCHANGED from before the curve (still match
  ReBirth). `pmo_p19`: tracks ReBirth across its velocity range.
- Re-run the section-13 ear-check patterns; the depth-cap results must not regress.

## ADDENDUM 2026-09-14 (2): the attack ramp / glide (FINDINGS 24)

Ours applies each trigger as an INSTANT jump of the envelope to f(velocity); ReBirth
RAMPS to it over ~30-40 ms (a one-pole approach, time constant ~10 ms). Measured on
`pmo_p16` (fine-time cutoff, continuous source): ours is a vertical sawtooth edge,
ReBirth a rounded wave. Add a short ATTACK GLIDE so the envelope eases to the trigger
target instead of jumping:

- On a trigger the envelope TARGET jumps to f(velocity) (unchanged); the envelope
  LEVEL glides toward it with a one-pole attack (fit the time constant, ~10 ms start,
  to ReBirth's pmo_p16 rise). The DECAY law is unchanged (still the measured 1-2-3-4-5-7
  table); only the rise is smoothed.
- Fit the attack time to RB's rise shape (pmo_p16, and cross-check pmo_p19). Prefer the
  simplest that matches (a one-pole toward the target). Keep the retrigger-REPLACES
  target rule (section 11) and the "pulls down on a lower trigger" behaviour.
- This affects only the first ~30 ms after each trigger, so the descent-based fits
  (depth-cap reach, velocity curve, decay) must be unchanged in the descent.

Acceptance (add to the set):
- `pmo_p16` rise: your cutoff RAMPS to the peak over ~30-40 ms (no vertical jump),
  matching ReBirth's rise; the per-cycle shape reads as a wave, not a sawtooth.
- The velocity-curve over-lift should ease: `pmo_p16` peak comes down toward RB's
  ~7.3 kHz (the ramp caps the fast-trigger reach). Re-fit the velocity gamma if needed
  so pmo_p16 peak matches RB within ~15% WITH the ramp in place.
- Full-velocity patterns (pmo_p00/p05, single trigger, the sweeps): a single trigger's
  attack now ramps too, but the DESCENT and the peak (which the reach fits use) must
  still match; re-verify the base sweep / clamp fixtures descend-match within tolerance
  and that ctest is green. If the ramp measurably shifts any prior-accepted DESCENT,
  say so.

## ADDENDUM 2026-09-14 (3): the attack must REACH the peak (fix the dense-pattern under-reach)

The tau-8ms attack ramp turned pat16 from a sawtooth into a wave (good), but it
REGRESSED the dense full-velocity patterns: `pmo_p05` peak dropped from ~14 kHz
(pre-ramp) to ~10.9 kHz (RB is ~14 kHz), and the sample-mode centroid fell. Cause:
the implemented model decays the TARGET from the instant of the trigger while the
LEVEL chases it (`envTgt_ *= envDecay; env_ += (envTgt_-env_)*slew`), so on a dense
pattern the level never catches the peak before the target has decayed; it settles
~15-20% below f(velocity). ReBirth's dense full-velocity patterns stay bright, i.e.
the attack REACHES the peak, THEN the decay runs.

FIX: make it a proper attack-then-decay. On a trigger the envelope rises to the FULL
target f(velocity) over the attack time (keep the ~8 ms rounded rise = the wave),
REACHES it, and only then decays by the measured law. The decay must NOT eat into the
attack. Smooth both edges (the rise, and the pull-down on a lower-velocity trigger)
with the attack slew so pat16 stays a wave, but the PEAK the level reaches must be
f(velocity), not a decayed value.

Acceptance (in addition to all prior):
- `pmo_p05` (dense, velocity 1.025) peak returns to ReBirth's ~14 kHz (p95), i.e. the
  pre-ramp brightness; the ramp must NOT darken dense full-velocity patterns.
- `pmo_p16` stays a rounded WAVE matching ReBirth's rise (the tau-8ms character is
  kept); its peak within ~15% of ReBirth's ~7.3 kHz.
- `pmo_p19` and the single-trigger / base-sweep / clamp descents still match; ctest green.

## ADDENDUM 2026-09-14 (4): the velocity curve must be S-SHAPED (FINDINGS 26)

The power-law velocity curve `f(v)=1.025*(v/1.025)^kVelGamma` over-boosts the LOW
velocities: on pat16 the quiet steps keep the cutoff half-open where ReBirth drops it
nearly shut. Measured pmo_p16 resonant-peak percentiles (trough p10 / median p50 /
peak p90): ReBirth 1378 / 3273 / 6546 Hz, ours 3015 / 4910 / 8096 Hz. Ours is shifted
UP and compressed; ReBirth swings wide (nearly closed on the quiet steps = a
percussive "click-and-clack", up to the chirp, and back).

Replace the power law with an S-SHAPED f(velocity):
- gentle TOE at low velocity: quiet steps stay quiet (f(0.34) ~ 0.5, not 0.79), so the
  filter returns LOW between beats. This is the character fix.
- boost in the MIDDLE (the sweep fill that the velocity curve was for).
- f(1.025) = 1.025 (full velocity unmoved, so all prior full-velocity acceptance and
  byte-identity hold).
- monotonic, f(0)=0.
Use a 2-parameter S-curve (logistic / Naka-Rushton / smoothstep-family), or a small
fitted lookup, your choice. Fit it so ours' pmo_p16 AND pmo_p19 cutoff(t) trajectory
matches ReBirth's: the TROUGH (p10) and median as well as the peak, not just the
peak. This is a trajectory match on the RB renders (robust to the per-trigger overlap
that makes single-trigger velocity extraction noisy).

Acceptance (add to the set):
- pmo_p16 trough/median/peak within ~15% of ReBirth's 1378 / 3273 / 6546 Hz (the
  filter must RETURN LOW on the quiet steps; the dynamic range must widen to ~RB's).
- pmo_p19 trough/median/peak track ReBirth similarly.
- pmo_p05/p00 (full velocity) unchanged (f(1.025)=1.025); the base-sweep/clamp descents
  and single-trigger sweeps still match; ctest green; non-PCF byte-identical. Re-fit
  kReachEps or kAttackTau only if needed and say what changed.
