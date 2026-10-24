#!/usr/bin/env python3
"""PCF makeup-vs-cutoff-at-resonance, 2026-09-06. Tests whether ReBirth's
resonance makeup (the passband level restoration, an attenuation at high Q)
depends on the CUTOFF position. Ours applies gainFor(reso) constant, verified
only at a moderate cutoff (Freq 64). The 909 patterns clamp the cutoff WIDE OPEN
at high Q and run ~3 dB quiet; if RB attenuates less when the cutoff is high, that
is the ~3-4 dB dynamic-makeup deficit, and the fix is a cutoff-dependent makeup.

Static (Amt 0), drone, LP. Passband makeup = the transfer's level well below the
corner (source divides out, so non-determinism does not matter). Freq sweep at
three resonances.

    python gen_pcf_makeup.py --out-dir ../../../fixtures-2026-09-06/pcf-makeup
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO, BARS = 140.0, 4
SOURCE_KNOBS = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(note=0, length=16):
    return [G.step(note=note, slide=True) for _ in range(length)]
QS = (0, 64, 96)
FS = (64, 80, 96, 112, 127)   # Freq high enough that the drone resolves the passband

def cases():
    out = [("mk_ref", None, "drone, PCF routed nowhere")]
    for q in QS:
        for f in FS:
            out.append((f"mk_q{q:03d}_f{f:03d}",
                        dict(on=1, freq=f, q=q, amt=0, pattern=0, decay=64, mode=MODE_LP),
                        f"makeup at Q {q}, Freq {f}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-06", "pcf-makeup"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song(SOURCE_KNOBS, drone(), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1", pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF makeup-vs-cutoff-at-resonance, 2026-09-06", "",
           "Static drone (Amt 0), LP. Passband makeup vs Freq at Q 0/64/96, ours",
           "vs ReBirth (transfer ratio, source divides out). Tests whether the",
           "resonance makeup depends on cutoff (ours ignores it).", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
