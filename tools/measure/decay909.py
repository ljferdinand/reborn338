#!/usr/bin/env python3
"""Amplitude decay of a step-locked drum hit, fitted on a proper envelope.

Why not a short power window: a tom at 91 Hz has an 11 ms period, so a 5 ms
window aliases against the waveform and produces a bouncing envelope that is not
real. This takes the magnitude of the analytic signal, which is the envelope by
construction, then averages ACROSS hits in the power domain (never coherently:
ReBirth's hits are not sample-aligned and summing waveforms cancels them).

The fit is a straight line through the dB envelope between two levels below the
peak, so it reports the decay of the part of the hit that carries the sound
rather than of its noise floor.
"""
import math, struct, sys
import numpy as np
from scipy.signal import hilbert

SR, BPM, BARS = 44100, 120.0, 4
STEP = 60.0 / BPM / 4.0
HITS = [0, 4, 8, 12]


def load_left(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i+4:i+8])[0]
    a = np.frombuffer(d[i+8:i+8+n], dtype="<i2").astype(np.float64) / 32768.0
    return a[0::2]


def envelope(x, win=0.45, smooth_ms=3.0):
    """Power-averaged envelope across the hits, as amplitude."""
    w = int(win * SR)
    k = max(1, int(smooth_ms / 1000.0 * SR))
    acc = None
    n = 0
    for bar in range(BARS):
        for st in HITS:
            t = int((bar * 16 + st) * STEP * SR)
            if t + w > len(x):
                continue
            e = np.abs(hilbert(x[t:t+w])) ** 2          # power
            acc = e if acc is None else acc + e
            n += 1
    if acc is None:
        return None
    acc /= n
    return np.sqrt(np.convolve(acc, np.ones(k) / k, mode="same"))


def decay_tau(x, lo_db=-6.0, hi_db=-26.0, win=0.45):
    """Amplitude tau in seconds, fitted between lo_db and hi_db below the peak.

    Returns (tau, t_lo, t_hi, r2). tau is the time to fall by 1 neper (8.686 dB).
    """
    env = envelope(x, win)
    if env is None:
        return None
    pk = env.max()
    if pk < 1e-7:
        return None
    db = 20 * np.log10(env / pk + 1e-30)
    ipk = int(np.argmax(env))
    seg = db[ipk:]
    below = lambda v: int(np.argmax(seg <= v)) if np.any(seg <= v) else -1
    i0, i1 = below(lo_db), below(hi_db)
    if i0 < 0 or i1 <= i0 + 20:
        return None
    t = np.arange(i0, i1) / SR
    y = seg[i0:i1]
    A = np.vstack([t, np.ones(len(t))]).T
    sl, ic = np.linalg.lstsq(A, y, rcond=None)[0]
    if sl >= 0:
        return None
    pred = sl * t + ic
    ss = 1.0 - float(np.sum((y - pred) ** 2)) / max(float(np.sum((y - y.mean()) ** 2)), 1e-12)
    return (-8.685889638 / sl, (ipk + i0) / SR, (ipk + i1) / SR, ss)


def crest_db(x, win=0.40):
    """Peak over RMS within a hit, median across hits. This is 'punch'."""
    w = int(win * SR)
    out = []
    for bar in range(BARS):
        for st in HITS:
            t = int((bar * 16 + st) * STEP * SR)
            if t + w > len(x):
                continue
            h = x[t:t+w]
            r = math.sqrt(float(np.mean(h ** 2)))
            p = float(np.max(np.abs(h)))
            if r > 1e-9:
                out.append(20 * math.log10(p / r))
    return float(np.median(out)) if out else 0.0


if __name__ == "__main__":
    for p in sys.argv[1:]:
        r = decay_tau(load_left(p))
        print("  %-60s %s" % (p.split("\\")[-1],
              "tau %.4f s  r2 %.3f  crest %.1f dB" % (r[0], r[3], crest_db(load_left(p)))
              if r else "no usable decay"))
