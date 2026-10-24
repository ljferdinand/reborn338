#!/usr/bin/env python3
"""
envmod_ab.py: characterise the 303 env-mod SWEEP DYNAMICS, ReBirth vs Reborn.

The open filter gap is not gain: ReBirth's spectrum stays flat across 30-2000 Hz
because its env-mod sweeps the resonant peak across the range each note, while
our core parks a peak and scoops the mids. Static spectra cannot see that, so
this measures the sweep itself:

  fill      octave-band energy profile (flat = swept, scooped = parked)
  traj      per-note centroid trajectory: centroid in successive windows after
            each onset (start -> mid -> tail), i.e. how the peak moves
  depth     bright-to-dark span within a note (max-min of the trajectory, in
            octaves), the actual "sweep depth"
  dwell     time for the note centroid to fall to half its peak (sweep speed)

Usage:
  python envmod_ab.py --fixtures DIR --exports DIR --render EXE --out DIR
                      [--filter rb] [--seconds 20] [--only substr]
"""
import os, sys, glob, argparse, subprocess, wave
import numpy as np

BANDS = [(30,120),(120,250),(250,500),(500,1000),(1000,2000),(2000,4000),(4000,8000)]

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
    else:
        x = np.frombuffer(raw, "<f4").astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr

def trim_active(x, thresh=0.02):
    e = np.abs(x)
    if e.max() <= 0: return x
    idx = np.where(e > e.max()*thresh)[0]
    return x[idx[0]:idx[-1]] if idx.size else x

def band_fill(x, sr):
    xa = trim_active(x)
    S = np.abs(np.fft.rfft(xa*np.hanning(len(xa))))**2
    f = np.fft.rfftfreq(len(xa), 1/sr)
    tot = S.sum()+1e-30
    return np.array([10*np.log10(S[(f>=lo)&(f<hi)].sum()/tot+1e-12) for lo,hi in BANDS])

def onsets(x, sr, thresh=0.25, refractory=0.10):
    w = max(8, int(0.005*sr))
    n = (len(x)//w)*w
    e = np.sqrt((x[:n].reshape(-1,w)**2).mean(axis=1))
    if e.max() <= 0: return [], e, w/sr
    thr = e.max()*thresh
    out, last = [], -1e9
    for i in range(1, len(e)):
        if e[i] > thr and e[i-1] <= thr and (i*(w/sr) - last) > refractory:
            out.append(i); last = i*(w/sr)
    return out, e, w/sr

def centroid(seg, sr):
    if len(seg) < 128: return float("nan")
    S = np.abs(np.fft.rfft(seg*np.hanning(len(seg))))**2
    f = np.fft.rfftfreq(len(seg), 1/sr)
    return float((f*S).sum()/(S.sum()+1e-30))

def note_trajectory(x, sr, wins=(0.010, 0.040, 0.090, 0.170)):
    """Median centroid in successive windows after each onset."""
    idx, e, dt = onsets(x, sr)
    rows = []
    for i0 in idx:
        s0 = int(i0*dt*sr)
        c = []
        prev = 0.0
        for w in wins:
            a, b = s0+int(prev*sr), s0+int(w*sr)
            c.append(centroid(x[a:b], sr))
            prev = w
        if all(v == v for v in c):
            rows.append(c)
    if not rows:
        return [float("nan")]*len(wins), float("nan"), float("nan")
    arr = np.array(rows)
    med = np.nanmedian(arr, axis=0)
    depth = float(np.log2(max(med)/max(1e-9, min(med)))) if min(med) > 0 else float("nan")
    # dwell: window index where centroid falls below halfway between max and min
    half = (max(med)+min(med))/2.0
    dwell = float("nan")
    for k, w in enumerate(wins):
        if med[k] <= half:
            dwell = w; break
    return med, depth, dwell

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--exports", required=True)
    ap.add_argument("--render", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--filter", default="")
    ap.add_argument("--seconds", default="20")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    env = os.environ.copy()
    if a.filter: env["REBORN_FILTER"] = a.filter

    print(f"{'fixture':18s} {'side':8s} {'centroid trajectory (Hz)':32s} {'depth':>6s} {'dwell':>6s}  bands 30/120/250/500/1k/2k/4k")
    for rbs in sorted(glob.glob(os.path.join(a.fixtures, "*.rbs"))):
        name = os.path.splitext(os.path.basename(rbs))[0]
        if a.only and a.only not in name: continue
        exp = None
        for pat in (name+".WAV", name+".wav"):
            m = glob.glob(os.path.join(a.exports, "**", pat), recursive=True)
            if m: exp = m[0]; break
        if not exp:
            continue
        ren = os.path.join(a.out, name+".wav")
        subprocess.run([a.render, "--song", rbs, "--solo", "bass1", "--no-dist",
                        "--seconds", a.seconds, "--out", ren], env=env,
                       capture_output=True, check=True)
        for side, path in (("ReBirth", exp), ("Reborn", ren)):
            x, sr = load_wav(path)
            med, depth, dwell = note_trajectory(x, sr)
            fill = band_fill(x, sr)
            traj = " ".join(f"{v:7.0f}" for v in med)
            bands = " ".join(f"{v:5.1f}" for v in fill)
            print(f"{name if side=='ReBirth' else '':18s} {side:8s} {traj:32s} "
                  f"{depth:6.2f} {dwell if dwell==dwell else -1:6.3f}  {bands}")
        print()

if __name__ == "__main__":
    main()
