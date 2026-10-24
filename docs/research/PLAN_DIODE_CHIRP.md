# Coupled-ladder retriggered chirp (branch exp/diode-ladder-chirp)

Started 2026-09-04, off `pcf-supertask` at the FB_SEP ship (`be0970e`). This
branch is EXPERIMENTAL and merges nowhere without Luke's sign-off. The shipped
buffered TPT voice is untouched (FB_SEP is on and stays on as the shipped core).

## The one goal

Close the last 303 character gap: the RETRIGGERED per-note "round chirp" curve.
FB_SEP gave the buffered TPT the HELD chirp (the resonant peak rides on a slid
note), but on a fast RETRIGGERED note TPT's zero-delay stages settle instantly and
the resonance cannot ring up before the pluck decays, so each beat reads as a flat
accent instead of ReBirth's rising filter curve (BASS303_FINDINGS 29-30, and Luke's
ear on the tour saw 2026-09-04: "a curve on every beat... ours sounds more of an
accent", measured per-beat centroid rise 165 Hz ours vs 376 Hz ReBirth).

A coupled ladder rings up over cycles (forward/backward Euler integration), so its
resonant peak builds and rides on a retriggered pluck the way ReBirth's does. The
task is a coupled core that RIDES the retriggered curve AND matches ReBirth's
band-shape on the whole net.

## What we already know (this session)

- **`REBORN_FILTER=rb` (forward-Euler coupled) RIDES the retriggered chirp**
  (b10_r100_saw peak 988->1308 where buffered/FB_SEP stay flat at 215; reso-127
  tracks ReBirth). But its BAND-SHAPE cannot be fit: net 1.896 -> 5.081, a
  structural forward-Euler native-rate rolloff tilt (6-9 dB too dark above 300 Hz
  at cutoff 64 reso 0), and a wide corner-law search only reaches +3.8 on the
  cutoff-heavy subset. Section 30.
- **`REBORN_FILTER=diode` (backward-Euler coupled) does NOT ride**: peak flat at
  215 Hz at every resonance including 127. It does not currently form a resonant
  peak. But backward Euler is unconditionally stable and its rolloff is tunable
  (the "correct framework" per `reborn338_diode_ladder_design.md`), so if it can
  be made to ring it is the better vehicle than the forward-Euler `rb`.

## Targets and scorers

- **Retriggered ride**: `tools/analyze/chirp_peak.py` on B10, especially
  `b10_r100_saw` (retriggered), target ReBirth's 215->425->482->291->215 arc; and
  `b10_r127_saw`. Held and square must stay matched (rides where RB rides, flat
  where RB is flat).
- **Band-shape parity**: `tools/analyze/regress_ab.py` (373-cell net), target
  TPT's ~1.9. This is the hard constraint the `rb` core fails.
- **Ear**: the tour saw per-beat curve, and the groove/voice, against ReBirth.

## Approach, in order

1. **Make the backward-Euler `diode` core RING.** Diagnose why it does not
   resonate (feedback gain `D_K`, saturation knee `D_VT`, integration `wT`/`D_G`,
   iteration count `D_ITER`). Get a resonant peak that rides the B10 chirp at all.
2. **Tune the diode cutoff->Hz and resonance** to match ReBirth's resonance-vs-
   cutoff curve (the sysid references) and ride the retriggered chirp.
3. **Fit the diode band-shape to the net.** Backward Euler's rolloff is tunable
   (oversampling `D_OS`, first-pole ratio `D_P1`), so unlike `rb` it should reach
   net parity. This is the make-or-break the `rb` core failed.
4. **A/B** vs ReBirth on B10 (chirp ride) + net + songs. If it rides AND reaches
   net parity, it is the candidate to replace the buffered core (a Luke sign-off,
   a core swap). If it cannot reach net parity, fall back to the `rb` core with the
   band-shape accepted as a documented limit, or keep the buffered FB_SEP voice and
   log the retriggered curve as a permanent residual.

