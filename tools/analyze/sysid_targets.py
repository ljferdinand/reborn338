#!/usr/bin/env python3
"""
sysid_targets.py: extract the diode-ladder fitting targets from the ReBirth
sysid exports (static reso-127 held low note, cutoff 20/45/70/95/120), and the
same measurements from Reborn renders for the gap table.

Per file:
  f0            fundamental (strongest peak < 250 Hz, quadratic-interpolated)
  emph_hz       resonance emphasis: strongest peak 300-6000 Hz
  emph_db       its level relative to the fundamental (dB)
  bw_hz         -6 dB bandwidth of the emphasis peak
  hf_flat       spectral flatness 2112-8448 Hz (comb broadness)
  n_peaks       comb peaks >= 8 dB prominence, 300-10000 Hz
  harm_frac     fraction of those peaks within 2.5% of an integer multiple of f0
  harm_med_dev  median |deviation from nearest harmonic| as % of f0
  fold_frac     fraction of INHARMONIC peaks within 2.5% of a first-order
                aliasing fold |fs - k*f0| of some harmonic k
Harmonic comb from a real nonlinearity => harm_frac ~1. Aliasing comb =>
harm_frac low and fold_frac high. This discriminates hypothesis A vs B.

Usage: python sysid_targets.py --rebirth-dir DIR --reborn-dir DIR --out targets.json
Expects files named sysid_c{020,045,070,095,120}*.{wav,WAV} in each dir.
"""
import os, sys, glob, json, argparse, wave
import numpy as np
from scipy.signal import welch, find_peaks

def load_wav(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        v[v >= (1 << 23)] -= (1 << 24)
        x = v.astype(np.float64) / (1 << 23)
    elif sw == 4:
        f = np.frombuffer(raw, "<f4")
        x = f.astype(np.float64) if np.isfinite(f).all() and np.max(np.abs(f)) <= 4.0 \
            else np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError("unsupported sample width")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr

def active_segment(x, sr):
    """Trim to the loud, steady portion: drop leading/trailing silence and the
    first 0.5 s of attack; cap at 20 s of signal."""
    w = int(0.05 * sr)
    n = (len(x) // w) * w
    e = (x[:n].reshape(-1, w) ** 2).mean(axis=1)
    db = 10 * np.log10(e / (e.max() + 1e-20) + 1e-20)
    act = np.where(db > -50)[0]
    if act.size < 4:
        return x
    i0, i1 = act[0] * w + int(0.5 * sr), (act[-1] + 1) * w
    if i1 - i0 < sr:
        i0 = act[0] * w
    return x[i0:min(i1, i0 + 20 * sr)]

def interp_peak(f, pdb, i):
    if 0 < i < len(pdb) - 1:
        a, b, c = pdb[i - 1], pdb[i], pdb[i + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c + 1e-20)
        d = np.clip(d, -1, 1)
        return f[i] + d * (f[1] - f[0]), b - 0.25 * (a - c) * d
    return f[i], pdb[i]

def measure(path):
    x, sr = load_wav(path)
    x = active_segment(x, sr)
    nps = 32768
    f, P = welch(x, fs=sr, window="hann", nperseg=nps, noverlap=nps // 2)
    pdb = 10 * np.log10(P + 1e-20)

    # fundamental
    lo = (f > 20) & (f < 250)
    pk_lo, _ = find_peaks(pdb[lo], prominence=6)
    if pk_lo.size == 0:
        return None
    base = np.where(lo)[0][0]
    i0 = base + pk_lo[np.argmax(pdb[lo][pk_lo])]
    f0, f0_db = interp_peak(f, pdb, i0)

    # comb peaks 300-10000
    band = (f >= 300) & (f <= 10000)
    bidx = np.where(band)[0]
    pk, props = find_peaks(pdb[band], prominence=8, distance=max(1, int(10 / (f[1] - f[0]))))
    peaks = []
    for j in pk:
        fj, dbj = interp_peak(f, pdb, bidx[j])
        peaks.append((fj, dbj))

    # resonance emphasis 300-6000
    emph = [(fj, dbj) for fj, dbj in peaks if fj <= 6000]
    if emph:
        emph_hz, emph_pk_db = max(emph, key=lambda p: p[1])
        emph_db = emph_pk_db - f0_db
        # -6 dB bandwidth around the emphasis peak
        ie = np.argmin(np.abs(f - emph_hz))
        thr = pdb[ie] - 6.0
        a = ie
        while a > 0 and pdb[a] > thr: a -= 1
        b = ie
        while b < len(pdb) - 1 and pdb[b] > thr: b += 1
        bw = f[b] - f[a]
    else:
        emph_hz, emph_db, bw = float("nan"), float("nan"), float("nan")

    # HF flatness 2112-8448 (runbook octave bands)
    hfb = (f >= 2112) & (f <= 8448)
    hp = P[hfb] + 1e-20
    hf_flat = float(np.exp(np.mean(np.log(hp))) / np.mean(hp))

    # harmonicity of the comb
    harm_dev, inharm = [], []
    for fj, dbj in peaks:
        k = max(1, round(fj / f0))
        dev = abs(fj - k * f0) / f0
        harm_dev.append(dev)
        if dev > 0.025:
            inharm.append(fj)
    harm_frac = float(np.mean([d <= 0.025 for d in harm_dev])) if harm_dev else float("nan")
    harm_med = float(np.median(harm_dev) * 100) if harm_dev else float("nan")

    # first-order fold check on the inharmonic peaks
    fold_hits = 0
    for fj in inharm:
        k = round((sr - fj) / f0)
        if k > 0 and abs((sr - k * f0) - fj) / f0 <= 0.025:
            fold_hits += 1
    fold_frac = fold_hits / len(inharm) if inharm else 0.0

    return dict(f0=round(f0, 2), emph_hz=round(float(emph_hz), 1),
                emph_db=round(float(emph_db), 2), bw_hz=round(float(bw), 1),
                hf_flat=round(hf_flat, 4), n_peaks=len(peaks),
                harm_frac=round(harm_frac, 3), harm_med_dev=round(harm_med, 2),
                fold_frac=round(fold_frac, 3), n_inharm=len(inharm))

def find_file(d, tag):
    for pat in (f"{tag}*.wav", f"{tag}*.WAV"):
        m = sorted(glob.glob(os.path.join(d, pat)))
        if m:
            return m[0]
    return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rebirth-dir", required=True)
    ap.add_argument("--reborn-dir", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--tags", default="sysid_c020,sysid_c045,sysid_c070,sysid_c095,sysid_c120")
    a = ap.parse_args()

    cols = ["f0", "emph_hz", "emph_db", "bw_hz", "hf_flat", "n_peaks",
            "harm_frac", "harm_med_dev", "fold_frac", "n_inharm"]
    out = {}
    for tag in a.tags.split(","):
        row = {}
        for side, d in (("rebirth", a.rebirth_dir), ("reborn", a.reborn_dir)):
            p = find_file(d, tag)
            if p is None:
                print(f"{tag} [{side}]: MISSING")
                continue
            m = measure(p)
            row[side] = m
            print(f"{tag} [{side:7s}] " + " ".join(f"{c}={m[c]}" for c in cols))
        out[tag] = row
    if a.out:
        with open(a.out, "w") as fo:
            json.dump(out, fo, indent=1)
        print("wrote", a.out)

if __name__ == "__main__":
    main()
