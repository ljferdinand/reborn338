#!/usr/bin/env python3
"""The low-frequency response shape, ours against ReBirth, resolved properly.

The defect: ReBirth loses low end as resonance rises and we do not. At resonance
127 its 55 Hz harmonic sits 6.3 dB below its own 220 Hz while ours sits 0.2 dB
below. A ladder cannot do that, because both frequencies are deep in the passband
where the response is flat at 1/(1+k) and feedback attenuates them equally.

Batch L could only say THAT it happens. This says what SHAPE it has, which is
what distinguishes a low shelf from a pole from a frequency-shaped makeup gain.

Batch N crosses two low notes so their harmonic combs interleave: C1 at 32.703 Hz
gives a point every 32.7 Hz and E1 at 41.203 gives one every 41.2, for eighteen
points below 340 Hz off two fixtures. The notes are TIED rather than plucked, so
the gate never closes and the fundamental gets about 65 cycles per bar instead of
the 2.0 a plucked half-step gate would allow at this pitch.

NORMALISATION. Every curve is referenced to its own level over REF_LO..REF_HI,
which is chosen high enough to be above whatever is happening down low and low
enough to stay far below the corner in every cell. So these are SHAPES, and a
plain level offset between the two sides cannot appear here as a low-end defect.
That separation is the whole point: the level offset is a separate, settled
finding and must not be allowed to leak into this one.

Run the checks before the curve. If the duplicates disagree, the export chain
drifted. If tied and plucked disagree, the tie path is behaving differently on
one side and the curve cannot be trusted. If the two combs disagree where they
nearly collide at 163.5 and 164.8 Hz, the harmonic extraction is wrong.

Usage:  python lowend_ab.py [--checks-only]
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics       # noqa: E402

RB = os.path.join(WS, "renders", "batchN_rb")
OURS = os.path.join(WS, "renders", "batchN_ours")
F0 = {"C1": 32.703, "E1": 41.203}
REF_LO, REF_HI = 230.0, 340.0      # the "flat" reference band
CUT = (64, 127)
RESO = (0, 60, 90, 127)


def curve(path, label):
    """Harmonic frequencies and dB, source divided out, referenced to REF band."""
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f, guess=F0[label], tol=0.20)
    ks, m = harmonics(P, f, f0, kmax=40)
    if len(ks) < 8:
        return None
    fr = ks * f0
    db = 20 * np.log10(m + 1e-300)
    sel = (fr >= REF_LO) & (fr <= REF_HI)
    if sel.sum() < 2:
        return None
    return fr, db - db[sel].mean(), f0


def load(d, name, label):
    p = os.path.join(d, name + ".wav")
    return curve(p, label) if os.path.exists(p) else None


def checks():
    ok = True
    print("=== 1. duplicate controls, ReBirth side ===")
    for name, lab in (("n303_C1_cut127_reso127_tie", "C1"),
                      ("n303_E1_cut064_reso000_tie", "E1")):
        a = load(RB, name, lab)
        b = load(RB, name + "_dup", lab)
        if not a or not b:
            print(f"  {name:<34s} MISSING")
            ok = False
            continue
        n = min(len(a[1]), len(b[1]))
        d = b[1][:n] - a[1][:n]
        print(f"  {name:<34s} f0 {a[2]:8.3f}/{b[2]:8.3f}  "
              f"rms {np.sqrt((d ** 2).mean()):.3f} dB  max {np.abs(d).max():.2f}")

    print("\n=== 2. tied vs plucked, same cell, each side ===")
    print("    the EFFECT SIZE must agree, or the tie path differs between sides")
    for r in (0, 127):
        row = f"  cut127 reso{r:3d} |"
        for tag, d in (("RB", RB), ("ours", OURS)):
            t = load(d, f"n303_C1_cut127_reso{r:03d}_tie", "C1")
            p = load(d, f"n303_C1_cut127_reso{r:03d}_pluck", "C1")
            if not t or not p:
                row += f" {tag}: missing |"
                continue
            # compare the fundamental's level relative to the reference band
            row += f" {tag}: tie {t[1][0]:+6.2f}  pluck {p[1][0]:+6.2f}  " \
                   f"diff {p[1][0] - t[1][0]:+5.2f} |"
        print(row)

    print("\n=== 3. the two combs where they nearly collide ===")
    print("    C1 harmonic 5 = 163.5 Hz, E1 harmonic 4 = 164.8 Hz")
    for tag, d in (("ReBirth", RB), ("ours", OURS)):
        for c in CUT:
            for r in RESO:
                a = load(d, f"n303_C1_cut{c:03d}_reso{r:03d}_tie", "C1")
                b = load(d, f"n303_E1_cut{c:03d}_reso{r:03d}_tie", "E1")
                if not a or not b:
                    continue
                va, vb = a[1][4], b[1][3]
                if abs(va - vb) > 1.5:
                    print(f"  {tag} cut{c} reso{r}: {va:+.2f} vs {vb:+.2f}  "
                          f"DISAGREE by {abs(va - vb):.2f} dB")
                    ok = False
        print(f"  {tag}: all cells agree within 1.5 dB" if ok else "")
    return ok


def main():
    checks()
    if "--checks-only" in sys.argv:
        return
    print("\n=== the low-frequency shape, dB relative to each curve's own "
          f"{REF_LO:.0f}-{REF_HI:.0f} Hz level ===")
    for c in CUT:
        print(f"\ncutoff {c}")
        print(f"  {'Hz':>7s} |" + "".join(f"  reso{r:<3d} RB   ours    gap" for r in RESO))
        pts = {}
        for r in RESO:
            for lab in ("C1", "E1"):
                A = load(RB, f"n303_{lab}_cut{c:03d}_reso{r:03d}_tie", lab)
                B = load(OURS, f"n303_{lab}_cut{c:03d}_reso{r:03d}_tie", lab)
                if not A or not B:
                    continue
                for i, fq in enumerate(A[0]):
                    if fq > REF_HI:
                        break
                    pts.setdefault(round(float(fq), 1), {})[r] = (A[1][i], B[1][i])
        for fq in sorted(pts):
            row = f"  {fq:7.1f} |"
            for r in RESO:
                if r in pts[fq]:
                    a, b = pts[fq][r]
                    row += f"  {a:7.2f}{b:7.2f}{b - a:7.2f}"
                else:
                    row += f"  {'':21s}"
            print(row)


if __name__ == "__main__":
    main()
