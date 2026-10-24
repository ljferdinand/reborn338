#!/usr/bin/env python3
"""Compressor ballistics analyzer (clean-room, supertask 6, 2026-09-16).

Reads the level-step renders (gen_comp_ballistics.py) and extracts attack /
release time constants from the output envelope around each fader transition.

Method: a short-window dB envelope (10 ms window, 2.5 ms hop). Transitions are
found from the envelope gradient. After a quiet->loud step the gain reduces
(ATTACK): the output overshoots then settles down to the loud-block steady value.
After a loud->quiet step the gain releases (RELEASE): the output rises from the
still-reduced level up to the quiet-block steady value. Each is fit to a single
exponential toward that block's own steady state, so no cross-render sample
alignment is needed (ReBirth exports are not sample-deterministic, but an RMS
envelope is phase-independent and each block's steady state is measured in-file).

Usage: python comp_ballistics.py <render.wav>
"""
import argparse, sys, wave
import numpy as np

def load(path):
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(1) if ch > 1 else x
    return x, sr

def envelope(x, sr, win_ms=10.0, hop_ms=2.5):
    win = max(1, int(win_ms * sr / 1000)); hop = max(1, int(hop_ms * sr / 1000))
    n = 1 + (len(x) - win) // hop
    e = np.empty(n); t = np.empty(n)
    for i in range(n):
        s = i * hop; seg = x[s:s + win]
        e[i] = 10.0 * np.log10(np.mean(seg * seg) + 1e-20)
        t[i] = (s + win / 2) / sr
    return t, e, hop_ms / 1000.0

def find_transitions(t, e, min_jump=3.0):
    """Return indices where the envelope steps by > min_jump dB between blocks."""
    d = np.diff(e)
    idx = []
    i = 1
    while i < len(d) - 1:
        # local extremum of gradient above threshold
        if abs(d[i]) > min_jump and abs(d[i]) >= abs(d[i-1]) and abs(d[i]) >= abs(d[i+1]):
            idx.append(i); i += int(0.2 / (t[1]-t[0]))   # skip 200 ms
        else:
            i += 1
    return idx

def fit_tau(t, e, i0, direction, dt):
    """Fit e(t)=steady+(start-steady)*exp(-(t-t0)/tau) over ~600 ms after i0.
    Returns (tau_ms, steady_dB, start_dB)."""
    span = int(0.6 / dt)
    seg_t = t[i0:i0+span] - t[i0]; seg_e = e[i0:i0+span]
    if len(seg_e) < 10:
        return None
    steady = np.median(seg_e[-int(0.15/dt):])       # last 150 ms of the window
    start = seg_e[2]
    amp = start - steady
    if abs(amp) < 0.5:
        return (None, steady, start)                # no measurable transient
    y = (seg_e - steady) / amp                       # -> 1 at start, 0 at steady
    y = np.clip(y, 1e-3, None)
    mask = y > 0.05
    if mask.sum() < 5:
        return (None, steady, start)
    # linear fit of ln(y) vs t : slope = -1/tau
    A = np.polyfit(seg_t[mask], np.log(y[mask]), 1)
    tau = -1.0 / A[0] if A[0] < 0 else None
    return (tau * 1000 if tau else None, steady, start)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    a = ap.parse_args(argv)
    x, sr = load(a.wav)
    t, e, dt = envelope(x, sr)
    idx = find_transitions(t, e)
    print(f"=== {a.wav}  ({len(x)/sr:.1f}s) ===")
    print(f"  transitions at (s): {[round(t[i],2) for i in idx]}")
    for i in idx:
        rising = e[i+1] > e[i-1]
        kind = "quiet->loud (ATTACK: gain reduces)" if rising else "loud->quiet (RELEASE: gain recovers)"
        r = fit_tau(t, e, i, rising, dt)
        if r and r[0]:
            print(f"  t={t[i]:5.2f}s {kind}")
            print(f"     start {r[2]:.1f} dB -> steady {r[1]:.1f} dB, tau ~ {r[0]:.0f} ms")
        elif r:
            print(f"  t={t[i]:5.2f}s {kind}: start {r[2]:.1f} -> steady {r[1]:.1f} dB (no clean transient)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
