#!/usr/bin/env python3
"""Fit the 303 filter against ReBirth's TRANSFER-FUNCTION CURVE, not a scalar.

Two problems with the objective the previous rounds used.

1. rolloff() snaps to the 110 Hz harmonic comb. A 2% change of threshold moves
   it 0.137 octaves on average, 0.356 at worst, against a fitted residual of
   0.237. Five of the six reso-0 cells, which are the span DENOMINATOR, sit
   between harmonics where that jump is 0.27 to 0.36 octaves.

2. It is a summary statistic. docs/research/RUNBOOK.md section 7 records the cost of
   that: the old plucky VCA matched ReBirth on envelope coefficient-of-variation
   AND silence fraction and was still the wrong shape entirely. Two scalars
   cannot distinguish two shapes.

So score the SHAPE. The fixtures play a sawtooth at f0 on both sides, and
wave_fit.py established ReBirth's saw is an ideal 1/k series, so

    |H(k*f0)|  proportional to  sqrt(PSD at harmonic k) * k

samples the filter's magnitude response every f0 Hz. Comparing those curves
point by point uses every harmonic in every cell, thousands of constraints
instead of 72, and cannot be gamed by trading one summary against another.

Each curve is normalised to its OWN passband before comparison, per the
measurement-protocol rule (docs/MEASUREMENT_PROTOCOL.md) that band energy is
reported relative to each file's own total so a level offset cannot masquerade
as a spectral error. The level offset is
reported separately.

Harmonics where ReBirth has already fallen below FLOOR_DB relative to its
passband are dropped: down there both sides are into dither and the comparison
measures noise, not filter.

Fit on a checkerboard subset, score on all 36, so a fit cannot flatter itself.
"""
import argparse
import itertools
import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics, CUT, RESO   # noqa: E402

FIX = os.path.join(WS, "fixtures-2026-07-27", "batchH")
RB = os.path.join(WS, "renders", "batchH_rb")
SCRATCH = os.path.join(WS, "renders", "curvefit")
# The fit cache is regenerable and belongs outside the repo, next to the render
# scratch it indexes, not in tools/.
CACHE = os.path.join(SCRATCH, "curve_fit_cache.json")

FLOOR_DB = -55.0        # below this, relative to passband, we are in dither
FIT_CELLS = [(c, r) for i, c in enumerate(CUT) for j, r in enumerate(RESO)
             if (i + j) % 2 == 0]


def curve(path):
    """Normalised filter magnitude curve: (freqs, dB relative to passband, level)."""
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f)
    ks, m = harmonics(P, f, f0)
    if len(ks) < 12:
        return None
    db = 20 * np.log10(m + 1e-300)
    pb = float(np.mean(db[:3]))          # passband = first three harmonics
    return ks * f0, db - pb, pb


def load_dir(d):
    out = {}
    for c in CUT:
        for r in RESO:
            p = os.path.join(d, f"h303_cut{c:03d}_reso{r:03d}.wav")
            if os.path.exists(p):
                cv = curve(p)
                if cv:
                    out[(c, r)] = cv
    return out


def compare(A, B, cells):
    """rms dB difference of the normalised curves, plus level offset."""
    num, den, lev, npts = 0.0, 0, [], 0
    worst = None
    for k in cells:
        if k not in A or k not in B:
            continue
        fa, da, pa = A[k]
        fb, db_, pb = B[k]
        n = min(len(da), len(db_))
        sel = da[:n] > FLOOR_DB
        if sel.sum() < 8:
            continue
        d = (db_[:n][sel] - da[:n][sel])
        num += float(np.sum(d ** 2))
        den += int(sel.sum())
        npts += int(sel.sum())
        lev.append(pb - pa)
        w = float(np.sqrt(np.mean(d ** 2)))
        if worst is None or w > worst[1]:
            worst = (k, w)
    if not den:
        return None
    return dict(rms=float(np.sqrt(num / den)), n=npts,
                level=float(np.mean(lev)), worst=worst)


