#!/usr/bin/env python3
"""PCF round 9: all 55 preset patterns, measured properly.

Round 5 measured them off an estimator that saturated, so its velocities came
back compressed and its phase came back rotated. Appendix D, read by pixel,
fixes the positions, the phase, the grid unit and the length, and round 7 and 8
confirmed all four against ReBirth's audio. But the drawing's VELOCITIES are
only right for the patterns on the 1/32 axis. On the 1/16 axis with dynamics it
draws them about 1.4 times too tall: slopes of 0.63, 0.74, 0.55, 0.66, 0.50 and
0.46 against drawn, at correlations up to 0.999, so the shape is right and the
scale is not.

That is not the estimator. Running the identical extraction over OUR OWN render,
where the shipped table's velocities are known exactly, recovers them at slope
0.948 with a correlation of 1.000.

So this measures all 55 in the configuration round 8 proved:

    Freq 64, Amt 63, Decay 96, Q 0, LP, 90 BPM, four bars

which resolved 32 of 32 grid positions on every pattern tried. Freq 64 keeps
velocity 0 at 372 Hz instead of 9 Hz, so the bottom of the range stays inside
what the probe resolves; Decay 96 is 886 ms against an 83 ms thirty-second, so
the envelope barely moves between triggers and each reading is a velocity rather
than a point on a decay. Four bars is 128 thirty-seconds, which sees any period
up to 64 repeat at least twice.

    python gen_pcf_round9.py --out-dir ../../../fixtures-2026-08-29/pcf-round9
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

MODE_LP = 1

PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100,
                   ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

TEMPO, BARS = 90.0, 4

# One decade, every two bytes. Pattern 0 fires on the whole note, the longest
# unobstructed decay any preset offers.
V_ALL = tuple(range(55))

# The twelve patterns round 7 could not read, plus the three it could, so the
# ones already answered come back through the same estimator as a check.
V_SET = (9, 12, 13, 16, 17, 24, 25, 32, 34, 42, 44, 52)
V_PCF = dict(on=1, freq=64, q=0, amt=63, decay=96, mode=MODE_LP)


def cases():
    out = [("r9_ref", None, "broadband denominator, PCF routed nowhere")]
    for n in V_ALL:
        out.append((f"w_pat{n:02d}", dict(V_PCF, pattern=n),
                    f"pattern {n}: positions, phase, length and velocities"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "pcf-round9"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS,
                            pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
        print(f"  {name}.rbs  {len(data):,} bytes")

    man = ["# PCF round 9, 2026-08-29", "",
           "All 55 preset patterns in the configuration round 8 proved:",
           "Freq 64, Amt 63, Decay 96, Q 0, LP, 90 BPM, four bars, broadband",
           "probe. Every measurement is a ratio against `r9_ref`, which has the",
           "PCF routed NOWHERE.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
