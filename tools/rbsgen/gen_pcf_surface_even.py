#!/usr/bin/env python3
"""PCF resonator surface, EVEN bands (clean-room, 2026-09-11).

The first surface (gen_pcf_surface.py) sampled the odd resonance bands 1/3/5/7
(bytes 24/56/88/120); the walled implementer INTERPOLATED the even bands 0/2/4/6.
This grid measures those even bands directly (representative bytes 8/40/72/104) so
the interpolation can be confirmed or refit. Same method: broadband static probe,
Amt 0, LP, prominence + corner vs cutoff. Ratio vs `sre_ref`.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
QBYTES = [8, 40, 72, 104]               # representatives of even bands 0, 2, 4, 6
FREQS  = [64, 96, 108, 116, 122]

def cases():
    out = [("sre_ref", None, "broadband denominator, PCF routed nowhere")]
    for q in QBYTES:
        for f in FREQS:
            out.append((f"sre_q{q:03d}_f{f:03d}",
                        dict(on=1, freq=f, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                        f"static LP, even Q band byte {q}, Freq {f}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-11", "pcf-surface-even"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=140.0, n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF resonator surface, even bands, 2026-09-11", "",
           "Broadband static probe, LP, Amt 0. Even Q band bytes {8,40,72,104} x",
           "Freq {64,96,108,116,122} to confirm/refit the interpolated even bands.",
           "Ratio vs `sre_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
