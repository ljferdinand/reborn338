#!/usr/bin/env python3
"""The 303's filter envelope and accent, from a batch B2 or B3 render.

B1 could read a corner off the drone's harmonic grid because nothing moved. Here
everything moves, and a window long enough to resolve a 65 Hz harmonic spacing
is 46 ms against a 125 ms step, far too coarse to watch a decay.

TWO MEASURES WERE TRIED AND DISCARDED BEFORE THIS ONE, and both failures are
worth keeping.

The spectral CENTROID needs no model, which is why it was attractive, and it
measures the source rather than the filter: a saw's residual harmonics dominate
it, so the entire env-mod range moved the centroid from 284 Hz to 646 Hz while
the corner itself travels several octaves.

Fitting a four-pole LADDER to the ratio against the wide-open reference fails
for a subtler reason: the 303's filter is never actually flat, so that ratio is
one ladder response divided by another and not a ladder response at all. It
pinned at the top of the search grid on 97 percent of frames. Same class of
error as a PCF reference with the unit on at zero depth.

What works needs no model and no reference. Both sides play the IDENTICAL
pattern, so BRIGHTNESS, the ratio of high-band to low-band energy, is monotone
in the corner over the range that matters and is computed the same way on each
side. It says nothing about the corner in Hz, and it answers the parity question
exactly, which is whether our envelope goes where ReBirth's goes and returns
when ReBirth's returns.

Steps are folded onto the sixteen-step grid and averaged over the bars in the
render, so one noisy note cannot become a law.

Three numbers per fixture:

  * PEAK, the highest centroid reached after a note, which is how far the
    envelope opens.
  * TAU, the time constant of the fall from that peak, fitted in log-centroid
    space over the part of the step that is above the floor.
  * FLOOR, the centroid just before the next note, which is where the envelope
    lands.

A CORRECTION, 2026-08-29. The floor was read from the last three columns of the
folded step and those are not the floor. A step at 120 BPM is 125 ms and the
analysis window is 23.2 ms wide, so every column from 36 on is a window that has
already crossed into the NEXT NOTE, and the last three are dominated by its
onset. The B2 table published that morning therefore reported the next note's
onset brightness as the floor, which is why it appeared not to depend on the
decay knob: an onset does not depend on a decay knob. `CLEAN` below is the last
column whose window ends inside the step, and nothing here reads past it.

    python bass303_envelope.py <rebirth-dir> <ours-dir> --prefix b2_
    python bass303_envelope.py <rebirth-dir> <ours-dir> --prefix b3_ --steps
"""
import argparse
import glob
import os
import sys
import wave

import numpy as np

