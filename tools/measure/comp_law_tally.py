#!/usr/bin/env python3
"""Tally the compressor's static campaigns through comp_wav, cell by cell.

For every compressed ReBirth export in the comp-law campaigns (names carrying
`rRRR_tTTT`, the fader in `_vNNN`, default 127, or 60 for the `bt_` cells), feed
ReBirth's own uncompressed export at the same fader (`off_vNNN.wav` in the same
directory) through our compressor with comp_wav, and compare levels over 1.0 to
7.9 s. comp_wav is exact for the master path (the engine agrees with it within
0.031 dB on all 152 cells, 2026-10-01), and feeding ReBirth's own source removes
every source difference, so the miss is the compressor's alone.

    miss = level(comp_wav(ReBirth off)) - level(ReBirth on)    per channel, dB

With --baseline, the same tally for a second comp_wav build, and every cell whose
worst-channel |miss| grew by more than --worse dB is listed. Round 2 of the
compressor (the 2026-09-17 sign-off) reaches 129 of 152 cells within 1.0 dB.

    python comp_law_tally.py [--comp-wav EXE] [--baseline EXE] [--cells]

The exports are ReBirth's, made from your own copy of ReBirth, and are not in
this repository. They are read from `renders/<campaign>-2026-09-16/` under
REBORN_WORKSPACE (default: the directory that contains this checkout, the same
rule as tools/rbsgen/rbsgen.py's workspace_path).
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
# ReBirth's exports live in the workspace, outside git (see the docstring).
WORKSPACE = os.environ.get("REBORN_WORKSPACE") or os.path.abspath(
    os.path.join(HERE, "..", "..", ".."))
RENDERS = os.path.join(WORKSPACE, "renders")
CAMPAIGNS = ["comp-law", "comp-law2", "comp-law3", "comp-law4", "comp-law5", "comp-law6"]
WIN = (1.0, 7.9)


def levels(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)[int(WIN[0] * sr):int(WIN[1] * sr)]
    return [20.0 * np.log10(np.sqrt(np.mean(x[:, c] ** 2)) + 1e-12) for c in range(ch)]


def tally(exe, tmp, tag):
    out = {}
    for d in CAMPAIGNS:
        rb_dir = os.path.join(RENDERS, d + "-2026-09-16")
        for p in sorted(glob.glob(os.path.join(rb_dir, "*.wav"))):
            name = os.path.basename(p)[:-4]
            m = re.search(r"r(\d+)_t(\d+)", name)
            if not m:
                continue
            r, t = int(m.group(1)), int(m.group(2))
            mv = re.search(r"_v(\d+)", name)
            v = mv.group(1) if mv else ("060" if name.startswith("bt") else "127")
            off = os.path.join(rb_dir, f"off_v{v}.wav")
            if not os.path.exists(off):
                print(f"  no uncompressed reference for {d}/{name}", file=sys.stderr)
                continue
            cw = os.path.join(tmp, f"{tag}_{d}_{name}.wav")
            subprocess.run([exe, off, cw, "%.6f" % (r / 127.0), "%.6f" % (t / 127.0)],
                           check=True, stdout=subprocess.DEVNULL)
            out[(d, name)] = [o - b for o, b in zip(levels(cw), levels(p))]
    return out


def summary(res, label):
    worst = {k: max(abs(v) for v in m) for k, m in res.items()}
    print(f"== {label}")
    for d in CAMPAIGNS:
        cells = [w for (dd, _), w in worst.items() if dd == d]
        if cells:
            print(f"   {d:10s} n={len(cells):3d}  median |miss| {np.median(cells):.2f}  "
                  f"max {max(cells):.2f}  over 1 dB: {sum(c > 1.0 for c in cells)}")
    within = sum(w <= 1.0 for w in worst.values())
    print(f"   all: {within} of {len(worst)} cells within 1.0 dB")
    return worst


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--comp-wav", default=os.path.join(HERE, "..", "..", "build",
                                                       "Release", "comp_wav.exe"))
    ap.add_argument("--baseline", default=None, help="a second comp_wav build to compare")
    ap.add_argument("--worse", type=float, default=0.3)
    ap.add_argument("--cells", action="store_true", help="print every cell")
    a = ap.parse_args(argv)
    if not os.path.isdir(RENDERS):
        sys.exit(f"no ReBirth exports at {RENDERS}: set REBORN_WORKSPACE to the "
                 "directory that holds renders/")
    tmp = tempfile.mkdtemp(prefix="comp_law_tally_")
    res = tally(a.comp_wav, tmp, "head")
    worst = summary(res, a.comp_wav)
    if a.cells:
        for (d, n), m in sorted(res.items()):
            print(f"   {d:10s} {n:22s} " + " ".join(f"{v:+6.2f}" for v in m))
    if a.baseline:
        base = tally(a.baseline, tmp, "base")
        bworst = summary(base, a.baseline)
        grew = sorted(((worst[k] - bworst[k], k) for k in worst if k in bworst), reverse=True)
        bad = [(g, k) for g, k in grew if g > a.worse]
        print(f"== cells worse than the baseline by more than {a.worse} dB: {len(bad)}")
        for g, (d, n) in bad:
            print(f"   {d:10s} {n:22s} |miss| {bworst[(d, n)]:.2f} -> {worst[(d, n)]:.2f} (+{g:.2f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
