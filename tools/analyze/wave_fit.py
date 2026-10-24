#!/usr/bin/env python3
"""Fit ReBirth's square oscillator: pulse width and level, filter cancelled out.

The trick that makes this clean. For a sawtooth the k-th harmonic is 1/k of the
fundamental; for a pulse of duty d it is |sin(k.pi.d)| / (k.sin(pi.d)). Both
fixtures pass through the SAME filter at the same knob settings, so dividing the
square's harmonic series by the saw's cancels the filter exactly:

    R(k) = square(k)/square(1)  /  ( saw(k)/saw(1) )  =  |sin(k.pi.d)| / sin(pi.d)

which depends only on d. At d = 0.5 that is 1 for odd k and 0 for even k, so any
even-harmonic content at all is a pulse-width offset, and the fit needs no model
of the filter, the envelope or the level.

The level is then the residual: the measured square/saw fundamental ratio against
the 2.sin(pi.d) a same-amplitude pulse would give.

Usage:  python wave_fit.py <dir> [dir ...]
"""
import os
import sys
import wave as wavemod

import numpy as np

# Mono-summing is SAFE here and was checked, not assumed. The workspace rule is
# to measure stereo, because a pan law seven times too wide once hid behind
# mono-summed metrics. These fixtures are a single centre-panned 303 with the
# effect rack off: L minus R is exactly 0 LSB on both sides, ReBirth's and ours.
# Re-check that if this is ever pointed at a fixture with two voices or any pan.

TEMPO = 120.0
F0 = 110.0
CUTS = (32, 64, 96)
KMAX = 13          # above this the saw harmonics fall into the noise at cut32


def load(path):
    with wavemod.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def spectrum(path, t0_ms=20.0, dur_ms=60.0, n_steps=8, first=1):
    """Harmonic amplitudes 1..KMAX plus the sub-20 Hz (DC) level, unnormalised.

    Unnormalised on purpose: the saw and square fixtures share an envelope, so
    their ratio is meaningful and rms-normalising each note would throw away
    exactly the level information being fitted.
    """
    x, sr = load(path)
    step = int(round(sr * 60.0 / TEMPO / 4.0))
    a, d = int(t0_ms * 0.001 * sr), int(dur_ms * 0.001 * sr)
    N = 1 << int(np.ceil(np.log2(d)) + 1)
    win = np.hanning(d)
    acc = np.zeros(N // 2 + 1)
    cnt = 0
    for s in range(first, first + n_steps):
        seg = x[s * step + a: s * step + a + d]
        if len(seg) < d:
            break
        y = np.zeros(N)
        y[:d] = seg * win
        acc += np.abs(np.fft.rfft(y))
        cnt += 1
    acc /= max(1, cnt)
    f = np.fft.rfftfreq(N, 1.0 / sr)
    h = []
    for k in range(1, KMAX + 1):
        i = np.argmin(np.abs(f - k * F0))
        h.append(acc[max(0, i - 1):i + 2].max())
    dc = acc[(f >= 0) & (f < 25.0)].max()
    return np.array(h), dc


def model_R(k, d):
    return np.abs(np.sin(k * np.pi * d)) / np.sin(np.pi * d)


def fit_dir(d_):
    rows = []
    for cut in CUTS:
        ps = os.path.join(d_, f"g303_wave0_cut{cut:03d}.wav")
        pq = os.path.join(d_, f"g303_wave1_cut{cut:03d}.wav")
        if not (os.path.exists(ps) and os.path.exists(pq)):
            continue
        hs, dcs = spectrum(ps)
        hq, dcq = spectrum(pq)
        rows.append((cut, hs, dcs, hq, dcq))
    if not rows:
        return None

    # duty: least squares in dB on R(k), pooled over cutoff, harmonics 2..KMAX
    ks = np.arange(2, KMAX + 1)
    meas = []
    for cut, hs, _ds, hq, _dq in rows:
        r = (hq[ks - 1] / hq[0]) / (hs[ks - 1] / hs[0])
        meas.append(r)
    meas = np.array(meas)
    best, berr = 0.5, 1e9
    for duty in np.arange(0.5, 0.60001, 0.00002):
        pred = model_R(ks, duty)
        e = np.mean((20 * np.log10(np.maximum(meas, 1e-9))
                     - 20 * np.log10(np.maximum(pred, 1e-9))) ** 2)
        if e < berr:
            berr, best = e, duty
    return rows, best, np.sqrt(berr), ks, meas


def report(name, d_):
    r = fit_dir(d_)
    print(f"=== {name}  ({d_}) ===")
    if r is None:
        print("  no wave fixtures found\n")
        return
    rows, duty, rms, ks, meas = r
    print(f"  best-fit pulse duty  {duty:.5f}   (offset from 50%: "
          f"{100*(duty-0.5):+.3f} %)   residual {rms:.2f} dB rms over "
          f"{meas.size} points")
    print()
    print("  R(k) = square/saw harmonic ratio, filter cancelled, dB")
    print("    k   " + "".join(f"{k:>7d}" for k in ks))
    for i, (cut, *_r) in enumerate(rows):
        print(f"  cut{cut:<3d}" + "".join(f"{20*np.log10(max(v,1e-9)):7.1f}" for v in meas[i]))
    print("  model " + "".join(f"{20*np.log10(max(v,1e-9)):7.1f}" for v in model_R(ks, duty)))
    print("  d=.50 " + "".join(f"{20*np.log10(max(v,1e-9)):7.1f}" for v in model_R(ks, 0.5)))
    print()
    print("  level: square fundamental vs saw fundamental")
    print(f"  {'cut':>5s} {'measured dB':>12s} {'same-amp pulse':>15s} "
          f"{'=> osc gain':>12s} {'sq DC/H1 dB':>12s} {'saw DC/H1 dB':>13s}")
    ideal = 20 * np.log10(2.0 * np.sin(np.pi * duty))
    gains = []
    for cut, hs, dcs, hq, dcq in rows:
        m = 20 * np.log10(hq[0] / hs[0])
        g = 10 ** ((m - ideal) / 20.0)
        gains.append(g)
        print(f"  {cut:5d} {m:12.2f} {ideal:15.2f} {g:12.4f} "
              f"{20*np.log10(dcq/hq[0]):12.1f} {20*np.log10(dcs/hs[0]):13.1f}")
    print(f"  mean oscillator gain, square vs saw: {np.mean(gains):.4f}  "
          f"({20*np.log10(np.mean(gains)):+.2f} dB)")
    print()


if __name__ == "__main__":
    for d_ in sys.argv[1:]:
        report(os.path.basename(d_.rstrip("/\\")), d_)
