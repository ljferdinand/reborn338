#!/usr/bin/env python3
"""Per-bar RMS of a whole song, ours against ReBirth's export, L and R separately.

The compressor's whole-song check (PARITY.md, "The four largest movers checked
against ReBirth", 2026-10-01) was scored this way by hand; this is that method as
a script. A bar is four beats at the song's own tempo (GLOB +2), counted from the
start of the file, which is where both an export and reborn_render start. Each
bar's RMS is taken per channel, in dB, and the difference is ours minus ReBirth.
Bars where ReBirth is under -60 dBFS in a channel are skipped for that channel,
so silence does not count as agreement or as error.

    python perbar.py <song.rbs> <rebirth.wav> <ours.wav> [--bars 73-79] [--bars ...]

Prints the mean and worst |difference| over the whole song and over each range
given, and the number of bars more than 3 and 6 dB out.
"""
import argparse
import math
import struct
import sys
import wave

import numpy as np


def tempo(path):
    """Tempo in BPM from the song's GLOB chunk (u32 BE at +2, BPM x 1000)."""
    b = open(path, "rb").read()
    j = b.find(b"GLOB")
    if j < 0:
        sys.exit("no GLOB chunk in %s" % path)
    return struct.unpack(">I", b[j + 8 + 2:j + 8 + 6])[0] / 1000.0


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def bars_db(x, sr, bpm):
    n = 4 * 60.0 / bpm * sr
    count = int(len(x) // n)
    out = np.zeros((count, x.shape[1]))
    for k in range(count):
        seg = x[int(round(k * n)):int(round((k + 1) * n))]
        out[k] = 10 * np.log10(np.mean(seg ** 2, axis=0) + 1e-20)
    return out


def summary(d, label):
    v = np.abs(d[~np.isnan(d)])
    if not len(v):
        return "%-14s no bars" % label
    return ("%-14s mean |diff| %5.2f dB, worst %6.2f, over 3 dB %3d, over 6 dB %3d (of %d bar-channels)"
            % (label, v.mean(), v.max(), int((v > 3).sum()), int((v > 6).sum()), len(v)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song")
    ap.add_argument("rebirth")
    ap.add_argument("ours")
    ap.add_argument("--bars", action="append", default=[], help="1-based inclusive range, e.g. 73-79")
    a = ap.parse_args(argv)
    bpm = tempo(a.song)
    r, sr = load(a.rebirth)
    o, sr2 = load(a.ours)
    if sr != sr2 or r.shape[1] != 2 or o.shape[1] != 2:
        sys.exit("both files must be stereo at one rate")
    rb, ob = bars_db(r, sr, bpm), bars_db(o, sr, bpm)
    n = min(len(rb), len(ob))
    d = ob[:n] - rb[:n]
    d[rb[:n] < -60.0] = np.nan
    print("%s: %.3f BPM, %d bars" % (a.song, bpm, n))
    for c, name in enumerate("LR"):
        print(summary(d[:, c], "whole song %s" % name))
    for spec in a.bars:
        lo, hi = (int(v) for v in spec.split("-"))
        seg = d[lo - 1:hi]
        for c, name in enumerate("LR"):
            print(summary(seg[:, c], "bars %s %s" % (spec, name)))
        means = np.nanmean(seg, axis=0)
        print("%-14s signed mean L %+.2f, R %+.2f dB (ours minus ReBirth)" % ("", means[0], means[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
