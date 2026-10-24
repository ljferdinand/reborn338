#!/usr/bin/env python3
"""Comb vs peak: is ReBirth's high-resonance sound a dense intermodulation comb
that a buffered ladder cannot make, or a single resonant peak we can approach?

This settles the diode-fork question (docs/research/reborn338_diode_ladder_design.md). The
design doc claims ReBirth's coupled diode ladder intermodulates the note's
harmonics into a dense comb at high resonance (many peaks, high flatness) while
our BUFFERED TPT cascade can only make a clean resonant peak on the harmonic grid.
If true, no constant on the current core matches it and the fork is necessary; if
ReBirth is just a taller peak on the same harmonic grid, it is a K_B refit.

METHOD, on a held saw at f0 (batchH is note A, env mod 0, so the source is a 1/k
harmonic series and any energy BETWEEN the harmonics is the filter's own doing):

  inter-harmonic fill   median PSD in the band halfway between consecutive
                        harmonics, relative to the harmonic peaks, in dB. A clean
                        resonant-filtered saw has deep nulls between harmonics
                        (very negative); a dense comb fills them in (toward 0).
  spectral flatness     geomean/mean of the PSD over 1-8 kHz, the resonance
                        region. A comb of many similar peaks is flatter than one
                        dominant peak.
  inharmonic peak       the largest spectral peak NOT within 15 Hz of a harmonic,
                        relative to the largest harmonic. A self-oscillation tone
                        not locked to the note shows here.

Run at high resonance (127) across cutoffs, ReBirth vs ours. Stereo pooled.

USAGE  python combtest.py <rebirth-dir> <ours-dir>   (batchH-style dirs)
"""
import os
import sys
import glob
import re
import wave

import numpy as np

F0 = 110.0
CUTS = (64, 90, 110, 127)
RESO = 127


def psd(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    x = x[int(0.3 * sr):]                      # skip attack
    F = 16384
    acc = np.zeros(F // 2 + 1)
    win = np.hanning(F)
    frm = 0
    for i in range(0, len(x) - F, F // 2):
        for c in range(x.shape[1]):
            acc += np.abs(np.fft.rfft(x[i:i + F, c] * win)) ** 2
        frm += 1
    if frm == 0:
        return None, None
    return acc / frm, np.fft.rfftfreq(F, 1.0 / sr)


def metrics(P, f):
    lo, hi = 1000.0, 8000.0
    band = (f >= lo) & (f <= hi)
    Pb = P[band]
    # spectral flatness in the resonance band
    flat = float(np.exp(np.mean(np.log(Pb + 1e-30))) / (np.mean(Pb) + 1e-30))
    # harmonic peaks vs inter-harmonic fill over the band
    peaks, fills = [], []
    k = int(lo / F0)
    while (k + 1) * F0 <= hi:
        fh = k * F0
        fm = (k + 0.5) * F0
        ih = np.argmin(np.abs(f - fh)); im = np.argmin(np.abs(f - fm))
        peaks.append(P[max(0, ih - 2):ih + 3].max())
        fills.append(np.median(P[im - 2:im + 3]))
        k += 1
    peaks, fills = np.array(peaks), np.array(fills)
    fill_db = float(10 * np.log10(np.median(fills) / np.median(peaks) + 1e-30))
    # largest inharmonic peak, relative to largest harmonic
    harm_mask = np.zeros(len(f), bool)
    for k in range(1, int(hi / F0) + 1):
        harm_mask |= np.abs(f - k * F0) < 15.0
    inh = P[band & ~harm_mask]
    hpk = P[band & harm_mask].max() if (band & harm_mask).any() else 1e-30
    inharm_db = float(10 * np.log10((inh.max() if len(inh) else 1e-30) / (hpk + 1e-30) + 1e-30))
    return flat, fill_db, inharm_db


def main(rbdir, ourdir):
    print("high-resonance comb test, reso %d, band 1-8 kHz\n" % RESO)
    print(f"{'cutoff':>6} | {'flatness (comb=high)':>22} | {'inter-harm fill dB (comb=high)':>30} | {'inharmonic pk dB':>17}")
    print(f"{'':>6} | {'RB':>10}{'ours':>12} | {'RB':>14}{'ours':>16} | {'RB':>8}{'ours':>9}")
    print("-" * 92)
    for c in CUTS:
        pr = glob.glob(os.path.join(rbdir, f"*cut{c:03d}_reso{RESO:03d}.[wW][aA][vV]"))
        po = glob.glob(os.path.join(ourdir, f"*cut{c:03d}_reso{RESO:03d}.wav"))
        if not (pr and po):
            print(f"{c:>6} | missing"); continue
        Pr, fr = psd(pr[0]); Po, fo = psd(po[0])
        if Pr is None or Po is None:
            continue
        fl_r, fi_r, ih_r = metrics(Pr, fr)
        fl_o, fi_o, ih_o = metrics(Po, fo)
        print(f"{c:>6} | {fl_r:>10.3f}{fl_o:>12.3f} | {fi_r:>14.1f}{fi_o:>16.1f} | {ih_r:>8.1f}{ih_o:>9.1f}")
    print("\nRead: if ReBirth's flatness and inter-harmonic fill are much higher than")
    print("ours, its high-res sound is a dense comb our buffered ladder cannot make")
    print("(diode fork needed). If they are close, it is a peak we can tune (K_B).")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
