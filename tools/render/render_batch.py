#!/usr/bin/env python3
"""Render a directory of .rbs fixtures with reborn_render, one wav per fixture.

Every A/B round needs our side re-rendered after a constant changes, and that
loop was being hand-written in the shell each time. It belongs in the repo: the
flags below are the ones that make a render comparable to a ReBirth export, and
getting any of them wrong quietly invalidates the measurement.

  --no-fx   the reference exports were all made with the effect rack off
  --solo    one component at a time, per docs/MEASUREMENT_PROTOCOL.md
  stereo    reborn_render always writes stereo; never mono-sum without saying so

Environment overrides (REBORN_*) are passed through, so a fit loop can sweep a
constant without rebuilding:

    REBORN_K_B=3.4 python render_batch.py ../fixtures/batchH out/ --solo bass1

Usage:
    python render_batch.py <fixture-dir> <out-dir> [--solo bass1] [--seconds N]
                           [--exe path] [--jobs N] [--filter substring]
"""
import argparse
import concurrent.futures
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def default_exe():
    for rel in ("build/Release/reborn_render.exe", "build/reborn_render",
                "build/Release/reborn_render"):
        p = os.path.join(REPO, rel)
        if os.path.exists(p):
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures")
    ap.add_argument("outdir")
    ap.add_argument("--solo", default="bass1")
    ap.add_argument("--seconds", type=float, default=8.0)
    ap.add_argument("--exe", default=None)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--filter", default="")
    ap.add_argument("--fx", action="store_true",
                    help="leave the effect rack ON (default is --no-fx)")
    a = ap.parse_args()

    exe = a.exe or default_exe()
    if not exe or not os.path.exists(exe):
        sys.exit("reborn_render not found; build first, or pass --exe")

    srcs = sorted(glob.glob(os.path.join(a.fixtures, "*.rbs")))
    if a.filter:
        srcs = [s for s in srcs if a.filter in os.path.basename(s)]
    if not srcs:
        sys.exit(f"no .rbs fixtures matched in {a.fixtures}")
    os.makedirs(a.outdir, exist_ok=True)

    def one(src):
        stem = os.path.splitext(os.path.basename(src))[0]
        out = os.path.join(a.outdir, stem + ".wav")
        cmd = [exe, "--song", src, "--out", out,
               "--seconds", str(a.seconds), "--solo", a.solo]
        if not a.fx:
            cmd.append("--no-fx")
        r = subprocess.run(cmd, capture_output=True, text=True)
        return stem, r.returncode, (r.stderr or r.stdout).strip()

    fails = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for stem, rc, msg in ex.map(one, srcs):
            if rc != 0:
                fails += 1
                print(f"FAIL {stem}: {msg}")
    print(f"rendered {len(srcs) - fails}/{len(srcs)} to {a.outdir}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
