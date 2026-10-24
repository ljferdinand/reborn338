#!/usr/bin/env python3
"""Batch B1's filter plane, read WITHOUT a passband. Supersedes the corner and
peak statistics in `bass303_response.py`.

WHY IT EXISTS. `bass303_response.py` reads a corner as "where the response falls
3 dB below its own passband", and takes the passband from the lowest four
partials of a 55 Hz drone, which are 65, 129, 194 and 231 Hz. **Wherever the
corner approaches those partials, the passband reading is already in the
stopband and the statistic stops meaning anything.** That is most of the
low-cutoff half of the plane. It is the same family of error as a denominator
that is not one: a reference taken from inside the thing being measured.

On 2026-08-29 that statistic said our corner is 0.284 octaves too low and drove a
refit, `CUT_SCALE` 0.88 with `CUT_SLOPE` 3.24, which the regression net then
rejected at +2.094 dB over 373 cells with 327 worse, disagreeing about the SIGN.
Both of the measures here are free of any passband, they agree with each other,
and they say the corner was never the problem.

  * CORNER AS THE RESONANT PEAK. At resonance 96 and 127 the peak is sharp and
    sits at the corner. Interpolated between harmonics in log frequency.
  * CORNER AS AN ABSOLUTE CROSSING. The frequency where the response falls
    through a fixed level relative to `b1_ref`, default -20 dB. Works at any
    resonance and involves no normalisation at all.
  * THE RESIDUAL IN THE FILTER'S OWN COORDINATES. Ours minus ReBirth in dB,
    binned by octaves from ReBirth's own corner and pooled across the cutoff
    knob. This is the one that says what is actually wrong, and it is a shape
    rather than a number.

    python bass303_plane.py <rebirth-dir> <ours-dir>
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_response import load, psd, harmonics, response

CUT = (0, 16, 32, 48, 64, 80, 96, 112, 127)
RESO = (0, 32, 64, 96, 127)


def base(d):
    r, sr = load(os.path.join(d, "b1_ref.wav"))
    p, f = psd(r, sr)
    return p, f, harmonics(p, f)


def curve(d, b, n):
    p0, f, idx = b
    q = os.path.join(d, n + ".wav")
    if not os.path.exists(q):
        return None, None
    x, sr = load(q)
    p, _ = psd(x, sr)
    return response(p0, p, f, idx)


def peak_freq(fr, db):
    i = int(np.argmax(db))
    if i == 0 or i == len(fr) - 1:
        return float(fr[i])
    y0, y1, y2 = db[i - 1], db[i], db[i + 1]
    den = y0 - 2 * y1 + y2
    d = (y0 - y2) / (2 * den) if den != 0 else 0.0
    d = max(-1.0, min(1.0, d))
    lf = np.log(fr)
    return float(np.exp(lf[i] + d * (lf[i + 1] - lf[i - 1]) / 2))


def crossing(fr, db, lvl):
    for i in range(1, len(fr)):
        if db[i] <= lvl < db[i - 1]:
            t = (lvl - db[i - 1]) / (db[i] - db[i - 1])
            return float(np.exp(np.log(fr[i - 1]) +
                                t * (np.log(fr[i]) - np.log(fr[i - 1]))))
    return np.nan


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--level", type=float, default=-20.0)
    a = ap.parse_args(argv)
    br, bo = base(a.ref_dir), base(a.ours_dir)

    def cells(r):
        for c in CUT:
            n = "b1_c%03d_r%03d" % (c, r)
            fa, da = curve(a.ref_dir, br, n)
            fb, db = curve(a.ours_dir, bo, n)
            if fa is not None and fb is not None:
                yield c, fa, da, fb, db

    print("CORNER as the RESONANT PEAK frequency. + means ours is HIGHER.")
    for r in (96, 127):
        es = []
        print("\n reso %d" % r)
        print(" %-8s %9s %9s %9s" % ("cutoff", "ReBirth", "ours", "oct err"))
        for c, fa, da, fb, db in cells(r):
            x, y = peak_freq(fa, da), peak_freq(fb, db)
            e = float(np.log2(y / x)); es.append(e)
            print(" %-8d %9.0f %9.0f %+9.3f" % (c, x, y, e))
        if es:
            print("   mean %+.3f oct, mean abs %.3f"
                  % (np.mean(es), np.mean(np.abs(es))))

    print("\nCORNER as the crossing of %.0f dB relative to b1_ref, no passband."
          % a.level)
    for r in RESO:
        es = []
        for c, fa, da, fb, db in cells(r):
            x, y = crossing(fa, da, a.level), crossing(fb, db, a.level)
            if np.isfinite(x) and np.isfinite(y):
                es.append(float(np.log2(y / x)))
        if es:
            print(" reso %-4d n=%d  mean %+.3f oct, mean abs %.3f"
                  % (r, len(es), np.mean(es), np.mean(np.abs(es))))

    print("\nRESIDUAL, ours minus ReBirth in dB, by octaves from ReBirth's own")
    print("corner, pooled across the cutoff knob. This is the real defect.")
    bins = np.arange(-3.5, 3.01, 0.5)
    print(" %-6s %s" % ("reso", " ".join("%6.1f" % b for b in bins)))
    for r in RESO:
        rows = [[] for _ in bins]
        for c, fa, da, fb, db in cells(r):
            fc = peak_freq(fa, da)
            oc = np.log2(fa / fc)
            e = db - da
            for k, b0 in enumerate(bins):
                m = (oc >= b0 - 0.25) & (oc < b0 + 0.25)
                if m.any():
                    rows[k].append(float(np.median(e[m])))
        print(" %-6d %s" % (r, " ".join(("%6.1f" % np.median(v)) if v else "%6s" % "."
                                        for v in rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
