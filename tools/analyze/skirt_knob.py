#!/usr/bin/env python3
"""Skirt slope over a band anchored to the CUTOFF KNOB, not to any measured peak.

Four separate findings here have been distorted by referencing a statistic to a
measured peak: the inversion's resonance-0 baseline, a peak-rise baseline taken
at a feedback where no peak exists, the gated skirt estimator, and the discovery
on 2026-07-31 that half the skirt's "mean offset" was the band sitting in
different places on the two sides. The pattern is consistent enough to be a rule.
Any statistic referenced to a measured peak is only as good as the peak.

This removes the peak from the measurement entirely.

The band is a fixed multiple of the NOMINAL corner implied by the fixture's
cutoff knob. The knob is an INPUT, identical for both sides by construction, so
the band cannot drift with either side's response. It carries no information
about how well either filter is fitted, and nothing measured feeds back into
where the band sits. Whether our engine's own corner ends up above or below that
nominal value is exactly the kind of thing this is trying not to be confused by.

Deliberately NOT included: any resonance dependence in the anchor. Our engine
moves its corner with resonance (RESO_OCT) and ReBirth appears to as well, but
building either into the band would reintroduce a fitted quantity into the
measurement and defeat the point.

Reports the level at the top of the band for both sides, so a read that has run
into the dither floor announces itself rather than quietly changing the fit.

Usage:  python skirt_knob.py [ours-dir] [rebirth-dir]
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics      # noqa: E402
from skirt_ab import slope_fixed                       # noqa: E402

# The nominal corner law, from src/devices/bass_synth.cpp. Only its SHAPE
# matters: a constant factor moves both sides' bands together and cancels.
BASE_HZ = 407.0
CUT_SLOPE = 2.8
CUT_SCALE = 1.05
LO_MULT, HI_MULT = 1.7, 4.0
TOP_CAP = 16000.0


def nominal_corner(knob):
    return CUT_SCALE * BASE_HZ * 2.0 ** (CUT_SLOPE * knob / 127.0)


def curve(path):
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f, guess=220.0, tol=0.35)
    ks, m = harmonics(P, f, f0, kmax=90)
    if len(ks) < 12:
        return None
    db = 20 * np.log10(m + 1e-300)
    return ks * f0, db - np.mean(db[:3])


def main():
    ours = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WS, "renders", "batchSW_ours")
    rb = sys.argv[2] if len(sys.argv) > 2 else os.path.join(WS, "renders", "batchSW_rb")
    cells = []
    for fn in sorted(os.listdir(rb)):
        m = re.search(r"_cut(\d+)_reso(\d+)\.wav$", fn)
        if m and os.path.exists(os.path.join(ours, fn)):
            cells.append((int(m.group(1)), int(m.group(2)), fn))
    print(f"{len(cells)} cells. Band = {LO_MULT}x..{HI_MULT}x the nominal corner")
    print("from the cutoff KNOB. No measured peak anywhere in the band choice.\n")
    print(f"{'cut':>4s} {'reso':>5s} | {'band kHz':>12s} | {'RB':>7s} {'ours':>7s} "
          f"{'diff':>6s} | {'RB@top':>7s} {'our@top':>8s} {'n':>3s}")
    by = {}
    for c, r, fn in cells:
        fc = nominal_corner(c)
        lo, hi = LO_MULT * fc, min(HI_MULT * fc, TOP_CAP)
        A = curve(os.path.join(rb, fn))
        B = curve(os.path.join(ours, fn))
        if not A or not B:
            continue
        sa, na, ta = slope_fixed(A[0], A[1], lo, hi)
        sb, nb, tb = slope_fixed(B[0], B[1], lo, hi)
        if not np.isfinite(sa) or not np.isfinite(sb):
            continue
        by.setdefault(c, []).append((r, sb - sa))
        print(f"{c:4d} {r:5d} | {lo/1000:5.1f}-{hi/1000:<6.1f} | {sa:7.1f} {sb:7.1f} "
              f"{sb-sa:6.1f} | {ta:7.1f} {tb:8.1f} {min(na,nb):3d}")
    print("\nours minus ReBirth, dB/oct. NEGATIVE means ours falls faster.")
    for c, rows in sorted(by.items()):
        r = np.array([x[0] for x in rows], float)
        d = np.array([x[1] for x in rows], float)
        drift = np.polyfit(r, d, 1)[0] * 100 if len(r) > 2 else float("nan")
        print(f"  cutoff {c}: mean {d.mean():+.2f}, spread {d.min():+.1f} to "
              f"{d.max():+.1f}, drift {drift:+.2f} per 100 knob")
    print()
    print("A drift that survives THIS band is a real resonance dependence, because")
    print("nothing about where the band sits depends on resonance or on either")
    print("side's response. A drift that vanishes here was the band moving.")


if __name__ == "__main__":
    main()
