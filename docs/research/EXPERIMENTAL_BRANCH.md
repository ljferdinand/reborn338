# Experimental work: where it lives and how it is kept safe

Rewritten 2026-08-04. This file previously described `exp/filter-probes`, a
branch from 2026-07-22/23 that has since been absorbed. It also carried two
statuses that had gone stale in opposite directions: the plucky VCA as a
"VALIDATED win, pending ear-check" (the ear check passed and it is on `main`),
and the diode ladder as a scaffold that "does not yet tune" (it is now a design
document, not a scaffold awaiting a fit).

## The live branch

**`303-env-domain`.** Everything experimental about the 303 is on it, and
nothing on it is merged. Its worklist is `PLAN_303_ENVELOPE.md`, which is the
entry point for the filter and leads with what is wrong rather than what was
found. `TASK_303_GESTURE_DEPTH.md` is the brief for the next piece of work.

Do not trust prose about which branch is live, here or anywhere else. It goes
stale within days on this project. Check `git branch --show-current` and
`git log`.

## How experiments are kept off the shipped build

Every experimental change in `src/devices/bass_synth.cpp` is a `REBORN_*`
environment probe that defaults to the shipped behaviour, and there are now
around ninety of them. The rule that makes this safe is the one worth stating:

**A default-off probe must be byte-identical to the shipped build.** Verify with
`tools/render/byte_neutral.py`, which renders the sixteen demo songs soloed to
both 303 voices and compares hashes. A probe that is byte-neutral in both
positions is not wired up, so check that it moves something when switched on.

That discipline is why probe work can sit on a branch indefinitely without
anyone having to remember which constants are "real". The defaults in the code
are the answer to what ships.

## The two alternative filter cores

`REBORN_FILTER` selects between three cores in one binary, which is what makes
an A/B cheap:

- default, the calibrated TPT ladder, which is what ships
- `rb`, a ReBirth-era forward-Euler ladder with its own knob-to-Hz law
- `diode`, a backward-Euler diode-ladder scaffold

Neither alternative has been scored against the current curve metric. That
scoring is cheap and remains available if the constants refit stalls, but it is
no longer motivated by the skirt: that direction call was withdrawn on
2026-07-31 and the skirt is a refit, not a topology question. See
`PLAN_303_ENVELOPE.md` §0 for which parts of the filter are which.

## The coupled diode ladder

`reborn338_diode_ladder_design.md` is the design for the candidate structural
fix, built clean-room from published papers (Huovilainen 2004, D'Angelo and
Fontana, D'Angelo and Valimaki) and the published 303 schematic. It is a design
document rather than pending work.

What motivates it now is not the skirt but the resonant-peak PROMINENCE: our
peak stands 5 to 6 dB short at high resonance and cutoff, only the `K_B`
feedback moves it, and `K_B` is already at 3.90 against a self-oscillation limit
of 4.0. Something must supply Q without costing corner position and no constant
in the current model does. `TASK_303_GESTURE_DEPTH.md` §5 carries this as the
§3.3 work along with its clean-room constraints.

## Clean-room, on every branch

The rule is about ReBirth, not a ban on reading other code: no decompiling or
transcribing ReBirth, at any point, experimental or not. Reading and adapting
MIT-licensed and free/libre 303 emulators (Open303 and the like) is now
permitted, under their own licenses and with attribution (see
`docs/PROVENANCE.md`). For the ReBirth-PARITY engine the legitimate inputs remain
published DSP literature, the published schematic, and black-box ReBirth exports,
because Open303 emulates the hardware 303 rather than ReBirth: it is a
synthesis/product input, never a parity input.
