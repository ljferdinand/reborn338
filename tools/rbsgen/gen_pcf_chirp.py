#!/usr/bin/env python3
"""PCF resonant-chirp measurement, 2026-09-06: translate Luke's ear ("flangy,
ring-modulator-like, a curve on every chirp") into a measurement. Broadband probe
(kick+crash+closed-hat every sixteenth, quasi-continuous so the resonant peak is
always excited), high resonance, big envelope sweep, mostly pattern 0 (whole
note) so each chirp is isolated and long. Track the resonant peak's frequency
trajectory over each trigger, ours vs ReBirth: does RB's peak curve/ride where
ours is flat, and is it LP or BP.

    python gen_pcf_chirp.py --out-dir ../../../fixtures-2026-09-06/pcf-chirp
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP, MODE_BP = 1, 0
TEMPO, BARS = 90.0, 4
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

def cases():
    out = [("chirp_ref", None, "broadband denominator, PCF routed nowhere")]
    # isolated long chirps: whole-note pattern 0, big sweep (Freq 30 base, Amt 110)
    for q in (80, 100, 120):
        out.append((f"lp_q{q:03d}", dict(on=1, freq=30, q=q, amt=110, pattern=0, decay=96, mode=MODE_LP),
                    f"LP resonant chirp, Q {q}, pattern 0"))
    out.append(("bp_q100", dict(on=1, freq=30, q=100, amt=110, pattern=0, decay=96, mode=MODE_BP),
                "BP resonant chirp, Q 100, pattern 0"))
    # faster chirps (pattern 5 = sixteenths) at high Q, shorter decay
    out.append(("lp_q100_p05", dict(on=1, freq=30, q=100, amt=110, pattern=5, decay=48, mode=MODE_LP),
                "LP resonant chirp, Q 100, pattern 5 (fast)"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-06", "pcf-chirp"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF resonant-chirp measurement, 2026-09-06", "",
           "Broadband probe, high resonance, big envelope sweep. Track the resonant",
           "peak trajectory over each trigger, ours vs ReBirth. Ratio/analysis vs",
           "`chirp_ref`. Tempo 90, 4 bars, comp off.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
