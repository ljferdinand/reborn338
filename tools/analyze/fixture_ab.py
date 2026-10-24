#!/usr/bin/env python3
"""
fixture_ab.py: class-aware A/B over the 2026-07-23 fixture set.

For every fixture .rbs: render the Reborn side (headless), load the human's
ReBirth export, and compute metrics appropriate to the fixture's class:

  tune     f0 of the held note (interpolated FFT peak)
  rawosc   HF comb structure at cutoff max / reso 0: flatness 2-16 kHz,
           peak count, harmonic fraction (vs f0), level
  decay    per-hit amplitude decay tau (median over hits)
  drum     per-hit: peak level dB, decay tau, spectral centroid, dominant
           frequency, 85% rolloff, hit duration; plus per-file aggregates

Usage:
  python fixture_ab.py --fixtures DIR --exports DIR --render EXE --samples DIR
                       [--only substr] [--out results.json]

Pairs are matched by basename (fixture x.rbs <-> export x.WAV).
"""
import os, sys, glob, json, argparse, subprocess, wave
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
        raise ValueError("unsupported width")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr

def env_rms(x, sr, win_ms=5.0):
    w = max(8, int(win_ms * 1e-3 * sr))
    n = (len(x) // w) * w
    e = np.sqrt((x[:n].reshape(-1, w) ** 2).mean(axis=1))
    return e, w / sr

def onsets(x, sr, thresh_ratio=0.15, refractory_s=0.06):
    e, dt = env_rms(x, sr, 5.0)
    if e.max() <= 0:
        return [], e, dt
    thr = e.max() * thresh_ratio
    idx = []
    last = -1e9
    for i in range(1, len(e)):
        if e[i] > thr and e[i - 1] <= thr and (i * dt - last) > refractory_s:
            idx.append(i)
            last = i * dt
    return idx, e, dt

def interp_fpeak(f, mag, i):
    if 0 < i < len(mag) - 1:
        a, b, c = mag[i - 1], mag[i], mag[i + 1]
        d = 0.5 * (a - c) / (a - 2 * b + c + 1e-30)
        d = float(np.clip(d, -1, 1))
        return f[i] + d * (f[1] - f[0])
    return f[i]

def measure_tune(x, sr):
    """f0 of a held tone: pick the strongest spectral peak below 800 Hz, then
    return the LOWEST peak that is a subharmonic-consistent fundamental."""
    seg = x[int(1.0 * sr):int(6.0 * sr)]
    if len(seg) < sr:
        seg = x
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18))
    f = np.fft.rfftfreq(1 << 18, 1 / sr)
    band = (f > 30) & (f < 800)
    fb, Sb = f[band], S[band]
    pk, _ = find_peaks(20 * np.log10(Sb + 1e-12), prominence=12)
    if len(pk) == 0:
        return float("nan")
    # strongest peak, then walk down: if a lower peak exists at ~1/2 or ~1/3
    # of it with decent level, prefer it (fundamental under strong harmonics)
    imax = pk[np.argmax(Sb[pk])]
    f_best = interp_fpeak(fb, Sb, imax)
    for div in (2, 3, 4):
        target = f_best / div
        if target < 25:
            break
        cand = [i for i in pk if abs(fb[i] - target) < 0.04 * target]
        if cand and Sb[cand[0]] > 0.05 * Sb[imax]:
            f_best = interp_fpeak(fb, Sb, cand[0])
    return float(f_best)

