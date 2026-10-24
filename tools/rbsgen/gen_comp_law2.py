#!/usr/bin/env python3
"""Compressor LAW campaign, batch 2 (clean-room, supertask 6, 2026-09-16).

Batch 1 (gen_comp_law.py) proved the loud source and the input-level lever and
showed the threshold/ratio/knee/make-up are coupled: ratio 127 and ratio 64
imply different threshold dBFS for the same byte. This batch measures dense LINES
through the surface so each law can be read in isolation:

  ts_r127_tTTT  fader 127, ratio 127, threshold swept  -> threshold(byte) -> dBFS
                (at ratio 127 the output pins to the threshold, reading it off)
  rs_rRRR_t000  fader 127, threshold 0, ratio swept    -> slope(byte)
                (input -15.8 dBFS sits well above the byte-0 threshold)
  bt_r127_tTTT  fader 60 (-40.6 dBFS), ratio 127, threshold swept -> is a
                below-threshold signal boosted toward the threshold (make-up /
                feedback leveller) or passed through (downward-only)?

off_v127 / off_v060 are the comp-off references for GR at each fader. One 303,
master compressor, no other effect. No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

SWEEP = (0, 16, 32, 48, 64, 80, 96, 112, 127)
BT_TH = (0, 32, 64, 96, 127)

def cases():
    out = [("off_v127", None, 127, "comp OFF, fader 127 (hot ref)"),
           ("off_v060", None,  60, "comp OFF, fader 60 (quiet ref)")]
    for t in SWEEP:                                  # threshold law
        out.append((f"ts_r127_t{t:03d}", C(127, t), 127,
                    f"threshold {t} at ratio 127, fader 127"))
    for r in SWEEP:                                  # ratio law
        out.append((f"rs_r{r:03d}_t000", C(r, 0), 127,
                    f"ratio {r} at threshold 0, fader 127"))
    for t in BT_TH:                                  # below-threshold / make-up
        out.append((f"bt_r127_t{t:03d}", C(127, t), 60,
                    f"threshold {t} at ratio 127, fader 60 (below-thr probe)"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law2"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, vol, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 2 (2026-09-16)", "",
           "Dense lines through the threshold/ratio surface + a below-threshold",
           "probe. One 303, master compressor, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
