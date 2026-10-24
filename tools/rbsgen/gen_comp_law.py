#!/usr/bin/env python3
"""Compressor LAW campaign, batch 1 (clean-room, supertask 6, 2026-09-16).

P8 (gen_comp_probe.py) confirmed byte 1 = Ratio and that byte 2 is
threshold-related, but the -25.8 dBFS drone was too quiet: the compressor bites
on loud material, so above the lowest threshold the drone sat below it and the
gain reduction was under 1 dB. The follow-up PLAN_COMP.md asks for a LOUDER
source and an INPUT-LEVEL dimension (source level x threshold x ratio).

Method here: keep the sustained 303 drone, but SWEEP the channel fader (chan_vol)
so the source moves up and down through a fixed threshold. At a low, engaged
threshold and a high ratio, gain reduction then traces GR vs (level - threshold)
from the loud side, which is a real segment of the static curve even though a
single 303 cannot reach 0 dBFS. Comp-off references are taken at every fader so
the true input level (dBFS) of each point is known, not assumed. Accent (knob +
per-step) gives one extra-loud point to see how far up the curve we can reach.

One 303, master compressor, no other effect. Gain reduction = level(on) vs the
matching level(off) at the same fader. No value from any binary analysis is used.

Fixtures (all: saw, cutoff 127, reso 0, tune 64; master comp; 4 bars @ 120 BPM):

  off_vNNN     comp OFF, fader NNN                 -> input-level calibration
  off_acc      comp OFF, accent, fader 127         -> loudest source we can make
  rRRR_tTTT_vNNN   comp ON ratio RRR threshold TTT fader NNN
  rRRR_tTTT_acc    comp ON, accented loud source
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# Sustained, un-retriggered, un-resonant source so "level" is well defined.
SRC     = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0,   waveform=0)
SRC_ACC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=127, waveform=0)

def drone(accent=False, length=16):
    return [G.step(note=0, slide=True, accent=accent) for _ in range(length)]

def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)   # -1 = master

def cases():
    out = []
    # ---- comp-OFF references: the input-level (fader) calibration ----
    for v in (127, 100, 80, 60, 40):
        out.append((f"off_v{v:03d}", SRC, False, None, v,
                    f"comp OFF, fader {v} (input-level ref)"))
    out.append(("off_acc", SRC_ACC, True, None, 127,
                "comp OFF, accent, fader 127 (loudest source)"))
    # ---- static curve: ratio 127 (max), threshold 0 (lowest), fader sweep ----
    for v in (127, 100, 80, 60, 40):
        out.append((f"r127_t000_v{v:03d}", SRC, False, C(127, 0), v,
                    f"ratio127 thr0, fader {v}"))
    out.append(("r127_t000_acc", SRC_ACC, True, C(127, 0), 127,
                "ratio127 thr0, accented loud source"))
    # ---- threshold 32 arm (does a higher threshold still engage?) ----
    for v in (127, 100, 80, 60):
        out.append((f"r127_t032_v{v:03d}", SRC, False, C(127, 32), v,
                    f"ratio127 thr32, fader {v}"))
    # ---- ratio 64 arm (mid ratio -> shallower slope) at threshold 0 ----
    for v in (127, 100, 80, 60):
        out.append((f"r064_t000_v{v:03d}", SRC, False, C(64, 0), v,
                    f"ratio64 thr0, fader {v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-law"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, src, acc, comp, vol, why in cases():
        data = G.build_song(src, drone(accent=acc), tempo=120.0, n_bars=4,
                            title=name, author="rbsgen", device="bass1",
                            comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor LAW campaign, batch 1 (2026-09-16)", "",
           "Sustained 303 drone through the MASTER compressor. The channel fader",
           "(chan_vol) is swept so the source moves through a fixed threshold;",
           "gain reduction = level(on) vs the matching `off_vNNN` (comp off) at",
           "the same fader. One 303, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
