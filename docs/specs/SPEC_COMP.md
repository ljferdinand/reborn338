# Compressor: behavioural specification for the implementer

You are implementing ReBirth's master compressor to match measured behaviour.
Work ONLY from this document, `docs/research/PLAN_COMP.md` (the measured facts and full
tables), the measured audio in `renders/comp-law-2026-09-16/`,
`renders/comp-law2-2026-09-16/`, `renders/comp-law3-2026-09-16/`,
`renders/comp-law4-2026-09-16/` and `renders/comp-ballistics2-2026-09-16/`, and
the existing `reborn/` source. Those are your only inputs. Do not consult any
third-party analysis, disassembly or decompilation of ReBirth; every requirement
below was measured from ReBirth's own audio output, and you fit your own
arithmetic to the measured numbers.

The file to change is `src/effects/compressor.cpp`. The decode and engine wiring
are already done: the loader reads the COMP track's Ratio and Threshold bytes as
`byte/127` and calls `setParamByName("ratio", ...)` and
`setParamByName("threshold", ...)`. Keep those two param names reachable. All
other controls are INTERNAL (see below).

## What ReBirth's compressor actually is

**Superseded in part by "Round 5" and "Round 5 result" at the end of this
document.** The measured facts below stand; the soft-knee form they were read
through is the round-2 MODEL, which round 5 replaced with a peak-follower
detector and a linear gain law.

Measured, not the manual. The manual says "levels below the threshold pass
unprocessed"; measurement shows the opposite at low threshold (quiet signals are
boosted). The measured static law is a soft-knee feed-forward DOWNWARD compressor
with AUTOMATIC make-up gain:

```
GR_dB(level) = makeup(ratio, threshold) - slope(ratio) * max(0, level - threshold)
out = level - GR_applied      (GR_applied is GR_dB after the ballistics smoother)
```

- Below threshold, `max(0, level-threshold)=0`, so `GR = makeup` (a FLAT offset,
  independent of input). Above threshold, GR falls with input at `slope(ratio)`.
- The "leveller toward a target" character is the make-up: at low threshold /
  high ratio it is large and positive, so quiet signals come out boosted. This is
  the behaviour to reproduce, not the manual's.

## What the current compressor.cpp gets wrong

`src/effects/compressor.cpp` is a textbook design with GUESSED ranges. All of
these must be refit or removed:

- **threshold maps `-40..0 dB`** (guessed). Measured threshold is a concave
  (~log) law, roughly `-32 dBFS` at byte 0 rising to `-19 dBFS` at byte 127.
- **ratio maps `1..20:1`** (guessed). Refit to the measured GR-vs-ratio curve;
  byte 0 must be EXACT unity (bypass), byte 127 a strong ratio.
- **make-up is a USER param (`0..18 dB`)**. ReBirth has NO make-up control.
  Make-up must become AUTOMATIC, a function of ratio and threshold, and it is what
  produces the below-threshold boost. Remove the user make-up param.
- **attack `0.1..100 ms`, release `20..1000 ms` as user params**. ReBirth exposes
  neither; they are internal constants. Set them to the measured values below and
  remove them as `.rbs`-driven params (they may stay as fixed internals).
- **fixed 6 dB knee**: refit the knee width to the measured plane.

Keep: the stereo-linked detector (the master compressor is stereo and applies the
same gain to both channels), the dB<->linear math, and all the stability guards
(floors, clamps, denormal flush, self-healing writes).

## The measured behaviour to reproduce

All levels are dBFS (power), steady-state RMS over a 2-6 s window, one 303 drone
through the master compressor. Input levels come from the fader: fader
127/100/80/60/40 = **-15.80 / -25.79 / -33.18 / -40.58 / -47.98 dBFS**.

**1. Ratio law** (threshold byte 0, input -15.80). GR (dB) vs ratio byte:

| byte | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|---|
| GR | 0.00 | -0.99 | -2.10 | -3.36 | -4.87 | -6.66 | -8.94 | -12.03 | -16.47 |

Byte 0 is exact bypass. (Analyzer: `comp_surface.py` on `comp-law2`.)

**2. Threshold law** (ratio byte 127, input -15.80). Output pins near the
threshold; output dBFS vs threshold byte:

| byte | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|---|
| out | -32.26 | -28.94 | -26.54 | -24.66 | -23.12 | -21.79 | -20.63 | -19.63 | -18.81 |

