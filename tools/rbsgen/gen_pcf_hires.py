#!/usr/bin/env python3
"""PCF high-cutoff resonance prominence, STATIC (clean-room, 2026-09-11).

The swept residual (PCF_FINDINGS 16) is that our resonance is weak at high cutoff
while ReBirth's stays a strong descending peak. Splitting static from dynamic:
does our resonance already fall off at high STATIC cutoff, or only when swept? The
303 drone can only resolve a resonant peak below ~8 kHz, so this was never tested.
A broadband probe (crash + hat, content to ~15 kHz) through a STATIC (Amt 0),
HIGH-Q lowpass at high Freq puts the resonant peak at 3-11 kHz where it can be
read. Compare RB and ours: if ours is already less prominent at high static
cutoff, the resonator needs a static high-cutoff fix; if ours matches statically,
the swept difference is purely dynamic (the ring). Comp/dist off; ratio vs ref.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
Q = 112               # top resonance band
FREQS = [64, 96, 108, 116, 122]

def cases():
    out = [("hr_ref", None, "broadband denominator, PCF routed nowhere")]
    for f in FREQS:
        out.append((f"hr_f{f:03d}", dict(on=1, freq=f, q=Q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"static LP high-Q ({Q}) resonance at Freq {f}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-11", "pcf-hires"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=140.0, n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF high-cutoff static resonance prominence, 2026-09-11", "",
           "Broadband probe, static Amt 0, Q 112, high Freq. Resonant peak at",
           "3-11 kHz, readable by a broadband source. RB vs ours: is our resonance",
           "weak at high STATIC cutoff or only dynamically? Ratio vs `hr_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
