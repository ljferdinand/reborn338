#!/usr/bin/env python3
"""Read the compressor ride probe (fixtures-2026-10-01/comp-rides).

Each fixture is a steady 303 drone through the master compressor with at most one
COMP ride, at tick 64 (4.0 s at 120 BPM). For each file this reports, per channel
(L and R measured separately, never mono-summed):

  before   RMS dB over 1.0 to 3.9 s (the opening setting, settled)
  after    RMS dB over 4.5 to 7.9 s (the ridden setting, settled)
  step     after - before
  t90      ms after 4.0 s until a 10 ms RMS envelope has covered 90 percent of
           the step in dB (blank when the step is under 1 dB)

The same script reads ReBirth's exports and ours, so the method is identical on
both sides.

    python comp_rides.py <rebirth-dir> [<ours-dir>]
"""
import glob
import os
import sys
import wave

import numpy as np

RIDE_S = 4.0
BEFORE = (1.0, 3.9)
AFTER = (4.5, 7.9)
ENV_WIN_S = 0.010


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return sr, x.reshape(-1, ch)


def rms_db(x):
    v = float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0
    return -999.0 if v <= 0 else 20.0 * np.log10(v)


def seg(sr, x, a, b):
    return x[int(a * sr):int(b * sr)]


def t90(sr, x, before, after):
    step = after - before
    if abs(step) < 1.0:
        return None
    hop = int(ENV_WIN_S * sr)
    i = int(RIDE_S * sr)
    while i + hop <= min(len(x), int(AFTER[0] * sr)):
        if (rms_db(x[i:i + hop]) - before) / step >= 0.9:
            return (i + hop / 2) / sr * 1000.0 - RIDE_S * 1000.0
        i += hop
    return None


def read(path):
    sr, x = load(path)
    out = []
    for c in range(x.shape[1]):
        b = rms_db(seg(sr, x[:, c], *BEFORE))
        a = rms_db(seg(sr, x[:, c], *AFTER))
        out.append((b, a, a - b, t90(sr, x[:, c], b, a)))
    return out


def fmt(row):
    b, a, s, t = row
    ts = "   -  " if t is None else "%6.0f" % t
    return "%7.2f %7.2f %+7.2f %s" % (b, a, s, ts)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    dirs = argv[:2]
    names = sorted(os.path.basename(p) for p in glob.glob(os.path.join(dirs[0], "*.wav")))
    hdr = "  before   after    step   t90ms"
    print("%-18s %-4s %s" % ("fixture", "ch", "   ".join(
        ["%-33s" % ("ReBirth" if i == 0 else "ours") + "" for i in range(len(dirs))])))
    print("%-18s %-4s %s" % ("", "", "   ".join([hdr] * len(dirs))))
    for n in names:
        rows = []
        for d in dirs:
            p = os.path.join(d, n)
            rows.append(read(p) if os.path.exists(p) else None)
        for c, cn in enumerate(("L", "R")):
            cells = [fmt(r[c]) if r else "%33s" % "missing" for r in rows]
            print("%-18s %-4s %s" % (n[:-4] if c == 0 else "", cn, "   ".join(cells)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