Concave (big steps low, small high; fits ~`A + B*ln(byte + ~29)`). The threshold
law is ratio-independent (confirmed at ratio 64, `comp-law3` `xr_*`).

**3. Make-up is real, scales with ratio, and is 0 at ratio 0.** At ratio 0,
sweeping threshold does NOTHING (bypass; `comp-law3` `mk_*` all GR 0.00). So the
make-up (and all processing) scales from nothing at ratio 0.

**4. The transfer surface** (ratio 96 plane, `comp-law4`). GR (out - in) by
(threshold byte, input dBFS):

```
input:    -15.80  -25.79  -33.18  -40.58  -47.99
thr  0:    -8.92   -4.64   +0.31   +6.40  +13.19
thr 32:    -6.67   -0.68   +1.10   +1.10   +1.10
thr 64:    -4.88   -0.72   -0.72   -0.72   -0.72
thr 96:    -3.38   -1.43   -1.43   -1.43   -1.43
thr127:    -2.16   -1.81   -1.81   -1.81   -1.81
```

Read it as: below threshold GR is a flat offset (= make-up); above threshold GR
falls with input at the ratio slope. Make-up per threshold (the flat below-knee
value) at ratio 96: `+1.10 / -0.72 / -1.43 / -1.81 dB` at thr 32/64/96/127, and
large-positive at thr 0 (its knee is below -48 so every input is above it). Fit
`makeup(ratio, threshold)` to reproduce this AND the ratio-127 boost (`comp-law2`
`bt_*`: at ratio 127 thr 0 a -40.6 input is lifted to -32.8, +7.76 dB; spectrally
a clean broadband gain, same drone spectrum).

**5. Ballistics** (`comp-ballistics2`, drum transients; analyzer
`comp_ballistics_drum.py`). Timing is ratio-independent.
- **Attack fast, ~10-20 ms:** at a loud onset the gain clamps to full reduction
  within ~30 ms (most in the first ~10 ms). Fit a one-pole whose gain reaches
  ~full reduction within ~30 ms.
- **Release ~100-200 ms:** after a loud kick the gain recovers to ~unity by
  ~250 ms (measured -3.2 dB at 125 ms -> -0.2 dB at 250 ms on the pumping probe).

## Model structure (what reproduces the measurements)

A feed-forward compressor, per sample:
1. **Detector:** stereo-linked level (e.g. the louder squared channel, as now),
   smoothed enough to read as level; the same gain goes to both channels.
2. **Static gain computer (dB):** `GR = makeup(ratio,threshold) - slope(ratio) *
   softknee(level - threshold)`, where `slope(ratio)` and `threshold(threshold)`
   and `makeup(ratio,threshold)` are the three fitted laws, and `softknee` is a
   knee of fitted width. `slope`/`makeup` must vanish at ratio 0 (exact bypass).
3. **Ballistics:** branching one-pole on GR (attack coeff while GR rising,
   release while falling), attack ~10-20 ms, release ~100-200 ms.
4. **Apply:** `gain = dbToLin(-GR_smoothed)`, multiply both channels equally.

The two live params are `ratio` and `threshold` (0..1 = byte/127, already fed by
the engine). Everything else is an internal constant or fitted law.

## Acceptance tests (Role A will independently re-run these)

Regenerate every fixture and render it with your build, then compare against the
committed ReBirth renders with the SAME analyzers:

```
python tools/rbsgen/gen_comp_law.py        # -> fixtures-2026-09-16/comp-law
python tools/rbsgen/gen_comp_law2.py       # -> .../comp-law2
python tools/rbsgen/gen_comp_law3.py       # -> .../comp-law3
python tools/rbsgen/gen_comp_law4.py       # -> .../comp-law4
python tools/rbsgen/gen_comp_ballistics2.py# -> .../comp-ballistics2
# render each fixture:  reborn_render --song <fixture.rbs> --out <our.wav>
# compare with the ReBirth renders using the matching analyzer:
python tools/measure/comp_curve.py    <our-renders-of comp-law>
python tools/measure/comp_surface.py  <our-renders-of comp-law2>
python tools/measure/comp_ballistics_drum.py attack  <our hit_off> <our hit_r127_t064>
```

