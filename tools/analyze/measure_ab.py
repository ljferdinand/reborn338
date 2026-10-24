#!/usr/bin/env python3
"""A/B parity measurement: compare a Reborn render against an export from the
original program.

Usage:
    python measure_ab.py <original.wav> <ours.wav> [seconds]

Reports octave-band energy distribution deltas, kick profile (fundamental,
peak, decay) from the 35-120 Hz band, and the hat-band (6-18 kHz) share.
See docs/PARITY.md for the reference targets.
"""
import sys, wave
import numpy as np


def load(path, secs=None):
    w = wave.open(path, 'rb')
    sr = w.getframerate()
    n = w.getnframes() if secs is None else min(w.getnframes(), int(secs * sr))
    d = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64) / 32768.0
    w.close()
    # Channel count ONLY. This used to read
    #     if w.getnchannels() == 2 or d.size % 2 == 0:
    # and the second clause is a trap: for a genuinely MONO file with an even
    # sample count it reshapes to (-1, 2) and averages ADJACENT SAMPLES, which
    # is a 2x decimation that halves the effective sample rate while sr is left
    # unchanged, shifting every frequency in the analysis by an octave. Latent
    # so far only because reborn_render and ReBirth both always write stereo.
    # Found by the optimiser work, 2026-07-31.
    if w.getnchannels() == 2:
        d = d.reshape(-1, 2).mean(axis=1)
    return d, sr


def bands(x, sr):
    W = 8192
    win = np.hanning(W)
    acc = np.zeros(W // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - W, W * 4):
        acc += np.abs(np.fft.rfft(x[i:i + W] * win)) ** 2
        cnt += 1
    psd = acc / max(1, cnt)
    freqs = np.fft.rfftfreq(W, 1 / sr)
    edges = [20, 40, 80, 160, 315, 630, 1250, 2500, 5000, 10000, 20000]
    e = np.array([psd[(freqs >= lo) & (freqs < hi)].sum()
                  for lo, hi in zip(edges[:-1], edges[1:])])
    return e / e.sum(), edges


def bandpass(x, lo, hi, sr):
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / sr)
    X[(f < lo) | (f > hi)] = 0
    return np.fft.irfft(X, len(x))


def kick_profile(x, sr, label):
    kb = bandpass(x, 35, 120, sr)
    env = np.convolve(np.abs(kb), np.ones(132) / 132, mode='same')
    d = np.diff(env)
    d[d < 0] = 0
    pos = d[d > 0]
    if pos.size == 0:
        print(f"{label}: no kick content")
        return
    th = np.percentile(pos, 97)
    pk, i = [], 0
    while i < len(d):
        if d[i] > th:
            j = i + int(np.argmax(env[i:i + int(0.02 * sr)]))
            pk.append(j)
            i = j + int(0.15 * sr)
        else:
            i += 1
    L = int(0.25 * sr)
    acc = np.zeros(L)
    cnt = 0
    pit = []
    for p in pk[2:40]:
        if p + L < len(kb):
            acc += np.abs(kb[p:p + L])
            cnt += 1
            z = kb[p + int(0.03 * sr):p + int(0.09 * sr)]
            zc = np.where(np.diff(np.sign(z)))[0]
            if len(zc) > 3:
                pit.append(sr / (2 * np.mean(np.diff(zc))))
    acc /= max(1, cnt)
    peak = acc.max()
    t90 = (np.argmax(acc < peak * 0.1) / sr * 1000
           if (acc < peak * 0.1).any() else 250.0)
    print(f"{label}: kicks={cnt} peak={peak:.3f} 90%decay={t90:.0f}ms "
          f"fund={np.median(pit):.1f}Hz")


def hat_share(x, sr, label):
    hb = bandpass(x, 6000, 18000, sr)
    r = np.sqrt((hb ** 2).mean()) / np.sqrt((x ** 2).mean())
    print(f"{label}: hat-band (6-18k) share = {20 * np.log10(r):.1f} dB")


def main():
    orig_path, ours_path = sys.argv[1], sys.argv[2]
    secs = float(sys.argv[3]) if len(sys.argv) > 3 else 120.0
    o, sr = load(orig_path, secs)
    u, _ = load(ours_path, secs)
    bo, e = bands(o, sr)
    bu, _ = bands(u, sr)
    print(f"{'band':>12} {'orig%':>7} {'ours%':>7} {'delta(dB)':>9}")
    for i, (lo, hi) in enumerate(zip(e[:-1], e[1:])):
        d = 10 * np.log10((bu[i] + 1e-12) / (bo[i] + 1e-12))
        print(f"{lo:>5}-{hi:<6} {bo[i]*100:6.2f}% {bu[i]*100:6.2f}% {d:+8.1f}")
    print("\nkick (35-120 Hz band):")
    kick_profile(o, sr, "  ORIG")
    kick_profile(u, sr, "  OURS")
    print("\nhats:")
    hat_share(o, sr, "  ORIG")
    hat_share(u, sr, "  OURS")


if __name__ == "__main__":
    main()
