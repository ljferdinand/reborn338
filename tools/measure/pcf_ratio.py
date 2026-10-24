#!/usr/bin/env python3
"""What the PCF does to a mix over time, as filtered-over-dry per octave band.

The static response of a filter says what it does to a steady tone. It says
nothing about where the envelope actually SITS while a song plays, and that is
the thing an ear notices: a filter with a perfect static response and an
envelope that will not stay open sounds dull and measures fine.

So this takes two renders of the same song, one with the PCF routed and one
without, and divides them band by band, frame by frame. The median of that
ratio is where the filter spends its time. Run it on ReBirth's pair and on ours
and the two medians are directly comparable, because each side is divided by
its own dry render and every difference in the voices cancels.

    python pcf_ratio.py <dry.wav> <filtered.wav> [--label NAME]
    python pcf_ratio.py a_dry.wav a_pcf.wav --against b_dry.wav b_pcf.wav

Stereo throughout: the two channels are POWER-summed per band, never averaged
as samples, because the two sides of a mix are not coherent and summing them
coherently cancels whatever is panned wide.
"""
import argparse
import os
import sys
import wave

import numpy as np

CENTRES = [44.0, 88.0, 176.0, 353.0, 707.0, 1414.0, 2828.0, 5656.0, 11313.0]
N, HOP = 2048, 1024
FLOOR_DB = -80.0     # frames this quiet in a band carry no information


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        raw = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return raw.reshape(-1, ch) / 32768.0, sr


def bands(x, sr):
    """Per-frame energy in each octave band, power-summed across channels."""
    win = np.hanning(N)
    f = np.fft.rfftfreq(N, 1.0 / sr)
    masks = [(f >= c / np.sqrt(2.0)) & (f < c * np.sqrt(2.0)) for c in CENTRES]
    nframes = (len(x) - N) // HOP
    out = np.zeros((nframes, len(CENTRES)))
    for i in range(nframes):
        seg = x[i * HOP:i * HOP + N, :]
        p = np.zeros(len(f))
        for c in range(seg.shape[1]):
            p += np.abs(np.fft.rfft(seg[:, c] * win)) ** 2
        for b, m in enumerate(masks):
            out[i, b] = p[m].sum()
    return out


def ratio(dry_path, wet_path):
    d, sr = load(dry_path)
    w, _ = load(wet_path)
    n = min(len(d), len(w))
    bd, bw = bands(d[:n], sr), bands(w[:n], sr)
    ref = bd.max(axis=0, keepdims=True) + 1e-30
    live = 10 * np.log10(bd / ref) > FLOOR_DB
    r = 10 * np.log10((bw + 1e-30) / (bd + 1e-30))
    return r, live


def report(label, r, live):
    print("\n%s: filtered over dry, dB" % label)
    print("%9s %8s %8s %8s %8s" % ("band", "median", "p10", "p90", "mean"))
    med = []
    for b, c in enumerate(CENTRES):
        v = r[live[:, b], b]
        if len(v) < 20:
            print("%9.0f %8s" % (c, "quiet"))
            med.append(np.nan)
            continue
        med.append(np.median(v))
        print("%9.0f %8.2f %8.2f %8.2f %8.2f"
              % (c, np.median(v), np.percentile(v, 10),
                 np.percentile(v, 90), v.mean()))
    return np.array(med)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dry")
    ap.add_argument("filtered")
    ap.add_argument("--label", default="A")
    ap.add_argument("--against", nargs=2, metavar=("DRY", "FILTERED"),
                    help="a second pair, reported alongside and differenced")
    ap.add_argument("--against-label", default="B")
    a = ap.parse_args(argv)

    ra, la = ratio(a.dry, a.filtered)
    ma = report(a.label, ra, la)
    if a.against:
        rb, lb = ratio(*a.against)
        mb = report(a.against_label, rb, lb)
        print("\n%s minus %s, by band, dB" % (a.label, a.against_label))
        print("%9s %8s" % ("band", "delta"))
        for c, x, y in zip(CENTRES, ma, mb):
            print("%9.0f %8.2f" % (c, x - y))
        ok = np.isfinite(ma) & np.isfinite(mb)
        print("\nmean |delta| over %d live bands: %.2f dB"
              % (ok.sum(), np.abs(ma[ok] - mb[ok]).mean()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