Targets (our render vs the ReBirth render, same analyzer):
- **Ratio 0 = exact bypass** (GR 0.00, output == input to within measurement).
- **Ratio law** GR curve (table 1) within ~0.7 dB at every byte.
- **Threshold law** pin (table 2) within ~0.7 dB at every byte.
- **Transfer plane** (table 4) GR surface within ~1.0 dB at every cell,
  INCLUDING the below-threshold flat offsets and the thr-0 boost.
- **Ballistics:** attack reaches full reduction within ~30 ms; release recovered
  by ~250 ms.
- `ctest` green; a non-comp render (compressor off) stays byte-identical to
  before your change (the compressor must be inert when off).

An A/B env flag `REBORN_COMP=old` that restores the current textbook DSP is
welcome for validation but not required (the compressor has never shipped on).

## Provenance

Every number above was measured from ReBirth exports (clean-room). Fit your own
coefficients; do not copy constants from any external source. Record what you fit
and the residuals you achieve.

## Round 5 (2026-10-01): the level detector

Read `docs/research/PLAN_COMP.md`, section "Material probe (2026-10-01): the detector
reads PEAKS, not RMS", first. Everything above this section stays in force; this
round changes one thing.

**The finding.** The fit so far used a single source, a slid bright saw drone.
Measured on seven sources at identical settings, ReBirth's detector reads steady
material by its PEAK level, not its RMS: relative to the saw, the square is read
about 2.9 dB lower than RMS would, a resonant drone about 2.8 dB higher, a dull
near-sine drone about 3.5 dB lower, which is what each source's crest factor
predicts for a peak detector to within about 0.6 dB. Ours is a 25 ms RMS detector,
so it misses those sources by about 3 dB at ratio 127. On moving material
(staccato 303 notes, a 909 kick every beat, a 909 closed hat every 16th) the
misses reach 7.6 dB and also depend on how the detector rises on a hit and decays
between hits, which you fit.

**What to change.** Only the level detector in `src/effects/compressor.cpp`, plus
whatever the static law needs to keep matching the saw once the detector reads it
differently (a peak reading of the saw sits above its RMS, so the threshold law
moves by about the saw's crest factor and must be re-expressed). Keep:
- the detector's INPUT, the mean of the two channels, `(l + r) / 2` (measured
  2026-10-01: a hard-panned source reads about 6 dB below the same one centred);
- the gain computer's shape and the gain ballistics unless the data require a
  change, and say so if they do;
- ratio 0 as an exact bypass;
- the param names `ratio` and `threshold`.
Do NOT touch `src/engine/engine.cpp`: the engine feeds the compressor at measured
levels (the master bus times the 0.72 headroom; a section at its fader-127 level
times 0.5), and compensating there would break measured topology. The compressor
must stay inert when off.

**Inputs.** Fixtures `fixtures-2026-10-01/comp-material` (generator
`tools/rbsgen/gen_comp_material.py`) and ReBirth's exports
`renders/comp-material-2026-10-01`, plus everything listed at the top of this
spec, plus `fixtures-2026-10-01/comp-rides` with `renders/comp-rides-2026-10-01`.
`comp_wav` (tools/render/comp_wav_main.cpp) runs the compressor alone over a WAV
and is exact for the master path; it is the fastest fitting loop, since feeding
ReBirth's own uncompressed export through it removes every source difference.

**Acceptance (Role A re-runs all of it).**
- `python tools/measure/comp_material.py renders/comp-material-2026-10-01 --comp-wav <your comp_wav>`:
  every STEADY row (saw, square, reso, dull) within 1.0 dB at all four settings;
  every MOVING row (stacc, kick, hats) within 1.5 dB at all four settings, or
  report the closest you reach and why.
- The earlier static campaigns through `comp_wav` on ReBirth's uncompressed
  exports: no cell of comp-law to comp-law6 more than 0.3 dB worse than round 2,
  and at least as many cells within 1.0 dB (round 2: 129 of 152). Build
  `comp_wav` once BEFORE you change anything and keep that copy as the round-2
  baseline, then run `python tools/measure/comp_law_tally.py --comp-wav <yours>
  --baseline <round-2 copy>`; it lists every cell that got worse.
