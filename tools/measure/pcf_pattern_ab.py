#!/usr/bin/env python3
"""Score our PCF against ReBirth's, pattern by pattern, on the cutoff trajectory.

The end-to-end check on the preset table AND the envelope AND the decay law all
at once. Both sides render the same fixture, both are divided by their own
unfiltered reference, and the cutoff parameter recovered from each is compared
frame by frame. Everything that is not the PCF divides out.

The number to read is the rms difference in the cutoff PARAMETER, which spans
10.653 octaves, so 0.01 of it is a tenth of an octave.

    python pcf_pattern_ab.py <rebirth-dir> <ours-dir>
    python pcf_pattern_ab.py <rebirth-dir> <ours-dir> --prefix x_pat --ref r10_ref.wav
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_cutoff import trajectory


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--prefix", default="w_pat")
    ap.add_argument("--ref", default="r9_ref.wav")
    ap.add_argument("--worst", type=int, default=8)
    a = ap.parse_args(argv)

    rows = []
    for n in range(55):
        name = "%s%02d.wav" % (a.prefix, n)
        fa = os.path.join(a.ref_dir, name)
        fb = os.path.join(a.ours_dir, name)
        if not (os.path.exists(fa) and os.path.exists(fb)):
            continue
        _, pa, _ = trajectory(os.path.join(a.ref_dir, a.ref), fa)
        _, pb, _ = trajectory(os.path.join(a.ours_dir, a.ref), fb)
        m = min(len(pa), len(pb))
        ok = np.isfinite(pa[:m]) & np.isfinite(pb[:m])
        if ok.sum() < 100:
            rows.append((n, np.nan, 0))
            continue
        d = pa[:m][ok] - pb[:m][ok]
        rows.append((n, float(np.sqrt((d * d).mean())), int(ok.sum())))

    good = [r for r in rows if np.isfinite(r[1])]
    v = np.array([r[1] for r in good])
    print("%d patterns compared" % len(good))
    print("cutoff parameter rms difference: mean %.4f, median %.4f, worst %.4f"
          % (v.mean(), np.median(v), v.max()))
    print("in octaves (the parameter spans 10.653): mean %.3f, worst %.3f"
          % (v.mean() * 10.653, v.max() * 10.653))
    print("\nworst %d:" % a.worst)
    for n, r, k in sorted(good, key=lambda x: -x[1])[:a.worst]:
        print("   pattern %2d  %.4f  (%d frames)" % (n, r, k))
    return 0


if __name__ == "__main__":
    sys.exit(main())