def render(overrides, tag, seconds=12.0, jobs=20):
    out = os.path.join(SCRATCH, tag)
    env = dict(os.environ)
    env.update({k: str(v) for k, v in overrides.items()})
    r = subprocess.run(
        [sys.executable, os.path.join(REPO, "tools", "render", "render_batch.py"),
         FIX, out, "--seconds", str(seconds), "--jobs", str(jobs)],
        env=env, capture_output=True, text=True, cwd=REPO)
    if r.returncode != 0:
        print("  RENDER FAILED:", (r.stderr or r.stdout).strip()[:200])
        return None
    return out


def load_cache():
    if os.path.exists(CACHE):
        try:
            with open(CACHE) as f:
                return json.load(f)
        except Exception:
            return {}
    return {}


def evaluate(params, A, cache, jobs=20):
    key = json.dumps(params, sort_keys=True)
    if key in cache:
        return cache[key]
    tag = "cf" + str(abs(hash(key)) % (10 ** 10))
    d = render(params, tag, 12.0, jobs)
    if d is None:
        return None
    B = load_dir(d)
    fit = compare(A, B, FIT_CELLS)
    allc = compare(A, B, list(A.keys()))
    if fit is None or allc is None:
        return None
    rec = dict(params=params, fit_rms=fit["rms"], all_rms=allc["rms"],
               n=allc["n"], level=allc["level"],
               worst=[list(allc["worst"][0]), allc["worst"][1]])
    cache[key] = rec
    with open(CACHE, "w") as f:
        json.dump(cache, f)
    return rec


