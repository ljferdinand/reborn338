#!/usr/bin/env python3
"""Joint fit of the three coupled 303 filter constants against batch H.

The corner rise, the base corner and the resonance feedback cannot be fitted one
after another: raising the corner and raising the feedback both lift the 85%
rolloff, but they pull the peak PROMINENCE in opposite directions relative to it,
so fitting either alone just relocates the error. Two previous rounds did exactly
that and moved the rms numbers without moving the resonance span.

Scored on all 36 cells, equally weighted between a rolloff term in octaves and a
prominence term in dB, plus an explicit span term so a fit cannot win by
averaging the diagnostic away.

Usage:  python joint_fit.py [--refine]
"""
import itertools, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
from span_probe import run, RB_SPAN
from batchH_filter import CUT

def score(s):
    if s is None: return None
    roll, prom, span, n = s
    spanerr = np.sqrt(np.mean([(np.log2(a / b)) ** 2 for a, b in zip(span, RB_SPAN)]))
    total = roll / 0.28 + prom / 3.95 + spanerr / 0.55
    return roll, prom, spanerr, total

GRID = dict(
    coarse=dict(cs=(0.55, 0.70, 0.85, 1.00), ro=(0.4, 0.7, 1.0), kb=(2.0, 3.0)),
    refine=dict(cs=(0.70, 0.80, 0.90), ro=(0.5, 0.7, 0.9), kb=(3.0, 3.4, 3.7, 3.9)),
)

if __name__ == "__main__":
    g = GRID["refine" if "--refine" in sys.argv else "coarse"]
    print(f"{'CUT_SCALE':>10s} {'RESO_OCT':>9s} {'K_B':>5s} | {'roll oct':>9s} "
          f"{'prom dB':>8s} {'span':>6s} {'TOTAL':>7s}   span by cutoff")
    print(f"{'ReBirth':>10s} {'':>9s} {'':>5s} | {0.0:9.4f} {0.0:8.2f} {0.0:6.3f} {0.0:7.3f}   "
          + " ".join(f"{v:5.2f}" for v in RB_SPAN))
    best = None
    for cs, ro, kb in itertools.product(g["cs"], g["ro"], g["kb"]):
        ov = {"REBORN_CUT_SCALE": str(cs), "REBORN_RESO_OCT": str(ro),
              "REBORN_K_B": str(kb), "REBORN_K_CUT": "0"}
        s = run(f"jf_{cs}_{ro}_{kb}".replace(".", ""), ov)
        sc = score(s)
        if sc is None:
            print(f"{cs:10.2f} {ro:9.2f} {kb:5.2f} | FAILED"); continue
        roll, prom, spanerr, total = sc
        print(f"{cs:10.2f} {ro:9.2f} {kb:5.2f} | {roll:9.4f} {prom:8.2f} {spanerr:6.3f} "
              f"{total:7.3f}   " + " ".join(f"{v:5.2f}" for v in s[2]), flush=True)
        if best is None or total < best[0]: best = (total, cs, ro, kb, roll, prom, spanerr)
    if best:
        print(f"\nBEST  CUT_SCALE {best[1]}  RESO_OCT {best[2]}  K_B {best[3]}")
        print(f"      roll {best[4]:.4f} oct  prom {best[5]:.2f} dB  span {best[6]:.3f}")
        print(f"      shipped baseline: roll 0.2798  prom 3.95  span 0.548")
