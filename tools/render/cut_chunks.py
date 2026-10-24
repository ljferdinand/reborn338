#!/usr/bin/env python3
"""Short 3-way listening chunks for quick A/B: ReBirth / ours shipped / candidate.

Long song files are hard to hold in the ear across a 30 to 60 second comparison,
so this cuts short named windows (5 to 10 s) of a song, three ways, level-matched,
for fast back-and-forth. Same convention as the full-song cutter: ONE gain across
all the windows of a call, from the loudest ReBirth window, so levels stay real;
ours rendered REBORN_BASS_ENGINE=tpt, rack off.

WINDOWS are "start:length:label" triples, seconds. Tour section starts (130 BPM,
1.846 s/bar): riff 0, cutoff 7.4, reso 14.8, envmod 22.2, decay 29.5, accent 36.9,
octaves 44.3, all-up 51.7.

USAGE
  python cut_chunks.py --song t303_tour_saw --after "REBORN_FC_MAX=9000 REBORN_EM_CENTRE=0.45" \
      --windows "22.2:7:envmod,36.9:7:accent,51.7:7:allup" --out ../../../listen-chunks
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


def read(p):
    with wave.open(p, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def write(p, x, sr):
    x = np.clip(x, -1.0, 1.0)
    with wave.open(p, "wb") as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((x * 32767.0).astype("<i2").tobytes())


def render(stem, secs, ov, tag):
    env = dict(os.environ); env["REBORN_BASS_ENGINE"] = "tpt"
    for kv in ov.split():
        if "=" in kv:
            k, v = kv.split("=", 1); env[k] = v
    out = os.path.join(os.environ["_CK"], f"_{stem}_{tag}.wav")
    subprocess.run([EXE, "--song", os.path.join(FX, stem + ".rbs"), "--no-fx",
                    "--seconds", "%g" % secs, "--out", out], check=True, env=env,
                   stdout=subprocess.DEVNULL)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--song", required=True)
    ap.add_argument("--after", required=True)
    ap.add_argument("--windows", required=True, help="start:len:label,...")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    out = os.path.abspath(a.out); os.makedirs(out, exist_ok=True)
    os.environ["_CK"] = out
    wins = []
    for w in a.windows.split(","):
        s, l, lab = w.split(":")
        wins.append((float(s), float(l), lab))
    need = max(s + l for s, l, _ in wins) + 1.0

    xr, sr = read(os.path.join(RB, a.song + ".wav"))
    xs, _ = read(render(a.song, need, "", "s"))
    xc, _ = read(render(a.song, need, a.after, "c"))

    peak = 0.0
    cut = []
    for s, l, lab in wins:
        i, j = int(s * sr), int((s + l) * sr)
        a_ = xr[i:j]; b_ = xs[i:j]; c_ = xc[i:j]
        n = min(len(a_), len(b_), len(c_))
        peak = max(peak, float(np.abs(a_[:n]).max()))
        cut.append((lab, a_[:n], b_[:n], c_[:n]))
    g = 0.89 / max(peak, 1e-9)
    print(f"{a.song}: one gain {g:.3f}, {len(cut)} windows")
    for lab, a_, b_, c_ in cut:
        write(os.path.join(out, f"{lab} - 1 REBIRTH.wav"), a_ * g, sr)
        write(os.path.join(out, f"{lab} - 2 OURS shipped.wav"), b_ * g, sr)
        write(os.path.join(out, f"{lab} - 3 OURS candidate.wav"), c_ * g, sr)
        print(f"  {lab}  ({len(a_)/sr:.1f}s)")
    for f in os.listdir(out):
        if f.startswith("_") and f.endswith(".wav"):
            os.remove(os.path.join(out, f))


if __name__ == "__main__":
    main()
