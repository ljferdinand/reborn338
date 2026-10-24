#!/usr/bin/env python3
"""Compressor LAW campaign, batch 5 (clean-room, supertask 6, 2026-09-16).

The refined compressor matches ReBirth within ~1 dB across the surface EXCEPT the
lowest-threshold corner (threshold byte 0, quietest inputs: worst -4.17 dB). The
model (makeup - slope*softKnee(over)) cannot fit that row: ReBirth's compression
slope there is STEEPEST just above threshold and RELAXES at higher level
(dGR/dover ~ -0.92 quiet -> -0.43 loud), the opposite of a soft knee (whose slope
only rises toward the nominal ratio). A level-dependent-slope term is needed, and
it must be fit from how the row morphs between threshold 0 (bad) and threshold 32
(already good), which the existing campaign samples too sparsely at low threshold.

This batch densifies the low-threshold region at ratio 96 (the plane ratio, which
has a finite, visible slope): threshold in {0,8,16,24,32} x fader
{127,100,80,60,40}. off_vNNN are the comp-off references. One 303, master
compressor, no other effect. No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

TH  = (0, 8, 16, 24, 32)
FAD = (127, 100, 80, 60, 40)

def cases():
    out = [(f"off_v{v:03d}", None, v, f"comp OFF, fader {v}") for v in FAD]
    for t in TH:
        for v in FAD:
            out.append((f"lo_r096_t{t:03d}_v{v:03d}", C(96, t), v,
                        f"ratio 96, threshold {t}, fader {v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law5"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, vol, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 5 (2026-09-16)", "",
           "Dense low-threshold region at ratio 96 (threshold 0..32 x fader), to",
           "fit the level-dependent slope the thr-0 corner needs. One 303, master",
           "compressor, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
