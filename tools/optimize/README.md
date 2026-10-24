# reborn-optimize

A deterministic-optimiser driver for fitting Reborn 303 parameters to a real
ReBirth export. No LLM in the inner loop: it renders, measures, and searches on
plain CPU, reproducibly, at zero token cost. Drop it in at `tools/optimize/`.

## The contract

Everything hangs off one function, `f(params) -> distance`:

1. **Render** (`harness.py`). Set the `REBORN_*` environment variables from
   `params`, then run `reborn_render` with fixed, deterministic base arguments.
2. **Guard** (`harness.py`). Refuse to score unless the methodology invariants
   hold: the isolation and effects-off flags are present in `base_args`, and the
   output has the expected channel count (so a mono/stereo regression cannot pass
   silently).
3. **Measure** (`metrics.py`). Score the render against the reference export.

The config file *is* the contract. `config.example.json` documents every field;
copy it to `config.json` and edit.

## Why 303-isolated, effects-off

`measure_ab.py` measures a whole mix, where the kick and hats dominate the low
and high bands and swamp the 303's low-mid shelf. So the reference and the
candidate must both be the 303 alone with effects off. `reborn_render` already
supports this: `--solo bass1 --no-fx`. Produce your reference export the same
way (solo the acid line, effects rack off), or the objective is measuring the
wrong thing. `required_args` in the config enforces those flags so a run cannot
quietly drop them.

## Setup

```
pip install -r requirements.txt          # numpy + scipy; add cma only for method=cma
cmake --build build --config Release      # so ./build/Release/reborn_render exists
```

Create a 303-solo, effects-off reference export from the original program and
point `reference` at it.

## Run

```
python optimize.py --config config.json --method auto --max-evals 200
```

Methods:
- `auto` (default): coarse grid to find the neighbourhood, then Nelder-Mead to
  polish. This is the Component A workflow.
- `grid`: lattice sweep (numpy only).
- `neldermead`: local polish from `init` (scipy).
- `cma`: CMA-ES for rugged, coupled landscapes (the `cma` package). For
  Component B, later.

Output lands in `workdir` (default `.opt_runs/`): `trials.jsonl` (every
evaluation with per-band deltas), `best.json` (best params plus the `REBORN_*`
values to set), and `best_candidate.wav` (the best render, for ear-checking).
`--resume` seeds from an existing `best.json`.

## The objective, and a seam

The default objective, `band_distance`, is the weighted RMS of the per-octave-
band dB deltas. It mirrors `measure_ab.py`'s band computation exactly (same
edges, window, and hop), so the numbers line up with the A/B report you trust.

For the 303 brightness gap your methodology prefers the env-swept spectral
centroid rather than a static band balance. That metric lives in your own
tooling and is not reproduced here rather than guessed at. `metrics.py` has a
stub, `env_swept_centroid_distance`, and a registry; paste your implementation
in, set `objective.type` to `env_swept_centroid`, and the driver picks it up
unchanged. Until then, weight the low-mid bands (see the `band_weights` hint in
the config) to focus `band_distance` on the shelf region.

## Component A vs Component B

Component A (the resonance-independent low-mid shelf) is a few smooth knobs and
is safe to optimise now: `auto` will handle it. Component B (the coupled diode
ladder) is a CMA-ES-shaped problem, but hold the optimiser until the
discretisation is derived from D'Angelo-Fontana and D'Angelo-Valimaki. An
optimiser fits the constants of a structure; it cannot fix a wrong topology, and
turning CMA-ES loose on the unresolved ladder will hand you a confident set of
numbers that sound wrong, which is exactly the overfitting the
compare-to-real-export discipline exists to catch.

## Guardrails baked in

- The reference must exist, or the run refuses to start.
- `required_args` must be present in `base_args`, keeping the 303-isolated,
  effects-off invariant from being dropped.
- Every render's channel count is checked against `expect_channels`.
- The master compressor must be off for parity renders. `--no-fx` is the
  documented effects-off path; confirm it disables the compressor in your build,
  and if there is a separate toggle, add it to `base_args` or `env_fixed`.
- Renders are deterministic (fixed note pattern, seconds, and sample rate), so
  the optimiser is chasing parameters, not render noise.
- The ear is the final gate. The optimiser proposes; you sign off by listening,
  same as the VCA pluck fix.

## A note found while wiring this

