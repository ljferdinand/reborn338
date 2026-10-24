#!/usr/bin/env python3
"""Cut a 303 listening set from ISOLATED fixtures, ReBirth against ours.

Not a song. Every fixture here sounds ONE 303, no second 303, no drums, no
effects, and each one passes `isolation_check.py`. That is the rule added on
2026-08-29 after every listening set to that point turned out to be a demo song
with three effects running.

THE NULL PAIR GOES FIRST, always: `b1_ref`, the same fixture with the filter wide
open, ours against ReBirth's. Luke hears what "the same" sounds like before being
asked whether anything differs.

ONE GAIN FOR THE WHOLE SET, taken from ReBirth's loudest file, so level
differences between files are real and not normalised away.

Ours is rendered with `REBORN_BASS_ENGINE=tpt`, because parity work is on the
native voice.

    python listen_303.py --out ../../../listen-303-filter-2026-08-29
"""
import argparse
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")

# fixture dir, ReBirth export dir, fixture stem, what it is for
SET = [
    ("b1-303-filter", "b1-303-filter-2026-08-29", "b1_ref",
     "00-NULL-PAIR filter wide open, no resonance. the calibration"),
    ("b2b3-303-env", "b2b3-303-env-2026-08-29", "b2_e127_d000",
     "01 plucked, env mod 127 decay 0. the envelope decay law"),
    ("b2b3-303-env", "b2b3-303-env-2026-08-29", "b2_e096_d064",
     "02 plucked, env mod 96 decay 64. the same filter, played musically"),
    ("b7-303-accent", "b7-303-accent-2026-08-29", "b7_c032_r060_k096",
     "03 accents in a stream, cutoff 32. the level tilt and the accent law"),
    ("b7-303-accent", "b7-303-accent-2026-08-29", "b7_c064_r060_k096",
     "04 accents in a stream, cutoff 64. the middle of both laws"),
    ("b7-303-accent", "b7-303-accent-2026-08-29", "b7_c096_r060_k096",
     "05 accents in a stream, cutoff 96. where the accent was already right"),
    ("b1-303-filter", "b1-303-filter-2026-08-29", "b1_c064_r096",
     "06 resonant drone, cutoff 64 resonance 96. the level law, held still"),
]
SECONDS = 8.0

# Everything 2026-08-29 changed, as overrides that put the voice back to how it
# sounded at the START of that day. Rendering this alongside gives a THREE-WAY:
# ReBirth, where we were, where we are. A two-way can only say whether we match;
# a three-way says whether a day's work moved TOWARD ReBirth or merely moved.
BEFORE = {
    "DEC_A": "0.030", "DEC_B": "1.87", "DEC_C": "0.0",
    "LVL_CUT": "0", "LVL_TRIM": "1.0",
    "ACC_SHORT": "0.34", "ACC_SHORT_CUT": "0",
    "ACC_ATK": "0.008", "ACC_DEC": "0.040",
}


def read(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def write(path, x, sr):
    x = np.clip(x, -1.0, 1.0)
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((x * 32767.0).astype("<i2").tobytes())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--before", action="store_true",
                    help="also render the pre-2026-08-29 voice, for a three-way")
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)

    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    envb = dict(env)
    for k, v in BEFORE.items():
        envb["REBORN_" + k] = v

    rows, peak, tmps = [], 0.0, []
    for fxdir, rbdir, stem, label in SET:
        rb = os.path.join(WS, "renders", rbdir, stem + ".wav")
        song = os.path.join(WS, "fixtures-2026-08-29", fxdir, stem + ".rbs")
        if not (os.path.exists(rb) and os.path.exists(song)):
            print("missing, skipped: %s" % stem)
            continue
        t_now = os.path.join(out, "_now_" + stem + ".wav")
        subprocess.run([EXE, "--song", song, "--no-fx", "--seconds",
                        str(SECONDS), "--out", t_now],
                       check=True, env=env, stdout=subprocess.DEVNULL)
        tmps.append(t_now)
        t_was = None
        if a.before:
            t_was = os.path.join(out, "_was_" + stem + ".wav")
            subprocess.run([EXE, "--song", song, "--no-fx", "--seconds",
                            str(SECONDS), "--out", t_was],
                           check=True, env=envb, stdout=subprocess.DEVNULL)
            tmps.append(t_was)
        xr, sr = read(rb)
        n = int(SECONDS * sr)
        xn = read(t_now)[0][:n]
        xw = read(t_was)[0][:n] if t_was else None
        peak = max(peak, float(np.abs(xr[:n]).max()))
        rows.append((label, xr[:n], xn, xw, sr))

    g = 0.89 / max(peak, 1e-9)
    print("one gain for the set: %.3f, from ReBirth's loudest file" % g)
    for label, xr, xn, xw, sr in rows:
        write(os.path.join(out, label + " - 1 REBIRTH.wav"), xr * g, sr)
        if xw is not None:
            write(os.path.join(out, label + " - 2 OURS BEFORE.wav"), xw * g, sr)
        write(os.path.join(out, label + " - 3 OURS NOW.wav"), xn * g, sr)
        print("  %s" % label)
    for t in tmps:
        if os.path.exists(t):
            os.remove(t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