- comp-ballistics2: full reduction within ~30 ms of a hit; recovered by ~250 ms.
- comp-rides through the engine (`tools/measure/comp_rides.py
  renders/comp-rides-2026-10-01 <your renders>`, REBORN_BASS_ENGINE=tpt): every
  compressed cell within 1.0 dB once the TPT drone's documented +0.94 dB bypass
  residual is accounted for as `PARITY.md` does; record the four ride t90s.
- `ctest` green; a compressor-off render byte-identical to before.

Record what you fit and the residuals you reach, in this document's style.

## Round 5 result (2026-10-01, walled implementer, branch `comp-detector-cleanroom`)

Fit from the inputs this spec names and nothing else: the comp-law to comp-law6
static cells, `comp-material`, `comp-ballistics2` and `comp-rides`, each through
`comp_wav` (ReBirth's own uncompressed export fed through our DSP) or, for the
rides, through the engine. `src/effects/compressor.cpp` carries the model and
the constants; this section carries the tables.

### What was fit

**The detector.** A peak follower on the lowpassed mid signal:
`lp` = `(l + r) / 2` through two cascaded one-pole lowpasses (24.26 us each,
-3 dB at about 4.45 kHz together at 44.1 kHz, 4.2 kHz being the analog
prototype of that tau); `env` rises toward `|lp|` with a one-pole attack
(8.94 ms) when `|lp|` exceeds it and restarts a 26.29 ms hold; after the hold it
falls at 150.9 dB/s; it never falls below the threshold floor `F`. There is no
smoother on the gain: the gain follows `env` sample by sample, and all of the
timing is in the detector. Fit by differential evolution on a simulation of
exactly this structure, jointly to the 28 material GRs, to ReBirth's gain
TRAJECTORIES on the 909 kick and hat at the four material settings (on over off,
1 ms windows folded over 8 beats; those exports are sample-aligned), and to the
three comp-ballistics2 trajectories.

What the trajectories showed, which a GR total alone cannot: after a hit
ReBirth's gain sits FLAT, then rises at a constant rate in dB (0.156 dB/ms at
ratio 127 threshold 0); at ratio 64 the same release rises at a rate that grows
with the gain (0.07 to 0.12 dB/ms); the release starts EARLIER at a higher
threshold (the hat: 39.9 ms after the hit at threshold 32, 47.9 ms at threshold
0); and a ride that removes 16 dB of cut lands within 15 ms. A held, dB-linear
envelope floored at `F`, read through a linear gain law, gives all four; a
smoother on the gain cannot give the last one together with the first.

**The static law, changed, and why.** The new detector releases, between hits,
to the gain the law gives at `env = F`, a below-threshold value the 2026-09
campaign never measured at ratio 64. Round 2's dB law extrapolated +8.8 dB
there; the kick tails in comp-material show +24.1 dB. So the law had to be
re-examined, and the 146 compressed static cells (ratio > 0) turn out to obey,
in the saw's RMS units, exactly

```
g = (1 - k) + k * T / max(D, F)        (linear gain)
k = 0.98996 * ratioByte / 127
T = 0.0230948 + 0.00072119 * thrByte   (-32.73 dBFS at byte 0, -18.81 at 127)
F = 0.00120028 * (thrByte + 0.62096)   (-62.55 dBFS at byte 0, -16.30 at 127)
```

five constants fit by least squares to all 146 cells (rms 0.005 dB, worst 0.023
dB) and to the two kick-tail ceilings (ratio 127 +29.72 dB, ratio 64 +24.08 dB;
both within 0.02 dB). It predicts the ratio-64 ceiling from the static cells
alone. In dB its slope is near 1 just above `F` and relaxes toward `1 - k` far
above it, which is the low-threshold "hump" rounds 3 and 4 could not make with a
soft knee. Ratio 0 is `k = 0`, `g = 1`, an exact bypass, and the DSP does not
apply a gain there at all. The detector reads in its own (peak) units, so `T`
and `F` are multiplied by the saw's peak over RMS as the detector reads it,
8.135 dB (fit). This is a larger change to the static law than "re-express the
threshold": it is reported here, as the brief asks, rather than presented as a
detector-only change.

Kept: the detector's input `(l + r) / 2`, one gain for both channels, the param
names `ratio` and `threshold`, ratio 0 an exact bypass, the compressor inert
when off, and `src/engine/engine.cpp` untouched.

### comp-material (comp_material.py, all 7 sources x 4 settings)

