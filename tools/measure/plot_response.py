#!/usr/bin/env python3
"""Draw batch B1's filter response plane, ReBirth against ours, as a picture.

WHY THIS EXISTS. Every 303 measurement in this project reduces a response to two
numbers, a corner and a peak, and on 2026-08-29 two of those reductions
contradicted each other about the SIGN of the corner error: B1's plane said our
corner is 0.28 octaves too low and the regression net said moving it up makes 327
of 373 cells worse. Two scalars cannot say which is confused, because they do not
carry the shape that would show it. This draws the shape.

One panel per cell of the cutoff by resonance plane. Each curve is the fixture's
power spectrum over `b1_ref`'s, read on the drone's harmonic grid, which is the
same quantity `bass303_response.py` reduces. Ours is drawn against ReBirth's on
the same axes so the eye can do what a summary statistic cannot.

    python plot_response.py <rebirth-dir> <ours-dir> --out plane.png
    python plot_response.py <rebirth-dir> <ours-dir> --reso 127 --out r127.png
"""
import argparse
import os
import re
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_response import load, psd, harmonics, response, describe

CUT = (0, 16, 32, 48, 64, 80, 96, 112, 127)
RESO = (0, 32, 64, 96, 127)


def grid(d):
    r, sr = load(os.path.join(d, "b1_ref.wav"))
    p, f = psd(r, sr)
    return p, f, harmonics(p, f)


def curve(d, base, name):
    p0, f, idx = base
    path = os.path.join(d, name + ".wav")
    if not os.path.exists(path):
        return None, None
    x, sr = load(path)
    p, _ = psd(x, sr)
    return response(p0, p, f, idx)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--out", required=True)
    ap.add_argument("--reso", type=int, default=None,
                    help="draw one resonance row across every cutoff")
    a = ap.parse_args(argv)

    base_r, base_o = grid(a.ref_dir), grid(a.ours_dir)
    rows = [a.reso] if a.reso is not None else list(RESO)
    nr, nc = len(rows), len(CUT)
    fig, axes = plt.subplots(nr, nc, figsize=(2.05 * nc, 2.15 * nr),
                             sharex=True, sharey=True, squeeze=False)
    for i, r in enumerate(rows):
        for j, c in enumerate(CUT):
            ax = axes[i][j]
            n = "b1_c%03d_r%03d" % (c, r)
            fa, da = curve(a.ref_dir, base_r, n)
            fb, db = curve(a.ours_dir, base_o, n)
            if fa is not None:
                ax.semilogx(fa, da, color="#1f77b4", lw=1.3, label="ReBirth")
            if fb is not None:
                ax.semilogx(fb, db, color="#d62728", lw=1.3, label="ours")
            ax.axhline(0, color="0.7", lw=0.6)
            ax.set_ylim(-60, 50)
            ax.set_xlim(50, 16000)
            ax.grid(True, which="both", alpha=0.18, lw=0.5)
            ax.tick_params(labelsize=6)
            if i == 0:
                ax.set_title("cutoff %d" % c, fontsize=8)
            if j == 0:
                ax.set_ylabel("reso %d\ndB" % r, fontsize=8)
            if i == 0 and j == 0:
                ax.legend(fontsize=6, loc="lower left")
    fig.suptitle("303 filter response, fixture over b1_ref, on the drone's "
                 "harmonic grid. ReBirth blue, ours red", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    fig.savefig(a.out, dpi=110)
    print("wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
