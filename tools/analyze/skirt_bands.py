#!/usr/bin/env python3
"""Slope in SUCCESSIVE octave bands above the corner, instead of one band.

The question this exists to answer, recorded as untested in the audit on
2026-07-31: is the excess steepness a property of the filter, or a function of
how far the analysis band sits above the corner?

The knob-anchored measurement uses ONE band, 1.7x to 4x the nominal corner, and
finds ReBirth at about -31 dB/oct and ours at about -35 where an ideal 4-pole
would read about -21. Both are far steeper than four poles, which is either a
real shared mechanism or an artifact of measuring that close to the corner. A
single band cannot tell those apart. Successive bands can:

  a real order difference      -> the gap is the same in every band
  a near-corner / peak effect  -> the gap shrinks as the band climbs
  both sides approaching -24   -> the "shared extra steepness" is band placement
                                  and there is nothing shared to explain

Anchoring is the same as skirt_knob.py and for the same reason: the band is a
multiple of the corner implied by the cutoff KNOB, an input, identical on both
sides, so it cannot walk with either side's response or with resonance. Read
that file's header before changing anything here.

The source is divided out by harmonics(), which returns amplitude * k, so these
slopes are the filter's transfer function and NOT the sawtooth's own -6 dB/oct.
An ideal 4-pole read this way gives -20.7 over 1.7x..4x and approaches -24 far
above; those are the numbers to compare against, not -27 and -30.

Every band reports the level at its TOP relative to the passband, so a band that
has run into the dither floor announces itself rather than quietly flattening
the slope. Anything below about -60 dB is not a measurement.

Usage:  python skirt_bands.py [ours-dir] [rebirth-dir]
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics      # noqa: E402
from skirt_knob import nominal_corner                  # noqa: E402

# Octave bands as multiples of the nominal corner. The first one straddles the
# resonant peak and is expected to be useless at high resonance; it is printed
# anyway because "useless where the peak is" is itself the diagnostic.
BANDS = [(1.0, 2.0), (2.0, 4.0), (4.0, 8.0), (8.0, 16.0)]
TOP_CAP = 16000.0        # harmonics() stops here
MIN_PTS = 5              # a slope over fewer harmonics than this is not a slope
FLOOR_DB = -60.0         # below this, the band is dither, not filter


def curve(path):
    """Harmonic frequencies, the filter's dB response, and the RAW level.

    The third value matters for deciding whether a band is measurable.
    harmonics() returns amplitude * k, so a flat dither floor read through it
    RISES at +6 dB/oct and sits 20*log10(k) higher than it really is: at k=70
    that is 37 dB of apparent headroom that does not exist. Judging "is this
    band above the floor" on the source-divided curve therefore passes bands
    that are pure dither. Judge it on the raw column.
    """
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f, guess=220.0, tol=0.35)
    ks, m = harmonics(P, f, f0, kmax=90)
    if len(ks) < 12:
        return None
    db = 20 * np.log10(m + 1e-300)
    raw = db - 20 * np.log10(ks)          # undo the 1/k source division
    return ks * f0, db - np.mean(db[:3]), raw - np.mean(raw[:3])


def band_slope(fr, db, lo, hi):
    sel = (fr >= lo) & (fr <= hi)
    n = int(sel.sum())
    if n < MIN_PTS:
        return float("nan"), n, float("nan")
    A = np.vstack([np.log2(fr[sel]), np.ones(n)]).T
    slope = float(np.linalg.lstsq(A, db[sel], rcond=None)[0][0])
    return slope, n, float(db[sel][-1])


def ideal4(lo_mult, hi_mult, n=2000):
    """What a perfect 4-pole reads over the same band, same estimator."""
    x = np.logspace(np.log10(lo_mult), np.log10(hi_mult), n)
    db = -40.0 * np.log10(1.0 + x * x)
    A = np.vstack([np.log2(x), np.ones(n)]).T
    return float(np.linalg.lstsq(A, db, rcond=None)[0][0])


def main():
    ours = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WS, "renders", "batchS_ours")
    rb = sys.argv[2] if len(sys.argv) > 2 else os.path.join(WS, "renders", "batchS_rb")
    cells = []
    for fn in sorted(os.listdir(rb)):
        m = re.search(r"_cut(\d+)_reso(\d+)\.wav$", fn)
        if m and os.path.exists(os.path.join(ours, fn)):
            cells.append((int(m.group(1)), int(m.group(2)), fn))

    print(f"{len(cells)} cells, {os.path.basename(ours)} vs {os.path.basename(rb)}")
    print("Bands are multiples of the corner implied by the cutoff KNOB.")
    print("Source divided out, so these are filter slopes. An ideal 4-pole reads:")
    print("   " + "  ".join(f"{a:g}-{b:g}x {ideal4(a, b):+.1f}" for a, b in BANDS))
    print("'top' is the level at the top of the band vs the passband. Below "
          f"{FLOOR_DB:.0f} dB it is floor, not filter.\n")

    hdr = f"{'cut':>4s} {'res':>4s} |"
    for a, b in BANDS:
        hdr += f" {f'{a:g}-{b:g}x':>21s} |"
    print(hdr)
    print(f"{'':>4s} {'':>4s} |" + "".join(
        f" {'RB':>6s}{'ours':>7s}{'diff':>6s}{'n':>3s} |" for _ in BANDS))

    acc = {i: [] for i in range(len(BANDS))}
    for c, r, fn in cells:
        fc = nominal_corner(c)
        A = curve(os.path.join(rb, fn))
        B = curve(os.path.join(ours, fn))
        if not A or not B:
            continue
        line = f"{c:4d} {r:4d} |"
        for i, (a, b) in enumerate(BANDS):
            lo, hi = a * fc, min(b * fc, TOP_CAP)
            sa, na, _ = band_slope(A[0], A[1], lo, hi)
            sb, nb, _ = band_slope(B[0], B[1], lo, hi)
            _, _, ta = band_slope(A[0], A[2], lo, hi)     # RAW level at the top
            _, _, tb = band_slope(B[0], B[2], lo, hi)
            if not (np.isfinite(sa) and np.isfinite(sb)):
                line += f" {'':>21s} |"
                continue
            clean = min(ta, tb) > FLOOR_DB and hi > lo * 1.4
            mark = " " if clean else "*"
            if clean:
                acc[i].append((c, r, sa, sb))
            line += f" {sa:6.1f}{sb:7.1f}{sb-sa:6.1f}{min(na,nb):3d}{mark}|"
        print(line)

    print("\n* = band truncated by the 16 kHz cap or into the dither floor. "
          "Excluded below.\n")
    print(f"{'band':>8s} {'cells':>6s} {'RB mean':>8s} {'ours':>8s} {'diff':>7s} "
          f"{'ideal4':>7s} {'RB minus ideal':>15s}")
    for i, (a, b) in enumerate(BANDS):
        rows = acc[i]
        if not rows:
            print(f"{f'{a:g}-{b:g}x':>8s} {0:6d}  no usable cells")
            continue
        sa = np.array([x[2] for x in rows])
        sb = np.array([x[3] for x in rows])
        idl = ideal4(a, b)
        print(f"{f'{a:g}-{b:g}x':>8s} {len(rows):6d} {sa.mean():8.1f} {sb.mean():8.1f} "
              f"{(sb-sa).mean():+7.2f} {idl:7.1f} {sa.mean()-idl:+15.1f}")

    print()
    print("Read the diff column down. Constant means an order difference that")
    print("does not depend on where you look. Shrinking means the excess lives")
    print("near the corner. And RB minus ideal4 going to zero far up means the")
    print("shared extra steepness was band placement all along.")


if __name__ == "__main__":
    main()
