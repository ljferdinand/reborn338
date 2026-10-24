#!/usr/bin/env python3
"""Fit the PCF's decay from the whole trajectory, not from one crossing time.

The law in `pcf.cpp` came from measuring how long each trigger took to carry the
cutoff down past ONE fixed frequency. That is proportional to tau if and only if
the SHAPE is already known, and the shape was assumed rather than measured. Its
residuals reached 15 percent and byte 80 came out at 197.8 ms where the trend
said about 380, which is the signature of a metric that cannot tell a wrong time
constant from a wrong shape.

So this fits the trajectory, and it fits two shapes rather than one:

    exponential   param(t) = v * exp(-t / tau)
    linear        param(t) = v - t / T

Those are genuinely different claims about the hardware. Exponential in the
PARAMETER means the envelope is a plain RC decay feeding an exponential
frequency control, which is the analogue arrangement. Linear in the parameter
means a constant number of octaves per second, which is what a digital design
that decrements a pitch counter would do. A single crossing time cannot
distinguish them; a trajectory can, and reports which one it is.

Each fixture is pattern 0, one trigger per whole note, which is the longest
unobstructed decay any preset offers.

    python pcf_decay.py <render-dir>
    python pcf_decay.py <render-dir> --tempo 30 --prefix d_t30_
"""
import argparse
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_cutoff import trajectory

MIN_POINTS = 8          # a decay with fewer resolved frames than this says nothing

# THE ESTIMATOR'S OWN BIAS, measured rather than assumed.
#
# Our engine's decay law is known exactly, so rendering the same fixtures with
# it and fitting them the same way says what this method does to a tau it
# already knows. Over bytes 16 to 127, two decades from 22 ms to 2.3 s, the
# fitted tau is high by a factor of 1.046 with a standard deviation of 0.007 and
# no trend, so it is a constant and divides out. After correction the method
# recovers a known tau to within 1.2 percent.
#
# Round 5's crossing-time metric had no equivalent number because a single
# crossing cannot produce one. That is most of why its residuals reached
# 15 percent and why byte 80 could sit 180 ms off the trend unchallenged.
#
# Bytes 0 and 8 do not resolve at all: tau there is 11 and 15 ms against an
# 11.6 ms analysis window, so there is no trajectory left to fit. They need a
# shorter window and a source that survives one, and are reported as such
# rather than extrapolated.
BIAS = 1.046


def segments(t, p, period_s, guard=0.0):
    """One decay per trigger, taken from the KNOWN grid rather than searched.

    Pattern 0 fires on the whole note and the render starts at the pattern's
    start, so the trigger times are arithmetic. Searching for the peak instead
    finds the ONSET OF THE NEXT trigger, because a frame's window reaches
    forward past its own timestamp; `guard` trims that much off the end of each
    segment so the next trigger cannot appear inside this one.
    """
    out = []
    k = 0
    while (k + 1) * period_s <= t[-1]:
        t0 = k * period_s
        m = (t >= t0) & (t < t0 + period_s - guard)
        tt, pp = t[m] - t0, p[m]
        ok = np.isfinite(pp)
        if ok.sum() >= MIN_POINTS:
            out.append((tt[ok], pp[ok]))
        k += 1
    return out


def fit_one(tt, pp):
    """(tau, rms_exp, slope, rms_lin, peak) for one decay."""
    peak = pp[0]
    # Exponential: a straight line through log(param).
    good = pp > 1e-4
    tau = rms_e = np.nan
    if good.sum() >= MIN_POINTS:
        b, a = np.polyfit(tt[good], np.log(pp[good]), 1)
        if b < 0:
            tau = -1.0 / b
            rms_e = float(np.sqrt(np.mean(
                (pp[good] - np.exp(a) * np.exp(b * tt[good])) ** 2)))
    # Linear: a straight line through param itself.
    b2, a2 = np.polyfit(tt, pp, 1)
    rms_l = float(np.sqrt(np.mean((pp - (a2 + b2 * tt)) ** 2)))
    slope = -1.0 / b2 if b2 < 0 else np.nan
    return tau, rms_e, slope, rms_l, peak


def analyse(ref, filt, bpm, n=512, hop=128, sr=44100.0):
    t, p, _ = trajectory(ref, filt, n, hop)
    period = 60.0 / bpm * 4.0                # pattern 0 fires every whole note
    segs = segments(t, p, period, guard=n / sr)
    rows = [fit_one(tt, pp) for tt, pp in segs]
    rows = [r for r in rows if np.isfinite(r[0])]
    if not rows:
        return None
    a = np.array(rows)
    return dict(n=len(rows), tau=float(np.median(a[:, 0])) / BIAS,
                tau_sd=float(np.std(a[:, 0])) / BIAS,
                rms_exp=float(np.median(a[:, 1])),
                slope=float(np.nanmedian(a[:, 2])),
                rms_lin=float(np.median(a[:, 3])),
                peak=float(np.median(a[:, 4])))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("--prefix", default="d_t90_")
    ap.add_argument("--ref", default="d_ref90.wav")
    ap.add_argument("--tempo", type=float, default=90.0)
    ap.add_argument("--raw", action="store_true",
                    help="do not divide out the estimator's measured 1.046 bias")
    a = ap.parse_args(argv)
    if a.raw:
        globals()["BIAS"] = 1.0

    ref = os.path.join(a.render_dir, a.ref)
    if not os.path.exists(ref):
        print("no reference at %s" % ref)
        return 1
    files = sorted(f for f in os.listdir(a.render_dir)
                   if f.startswith(a.prefix) and f.endswith(".wav"))
    print("%5s %5s %8s %7s %8s %9s %8s  %s"
          % ("byte", "n", "tau ms", "sd ms", "peak", "rms exp", "rms lin", "shape"))
    for f in files:
        b = int(re.search(r"(\d+)\.wav$", f).group(1))
        r = analyse(ref, os.path.join(a.render_dir, f), a.tempo)
        if r is None:
            print("%5d %5s  (no resolvable decay)" % (b, "-"))
            continue
        shape = ("exponential" if r["rms_exp"] < r["rms_lin"] * 0.9
                 else "LINEAR" if r["rms_lin"] < r["rms_exp"] * 0.9
                 else "either")
        print("%5d %5d %8.1f %7.1f %8.3f %9.4f %8.4f  %s"
              % (b, r["n"], r["tau"] * 1000.0, r["tau_sd"] * 1000.0,
                 r["peak"], r["rms_exp"], r["rms_lin"], shape))
    return 0


if __name__ == "__main__":
    sys.exit(main())
