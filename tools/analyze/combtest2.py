#!/usr/bin/env python3
"""Comb-vs-peak on the driven high-resonance combtest fixtures, RB vs ours.

Extends combtest.py with a COMB DENSITY count (how many distinct inter-harmonic
peaks the spectrum carries) and runs on the cb_* fixtures. A coupled-diode comb
shows as MANY inter-harmonic peaks of similar height (dense, flat); a buffered
ladder shows FEW (the harmonic series plus one resonant peak at the corner).

USAGE  python combtest2.py <rebirth-dir> <ours-dir>
"""
import os, sys, glob, wave
import numpy as np

F0 = 110.0
FIX = ["cb_c064_r127_em127_d127", "cb_c090_r127_em127_d127",
       "cb_c110_r127_em127_d127", "cb_c090_r127_em127_d096",
       "cb_c127_r127_em000_d064"]


def psd(path, t0=0.3, t1=2.0):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)[int(t0 * sr):int(t1 * sr)]
    F = 16384
    acc = np.zeros(F // 2 + 1); win = np.hanning(F); frm = 0
    for i in range(0, len(x) - F, F // 2):
        for c in range(x.shape[1]):
            acc += np.abs(np.fft.rfft(x[i:i + F, c] * win)) ** 2
        frm += 1
    return (acc / frm, np.fft.rfftfreq(F, 1.0 / sr)) if frm else (None, None)


def metrics(P, f):
    lo, hi = 1000.0, 8000.0
    band = (f >= lo) & (f <= hi)
    Pb, fb = P[band], f[band]
    flat = float(np.exp(np.mean(np.log(Pb + 1e-30))) / (np.mean(Pb) + 1e-30))
    # inter-harmonic fill
    peaks, fills = [], []
    k = int(lo / F0)
    while (k + 1) * F0 <= hi:
        ih = np.argmin(np.abs(f - k * F0)); im = np.argmin(np.abs(f - (k + 0.5) * F0))
        peaks.append(P[max(0, ih - 2):ih + 3].max()); fills.append(np.median(P[im - 2:im + 3]))
        k += 1
    fill_db = float(10 * np.log10(np.median(fills) / np.median(peaks) + 1e-30))
    # comb density: local maxima in-band, >6 dB above the local median floor, NOT
    # within 15 Hz of a harmonic. Counts the inter-harmonic peaks only.
    logP = 10 * np.log10(Pb + 1e-30)
    floor = np.median(logP)
    n = 0
    for i in range(2, len(Pb) - 2):
        if logP[i] > logP[i-1] and logP[i] >= logP[i+1] and logP[i] > floor + 6:
            if min(abs(fb[i] - round(fb[i] / F0) * F0), abs(fb[i] - fb[i])) > 15.0 \
               and abs(fb[i] - round(fb[i] / F0) * F0) > 15.0:
                n += 1
    # strongest inharmonic peak vs strongest harmonic
    harm = np.zeros(len(f), bool)
    for k in range(1, int(hi / F0) + 1):
        harm |= np.abs(f - k * F0) < 15.0
    inh = P[band & ~harm]; hpk = P[band & harm].max() if (band & harm).any() else 1e-30
    inharm_db = float(10 * np.log10((inh.max() if len(inh) else 1e-30) / (hpk + 1e-30) + 1e-30))
    return flat, fill_db, n, inharm_db


def main(rbdir, ourdir):
    print("driven high-resonance comb test, band 1-8 kHz, sustain 0.3-2.0 s\n")
    print(f"{'fixture':<26}{'flatness':>18}{'fill dB':>16}{'inter-h peaks':>20}{'inharm dB':>16}")
    print(f"{'':<26}{'RB / ours':>18}{'RB / ours':>16}{'RB / ours':>20}{'RB / ours':>16}")
    print("-" * 96)
    for name in FIX:
        pr = glob.glob(os.path.join(rbdir, name + ".[wW][aA][vV]"))
        po = glob.glob(os.path.join(ourdir, name + ".wav"))
        if not (pr and po):
            print(f"{name:<26} missing"); continue
        Pr, fr = psd(pr[0]); Po, fo = psd(po[0])
        mr = metrics(Pr, fr); mo = metrics(Po, fo)
        print(f"{name:<26}{mr[0]:>8.3f}/{mo[0]:<8.3f}{mr[1]:>7.1f}/{mo[1]:<7.1f}"
              f"{mr[2]:>9d}/{mo[2]:<9d}{mr[3]:>7.1f}/{mo[3]:<7.1f}")
    print("\ncomb = high flatness, high (near-0) fill, MANY inter-harmonic peaks on RB")
    print("that ours lacks. peak = low flatness, deep fill, few peaks, both alike.")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