Miss = our GR minus ReBirth's, dB. L and R were measured separately only on
ReBirth's side (they agree to 0.00 in every row there); on ours the agreement is
guaranteed by `comp_wav`, which reads its input mono and feeds L = R, so it is
not a finding about the DSP.

| source | setting | ReBirth GR | round 2 miss | round 5 GR | round 5 miss |
|---|---|---:|---:|---:|---:|
| saw | r127_t000 | -16.45 | -0.62 | -16.44 | +0.02 |
| saw | r127_t032 | -10.72 | -0.64 | -10.69 | +0.04 |
| saw | r127_t064 | -7.31 | -0.48 | -7.27 | +0.05 |
| saw | r064_t000 | -4.86 | -0.19 | -4.85 | +0.00 |
| square | r127_t000 | -13.35 | -2.83 | -13.32 | +0.03 |
| square | r127_t032 | -7.53 | -2.94 | -7.51 | +0.02 |
| square | r127_t064 | -4.07 | -2.83 | -4.06 | +0.01 |
| square | r064_t000 | -4.38 | -0.41 | -4.37 | +0.01 |
| reso | r127_t000 | -16.40 | +2.81 | -16.44 | -0.04 |
| reso | r127_t032 | -10.67 | +2.80 | -10.72 | -0.04 |
| reso | r127_t064 | -7.24 | +2.94 | -7.30 | -0.06 |
| reso | r064_t000 | -4.85 | +0.83 | -4.85 | -0.00 |
| dull | r127_t000 | -5.11 | -3.36 | -5.06 | +0.05 |
| dull | r127_t032 | +0.84 | -3.59 | +0.88 | +0.05 |
| dull | r127_t064 | -0.97 | -0.18 | -0.98 | -0.00 |
| dull | r064_t000 | -2.20 | -0.30 | -2.19 | +0.02 |
| stacc | r127_t000 | -11.81 | -3.28 | -11.85 | -0.04 |
| stacc | r127_t032 | -5.95 | -3.43 | -6.00 | -0.05 |
| stacc | r127_t064 | -3.47 | -2.46 | -3.44 | +0.03 |
| stacc | r064_t000 | -4.13 | -0.39 | -4.11 | +0.01 |
| kick | r127_t000 | -2.38 | +3.24 | -2.23 | +0.15 |
| kick | r127_t032 | -3.32 | -2.98 | -3.27 | +0.05 |
| kick | r127_t064 | -2.27 | -3.10 | -2.25 | +0.03 |
| kick | r064_t000 | -1.71 | +0.24 | -1.60 | +0.11 |
| hats | r127_t000 | +3.51 | -7.56 | +3.53 | +0.02 |
| hats | r127_t032 | +0.64 | -2.05 | +0.80 | +0.17 |
| hats | r127_t064 | -1.00 | -0.33 | -1.01 | -0.00 |
| hats | r064_t000 | +1.85 | -3.32 | +1.86 | +0.02 |

Steady rows (target 1.0 dB): worst 0.06. Moving rows (target 1.5 dB): worst
0.17. The stacc exports are not sample-aligned (the notes' oscillator phase
differs between on and off), so the stacc rows were fit as GR totals only.

### Gain trajectories (comp_wav output, rms of ours minus ReBirth's gain)

Windows where the uncompressed source is above -45 dB.

| trajectory | round 5 rms / max, dB | round 2 rms / max, dB |
|---|---:|---:|
| kick r127_t000 (1 ms, 8 beats) | 0.08 / 0.44 | 9.90 / 14.60 |
| kick r127_t032 | 0.05 / 0.16 | 6.33 / 11.10 |
| kick r127_t064 | 0.03 / 0.15 | 3.72 / 6.75 |
| kick r064_t000 | 0.05 / 0.43 | 4.52 / 13.62 |
| hats r127_t000 | 0.39 / 0.72 | 9.17 / 10.38 |
| hats r127_t032 | 0.21 / 0.39 | 4.55 / 5.54 |
| hats r127_t064 | 0.01 / 0.05 | 0.50 / 0.68 |
| hats r064_t000 | 0.21 / 0.41 | 2.92 / 6.35 |
| ballistics2 hit r127_t064 (2 ms, per bar) | 0.16 / 0.93 | |
| ballistics2 hit r096_t064 | 0.11 / 1.10 | |
| ballistics2 pump r096_t048 | 0.06 / 0.19 | |