If the diode core cannot be made to both ride and match band-shape, that is itself
a finding: the retriggered curve may be beyond what any single-file core swap can
give without the full nonlinear-network solve (D'Angelo fixed-point), which is a
larger build.

## Progress

### 2026-09-04: the diode core RIDES the retriggered chirp (step 1 done)

Step 1 is done: the backward-Euler `diode` core rings and rides. It was flat
because its feedback gain was far too low (`D_K` 1.0 gives kd = k, max ~4, where
the coupled structure needs kd ~30-40 like the `rb` core's `RB_KGAIN`). Raising
`D_K` makes it ring, and the cutoff scale `D_G` places the resonant frequency.
Probed on `b10_r100_saw` (retriggered, RB target 215->425->482->291->215):

    D_G 0.5  D_K 10  D_VT 0.3   215 -> 420 -> 799 -> 221   RIDES (overshoots peak)
    D_G 0.5  D_K 14+            self-oscillates from onset (runaway)
    D_G 1.0  D_K 10            flat (too weak);  D_K 14  rides but at ~3900 Hz (corner too high)
    D_G 0.25 D_K 10           rings from onset at ~1000 (not tracking the sweep)

So `D_G` 0.5, `D_K` ~10, `D_VT` 0.3 is the first riding point: the peak rises and
falls tracking the corner sweep, roughly ReBirth's shape, where the buffered TPT
(and FB_SEP) stay flat at 215. `D_VT` 1.0 (soft knee) kills the ring; the hard 0.3
knee is needed. All via env probes; the diode code and its defaults are unchanged.

NEXT (open): (2) fine-tune `D_G`/`D_K` to hit RB's peak (~482 not 799) and match
the ride at reso 127; (3) tune the cutoff->Hz and resonance across the range
against the sysid references; (4) THE MAKE-OR-BREAK: fit the diode band-shape to
the net (target ~1.9) using the backward-Euler rolloff levers (`D_OS`, `D_P1`),
which is where the forward-Euler `rb` core failed; (5) A/B vs ReBirth on B10 +
net + songs. Scorer for the ride is `chirp_peak.py`; for band-shape `regress_ab.py`.

### 2026-09-04: the band-shape make-or-break leans POSITIVE

The reso-0 rolloff diagnostic (cut064 reso000, feedback off, pure rolloff), the
same test the `rb` core FAILED (6-9 dB too dark in the mids, structural). The
backward-Euler diode with `D_G` 0.8, `D_P1` 2.0:

    band vs RB: 224-3592 Hz within +/-1.2 dB (as good as TPT, which is the shipped
    net-1.9 core), where the rb core was -6 to -9 dB. The diode's fundamental
    rolloff MATCHES ReBirth in the mid band.

The one defect is EXCESS HF above ~4.5 kHz: +2.4 dB at 4.5k rising to +10 dB at
9k. Oversampling does NOT touch it (D_OS 4/8/16 identical, band-rms ~3.33 dB), so
it is not aliasing; it is the diode's inherent HF rolloff being too flat (the
tanh-generated HF the 4-pole does not fully attenuate). That is a narrow,
tunable high-end problem (an output HF rolloff/shelf, or pole-structure/`D_P1`
tuning), NOT the structural mid-band wall the `rb` core hit.

CONCLUSION so far: the diode core RIDES the retriggered curve (step 1) AND its
mid-band rolloff matches ReBirth (step 4 make-or-break, the part that killed the
`rb` core). The remaining band-shape work is the HF excess above 4.5 kHz. So the
diode path has real legs: it can plausibly reach net parity and ride, which the
forward-Euler `rb` could not. Next session: tame the HF excess, then the full
cutoff/reso fit and the net A/B.

### 2026-09-04 (cont.): the make-or-break is PASSED audibly; the HF excess is a net-metric artifact

The flat band net rms overweights inaudible bands. Energy-weighting the reso-0
band error by ReBirth's own per-band energy (cut064 reso0, diode `D_G` 0.8, `D_P1`
2.0, `D_VT` 0.3):

    flat band-rms (the net metric):   3.33 dB
    ENERGY-weighted band-rms:         1.10 dB   <- the AUDIBLE band-shape error

1.10 dB is TPT-class. The +7 to +10 dB HF excess above 4.5 kHz sits in bands
holding 0.05% of the energy (ReBirth 0.03%), so it is inaudible; it just inflates
the flat rms. So the diode's audible band-shape matches ReBirth AND it rides the
retriggered curve, which no other core did. Make-or-break passed on the audible
metric.