BPM = 120.0
STEPS = 16
N, HOP = 1024, 128          # 23 ms window, 2.9 ms hop
HI_BAND = (1000.0, 8000.0)   # brightness numerator
LO_BAND = (60.0, 250.0)      # brightness denominator, the fundamental region
SKIP_BARS = 1               # let the first bar settle
# Last column whose N-sample window still ends inside a 125 ms step at 120 BPM.
# Anything past this contains the next note's onset. See the correction above.
CLEAN = int((60.0 / BPM / 4.0 * 44100.0 - N) // HOP)


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def centroid_track(x, sr):
    """(times, brightness dB, level dB) at HOP resolution.

    Brightness is high-band over low-band energy. Monotone in the filter's
    corner across the range these fixtures use, and free of any model.
    """
    win = np.hanning(N)
    f = np.fft.rfftfreq(N, 1.0 / sr)
    hi = (f >= HI_BAND[0]) & (f <= HI_BAND[1])
    lo = (f >= LO_BAND[0]) & (f <= LO_BAND[1])
    nf = max(0, (len(x) - N) // HOP)
    c = np.full(nf, np.nan)
    lv = np.full(nf, -200.0)
    for i in range(nf):
        seg = x[i * HOP:i * HOP + N, 0] + x[i * HOP:i * HOP + N, 1]
        p = np.abs(np.fft.rfft(seg * win)) ** 2
        a, b = p[hi].sum(), p[lo].sum()
        if b > 1e-16 and a > 1e-18:
            c[i] = 10.0 * np.log10(a / b)
            lv[i] = 10.0 * np.log10(p.sum())
    t = (np.arange(nf) * HOP + N * 0.5) / sr
    return t, c, lv


def fold(t, v, sr):
    """Average each step's trajectory over the bars in the render."""
    step = 60.0 / BPM / 4.0
    per = int(round(step * sr / HOP))
    start = int(round(SKIP_BARS * STEPS * step * sr / HOP))
    out = np.full((STEPS, per), np.nan)
    for s in range(STEPS):
        rows = []
        k = 0
        while True:
            i = start + (s + k * STEPS) * per
            if i + per > len(v):
                break
            seg = v[i:i + per]
            if np.isfinite(seg).sum() > per // 2:
                rows.append(seg)
            k += 1
        if rows:
            out[s] = np.nanmedian(np.vstack(rows), axis=0)
    return out


def describe(step_track, sr):
    """(peak dB, fall ms, floor dB) for one step's averaged trajectory.

    Brightness is already logarithmic, so an exponential decay of the CORNER is
    a straight line here. `fall` is the time to drop 6 dB of brightness from the
    peak, which is a robust stand-in for a time constant and does not need the
    trajectory to reach a floor inside the step.
    """
    v = step_track[:CLEAN + 1]
    ok = np.isfinite(v)
    if ok.sum() < 8:
        return np.nan, np.nan, np.nan
    i0 = int(np.nanargmax(np.where(ok, v, -np.inf)))
    peak = float(v[i0])
    floor = float(np.nanmedian(v[-3:]))
    fall = np.nan
    tail = v[i0:]
    for j in range(1, len(tail)):
        if np.isfinite(tail[j]) and tail[j] <= peak - 6.0:
            y0, y1 = tail[j - 1], tail[j]
            frac = (peak - 6.0 - y0) / (y1 - y0) if y1 != y0 else 0.0
            fall = (j - 1 + frac) * HOP / sr * 1000.0
            break
    return peak, fall, floor


def analyse(ref, fixture):
    xr, sr = load(ref)
    xf, _ = load(fixture)
    _, cr, _ = centroid_track(xr, sr)
    tf, cf, _ = centroid_track(xf, sr)
    fr = fold(None, cr, sr)
    ff = fold(None, cf, sr)
    return fr, ff, sr


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--ref", default="b23_ref.wav")
    ap.add_argument("--prefix", default="b2_")
    ap.add_argument("--steps", action="store_true",
                    help="report every step, not the median over steps")
    a = ap.parse_args(argv)

    names = sorted(os.path.basename(x)[:-4]
                   for x in glob.glob(os.path.join(a.ref_dir, a.prefix + "*.wav")))
    print("%-16s %19s %19s %19s"
          % ("fixture", "peak dB (R / ours)", "-6 dB ms (R / ours)",
             "floor dB (R / ours)"))
    pk, ta = [], []
    for n in names:
        fa = os.path.join(a.ref_dir, n + ".wav")
        fb = os.path.join(a.ours_dir, n + ".wav")
        if not (os.path.exists(fa) and os.path.exists(fb)):
            continue
        xa, sr = load(fa)
        xb, _ = load(fb)
        _, ca, _ = centroid_track(xa, sr)
        _, cb, _ = centroid_track(xb, sr)
        fa_ = fold(None, ca, sr)
        fb_ = fold(None, cb, sr)
        A = np.array([describe(fa_[s], sr) for s in range(STEPS)])
        B = np.array([describe(fb_[s], sr) for s in range(STEPS)])
        if a.steps:
            print("\n%s" % n)
            for s in range(STEPS):
                print("   step %2d  peak %7.0f / %7.0f   tau %6.1f / %6.1f"
                      % (s, A[s, 0], B[s, 0], A[s, 1], B[s, 1]))
            continue
        ma, mb = np.nanmedian(A, axis=0), np.nanmedian(B, axis=0)
        pk.append(mb[0] - ma[0])
        ta.append(mb[1] / ma[1] if ma[1] > 0 and mb[1] > 0 else np.nan)
        print("%-16s %8.0f %8.0f    %8.1f %8.1f    %8.0f %8.0f"
              % (n, ma[0], mb[0], ma[1], mb[1], ma[2], mb[2]))
    pk = np.array(pk, dtype=float)
    ta = np.array(ta, dtype=float)
    if np.isfinite(pk).any():
        print("\npeak error:  %.3f octaves mean, %.3f worst"
              % (np.nanmean(np.abs(pk)), np.nanmax(np.abs(pk))))
    if np.isfinite(ta).any():
        print("fall-time ratio:       %.2f mean (1.00 is exact), %.2f to %.2f"
              % (np.nanmean(ta), np.nanmin(ta), np.nanmax(ta)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
