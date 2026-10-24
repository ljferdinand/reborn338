#!/usr/bin/env python3
"""PCF round 11, 2026-09-05: settle the two remaining static-filter gaps.

Everything here is STATIC: Amt = 0 so the envelope is out of the path and each
render divided by `r11_ref` (PCF routed nowhere) is the filter's magnitude
response. Broadband probe throughout (kick + crash + closed hat on every
sixteenth, 43 Hz to 18 kHz), which resolves a narrow high-Q peak and the
sub-bass that survives a nearly-closed filter, both of which the round-6 303
drone (a 64.6 Hz harmonic grid, fundamental above the corner at Freq 0..2) could
not. Same probe as rounds 7 to 10.

GAP 1, the corner drift with resonance. Round 6 fitted ReBirth's corner drifting
373 -> 350 -> 440 Hz as Q rose while ours holds 372 (2.15 dB rms at Q 96), and
could not tell a real drift from the fit trading fc against Q on a coarse grid.
This sweeps Q at TWO cutoffs so a 2-pole (fc, Q) fit to the whole broadband curve
separates them, and says whether any drift is a fixed Hz offset or a fraction of
fc.

GAP 2, the byte extremes. The bottom three cutoff bytes (Freq 0, 1, 2) and the
top two resonance bytes (Q 120, 127), which the drone could not resolve.

    python gen_pcf_round11.py --out-dir ../../../fixtures-2026-09-05/pcf-round11
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

# Q sweep at Freq 64 (round 6's cutoff, so the drone and the broadband probe are
# directly comparable), including the top two resonance bytes.
Q64 = (0, 24, 48, 72, 96, 112, 120, 127)
# Q sweep at Freq 96, sparser: does any corner drift scale with fc?
Q96 = (0, 96, 127)
# Bottom cutoff bytes at Q 0, the ones the drone could not resolve.
FLOW = (0, 1, 2, 4, 8)


def cases():
    out = [("r11_ref", None, "broadband denominator, PCF routed nowhere")]
    for q in Q64:
        out.append((f"q64_q{q:03d}",
                    dict(on=1, freq=64, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"corner-vs-resonance at Freq 64, Q {q}"))
    for q in Q96:
        out.append((f"q96_q{q:03d}",
                    dict(on=1, freq=96, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"corner-vs-resonance at Freq 96, Q {q}"))
    for f in FLOW:
        out.append((f"f_f{f:03d}",
                    dict(on=1, freq=f, q=0, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"cutoff law at the bottom byte {f}, Q 0"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-09-05", "pcf-round11"))
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

    man = ["# PCF round 11, 2026-09-05", "",
           "Static filter (Amt = 0), broadband probe (kick + crash + closed hat",
           "on every sixteenth). Every measurement is a power-spectrum RATIO",
           "against `r11_ref`, which has the PCF routed NOWHERE, so the probe",
           "divides out and what is left is the filter's magnitude response.",
           "Tempo 90, 4 bars, master compressor off, no other effects.", "",
           "Two gaps: the corner drift with resonance (Q sweeps at Freq 64 and 96,",
           "fit fc and Q from the whole curve to separate them), and the byte",
           "extremes (bottom cutoff bytes, top resonance bytes) the drone could",
           "not resolve.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