The largest left is the hat at ratio 127 threshold 0: ours holds about 0.3 dB
less cut through the hold and starts its release about 8 ms later than ReBirth's
47.9 ms. The 1 dB maxima on the ballistics2 hits are single 2 ms windows inside
the 808 stack's attack.

### Static campaigns (comp_law_tally.py, baseline = the round-2 comp_wav)

| campaign | n | round 5 median / max | round 2 median / max | round 2 over 1 dB |
|---|---:|---:|---:|---:|
| comp-law | 14 | 0.02 / 0.03 | 0.64 / 2.17 | 3 |
| comp-law2 | 23 | 0.01 / 0.03 | 0.47 / 1.09 | 2 |
| comp-law3 | 15 | 0.01 / 0.02 | 0.10 / 0.49 | 0 |
| comp-law4 | 25 | 0.01 / 0.03 | 0.13 / 4.20 | 3 |
| comp-law5 | 25 | 0.01 / 0.03 | 0.49 / 4.20 | 8 |
| comp-law6 | 50 | 0.01 / 0.03 | 0.28 / 2.13 | 7 |

152 of 152 cells within 1.0 dB (round 2: 129), all within 0.03 dB. Cells worse
than round 2 by more than 0.3 dB: none. The ratio-96 threshold-0 corner
(BACKLOG 16), -4.20 dB in round 2 (gp_r096_t000_v040), is -0.03 dB.

### comp-ballistics2 (comp_ballistics_drum.py)

Attack, hit_r127_t064: ours -5.80 dB at 4 ms, -8.28 at 10 ms, -9.90 at 26 ms,
-10.46 at 34 ms, settled -10.58 (ReBirth -5.53, -8.58, -10.27, -10.90, -10.87):
full reduction within about 30 ms. Release, pump_r096_t048: ours -3.12 dB at 125
ms, -0.15 at 250 ms (ReBirth -3.22, -0.23): recovered by 250 ms.

### comp-rides (through the engine, REBORN_BASS_ENGINE=tpt, 8 s)

Corrected as `PARITY.md` does: `comp_wav` run on both uncompressed drones at the
cell's setting (scaled by -g dB and put back, g = 0 on the master, 6.02 dB for a
section or a hard-left master cell; the uncompressed fader offset added for the
mt_ and v090 cells), and that source difference taken out of the raw miss. L
channel; R is the same or silent.

| cell | ReBirth | ours | raw | source | corrected | round 2 corrected |
|---|---:|---:|---:|---:|---:|---:|
| cr_ref_r127_t000 | -32.21 | -29.98 | +2.23 | +2.24 | -0.02 | -0.66 |
| cr_ref_r127_t127 | -18.75 | -17.33 | +1.42 | +1.44 | -0.02 | -0.58 |
| mp_r127_t000_p000 | -26.47 | -24.21 | +2.26 | +2.29 | -0.02 | -0.08 |
| mp_r127_t032_p000 | -20.61 | -18.32 | +2.29 | +2.23 | +0.06 | -0.24 |
| mp_r127_t064_p000 | -17.14 | -15.82 | +1.32 | +1.27 | +0.05 | -0.24 |
| mt_r127_t000_m070 | -43.31 | -41.09 | +2.22 | +2.24 | -0.02 | -0.66 |
| mt_r127_t000_m115 | -26.66 | -24.43 | +2.23 | +2.25 | -0.02 | -0.66 |
| mt_r127_t127_m070 | -29.86 | -28.44 | +1.42 | +1.43 | -0.01 | -0.57 |
| mt_r127_t127_m115 | -13.21 | -11.78 | +1.43 | +1.44 | -0.02 | -0.58 |
| st_r064_t000_p064_v127 | -19.64 | -18.36 | +1.28 | +1.27 | +0.01 | +0.66 |
| st_r127_t000_p000_v127 | -26.47 | -24.21 | +2.26 | +2.29 | -0.02 | -0.08 |
| st_r127_t000_p064_v090 | -40.16 | -37.91 | +2.25 | +2.27 | -0.02 | -0.08 |
| st_r127_t000_p064_v127 | -26.47 | -24.21 | +2.26 | +2.29 | -0.02 | -0.08 |
| st_r127_t016_p064_v127 | -23.05 | -20.78 | +2.28 | +2.22 | +0.06 | -0.53 |
| st_r127_t032_p064_v127 | -20.61 | -18.32 | +2.29 | +2.23 | +0.06 | -0.24 |
| st_r127_t048_p064_v127 | -18.75 | -16.42 | +2.33 | +2.25 | +0.07 | -0.11 |
| st_r127_t064_p064_v127 | -17.14 | -15.82 | +1.32 | +1.27 | +0.05 | -0.23 |
| st_r127_t096_p064_v127 | -17.75 | -16.81 | +0.94 | +0.94 | -0.00 | -0.45 |
| st_r127_t127_p000_v127 | -18.27 | -17.33 | +0.94 | +0.94 | -0.00 | -0.63 |
| st_r127_t127_p064_v090 | -31.95 | -31.03 | +0.93 | +0.93 | -0.00 | -0.63 |
| st_r127_t127_p064_v127 | -18.29 | -17.33 | +0.95 | +0.94 | +0.01 | -0.62 |

