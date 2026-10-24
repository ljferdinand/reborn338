#!/usr/bin/env python3
"""Distortion byte1 x byte2 grid, batch 2 (clean-room, supertask 7, 2026-09-17).

Batch 1 showed ReBirth's distortion adds +17..+50 dB of 2-16 kHz harmonics (ours
is far softer), and that byte2 raises brightness monotonically while byte1 does
not, but neither sweep isolated a byte from zero, so which byte is Amount
("dirtier") and which is Shape ("the character, grungier") is still open. This
batch is the full 5x5 grid of both bytes from 0, so each byte's effect is seen
holding the other, including the both-zero corner (does the distortion vanish, or
is it always shaping when on?).

303 saw drone (cutoff wide open, slid) through ONE section's distortion,
everything else off. d_off is the clean denominator. No binary-analysis value.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def D(b1, b2):
    return dict(on=1, b1=b1, b2=b2, sections=(1, 0, 0, 0))

BYTES = (0, 32, 64, 96, 127)

def cases():
    out = [("d_off", None, "clean drone, distortion OFF (denominator)")]
    for b1 in BYTES:
        for b2 in BYTES:
            out.append((f"g_b1{b1:03d}_b2{b2:03d}", D(b1, b2), f"byte1={b1}, byte2={b2}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-17", "dist-grid"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, dist, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", dist=dist)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Distortion byte1 x byte2 grid, batch 2 (2026-09-17)", "",
           "303 drone through one section's distortion; full 5x5 byte grid to",
           "identify Amount vs Shape and characterise both. d_off = clean.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
