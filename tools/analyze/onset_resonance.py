#!/usr/bin/env python3
"""Onset-domain resonance measurement for the 303 filter character work.

WHY THIS EXISTS. The 303 resonance-CHARACTER gap (2026-08-08) does not live in
the steady spectrum. Three static-spectral "fixes" each matched ReBirth on their
own metric and were inaudible: the 2-pole tap matched the rolloff slope, 6x
oversampling matched the inter-harmonic aliasing mud, and both were nulls by ear.
The character is in the TRANSIENT, so measure it there.

WHAT IT REPORTS, per cell, for a ReBirth export and one of our renders:
  - peak-frequency TRACK over the note onset (the GLIDE). CAUTION, learned the
    hard way: ReBirth is NOT flat. It glides ~0.3 oct per onset too. Ours glides
    a hair more and starts a hair higher. The glide is SHARED, not the gap. An
    eyeballed spectrogram of a sharp bright line (ours) against a broad diffuse
    band (ReBirth) reads as "glide vs flat" and is wrong; plot the track.
  - instantaneous peak WIDTH per frame (Q proxy). Frame by frame ours is SHARPER
    (~120 Hz) than ReBirth (~300-500).
  - steady-state peak width from the time-averaged PSD. This INVERTS: ours comes
    out BROADER (~436 vs ~334 Hz) because our sharp peak glides and smears in the
    average. Both are true; they measure different things. Report both and do not
    claim one as THE difference.
  - inter-harmonic mud (energy between the harmonics), an aliasing proxy. ReBirth
    ~0.003; ours ~0.010 at 4x oversampling, ~0.003 at 6x. Matches on the meter,
    inaudible by ear.

USAGE
    python onset_resonance.py <rebirth.wav> <ours.wav> [f0]
The batchH cells are note A (f0=110). Env mod 0, so the corner is static and any
onset glide is the resonant feedback settling, not the filter envelope.
"""
import sys, wave
import numpy as np
from scipy.signal import spectrogram, welch


def read_mono(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def find_onset(x, sr, after=0.25):
    win = int(0.005 * sr)
    e = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, "same"))
    s = int(after * sr)
    idx = np.where(e[s:] > 0.5 * e[s:].max())[0]
    return (s + idx[0]) if len(idx) else s


def peak_track(x, sr, on, lo=2400, hi=4600, span=0.13):
    seg = x[on - int(0.01 * sr): on + int(span * sr)]
    f, t, S = spectrogram(seg, fs=sr, nperseg=384, noverlap=336, nfft=4096)
    b = (f >= lo) & (f <= hi)
    fb = f[b]
    pk, wid = [], []
    for i in range(S.shape[1]):
        col = S[b, i]
        pk.append(fb[int(np.argmax(col))])
        half = col.max() / 2.0
        idx = np.where(col > half)[0]
        wid.append((fb[idx[-1]] - fb[idx[0]]) if len(idx) > 1 else 0.0)
    return (t - 0.01) * 1000.0, np.array(pk), np.array(wid)


def steady_width(x, sr, lo=2000, hi=5000):
    xs = x[int(0.3 * sr):]
    f, P = welch(xs, fs=sr, nperseg=8192, noverlap=4096)
    b = (f >= lo) & (f <= hi)
    fb, Pb = f[b], P[b]
    i = int(np.argmax(Pb))
    idx = np.where(Pb > Pb[i] / 2.0)[0]
    return fb[i], (fb[idx[-1]] - fb[idx[0]]) if len(idx) > 1 else 0.0


def inter_harmonic_mud(x, sr, f0, lo=2400, hi=4600, nfft=32768, hw=8):
    xs = x[int(0.3 * sr):]
    win = np.hanning(nfft)
    acc = np.zeros(nfft // 2 + 1)
    c = 0
    for i in range(0, len(xs) - nfft, nfft // 2):
        acc += np.abs(np.fft.rfft(xs[i:i + nfft] * win)) ** 2
        c += 1
    P = acc / max(c, 1)
    f = np.fft.rfftfreq(nfft, 1.0 / sr)
    df = f[1] - f[0]
    at = lambda fr: P[max(0, int(round(fr / df)) - hw): int(round(fr / df)) + hw + 1].max()
    ks = np.arange(int(lo / f0), int(hi / f0) + 1)
    hpk = np.median([at(k * f0) for k in ks])
    tr = np.median([at((k + 0.5) * f0) for k in ks[:-1]])
    return float(tr / (hpk + 1e-30))


def summarize(label, p, f0):
    x, sr = read_mono(p)
    on = find_onset(x, sr)
    t, pk, wid = peak_track(x, sr, on)

    def at(ms):
        return pk[int(np.argmin(np.abs(t - ms)))]
    early, late = at(15), at(95)
    sf, sw = steady_width(x, sr)
    mud = inter_harmonic_mud(x, sr, f0)
    iw = float(np.median(wid[(t > 20) & (t < 100)]))
    print(f"{label:<10} glide {early:5.0f}->{late:5.0f} Hz ({np.log2(early/late):+.2f} oct)"
          f"   inst.width {iw:5.0f}   steady.width {sw:5.0f}   mud {mud:.4f}")


if __name__ == "__main__":
    rb, ours = sys.argv[1], sys.argv[2]
    f0 = float(sys.argv[3]) if len(sys.argv) > 3 else 110.0
    summarize("ReBirth", rb, f0)
    summarize("ours", ours, f0)
