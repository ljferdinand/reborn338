#!/usr/bin/env python3
"""PCF envelope base/Amt characterisation (clean-room, 2026-09-13).

The swept residual is the cutoff CLAMP (PCF_FINDINGS 21). The headroom law fixed
it for base >= ~30 but over-opens at very low base, where ReBirth's envelope reach
ROLLS OFF (base 10 at Amt 110 reaches only ~6.3 kHz, not the ceiling). The prior
sweep had only ONE low-base point, too sparse to fit the true reach-vs-base law.

This grid measures the envelope's peak REACH as a function of base Freq (dense at
the low end) at a saturated Amt, plus an Amt sweep at a low base (lots of headroom,
so the Amt->reach saturation is visible) to confirm the Amt-saturation shape and
whether it is base-independent. Same broadband probe, PCF LP, pattern 0 (one
trigger per bar), tempo 90 for a long tail, decay 96, as gen_pcf_sweep.py. Ratio
vs `esw_ref`. Nothing from any binary analysis is used; the finding is the measured
peak-cutoff trajectory.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO = 90
DECAY = 96
Q = 100
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

# Base sweep at saturated Amt (the reach-vs-base law), dense at the low end.
BASE_SWEEP = [5, 10, 15, 20, 25, 30, 40, 50, 64, 80, 100, 110]
# Amt sweep at a low base (lots of headroom -> the Amt->reach saturation is visible
# without the ceiling cap masking it), to fit Asat and check base-independence.
LOWBASE = 15
AMT_SWEEP = [40, 70, 90, 110, 127]

def cases():
    out = [("esw_ref", None, "broadband denominator, PCF routed nowhere")]
    for b in BASE_SWEEP:
        out.append((f"esw_b{b:03d}", dict(on=1, freq=b, q=Q, amt=110, pattern=0, decay=DECAY, mode=MODE_LP),
                    f"base Freq {b}, Amt 110 (reach-vs-base)"))
    for a in AMT_SWEEP:
        out.append((f"esw_lb{LOWBASE:03d}_a{a:03d}", dict(on=1, freq=LOWBASE, q=Q, amt=a, pattern=0, decay=DECAY, mode=MODE_LP),
                    f"base Freq {LOWBASE}, Amt {a} (Asat at low base)"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-13", "pcf-envsweep"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=float(TEMPO), n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF envelope base/Amt characterisation, 2026-09-13", "",
           "Broadband probe, PCF LP Q100 pattern-0, tempo 90, decay 96. A dense base",
           "sweep at Amt 110 (reach vs base) and an Amt sweep at base 15 (Asat with",
           "full headroom). Ratio vs `esw_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
