#!/usr/bin/env python3
"""Fit the 303's accent across the WHOLE KNOB, in brightness and in level.

WHY THIS EXISTS ALONGSIDE `fit_accent.py`. That tool fits the accent's shape
through a step, and it does it at ONE accent setting, knob 96, on `b3_run1`. It
found the three time constants and it is right about them. It cannot see the
knob LAW, and measured across the sweep the knob law is where the accent is now
wrong: ReBirth's accent moves brightness 13.6 dB from knob 0 to knob 127 and
ours moves 8.4. Fitting at one operating point and generalising is the trap this
project's audit is built out of, and this is that trap in the accent.

THE DENOMINATOR IS `b3_ref_plain`, THE SAME PATTERN WITH NO ACCENTED STEPS, and
the comparison is made on the SAME STEP INDICES, 4 and 12. So the pattern, the
note, the filter envelope and the VCA all cancel and what is left is the accent
alone. An accent knob at zero is NOT an unaccented pattern, which is why that
reference exists and why knob 0 is a measurement rather than a control.

TWO SCORES, because the accent does two things and they are fitted by different
constants:

  BRIGHTNESS  high-band over low-band, the tonal part. `ACC_SHORT` sets the
              knob-INDEPENDENT squelch, which is what knob 0 measures on its
              own, and `ACC_SWEEP` sets how much the knob adds on top.
  LEVEL       total power, the loudness part, set by `ACC_LVL`.

    python fit_accent_knob.py <rebirth-dir> --check ACC_SHORT=0.28 ACC_LVL=3.6
    python fit_accent_knob.py <rebirth-dir> --grid
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

KNOBS = (0, 32, 64, 96, 127)
ACC_STEPS = (4, 12)
NAMES = ["b3_k%03d" % k for k in KNOBS] + ["b3_ref_plain"]


def folded(path):
    x, sr = load(path)
    _, c, lv = centroid_track(x, sr)
    return fold(None, c, sr)[:, :CLEAN + 1], fold(None, lv, sr)[:, :CLEAN + 1]


def depths(d):
    """(brightness, level) depth per knob, against the no-accent reference."""
    rb, rl = folded(os.path.join(d, "b3_ref_plain.wav"))
    B = np.full(len(KNOBS), np.nan)
    L = np.full(len(KNOBS), np.nan)
    for i, k in enumerate(KNOBS):
        p = os.path.join(d, "b3_k%03d.wav" % k)
        if not os.path.exists(p):
            continue
        fb, fl = folded(p)
        B[i] = np.nanmean([np.nanmean(fb[s] - rb[s]) for s in ACC_STEPS])
        L[i] = np.nanmean([np.nanmean(fl[s] - rl[s]) for s in ACC_STEPS])
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
    ap.add_argument("--grid", action="store_true")
    a = ap.parse_args(argv)

    BR, LR = depths(a.ref_dir)

    def run(o):
        with tempfile.TemporaryDirectory() as td:
            render(o, td)
            return depths(td)

    if a.grid:
        best = None
        print("%-46s %8s %8s %8s" % ("candidate", "bright", "level", "sum"))
        cands = [{"ACC_SHORT": s, "ACC_SWEEP": w, "ACC_LVL": l}
                 for s in (0.20, 0.26, 0.34)
                 for w in (3.30, 4.20, 5.10)
                 for l in (3.2, 3.6)]
        for c in cands:
            B, L = run(c)
            eb, el = rms(BR, B), rms(LR, L)
            print("%-46s %8.3f %8.3f %8.3f"
                  % (" ".join("%s=%s" % kv for kv in sorted(c.items())),
                     eb, el, eb + el))
            sys.stdout.flush()
            if best is None or eb + el < best[0]:
                best = (eb + el, c)
        print("\nbest: %s at %.3f"
              % (" ".join("%s=%s" % kv for kv in sorted(best[1].items())), best[0]))
        return 0

    o = parse(a.check)
    B, L = run(o)
    print("overrides: %s" % (" ".join("%s=%s" % kv for kv in sorted(o.items()))
                             or "(defaults)"))
    print("brightness %.3f dB rms, level %.3f dB rms" % (rms(BR, B), rms(LR, L)))
    print("%-8s %19s %19s" % ("knob", "brightness R / ours", "level R / ours"))
    for i, k in enumerate(KNOBS):
        print("%-8d %8.2f %8.2f %2s %8.2f %8.2f" % (k, BR[i], B[i], "", LR[i], L[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
