#!/usr/bin/env python3
"""Three-way listening set for the env-mod corner candidate (section 20).

ReBirth / ours shipped / ours candidate, on the four 303 listening songs. The
candidate is FC_MAX=9000 with EM_CENTRE=0.40, which opens the env-mod corner to
ReBirth's reach and more than halves the tour band-shape error while the static
net rejects it. This is the listen that decides whether the brighter corner
sounds closer to ReBirth and whether the raised ceiling whistles.

Convention from listen_303.py: ONE GAIN for the whole set from ReBirth's loudest
file, so level differences are real and not normalised away. Ours rendered with
REBORN_BASS_ENGINE=tpt. The groove (static patch) is first, as the calibration:
compare REBIRTH against OURS SHIPPED to hear the current gap, then OURS CANDIDATE
to hear whether it closes.

    python cut_303_emcorner.py --out ../../../listen-303-emcorner-2026-09-03
"""
import argparse
import os
import subprocess
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")
RB = os.path.join(WS, "renders", "303-songs-2026-08-30")
FX = os.path.join(WS, "fixtures-2026-08-30", "303-songs")

CAND = {"REBORN_FC_MAX": "9000", "REBORN_EM_CENTRE": "0.45"}

# stem, seconds, label (groove first: static patch, the calibration)
SONGS = [
    ("t303_groove_saw",    30.0, "00 groove SAW  (static patch, the calibration)"),
    ("t303_groove_square", 30.0, "01 groove SQUARE"),
    ("t303_tour_saw",      60.0, "02 tour SAW  (knob sweeps, where the fix bites)"),
    ("t303_tour_square",   60.0, "03 tour SQUARE"),
]


def read(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def write(path, x, sr):
    x = np.clip(x, -1.0, 1.0)
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((x * 32767.0).astype("<i2").tobytes())


def render(stem, secs, overrides):
    env = dict(os.environ); env["REBORN_BASS_ENGINE"] = "tpt"
    env.update(overrides)
    out = os.path.join(os.environ["_TMPD"], stem + ("_c" if overrides else "_s") + ".wav")
    subprocess.run([EXE, "--song", os.path.join(FX, stem + ".rbs"), "--no-fx",
                    "--seconds", "%g" % secs, "--out", out],
                   check=True, env=env, stdout=subprocess.DEVNULL)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    os.environ["_TMPD"] = out

    rows, peak, tmps = [], 0.0, []
    for stem, secs, label in SONGS:
        rbp = os.path.join(RB, stem + ".wav")
        if not (os.path.exists(rbp) and os.path.exists(os.path.join(FX, stem + ".rbs"))):
            print("missing, skipped:", stem); continue
        ps = render(stem, secs, {}); pc = render(stem, secs, CAND)
        tmps += [ps, pc]
        xr, sr = read(rbp)
        n = int(secs * sr); n = min(n, len(xr))
        xs = read(ps)[0]; xc = read(pc)[0]
        n = min(n, len(xs), len(xc))
        peak = max(peak, float(np.abs(xr[:n]).max()))
        rows.append((label, xr[:n], xs[:n], xc[:n], sr))

    g = 0.89 / max(peak, 1e-9)
    print("one gain for the set: %.3f, from ReBirth's loudest song" % g)
    for label, xr, xs, xc, sr in rows:
        write(os.path.join(out, label + " - 1 REBIRTH.wav"), xr * g, sr)
        write(os.path.join(out, label + " - 2 OURS shipped.wav"), xs * g, sr)
        write(os.path.join(out, label + " - 3 OURS candidate (open corner).wav"), xc * g, sr)
        print(" ", label)
    for t in tmps:
        try: os.remove(t)
        except OSError: pass


if __name__ == "__main__":
    main()