The four rides land at their static cells the same way (corrected -0.02 to
+0.01). Ride t90s (comp_rides.py, 10 ms windows), ratio down / ratio up /
threshold down / threshold up: ours 5 / 15 / 15 / 5 ms, ReBirth 15 / 15 / 25 /
15, round 2 95 / 15 / 15 / 85.

**Every compressed cell within 0.07 dB corrected (round 2: 0.66). The RAW
misses grew, from -0.67 to +1.33 in round 2 to +0.93 to +2.33 now, and that is
the 303, not the compressor.** Our TPT drone is 0.94 dB hotter than ReBirth's in
RMS, as documented, but its PEAK is 1.37 dB LOWER: crest 5.83 dB against
ReBirth's 8.16 (`cr_ref_r000_t000`, 2.0 to 6.0 s). The RMS detector saw only the
first; a peak detector sees both, so wherever the detector sits above the knee
our drone is read 1.37 dB low and cut about that much less. Through the engine
on the TPT voice, compressed 303 material now comes out up to about 2.3 dB hot
against ReBirth until the TPT saw's crest is fixed (BACKLOG 22). This is the one
place the round trades a passing raw number for a better corrected one, and it is
stated here so it is not mistaken for a compressor regression.

### Other checks

`ctest` green (2 of 2). The new test
`test_compressor_detector_reads_peaks_and_releases_linearly_in_db` pins a square
read 3 dB below a sine of the same RMS and a held, dB-linear release; it fails
three checks on the round-2 compressor. `test_section_compressor_precedes_the_fader`
now skips the samples the master clamp cuts at fader 127 (54 of 88200: the
measured +29.7 dB ceiling lets an 808 onset through boosted until the attack
catches it); on the rest the fader is linear to 3e-8. Compressor-off renders
byte-identical to round 2 (cm_saw_off, cm_hats_off, cm_kick_off,
st_off_p064_v127, mt_off_m115, TPT).

### What is not measured, or not met

- Every acceptance target above is met. The corrected-ride bar is met only in
  the corrected sense; the raw rides are worse, for the 303 reason above.
- The lowpass's ORDER is a fit choice: a single one-pole near 3.8 kHz fit almost
  as well (joint cost 0.60 against 0.49; hat trajectories 0.26 against 0.18 dB
  rms). No fixture separates them.
- The detector's input is still assumed to be the signed sum; `|l| + |r|` needs
  an antiphase source.
- The hat release timing (about 8 ms late at ratio 127 threshold 0) is the
  largest trajectory residual; the hat GR totals are within 0.17 dB regardless.
- Not checked against the demo songs, which are outside this round's inputs.

### Verification notes (added 2026-10-01, after independent review)

- **In-sample and out-of-sample.** The comp-material GRs, the gain
  trajectories and the comp-ballistics2 numbers above were fit inputs of the
  detector, and the 146 static cells were the fit inputs of the static law, so
  their residuals (0.17 dB or less, 0.03 dB or less, and the ballistics match)
  measure the fit on its own data. The independent evidence is the comp-rides
  cells, which were not a fit input (within 0.07 dB corrected), and the demo
  songs, measured by the verifier against `renders/demo-comp-check-2026-10-01`:
  on the KiloMix bars whose uncompressed sources match ReBirth's within 0.5 dB
  the mean |gain miss| went from 0.69 to 0.44 dB and the worst from 5.11 to
  1.06; five bar-channels got 0.3 to 0.4 dB worse (505 R, 525 R, 531 L and R,
  536 R), the worst of them now +0.93.
