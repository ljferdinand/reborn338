#!/usr/bin/env python3
"""Compressor LAW campaign, batch 4 (clean-room, supertask 6, 2026-09-16).

Batches 1-3 gave dense LINES (ratio axis at threshold 0, threshold axis at ratio
127/64) and showed a leveller-toward-target topology that steady-state lines
cannot uniquely resolve. This batch fills the input x threshold PLANE at a single
strong ratio (96) so the transfer SURFACE is sampled in 2-D and can be fit
topology-agnostically together with the ratio-64 and ratio-127 lines already
measured.

  gp_r096_tTTT_vNNN : ratio 96, threshold in {0,32,64,96,127}, fader in
                      {127,100,80,60,40} -> output(input, threshold) at ratio 96.

off_vNNN are the comp-off references. One 303, master compressor, no other
effect. No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

TH  = (0, 32, 64, 96, 127)
FAD = (127, 100, 80, 60, 40)

def cases():
    out = [(f"off_v{v:03d}", None, v, f"comp OFF, fader {v}") for v in FAD]
    for t in TH:
        for v in FAD:
            out.append((f"gp_r096_t{t:03d}_v{v:03d}", C(96, t), v,
                        f"ratio 96, threshold {t}, fader {v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law4"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, vol, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 4 (2026-09-16)", "",
           "The input x threshold PLANE at ratio 96. One 303, master compressor,",
           "no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
