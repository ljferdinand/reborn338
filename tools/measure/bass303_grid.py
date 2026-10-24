#!/usr/bin/env python3
"""Batch B5: the 303 filter on a pooled, dense frequency grid.

Six pitches a whole tone apart, each a held drone that slides on every step so
the instrument never retriggers and the filter is static. Six harmonic series
interleave, and their union is dense exactly where a single series is sparsest,
which is below the corner where every passband-relative statistic in this
project has failed.

THREE THINGS ARE REPORTED AND THEY ANSWER DIFFERENT QUESTIONS.

  1. **The reference check.** `b5_ref_open` over `b5_ref_flat`, per side. This
     says how much of the audio band B1's old denominator was quietly filtering.
     If the two references differ by a few dB anywhere the measurement cares
     about, every B1 number in that region carried that difference.

  2. **The absolute response**, fixture over `b5_ref_open`. Only as good as the
     open reference is flat, which is why 1 comes first.

  3. **What resonance does**, fixture over the SAME CUTOFF at resonance 0, per
     side. This needs no reference at all: the oscillator, the VCA, the output
     stage and any shared reference filter all cancel, and the numerator differs
     from the denominator in exactly one knob. When 2 and 3 disagree, 3 is the
     one to believe.

The open reference is read EARLY, from 50 to 300 ms, because its corner is held
up by an envelope that is decaying. The cells are read in steady state.

    python bass303_grid.py <rebirth-dir> <ours-dir>
"""
import argparse
import os
import sys
import wave

import numpy as np

NOTES = (0, 2, 4, 6, 8, 10)
CELLS = ("c064_r000", "c064_r064", "c064_r096", "c064_r127",
         "c032_r096", "c112_r096")
NPERSEG = 8192
REF_WINDOW = (0.05, 0.30)      # the open reference, while its envelope is open
CELL_WINDOW = (0.60, 5.50)     # the cells, in steady state


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def psd(path, window):
    x, sr = load(path)
    a, b = int(window[0] * sr), int(window[1] * sr)
    x = x[a:min(b, len(x))]
    h = np.hanning(NPERSEG)
    acc, k = np.zeros(NPERSEG // 2 + 1), 0
    for s in range(0, len(x) - NPERSEG, NPERSEG // 4):
        seg = x[s:s + NPERSEG, 0] + x[s:s + NPERSEG, 1]
        acc += np.abs(np.fft.rfft(seg * h)) ** 2
        k += 1
    if k == 0:
        return None, None
    return acc / k, np.fft.rfftfreq(NPERSEG, 1.0 / sr)


def partials(p, f, lo=40.0, hi=12000.0, n=64):
    """The drone's own partials, as local maxima of its spectrum."""
    m = np.where((f >= lo) & (f <= hi))[0]
    pk = [i for i in m[1:-1] if p[i] > p[i - 1] and p[i] > p[i + 1]]
    pk.sort(key=lambda i: -p[i])
    return sorted(pk[:n])


def window_for(stem):
    return REF_WINDOW if stem.startswith("ref_open") else CELL_WINDOW


def pooled(d, stem, ref_stem):
    """(freqs, dB) for one cell, pooled over every pitch, against `ref_stem`.

    THE PARTIAL GRID ALWAYS COMES FROM `ref_open`, never from whichever file is
    acting as the denominator. A denominator that is itself heavily filtered has
    its loudest peaks all at the bottom, so picking the grid off it would sample
    the response where it is already known and skip where it is not. The grid is
    a property of the SOURCE, and `ref_open` is the closest thing to the source
    on its own that this instrument can be made to produce.
    """
    fs, ds = [], []
    for n in NOTES:
        a = os.path.join(d, "b5_%s_n%02d.wav" % (stem, n))
        b = os.path.join(d, "b5_%s_n%02d.wav" % (ref_stem, n))
        g = os.path.join(d, "b5_ref_open_n%02d.wav" % n)
        if not (os.path.exists(a) and os.path.exists(b) and os.path.exists(g)):
            continue
        pa, f = psd(a, window_for(stem))
        pb, _ = psd(b, window_for(ref_stem))
        pg, _ = psd(g, REF_WINDOW)
        if pa is None or pb is None or pg is None:
            continue
        idx = partials(pg, f)
        fs.append(f[idx])
        ds.append(10 * np.log10((pa[idx] + 1e-30) / (pb[idx] + 1e-30)))
    if not fs:
        return None, None
    f = np.concatenate(fs)
    d_ = np.concatenate(ds)
    o = np.argsort(f)
    return f[o], d_[o]


def binned(f, d, edges):
    out = np.full(len(edges) - 1, np.nan)
    for i in range(len(edges) - 1):
        m = (f >= edges[i]) & (f < edges[i + 1])
        if m.sum():
            out[i] = float(np.median(d[m]))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    a = ap.parse_args(argv)

    edges = 50.0 * 2.0 ** (np.arange(0, 25) / 3.0)     # third octaves, 50 Hz up
    ctr = np.sqrt(edges[:-1] * edges[1:])
    show = list(range(0, len(ctr), 2))
    head = " ".join("%6.0f" % ctr[i] for i in show)

    print("1. THE REFERENCE CHECK: b5_ref_open over b5_ref_flat, per side.")
    print("   How much of the band B1's denominator was filtering. dB.")
    print("%-10s %s" % ("Hz", head))
    for lbl, d in (("ReBirth", a.ref_dir), ("ours", a.ours_dir)):
        f, v = pooled(d, "ref_open", "ref_flat")
        if f is None:
            print("%-10s (missing)" % lbl); continue
        b = binned(f, v, edges)
        print("%-10s %s" % (lbl, " ".join("%6.1f" % b[i] for i in show)))

    print("\n2. ABSOLUTE RESPONSE over the OPEN reference, ours minus ReBirth, dB")
    print("%-12s %s" % ("cell", head))
    for c in CELLS:
        fa, va = pooled(a.ref_dir, c, "ref_open")
        fb, vb = pooled(a.ours_dir, c, "ref_open")
        if fa is None or fb is None:
            continue
        d = binned(fb, vb, edges) - binned(fa, va, edges)
        print("%-12s %s" % (c, " ".join("%6.1f" % d[i] for i in show)))

    print("\n3. WHAT RESONANCE DOES, cell over the same cutoff at resonance 0.")
    print("   No reference at all. ours minus ReBirth, dB")
    print("%-12s %s" % ("cell", head))
    for c in CELLS:
        base = c.split("_")[0] + "_r000"
        if base == c:
            continue
        fa, va = pooled(a.ref_dir, c, base)
        fb, vb = pooled(a.ours_dir, c, base)
        if fa is None or fb is None:
            print("%-12s (needs %s)" % (c, base)); continue
        d = binned(fb, vb, edges) - binned(fa, va, edges)
        print("%-12s %s" % (c, " ".join("%6.1f" % d[i] for i in show)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
