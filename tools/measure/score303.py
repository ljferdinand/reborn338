#!/usr/bin/env python3
"""
score303.py - reconstructed 303 A/B scoring harness.

Implements the section-5 measurement recipe from docs/research/RUNBOOK.md:
  Welch PSD (nperseg=8192, noverlap=4096, Hann), both sides measured with one
  identical method.
    - Cutoff:     85% spectral rolloff, magnitude-weighted spectral centroid.
    - Resonance:  peak prominence in 800-6000 Hz relative to the 110 Hz
                  fundamental, plus a power-weighted centroid.

Scores the Reborn renders in renders/ against the section-6 ReBirth targets
(hard-coded below). Prints per-point measured / target / delta and per-metric
mean absolute error.

Note A fundamental = 110 Hz. Analysis band 20 Hz .. Nyquist. Stereo is
mono-averaged.
"""
import wave, glob, os
import numpy as np
from scipy.signal import welch

FS = 44100
FUND = 110.0                 # note A fundamental (Hz)
FUND_BAND = (100.0, 122.0)   # window to locate the fundamental peak
PEAK_BAND = (800.0, 6000.0)  # resonance search band
LO = 20.0                    # drop DC / sub-bass for centroid & rolloff

def load_mono(path):
    w = wave.open(path, "rb")
    sw, ch, n = w.getsampwidth(), w.getnchannels(), w.getnframes()
    raw = w.readframes(n)
    w.close()
    if sw == 2:
        d = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        v = (a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8)
             | (a[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        d = v.astype(np.float64) / (1 << 23)
    elif sw == 4:
        d = np.frombuffer(raw, dtype="<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unhandled sample width {sw} bytes")
    if ch > 1:
        d = d.reshape(-1, ch).mean(axis=1)
    return d

def psd(x):
    f, P = welch(x, fs=FS, nperseg=8192, noverlap=4096, window="hann")
    return f, P

def rolloff85(f, P):
    m = f >= LO
    f, P = f[m], P[m]
    c = np.cumsum(np.sqrt(P))          # magnitude cumulative (matches section-6 rolloff convention)
    thr = 0.85 * c[-1]
    i = np.searchsorted(c, thr)
    return float(f[min(i, len(f) - 1)])

def centroid(f, P, power=False):
    m = f >= LO
    f, P = f[m], P[m]
    w = P if power else np.sqrt(P)
    return float(np.sum(f * w) / np.sum(w))

def peak_prominence_db(f, P):
    fb = (f >= FUND_BAND[0]) & (f <= FUND_BAND[1])
    pb = (f >= PEAK_BAND[0]) & (f <= PEAK_BAND[1])
    fund = P[fb].max()
    peak = P[pb].max()
    return 10.0 * np.log10(peak / fund)

def m_cutoff(path):
    f, P = psd(load_mono(path))
    return rolloff85(f, P), centroid(f, P, power=False)

def m_reso(path):
    f, P = psd(load_mono(path))
    return peak_prominence_db(f, P), centroid(f, P, power=True)

R = "renders"

# ---- section-6 ReBirth targets -------------------------------------------
# cutoff sweep (reso120, envmod0): rolloff, centroid at cut 30/60/90/120
CUT = {
    30:  (770, 528), 60: (1238, 804), 90: (2073, 1252), 120: (3402, 2004),
}
# env-swept resonance (cut90, envmod70): peak-prominence dB, centroid at reso 20/60/100/127
ENV = {
    20: (-18.7, 243), 60: (-15.9, 413), 100: (-15.3, 959), 127: (-14.1, 1824),
}
# static resonance (envmod0): peak dB, centroid
STAT90 = {20: (-22.3, 191), 60: (-15.4, 313), 100: (-8.4, 635), 127: (-2.8, 1060)}
STAT120 = {20: (-19.8, 223), 60: (-15.9, 377), 100: (-11.4, 838), 127: (-6.2, 1519)}

def pct(meas, tgt):
    return 100.0 * (meas - tgt) / tgt

def main():
    ro_err, cen_err = [], []       # cutoff: rolloff %err, centroid %err
    pk_err, rcen_err = [], []      # resonance: peak dB abs err, centroid %err

    print("\n=== CUTOFF SWEEP (reso120, envmod0, note A) ===")
    print(f"{'cut':>4} | {'rolloff meas/tgt (Hz)':>26} {'d%':>7} | {'centroid meas/tgt (Hz)':>26} {'d%':>7}")
    for c in (30, 60, 90, 120):
        p = f"{R}/scratch_303_cut{c:03d}_reso120.wav"
        ro, cen = m_cutoff(p)
        tro, tcen = CUT[c]
        ro_err.append(abs(pct(ro, tro))); cen_err.append(abs(pct(cen, tcen)))
        print(f"{c:>4} | {ro:8.0f} / {tro:<8.0f}{'':>7} {pct(ro,tro):+6.1f} | "
              f"{cen:8.0f} / {tcen:<8.0f}{'':>7} {pct(cen,tcen):+6.1f}")

    def reso_block(title, tgt, fname):
        print(f"\n=== {title} ===")
        print(f"{'reso':>4} | {'peak meas/tgt (dB)':>24} {'dDB':>7} | {'centroid meas/tgt (Hz)':>26} {'d%':>7}")
        for r in (20, 60, 100, 127):
            p = f"{R}/{fname(r)}"
            pk, cen = m_reso(p)
            tpk, tcen = tgt[r]
            pk_err.append(abs(pk - tpk)); rcen_err.append(abs(pct(cen, tcen)))
            print(f"{r:>4} | {pk:9.1f} / {tpk:<9.1f}{'':>4} {pk-tpk:+6.1f} | "
                  f"{cen:8.0f} / {tcen:<8.0f}{'':>7} {pct(cen,tcen):+6.1f}")

    reso_block("ENV-SWEPT RESONANCE (cut90, envmod70, note A)", ENV,
               lambda r: f"scratch_303_reso{r:03d}_cut090.wav")
    reso_block("STATIC RESONANCE cut90 (envmod0, note A)", STAT90,
               lambda r: f"scratch_303_STATIC_reso{r:03d}_cut090.wav")
    reso_block("STATIC RESONANCE cut120 (envmod0, note A)", STAT120,
               lambda r: f"scratch_303_STATIC_reso{r:03d}_cut120.wav")

    print("\n=== MEAN ABSOLUTE ERROR vs section-6 ReBirth targets ===")
    print(f"  cutoff rolloff:   {np.mean(ro_err):5.1f} %")
    print(f"  cutoff centroid:  {np.mean(cen_err):5.1f} %")
    print(f"  reso peak:        {np.mean(pk_err):5.2f} dB")
    print(f"  reso centroid:    {np.mean(rcen_err):5.1f} %")

if __name__ == "__main__":
    main()
