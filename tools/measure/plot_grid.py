#!/usr/bin/env python3
"""Draw batch B5's pooled dense-grid responses, ReBirth against ours.

The companion to `plot_response.py`, for the batch that has a reference worth
dividing by. One panel per cell, every pitch pooled, plotted against absolute
frequency. Levels are NOT normalised and cannot be: the two sides' overall gains
are not comparable, so read the SHAPE and ignore any vertical offset.

    python plot_grid.py <rebirth-dir> <ours-dir> --out grid.png
"""
import argparse
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_grid import pooled

PANELS = ["ref_flat", "c112_r000", "c064_r000", "c032_r000",
          "c112_r096", "c064_r096", "c032_r096", "c064_r127"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--out", required=True)
    a = ap.parse_args(argv)

    nc = 4
    nr = (len(PANELS) + nc - 1) // nc
    fig, axes = plt.subplots(nr, nc, figsize=(4.0 * nc, 3.1 * nr),
                             sharex=True, squeeze=False)
    for i, name in enumerate(PANELS):
        ax = axes[i // nc][i % nc]
        fa, da = pooled(a.ref_dir, name, "ref_open")
        fb, db = pooled(a.ours_dir, name, "ref_open")
        if fa is not None:
            ax.semilogx(fa, da, ".", ms=3, color="#1f77b4", label="ReBirth")
        if fb is not None:
            ax.semilogx(fb, db, ".", ms=3, color="#d62728", label="ours")
        ax.set_xlim(55, 12000)
        ax.grid(True, which="both", alpha=0.2, lw=0.5)
        ax.set_title(name, fontsize=9)
        ax.tick_params(labelsize=7)
        if i == 0:
            ax.legend(fontsize=7, loc="lower left")
        if i % nc == 0:
            ax.set_ylabel("dB over the open reference", fontsize=8)
    for j in range(len(PANELS), nr * nc):
        axes[j // nc][j % nc].axis("off")
    fig.suptitle("303 filter, batch B5, six pitches pooled. levels are not "
                 "comparable between sides, only shapes are", fontsize=10)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(a.out, dpi=110)
    print("wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
