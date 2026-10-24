#!/usr/bin/env python3
"""Cut a listening set: ReBirth against us, same window, one gain per set.

WHICH DRUMS. Without `--samples` this renders our own drum synthesis, which is
what we ship and the right thing to judge the product by. With it, both sides
play ReBirth's own recordings, which is the right thing to judge an EFFECT by:
it takes our synthesis out of the comparison so an ear hears the filter and not
the voices. Luke asked for exactly that on 2026-08-28 and it was the step that
made the dull-drums isolation clean. Cut both when the question is an effect.

Excerpts are the point. A whole song hides a defect in one section behind three
others, and a 45 second window on the part that exercises the thing under test
does not. Each set shares ONE gain, taken from ReBirth's peak in that window, so
level differences between the files are real and not normalised away.

    python listen_pcf.py --out ../../../listen-pcf-2
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

# song, start seconds, what it exercises
CUTS = [
    ("Metallicon City", 241.0, "the PCF on the 808"),
    ("G-Spot",           51.0, "the PCF on the 909"),
    ("Calm",            211.0, "where the 303 vanished, 2026-08-28"),
    ("SuperNova",        21.0, "the PCF on bass 1, in bandpass"),
]
LENGTH = 45.0


def read(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def write(path, x, sr):
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes((np.clip(x, -1.0, 1.0) * 32767.0).astype("<i2").tobytes())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--ref-dir",
                    default=os.path.join(WS, "rb338-exports", "wholemix-2026-08-27"))
    ap.add_argument("--song-dir",
                    default=os.path.join(WS, "fixtures-2026-08-27", "wholemix"))
    ap.add_argument("--label", default="now")
    ap.add_argument("--samples",
                    help="drum sample set. One directory serves BOTH machines: "
                         "the loader looks for 808_* and 909_* names inside it, "
                         "so copy the two sets together. Use this when the "
                         "question is about an effect rather than about the "
                         "drum voices, since it takes our synthesis out of the "
                         "comparison entirely.")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)

    for i, (song, start, why) in enumerate(CUTS, 1):
        ref = os.path.join(a.ref_dir, song + ".wav")
        if not os.path.exists(ref):
            print("missing %s" % ref)
            continue
        xr, sr = read(ref)
        tmp = os.path.join(a.out, "_tmp.wav")
        cmd = [EXE, "--song", os.path.join(a.song_dir, song + ".rbs"),
               "--seconds", "%.3f" % (len(xr) / sr), "--out", tmp]
        if a.samples:
            cmd += ["--samples", a.samples]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        xo, _ = read(tmp)

        s, e = int(start * sr), int((start + LENGTH) * sr)
        cr, co = xr[s:e], xo[s:min(e, len(xo))]
        g = 0.89 / max(1e-9, np.abs(cr).max())     # one gain for the set
        tag = song.replace(" ", "")
        write(os.path.join(a.out, "%02da_%s_REBIRTH.wav" % (i, tag)), cr * g, sr)
        write(os.path.join(a.out, "%02db_%s_REBORN_%s.wav" % (i, tag, a.label)),
              co * g, sr)
        os.remove(tmp)
        print("  %02d %-16s %s" % (i, song, why))
    return 0


if __name__ == "__main__":
    sys.exit(main())
