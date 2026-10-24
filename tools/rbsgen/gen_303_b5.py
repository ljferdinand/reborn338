#!/usr/bin/env python3
"""Batch B5: the 303 filter measured on a DENSE frequency grid, with a
reference that is actually open.

TWO THINGS B1 COULD NOT DO, both of which cost a wrong finding on 2026-08-29.

**The grid was too coarse.** Every filter measurement in this project reads the
response off the partials of one held note, and one note at 65 Hz gives partials
at 65, 129, 194, 231 and up. A statistic that needs a passband runs out of
partials below the corner, which is exactly how B1's corner reading came to say
0.284 octaves when two passband-free measures say 0.08. Six DIFFERENT PITCHES a
whole tone apart interleave six harmonic series, and the union is dense where one
series is sparsest. Nothing about the filter changes between them, so they pool.

**The reference was not open.** `b1_ref` is cutoff 127 with resonance 0, and the
top of the cutoff knob is only about 2.2 kHz, so the "unfiltered" denominator is
a lowpass sitting inside the audio band. Dividing by it is dividing one filter
response by another, which is the error this project has now made four times.
`b5_ref_open` drives the corner far above the band with the ENVELOPE instead:
cutoff 127, env mod 127, decay 127, read in the first quarter second while the
envelope is still open. `b5_ref_flat` is B1's old reference at the same pitches,
so the difference between the two can be measured rather than assumed.

The drone slides on every step, so the 303 never retriggers and the filter is
static for the whole render, which is what makes a steady-state read legal.

    python gen_303_b5.py --out-dir ../../../fixtures-2026-08-29/b5-303-grid
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS = 120.0, 4

# Whole tones across an octave. Six series interleave to a grid whose widest gap
# near the fundamental is about 11 Hz instead of 65.
NOTES = (0, 2, 4, 6, 8, 10)

# The resonance axis at one cutoff, plus two cells off it to catch any cutoff
# dependence. Deliberately narrow: B1 already has the whole plane coarsely, and
# this batch is here to make one axis of it trustworthy rather than to redo it.
CELLS = [
    ("c064_r000", 64, 0),
    ("c064_r064", 64, 64),
    ("c064_r096", 64, 96),
    ("c064_r127", 64, 127),
    ("c032_r096", 32, 96),
    ("c112_r096", 112, 96),
    # Every resonant cell needs its own resonance-0 partner at the SAME cutoff,
    # or the reference-free comparison (fixture over reso 0) has no denominator
    # for it. Added after the first export run, where c032 and c112 had none.
    ("c032_r000", 32, 0),
    ("c112_r000", 112, 0),
]


def drone(note):
    """Slide on every step: the 303 never retriggers, so the filter is static."""
    return [G.step(note=note, slide=True) for _ in range(16)]


def knobs(cutoff, reso, envmod=0, decay=64):
    return dict(tune=64, cutoff=cutoff, reso=reso, envmod=envmod, decay=decay,
                accent=0, waveform=0)


def cases():
    for n in NOTES:
        yield ("b5_ref_open_n%02d" % n, knobs(127, 0, envmod=127, decay=127), n,
               "OPEN reference, corner driven above the band by the envelope")
        yield ("b5_ref_flat_n%02d" % n, knobs(127, 0), n,
               "B1's old reference at the same pitch, for comparison")
        for name, c, r in CELLS:
            yield ("b5_%s_n%02d" % (name, n), knobs(c, r), n,
                   "cutoff %d resonance %d" % (c, r))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "b5-303-grid"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, k, note, why in cases():
        data = G.build_song(k, drone(note), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
    print("wrote %d fixtures to %s" % (len(rows), a.out_dir))

    man = ["# 303 batch B5, 2026-08-29", "",
           "The filter on a DENSE frequency grid, with a reference that is",
           "actually open. One 303, no effects, a held drone that slides on",
           "every step so the instrument never retriggers.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt`.", "",
           "**Six pitches, a whole tone apart.** One note gives partials 65 Hz",
           "apart and a statistic that needs a passband runs out of them below",
           "the corner. Six interleaved series close that gap to about 11 Hz.",
           "Nothing about the filter changes between pitches, so they pool.", "",
           "**Two references.** `b5_ref_open` drives the corner above the band",
           "with the envelope (cutoff 127, env mod 127, decay 127) and is read",
           "in the first quarter second. `b5_ref_flat` is B1's old reference,",
           "cutoff 127 at env mod 0, whose corner is only about 2.2 kHz. Both",
           "are here so the difference can be MEASURED rather than assumed.", "",
           "| fixture | what it pins down |", "|---|---|"]
    for name, why in rows:
        man.append("| `%s` | %s |" % (name, why))
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
