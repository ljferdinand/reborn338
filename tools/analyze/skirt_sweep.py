#!/usr/bin/env python3
"""Skirt slope against the RESONANCE knob, scored with a fixed band.

The 16-fixture sweep at cutoff 110 and 127 crossed with resonance 45 to 127 was
already exported. It was first scored with a gated estimator that is now
discredited (see docs/research/FINDINGS_AUDIT.md: two exports of one fixture gave slopes
8.8 dB/oct apart while their band energies agreed to 0.03 dB), so it returned
nothing usable. No new ReBirth time is needed to re-answer it, only a method
that does not gate.

Band: 1.7x to 4x ReBirth's own peak, the same frequencies for both sides,
capped at 16 kHz. Scaling from the peak rather than fixing absolute limits keeps
the band in the same place relative to the corner as the corner moves, which is
what "the slope above the corner" means. The level at the top of the band is
printed for BOTH sides so a read that has run into the floor is visible rather
than silent.

Usage:  python skirt_sweep.py [ours-dir] [rebirth-dir]
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics       # noqa: E402
from skirt_ab import slope_fixed                        # noqa: E402

TOP_CAP = 16000.0


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
    print(f"{len(cells)} cells, band = 1.7x..4x ReBirth's peak, no gating\n")
    print(f"{'cut':>4s} {'reso':>5s} | {'band kHz':>12s} | {'RB slope':>9s} "
          f"{'RB@top':>7s} | {'our slope':>10s} {'our@top':>8s} | {'diff':>6s}")
    by = {}
    for c, r, fn in cells:
        A, B = curve(os.path.join(rb, fn)), curve(os.path.join(ours, fn))
        if A is None or B is None:
            continue
        fa, da = A
        fb, db_ = B
        pk = float(fa[int(np.argmax(da))])
        lo, hi = pk * 1.7, min(pk * 4.0, TOP_CAP)
        if hi <= lo * 1.3:
            print(f"{c:4d} {r:5d} | peak {pk:.0f} Hz too high for a band under {TOP_CAP/1000:.0f} kHz")
            continue
        sa, na, ta = slope_fixed(fa, da, lo, hi)
        sb, nb, tb = slope_fixed(fb, db_, lo, hi)
        if not (np.isfinite(sa) and np.isfinite(sb)):
            continue
        by.setdefault(c, []).append((r, sb - sa))
        print(f"{c:4d} {r:5d} | {lo/1000:5.1f}-{hi/1000:<6.1f} | {sa:9.1f} {ta:7.1f} "
              f"| {sb:10.1f} {tb:8.1f} | {sb-sa:+6.1f}")
    print("\nours minus ReBirth, dB/oct. NEGATIVE means ours falls faster.")
    for c, v in sorted(by.items()):
        vals = [d for _, d in v]
        trend = np.polyfit([r for r, _ in v], vals, 1)[0] * 100 if len(v) > 2 else float("nan")
        print(f"  cutoff {c}: mean {np.mean(vals):+5.2f}, spread "
              f"{min(vals):+.1f} to {max(vals):+.1f}, drift {trend:+.2f} per 100 knob")
    print("\n  A real law needs the spread to be small against the trend. If the")
    print("  spread swamps the drift there is no law here, only scatter.")
    print("  Check the @top columns: any near the dither floor (about -80 dB")
    print("  relative to passband) makes that row's slope suspect.")


if __name__ == "__main__":
    main()
