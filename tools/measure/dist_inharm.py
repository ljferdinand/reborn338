#!/usr/bin/env python3
"""Distortion inharmonic-content analyzer (clean-room, supertask 7, 2026-09-17).

Measures the "vocoder texture" Luke's ear identified in ReBirth's distortion: the
fraction of energy that is INHARMONIC (aliased/foldback partials NOT at multiples
of the fundamental), which a wavefolder driven without oversampling produces and
which reads as a metallic, formant-like, vocoder-ish character. The band-energy
metric (dist_spectrum.py) is deaf to it: two renders with the same HF energy can
differ hugely in how much of it is harmonic vs aliased.

Method (fixed-pitch drone only): Welch PSD (nperseg 16384) over a steady 2-6 s
window. The fundamental f0 is taken from a CLEAN reference (heavy distortion makes
f0 detection unreliable, but the drone's pitch is fixed, so read it once from the
clean file). Inharmonic fraction = share of 500 Hz - 16 kHz energy that is NOT
within +/-12 Hz of any harmonic k*f0.

Usage: python dist_inharm.py <renders_dir> --ref <clean.wav name> [files...]
"""
import argparse, glob, os, sys, wave
import numpy as np

def load(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    return (np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64)/32768.0).reshape(-1, ch).mean(1), sr

def psd_of(p, lo=2.0, hi=6.0):
    x, sr = load(p); seg = x[int(lo*sr):int(hi*sr)]; nper = 16384; win = np.hanning(nper)
    acc = np.zeros(nper//2+1); k = 0
    for s in range(0, len(seg)-nper, nper//2):
        acc += np.abs(np.fft.rfft(seg[s:s+nper]*win))**2; k += 1
    return np.fft.rfftfreq(nper, 1.0/sr), acc/max(1, k)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders"); ap.add_argument("--ref", default="d_off")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args(argv)
    f, pc = psd_of(os.path.join(a.renders, a.ref + ".wav"))
    sub = (f > 40) & (f < 200)
    f0 = f[sub][np.argmax(pc[sub])]
    band = (f >= 500) & (f < 16000)
    harm = np.zeros_like(f, bool)
    for kk in range(1, int(16000/f0)+1):
        harm |= (np.abs(f - kk*f0) < 12)
    def inh(p):
        _, pp = psd_of(p); return 100*pp[band & ~harm].sum()/(pp[band].sum()+1e-30)
    files = a.files or sorted(glob.glob(os.path.join(a.renders, "*.wav")))
    print(f"f0 = {f0:.2f} Hz;  inharmonic %% of 500Hz-16k energy (the vocoder texture):")
    for fp in files:
        p = fp if os.path.isabs(fp) or os.path.exists(fp) else os.path.join(a.renders, fp)
        if os.path.exists(p):
            print(f"  {os.path.basename(p):28s} {inh(p):5.1f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
