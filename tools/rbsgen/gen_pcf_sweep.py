#!/usr/bin/env python3
"""PCF swept-envelope characterisation (clean-room, 2026-09-11).

The unsolved PCF residual is the swept "flangy chirp": on a big envelope sweep
ReBirth's cutoff peaks IN-BAND while ours over-opens. Earlier work proved it is an
envelope-dynamics effect, not a cutoff-law or static-resonance one, and the
obvious envelope models failed. This probe asks the discriminating question that
was never measured: how does ReBirth's PEAK cutoff on the sweep depend on the Amt
knob and on the base Freq?

If the peak cutoff SATURATES as Amt rises (a clip), that clip is a measurable rule
an implementer can reproduce. If it keeps scaling with Amt, the reach is just
smaller than ours and points elsewhere. Sweeping base Freq at fixed Amt shows how
the base shifts the peak.

Broadband probe (crash + hat + kick), PCF LP, high resonance, pattern 0 (one
trigger per bar), tempo 90 for a long tail. Ratio vs `psw_ref`. Nothing from any
binary analysis is used; the finding is the measured peak-cutoff trajectory.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO = 90
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
Q = 100

def cases():
    out = [("psw_ref", None, "broadband denominator, PCF routed nowhere")]
    # Amt sweep at base Freq 30 (envelope reach vs Amt).
    for a in (40, 70, 90, 110, 127):
        out.append((f"psw_a{a:03d}", dict(on=1, freq=30, q=Q, amt=a, pattern=0, decay=96, mode=MODE_LP),
                    f"Amt {a}, base Freq 30, Q {Q}"))
    # Base Freq sweep at Amt 110 (30 covered by psw_a110).
    for f in (10, 50, 70):
        out.append((f"psw_f{f:03d}", dict(on=1, freq=f, q=Q, amt=110, pattern=0, decay=96, mode=MODE_LP),
                    f"base Freq {f}, Amt 110, Q {Q}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-11", "pcf-sweep"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=float(TEMPO), n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF swept-envelope characterisation, 2026-09-11", "",
           "Broadband probe, PCF LP Q100 pattern-0, tempo 90. Amt sweep (base 30)",
           "and base-Freq sweep (Amt 110) to measure how the swept PEAK cutoff",
           "depends on Amt and base. Ratio vs `psw_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
