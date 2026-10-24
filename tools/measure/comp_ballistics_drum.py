#!/usr/bin/env python3
"""Compressor drum-transient ballistics analyzer (clean-room, 2026-09-16).

ATTACK from the stack hit: gain(t) = level(on) - level(off) in dB, the two
renders aligned at each hit onset, fine envelope (2 ms window, 0.5 ms hop). At the
onset the gain starts near unity and clamps to the steady reduction over the
attack time; the curve is averaged over all the hits (one per bar).

RELEASE from the pump: the kick slams the gain down at each bar downbeat; the
closed hats after it (one per 16th) read the gain recovering. For each hat step
its RMS in the on and off renders gives gain(step), and step k sits k*(bar/16) s
after the kick, so gain vs time is the release curve.

Onsets are found independently in each render (ReBirth exports are not
sample-deterministic) then stepped by the exact bar length. One method both sides.

Usage:
    python comp_ballistics_drum.py attack <off.wav> <on.wav> [--bpm 120]
    python comp_ballistics_drum.py release <off.wav> <on.wav> [--bpm 120]
"""
import argparse, sys, wave
import numpy as np

def load(path):
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(1) if ch > 1 else x
    return x, sr

def first_onset(x, sr, frac=0.30):
    """Sample index of the first hit: first crossing of frac*peak."""
    thr = frac * np.max(np.abs(x))
    idx = np.argmax(np.abs(x) > thr)
    return int(idx)

def fine_env(x, sr, s0, dur_s, win_ms=2.0, hop_ms=0.5):
    win = max(1, int(win_ms*sr/1000)); hop = max(1, int(hop_ms*sr/1000))
    n = int(dur_s*sr/hop)
    e = np.empty(n)
    for i in range(n):
        s = s0 + i*hop; seg = x[s:s+win]
        e[i] = 10*np.log10(np.mean(seg*seg)+1e-20) if len(seg) else -120.0
    t = np.arange(n)*hop/sr
    return t, e

def attack(off_p, on_p, bpm):
    xo, sr = load(off_p); xn, _ = load(on_p)
    bar = int(sr*4*60/bpm/4)              # 4 beats/bar -> bar seconds; = sr*60/bpm*4? see below
    bar = int(sr*(60.0/bpm)*4)            # 4 beats per bar
    o0 = first_onset(xo, sr); n0 = first_onset(xn, sr)
    nbars = min((len(xo)-o0)//bar, (len(xn)-n0)//bar)
    curves = []
    for k in range(nbars):
        to, eo = fine_env(xo, sr, o0+k*bar, 0.20)
        tn, en = fine_env(xn, sr, n0+k*bar, 0.20)
        m = min(len(eo), len(en))
        curves.append(en[:m]-eo[:m])
    L = min(len(c) for c in curves)
    g = np.mean(np.array([c[:L] for c in curves]), axis=0)
    t = np.arange(len(g))*0.5/1000.0
    steady = np.median(g[int(0.15/0.0005):])       # 150-200 ms
    print(f"=== ATTACK  ({nbars} hits averaged)  steady GR = {steady:.1f} dB ===")
    print("   ms   gain(dB)")
    for i in range(0, len(g), 4):                   # every 2 ms
        print(f"   {t[i]*1000:5.1f}  {g[i]:6.2f}")
        if t[i] > 0.06: break
    # attack tau: time for gain to reach (1-1/e) of the way to steady
    target = 0.632*steady
    ci = np.argmax(g <= target) if steady < 0 else 0
    print(f"   attack: reaches 63% of {steady:.1f} dB at ~{t[ci]*1000:.0f} ms")

def release(off_p, on_p, bpm):
    xo, sr = load(off_p); xn, _ = load(on_p)
    bar = int(sr*(60.0/bpm)*4); step = bar//16
    o0 = first_onset(xo, sr); n0 = first_onset(xn, sr)
    nbars = min((len(xo)-o0)//bar, (len(xn)-n0)//bar)
    win = int(0.02*sr)
    print(f"=== RELEASE (pumping, {nbars} bars averaged) ===")
    print("   step  t(ms)   gain(dB)")
    for s in range(1, 16):
        go = []; gn = []
        for k in range(nbars):
            so = o0 + k*bar + s*step; sn = n0 + k*bar + s*step
            po = 10*np.log10(np.mean(xo[so:so+win]**2)+1e-20)
            pn = 10*np.log10(np.mean(xn[sn:sn+win]**2)+1e-20)
            go.append(po); gn.append(pn)
        gain = np.median(np.array(gn)-np.array(go))
        print(f"   {s:4d}  {s*step/sr*1000:6.1f}  {gain:6.2f}")

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["attack", "release"])
    ap.add_argument("off"); ap.add_argument("on")
    ap.add_argument("--bpm", type=float, default=120.0)
    a = ap.parse_args(argv)
    (attack if a.mode == "attack" else release)(a.off, a.on, a.bpm)
    return 0

if __name__ == "__main__":
    sys.exit(main())
