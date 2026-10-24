#!/usr/bin/env python3
"""Cut a THREE-WAY listening set from a WINDOW of a real song, rack off.

A song is not a fixture and docs/MEASUREMENT_PROTOCOL.md is right that it cannot
isolate anything. It is still the thing the work is for, so once a component is
measured it is worth hearing in music, provided the window is chosen honestly
and the confounds are named rather than hidden.

TWO RULES MAKE THIS LEGITIMATE RATHER THAN A DEMO.

**The rack is OFF on both sides.** `renders/nofxexports/` holds ReBirth exports
with the effect rack switched off, and ours is rendered `--no-fx` to match. That
removes the compressor, which we do not implement at all, and the delay and
distortion, which are only partly fitted. Without it every difference heard is
attributable to nothing in particular.

**The window is chosen for what is PLAYING in it.** Calm's first 48 seconds is
bass 1 alone: measured on ReBirth's own export, the 4 to 10 kHz share is 0.00
percent and the level is flat, and the drums enter at 50 s. So a window inside
that is a single 303 in real music, which is as close to a fixture as a song
gets. A window after it is a whole mix and is labelled as one.

One gain across the whole set, from ReBirth's loudest window.

    python listen_song.py --out ../../../listen-calm
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

SONG = os.path.join(WS, "fixtures-2026-08-27", "wholemix", "Calm.rbs")
RBEXPORT = os.path.join(WS, "renders", "nofxexports", "Calm.WAV")

# label, start seconds, length, what is actually playing
WINDOWS = [
    ("00 Calm 12-40s, bass 1 ALONE, rack off", 12.0, 28.0,
     "one 303 in real music. no drums are playing, verified on ReBirth's own "
     "export: 0.00 percent of its energy is above 4 kHz here"),
    ("01 Calm 60-88s, the whole mix, rack off", 60.0, 28.0,
     "303s and both drum machines. CONFOUNDED by our 808 and 909, which are "
     "their own supertasks, but not by any effect"),
]

# The voice as it sounded at the START of 2026-08-29, for the third file.
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
    a = ap.parse_args(argv)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)

    need = max(s + l for _, s, l, _ in WINDOWS) + 2.0
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    envb = dict(env)
    for k, v in BEFORE.items():
        envb["REBORN_" + k] = v

    tmp = {}
    for tag, e in (("now", env), ("was", envb)):
        p = os.path.join(out, "_%s.wav" % tag)
        subprocess.run([EXE, "--song", SONG, "--no-fx", "--seconds", "%g" % need,
                        "--out", p], check=True, env=e, stdout=subprocess.DEVNULL)
        tmp[tag] = p

    xr, sr = read(RBEXPORT)
    xn, _ = read(tmp["now"])
    xw, _ = read(tmp["was"])

    peak = 0.0
    cuts = []
    for label, s, l, why in WINDOWS:
        i, j = int(s * sr), int((s + l) * sr)
        a_ = xr[i:j]
        b_ = xn[i:j] if j <= len(xn) else xn[i:]
        c_ = xw[i:j] if j <= len(xw) else xw[i:]
        n = min(len(a_), len(b_), len(c_))
        peak = max(peak, float(np.abs(a_[:n]).max()))
        cuts.append((label, a_[:n], c_[:n], b_[:n], why))

    g = 0.89 / max(peak, 1e-9)
    print("one gain for the set: %.3f, from ReBirth's loudest window" % g)
    for label, a_, c_, b_, why in cuts:
        write(os.path.join(out, label + " - 1 REBIRTH.wav"), a_ * g, sr)
        write(os.path.join(out, label + " - 2 OURS BEFORE.wav"), c_ * g, sr)
        write(os.path.join(out, label + " - 3 OURS NOW.wav"), b_ * g, sr)
        print("  %s\n      %s" % (label, why))
    for p in tmp.values():
        os.remove(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
