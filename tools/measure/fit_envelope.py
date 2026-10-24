#!/usr/bin/env python3
"""Fit the 303's FILTER ENVELOPE against ReBirth's measured trajectory (B4).

The target is the SHAPE of brightness across a step, on all 25 cells of batch
B2's env-mod by decay grid, which is a plane and not a line. Both sides play the
identical pattern, so brightness needs no model and no reference.

WHAT THE B2 SUMMARY GOT WRONG, AND IT MATTERS. `bass303_envelope.py` reports a
`floor` read from the last three columns of the folded step. A step is 125 ms
and the analysis window is 23.2 ms wide, so every column from 36 on is a window
that has already crossed into the NEXT NOTE. Those three columns are the next
onset, not the floor. That is why the 2026-08-29 table showed ReBirth's "floor"
barely depending on the decay knob: an onset does not depend on a decay knob.
Read the clean columns only, which is 0 to 34, ending at 99 ms.

THE SCORE IS SHAPE, NOT LEVEL. Each trajectory has its own column 0 subtracted
before comparison, so a static brightness offset cannot enter the fit. Our
corner anchor is about 1.6 dB bright at the note onset and that belongs to the
CUTOFF refit (defect 3), which is a separate fit taken after this one. Scoring
raw would let the envelope bend to absorb an anchor error, which is the failure
mode the audit is full of. The bias is reported next to the score so it stays
visible rather than hidden.

The grid then constrains the two things independently. A wrong decay TIME shows
up as the same span reached at the wrong moment, and it varies down the decay
axis. A wrong envelope DEPTH shows up as the wrong span, and it varies along the
env-mod axis. One number per cell could not separate them; 25 trajectories can.

    python fit_envelope.py <rebirth-dir> --report
    python fit_envelope.py <rebirth-dir> --check DEC_A=0.070 DEC_B=0.901
    python fit_envelope.py <rebirth-dir> --grid dec
"""
import argparse
import itertools
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_envelope import load, centroid_track, fold, STEPS

WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")
FIX = os.path.join(WS, "fixtures-2026-08-29", "b2b3-303-env")

CLEAN = 35          # columns 0..34; column 36 is the first to reach the next note
ENVMOD = (0, 32, 64, 96, 127)
DECAY = (0, 32, 64, 96, 127)
NAMES = ["b2_e%03d_d%03d" % (e, d) for e in ENVMOD for d in DECAY]


def trajectory(dirpath, names=NAMES):
    """Brightness through one step, per fixture, median over the sixteen steps."""
    out = np.full((len(names), CLEAN), np.nan)
    for i, n in enumerate(names):
        p = os.path.join(dirpath, n + ".wav")
        if not os.path.exists(p):
            continue
        x, sr = load(p)
        _, c, _ = centroid_track(x, sr)
        out[i] = np.nanmedian(fold(None, c, sr), axis=0)[:CLEAN]
    return out


def render(overrides, out, names=NAMES):
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for k, v in overrides.items():
        env["REBORN_" + k] = str(v)
    for n in names:
        subprocess.run([EXE, "--song", os.path.join(FIX, n + ".rbs"),
                        "--no-fx", "--seconds", "8",
                        "--out", os.path.join(out, n + ".wav")],
                       check=True, env=env, stdout=subprocess.DEVNULL)


def shape(T):
    """Each trajectory relative to its own onset, so level cannot enter."""
    return T - T[:, :1]


def usable(A, B):
    """Mask off each cell's tail once EITHER side's brightness has turned around.

    Brightness is monotone in the corner only while the corner is above the
    fundamental. Sweep the corner far enough below it and the 60 to 250 Hz
    denominator starts falling too, so the ratio bottoms out and climbs again.
    Past that turning point the measure carries no information about where the
    corner is, and a fit allowed to score there will read "deeper" as "better"
    without limit. It is the same call as the accent fit reading only the first
    62 ms: a measure is used where it means something and not past it.

    THE MASK IS READ OFF REBIRTH ALONE and never off the candidate. Taking the
    earlier of the two turning points looks fairer and is not: a candidate that
    sweeps too deep turns around sooner, so it would be scored on fewer and
    earlier columns than an accurate one, and "deeper" would buy a smaller
    scoring region. The window has to be a property of the reference, fixed
    across every candidate, or the search optimises the window instead of the
    fit.
    """
    m = np.zeros(A.shape, dtype=bool)
    for i in range(A.shape[0]):
        if not np.isfinite(A[i]).any():
            continue
        m[i, :int(np.nanargmin(A[i])) + 1] = True
    return m & np.isfinite(A) & np.isfinite(B)


