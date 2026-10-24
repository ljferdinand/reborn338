#!/usr/bin/env python3
"""Read Appendix D's 55 PCF pattern diagrams numerically, off the page images.

This is NOT the primary source. `pcf_patterns.py` measures the patterns off
ReBirth's own audio and that is what the preset table is built from. This tool
exists as an INDEPENDENT CHECK on that measurement, because the two disagreed
about pattern 32 and one of them had to be wrong.

Reading the drawings by eye is what the audio measurement was meant to avoid,
and rightly. Reading them by pixel is a different thing: the bars are solid
black on white, sitting on a horizontal axis, and their heights and positions
come out as numbers with no judgement in between.

Geometry, calibrated on page 207 where patterns 3, 4, 5 and 6 have known step
positions and no dynamics:

  * four diagrams per page, axis rows found as the only rows with more than 600
    black pixels
  * the sixteenth grid starts at x = 71 with 37.25 px per sixteenth, which puts
    pattern 5's sixteen bars on sixteenths 0..15 to within 0.08 of a step
  * a full-height bar is 238 px, and every bar in patterns 0 to 11 measures
    237 to 238, which is what the manual means by "no dynamics"
  * the loop-end marker below the axis is a left-pointing triangle whose TIP is
    its left edge; length is floor((tip - 71) / 37.25) + 1 grid steps, which
    gives 12 for pattern 3 and 16 for patterns 4, 5 and 6

THE GRID UNIT IS NOT THE SAME ON EVERY DIAGRAM, and reading it as sixteenths
throughout is the trap here. Patterns 0 to 33 are drawn on an axis labelled
1..16 twice, two bars of sixteenths. Patterns 34 to 45 are drawn on an axis
labelled 1..32, one bar of THIRTY-SECONDS. The manual says as much in prose
(p65) and the mixed group 46 and up turns out to use both: 46, 47 and 52 are
thirty-second diagrams and the rest are sixteenth.

The unit is detected, not assumed. The label at grid position 17 is a bare "1"
on a doubled sixteenth axis and "17" on a thirty-second axis, so the ink extent
in a window just right of that position separates them: 5 px against 21 px, with
nothing in between across all 55.

    python pcf_appendixd.py            # all 55, in thirty-seconds
    python pcf_appendixd.py --raw      # un-snapped positions, to check the grid
    python pcf_appendixd.py --pattern 32
    python pcf_appendixd.py --inc > ../../src/effects/pcf_presets.inc
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = os.path.normpath(os.path.join(
    HERE, "..", "..", "..", "manualPNGs", "19_Appendix_D_PCF_Pattern_Diagrams"))
NAME = "propellerhead_rebirth_rb338_v2.01_manual_Page_%03d.png"

X0, DX = 71.0, 37.25          # sixteenth grid, calibrated on page 207
FULL = 238.0                  # a velocity-1.0 bar, in pixels
AXIS_MIN_INK = 600            # an axis row is the only row this black
BAR_X_MIN = 65                # left of this is the y-axis and its ticks


LABEL17 = int(round(X0 + DX * 16 + 4))   # where grid position 17's label sits


def grid_unit(a, y0):
    """32 if this diagram's axis is labelled 1..32, else 16.

    Position 17's label is "1" on a doubled sixteenth axis and "17" on a
    thirty-second one. The window is deliberately lopsided so the neighbouring
    labels, 37 px away on both sides, cannot leak into it.
    """
    band = a[y0 + 30:y0 + 62, :].sum(0) > 0
    w = np.where(band[LABEL17 - 10:LABEL17 + 21])[0]
    ext = (w.max() - w.min() + 1) if len(w) else 0
    return 32 if ext > 12 else 16


def diagrams(page):
    """Every diagram on one page, as (unit, [(x_lo, x_hi, height)], tip_x)."""
    a = np.asarray(Image.open(os.path.join(PAGES, NAME % page)).convert("L")) < 128
    out = []
    for y0 in [y for y in range(a.shape[0]) if a[y].sum() > AXIS_MIN_INK]:
        top = y0 - 270
        col = a[top:y0 - 1, :].sum(0) > 0
        bars, i = [], BAR_X_MIN
        while i < len(col):
            if col[i]:
                j = i
                while j < len(col) and col[j]:
                    j += 1
                seg = a[top:y0 - 1, i:j]
                ys = np.where(seg.any(1))[0]
                bars.append((i, j - 1, (y0 - 1) - (top + ys.min())))
                i = j
            else:
                i += 1
        # Markers sit below the axis. The last one is the loop end.
        bel = a[y0 + 2:y0 + 30, :].sum(0) > 0
        marks, i = [], 0
        while i < len(bel):
            if bel[i]:
                j = i
                while j < len(bel) and bel[j]:
                    j += 1
                marks.append(i)
                i = j
            else:
                i += 1
        out.append((grid_unit(a, y0), bars, marks[-1] if len(marks) > 1 else None))
    return out


def read_all():
    """All 55 patterns as (steps32, velocities, length32, unit).

    Steps and length come back in THIRTY-SECONDS whatever the diagram's own
    unit, because that is what the engine's preset table counts in.
    """
    pats, n = {}, 0
    for page in range(206, 221):
        path = os.path.join(PAGES, NAME % page)
        if not os.path.exists(path):
            continue
        for unit, bars, tip in diagrams(page):
            if not bars:
                continue
            mul = 2 if unit == 16 else 1     # sixteenth diagrams count double
            steps, vels = [], []
            for lo, hi, h in bars:
                steps.append((lo - X0) / DX * mul)
                vels.append(h / FULL)
            length = (int((tip - X0) / DX) + 1) * mul if tip is not None else None
            pats[n] = (steps, vels, length, unit)
            n += 1
            if n > 54:
                return pats
    return pats


HEADER = '''// PCF preset patterns 0 to 54.
//
// Generated by tools/measure/pcf_appendixd.py, which reads Appendix D's bar
// charts off the manual's page images by pixel. Do not hand-edit: regenerate.
//
// Each pattern is a list of (step, velocity) where step counts THIRTY-SECOND
// notes from the pattern's start and velocity is 0..1. `length32` is how many
// thirty-seconds the pattern occupies before it repeats.
//
// WHY THE DRAWINGS AND NOT THE AUDIO. The 2026-08-27 table was measured off
// ReBirth's own renders, which is the right instinct and was the wrong result.
// The fixtures were fine; the ESTIMATOR saturated. It held Amt at 64/127 and
// read the cutoff out of an 88..354 Hz band, and at that Amt a velocity-1.0
// trigger puts the corner at 372 Hz, right at the top of the band. Its
// sensitivity falls from 64.6 dB per unit velocity at v = 0.1 to 12.6 at
// v = 0.9, and v = 1.0 lands at +4.04 dB of band attenuation, which its own
// rejection threshold of +3.93 throws away as unmeasurable. So the top of the
// velocity range was compressed five to one and its very top discarded, while
// the patterns WITHOUT dynamics, which the round normalised against, came back
// correct because they only ever sat at one point on that curve. Pattern 12 is
// the clearest case: the manual draws a clean 0.03 to 0.90 triangle and the
// audio round read 0.14 to 0.34.
//
// The two sources agree EXACTLY where the audio one was not saturated: over
// patterns 0 to 8 and 10, every step position matches and the largest velocity
// difference is 0.01. They also agree on pattern 3's twelve-step length and on
// pattern 5 triggering on sixteenths, which is the point on which the manual's
// own tutorial prose (p62) is wrong. So this is not the manual overruling a
// measurement; it is the same answer read where the measurement could not see.
//
// Two things here are NOT yet confirmed against ReBirth audio and are the
// subject of the next fixture round:
//
//   * THE TINY BARS. Around a hundred steps across the 32nd-note patterns are
//     drawn at velocity 0.04 to 0.05, standing apart from an otherwise
//     continuous distribution with a gap between 0.07 and 0.09. They are
//     emitted as drawn. If they are instead the drawing's way of marking a
//     step that carries no trigger, then whether they matter depends entirely
//     on the retrigger rule, because a reset-style retrigger would slam a
//     decaying envelope down to 0.05 where a max-style one would ignore it.
//   * PHASE. The audio round rotated every pattern so its first trigger landed
//     on step 0, because it took its origin from the first trigger it found.
//     The diagrams place them against the bar, and pattern 9 is the proof: same
//     rhythm, offset by one sixteenth.
'''


def emit_inc(pats):
    """The presets as a C++ include, matching what pcf.cpp expects."""
    out = [HEADER, "\nnamespace reborn {\n",
           "struct PcfPresetStep { unsigned char step32; float velocity; };\n",
           "struct PcfPreset { int length32; int count; "
           "const PcfPresetStep* steps; };\n"]
    for i in sorted(pats):
        steps, vels, length, _ = pats[i]
        rows = ["{%d, %.3ff}" % (int(round(s)), v)
                for s, v in zip(steps, vels) if round(s) < length]
        body, line = [], "    "
        for r in rows:
            if len(line) + len(r) > 76:
                body.append(line.rstrip())
                line = "    "
            line += r + ", "
        body.append(line.rstrip().rstrip(","))
        out.append("\nstatic const PcfPresetStep kPcfPat%02d[] = {\n%s\n};\n"
                   % (i, "\n".join(body)))
    out.append("\nstatic const PcfPreset kPcfPresets[55] = {\n")
    for i in sorted(pats):
        steps, _, length, _ = pats[i]
        n = len([s for s in steps if round(s) < length])
        out.append("    { %3d, %3d, kPcfPat%02d },\n" % (length, n, i))
    out.append("};\n\n} // namespace reborn\n")
    return "".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pattern", type=int)
    ap.add_argument("--raw", action="store_true",
                    help="print un-snapped step positions, to check the grid")
    ap.add_argument("--inc", action="store_true",
                    help="emit the presets as pcf_presets.inc")
    a = ap.parse_args(argv)

    if a.inc:
        sys.stdout.write(emit_inc(read_all()))
        return 0

    pats = read_all()
    which = [a.pattern] if a.pattern is not None else sorted(pats)
    worst = 0.0
    for i in which:
        if i not in pats:
            continue
        steps, vels, length, unit = pats[i]
        mul = 2 if unit == 16 else 1
        worst = max(worst, max(abs(s / mul - round(s / mul)) for s in steps))
        fmt = "%.2f:%.2f" if a.raw else "%d:%.2f"
        body = "  ".join(fmt % ((s if a.raw else round(s)), v)
                         for s, v in zip(steps, vels))
        print("pat %2d  1/%d grid  len32 %s  n %2d  %s"
              % (i, unit, str(length), len(steps), body))
    if len(which) > 1:
        print("\nworst distance from a grid point: %.2f of a step" % worst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