def measure_rawosc(x, sr):
    seg = x[int(1.0 * sr):int(6.0 * sr)]
    if len(seg) < sr:
        seg = x
    nps = 32768
    f, P = welch(seg, fs=sr, nperseg=nps, noverlap=nps // 2)
    pdb = 10 * np.log10(P + 1e-24)
    hf = (f >= 2000) & (f <= 16000)
    hp = P[hf] + 1e-24
    flat = float(np.exp(np.mean(np.log(hp))) / np.mean(hp))
    f0 = measure_tune(x, sr)
    band = (f >= 300) & (f <= 16000)
    bidx = np.where(band)[0]
    pk, _ = find_peaks(pdb[band], prominence=10)
    peaks = [interp_fpeak(f, pdb, bidx[j]) for j in pk]
    harm = [p for p in peaks if abs(p - round(p / f0) * f0) < max(2.5, 0.02 * f0)] if f0 == f0 else []
    rms = float(np.sqrt((seg ** 2).mean()))
    return dict(f0=round(f0, 2), hf_flat=round(flat, 4), n_peaks=len(peaks),
                harm_frac=round(len(harm) / max(1, len(peaks)), 3),
                rms_db=round(20 * np.log10(rms + 1e-12), 1))

def fit_decay_tau(e, dt, i0, i1):
    """Fit exp decay to the envelope between the peak after onset i0 and i1."""
    seg = e[i0:i1]
    if len(seg) < 6:
        return float("nan")
    p = int(np.argmax(seg))
    tail = seg[p:]
    tail = tail[tail > seg[p] * 0.02]
    if len(tail) < 5:
        return float("nan")
    y = np.log(tail + 1e-12)
    t = np.arange(len(tail)) * dt
    slope = np.polyfit(t, y, 1)[0]
    if slope >= -1e-9:
        return float("inf")
    return float(-1.0 / slope)

def measure_hits(x, sr, fmax_dom=None):
    """Per-hit metrics for drum/decay fixtures."""
    idx, e, dt = onsets(x, sr)
    hits = []
    for k, i0 in enumerate(idx):
        i1 = idx[k + 1] if k + 1 < len(idx) else len(e)
        tau = fit_decay_tau(e, dt, i0, i1)
        s0 = int(i0 * dt * sr)
        s1 = min(len(x), s0 + int(0.35 * sr))
        seg = x[s0:s1]
        if len(seg) < 256:
            continue
        S = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1 / sr)
        Ptot = (S ** 2)
        cent = float((f * Ptot).sum() / (Ptot.sum() + 1e-30))
        csum = np.cumsum(Ptot)
        roll = float(f[int(np.searchsorted(csum, 0.85 * csum[-1]))])
        lim = fmax_dom or 2000.0
        dom_band = (f > 25) & (f < lim)
        dom = float(f[dom_band][np.argmax(S[dom_band])]) if dom_band.any() else float("nan")
        peak_db = float(20 * np.log10(e[i0:i1].max() + 1e-12))
        # audible hit duration: time until env falls 40 dB under its peak
        pk = e[i0:i1].max()
        below = np.where(e[i0:i1] < pk * 0.01)[0]
        dur = float(below[0] * dt) if below.size else float((i1 - i0) * dt)
        hits.append(dict(tau=round(tau, 4), cent=round(cent, 1), roll=round(roll, 1),
                         dom=round(dom, 1), peak_db=round(peak_db, 1), dur=round(dur, 3)))
    def med(key):
        v = [h[key] for h in hits if h[key] == h[key] and h[key] != float("inf")]
        return round(float(np.median(v)), 4) if v else float("nan")
    return dict(n_hits=len(hits),
                tau=med("tau"), cent=med("cent"), roll=med("roll"),
                dom=med("dom"), peak_db=med("peak_db"), dur=med("dur"),
                hits=hits[:6])

def classify(name):
    if name.startswith("t303_tune"):
        return "tune"
    if name.startswith("t303_rawosc"):
        return "rawosc"
    if name.startswith("t303_decay"):
        return "decay"
    return "drum"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--exports", required=True)
    ap.add_argument("--render", required=True)
    # Omit --samples to measure the pure SYNTHESIS path. Both are real modes of
    # the engine: a loaded sample overrides synthesis per voice, so a run with
    # no sample directory exercises the clean-room voices instead.
    ap.add_argument("--samples", default="")
    ap.add_argument("--rendered-dir", required=True)
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=None)
    ap.add_argument("--seconds", default="10")
    a = ap.parse_args()

    os.makedirs(a.rendered_dir, exist_ok=True)
    results = {}
    rbs_files = sorted(glob.glob(os.path.join(a.fixtures, "**", "*.rbs"), recursive=True))
    for rbs in rbs_files:
        name = os.path.splitext(os.path.basename(rbs))[0]
        if a.only and a.only not in name:
            continue
        exp = None
        for pat in (name + ".WAV", name + ".wav"):
            m = glob.glob(os.path.join(a.exports, "**", pat), recursive=True)
            if m:
                exp = m[0]
                break
        if not exp:
            print(f"{name}: NO EXPORT, skipped")
            continue
        ren = os.path.join(a.rendered_dir, name + ".wav")
        cls = classify(name)
        cmd = [a.render, "--song", rbs, "--seconds", a.seconds, "--out", ren]
        if cls in ("tune", "rawosc", "decay"):
            cmd += ["--solo", "bass1", "--no-dist"]
        elif a.samples:
            cmd += ["--samples", a.samples]
        subprocess.run(cmd, capture_output=True, check=True)

        row = {}
        for side, path in (("rebirth", exp), ("reborn", ren)):
            x, sr = load_wav(path)
            if cls == "tune":
                row[side] = dict(f0=round(measure_tune(x, sr), 2))
            elif cls == "rawosc":
                row[side] = measure_rawosc(x, sr)
            elif cls == "decay":
                m = measure_hits(x, sr)
                row[side] = dict(n_hits=m["n_hits"], tau=m["tau"], peak_db=m["peak_db"])
            else:
                row[side] = measure_hits(x, sr)
                row[side].pop("hits", None)
        results[name] = row
        rb, rn = row["rebirth"], row["reborn"]
        keys = [k for k in rb.keys() if k in rn and not isinstance(rb[k], list)]
        diff = "  ".join(f"{k}:{rb[k]}|{rn[k]}" for k in keys)
        print(f"{name} [{cls}]  (rebirth|reborn)  {diff}")

    if a.out:
        with open(a.out, "w") as fo:
            json.dump(results, fo, indent=1)
        print("wrote", a.out)

if __name__ == "__main__":
    main()
