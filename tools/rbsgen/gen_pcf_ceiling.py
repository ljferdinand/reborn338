#!/usr/bin/env python3
"""PCF cutoff-law CEILING / high-Freq refit measurement (2026-09-10).

The shipped cutoff map fc = 9.01*2^(byte/127*10.653) was fitted on the 303 drone,
which cannot resolve a corner above its own harmonics (~8 kHz), so the top of the
law (Freq bytes ~112..127) is extrapolation. A broadband probe (crash+hat 16ths,
kick on the beat) through a STATIC (Amt 0), Q 0 lowpass reads the rolloff corner
directly, including above 8 kHz. A first pass (bytes 96/108/116/120/123/127,
exp/pcf-attack) showed ReBirth's high-Freq cutoff is MORE open than ours; this
dense sweep fits the top of the law properly.

Low Q gives a clean 2-pole corner (no resonant peak); Amt 0 keeps the envelope
out of the path so the sum is just the Freq byte.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO, BARS = 90.0, 4
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
# Dense across the divergent region (>= byte 108), with byte 64 as a low control.
FREQS = [64, 96, 104, 108, 112, 116, 119, 120, 122, 123, 125, 127]

def cases():
    out = [("ceil_ref", None, "broadband denominator, PCF routed nowhere")]
    for f in FREQS:
        out.append((f"ceil_f{f:03d}", dict(on=1, freq=f, q=0, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"static LP corner at Freq {f} (Q 0, Amt 0)"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-10", "pcf-ceiling"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF cutoff-law ceiling / high-Freq refit, 2026-09-10", "",
           "Broadband probe (crash+hat 16ths, kick on beat), STATIC Amt 0, Q 0 LP,",
           "dense rising Freq. Reads the rolloff corner ours vs ReBirth to refit the",
           "extrapolated top of the cutoff law. Tempo 90, 4 bars, comp off. Ratio vs `ceil_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
