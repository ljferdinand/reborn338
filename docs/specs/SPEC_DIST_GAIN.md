# Distortion gain-law fit (walled implementer brief, supertask 7 item 3)

You are the firewalled implementer. Fit ONE thing: the distortion's output-gain
law. Fit it from the measured data in this brief and your own re-measurements
only. Do NOT read any disassembly or decompilation of
ReBirth; nothing you produce may come from it. Everything you need is here.

## What is wrong today

`src/effects/distortion.cpp`, in `process()`, sets the output trim to a CONSTANT:

    const float outGainT = kOutTrim;   // kOutTrim = 0.6105

That constant centres the level but is not a fitted law. Measured against ReBirth
across the Amount x Shape grid, our output runs up to ~13 dB too LOUD at high
drive, because the wavefolder gains energy with drive while ReBirth's output
level FALLS with drive. Replace the constant with a fitted
`outGain(amount, shape)`.

## The target (measured, ReBirth ground truth)

`docs/dist_gain_target.csv`, columns:
`shape_byte, amount_byte, amount01, shape01, L_rb, L_ours_current, gap_dB, outGain_target`

- `L_rb` = broadband RMS of ReBirth's distorted drone render, steady 2-6 s window.
- `L_ours_current` = RMS of ReBirth's own clean drone fed through OUR distortion
  at that (amount01, shape01) via the `dist_wav` harness, i.e. our CURRENT output
  level (it already includes `kOutTrim`).
- `outGain_target = kOutTrim * (L_rb / L_ours_current)`: the absolute output
  gain that would make our level match ReBirth's at that cell. THIS is what your
  law must reproduce. `gap_dB` is how far the current constant is off (positive =
  we are too quiet; the high-drive cells are strongly negative = too loud).

The renders lived at `renders/dist-grid2-2026-09-17/` in the author's workspace,
outside the repository (49 cells `h_b1SSS_b2AAA.wav` plus `d_off.wav`), and
`tools/measure/dist_gain.py` regenerates the whole surface (it drives your freshly
built `dist_wav`, so re-run it after each change to see your law's effect).

## The shape of the law (from the marginals, not prescriptive constants)

The surface is bumpy cell-to-cell, the same hard-driven-fold aliasing chaos that
made the CHARACTER fit trend-level. Fit the TREND, targeting the marginals, not
individual cells:

- outGain_target falls as **Amount** rises: Amount-marginal median (across Shape)
  goes ~0.76 -> 0.44 -> 0.43 -> 0.37 -> 0.27 -> 0.24 at amount01
  0.165/0.339/0.504/0.669/0.835/1.000.
- outGain_target also falls as **Shape** rises: Shape-marginal median (across
  Amount) ~0.52/0.69/0.54/0.44/0.26/0.27/0.22 at shape01
  0/.165/.339/.504/.669/.835/1.0 (weaker and noisier than the Amount trend).

Choose whatever low-parameter form reproduces both marginals (a separable
`base * fA(amount) * fS(shape)`, an in-dB additive form, etc.). Fit the constants
to the CSV. The Amount trend is the dominant, cleanest one; get it first, then let
Shape modulate it.

## Hard constraints

1. **Amount 0 stays EXACT bypass.** The `if (amount <= 0.0f) { snap_ = true;
   return; }` branch must be untouched; the gain law is evaluated only for
   amount > 0. (The Amount-0 rows in the CSV are bypass and not a fit target.)
2. **This is a pure output scalar. It must not change the CHARACTER.** The
   normalized spectrum and the inharmonic fraction must stay put; you are only
   scaling the output, not touching the fold/quantize/LP stages. Confirm with
   `dist_spectrum.py` (per-file-normalized bands unchanged) and `dist_inharm.py`
   (inharmonic % unchanged) before and after.
3. **Stable and finite.** outGain > 0 and finite for all (amount, shape) in
   [0,1]; clamp to a sane range.
4. Smooth it the same way the other block-rate targets are smoothed
   (`sOutGain_`/`smoothTo` are already wired; keep using them so a knob move
   does not click).

## Verify (report these numbers in your commit)

- `tools/measure/dist_gain.py` before vs after: median |gap_dB| across the 49
  cells, and the Amount-marginal medians, should collapse toward 0.
- `dist_spectrum.py` and `dist_inharm.py`: character metrics unchanged within
  noise.
- `ctest` green; Amount 0 byte-identical bypass preserved.

Fit FAST and IN-TURN: do the fit, edit `distortion.cpp`, build, measure, and
COMMIT on `dist-cleanroom` in this same turn. Do not spin off a long detached
global search and end the turn with nothing committed. One tight iteration,
commit, report.
