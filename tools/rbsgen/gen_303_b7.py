#!/usr/bin/env python3
"""Batch B7: the accent knob CROSSED with cutoff and resonance.

WHY THIS BATCH EXISTS. On 2026-08-29 two measurements of the same quantity, the
accent's brightness law across its knob, disagreed by a factor of three:

    span of the accent's brightness effect, knob 0 to 127
    batch B3   (cutoff 48, resonance 48)   ReBirth 13.6 dB, ours 8.4   deficit 5.2
    accentdepth (cutoff 64, resonance 60)  ReBirth 11.0 dB, ours 9.3   deficit 1.7

Fitting the first takes `ACC_SHORT` 0.34 to 0.26 and `ACC_SWEEP` 3.30 to 5.10 and
improves B3's law from 2.663 to 0.687 dB rms. The regression net then rejects it
outright: accentdepth +1.827, accentprobe +1.656, accentrate +0.733. No single
triple satisfies both patches, which was checked over a spread of candidates
rather than assumed.

So the accent's knob law is NOT the same at every operating point, and the
constant that would carry that, `ACC_RESO_SLOPE`, is the least tested thing in
the accent path. docs/MEASUREMENT_PROTOCOL.md already says what to do here, from the
round-2 accent finding: **cross the sweep with anything it might interact with
rather than varying two things at once.** B3 and accentdepth differ in BOTH
cutoff and resonance, so neither can attribute the difference to either.

THE DESIGN. The accent knob at five points, crossed against resonance at one
cutoff and against cutoff at one resonance, so each axis is separable:

    knob x resonance   cutoff 64,  resonance 16 / 60 / 100
    knob x cutoff      resonance 60, cutoff 32 / 96

Every (cutoff, resonance) pair gets its OWN no-accent control with the identical
pattern, because an accent knob at zero is not an unaccented pattern and the
denominator has to differ from the numerator in exactly one thing.

Accents on steps 4 and 12, the same placement as B3, so the two batches measure
the same way and can be compared directly.

    python gen_303_b7.py --out-dir ../../../fixtures-2026-08-29/b7-303-accent
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS, NOTE = 120.0, 4, 0
KNOBS = (0, 32, 64, 96, 127)
ACC_STEPS = (4, 12)
# (cutoff, resonance). The first three vary resonance at one cutoff, the last
# two vary cutoff at one resonance, and (64, 60) is the point they share.
CELLS = [(64, 16), (64, 60), (64, 100), (32, 60), (96, 60)]
# Env mod and decay mid-scale and identical everywhere, so the filter envelope
# is a constant background and only the accent moves.
EM, DEC = 64, 64


def pattern(accents=()):
    return [G.step(note=NOTE, accent=(i in accents)) for i in range(16)]


def knobs(cut, reso, accent):
    return dict(tune=64, cutoff=cut, reso=reso, envmod=EM, decay=DEC,
                accent=accent, waveform=0)


def cases():
    for c, r in CELLS:
        yield ("b7_c%03d_r%03d_plain" % (c, r), knobs(c, r, 0), (),
               "no-accent control for cutoff %d resonance %d" % (c, r))
        for a in KNOBS:
            yield ("b7_c%03d_r%03d_k%03d" % (c, r, a), knobs(c, r, a), ACC_STEPS,
                   "accent knob %d at cutoff %d resonance %d" % (a, c, r))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "b7-303-accent"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, k, acc, why in cases():
        data = G.build_song(k, pattern(acc), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
    print("wrote %d fixtures to %s" % (len(rows), a.out_dir))
    man = ["# 303 batch B7, 2026-08-29", "",
           "The accent knob CROSSED with cutoff and resonance, because two",
           "measurements of the accent's knob law disagreed by a factor of three",
           "and the two patches differed in BOTH cutoff and resonance, so",
           "neither could attribute the difference to either.", "",
           "One 303, no effects, accents on steps 4 and 12, the same placement",
           "as B3 so the two batches compare directly. Every cell has its own",
           "no-accent control with the identical pattern.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt`.", "",
           "| fixture | what it pins down |", "|---|---|"]
    for name, why in rows:
        man.append("| `%s` | %s |" % (name, why))
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
