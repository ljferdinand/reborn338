#!/usr/bin/env python3
"""Distortion transfer-curve probe, batch 3 (clean-room, supertask 7, 2026-09-17).

byte1 = Shape, byte2 = Amount (batch 2). To reverse-engineer the static
waveshaping function, feed a NEAR-SINE (a heavily-lowpassed 303 saw, mostly its
fundamental) so the distortion's output harmonics are clean harmonics of one
tone; their amplitudes are the transfer curve's harmonic decomposition.

  cs_a{AA}      Amount (byte2) swept at Shape (byte1) = 0 -> the drive/curve family
  cs_s{SS}      Shape (byte1) swept at Amount (byte2) = 96 -> what Shape does to it
  cs_off        clean near-sine, distortion OFF (denominator; also a sine-quality
                check; its harmonics above the fundamental should be small)

303 saw, cutoff low (near-sine), slid drone, through ONE section's distortion,
everything else off. No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# Near-sine: heavily lowpassed saw. accent on to keep enough level to drive.
SRC = dict(tune=64, cutoff=40, reso=0, envmod=0, decay=64, accent=100, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True, accent=True) for _ in range(length)]
def D(b1, b2):
    return dict(on=1, b1=b1, b2=b2, sections=(1, 0, 0, 0))

def cases():
    out = [("cs_off", None, "clean near-sine, distortion OFF (denominator)")]
    for a in (0, 16, 32, 48, 64, 96, 127):         # Amount (byte2) at Shape 0
        out.append((f"cs_a{a:03d}", D(0, a), f"Amount(byte2)={a}, Shape(byte1)=0"))
    for s in (0, 32, 64, 96, 127):                 # Shape (byte1) at Amount 96
        out.append((f"cs_s{s:03d}", D(s, 96), f"Shape(byte1)={s}, Amount(byte2)=96"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-17", "dist-curve"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, dist, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", dist=dist)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Distortion transfer-curve probe, batch 3 (2026-09-17)", "",
           "Near-sine 303 through one section's distortion. Amount (byte2) swept",
           "at Shape 0, and Shape (byte1) swept at Amount 96, for clean harmonic",
           "amplitudes. cs_off = clean denominator.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
