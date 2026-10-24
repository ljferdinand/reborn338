#!/usr/bin/env python3
"""PCF resonator SURFACE (clean-room, 2026-09-11).

Defines what the re-derived PCF resonator must reproduce: the resonant-peak
PROMINENCE and CORNER across the (Q band x cutoff) plane, measured static (Amt 0)
with a broadband probe so peaks are resolvable up to ~15 kHz. Section 17 showed
our resonance is 10-15 dB under-prominent at high cutoff/high Q; this grid gives
the target per Q band across cutoff for the walled implementer to fit.

Q bytes are one representative per resonance band (P3: bands are 16 bytes wide, a
transfer is identical within a band): bands 1/3/5/7 span low to high resonance.
Cutoffs run Freq 64 (control, where ours already matches) up to 122 (the defect
region). Broadband probe, LP, comp/dist off; ratio vs `srf_ref`.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
QBYTES = [24, 56, 88, 120]              # representatives of resonance bands 1, 3, 5, 7
FREQS  = [64, 96, 108, 116, 122]

def cases():
    out = [("srf_ref", None, "broadband denominator, PCF routed nowhere")]
    for q in QBYTES:
        for f in FREQS:
            out.append((f"srf_q{q:03d}_f{f:03d}",
                        dict(on=1, freq=f, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                        f"static LP, Q band byte {q}, Freq {f}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-11", "pcf-surface"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=140.0, n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF resonator surface, 2026-09-11", "",
           "Broadband static probe, LP, Amt 0. Resonant-peak prominence + corner",
           "over (Q band byte {24,56,88,120}) x (Freq {64,96,108,116,122}). The",
           "target for the resonator re-derivation. Ratio vs `srf_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
