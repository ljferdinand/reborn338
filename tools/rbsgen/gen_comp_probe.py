#!/usr/bin/env python3
"""P8 compressor probe (clean-room, 2026-09-10).

A loud, sustained 303 drone (slide on every step so the filter never retriggers)
through the MASTER compressor, with the two candidate control bytes swept. The
COMP chunk in a shipped song (Standard Components/Demo.rbs) is [1, 70, 64, 127,
..] and the manual lists exactly two controls (Ratio 106, Threshold 107); the
HYPOTHESIS is byte 1 = Ratio, byte 2 = Threshold. This probe decides it from the
audio: if sweeping byte 1 changes the amount of gain reduction (ratio) and byte 2
changes the level at which reduction begins (threshold), the assignment holds; if
the behaviours swap, so does the assignment.

Gain reduction is read as the drone's output level with the compressor on versus
off (`c_ref`). Comp is on the master; everything else off. No value from any
binary analysis is used; the sweeps just vary the bytes and the finding comes
from the audio.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# Loud sustained source: 303 drone, cutoff wide open.
SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(note=0, length=16):
    return [G.step(note=note, slide=True) for _ in range(length)]

def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)   # -1 = master strip

def cases():
    out = [("c_ref", None, "denominator: the drone, compressor OFF")]
    # Ratio sweep at a low (engaged) threshold.
    for r in (0, 32, 64, 96, 127):
        out.append((f"c_r{r:03d}", C(r, 32), f"ratio byte {r} at threshold 32"))
    # Threshold sweep at a strong ratio (drop 32, covered by c_r096).
    for t in (0, 64, 96, 127):
        out.append((f"c_t{t:03d}", C(96, t), f"threshold byte {t} at ratio 96"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-10", "comp-probe"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, why in cases():
        data = G.build_song(SRC, drone(0), tempo=120.0, n_bars=4, title=name, author="rbsgen",
                            device="bass1", comp=comp)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# P8 compressor probe, 2026-09-10", "",
           "Loud sustained 303 drone through the MASTER compressor, candidate",
           "Ratio (byte 1) and Threshold (byte 2) swept. Gain reduction = level",
           "vs `c_ref` (comp off). Everything else off.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
