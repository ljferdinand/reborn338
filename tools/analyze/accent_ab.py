#!/usr/bin/env python3
"""
accent_ab.py: characterise the 303 ACCENT circuit, ReBirth vs Reborn.

The accent fixtures accent every 4th sixteenth, so each file contains both
accented and unaccented notes under identical knobs. That lets us separate the
two things accent does, without any cross-file level confound:

  level lift   accented peak dB  -  unaccented peak dB   (the VCA boost)
  bright lift  accented centroid / unaccented centroid   (the extra filter sweep)

Comparing accent_on (knob 110) against accent_off (knob 0) then shows how much
of each the ACCENT KNOB actually controls.

Usage:
  python accent_ab.py --fixtures DIR --exports DIR --render EXE --out DIR
                      [--seconds 8]
"""
import os, sys, glob, argparse, subprocess
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from envmod_ab import load_wav, onsets, centroid

def per_note(x, sr, win=0.060):
    """Peak level (dB) and centroid for every note onset."""
    idx, e, dt = onsets(x, sr, thresh=0.18, refractory=0.060)
    rows = []
    for k, i0 in enumerate(idx):
        i1 = idx[k+1] if k+1 < len(idx) else len(e)
        pk = float(e[i0:i1].max())
        s0 = int(i0*dt*sr)
        c = centroid(x[s0:s0+int(win*sr)], sr)
        if pk > 0 and c == c:
            rows.append((20*np.log10(pk+1e-12), c))
    return rows

def summarise(x, sr, period=4):
    rows = per_note(x, sr)
    if len(rows) < 8:
        return None
    # phase-align: the accented note is the loudest of the first `period`
    first = [rows[i][0] for i in range(min(period, len(rows)))]
    off = int(np.argmax(first))
    acc = [r for i, r in enumerate(rows) if (i - off) % period == 0]
    una = [r for i, r in enumerate(rows) if (i - off) % period != 0]
    if not acc or not una:
        return None
    a_db = float(np.median([r[0] for r in acc]));  u_db = float(np.median([r[0] for r in una]))
    a_c  = float(np.median([r[1] for r in acc]));  u_c  = float(np.median([r[1] for r in una]))
    return dict(n=len(rows), a_db=a_db, u_db=u_db, lift_db=a_db-u_db,
                a_c=a_c, u_c=u_c, bright=a_c/max(1.0, u_c))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True)
    ap.add_argument("--exports", required=True)
    ap.add_argument("--render", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seconds", default="8")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    print(f"{'fixture':18s} {'side':8s} {'notes':>5s} {'acc dB':>7s} {'unacc dB':>9s} "
          f"{'LIFT dB':>8s} {'acc cent':>9s} {'unacc cent':>11s} {'BRIGHT x':>9s}")
    for name in ("t303_accent_off", "t303_accent_on"):
        rbs = os.path.join(a.fixtures, name + ".rbs")
        if not os.path.exists(rbs):
            continue
        exp = None
        for pat in (name+".WAV", name+".wav"):
            m = glob.glob(os.path.join(a.exports, "**", pat), recursive=True)
            if m: exp = m[0]; break
        ren = os.path.join(a.out, name + ".wav")
        subprocess.run([a.render, "--song", rbs, "--solo", "bass1", "--no-dist",
                        "--seconds", a.seconds, "--out", ren], capture_output=True, check=True)
        for side, path in (("ReBirth", exp), ("Reborn", ren)):
            if not path: continue
            x, sr = load_wav(path)
            s = summarise(x, sr)
            if not s:
                print(f"{name if side=='ReBirth' else '':18s} {side:8s}  (too few notes)")
                continue
            print(f"{name if side=='ReBirth' else '':18s} {side:8s} {s['n']:5d} "
                  f"{s['a_db']:7.1f} {s['u_db']:9.1f} {s['lift_db']:8.2f} "
                  f"{s['a_c']:9.0f} {s['u_c']:11.0f} {s['bright']:9.3f}")
        print()

if __name__ == "__main__":
    main()
