#!/usr/bin/env python3
"""Fit the 303's accent on BOTH fixture designs at once.

THE RULE THIS TOOL EXISTS TO ENFORCE. On 2026-08-29 an accent refit that
improved one design fourfold was rejected outright by the other, and the reason
was not the constants, it was that the two designs measure different things:

    accentdepth   ONE NOTE PER BAR, 1 note and 15 rests. An accent on an
                  ISOLATED note, nothing carrying in, the filter envelope
                  starting from its floor.
    B7 (and B3)   CONTINUOUS SIXTEENTHS, 2 of 16 accented. An accent inside a
                  STREAM, with the previous note's envelope, the accent latch
                  and the shortened decay all still live.

Both happen in real music and neither fixture is wrong. **No accent fit is
finished until it is scored on both**, so this scores both on every candidate
rather than fitting one and checking the other at the end.

Both are read the same way: the accented step's brightness against the SAME step
of that cell's own no-accent control, so the pattern, the note, the filter
envelope and the VCA cancel and only the accent is left.

    python fit_accent_joint.py --check ACC_SWEEP_CUT=1.3
    python fit_accent_joint.py --grid
"""
import argparse
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bass303_envelope import load, centroid_track, fold, CLEAN

WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")
B7FIX = os.path.join(WS, "fixtures-2026-08-29", "b7-303-accent")
ADFIX = os.path.join(WS, "fixtures-2026-08-02", "accentdepth")
B7RB = os.path.join(WS, "renders", "b7-303-accent-2026-08-29")
ADRB = os.path.join(WS, "renders", "accentdepth_rb")

CELLS = [(64, 16), (64, 60), (64, 100), (32, 60), (96, 60)]
KNOBS = (0, 32, 64, 96, 127)
ACC_STEPS = (4, 12)
AD_PATCH = [("c064_r060", (0, 16, 32, 48, 64, 80, 96, 112, 127)),
            ("c100_r030", (0, 32, 64, 96, 127))]

B7NAMES = ([ "b7_c%03d_r%03d_plain" % (c, r) for c, r in CELLS ] +
           [ "b7_c%03d_r%03d_k%03d" % (c, r, k) for c, r in CELLS for k in KNOBS ])
ADNAMES = (["d303_acc000_off_c064_r060"] +
           [ "d303_acc%03d_on_%s" % (k, p) for p, ks in AD_PATCH for k in ks ])


def b7_law(d):
    def fo(p):
        x, sr = load(p)
        _, c, _ = centroid_track(x, sr)
        return fold(None, c, sr)[:, :CLEAN + 1]
    out = np.full((len(CELLS), len(KNOBS)), np.nan)
    for i, (c, r) in enumerate(CELLS):
        p0 = os.path.join(d, "b7_c%03d_r%03d_plain.wav" % (c, r))
        if not os.path.exists(p0):
            continue
        rb = fo(p0)
        for j, k in enumerate(KNOBS):
            p = os.path.join(d, "b7_c%03d_r%03d_k%03d.wav" % (c, r, k))
            if os.path.exists(p):
                fb = fo(p)
                out[i, j] = np.nanmean([np.nanmean(fb[s] - rb[s]) for s in ACC_STEPS])
    return out


def _bright(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)[int(0.3 * sr):]
    F = 8192
    acc = np.zeros(F // 2 + 1)
    win = np.hanning(F)
    k = 0
    for s in range(0, len(x) - F, F // 2):
        for c in range(ch):
            acc += np.abs(np.fft.rfft(x[s:s + F, c] * win)) ** 2
        k += 1
    acc /= max(k, 1)
    f = np.fft.rfftfreq(F, 1.0 / sr)
    return 10 * np.log10(acc[(f >= 1000) & (f <= 8000)].sum() /
                         acc[(f >= 60) & (f <= 250)].sum())


def ad_law(d):
    off = os.path.join(d, "d303_acc000_off_c064_r060.wav")
    if not os.path.exists(off):
        return None
    b0 = _bright(off)
    out = []
    for p, ks in AD_PATCH:
        for k in ks:
            q = os.path.join(d, "d303_acc%03d_on_%s.wav" % (k, p))
            out.append(_bright(q) - b0 if os.path.exists(q) else np.nan)
    return np.array(out)


def render(overrides, out):
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for k, v in overrides.items():
        env["REBORN_" + k] = str(v)
    for fixdir, names, solo in ((B7FIX, B7NAMES, None), (ADFIX, ADNAMES, "bass1")):
        for n in names:
            cmd = [EXE, "--song", os.path.join(fixdir, n + ".rbs"),
                   "--no-fx", "--seconds", "8", "--out", os.path.join(out, n + ".wav")]
            if solo:
                cmd[3:3] = ["--solo", solo]
            subprocess.run(cmd, check=True, env=env, stdout=subprocess.DEVNULL)


def rms(a, b):
    d = a - b
    ok = np.isfinite(d)
    return float(np.sqrt((d[ok] ** 2).mean())) if ok.any() else np.inf


def parse(items):
    o = {}
    for it in items or []:
        k, _, v = it.partition("=")
        o[k.strip().upper()] = v.strip()
    return o


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", nargs="*", default=None, metavar="KEY=VAL")
    ap.add_argument("--grid", action="store_true")
    a = ap.parse_args(argv)

    T7, TA = b7_law(B7RB), ad_law(ADRB)

    def run(o):
        with tempfile.TemporaryDirectory() as td:
            render(o, td)
            return b7_law(td), ad_law(td)

    if a.grid:
        cands = [{"ACC_SWEEP": w, "ACC_SWEEP_CUT": c}
                 for w in (3.30, 4.20, 5.10, 6.00)
                 for c in (0.0, 0.6, 1.3, 2.0)]
        print("%-44s %9s %9s %9s" % ("candidate", "B7", "accdepth", "sum"))
        best = None
        for c in cands:
            S7, SA = run(c)
            e7, ea = rms(T7, S7), rms(TA, SA)
            print("%-44s %9.3f %9.3f %9.3f"
                  % (" ".join("%s=%s" % kv for kv in sorted(c.items())), e7, ea, e7 + ea))
            sys.stdout.flush()
            if best is None or e7 + ea < best[0]:
                best = (e7 + ea, c)
        print("\nbest: %s at %.3f"
              % (" ".join("%s=%s" % kv for kv in sorted(best[1].items())), best[0]))
        return 0

    o = parse(a.check)
    S7, SA = run(o)
    print("overrides: %s" % (" ".join("%s=%s" % kv for kv in sorted(o.items()))
                             or "(defaults)"))
    print("B7 streamed %.3f dB rms, accentdepth isolated %.3f dB rms"
          % (rms(T7, S7), rms(TA, SA)))
    print("%-12s %10s %10s %10s" % ("B7 cell", "RB span", "our span", "deficit"))
    for i, (c, r) in enumerate(CELLS):
        sa, sb = T7[i, -1] - T7[i, 0], S7[i, -1] - S7[i, 0]
        print("%-12s %10.2f %10.2f %10.2f" % ("c%d r%d" % (c, r), sa, sb, sa - sb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
