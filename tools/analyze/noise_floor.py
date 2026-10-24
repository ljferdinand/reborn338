#!/usr/bin/env python3
"""Is the filter fit above its own measurement noise, or inside it?

The filter constants have been fitted twice against batch H and never once
checked against the floor of the instrument doing the fitting. Batch H had no
duplicate exports, so there was nothing to check against.

`renders/batchH_floor/` fixes that: six cells spanning the grid, five ReBirth
exports each, so ten pairs per cell. The floor is what the SAME input scores
against ITSELF through the same objective.

Two things make this less obvious than it sounds.

**ReBirth has about six discrete render states**, not a random phase, so two
exports collide outright roughly one time in six and a colliding pair scores
exactly zero. A single-pair floor is a draw from a distribution containing
zeros and will sometimes report perfect precision. Use the median over all
pairs, and report the max as the pessimistic case.

**The comparison must be like for like.** `curve_fit.compare()` pools every
harmonic from every cell into ONE rms, so the headline "2.28 dB rms" is a
36-cell pooled number. Scoring it against a floor measured on six cells would
compare two different statistics, which is the exact mistake that produced two
false alarms on 2026-07-30. So the fit is re-scored here restricted to the same
six cells, pooled the same way.

Usage:
    python noise_floor.py                 # floor only
    python noise_floor.py <ours-dir> ...  # floor plus those builds scored against it
"""
import itertools
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from curve_fit import curve, FLOOR_DB            # noqa: E402

FLOORDIR = os.path.join(WS, "renders", "batchH_floor")
RBDIR = os.path.join(WS, "renders", "batchH_rb")
CELLS = [(16, 0), (40, 90), (64, 60), (90, 127), (110, 30), (127, 127)]


def pooled(A, B, cells):
    """curve_fit.compare()'s statistic: every harmonic from every cell, one rms."""
    num, den = 0.0, 0
    per = {}
    for k in cells:
        if k not in A or k not in B:
            continue
        _fa, da, _pa = A[k]
        _fb, db_, _pb = B[k]
        n = min(len(da), len(db_))
        sel = da[:n] > FLOOR_DB
        if sel.sum() < 8:
            continue
        d = db_[:n][sel] - da[:n][sel]
        num += float(np.sum(d ** 2))
        den += int(sel.sum())
        per[k] = float(np.sqrt(np.mean(d ** 2)))
    if not den:
        return None, per
    return float(np.sqrt(num / den)), per


def load_reps():
    """{(cut,reso): [curve per rep]} from the duplicate exports."""
    reps = {}
    for c, r in CELLS:
        cv = []
        i = 1
        while True:
            p = os.path.join(FLOORDIR, f"h303_cut{c:03d}_reso{r:03d}_rep{i}.wav")
            if not os.path.exists(p):
                break
            k = curve(p)
            if k:
                cv.append(k)
            i += 1
        if cv:
            reps[(c, r)] = cv
    return reps


def main(ours_dirs):
    reps = load_reps()
    if not reps:
        sys.exit(f"no duplicate exports in {FLOORDIR}")
    n = min(len(v) for v in reps.values())
    print(f"{len(reps)} cells, {n} ReBirth exports each, "
          f"{n*(n-1)//2} pairs per cell\n")

    # --- per cell -----------------------------------------------------------
    print("=== FLOOR per cell: the same input scored against itself ===")
    print(f"{'cell':>16s} {'min':>7s} {'median':>8s} {'max':>7s} {'collisions':>11s}")
    for k in CELLS:
        if k not in reps:
            continue
        v = []
        for a, b in itertools.combinations(reps[k], 2):
            p, _ = pooled({k: a}, {k: b}, [k])
            if p is not None:
                v.append(p)
        v = np.array(v)
        print(f"{'cut%d reso%d' % k:>16s} {v.min():7.3f} {np.median(v):8.3f} "
              f"{v.max():7.3f} {int((v <= 1e-9).sum()):8d}/{len(v)}")
    print()

    # --- pooled, which is the statistic the fit is reported in --------------
    print("=== FLOOR pooled across the six cells, the same statistic as the fit ===")
    idx = list(range(n))
    vals = []
    for i, j in itertools.combinations(idx, 2):
        A = {k: reps[k][i] for k in reps}
        B = {k: reps[k][j] for k in reps}
        p, _ = pooled(A, B, list(reps))
        if p is not None:
            vals.append(p)
    vals = np.array(vals)
    print(f"  {len(vals)} pairs   min {vals.min():.3f}   MEDIAN {np.median(vals):.3f}   "
          f"max {vals.max():.3f} dB rms")
    print(f"  collisions {int((vals <= 1e-9).sum())}/{len(vals)}")
    floor_med, floor_max = float(np.median(vals)), float(vals.max())
    print()

    # --- our builds ---------------------------------------------------------
    if not ours_dirs:
        return
    print("=== OUR BUILDS, scored on the same six cells, pooled the same way ===")
    print("    against a SINGLE ReBirth export (rep1), and against all five")
    print("    averaged pairwise, which is what the floor says to prefer.")
    print()
    print(f"{'build':>22s} {'vs rep1':>9s} {'mean vs all 5':>14s} {'floor med':>10s} "
          f"{'ratio':>7s} {'verdict':>26s}")
    ref1 = {k: reps[k][0] for k in reps}
    for d in ours_dirs:
        dd = d if os.path.isabs(d) else os.path.join(WS, "renders", d)
        ours = {}
        for c, r in CELLS:
            p = os.path.join(dd, f"h303_cut{c:03d}_reso{r:03d}.wav")
            if os.path.exists(p):
                cv = curve(p)
                if cv:
                    ours[(c, r)] = cv
        if not ours:
            print(f"{os.path.basename(dd):>22s}   no renders found")
            continue
        s1, _ = pooled(ref1, ours, list(reps))
        allv = []
        for i in range(n):
            A = {k: reps[k][i] for k in reps}
            p, _ = pooled(A, ours, list(reps))
            if p is not None:
                allv.append(p)
        sm = float(np.mean(allv))
        ratio = sm / floor_med if floor_med > 0 else float("inf")
        if sm < floor_max:
            verdict = "INSIDE the floor's spread"
        elif ratio < 1.5:
            verdict = "barely above the floor"
        elif ratio < 3.0:
            verdict = "above, but same order"
        else:
            verdict = "clearly above the floor"
        print(f"{os.path.basename(dd):>22s} {s1:9.3f} {sm:14.3f} {floor_med:10.3f} "
              f"{ratio:7.2f} {verdict:>26s}")
    print()
    print("  A build is only meaningfully better than another if the gap between")
    print("  them exceeds the floor's spread. Read the ratio, not the third decimal.")


if __name__ == "__main__":
    main(sys.argv[1:])
