# Concluded experiments

Three investigations were run on branches of their own and never merged. Their code
was default-off `REBORN_*` probes, so nothing shipped changes without them. The
branches stay in the author's private history; what they found is kept here, with
each one's plan document beside this file and its code change as a patch in
`experiments/`, so the work need not be repeated.

## Coupled-ladder retriggered chirp (2026-09-04 to 05)

Plan: `PLAN_DIODE_CHIRP.md`. Patch: `experiments/diode-ladder-chirp.patch`
(`src/devices/bass_synth.cpp`, behind `REBORN_FILTER=rb|diode|zd`).

The question was whether a coupled filter core could reproduce the 303's
retriggered "round chirp", the per-note rise of the resonant peak on fast
retriggered notes, which the shipped zero-delay TPT core renders as a flat accent.
Three cores were built: a forward-Euler coupled ladder (`rb`), a backward-Euler
diode ladder (`diode`) and a zero-delay diode ladder (`zd`).

Verdict: the ride and fittability are in structural tension. The chirp's ring-up is
a marginally stable, loop-delayed forward-Euler effect. Only the `rb` core rides it,
and its band shape is about twice as far from ReBirth's as the shipped voice on an
energy-weighted third-octave error (13.8 against 6.9 mean dB). Any core stable and
decoupled enough to fit removes the dynamics that produce the ride. The shipped
voice was kept and the mid-resonance retriggered curve is a documented residual
(`BASS303_FINDINGS.md`). Open routes, neither tried: un-darken the `rb` core, or a
full nonlinear diode network deliberately tuned near instability.

## PCF swept resonance, envelope attack and cutoff ceiling (2026-09-10)

Plan: `PLAN_PCF_ATTACK.md`. Patch: `experiments/pcf-attack.patch`
(`src/effects/pcf.cpp`, behind `REBORN_PCF_ATK`, `REBORN_PCF_SAT`,
`REBORN_PCF_FMAX`). Fixture generator: `tools/rbsgen/gen_pcf_ceiling.py` (its later, denser form, which the cutoff refit used).

Three co-models of the PCF's swept "chirp" were tried: a finite envelope attack, an
excursion saturation, and a cutoff ceiling above an 8 kHz knee. The ceiling looked
strong on the chirp, but a direct broadband measurement showed ReBirth's cutoff is
MORE open at the top than ours (open past 18 kHz at Freq 127, against our 14.4 kHz
corner), so the ceiling helped for the wrong reason. Do not propose a cutoff
ceiling again. Superseded: the PCF was later matched by a different route, the
depth-capped envelope, the S-shaped velocity curve and the attack glide
(`PCF_FINDINGS.md` 21 to 26, signed off 2026-09-16).

## PCF resonance dig (2026-09-06 to 07)

Plan: `PLAN_PCF_RESONANCE.md`. Patch: `experiments/pcf-resonance.patch`
(`src/effects/pcf.cpp`, behind `REBORN_PCF_CORE` and related probes). Fixture
generators: `tools/rbsgen/gen_pcf_fsweep.py`, `tools/rbsgen/gen_pcf_makeup.py`.

Ruled out, each by measurement: a wrong resonance makeup (ours matches ReBirth across
cutoff at Q0 and Q96), a too-high maximum cutoff, a resonance-core rewrite (a
delayed-feedback Chamberlin SVF behaves like the TPT), and a cutoff-dependent
envelope depth (does not converge). Found: an envelope offset that fixes the
low-cutoff over-opening, and that the remaining pattern deficit follows trigger
density. Both findings fed the later envelope work that closed the PCF.
