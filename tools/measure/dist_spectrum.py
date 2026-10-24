#!/usr/bin/env python3
"""Distortion spectrum analyzer (clean-room, supertask 7, 2026-09-17).

Characterises what the distortion does to a steady 303 drone: the harmonic
content it adds, versus the clean (dist-off) reference. ONE method applied to
every file (docs/MEASUREMENT_PROTOCOL.md): Welch PSD (nperseg 8192, 50% overlap) over a steady 2-6 s
window, reported as octave-ish band energies RELATIVE to each file's own total
(so an overall level change cannot masquerade as a spectral change), plus the
magnitude-weighted spectral centroid and the overall level in dBFS. The clean
reference `d_off` is printed first; each distorted file is shown with its band
energies and the dB CHANGE per band against the clean reference (the added
harmonics).

Usage: python dist_spectrum.py <renders_dir> [--ref d_off]
"""
import argparse, glob, os, sys, wave
import numpy as np

BANDS = [(20, 200), (200, 500), (500, 1000), (1000, 2000),
         (2000, 4000), (4000, 8000), (8000, 16000)]

def load(path):
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(1) if ch > 1 else x
    return x, sr

def welch(x, sr, nper=8192):
    hop = nper // 2; win = np.hanning(nper)
    acc = np.zeros(nper // 2 + 1); k = 0
    for s in range(0, len(x) - nper, hop):
        acc += np.abs(np.fft.rfft(x[s:s+nper] * win)) ** 2; k += 1
    psd = acc / max(1, k)
    f = np.fft.rfftfreq(nper, 1.0 / sr)
    return f, psd

def analyze(path, lo=2.0, hi=6.0):
    x, sr = load(path)
    seg = x[int(lo*sr):int(hi*sr)]
    f, psd = welch(seg, sr)
    total = psd.sum() + 1e-30
    bands = []
    for a, b in BANDS:
        m = (f >= a) & (f < b)
        bands.append(psd[m].sum() / total)
    centroid = (f * psd).sum() / total
    leveldb = 10.0 * np.log10(np.mean(seg * seg) + 1e-20)
    return leveldb, centroid, np.array(bands)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders"); ap.add_argument("--ref", default="d_off")
    a = ap.parse_args(argv)
    files = sorted(glob.glob(os.path.join(a.renders, "*.wav")))
    if not files: print("no wavs"); return 1
    ref_path = os.path.join(a.renders, a.ref + ".wav")
    refL, refC, refB = analyze(ref_path) if os.path.exists(ref_path) else (None, None, None)
    hdr = "  ".join(f"{a_//1000 if a_>=1000 else a_}{'k' if a_>=1000 else ''}-{b//1000 if b>=1000 else b}{'k' if b>=1000 else ''}" for a_, b in BANDS)
    if refB is not None:
        print(f"REF {a.ref}: level {refL:.2f} dBFS  centroid {refC:.0f} Hz")
        print("  band fractions (%):", "  ".join(f"{100*v:5.2f}" for v in refB))
        print()
    print(f"{'fixture':16s} {'lvl':>7s} {'centr':>6s}   band-energy CHANGE vs ref (dB): {hdr}")
    for fp in files:
        stem = os.path.splitext(os.path.basename(fp))[0]
        if stem == a.ref: continue
        L, C, B = analyze(fp)
        if refB is not None:
            dchg = 10.0 * np.log10((B + 1e-12) / (refB + 1e-12))
            print(f"{stem:16s} {L:7.2f} {C:6.0f}   " + "  ".join(f"{d:+5.1f}" for d in dchg))
        else:
            print(f"{stem:16s} {L:7.2f} {C:6.0f}   " + "  ".join(f"{100*v:5.2f}" for v in B))
    return 0

if __name__ == "__main__":
    sys.exit(main())