def show(rec, label):
    if rec is None:
        print(f"  {label:<46s} FAILED")
        return
    print(f"  {label:<46s} fit {rec['fit_rms']:6.3f}  ALL {rec['all_rms']:6.3f} dB rms"
          f"  ({rec['n']} pts)  level {rec['level']:+5.2f} dB"
          f"  worst cell {tuple(rec['worst'][0])} {rec['worst'][1]:.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=20)
    ap.add_argument("--stage", default="baseline")
    a = ap.parse_args()

    A = load_dir(RB)
    print(f"ReBirth curves: {len(A)} cells, floor {FLOOR_DB} dB below passband")
    print(f"fit cells {len(FIT_CELLS)}, held out {len(A)-len(FIT_CELLS)}\n")
    cache = load_cache()

    if a.stage == "baseline":
        show(evaluate({}, A, cache, a.jobs), "SHIPPED (branch 303-corner-rise)")
        # What the branch's own predecessor scored, for context.
        show(evaluate({"REBORN_RESO_OCT": 0.0}, A, cache, a.jobs),
             "RESO_OCT=0 (the pre-branch model)")
        return

    if a.stage == "coarse":
        grid = dict(
            cs=(0.70, 0.85, 1.00),
            slope=(2.5, 2.9, 3.3),
            ro=(0.0, 0.25, 0.50),
            rc=(0.0, 0.35, 0.70),
        )
        combos = list(itertools.product(grid["cs"], grid["slope"], grid["ro"], grid["rc"]))
        print(f"{len(combos)} combinations\n")
        best = None
        for cs, sl, ro, rc in combos:
            p = {"REBORN_CUT_SCALE": cs, "REBORN_CUT_SLOPE": sl,
                 "REBORN_RESO_OCT": ro, "REBORN_RESO_OCT_CUT": rc}
            rec = evaluate(p, A, cache, a.jobs)
            show(rec, f"cs{cs} slope{sl} ro{ro} rc{rc}")
            if rec and (best is None or rec["fit_rms"] < best["fit_rms"]):
                best = rec
        if best:
            print("\nBEST on the fit cells:")
            show(best, "best")
        return

    if a.stage.startswith("joint"):
        # Refit the FILTER with the drive pinned linear-ish, which is the joint
        # fit docs/research/RUNBOOK.md item 2 asks for. drive_probe.py shows the two
        # instruments disagree: lowering the drive takes the linearity residual
        # from 2.31 dB to 0.07 (ReBirth is 0.03) while the filter rms goes 2.54
        # to 4.24. Either the shaper is real, or it has been standing in for a
        # filter that is too dark above the corner. Only a refit at the low
        # drive can tell those apart, and that is this.
        drive = a.stage.split(":", 1)[1] if ":" in a.stage else "0.25"
        cur = {"REBORN_DRIVE": drive,
               "REBORN_CUT_SCALE": 1.05, "REBORN_CUT_SLOPE": 2.8,
               "REBORN_RESO_OCT": 0.325, "REBORN_RESO_OCT_CUT": 0.425,
               "REBORN_K_B": 3.40, "REBORN_HFQ_ON": 4000.0}
        axes = {
            "REBORN_CUT_SCALE": [0.85, 0.95, 1.05, 1.15, 1.25],
            "REBORN_CUT_SLOPE": [2.4, 2.6, 2.8, 3.0, 3.2],
            "REBORN_RESO_OCT": [0.15, 0.25, 0.325, 0.45, 0.60],
            "REBORN_RESO_OCT_CUT": [0.20, 0.325, 0.425, 0.55, 0.70],
            "REBORN_K_B": [2.8, 3.1, 3.4, 3.7, 3.9],
            "REBORN_HFQ_ON": [3000.0, 4000.0, 5000.0, 9000.0],
        }
        rec = evaluate(dict(cur), A, cache, a.jobs)
        show(rec, f"start (drive {drive})")
        bestv = rec["fit_rms"]
        for it in range(4):
            moved = False
            for ax, vals in axes.items():
                trials = []
                for v in vals:
                    p = dict(cur)
                    p[ax] = v
                    r = evaluate(p, A, cache, a.jobs)
                    if r:
                        trials.append((r["fit_rms"], v, r))
                trials.sort()
                if trials and trials[0][0] < bestv - 1e-4:
                    bestv, cur[ax] = trials[0][0], trials[0][1]
                    moved = True
                    show(trials[0][2], f"p{it} {ax.replace('REBORN_','')}={trials[0][1]}")
            if not moved:
                print(f"  converged after pass {it}")
                break
        print("\nFINAL (drive pinned):")
        show(evaluate(dict(cur), A, cache, a.jobs), "final")
        print("  " + "  ".join(f"{k.replace('REBORN_','')}={v}" for k, v in cur.items()))
        print(f"\n  compare: drive 1.0 refitted filter = 2.543 dB rms with a"
              f" 2.31 dB linearity residual.")
        return

    if a.stage == "refine":
        # Coordinate descent from the coarse optimum. The parameters are
        # coupled, so each pass re-sweeps every axis against the current best of
        # the others and repeats until nothing moves. That explores the valley a
        # fixed grid would only sample.
        cur = {"REBORN_CUT_SCALE": 1.0, "REBORN_CUT_SLOPE": 2.9,
               "REBORN_RESO_OCT": 0.25, "REBORN_RESO_OCT_CUT": 0.35,
               "REBORN_K_B": 3.70, "REBORN_HFQ_ON": 3000.0}
        axes = {
            "REBORN_CUT_SCALE": [0.90, 0.95, 1.00, 1.05, 1.10],
            "REBORN_CUT_SLOPE": [2.7, 2.8, 2.9, 3.0, 3.1],
            "REBORN_RESO_OCT": [0.10, 0.175, 0.25, 0.325, 0.40],
            "REBORN_RESO_OCT_CUT": [0.20, 0.275, 0.35, 0.425, 0.50],
            "REBORN_K_B": [3.10, 3.40, 3.70, 3.90],
            "REBORN_HFQ_ON": [3000.0, 4000.0, 5000.0, 9000.0],
        }
        rec = evaluate(dict(cur), A, cache, a.jobs)
        show(rec, "start")
        bestv = rec["fit_rms"]
        for it in range(3):
            moved = False
            for ax, vals in axes.items():
                trials = []
                for v in vals:
                    p = dict(cur)
                    p[ax] = v
                    r = evaluate(p, A, cache, a.jobs)
                    if r:
                        trials.append((r["fit_rms"], v, r))
                if not trials:
                    continue
                trials.sort()
                if trials[0][0] < bestv - 1e-4:
                    bestv, cur[ax] = trials[0][0], trials[0][1]
                    moved = True
                    show(trials[0][2], f"pass{it} {ax.replace('REBORN_','')}={trials[0][1]}")
            if not moved:
                print(f"  converged after pass {it}")
                break
        final = evaluate(dict(cur), A, cache, a.jobs)
        print("\nFINAL:")
        show(final, "final")
        print("  " + "  ".join(f"{k.replace('REBORN_','')}={v}" for k, v in cur.items()))


if __name__ == "__main__":
    main()