def gains(A, B):
    """Per cell, the least-squares scale taking our fall onto ReBirth's.

    WHY THIS EXISTS, and it is the correction that saved the fit. Brightness is
    monotone in the corner but it is not CALIBRATED: how many dB of high-over-low
    energy one octave of corner movement buys depends on the slope and skirt of
    the filter doing the moving, and B1 has already shown ours is not ReBirth's
    there. So a raw dB residual cannot tell "our envelope travels too many
    octaves" from "our octaves are worth too many dB", and a fit allowed to score
    it will pay for the second with the first. It did: scored raw, B2 asked for
    EM_OCT 4.45 down to about 3.45, and batchI, envshape and envfloor all reject
    that by a wide margin while batchQ, which runs at env mod 0 and cannot see
    EM_OCT at all, was the only set that liked it.

    Dividing that gain out leaves TIMING, which is what the decay law is, and
    the gain itself is reported rather than hidden: if it were near 1.00 the
    depth would be right, and it is not.
    """
    g = np.full(A.shape[0], np.nan)
    sa, sb = shape(A), shape(B)
    ok = usable(A, B)
    for i in range(A.shape[0]):
        m = ok[i]
        if m.sum() < 6 or not np.any(sb[i][m] != 0):
            continue
        g[i] = float(np.dot(sa[i][m], sb[i][m]) / np.dot(sb[i][m], sb[i][m]))
    return g


def score(A, B):
    """(timing rms dB, level bias dB). A is ReBirth, B is ours.

    The residual after each cell's own brightness-per-octave gain is divided
    out, so this is a pure timing score. `gains()` says why.
    """
    g = gains(A, B)
    d = shape(A) - shape(B) * g[:, None]
    ok = usable(A, B) & np.isfinite(d)
    rms = float(np.sqrt((d[ok] ** 2).mean())) if ok.any() else np.inf
    lv = A[:, 0] - B[:, 0]
    return rms, float(np.nanmean(lv))


def per_cell(A, B):
    g = gains(A, B)
    d = shape(A) - shape(B) * g[:, None]
    ok = usable(A, B) & np.isfinite(d)
    out = np.full(A.shape[0], np.nan)
    for i in range(A.shape[0]):
        if ok[i].any():
            out[i] = np.sqrt((d[i][ok[i]] ** 2).mean())
    return out


def table(A, B, label):
    r = per_cell(A, B).reshape(len(ENVMOD), len(DECAY))
    print("\n%s, shape error in dB rms per cell" % label)
    print("%-9s %s" % ("envmod/dec", " ".join("%6d" % d for d in DECAY)))
    for i, e in enumerate(ENVMOD):
        print("%-9d %s" % (e, " ".join("%6.2f" % v for v in r[i])))


def parse_overrides(items):
    o = {}
    for it in items:
        k, _, v = it.partition("=")
        o[k.strip().upper()] = v.strip()
    return o