`measure_ab.py`'s `load()` downmixes when `channels == 2 OR the sample count is
even`, so a mono file with an even sample count would be folded into fake
stereo. `metrics.py` keys off the real channel count only. If you refactor
`measure_ab.py` to import `load_wav`/`band_energies` from `metrics.py` (one
source of truth for the measurement, shared by this driver and any Claude-side
tool), that latent bug goes away.

## Files

| File | Role |
|------|------|
| `metrics.py` | The objective: load, band energies, `band_distance`, the centroid seam |
| `harness.py` | `f(params)`: render, guard, measure |
| `optimize.py` | The driver: grid / Nelder-Mead / CMA-ES / auto, logging, resume |
| `config.example.json` | The contract, documented |
| `requirements.txt` | Dependencies, per method |

---

## Added 2026-07-31: the harmonic-curve objective, and a noise-floor gate

Two changes, both from the 303 filter round recorded in
`docs/research/FINDINGS_AUDIT.md`.

### harmonic_curve, a better objective for the filter

`band_distance` is ten numbers. `docs/research/RUNBOOK.md` section 7 records what
summary statistics have cost here: the old plucky VCA matched ReBirth on
envelope coefficient-of-variation AND silence fraction and was still the wrong
shape entirely.

For a filter there is a better instrument. These fixtures play a sawtooth at f0
on both sides and `tools/analyze/wave_fit.py` established ReBirth's saw is an
ideal 1/k series, so `sqrt(PSD at harmonic k) * k` recovers the filter's
magnitude response sampled every f0 Hz with the source divided out. Comparing
those curves point by point is thousands of constraints rather than ten, and it
cannot be gamed by trading one band against another. Set `objective.type` to
`harmonic_curve`.

It takes no weights, deliberately. A weight vector is a free parameter and this
project has a standing rule against buying small gains with free parameters.

### noise_floor(), and why it is the important half

**Calibrate before you search.** ReBirth's exports are not sample-deterministic,
so scoring two exports of the SAME fixture against each other gives the
objective's own resolution. A candidate at or below that number is not nearly
perfect, it is indistinguishable from a second export of the reference.

This is not hypothetical. On 2026-07-30 an estimator that looked healthy in a
single run swung 8.8 dB/oct between two exports of identical settings while
their octave-band energies agreed to 0.029 dB. A finding had already been
committed on it and had to be retracted. An optimiser makes this worse, not
better: it finds and exploits that noise far faster and more confidently than a
human sweeping by hand.

Measured on five independently exported duplicate pairs (`renders/batchS_rb`
against `renders/batchSW_rb`):

| cell | band_distance | harmonic_curve |
|---|---|---|
| cut110 reso60 | 0.149 | 0.110 |
| cut110 reso90 | 0.584 | 0.630 |
| cut127 reso60 | 0.769 | 1.684 |
| cut127 reso90 | 0.148 | 1.633 |
| cut127 reso127 | 0.717 | 3.178 |
| **worst** | **0.77** | **3.18** |

Two things to read off this. The floor is strongly CELL-DEPENDENT: cutoff 110
is worth 0.1 to 0.6 and cutoff 127 is worth 1.6 to 3.2, because at cutoff 127
the analysis band sits far above the corner where there is little signal. And
harmonic_curve has a HIGHER floor than band_distance, which is what being a
more sensitive instrument costs. It resolves more, and it resolves more of the
noise too.

So do not chase harmonic_curve below about 1.2 on a cutoff-127-heavy fixture
set, and re-measure the floor for any new fixture set rather than reusing these
numbers.

A sixth pair, cut110 reso127, came back BYTE-IDENTICAL across two batches. That
contradicts ReBirth's documented non-determinism, it is excluded from the table
above, and it is unexplained. Treat any duplicate pair scoring exactly zero as a
broken control rather than a perfect one.

### Corrections to the notes above

- The `measure_ab.py` load() bug this README flagged was real and is fixed. It
  was worse than described: for a mono file it averaged ADJACENT SAMPLES,
  halving the effective rate while sr stayed put, which shifts every frequency
  by an octave.
- `REBORN_AMP_DECAY` and `REBORN_AMP_SUS` are the RETIRED plucky-VCA constants.
  AMP_DECAY now defaults to 0.0 and the code marks it "0 = off". The live VCA is
  gate-and-release (`REBORN_GATE_FRAC` 0.500 plus a 22.2 ms release), fitted
  across five tempos. Setting AMP_DECAY=0.06 re-enables a shape that was
  measured wrong. Do not put those two in a search space.
- The Component A / Component B framing above comes from the 2026-07-22
  handoff, which `docs/research/RUNBOOK.md` marks superseded. The current worklist is
  `docs/COMPONENT_MATRIX.md`. The README's own warning stands and is more
  relevant than ever: an optimiser fits the constants of a structure and cannot
  fix a wrong topology, and the 303's open residual is now a topology statement
  (ReBirth's skirt shallows with resonance and ours does not).

### 2026-07-31, later: ReBirth has about SIX render states, not random phase

The single-pair `noise_floor()` above is superseded by `noise_floor_many()`.
Use the new one.

Six exports of one fixture, identical input, produced only **four distinct
outputs**: rep2 matched rep4 and rep3 matched rep6, while the differing renders
were sample-wise uncorrelated (r between -0.16 and +0.25). For N equally likely
states the expected distinct count in six draws is N(1-(1-1/N)^6), which is 3.99
at N=6. So ReBirth does not start from a random phase, it starts from a small
set of discrete states, roughly six of them.

That also predicts about one collision per six independent pairs, which is
precisely the puzzling byte-identical pair in the batch S duplicates. It was not
an anomaly, it was the expected rate.

**Why this breaks a single-pair floor.** Two exports collide outright about one
time in six, and a colliding pair scores exactly zero. A single-pair floor is
therefore a draw from a distribution containing zeros, and it will sometimes
report perfect precision. That happened here: the floor first recorded for
cut110 reso127 was 0.000, while the true spread over all fifteen pairs of that
same cell is a median of 1.43 and a max of 1.58. The table earlier in this file
understates every cell it happened to draw a collision on.

    harmonic_curve over 15 pairs:  min 0.000  median 1.425  max 1.577
    band_distance  over 15 pairs:  min 0.000  median 0.359  max 0.795

Use the MEDIAN as the working floor, the max as the pessimistic one, and never
a single pair.

**Null-testing stays forbidden.** Renders differ 13 times in 15. The rule in
`docs/MEASUREMENT_PROTOCOL.md` is unchanged.

**Averaging is the win.** `average_reference()` averages the harmonic CURVES of
N exports (never the waveforms: the states are uncorrelated, so summing them
cancels signal rather than noise). Per-harmonic spread falls as 1/sqrt(N):

    1 export  0.46 dB     3 exports  0.27 dB
    2 exports 0.32 dB     6 exports  0.19 dB

Three exports more than halves the floor for about 30 seconds of extra ReBirth
time. For any cell where precision matters, export it three times and average.
