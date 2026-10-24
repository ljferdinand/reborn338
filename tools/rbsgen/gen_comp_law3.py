#!/usr/bin/env python3
"""Compressor LAW campaign, batch 3 (clean-room, supertask 6, 2026-09-16).

Batch 2 measured the ratio law (threshold 0) and the threshold law (ratio 127)
as dense lines, and showed a below-threshold BOOST (gain > 1) at low threshold.
The open question is whether a make-up gain is tied to the threshold, or the gain
law itself produces the target-leveling. Three arms resolve it:

  mk_r000_tTTT  ratio 0 (exact bypass), threshold swept, fader 127
                -> if the output moves, threshold carries a make-up; if it stays
                   at the input, there is no threshold-tied make-up.
  xr_r064_tTTT  ratio 64, threshold swept, fader 127
                -> is threshold(byte) -> dBFS the SAME curve at a second ratio?
  xf_r064_t064_vNNN  ratio 64, threshold 64, fader swept
                -> a full transfer curve (input -> output) through a soft knee.

off_vNNN are comp-off references at each fader. One 303, master compressor, no
other effect. No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

TH = (0, 32, 64, 96, 127)
FAD = (127, 100, 80, 60, 40)

def cases():
    out = []
    for v in FAD:
        out.append((f"off_v{v:03d}", None, v, f"comp OFF, fader {v}"))
    for t in TH:                                    # make-up probe (ratio 0)
        out.append((f"mk_r000_t{t:03d}", C(0, t), 127,
                    f"ratio 0 (bypass), threshold {t}, fader 127"))
    for t in TH:                                    # threshold law at ratio 64
        out.append((f"xr_r064_t{t:03d}", C(64, t), 127,
                    f"ratio 64, threshold {t}, fader 127"))
    for v in FAD:                                   # transfer curve, mid settings
        out.append((f"xf_r064_t064_v{v:03d}", C(64, 64), v,
                    f"ratio 64, threshold 64, fader {v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law3"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, vol, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 3 (2026-09-16)", "",
           "Make-up probe (ratio 0), threshold law at a second ratio, and a full",
           "transfer curve. One 303, master compressor, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
