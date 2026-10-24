#!/usr/bin/env python3
"""Score candidate preset tables and envelope models against a measured trace.

Reading velocities off a trajectory by eye works while the decay is short enough
that each trigger stands alone, and stops working the moment it is not. So this
goes the other way: take a candidate preset table and a candidate envelope
model, SIMULATE the cutoff parameter the fixture should have produced, and score
it against what ReBirth actually did.

That compares whole trajectories rather than picked peaks, it uses the parts
between triggers as well as the triggers, and it cannot be fooled by a window
landing in the wrong place.

Two tables:  the shipped `pcf_presets.inc`, and Appendix D read by pixel.
Two models:  RESET, where a trigger sets the envelope to its velocity, and MAX,
             where it raises the envelope to its velocity but never lowers it.

    python pcf_model_ab.py <render-dir> t_p12_long --pattern 12 --decay 96
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_cutoff import trajectory
from pcf_appendixd import read_all
from pcf_presets_ab import read_inc


def simulate(steps, vels, length32, bpm, tau, dur, freq, amt,
             model="reset", sr=1000.0):
    """The cutoff parameter this pattern should produce, at `sr` samples/second."""
    n = int(dur * sr)
    t = np.arange(n) / sr
    env = np.zeros(n)
    step32 = 60.0 / bpm / 8.0
    fires = {}
    for s, v in zip(steps, vels):
        k = 0
        while (s + k * length32) * step32 < dur:
            fires[int(round((s + k * length32) * step32 * sr))] = v
            k += 1
    e = 0.0
    dec = float(np.exp(-1.0 / (tau * sr)))
    for i in range(n):
        if i in fires:
            v = fires[i]
            e = v if model == "reset" else max(e, v)
        env[i] = e
        e *= dec
    return t, np.clip(freq + amt * env, 0.0, 1.0)


def score(meas_t, meas_p, sim_t, sim_p):
    """RMS parameter error over the frames the measurement actually resolved."""
    ok = np.isfinite(meas_p)
    if ok.sum() < 50:
        return np.nan, 0
    s = np.interp(meas_t[ok], sim_t, sim_p)
    return float(np.sqrt(np.mean((meas_p[ok] - s) ** 2))), int(ok.sum())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("fixture")
    ap.add_argument("--ref", default="t_ref.wav")
    ap.add_argument("--pattern", type=int, required=True)
    ap.add_argument("--bpm", type=float, default=90.0)
    ap.add_argument("--tau", type=float, required=True, help="seconds")
    ap.add_argument("--freq", type=float, default=0.0, help="Freq byte")
    ap.add_argument("--amt", type=float, default=127.0, help="Amt byte")
    a = ap.parse_args(argv)

    ref = os.path.join(a.render_dir, a.ref)
    filt = os.path.join(a.render_dir, a.fixture + ".wav")
    mt, mp, _ = trajectory(ref, filt)
    dur = float(mt[-1])

    inc = read_inc()
    man = read_all()
    tables = {}
    if a.pattern in inc:
        s, v, L = inc[a.pattern]
        tables["shipped"] = (s, v, L)
    if a.pattern in man:
        s, v, L, _ = man[a.pattern]
        tables["AppendixD"] = ([int(round(x)) for x in s], v, L)

    print("pattern %d, %s, tau %.0f ms, Freq %g, Amt %g"
          % (a.pattern, a.fixture, a.tau * 1000, a.freq, a.amt))
    print("%-12s %-7s %10s %10s" % ("table", "model", "rms param", "frames"))
    best = None
    for tname, (s, v, L) in tables.items():
        for model in ("reset", "max"):
            st, sp = simulate(s, v, L, a.bpm, a.tau, dur,
                              a.freq / 127.0, a.amt / 127.0, model)
            r, n = score(mt, mp, st, sp)
            print("%-12s %-7s %10.4f %10d" % (tname, model, r, n))
            if best is None or (np.isfinite(r) and r < best[0]):
                best = (r, tname, model)
    if best:
        print("\nbest: %s + %s at %.4f" % (best[1], best[2], best[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
