#!/usr/bin/env python3
"""Render batch H under a set of REBORN_* overrides and score the filter.

The inner loop of the resonance-span hunt. One command per hypothesis:

    python span_probe.py --tag rawtap REBORN_RAW_TAP=1

reports the three numbers that matter, all against the same ReBirth grid:

  rolloff rms   85% rolloff error in octaves, over all 36 cells
  prom rms      resonant peak prominence error in dB, over all 36 cells
  span row      (rolloff at reso 127) / (rolloff at reso 0), per cutoff

The span row is the diagnostic. ReBirth runs 1.92 / 2.24 / 2.99 / 3.59 / 3.96 /
4.45 across cutoff 16 / 40 / 64 / 90 / 110 / 127, i.e. its filter opens more than
four times as far at high cutoff as the resonance knob travels. The shipped build
runs 1.77 / 1.91 / 1.86 / 2.17 / 2.31 / 2.36. Two refits have moved the rms
errors without moving that row, which is why it gets its own tool.

Renders go to renders/probe_<tag>/ so parallel probes do not collide.
"""
import argparse
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from batchH_filter import measure, CUT, RESO           # noqa: E402

WS = os.path.dirname(REPO)
FIX = os.path.join(WS, "fixtures-2026-07-27", "batchH")
RB = os.path.join(WS, "renders", "batchH_rb")

RB_SPAN = (1.92, 2.24, 2.99, 3.59, 3.96, 4.45)


def score(d):
    A, B = measure(RB), measure(d)
    k = sorted(set(A) & set(B))
    if not k:
        return None
    roll = np.sqrt(np.mean([np.log2(B[x][0] / A[x][0]) ** 2 for x in k]))
    prom = np.sqrt(np.mean([(B[x][2] - A[x][2]) ** 2 for x in k]))
    span = [B[(c, RESO[-1])][0] / B[(c, RESO[0])][0] if (c, RESO[-1]) in B and (c, RESO[0]) in B
            else float("nan") for c in CUT]
    return roll, prom, span, len(k)


def run(tag, overrides, seconds=12.0, jobs=4):
    out = os.path.join(WS, "renders", "probe_" + tag)
    env = dict(os.environ)
    env.update(overrides)
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "render", "render_batch.py"),
                        FIX, out, "--seconds", str(seconds), "--jobs", str(jobs)],
                       env=env, capture_output=True, text=True, cwd=REPO)
    if r.returncode != 0:
        print(f"  render FAILED: {(r.stderr or r.stdout).strip()[:300]}")
        return None
    return score(out)


def report(tag, s, overrides):
    if s is None:
        print(f"{tag:<22s} FAILED")
        return
    roll, prom, span, n = s
    ov = " ".join(f"{k.replace('REBORN_','')}={v}" for k, v in overrides.items()) or "(shipped)"
    print(f"{tag:<22s} roll {roll:6.4f}  prom {prom:5.2f}  span " +
          " ".join(f"{v:5.2f}" for v in span) + f"   [{ov}]")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", default="probe")
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("overrides", nargs="*", help="REBORN_X=value ...")
    a = ap.parse_args()
    ov = dict(o.split("=", 1) for o in a.overrides)
    print(f"{'':<22s} {'':>11s}  {'':>10s}  span " +
          " ".join(f"cut{c:<3d}" for c in CUT))
    print(f"{'ReBirth':<22s} roll 0.0000  prom  0.00  span " +
          " ".join(f"{v:5.2f}" for v in RB_SPAN))
    report(a.tag, run(a.tag, ov, a.seconds, a.jobs), ov)
