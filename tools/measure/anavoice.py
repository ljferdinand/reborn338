#!/usr/bin/env python3
"""Read a single ReBirth 808 export and report its real structure: the partials
(peaks with relative dB), the decay time constant, the band-energy split, and the
noise fraction. Power-averaged over the step-locked hits, never coherently, so a
non-aligned HF voice is not cancelled (the trap that dominates this project).

  anavoice.py <export.wav> [--wide] [--win 0.45] [--band lo,hi] [--peaks N]

Reads the export directly; does not render our side. For finding a target before
touching the model.
"""
import sys, os, struct, argparse, math
import numpy as np
from scipy.signal import welch

SR = 44100
STEP = 60.0 / 120.0 / 4.0
HITS = [0, 4, 8, 12]
BARS = 4


def load(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i + 4:i + 8])[0]
    a = np.frombuffer(d[i + 8:i + 8 + n], dtype="<i2").astype(np.float64) / 32768.0
    return a[0::2]                                   # left


def windows(x, win, wide):
    w = int(win * SR)
    pos = ([bar * 16 * STEP for bar in range(BARS)] if wide
           else [(bar * 16 + st) * STEP for bar in range(BARS) for st in HITS])
    out = []
    for t0 in pos:
        t = int(t0 * SR)
        if t + w <= len(x):
            out.append(x[t:t + w])
    return out or [x[:w]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--wide", action="store_true")
    ap.add_argument("--win", type=float, default=0.45)
    ap.add_argument("--band", default="20,20000")
    ap.add_argument("--peaks", type=int, default=6)
    a = ap.parse_args()

    x = load(a.wav)
    wins = windows(x, a.win, a.wide)

    # Power-averaged PSD.
    P = None
    for h in wins:
        f, p = welch(h, SR, nperseg=8192, noverlap=4096)
        P = p if P is None else P + p
    P /= len(wins)
    Pdb = 10 * np.log10(P + 1e-20)
    Pdb -= Pdb.max()

    lo, hi = (float(v) for v in a.band.split(","))
    sel = (f >= lo) & (f <= hi)

    # Peak picking: local maxima at least 3 dB above neighbours, top N by level.
    peaks = []
    for k in range(1, len(f) - 1):
        if sel[k] and P[k] > P[k - 1] and P[k] >= P[k + 1] and Pdb[k] > -40:
            peaks.append((f[k], Pdb[k]))
    peaks.sort(key=lambda t: -t[1])
    print("peaks (Hz @ dB rel), top %d:" % a.peaks)
    for fr, db in peaks[:a.peaks]:
        print("  %8.1f  %+6.1f" % (fr, db))

    # Band split.
    edges = [0, 100, 300, 1000, 3000, 8000, 16000, SR / 2]
    tot = np.sum(P[sel]) + 1e-20
    print("band energy (fraction of total in %g..%g):" % (lo, hi))
    for j in range(len(edges) - 1):
        b = (f >= edges[j]) & (f < edges[j + 1])
        print("  %6.0f..%-6.0f  %5.1f%%" % (edges[j], edges[j + 1], 100 * np.sum(P[b]) / tot))

    # Decay: RMS envelope of the first window, time to -30 dB.
    h = wins[0]
    env = np.sqrt(np.convolve(h ** 2, np.ones(220) / 220, "same")) + 1e-12
    edb = 20 * np.log10(env / env.max())
    peak_i = int(np.argmax(env))
    below = np.where(edb[peak_i:] < -30)[0]
    t30 = (below[0] / SR * 1000) if len(below) else float("nan")
    below20 = np.where(edb[peak_i:] < -20)[0]
    t20 = (below20[0] / SR * 1000) if len(below20) else float("nan")
    print("decay from peak: -20 dB at %.0f ms, -30 dB at %.0f ms" % (t20, t30))
    # Single-exponential tau from the -20 point (env ~ e^{-t/tau}, -20 dB = 2.30 tau... use ratio).
    if not math.isnan(t20):
        tau = (t20 / 1000.0) / (20.0 / 8.686)        # 8.686 dB per tau
        print("            implied single-exp tau ~ %.3f s" % tau)


if __name__ == "__main__":
    main()