- **Hold-out check (Role A, 2026-10-01): the model generalises.** Instead of a
  hold-out refit, a set the fit never saw, in both sources and settings:
  `fixtures-2026-10-01/comp-holdout` (`tools/rbsgen/gen_comp_holdout.py`, 20
  fixtures), exports `renders/comp-holdout-2026-10-01`, read with
  `tools/measure/comp_material.py <exports> --prefix ch`. Five new sources (909
  snare on every beat, 909 ride on every 8th, 808 cowbell on every 8th, an acid
  303 line with env mod, accents and slides, a square staccato 303) at three new
  settings (ratio 127 threshold 16, ratio 96 threshold 48, ratio 112 threshold
  0). Miss, our GR minus ReBirth's, in dB, round 5 against round 2:

  | source | r127 t016 | r112 t000 | r096 t048 |
  |---|---:|---:|---:|
  | acid | -0.04 / -2.12 | +0.01 / -0.69 | +0.01 / -0.62 |
  | cowbell | +0.05 / -0.08 | +0.14 / -2.47 | -0.00 / -0.04 |
  | ride | -0.07 / -7.23 | +0.22 / -5.14 | -0.00 / -0.89 |
  | snare | +0.08 / +1.44 | +0.09 / +4.18 | +0.02 / -0.24 |
  | sqstac | +0.05 / -3.92 | +0.04 / -1.86 | +0.02 / -1.39 |

  Every hold-out cell is within 0.22 dB (worst: the ride at ratio 112 threshold
  0); round 2 missed the same cells by up to 7.23 dB. Our-side L = R by
  construction (`comp_wav`); ReBirth's L and R agree to 0.01 dB.
- **Demo songs, whole mix (Role A, 2026-10-01; corrected 2026-10-04).** Only
  the stock songs are evidence: Reborn plays every mod's drums as noise past the
  first kilobyte of each sample (BACKLOG 17), so the figures first recorded here
  for Alloy Cowboy (PBE2), Clodhopper and Groove It Up (MSM2), including the
  "compressor gain closer, sources hot" reading, compared ReBirth's drums with
  our noise and are withdrawn. KiloMix names the stock set. Over its bars the
  compressor work moved, mean |ours - ReBirth| per bar for the build before the
  topology fix, the topology build (`review/2026-09`) and round 5 is 8.59 /
  4.18 / 2.98 dB. Split with a copy exported with the compressor routed nowhere
  (`fixtures-2026-10-01/demo-comp-check`), its bars whose sources match ReBirth's
  within 1 dB show round 5's gain miss at 0.74 dB (signed +0.64) against the
  topology build's 0.35 (+0.24); the verifier's 0.5 dB subset read 0.69 to 0.44
  the other way, so the two builds are within the noise of the method there.
  Our KiloMix renders played its drums through our synthesis path, not the stock
  samples (`--samples` not passed); its compressed bars 538 to 543 are bass 1
  alone, so that does not touch them.
- **Sample rate.** The fit is valid at 44.1 kHz. The timing constants are
  rate-invariant (hold and release gains at 2 to 110 ms agree within 0.01 dB at
  44.1, 48 and 96 kHz), but a peak detector's reading of a high-frequency
  source moves with the rate, because the source's own sample peaks do:
  cm_hats_r127_t000 through the engine gains +3.34 / +3.61 / +5.25 dB at 44.1 /
  48 / 96 kHz (a 1.91 dB spread; round 2's was 1.27), cm_hats_r064_t000 spreads
  1.26 dB (round 2: 0.37). The saw, square and stacc spread 0.00 dB, the kick
  0.16.
- **Raw rides.** Through the engine on the TPT voice the raw comp-rides misses
  are +0.93 to +2.33 dB, because of the drone's crest (BACKLOG 22, now marked
  as blocking the compressor's raw parity); `PARITY.md` carries the numbers.
- **Ride t90s.** Ours are faster than ReBirth's on three of the four rides (5 /
  15 / 15 / 5 ms against 15 / 15 / 25 / 15), by one 10 ms window each: there is
  no gain smoother and the params are decoded once per 512-sample block. Left
  as is until ReBirth's ride glide is measured finer (BACKLOG 18).