`D_VT` does not reduce the HF excess (0.3 is best, higher is worse everywhere). A
one-pole output LP (`D_HF` probe, added default-off) also does NOT flatten it: it
darkens the mid while the HF stays +6.8, so the excess is NOT simple output HF
content that a lowpass removes. It is likely generated in the post-filter path
(makeup / output stage) or is a discretization artifact of the diode core, and is
the next debugging item to make the FLAT net agree with the audible reality.

STATE: diode viability confirmed (rides + audible band-shape matches). Open, in
order: (a) find and cut the inaudible HF excess so the flat net drops toward
parity (post-filter path / discretization, not a simple output LP); (b) fine-tune
the ride to RB's peak and match reso 127; (c) full cutoff/reso fit vs the sysid
references; (d) net + song + B10 A/B. The `D_HF` probe is default 0.0 (off).

### 2026-09-05: the "HF excess" is an own-normalization artifact, NOT real (item (a) resolved)

RAW (un-normalized) band levels at cut064 reso0 settle it:

    band       RB      TPT     diode(G.8,P2,VT.3)
    mid 1-3.5k  17.0    17.8     6.1     <- diode mid is ~11 dB LOWER
    6-9 kHz    -25.4   -26.0   -26.1     <- diode HF MATCHES RB and TPT

The diode's HF is at the SAME absolute level as ReBirth and TPT. There is no real
HF excess. The diode's musical mid is ~11 dB quieter, so the shared HF floor (the
VCA-attack transient / saw character, identical across all three cores) reads
prominent once each spectrum is divided by its own (lower) total. The `D_HF` LP
did not touch the output HF because that HF is generated after y4 (the shared post
path), not in the filter. So item (a) is NOT a diode defect: it is the flat
own-normalized net overweighting a shared, inaudible HF floor against a quieter
mid. The audible band-shape matches (energy-weighted 1.10 dB, section above).

CONSEQUENCE: the flat `regress_ab` net is the WRONG guardrail for fitting the
diode (it will always read the shared HF floor as a diode regression). The diode
fit uses the AUDIBLE metric: energy-weighted band error + the chirp ride
(`chirp_peak.py`). `D_HF` stays a probe, default 0.0, not needed.

NEXT: (b/c) the unified fit. The ride wants `D_G` ~0.5 / `D_K` ~10; the reso-0
band-shape wanted `D_G` ~0.8. Reconcile to ONE `D_G`/`D_K`/`D_VT`/`D_P1` that
rides the chirp across reso AND matches the band-shape across cutoff, scored on
the audible metric. Then (d) the A/B.

### 2026-09-05 (cont.): the reconciliation obstacle is the diode CORNER CALIBRATION

Trying to reconcile the ride and the band-shape at one setting surfaced the real
obstacle: the diode's resonant/ring frequency does not track the intended corner
`fc` predictably.

- The ride (b10_r100) wants `D_G` ~0.5 (peak lands ~482-799); the reso-0
  band-shape wants `D_G` ~0.8 (bandEW 1.10 vs 2.54 at 0.5). One `D_G` does not
  serve both.
- `D_OS` does not just reduce warp: at `D_OS` 8 the ring frequency JUMPS UP
  (D_G 0.5 rode to 799 at OS4, to 2557 at OS8), so the ring frequency vs the
  parameters is not a clean corner relationship. `D_K`/`D_G`/`D_OS` interact.
- The corner is not cleanly readable at reso 0 (85% rolloff there is
  fundamental-dominated, ~110-160 Hz for RB, and the diode reads even lower),
  so the reso-0 rolloff is not a usable corner-calibration signal.

So the diode needs a PROPER corner calibration before the ride/band-shape can be
jointly fit: measure the diode's actual resonant EMPHASIS frequency vs the cutoff
knob at a moderate/high static resonance (the method the `sysid_recon`
fixtures + the ReBirth `sysid_c*` exports in `renders/` were built for, and how
the `rb` core was originally fit), build the corrected `D_G`/`wT` mapping (and
whatever `D_OS`/curve correction the ring-frequency behavior needs), THEN fit the
resonance (`D_K`) to ride and the band-shape. This is a methodical multi-step DSP
calibration, the substantive core of the branch.

