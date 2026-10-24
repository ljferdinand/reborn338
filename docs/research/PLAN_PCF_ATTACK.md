# PCF swept-resonance, co-model attempt (branch exp/pcf-attack)

Started 2026-09-10 off `pcf-supertask`. EXPERIMENTAL, merges nowhere. Opened
when Luke, listening to the BP-fixed set, said the "curve" on ReBirth's chirp
still sounds as far off as ever and chose to reach for the full envelope+filter
co-model (the one lever `PLAN_PCF_RESONANCE` left untried). `pcf-supertask` is
untouched; every model here is behind a default-off env flag, so an unflagged
render is byte-identical to the shipped PCF (verified).

## What the chirp gap actually is (sharpened)

Measured trajectories on `renders/pcf-chirp-2026-09-06` (broadband probe, big
sweep, `scratchpad/pcf_chirp_metric.py`, averaged over triggers). ReBirth's
brightness rises to a peak at ~52 ms then descends smoothly; ours is duller at
the attack and peaks ~200 ms LATE. Split by two metrics:

- **A. Cutoff POSITION at the attack.** Centroid: RB peaks in-band (~6.7 kHz
  centroid at 52 ms) and descends; ours over-opens (cutoff rails to the
  extrapolated ~14.5 kHz top, above the source) so the bright in-band phase is
  wasted and the centroid peaks late.
- **B. Resonance PROMINENCE / ring.** RB holds a 38-45 dB sharp resonant peak
  riding the whole sweep; ours is 5-10 dB broader in the mid-sweep (up to 17 dB
  at Q120 by 200 ms), untouched by any position fix. This is the resonance
  ringing up during a fast sweep, the same structural behaviour the 303
  round-chirp established the zero-delay TPT cannot produce (PLAN_DIODE_CHIRP).

## Models tried, all FALSIFIED

Three envelope/cutoff models were built behind flags in `src/effects/pcf.cpp`.

1. **Finite envelope ATTACK** (`REBORN_PCF_ATK` = ms; one-pole slew of env
   toward a decaying target). FALSIFIED as a fit: no single time constant is
   both bright at 20 ms (RB is already ~5.3 kHz there) AND undershooting the
   clamp at the 52 ms peak. A fast slew reaches full and over-opens; a slow slew
   ramps from zero and is dark at 20 ms. The peak-time and peak-height cannot be
   had together from a one-pole attack.
2. **Envelope-excursion SATURATION within headroom** (`REBORN_PCF_SAT`; cparam =
   cut + h*tanh(exc/h), h = 1-cut). Static-exact by construction (exc=0 => no
   change; verified byte-identical). FALSIFIED: it over-shrinks the sweep, peak
   caps ~5.7 kHz where RB wants ~8 kHz, logerr 0.53 WORSE than the hard clamp
   (0.37). The headroom-relative tanh bends the whole excursion, not just the top.
3. **Cutoff-law CEILING** (`REBORN_PCF_FMAX` = Hz; soft-saturate log-frequency
   toward a ceiling above an 8 kHz knee, leaving the measured law <= 8 kHz
   intact). This one LOOKED good: chirp logerr fell 25-33% across lp_q080/q100/
   q120 and bp_q100, generalised across Q and mode, was not a pure metric
   artifact, and left every MEASURED static point <= 8 kHz byte-identical
   (regression workflow, both LP and BP). A skeptical review flagged that the
   ceiling lives in the region the drone cannot resolve, so it was fit to the
   chirp, not measured; and that FMAX helped fixtures where HARD was
   centroid-under-open, not the over-open case it was named for.

## The measurement that killed FMAX (2026-09-10)

`fixtures-2026-09-10/pcf-ceiling`, `renders/pcf-ceiling-2026-09-10`
(`gen_pcf_ceiling.py`): a BROADBAND probe (crash+hat 16ths) through a STATIC
(Amt 0), Q 0 lowpass at rising Freq. Low Q gives a clean rolloff corner, and a
broadband source RESOLVES that corner above 8 kHz where the drone cannot. This
directly measures ReBirth's actual cutoff ceiling, the thing FMAX assumed.

Rolloff corner (-3 dB), ours vs ReBirth vs our law:

| Freq | law | OURS | ReBirth |
|---|---|---|---|
| 96  | 2392  | 2353  | 2379  (control, matches) |
| 108 | 4807  | 4716  | 4893  (control, matches) |
| 116 | 7653  | 7520  | 8290 |
| 120 | 9657  | 9507  | 11273 |
| 123 | 11497 | 11343 | 15116 |
| 127 | 14508 | 14368 | open (>18 kHz, -0.9 dB at 18 kHz) |

ReBirth's cutoff at high Freq is MORE open than ours, not less: at Freq 127 it
passes full band while ours corners ~14.4 kHz. So the FMAX premise (a ~9-10 kHz
ceiling) is the opposite of the truth. FMAX moved the chirp metric by lifting
the mean centroid, not by matching ReBirth's mechanism. **DO NOT SHIP FMAX;**
do not re-propose a cutoff ceiling.

## Two conclusions this leaves

1. **The chirp gap is provably NOT a cutoff-position or cutoff-law effect.**
   ReBirth's cutoff law is more open than ours at the top, yet ReBirth's swept
   chirp peaks LOWER (in-band) than ours. Nothing in the cutoff mapping can
   reconcile "more open statically" with "less open dynamically". The gap is an
   envelope-DYNAMICS effect (the envelope reaches a lower peak cutoff on a fast
   sweep than a 1:1 static map would give) plus the resonance ring (B). The
   position-family levers (FMAX, and PLAN_PCF_RESONANCE's offset / depth-as-
   cutoff) are all ruled out. The obvious envelope-dynamics models (one-pole
   attack, excursion saturation) are falsified above. What remains untried is a
   genuinely different envelope shape or a ringing resonance core, and the 303
   precedent says the ring is in structural tension with static parity. The
   swept-resonance stays a documented residual (PCF_FINDINGS 13).

2. **NEW measured finding, separable from the swept work: our cutoff LAW is
   slightly too LOW / too steep above ~8 kHz.** Our fit (drone, bytes 48..127)
   could not see above 8 kHz and extrapolated the top; the broadband corner
   measurement shows ReBirth stays more open (Freq 120: 11.3 kHz vs our 9.5;
   Freq 127: open vs our 14.4). This is Amt-0 static in a rarely-used high-Freq
   region, low audible weight, and it points OPPOSITE to the chirp (opening our
   top MORE would make the chirp over-open worse), so it is not part of the
   swept fix. Logged for Luke's call; a refit of the cutoff-law top from
   `pcf-ceiling` would close it. Candidate for BACKLOG / PCF_FINDINGS on the
   mainline.

## Flags (all default off, inert unflagged)

`REBORN_PCF_ATK`, `REBORN_PCF_SAT`, `REBORN_PCF_FMAX` in `src/effects/pcf.cpp`.
Kept for the record; none is a candidate to merge.
