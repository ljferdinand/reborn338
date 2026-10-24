#!/usr/bin/env python3
"""Batch B8: the two WAVEFORMS, measured with the filter driven out of the way.

The 2026-08-30 listening songs found something no fixture in this project could
have: our SAW runs 2 to 4 dB darker than ReBirth's while our SQUARE is within
half a dB. Every 303 fixture in the set runs the saw, so the square has never
been a control, and a defect living in one waveform is invisible when only one
is measured.

This batch is B5's open-reference design in BOTH waveforms. Cutoff 127 with env
mod 127 and decay 127, read in the first quarter second while the envelope is
still open, which puts our corner near 12 kHz and leaves the oscillator itself as
the only thing shaping the spectrum below about 2 kHz. Six pitches a whole tone
apart, because the 2026-08-29 rounds showed a difference can be fixed in
frequency rather than in harmonic number and one pitch cannot tell those apart.

Twelve fixtures. The comparison that matters is not ours against ReBirth on each
waveform separately, it is whether the SAW-to-SQUARE relationship is the same on
both sides, which needs no cross-side level calibration at all.

    python gen_303_b8.py --out-dir ../../../fixtures-2026-08-30/b8-303-wave
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS = 120.0, 4
NOTES = (0, 2, 4, 6, 8, 10)
WAVES = ((0, "saw"), (1, "sqr"))


def drone(note):
    """Slide on every step: the 303 never retriggers, so the filter is static."""
    return [G.step(note=note, slide=True) for _ in range(16)]


def knobs_open(waveform):
    # Cutoff wide open and the envelope holding it there, so the filter is as
    # far out of the way as this instrument can put it.
    return dict(tune=64, cutoff=127, reso=0, envmod=127, decay=127,
                accent=0, waveform=waveform)


def knobs_engaged(waveform):
    """The SAME comparison with the filter in the path, which is the control the
    open condition needs and did not have.

    The open cells put our oscillator 4.45 dB short of ReBirth's in harmonics
    relative to the fundamental, and the two constants that fix that are rejected
    by the regression net at +0.776 with 286 of 373 cells worse. Those two
    measurements cannot both be describing the same thing, and the one difference
    between them is that B8's open cells are the only fixtures in the project
    that measure with the filter driven out of the way while every set on the net
    has it engaged. So: engage it, statically, and ask the same question.

    If the gap survives here it is the oscillator and the fix is being applied in
    the wrong place. If it collapses here then "open" is not the same state on
    both sides and the finding belongs to the envelope at its extreme."""
    return dict(tune=64, cutoff=64, reso=16, envmod=0, decay=64,
                accent=0, waveform=waveform)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-30", "b8-303-wave"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for kind, kfn, why in (("", knobs_open, "filter driven open"),
                           ("eng_", knobs_engaged, "filter ENGAGED at cutoff 64")):
        for w, wn in WAVES:
            for n in NOTES:
                name = "b8_%s%s_n%02d" % (kind, wn, n)
                data = G.build_song(kfn(w), drone(n), tempo=TEMPO, n_bars=BARS,
                                    title=name, author="rbsgen", device="bass1")
                open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
                rows.append((name, "%s at note %d, %s" % (wn, n, why)))
    print("wrote %d fixtures to %s" % (len(rows), a.out_dir))

    man = ["# 303 batch B8, 2026-08-30", "",
           "The two waveforms with the filter driven out of the way: cutoff 127,",
           "env mod 127, decay 127, read in the first quarter second while the",
           "envelope is still open. Six pitches a whole tone apart in each",
           "waveform, one 303, no effects.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt`.", "",
           "The comparison that needs no cross-side calibration is the",
           "SAW-to-SQUARE ratio on each side.", "",
           "| fixture | what it pins down |", "|---|---|"]
    for name, why in rows:
        man.append("| `%s` | %s |" % (name, why))
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
