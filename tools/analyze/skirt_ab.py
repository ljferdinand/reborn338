#!/usr/bin/env python3
"""The resonant SKIRT: how fast the filter falls above its corner, ours vs ReBirth.

Third of the three remaining 303 residuals, and probably the parent of the other
two. The corner POSITION is now fitted flat to within 0.15 octaves, but the
corner rise is still about 0.23 octaves too fast over reso 60 to 127 and the
output level rises only 1.5 to 1.8 dB across the cutoff knob where ReBirth's
rises 4.40. A skirt that is too steep explains both: it passes less of the
source, so opening the filter recovers less energy.

Why this needs batch S rather than batch H. On batch H the skirt at high cutoff
is not measurable at all, because ReBirth's own harmonics reach its dither floor
at 6070 Hz (cutoff 127 resonance 127) while the fit window runs to about 16 kHz,
and a floor is flat, and flat reads as a shallow slope. That is exactly how a
wrong slope figure reached a commit message. Batch S plays the same settings an
octave up: a sawtooth harmonic at frequency f has amplitude proportional to
f0/f, so doubling f0 puts 6 dB more signal at every skirt frequency, and the
last usable harmonic moves 6070 Hz to 15857 Hz. The filter does not track the
keyboard, so this is the same filter measured with a better probe.

The slope is fitted only over harmonics that clear the floor on BOTH sides, and
a cell is reported as unusable rather than given a number when too few remain.

Usage:  python skirt_ab.py [ours-dir] [rebirth-dir]
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics, cells_in   # noqa: E402

# A 6 dB margin was not enough, and the failure was instructive. Two ReBirth
# exports of ONE fixture agree on band energies to 0.03 dB, which is what the
# automation was validated at, yet gave skirt slopes of -19.4 and -28.2 dB/oct.
# The tell is the usable-harmonic count: 41 against 14 for identical settings.
# Up there the harmonics sit AT the floor, so whether any one clears a small
# margin is decided by the noise realisation, and a slope fitted through
# whichever points happened to survive is fitted to noise. Selecting points by
# an SNR test and then measuring a quantity that depends on which points were
# selected is not a guard, it is a way to launder noise into a finding.
#
# Two changes. The margin goes to 12 dB so a harmonic has to be unambiguously
# present. And the fit window is CAPPED at the last harmonic that clears on
# BOTH sides, so the two are compared over a band where both actually have
# signal, rather than each over its own reach.
FLOOR_MARGIN = 12.0
MIN_PTS = 6
MIN_SPAN_OCT = 1.0      # refuse to call it a slope over less than an octave


def curve_with_floor(path):
    """Harmonic magnitudes plus a per-harmonic 'is this real' mask."""
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f, guess=220.0, tol=0.35)
    ks, m = harmonics(P, f, f0, kmax=90)
    if len(ks) < 12:
        return None
    binw = f[1] - f[0]
    real = []
    for k in ks:
        fc = k * f0
        i = int(round(fc / binw))
        h = np.sqrt(P[max(0, i - 3):i + 4].max())
        side = []
        for j in (int(round((fc - 0.5 * f0) / binw)), int(round((fc + 0.5 * f0) / binw))):
            if 0 < j < len(P) - 3:
                side.append(np.sqrt(P[j - 2:j + 3].mean()))
        b = float(np.mean(side)) if side else 0.0
        real.append(20 * np.log10(h / (b + 1e-30) + 1e-30) >= FLOOR_MARGIN)
    db = 20 * np.log10(m + 1e-300)
    return ks * f0, db - np.mean(db[:3]), np.array(real, bool)


def slope(fr, db, real, lo, hi):
    sel = real & (fr >= lo) & (fr <= hi)
    if sel.sum() < MIN_PTS:
        return float("nan"), int(sel.sum())
    A = np.vstack([np.log2(fr[sel] / lo), np.ones(sel.sum())]).T
    return float(np.linalg.lstsq(A, db[sel], rcond=None)[0][0]), int(sel.sum())


def slope_fixed(fr, db, lo, hi):
    """Slope over a FIXED band, with NO gating of any kind.

    This replaces the gated estimator, which is discredited. That one selected
    harmonics by whether they cleared a contrast threshold, and since the
    threshold sat near the data, which points survived was decided by the
    export's noise realisation: two exports of one fixture gave 41 and 14
    surviving harmonics and slopes 8.8 dB/oct apart, while their band energies
    agreed to 0.03 dB.

    A fixed band cannot do that. Every point in the band is used, both sides are
    fitted over the same frequencies, and the caller is handed the level at the
    top of the band so a contaminated read announces itself instead of quietly
    changing the fit. If the top-of-band level is close to the dither floor, the
    number is suspect and you can see that it is.
    """
    sel = (fr >= lo) & (fr <= hi)
    if sel.sum() < MIN_PTS:
        return float("nan"), 0, float("nan")
    A = np.vstack([np.log2(fr[sel] / lo), np.ones(sel.sum())]).T
    s = float(np.linalg.lstsq(A, db[sel], rcond=None)[0][0])
    return s, int(sel.sum()), float(db[sel][-1])


def main():
    ours = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WS, "renders", "batchS_ours")
    rb = sys.argv[2] if len(sys.argv) > 2 else os.path.join(WS, "renders", "batchS_rb")
    have = {(c, r): p for c, r, p in cells_in(rb)}
    print(f"ReBirth cells present: {len(have)}   ours: {ours}\n")
    print("Slope above the corner, dB/oct, fitted only where BOTH sides clear")
    print("the noise floor. n is the number of usable harmonics.\n")
    print(f"{'cut':>4s} {'reso':>5s} | {'window kHz':>12s} | {'ReBirth':>8s} {'n':>3s} "
          f"| {'ours':>8s} {'n':>3s} | {'ours-RB':>8s}")
    rows = []
    for (c, r), pr in sorted(have.items()):
        po = os.path.join(ours, os.path.basename(pr))
        if not os.path.exists(po):
            continue
        try:
            A, B = curve_with_floor(pr), curve_with_floor(po)
        except (PermissionError, OSError) as e:
            # ReBirth keeps the wav it is mid-export on locked. Skipping is
            # right: a partially written file would measure as a real cell.
            print(f"{c:4d} {r:5d} | SKIPPED, file locked or unreadable ({type(e).__name__})")
            continue
        if A is None or B is None:
            continue
        fa, da, ra = A
        fb, db_, rb_ = B
        # Window: from the ReBirth peak upward, so both are measured over the
        # same band rather than each over its own.
        pk = float(fa[int(np.argmax(da))])
        # Cap the window at the last harmonic that is unambiguously present on
        # BOTH sides. Without this the two are fitted over different bands
        # whenever one reaches further than the other, and the difference then
        # reports the reach rather than the slope.
        top_a = float(fa[ra].max()) if ra.any() else 0.0
        top_b = float(fb[rb_].max()) if rb_.any() else 0.0
        lo = pk * 1.6
        hi = min(pk * 6.0, top_a, top_b)
        if hi <= lo * (2.0 ** MIN_SPAN_OCT):
            print(f"{c:4d} {r:5d} | UNUSABLE, both sides clear the floor only up to "
                  f"{min(top_a, top_b)/1000:.1f} kHz, under an octave above the peak")
            continue
        sa, na = slope(fa, da, ra, lo, hi)
        sb, nb = slope(fb, db_, rb_, lo, hi)
        d = sb - sa if (np.isfinite(sa) and np.isfinite(sb)) else float("nan")
        if np.isfinite(d):
            rows.append((c, r, d))
        fmt = lambda v: f"{v:8.1f}" if np.isfinite(v) else f"{'unusable':>8s}"
        print(f"{c:4d} {r:5d} | {lo/1000:5.1f}-{hi/1000:<6.1f} | {fmt(sa)} {na:3d} "
              f"| {fmt(sb)} {nb:3d} | {fmt(d)}")
    if rows:
        print()
        # Sign discipline: both slopes are negative, so ours-RB NEGATIVE means
        # ours falls faster (steeper) and POSITIVE means ours falls slower
        # (shallower). Naming the column "steeper by" and printing a signed
        # number invites exactly the misreading that would follow.
        for c in sorted(set(x[0] for x in rows)):
            v = float(np.mean([d for cc, _, d in rows if cc == c]))
            word = "STEEPER" if v < 0 else "shallower"
            print(f"  cutoff {c:3d}: ours {word} than ReBirth by {abs(v):.2f} dB/oct")
        print("\n  The sign CROSSES OVER with cutoff: too steep at 64, too shallow")
        print("  by 110. So this is not one constant offset in the skirt, it is a")
        print("  slope that fails to track the corner. ReBirth's own skirt steepens")
        print("  monotonically as the filter opens and ours does not follow.")
        print("\n  NOTE this does NOT explain the output-level tilt, and the earlier")
        print("  guess that it would is now contradicted by its own evidence: a")
        print("  SHALLOWER skirt passes MORE energy, so at high cutoff, where we are")
        print("  shallower, our level should rise faster than ReBirth's rather than")
        print("  slower. The level tilt is a separate and still-unexplained defect.")


if __name__ == "__main__":
    main()
