#!/usr/bin/env python3
"""Compressor static-curve analyzer (clean-room, supertask 6, 2026-09-16).

Reads the comp-law fixtures (gen_comp_law.py): a sustained 303 drone through the
master compressor at swept fader / threshold / ratio. Gain reduction is the
steady-state level of a comp-ON render minus the matching comp-OFF render at the
SAME fader, in dB. Steady-state RMS over a mid-song window is phase-independent,
so it is a valid A/B level comparison for ReBirth's non-deterministic exports
(docs/MEASUREMENT_PROTOCOL.md forbids a sample null test, not an energy
comparison).

ONE method applied to every file: mean-square over [win_lo, win_hi] seconds,
stereo measured per channel and as the mean, reported in dBFS.

Naming (gen_comp_law.py): `off_<key>.wav` is the comp-OFF reference for fader
`<key>` (v127/v100/.../acc); `<config>_<key>.wav` (e.g. r127_t000_v080) is a
comp-ON render whose reference is `off_<key>`.

Usage:
    python comp_curve.py <renders_dir>            # full table + per-config curves
    python comp_curve.py <renders_dir> --win 2 6  # custom steady window (s)
"""
import argparse, glob, os, re, sys, wave
import numpy as np

def load(path):
    w = wave.open(path)
    sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    x = x.reshape(-1, ch) if ch > 1 else x.reshape(-1, 1)
    return x, sr

def rms_db(x):
    ms = np.mean(x * x)
    return 10.0 * np.log10(ms + 1e-20)   # dBFS (power)

def level(path, lo, hi):
    """Steady-state level in dBFS: per-channel and channel-mean over [lo,hi] s."""
    x, sr = load(path)
    a, b = int(lo * sr), int(hi * sr)
    b = min(b, x.shape[0]); a = min(a, max(0, b - 1))
    seg = x[a:b]
    perch = [rms_db(seg[:, c]) for c in range(seg.shape[1])]
    mean = rms_db(seg)                      # power over both channels
    return mean, perch

def parse(name):
    """(config, key). off_v127 -> ('off','v127'); r127_t000_v080 -> ('r127_t000','v080')."""
    stem = os.path.splitext(os.path.basename(name))[0]
    m = re.match(r"^(.*)_(v\d{3}|acc)$", stem)
    if not m:
        return stem, None
    return m.group(1), m.group(2)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders")
    ap.add_argument("--win", nargs=2, type=float, default=[2.0, 6.0],
                    metavar=("LO", "HI"), help="steady-state window, seconds")
    a = ap.parse_args(argv)
    lo, hi = a.win
    files = sorted(glob.glob(os.path.join(a.renders, "*.wav")))
    if not files:
        print("no wavs in", a.renders); return 1

    lv = {}   # stem -> (mean_db, [L,R])
    for f in files:
        stem = os.path.splitext(os.path.basename(f))[0]
        lv[stem] = level(f, lo, hi)

    # Reference (comp-OFF) input level per fader key.
    off = {}
    for stem, (mean, _) in lv.items():
        cfg, key = parse(stem)
        if cfg == "off" and key:
            off[key] = mean

    print(f"=== comp curve  (window {lo}-{hi}s, dBFS power, GR = on - off at same fader) ===\n")
    print("-- input-level calibration (comp OFF) --")
    for key in sorted(off, key=lambda k: (k != "acc", k)):
        print(f"   off_{key:4s}  {off[key]:7.2f} dBFS")

    # Group comp-ON fixtures by config, print GR vs input level.
    configs = {}
    for stem, (mean, perch) in sorted(lv.items()):
        cfg, key = parse(stem)
        if cfg in ("off", None) or key not in off:
            continue
        configs.setdefault(cfg, []).append((off[key], mean - off[key], key, mean))
    for cfg in sorted(configs):
        print(f"\n-- {cfg} : GR vs input level --")
        print("     inLvl(dBFS)   GR(dB)   outLvl   fader")
        for inlvl, gr, key, out in sorted(configs[cfg], reverse=True):
            print(f"     {inlvl:8.2f}   {gr:7.2f}  {out:7.2f}   {key}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
