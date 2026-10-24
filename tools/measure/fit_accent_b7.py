#!/usr/bin/env python3
"""Fit the 303's accent on batch B7, the knob CROSSED with cutoff and resonance.

`fit_accent.py` fits the accent's SHAPE at one setting. `fit_accent_knob.py`
fits its LAW along the knob at one patch, and that was not enough: the law
fitted on B3's patch was rejected by the regression net on accentdepth's, and
the two patches differ in both cutoff and resonance so neither could say which
axis mattered.

B7 crosses them, and the answer is that IT IS CUTOFF. Span of the accent's
brightness effect across the knob, ReBirth minus ours:

    resonance axis, cutoff 64    reso 16: 4.02   reso 60: 4.54   reso 100: 5.25
    cutoff axis, resonance 60    cut 32: 5.69    cut 64: 4.54    cut 96:   1.34

Resonance moves the deficit by 1.2 dB across its whole range and cutoff moves it
by 4.4. So the constant to look at is not `ACC_RESO_SLOPE`, which is where the
suspicion was, and the accent's corner push needs a CUTOFF term it does not have.

Scored over all 25 cells at once, five knobs at each of five operating points,
each against that cell's own no-accent control.

    python fit_accent_b7.py <rebirth-dir> --check ACC_SWEEP=4.2
    python fit_accent_b7.py <rebirth-dir> --grid
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_envelope import load, centroid_track, fold, CLEAN

WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")
FIX = os.path.join(WS, "fixtures-2026-08-29", "b7-303-accent")

CELLS = [(64, 16), (64, 60), (64, 100), (32, 60), (96, 60)]
KNOBS = (0, 32, 64, 96, 127)
ACC_STEPS = (4, 12)
NAMES = ([ "b7_c%03d_r%03d_plain" % (c, r) for c, r in CELLS ] +
         [ "b7_c%03d_r%03d_k%03d" % (c, r, k) for c, r in CELLS for k in KNOBS ])


def folded(path):
    x, sr = load(path)
    _, c, lv = centroid_track(x, sr)
    return fold(None, c, sr)[:, :CLEAN + 1], fold(None, lv, sr)[:, :CLEAN + 1]


def surface(d):
    """(brightness, level) depth, cells by knobs, each against its own control."""
    B = np.full((len(CELLS), len(KNOBS)), np.nan)
    L = np.full((len(CELLS), len(KNOBS)), np.nan)
    for i, (c, r) in enumerate(CELLS):
        p0 = os.path.join(d, "b7_c%03d_r%03d_plain.wav" % (c, r))
        if not os.path.exists(p0):
            continue
        rb, rl = folded(p0)
        for j, k in enumerate(KNOBS):
            p = os.path.join(d, "b7_c%03d_r%03d_k%03d.wav" % (c, r, k))
            if not os.path.exists(p):
                continue
            fb, fl = folded(p)
            B[i, j] = np.nanmean([np.nanmean(fb[s] - rb[s]) for s in ACC_STEPS])
            L[i, j] = np.nanmean([np.nanmean(fl[s] - rl[s]) for s in ACC_STEPS])
    return B, L


def render(overrides, out):
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for k, v in overrides.items():
        env["REBORN_" + k] = str(v)
    for n in NAMES:
        subprocess.run([EXE, "--song", os.path.join(FIX, n + ".rbs"),
                        "--no-fx", "--seconds", "8",
                        "--out", os.path.join(out, n + ".wav")],
                       check=True, env=env, stdout=subprocess.DEVNULL)


def rms(a, b):
    d = a - b
    ok = np.isfinite(d)
    return float(np.sqrt((d[ok] ** 2).mean())) if ok.any() else np.inf


def parse(items):
    o = {}
    for it in items or []:
        k, _, v = it.partition("=")
        o[k.strip().upper()] = v.strip()
    return o


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("--check", nargs="*", default=None, metavar="KEY=VAL")
    ap.add_argument("--grid", default=None, choices=("sweep", "cut"))
    a = ap.parse_args(argv)
    BR, LR = surface(a.ref_dir)

    def run(o):
        with tempfile.TemporaryDirectory() as td:
            render(o, td)
            return surface(td)

    if a.grid:
        if a.grid == "sweep":
            cands = [{"ACC_SWEEP": w, "ACC_RESO_SLOPE": s}
                     for w in (3.30, 4.00, 4.70, 5.40)
                     for s in (-0.708, -0.50, -0.30)]
        else:
            cands = [{"ACC_SWEEP": w, "ACC_SHORT": s}
                     for w in (3.30, 4.00, 4.70)
                     for s in (0.26, 0.30, 0.34)]
        best = None
        print("%-48s %9s %9s" % ("candidate", "bright", "level"))
        for c in cands:
            B, L = run(c)
            eb, el = rms(BR, B), rms(LR, L)
            print("%-48s %9.3f %9.3f"
                  % (" ".join("%s=%s" % kv for kv in sorted(c.items())), eb, el))
            sys.stdout.flush()
            if best is None or eb < best[0]:
                best = (eb, c)
        print("\nbest brightness: %s at %.3f"
              % (" ".join("%s=%s" % kv for kv in sorted(best[1].items())), best[0]))
        return 0

    o = parse(a.check)
    B, L = run(o)
    print("overrides: %s" % (" ".join("%s=%s" % kv for kv in sorted(o.items()))
                             or "(defaults)"))
    print("brightness %.3f dB rms, level %.3f dB rms" % (rms(BR, B), rms(LR, L)))
    print("%-12s %10s %10s %10s" % ("cell", "RB span", "our span", "deficit"))
    for i, (c, r) in enumerate(CELLS):
        sa, sb = BR[i, -1] - BR[i, 0], B[i, -1] - B[i, 0]
        print("%-12s %10.2f %10.2f %10.2f" % ("c%d r%d" % (c, r), sa, sb, sa - sb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
