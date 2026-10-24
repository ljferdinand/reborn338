#!/usr/bin/env python3
"""Compressor LAW campaign, batch 6 (clean-room, supertask 6, 2026-09-17).

The threshold-0 corner needs a ratio-DEPENDENT concave (saturating) term in the
gain computer: at low threshold the compression slope relaxes far above threshold,
and how much it relaxes depends on the ratio. comp-law5 densely measured that
shape at ratio 96 only; fitting the ratio-dependence from one ratio (plus the
ratio-127 anchor) overfits. This batch adds two more dense low-threshold ratios:

  ratio 112: a high but FINITE slope (~4:1), where the concave relaxation is
               most visible (ratio 127 is near-limiter and pins the output).
  ratio 127: the limiter/leveller anchor, densely, to constrain the far-above-
               threshold region (over reaches ~44 dB at threshold 0) where the
               round-3 concave term leaked.

Both: threshold {0,8,16,24,32} x fader {127,100,80,60,40}. With comp-law5
(ratio 96) this gives three dense low-threshold ratios to fit the dependence.
off_vNNN are the comp-off references. One 303, master comp, no other effect.
No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

RATIOS = (112, 127)
TH  = (0, 8, 16, 24, 32)
FAD = (127, 100, 80, 60, 40)

def cases():
    out = [(f"off_v{v:03d}", None, v, f"comp OFF, fader {v}") for v in FAD]
    for rr in RATIOS:
        for t in TH:
            for v in FAD:
                out.append((f"lo_r{rr:03d}_t{t:03d}_v{v:03d}", C(rr, t), v,
                            f"ratio {rr}, threshold {t}, fader {v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law6"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, vol, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 6 (2026-09-17)", "",
           "Dense low-threshold at ratio 112 and 127 (with comp-law5's ratio 96),",
           "to fit the ratio-dependent concave term the thr-0 corner needs.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
