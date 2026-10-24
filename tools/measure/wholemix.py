#!/usr/bin/env python3
"""Score our renders of whole demo songs against ReBirth's exports of the same.

The per-component fixtures answer "is this law right". This answers "does a real
song come out the same", which is a different question and the one an ear asks.
Six songs, chosen to put the PCF on each kind of section plus one control that
does not use it.

Three numbers per song:

  * LEVEL, full-mix RMS difference in dB.
  * OCTAVE ERROR, each file's band energies normalised to its OWN total before
    differencing, so a level offset cannot masquerade as a spectral error.
  * SKEW, left RMS minus right RMS, reported for both sides. Ours against
    ReBirth's, never mono-summed.

    python wholemix.py <ref-dir> <ours-dir>
    python wholemix.py <ref-dir> <ours-dir> --render <song-dir> [--samples DIR]

With --render it renders each song first, for the duration of the matching
reference export, which is what keeps the two aligned: ReBirth's export covers
the transport LOOP and the songs in the fixture directory have had theirs
widened to the whole arrangement.
"""
import argparse
import math
import os
import subprocess
import sys
import wave

import numpy as np

CENTRES = [44.0, 88.0, 176.0, 353.0, 707.0, 1414.0, 2828.0, 5656.0, 11313.0]
N = 4096


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def db(v):
    return 20.0 * math.log10(max(float(v), 1e-20))


def octaves(x, sr):
    """Total energy per octave band over the whole file, power-summed L and R."""
    win = np.hanning(N)
    f = np.fft.rfftfreq(N, 1.0 / sr)
    masks = [(f >= c / math.sqrt(2.0)) & (f < c * math.sqrt(2.0)) for c in CENTRES]
    acc = np.zeros(len(CENTRES))
    for i in range(0, len(x) - N, N // 2):
        seg = x[i:i + N, :]
        p = np.zeros(len(f))
        for c in range(seg.shape[1]):
            p += np.abs(np.fft.rfft(seg[:, c] * win)) ** 2
        for b, m in enumerate(masks):
            acc[b] += p[m].sum()
    return acc


def skew(x):
    if x.shape[1] < 2:
        return 0.0
    return db(np.sqrt((x[:, 0] ** 2).mean())) - db(np.sqrt((x[:, 1] ** 2).mean()))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--render", metavar="SONG_DIR",
                    help="render each song into ours_dir first")
    ap.add_argument("--samples", help="sample set for the drum machines")
    ap.add_argument("--exe", default="build/Release/reborn_render.exe")
    a = ap.parse_args(argv)

    refs = sorted(f for f in os.listdir(a.ref_dir) if f.lower().endswith(".wav"))
    if a.render:
        os.makedirs(a.ours_dir, exist_ok=True)
        for f in refs:
            with wave.open(os.path.join(a.ref_dir, f), "rb") as w:
                secs = w.getnframes() / float(w.getframerate())
            song = os.path.join(a.render, os.path.splitext(f)[0] + ".rbs")
            cmd = [a.exe, "--song", song, "--seconds", "%.3f" % secs,
                   "--out", os.path.join(a.ours_dir, f)]
            if a.samples:
                cmd += ["--samples", a.samples]
            subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

    print("%-20s %9s %9s %9s %9s" % ("song", "level dB", "octave dB",
                                     "skew ours", "skew ReB"))
    lv, oc = [], []
    for f in refs:
        r, sr = load(os.path.join(a.ref_dir, f))
        o, _ = load(os.path.join(a.ours_dir, f))
        n = min(len(r), len(o))
        r, o = r[:n], o[:n]
        dlevel = db(np.sqrt((o ** 2).mean())) - db(np.sqrt((r ** 2).mean()))
        br, bo = octaves(r, sr), octaves(o, sr)
        e = 10 * np.log10((bo / bo.sum()) / (br / br.sum()))
        lv.append(abs(dlevel))
        oc.append(np.abs(e).mean())
        print("%-20s %9.2f %9.2f %9.2f %9.2f"
              % (os.path.splitext(f)[0], dlevel, np.abs(e).mean(),
                 skew(o), skew(r)))
    print("\nmean absolute level error  %.2f dB" % np.mean(lv))
    print("mean absolute octave error %.2f dB" % np.mean(oc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
