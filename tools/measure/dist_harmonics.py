#!/usr/bin/env python3
"""Distortion harmonic analyzer (clean-room, supertask 7, 2026-09-17).

For a NEAR-SINE input (dist-curve batch) the distortion's output is (mostly)
harmonics of one fundamental, and their amplitudes ARE the static transfer
curve's harmonic decomposition: how fast they decay tells soft vs hard clip, and
the odd/even balance tells symmetric vs asymmetric. ONE method per file: Welch
PSD over a steady 2-6 s window, fundamental found as the strongest peak below
300 Hz, then the amplitude at each harmonic k*f0 (summed over the +/-2 bin around
each, so slight detune is captured), reported in dB relative to the fundamental.

Usage: python dist_harmonics.py <renders_dir> [--ref cs_off] [--nharm 24]
"""
import argparse, glob, os, sys, wave
import numpy as np

def load(path):
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    return (x.reshape(-1, ch).mean(1) if ch > 1 else x), sr

def welch(x, sr, nper=16384):
    hop = nper // 2; win = np.hanning(nper)
    acc = np.zeros(nper // 2 + 1); k = 0
    for s in range(0, len(x) - nper, hop):
        acc += np.abs(np.fft.rfft(x[s:s+nper] * win)) ** 2; k += 1
    return np.fft.rfftfreq(nper, 1.0/sr), acc / max(1, k)

def harmonics(path, nharm, lo=2.0, hi=6.0):
    x, sr = load(path); seg = x[int(lo*sr):int(hi*sr)]
    f, psd = welch(seg, sr)
    df = f[1] - f[0]
    band = f < 300
    f0 = f[band][np.argmax(psd[band])]
    def amp(freq):
        k0 = int(round(freq / df))
        return np.sqrt(psd[max(1, k0-2):k0+3].sum()) if k0 < len(psd)-3 else 0.0
    h = np.array([amp((k+1) * f0) for k in range(nharm)])
    return f0, h

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders"); ap.add_argument("--ref", default="cs_off")
    ap.add_argument("--nharm", type=int, default=24)
    a = ap.parse_args(argv)
    files = sorted(glob.glob(os.path.join(a.renders, "*.wav")))
    if not files: print("no wavs"); return 1
    print(f"harmonic amplitudes in dB re the fundamental (h1); f0 shown; nharm={a.nharm}")
    print("fixture           f0    h2   h3   h4   h5   h6   h7   h8  h10  h12  h16  h20   ODDdB EVENdB")
    idx = [2,3,4,5,6,7,8,10,12,16,20]
    for fp in files:
        stem = os.path.splitext(os.path.basename(fp))[0]
        f0, h = harmonics(fp, a.nharm)
        h1 = h[0] + 1e-12
        db = 20*np.log10(h/h1 + 1e-9)
        odd  = 20*np.log10(np.sqrt((h[2::2]**2).sum())/h1 + 1e-9)   # h3,h5,h7...
        even = 20*np.log10(np.sqrt((h[1::2]**2).sum())/h1 + 1e-9)   # h2,h4,h6...
        cols = "  ".join(f"{db[i-1]:5.1f}" for i in idx)
        print(f"{stem:14s} {f0:6.1f}  {cols}   {odd:5.1f} {even:5.1f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
