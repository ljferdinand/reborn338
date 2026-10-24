#!/usr/bin/env python3
"""Read the compressor material probe (fixtures-2026-10-01/comp-material).

For every source it needs ReBirth's exports only. ReBirth's uncompressed export
(`cm_<src>_off.wav`) is fed through OUR compressor with comp_wav, which is exact
for the master path (the engine agrees with it within 0.031 dB on all 152
comp-law cells), so the source is identical on both sides by construction. Then,
per setting, per channel (L and R are read separately; comp_wav feeds L = R from
the mono mix of a centred source, which is the same signal):

  RB GR    ReBirth's compressed level minus its uncompressed level, dB
  our GR   comp_wav's output level minus the same uncompressed level, dB
  miss     our GR - RB GR (negative: we cut too much)

over the steady window 2.0 to 6.0 s (bars 2 and 3 at 120 BPM). Also printed per
source: the uncompressed level, its crest factor (peak / RMS) and its form
factor (RMS / mean |x|), which is what separates an RMS, an average and a peak
detector on steady material.

    python comp_material.py <rebirth-dir> [--prefix cm] [--comp-wav EXE] [--scratch DIR]

Sources and settings are read from the file names, `<prefix>_<source>_off.wav`
and `<prefix>_<source>_rRRR_tTTT.wav`, so the same reader serves any set built
that way (the material probe is `cm`, the hold-out check `ch`).
"""
import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WIN = (2.0, 6.0)
ORDER = ["saw", "square", "reso", "dull", "stacc", "kick", "hats"]   # the cm set's own order


def discover(rb_dir, prefix):
    """{source: [(tag, ratio, threshold), ...]} from the export names."""
    found = {}
    for p in glob.glob(os.path.join(rb_dir, f"{prefix}_*_off.wav")):
        src = os.path.basename(p)[len(prefix) + 1:-len("_off.wav")]
        cells = []
        for q in glob.glob(os.path.join(rb_dir, f"{prefix}_{src}_r*_t*.wav")):
            m = re.fullmatch(rf"{re.escape(prefix)}_{re.escape(src)}_(r(\d+)_t(\d+))\.wav",
                             os.path.basename(q))
            if m:
                cells.append((m.group(1), int(m.group(2)), int(m.group(3))))
        found[src] = sorted(cells, key=lambda c: (-c[1], c[2]))
    return dict(sorted(found.items(),
                       key=lambda kv: (ORDER.index(kv[0]) if kv[0] in ORDER else 99, kv[0])))


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return sr, x.reshape(-1, ch)


def seg(path):
    sr, x = load(path)
    return x[int(WIN[0] * sr):int(WIN[1] * sr)]


def db(v):
    return -999.0 if v <= 0 else 20.0 * np.log10(v)


def rms_db(x):
    return [db(float(np.sqrt(np.mean(x[:, c] ** 2)))) for c in range(x.shape[1])]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("rebirth")
    ap.add_argument("--prefix", default="cm")
    ap.add_argument("--comp-wav", default=os.path.join(HERE, "..", "..", "build",
                                                       "Release", "comp_wav.exe"))
    ap.add_argument("--scratch", default=None)
    a = ap.parse_args(argv)
    tmp = a.scratch or tempfile.mkdtemp(prefix="comp_material_")
    os.makedirs(tmp, exist_ok=True)

    print("source  uncompressed L/R dBFS   crest  form   (window %.1f-%.1f s)" % WIN)
    rows = []
    for src, settings in discover(a.rebirth, a.prefix).items():
        off = os.path.join(a.rebirth, f"{a.prefix}_{src}_off.wav")
        x = seg(off)
        lv = rms_db(x)
        m = x.mean(axis=1)
        rms = float(np.sqrt(np.mean(m ** 2)))
        crest = float(np.max(np.abs(m))) / rms if rms > 0 else 0.0
        form = rms / float(np.mean(np.abs(m))) if rms > 0 else 0.0
        print(f"{src:7s} {lv[0]:7.2f} {lv[1]:7.2f}        {crest:5.2f}  {form:5.3f}")
        for tag, r, t in settings:
            on = os.path.join(a.rebirth, f"{a.prefix}_{src}_{tag}.wav")
            ours = os.path.join(tmp, f"cw_{src}_{tag}.wav")
            subprocess.run([a.comp_wav, off, ours, "%.6f" % (r / 127.0), "%.6f" % (t / 127.0)],
                           check=True, stdout=subprocess.DEVNULL)
            rb_gr = [o - u for o, u in zip(rms_db(seg(on)), lv)]
            our_gr = [o - u for o, u in zip(rms_db(seg(ours)), lv)]
            rows.append((src, tag, rb_gr, our_gr))
    print()
    print("source  setting      RB GR L/R        our GR L/R       miss L/R")
    for src, tag, rb, ou in rows:
        print(f"{src:7s} {tag:10s}  {rb[0]:+7.2f} {rb[1]:+7.2f}   {ou[0]:+7.2f} {ou[1]:+7.2f}   "
              f"{ou[0]-rb[0]:+6.2f} {ou[1]-rb[1]:+6.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
