#!/usr/bin/env python3
"""Recover the PCF's envelope from a fixture render, as cutoff against time.

The static rounds measured the filter with Amt = 0. With Amt above zero the
pattern drives the cutoff, so the envelope can be read back by estimating the
cutoff frame by frame and undoing the cutoff law.

Method. Both the fixture and its `pcf_ref` are cut into overlapping frames. In
each frame the ratio of the two power spectra AT THE DRONE'S HARMONICS is the
filter's magnitude response at that instant, exactly as in `pcf_response.py`,
and a one-dimensional search over cutoff fits it with Q and gain held at their
measured values. The result is fc(t), which the cutoff law turns back into the
0..1 parameter the envelope actually moves:

    param(t) = log2(fc(t) / 9.01) / 10.653

That is the domain to fit the envelope in. The envelope sums with the Freq knob
in PARAMETER space, not in hertz, so a decay that is exponential in parameter
looks like nothing in particular if it is fitted in hertz.

Two traps carried over from the 909 pitch work.

**The window smooths the envelope.** Every frame reports an average over its own
length, so a decay shorter than the window reads shallower and slower than it
is. The window is reported alongside every fit so the two are never separated,
and `--window` exists to check a fit against a different one.

**Frames are comparable across renders even though samples are not.** ReBirth's
exports are not sample-deterministic, but the SEQUENCER is: only oscillator
phase differs between two renders of one song, so frame N of the fixture and
frame N of the reference cover the same musical instant.

    python pcf_envelope.py <render-dir> --fixture env_amt096
    python pcf_envelope.py <render-dir> --sweep amt
"""
import argparse
import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_response import load, psd, fundamental, noise_floor_db, svf_db

# The measured static laws, from docs/research/PCF_FINDINGS.md section 3.
FC0, FC_OCT = 9.01, 10.653          # fc = FC0 * 2^(FC_OCT * param)
Q_AT_ZERO = 0.693
GAIN_AT_ZERO_DB = 4.33


def param_of(fc):
    """Undo the cutoff law: hertz back to the 0..1 knob parameter."""
    return np.log2(np.maximum(fc, 1e-6) / FC0) / FC_OCT


def fc_of(param):
    return FC0 * 2.0 ** (FC_OCT * np.asarray(param, float))


def frames(x, n, hop):
    w = np.hanning(n)
    out = []
    for s in range(0, len(x) - n, hop):
        out.append(np.fft.rfft(x[s:s + n] * w))
    return np.array(out)


def track_cutoff(fix, ref, sr, f0, window=2048, hop=256, halfwidth=1,
                 fmin=40.0, fmax=16000.0, q=Q_AT_ZERO, gain=GAIN_AT_ZERO_DB):
    """fc for every frame, by fitting the harmonic ratio one frame at a time."""
    A = frames(fix, window, hop)
    B = frames(ref, window, hop)
    nf = min(len(A), len(B))
    freqs = np.fft.rfftfreq(window, 1.0 / sr)
    df = freqs[1] - freqs[0]

    idx, fh = [], []
    for k in range(1, int(fmax / f0) + 1):
        c = int(round(k * f0 / df))
        if c - halfwidth < 0 or c + halfwidth >= len(freqs):
            break
        if fmin <= k * f0 <= fmax:
            idx.append(c)
            fh.append(k * f0)
    idx = np.array(idx)
    fh = np.array(fh)

    grid = np.exp(np.linspace(np.log(8.0), np.log(24000.0), 400))
    models = np.array([svf_db(fh, fc, q, "lp", gain) for fc in grid])

    pa = np.abs(A[:nf, :]) ** 2
    pb = np.abs(B[:nf, :]) ** 2
    out = np.full(nf, np.nan)
    for t in range(nf):
        a = np.array([pa[t, i - halfwidth:i + halfwidth + 1].sum() for i in idx])
        b = np.array([pb[t, i - halfwidth:i + halfwidth + 1].sum() for i in idx])
        keep = (a > 0) & (b > 0)
        if keep.sum() < 6:
            continue
        h = 10 * np.log10(a[keep] / b[keep])
        err = ((models[:, keep] - h) ** 2).mean(axis=1)
        out[t] = grid[int(np.argmin(err))]
    return out, hop / sr


def fit_decay(t, p, floor=None):
    """Fit p(t) = floor + depth * exp(-t / tau) over one decay segment."""
    if floor is None:
        floor = float(np.nanmin(p))
    y = p - floor
    good = np.isfinite(y) & (y > 1e-4)
    if good.sum() < 6:
        return None
    A = np.vstack([t[good], np.ones(good.sum())]).T
    sol, *_ = np.linalg.lstsq(A, np.log(y[good]), rcond=None)
    tau = -1.0 / sol[0] if sol[0] < 0 else np.inf
    return tau, float(np.exp(sol[1])), floor


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("--ref", default="pcf_ref.wav")
    ap.add_argument("--fixture")
    ap.add_argument("--sweep", choices=["amt", "dec"])
    ap.add_argument("--window", type=int, default=2048)
    ap.add_argument("--hop", type=int, default=256)
    a = ap.parse_args(argv)

    rx, sr = load(os.path.join(a.render_dir, a.ref))
    pref, freqs = psd(rx[:, 0], sr)
    f0 = fundamental(pref, freqs)

    names = ([a.fixture] if a.fixture else
             sorted(os.path.splitext(os.path.basename(p))[0]
                    for p in glob.glob(os.path.join(a.render_dir,
                                                    f"env_{a.sweep}*.wav"))))
    print(f"drone f0 {f0:.2f} Hz, window {a.window} "
          f"({1000.0 * a.window / sr:.0f} ms), hop {1000.0 * a.hop / sr:.1f} ms")
    for n in names:
        x, _ = load(os.path.join(a.render_dir, n + ".wav"))
        fc, dt = track_cutoff(x[:, 0], rx[:, 0], sr, f0,
                              window=a.window, hop=a.hop)
        p = param_of(fc)
        ok = np.isfinite(p)
        print(f"\n{n}: param min {np.nanmin(p):.3f} max {np.nanmax(p):.3f} "
              f"median {np.nanmedian(p):.3f}  ({ok.sum()} frames)")
        t = np.arange(len(p)) * dt
        step = max(1, len(p) // 40)
        for i in range(0, len(p), step):
            if np.isfinite(p[i]):
                print(f"   {t[i]:6.3f}s  fc {fc[i]:8.1f} Hz  param {p[i]:.3f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
