#!/usr/bin/env python3
"""PCF clean-room probes P3 + P4 (2026-09-10).

P3 - resonance quantisation and filter order. Static (Amt 0) LP at a fixed Freq,
Q swept byte by byte across a few band boundaries. If the resonance control is
quantised, bytes inside one band give an identical transfer and there is a step
at the boundary; if it is continuous, every byte differs. Open question, decided
from the exports. (Filter ORDER / rolloff slope is read separately from the
existing pcf-ceiling LP Q0 renders, no new fixture needed.)

P4 - decay law and its top. A broadband sweep (Freq low, Amt high, high Q,
pattern 0 = one trigger per bar) whose cutoff decays after each trigger; the
decay byte is swept, with emphasis on the top. Measures the decay time per byte
and, at the top byte, whether the cutoff holds (no decay) or still falls.

Broadband probe (crash + hat every sixteenth, kick on the beat). Ratio vs
`pcfp_ref`. Comp/dist off. Nothing from any binary analysis is used; the sweeps
just vary the controls and the findings come from the audio.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

# P3: static Amt-0 LP, Freq 64 (corner ~372 Hz, resonant peak in-band), Q bytes
# straddling the suspected 16-byte band boundaries plus within-band neighbours.
P3_TEMPO = 140
P3_FREQ = 64
P3_Q = [8, 15, 16, 24, 31, 32, 48, 63, 64, 96, 112, 120, 127]

# P4: broadband sweep, Freq 30 / Amt 110 / Q 100 / pattern 0, tempo 90 for a long
# tail; decay bytes spanning the range with the top densely sampled.
P4_TEMPO = 90
P4_DECAY = [0, 32, 64, 96, 112, 119, 120, 124, 127]

def cases():
    out = [("pcfp_ref", P3_TEMPO, None, "broadband denominator, PCF routed nowhere")]
    for q in P3_Q:
        out.append((f"q_{q:03d}", P3_TEMPO,
                    dict(on=1, freq=P3_FREQ, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"P3 resonance quantisation, static LP Freq {P3_FREQ} Q {q}"))
    for d in P4_DECAY:
        out.append((f"dec_{d:03d}", P4_TEMPO,
                    dict(on=1, freq=30, q=100, amt=110, pattern=0, decay=d, mode=MODE_LP),
                    f"P4 decay law, sweep Q100 Freq30 Amt110 decay {d}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-10", "pcf-probes"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, tempo, pcf, why in cases():
        data = G.build_song({}, [], tempo=float(tempo), n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF probes P3 (resonance quantisation) + P4 (decay), 2026-09-10", "",
           "Broadband probe, comp/dist off. P3: static Amt-0 LP Freq 64, Q swept",
           "across band boundaries. P4: Q100 Freq30 Amt110 pattern-0 sweep, decay",
           "byte swept (top dense). Ratio vs `pcfp_ref`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
