#!/usr/bin/env python3
"""PCF bandpass-vs-resonance measurement, 2026-09-06: the clean 303 drone through
the PCF in BANDPASS across resonance and cutoff, plus matching lowpass points, so
the BP resonant-peak level law can be fit (it is currently scaled by k=1/Q, which
flattens the peak; the manual shows BP with a full resonant peak like the LP).

Static (Amt = 0), drone source, every render a ratio against `bp_ref` (PCF routed
nowhere). Includes LP at the same Freq/Q so the BP peak can be compared directly
to the LP resonant peak, which already matches ReBirth.

    python gen_pcf_bp.py --out-dir ../../../fixtures-2026-09-06/pcf-bp
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

MODE_LP, MODE_BP = 1, 0
TEMPO, BARS = 140.0, 4
SOURCE_KNOBS = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(note=0, length=16):
    return [G.step(note=note, slide=True) for _ in range(length)]

BP_Q   = (0, 16, 32, 48, 64, 80, 96, 112)   # BP peak vs resonance, Freq 64
BP_F   = (32, 48, 80, 96, 112)              # BP peak vs cutoff, Q 64
LP_Q   = (0, 64, 96, 112)                   # LP at Freq 64 for direct peak comparison

def cases():
    out = [("bp_ref", None, "denominator: drone, PCF routed nowhere")]
    for q in BP_Q:
        out.append((f"bp_f064_q{q:03d}", dict(on=1, freq=64, q=q, amt=0, pattern=0, decay=64, mode=MODE_BP),
                    f"BP peak vs resonance, Freq 64 Q {q}"))
    for f in BP_F:
        out.append((f"bp_f{f:03d}_q064", dict(on=1, freq=f, q=64, amt=0, pattern=0, decay=64, mode=MODE_BP),
                    f"BP peak vs cutoff, Freq {f} Q 64"))
    for q in LP_Q:
        out.append((f"lp_f064_q{q:03d}", dict(on=1, freq=64, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                    f"LP at Freq 64 Q {q} (peak reference)"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-06", "pcf-bp"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song(SOURCE_KNOBS, drone(), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1", pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF bandpass measurement, 2026-09-06", "",
           "Clean 303 drone through the PCF, static (Amt 0). BP across resonance",
           "and cutoff, plus LP points at Freq 64 for a direct BP-vs-LP peak",
           "comparison. Ratio against `bp_ref`. Tempo 140, 4 bars, comp off.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
