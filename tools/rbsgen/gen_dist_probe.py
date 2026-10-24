#!/usr/bin/env python3
"""Distortion byte-ID + character probe, batch 1 (clean-room, supertask 7, 2026-09-17).

The DIST chunk has two knob bytes; Appendix C lists Shape 104 then Amount 105, but
the file byte order is not the MIDI order elsewhere in this format, so which byte
is Amount ("dirtier") and which is Shape ("the character, grungier") is a
measurement. The current loader ASSUMES byte1=Amount, byte2=Shape; this probe
tests it and characterises what each byte does to the spectrum.

A 303 saw drone (cutoff wide open, slid so it never retriggers) through ONE
section's distortion, everything else off (PCF off, compressor off, delay send 0).
byte1 swept with byte2 fixed, and byte2 swept with byte1 fixed; d_off is the clean
(dist-off) denominator. The byte that changes overall drive/dirtiness vs the byte
that changes the harmonic CHARACTER identifies Amount vs Shape. No value from any
binary analysis is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# Rich, steady source so the distortion has harmonics to reshape.
SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]

def D(b1, b2):
    # dist on the sounding section (bass1 = channel 0); one unit, no other effect.
    return dict(on=1, b1=b1, b2=b2, sections=(1, 0, 0, 0))

def cases():
    out = [("d_off", None, "clean drone, distortion OFF (denominator)")]
    for v in (0, 32, 64, 96, 127):                 # byte1 sweep, byte2 fixed 64
        out.append((f"d_b1{v:03d}", D(v, 64), f"DIST byte1={v}, byte2=64"))
    for v in (0, 32, 64, 96, 127):                 # byte2 sweep, byte1 fixed 96
        out.append((f"d_b2{v:03d}", D(96, v), f"DIST byte1=96, byte2={v}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-17", "dist-probe"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, dist, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", dist=dist)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Distortion byte-ID + character probe, batch 1 (2026-09-17)", "",
           "303 saw drone through ONE section's distortion, everything else off.",
           "byte1 and byte2 swept independently to identify Amount vs Shape and",
           "characterise each. `d_off` is the clean denominator.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