def evaluate(A, overrides):
    with tempfile.TemporaryDirectory() as td:
        render(overrides, td)
        B = trajectory(td)
    return B


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("--check", nargs="*", default=None,
                    metavar="KEY=VAL", help="score one candidate, e.g. DEC_A=0.070")
    ap.add_argument("--report", action="store_true",
                    help="print ReBirth against ours, cell by cell")
    ap.add_argument("--grid", default=None,
                    choices=("dec", "domain", "floor", "joint"))
    a = ap.parse_args(argv)

    A = trajectory(a.ref_dir)

    if a.grid:
        base = {}
        if a.grid == "dec":
            # Parametrised by the two ENDS and the curvature, not by the raw
            # coefficients. tau(knob) = A * 10^(B k + C k^2), so A is the time
            # constant at knob 0 and B is pinned by the time constant at knob
            # 127. Searching A and B directly moves both ends at once and the
            # score cannot say which end asked for it.
            cands = []
            for tau0 in (0.050, 0.060, 0.070, 0.085, 0.100, 0.120):
                for tau1 in (1.90, 2.14, 2.40):
                    for c in (0.0, 0.30, 0.584, 0.80, 1.00):
                        b = math.log10(tau1 / tau0) - c
                        cands.append({"DEC_A": "%.4f" % tau0,
                                      "DEC_B": "%.4f" % b,
                                      "DEC_C": "%.4f" % c})
        elif a.grid == "domain":
            cands = [{}, {"ENV_DOMAIN": "hz"}]
        elif a.grid == "floor":
            cands = [{"EM_FLOOR": f} for f in (0.4, 0.6, 0.85, 1.1, 1.4)]
        else:
            # Decay TIME crossed with envelope DEPTH. They are not independent
            # in the score, so they are searched together, but the printout
            # splits the error by decay knob and gives the env-mod-0 row on its
            # own. That row has no env mod, so its whole depth is EM_FLOOR and
            # it pins EM_FLOOR without EM_OCT in the way.
            cands = []
            for tau0 in (0.055, 0.070, 0.085, 0.100):
                for fl in (0.50, 0.60, 0.70, 0.80):
                    for oc in (3.0, 3.3, 3.6, 3.9):
                        b = math.log10(2.14 / tau0) - 0.584
                        cands.append({"DEC_A": "%.4f" % tau0,
                                      "DEC_B": "%.4f" % b,
                                      "DEC_C": "0.584",
                                      "EM_FLOOR": "%.3f" % fl,
                                      "EM_OCT": "%.3f" % oc})
        best = None
        print("%-64s %7s %7s %7s %7s" % ("candidate", "rms", "e000", "d000", "bias"))
        for c in cands:
            o = dict(base); o.update(c)
            B = evaluate(A, o)
            s, bias = score(A, B)
            pc = per_cell(A, B).reshape(len(ENVMOD), len(DECAY))
            e0 = float(np.sqrt(np.nanmean(pc[0] ** 2)))
            d0 = float(np.sqrt(np.nanmean(pc[:, 0] ** 2)))
            print("%-64s %7.3f %7.3f %7.3f %7.2f"
                  % (" ".join("%s=%s" % kv for kv in sorted(c.items())) or "(defaults)",
                     s, e0, d0, bias))
            if best is None or s < best[0]:
                best = (s, c)
            sys.stdout.flush()
        print("\nbest: %s at %.3f dB rms"
              % (" ".join("%s=%s" % kv for kv in sorted(best[1].items())) or "(defaults)",
                 best[0]))
        return 0

    o = parse_overrides(a.check or [])
    B = evaluate(A, o)
    s, bias = score(A, B)
    print("overrides: %s" % (" ".join("%s=%s" % kv for kv in sorted(o.items())) or "(defaults)"))
    print("shape error %.3f dB rms, onset level bias %+.2f dB (ReBirth minus ours)"
          % (s, bias))
    table(A, B, "candidate")
    g = gains(A, B).reshape(len(ENVMOD), len(DECAY))
    print("")
    print("brightness-per-octave gain, ReBirth over ours. 1.00 means our")
    print("envelope buys exactly as much brightness per octave as ReBirth's")
    print("%-9s %s" % ("envmod/dec", " ".join("%6d" % d for d in DECAY)))
    for i, e in enumerate(ENVMOD):
        print("%-9d %s" % (e, " ".join("%6.2f" % v for v in g[i])))
    if a.report:
        ms = np.arange(0, CLEAN, 5) * 128.0 / 44100.0 * 1000.0
        print("\nbrightness through one step, ReBirth over ours, dB")
        print("%-16s %s" % ("ms", " ".join("%6.0f" % m for m in ms)))
        for i, n in enumerate(NAMES):
            print("%-16s %s" % (n, " ".join("%6.1f" % v for v in A[i, ::5])))
            print("%-16s %s" % ("", " ".join("%6.1f" % v for v in B[i, ::5])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
