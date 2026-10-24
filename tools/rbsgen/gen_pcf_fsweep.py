#!/usr/bin/env python3
"""PCF Freq-sweep of resonant chirps, 2026-09-07, for the cutoff-dependent
envelope-depth fit. Same resonant sweep (Q100, Amt110, decay96, pattern 0,
broadband probe) across base cutoff Freq. Ours over-opens the cutoff at the
attack by a Freq-dependent amount; fit ENVSC(Freq) so ours' attack-centroid
trajectory matches ReBirth's at each base cutoff.

    python gen_pcf_fsweep.py --out-dir ../../../fixtures-2026-09-07/pcf-fsweep
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO, BARS = 90.0, 4
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
FREQS = (0, 15, 30, 45, 60, 80, 100)

def cases():
    out = [("fs_ref", None, "broadband denominator, PCF routed nowhere")]
    for f in FREQS:
        out.append((f"fs_f{f:03d}", dict(on=1, freq=f, q=100, amt=110, pattern=0, decay=96, mode=MODE_LP),
                    f"resonant chirp at Freq {f}, Q100 Amt110"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-07", "pcf-fsweep"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF Freq-sweep resonant chirps, 2026-09-07", "",
           "Q100 Amt110 decay96 pattern0, broadband probe, across base Freq. For",
           "the cutoff-dependent envelope-depth fit (ENVSC vs Freq).", "",
           "| fixture | what |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
