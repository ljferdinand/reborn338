#!/usr/bin/env python3
"""Band-by-band and envelope comparison of two renders. One method, both sides.

score808/score909 report centroid and rolloff, which are single numbers and can
both "match" while the sound is audibly wrong: a spectral hole in the middle, a
gentle high cut, or a tail that rings too long are all invisible to them. This
tool is what to reach for when an ear says something the summary metrics deny,
which is the measurement-protocol rule about widening the measurement rather
than refining the current metric (docs/MEASUREMENT_PROTOCOL.md).

Both files are POWER-averaged over their step-locked hits (never coherently, which
would cancel non-aligned HF content) and each spectrum is normalised to its own
total, so a level offset cannot masquerade as a spectral difference.

  banddiff.py OURS.wav REBIRTH.wav              # band table
  banddiff.py OURS.wav REBIRTH.wav --env        # add the decay envelope
  banddiff.py A.wav B.wav --wide --win 1.2      # one hit per bar (long tails)
"""
import sys, struct, argparse, math
import numpy as np
from scipy.signal import welch

SR = 44100
STEP = 60.0 / 120.0 / 4.0
HITS = [0, 4, 8, 12]
BARS = 4
EDGES = [20, 40, 60, 80, 100, 200, 300, 500, 800, 1200, 2000, 3500, 6000,
         9000, 13000, 18000]


def load(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i + 4:i + 8])[0]
    a = np.frombuffer(d[i + 8:i + 8 + n], dtype="<i2").astype(np.float64) / 32768.0
    return a[0::2]                                   # left channel


def windows(x, win, wide):
    w = int(win * SR)
    pos = ([b * 16 * STEP for b in range(BARS)] if wide
           else [(b * 16 + s) * STEP for b in range(BARS) for s in HITS])
    out = [x[int(t * SR):int(t * SR) + w] for t in pos if int(t * SR) + w <= len(x)]
    return out or [x[:w]]


def psd(x, win, wide):
    P = None
    for h in windows(x, win, wide):
        f, p = welch(h, SR, nperseg=8192, noverlap=4096)
        P = p if P is None else P + p
    return f, P / len(windows(x, win, wide))


def envelope(x, win, wide):
    """RMS envelope of the first hit, in dB below its own peak."""
    h = windows(x, win, wide)[0]
    env = np.sqrt(np.convolve(h ** 2, np.ones(441) / 441, "same")) + 1e-12
    pk = int(np.argmax(env))
    return env, pk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ours")
    ap.add_argument("theirs")
    ap.add_argument("--wide", action="store_true")
    ap.add_argument("--win", type=float, default=0.45)
    ap.add_argument("--env", action="store_true", help="also show the decay envelope")
    a = ap.parse_args()

    xo, xt = load(a.ours), load(a.theirs)
    f, Po = psd(xo, a.win, a.wide)
    _, Pt = psd(xt, a.win, a.wide)
    Po = Po / Po.sum()
    Pt = Pt / Pt.sum()

    print(f"{'band Hz':16} {'ours%':>7} {'RB%':>7} {'ours/RB dB':>11}")
    for j in range(len(EDGES) - 1):
        lo, hi = EDGES[j], EDGES[j + 1]
        b = (f >= lo) & (f < hi)
        o, t = 100 * Po[b].sum(), 100 * Pt[b].sum()
        print(f"{lo:6.0f}-{hi:<9.0f} {o:7.2f} {t:7.2f} {10*math.log10((o+1e-9)/(t+1e-9)):+11.1f}")

    if a.env:
        eo, po = envelope(xo, a.win, a.wide)
        et, pt = envelope(xt, a.win, a.wide)
        print(f"\n{'ms after onset':16} {'ours dB':>9} {'RB dB':>9}")
        for ms in (0, 25, 50, 100, 150, 200, 300, 400, 600, 800):
            io, it = po + int(ms / 1000 * SR), pt + int(ms / 1000 * SR)
            vo = 20 * math.log10(eo[io] / eo[po]) if io < len(eo) else float("-inf")
            vt = 20 * math.log10(et[it] / et[pt]) if it < len(et) else float("-inf")
            print(f"{ms:>10} ms    {vo:+9.1f} {vt:+9.1f}")


if __name__ == "__main__":
    main()