STATUS at this checkpoint: viability proven (rides + audible band-shape
achievable); the "HF excess" resolved as an inaudible own-normalization artifact
(use the audible/energy-weighted metric, not the flat net); the open work is the
corner calibration above, which is where the next session should start (the
`sysid_c*` emphasis-vs-cutoff references are the target). Nothing shipped; the
buffered FB_SEP voice on `pcf-supertask` is untouched.

### 2026-09-05 (cont.): the corner calibration shows the current diode is NOT cleanly fittable

The calibration ran, and the result is a structural one, not a tuning value.

- **RB emphasis vs cutoff**: c020 390, c045 523, c070 913, c095 1306 Hz (rising),
  matching the diode-doc's recorded c070 914 / c095 1306. But RB's emphasis sits
  LOWER than our net-good TPT (c020 597, c070 1430), so **matching the
  emphasis-vs-cutoff is NOT required for parity** (TPT does not, and is net 1.9).
  The emphasis (the `rb` core's old fitting target) is a red herring for the diode.
- **`D_G` does not behave like a corner scale.** Lowering `D_G` (0.8 -> 0.5 -> 0.3)
  makes the reso-100 ride go HIGHER (flat -> 799 -> 1077) and the reso-0 audible
  band-shape WORSE (bandEW 1.10 -> 2.5 -> 5.1), the opposite directions. `D_OS`
  also raises the ring frequency rather than just de-warping. So `D_G`/`D_K`/`D_OS`
  couple the corner, the ring frequency and the resonance non-monotonically: there
  is no clean "corner in Hz" knob, and the ride and the band-shape cannot be
  reconciled by tuning them.

CONCLUSION: the current backward-Euler diode scaffold is VIABLE (a coupled ladder
rides the retriggered curve and can match the audible band-shape, separately) but
NOT cleanly FITTABLE as parameterized: `wT`/`D_G` sets the integration step, which
tangles the corner frequency with the loop gain / ring, so a knob cannot set the
corner independent of the resonance. Making a coupled core fittable needs a DSP
REIMPLEMENTATION with a decoupled, prewarped corner control and a separate
resonance gain (a TPT-style zero-delay diode ladder, or the D'Angelo
fixed-point nonlinear-network with proper corner prewarp) rather than tuning this
scaffold. That is the larger build the plan's fallback named, and it is the honest
next step for the retriggered curve. Viability (the motivation) stands; the
shipped FB_SEP held-chirp voice is unaffected.

### 2026-09-05 (cont.): the zero-delay diode ladder was BUILT, and it does NOT ride (premise falsified)

On Luke's sign-off ("build the zero-delay ladder") the TPT-style zero-delay
coupled diode ladder was implemented as `REBORN_FILTER=zd` (bass_synth.cpp): four
COUPLED stages (each a trapezoidal integrator fed the net diode current, the
saturating difference to the stage below minus the one above), the corner set
EXACTLY by `g = tan(pi*fc/sr)` and DECOUPLED from the diode knee (`ZD_VT`) and the
resonance gain (`ZD_K`) via unit-slope saturators, the whole loop solved
implicitly per substep by fixed-point iteration. This is exactly the "decoupled,
prewarped corner + separate resonance gain" the section above prescribed, and the
decoupling WORKS as designed (Vt and k move the knees without shifting the corner).

But the core does NOT ride the retriggered chirp. Peak-center trajectory (STFT,
level-blind) on `b10_r100_saw` (RB arc 215->215->425->482->291->218):

    ReBirth           215  215  425  482  291  218    (flat, then RINGS UP, then falls)
    shipped FB_SEP    215  215  215  218  215  226    (flat: the known deficit)
    zd (zero-delay)  4054 3383 2845 2404 2086  215    (starts HIGH, tracks the corner DOWN)
    rb (fwd-Euler)    988 1184 1308 1163 1273  915    (RISES 988->1308 then falls: RIDES)

The zero-delay peak is fully present at 20 ms and decays; ReBirth's peak EMERGES
only after ~100 ms. That emergence is a resonance RING-UP, a loop-delay transient,
and zero-delay resolution removes exactly the lag that produces it. Diagnostics
that pin the mechanism, not a tuning:

- **Not fixed-point under-convergence.** ITER 4 and ITER 16 are identical
  everywhere. The ride is not a solver artifact.
- **The axis that controls the ride is FEEDBACK DELAY, not the integrator.** Added
  `ZD_INT` (0 trapezoidal / 1 backward-Euler) and `ZD_FBD` (0 implicit
  zero-delay feedback / 1 explicit delayed feedback). Implicit feedback (trap or
  BE) never rings up: it either starts high and falls (trap) or stays flat (BE).
  EXPLICIT delayed feedback + backward-Euler finally rings UP (`b10_r127_saw`
  ZD_FBD=1 BE K13: 215->1093->2048->2196, a genuine rise). So the loop delay is
  the ride, confirming the mechanism. But even delayed+BE does NOT ride
  `b10_r100_saw` (the round-chirp fixture): 215->215->215->231, still flat.
- **The forward-Euler `rb` core rides `r100` where NO zd variant does.** What `rb`
  has that the stable zd reformulations lack: LINEAR ladder stages with a SINGLE
  feedback clipper, and a marginally-stable EXPLICIT forward-Euler loop. The slow
  ~100 ms resonance build is that near-instability. The zd core's distributed
  unit-slope tanh on every inter-stage difference plus its stable solve are too
  well-behaved to build that way.

CONCLUSION (the branch's central finding): the retriggered "round chirp" ring-up
is a MARGINALLY-STABLE, loop-delayed forward-Euler phenomenon. Any core stable and
decoupled enough to be cleanly FITTABLE (zero-delay TPT, or a stable backward-Euler
diode) removes the very dynamics that produce the ride. The two properties are in
structural tension: fittability wants exact/stable integration; the ride wants the
near-unstable delayed loop. The `rb` forward-Euler core is the only core that rides
`r100`, and its band-shape is genuinely darker than the shipped voice, ~2x on an
energy-weighted third-octave band error over the sysid cutoff sweep (rb 13.8 vs
shipped 6.9 mean |band dB|, RB-energy-weighted), so `rb`'s darkness is audible and
real, not a flat-net artifact. The plan's proposed zero-delay build is therefore
NOT the vehicle for the retriggered curve.

OPTIONS from here (Luke's call; nothing shipped, buffered FB_SEP voice untouched):
1. Pivot the branch to FIXING the forward-Euler `rb` band-shape (it already rides;
   the task becomes un-darkening it). Directly targets the one riding core, but the
   darkness looked structural in the earlier `rb` work and the quick check agrees.
2. CONCLUDE the retriggered curve is beyond a single-file fittable core swap. Keep
   the shipped FB_SEP voice (it nails the HELD chirp and is close on `r127` max
   reso: 2789->..->1254 vs RB 2417->..->1292), and log the mid-reso retriggered
   curve as a permanent, documented residual.
3. A larger build: a full D'Angelo nonlinear network with a deliberately-tuned
   marginally-stable resonance. Big undertaking, uncertain payoff on a subtle
   mid-reso nuance.

The `zd` core, `ZD_INT`, `ZD_FBD` and the probes stay on this branch (all
default-off; REBORN_FILTER unset is the shipped voice) as the evidence for the
falsification.

### 2026-09-05: DECISION: branch CONCLUDED (option 2), FB_SEP kept

Luke chose option 2: the mid-reso retriggered "round chirp" is beyond a fittable
single-file core swap (the ride and fittability are in structural tension, proven
above), so we keep the ear-approved shipped FB_SEP voice and log the retriggered
mid-reso curve as a permanent, documented residual. This branch
(`exp/diode-ladder-chirp`) is CONCLUDED and merges nowhere; it stands as the
record of the investigation and its three cores (`rb`, `diode`, `zd`), all
default-off. The residual is recorded on `pcf-supertask` in
`docs/research/BASS303_FINDINGS.md` (where the shipped voice lives) so it is visible when
the 303 is reviewed for done. No further diode/coupled-ladder work is planned
unless Luke reopens it (option 1, un-darken `rb`; or option 3, the D'Angelo
nonlinear network).
