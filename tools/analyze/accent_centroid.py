#!/usr/bin/env python3
"""Differential accent brightness at a driven corner: batch B9.

For each (env-mod, accent-knob) cell in the B9 grid, measure the spectral
centroid and 85% rolloff of the accented steps (4 and 12 in each of 4 bars) and
subtract the same steps of the env-mod-matched no-accent control. That
differential is the accent's own brightness lift, with the note, pattern, filter
envelope and VCA cancelled. Then compare ReBirth's lift to ours at each knob, and
separately compare the PLAIN note (no accent) to isolate the env-mod corner.

One method both sides, step-locked windows, stereo averaged (a single 303 with no
effects is centered), magnitude spectra, never a null test.

ALIGNMENT. The onset is detected ONCE per (side, env-mod) from that side's
no-accent control and reused across every knob, so a loud accented file cannot
pull the window late (a peak-relative threshold does, and it inflated the
accent-over-push reading by ~150 Hz until this was fixed on 2026-09-04).

    python tools/analyze/accent_centroid.py \
        --rb   ../renders/b9-303-accent-corner-2026-09-04 \
        --ours ../renders/b9-303-accent-corner-2026-09-04/ours
"""
import argparse
import os
import numpy as np
from scipy.io import wavfile

SR = 44100
TEMPO = 130.0
BAR_S = 4 * 60.0 / TEMPO
STEP_S = BAR_S / 16.0
ACC_STEPS = (4, 12)
BARS = 4
EMS = (64, 90, 127)
KNOBS = (0, 32, 64, 96, 127)


def load_mono(path):
    _, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    return x / 32768.0


def onset_abs(x, thr=0.004):
    """Absolute threshold (~-48 dBFS), independent of peak, so accented and
    plain files land on the same onset."""
    return int(np.argmax(np.abs(x) > thr))


def metrics(seg):
    w = np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg * w))
    f = np.fft.rfftfreq(len(seg), 1.0 / SR)
    m = (f >= 20) & (f <= 20000)
    f, X = f[m], X[m]
    tot = X.sum()
    if tot <= 0:
        return np.nan, np.nan
    cent = (f * X).sum() / tot
    roll = f[np.searchsorted(np.cumsum(X), 0.85 * tot)]
    return cent, roll


def file_metrics(path, off):
    x = load_mono(path)
    vals = []
    for bar in range(BARS):
        for st in ACC_STEPS:
            t0 = bar * BAR_S + st * STEP_S
            a = off + int((t0 + 0.002) * SR)
            b = off + int((t0 + STEP_S) * SR)
            if b <= len(x):
                vals.append(metrics(x[a:b]))
    return np.nanmean(np.array(vals), axis=0)


def measure(root):
    out = {}
    for em in EMS:
        off = onset_abs(load_mono(os.path.join(root, "b9_em%03d_plain.wav" % em)))
        out[(em, "plain")] = file_metrics(
            os.path.join(root, "b9_em%03d_plain.wav" % em), off)
        for k in KNOBS:
            out[(em, k)] = file_metrics(
                os.path.join(root, "b9_em%03d_k%03d.wav" % (em, k)), off)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rb", required=True, help="dir of ReBirth b9_*.wav")
    ap.add_argument("--ours", required=True, help="dir of our b9_*.wav (tpt)")
    a = ap.parse_args(argv)
    rb, ou = measure(a.rb), measure(a.ours)

    print("PLAIN env-mod corner (no accent), centroid / rolloff85, ours minus RB")
    print("  em  | RB cent our cent   d | RB roll our roll   d")
    for em in EMS:
        r, o = rb[(em, "plain")], ou[(em, "plain")]
        print("  %3d | %7.0f %7.0f %+5.0f | %7.0f %7.0f %+5.0f"
              % (em, r[0], o[0], o[0]-r[0], r[1], o[1], o[1]-r[1]))

    print("\nACCENT LIFT (accented centroid minus the no-accent control)")
    print("  em  knob | RB lift  our lift |   gap")
    for em in EMS:
        rp, op = rb[(em, "plain")][0], ou[(em, "plain")][0]
        for k in KNOBS:
            rl, ol = rb[(em, k)][0] - rp, ou[(em, k)][0] - op
            print("  %3d %4d | %8.0f %8.0f | %+7.0f" % (em, k, rl, ol, ol - rl))
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
