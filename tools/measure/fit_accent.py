#!/usr/bin/env python3
"""Fit the 303's accent time constants against ReBirth's measured trajectory.

The target is not a summary number, it is the SHAPE of what the accent does to
brightness across the step it fires on and the step after it. That shape, taken
from batch B3's `b3_run1` against `b3_ref_plain`, is:

    ReBirth  step 0:  +2.7  +1.5  +1.0  +1.3  -0.7  -3.9  -9.2 -13.6 -16.3 -16.7
             step 1:  -0.2  +0.2  +0.2   0.0  +0.2  -0.1  +0.1  +0.3  -0.3  +0.2

twelve milliseconds a column. Three things are in it and only the first was ever
fitted. The accent is bright IMMEDIATELY and that brightness is gone inside
40 ms. It then drives the note far DARKER than an unaccented one for the rest of
the step, reaching -16.7 dB. And it leaves nothing at all behind: the following
step is flat to within 0.3 dB.

Ours does none of the three. It peaks late at 48 ms, reaches only -5.0 dB of
squelch, and leaves +1.5 dB on the next step.

Three constants, all env-overridable so no rebuild is needed:

    ACC_ATK    accent envelope attack, seconds        shipped 0.010
    ACC_DEC    accent envelope decay, seconds         shipped 0.065
    ACC_SHORT  main-envelope decay multiplier while
               an accent is latched                   shipped 0.45

Scored as rms dB against the target over both steps, so a candidate cannot buy
the squelch by leaking into the next step or the other way round.

    python fit_accent.py <rebirth-dir> --grid
    python fit_accent.py <rebirth-dir> --check 0.004 0.020 0.20
"""
import argparse
import itertools
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
FIX = os.path.join(WS, "fixtures-2026-08-29", "b2b3-303-env")
STEPS_USED = (0, 1)


def trajectory(dirpath):
    """Accent minus no-accent brightness, over the steps under test."""
    a, sr = load(os.path.join(dirpath, "b3_run1.wav"))
    b, _ = load(os.path.join(dirpath, "b3_ref_plain.wav"))
    _, ca, _ = centroid_track(a, sr)
    _, cb, _ = centroid_track(b, sr)
    fa, fb = fold(None, ca, sr), fold(None, cb, sr)
    # Drop the tail of each step: the window straddles the next onset there and
    # the difference is an edge artefact, not the accent. TIGHTENED 2026-08-29
    # from `per - 4` to CLEAN, the last column whose window ends inside the
    # step. The old cut still let three contaminated columns through. Re-scored
    # after the change the shipped constants are unmoved and still the minimum,
    # so the accent refit does not rest on it.
    keep = slice(0, CLEAN + 1)
    return np.concatenate([(fa[s] - fb[s])[keep] for s in STEPS_USED])


def render(atk, dec, short, out):
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    env["REBORN_ACC_ATK"] = "%g" % atk
    env["REBORN_ACC_DEC"] = "%g" % dec
    env["REBORN_ACC_SHORT"] = "%g" % short
    for n in ("b3_run1", "b3_ref_plain"):
        subprocess.run([EXE, "--song", os.path.join(FIX, n + ".rbs"),
                        "--no-fx", "--seconds", "8",
                        "--out", os.path.join(out, n + ".wav")],
                       check=True, env=env, stdout=subprocess.DEVNULL)


def score(target, cand):
    n = min(len(target), len(cand))
    ok = np.isfinite(target[:n]) & np.isfinite(cand[:n])
    if ok.sum() < 20:
        return np.inf
    d = target[:n][ok] - cand[:n][ok]
    return float(np.sqrt((d * d).mean()))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("--grid", action="store_true")
    ap.add_argument("--check", nargs=3, type=float,
                    metavar=("ATK", "DEC", "SHORT"))
    a = ap.parse_args(argv)

    target = trajectory(a.ref_dir)
    print("target, ReBirth: %d columns, %.1f to %.1f dB"
          % (len(target), np.nanmin(target), np.nanmax(target)))

    if a.check:
        with tempfile.TemporaryDirectory() as td:
            render(*a.check, out=td)
            c = trajectory(td)
        print("ACC_ATK %g  ACC_DEC %g  ACC_SHORT %g   rms %.2f dB"
              % (a.check[0], a.check[1], a.check[2], score(target, c)))
        n = min(len(target), len(c))
        print("  target: " + " ".join("%5.1f" % v for v in target[:n:4]))
        print("  ours:   " + " ".join("%5.1f" % v for v in c[:n:4]))
        return 0

    best = None
    ATK = (0.002, 0.004, 0.008, 0.012)
    DEC = (0.010, 0.018, 0.030, 0.050, 0.065)
    SHORT = (0.10, 0.20, 0.30, 0.45)
    print("\n%8s %8s %8s %8s" % ("ACC_ATK", "ACC_DEC", "ACC_SHORT", "rms dB"))
    with tempfile.TemporaryDirectory() as td:
        for atk, dec, short in itertools.product(ATK, DEC, SHORT):
            render(atk, dec, short, td)
            s = score(target, trajectory(td))
            print("%8g %8g %8g %8.2f" % (atk, dec, short, s))
            if best is None or s < best[0]:
                best = (s, atk, dec, short)
    print("\nbest: ACC_ATK %g  ACC_DEC %g  ACC_SHORT %g  at %.2f dB rms"
          % (best[1], best[2], best[3], best[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
