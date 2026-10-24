#!/usr/bin/env python3
"""Batch B1: the 303 filter's static response, over the cutoff x resonance plane.

The first round of the 303 work Luke reopened on 2026-08-29, and the foundation
under everything else: three of the four effects sit on a 303 in every shipped
song, so an effect cannot be judged through a source that is not right.

TARGET IS THE NATIVE TPT VOICE. Luke's call the same day: Open303 stays as close
to its own source as possible and is not to be bent toward ReBirth, so parity
work happens on our own filter. Render these with `REBORN_BASS_ENGINE=tpt`.
Whether Open303 or TPT ships as the default is a separate and open question.

WHY THIS GRID AND NOT ANOTHER. `COMPONENT_MATRIX.md` records cutoff as a
four-point sweep at note A only, and resonance as eight static points also at
note A. That is one line through a two-dimensional surface, and the surface is
known not to be separable: the shelved diode-ladder plan
(`PLAN_303_FILTER_TRACKB.md`) concluded that what is needed is "a RESO-and-
cutoff-dependent K_B refit", which is precisely a fit that a one-dimensional
sweep cannot supply. So this measures the plane.

    cutoff  0, 16, 32, 48, 64, 80, 96, 112, 127     nine points, both extremes
    reso    0, 32, 64, 96, 127                      five points, both extremes

Forty-five fixtures, plus a second note at a nine-point subset to test the
note-dependence the matrix flags as never checked, plus one unfiltered reference.

THE SOURCE IS A DRONE: one note held across all sixteen steps by putting SLIDE on
every step, which is the manual's own recipe (p61) and what the PCF rounds used.
It never retriggers, so the filter envelope and the VCA stay put and what is
measured is the filter's STATIC response with nothing moving. Env mod is zero and
accent is zero for the same reason. The envelope, the accent and the decay are
batch B2 and are deliberately not crossed with this.

A low note is deliberate: note 0 puts partials every ~65 Hz, dense enough to fit
a curve anywhere in the audible range, and it is the register these lines live in.

Every fixture passes `isolation_check.py --device bass1 --effect none`.

    python gen_303_b1.py --out-dir ../../../fixtures-2026-08-29/b1-303-filter
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS = 120.0, 4

CUTOFF = (0, 16, 32, 48, 64, 80, 96, 112, 127)
RESO = (0, 32, 64, 96, 127)
# The second note, to test whether the law is note-dependent at all. The matrix
# has every cutoff and resonance point measured at one note, so a difference
# here would invalidate more than it looks like.
NOTE_B = 7
CUTOFF_B = (32, 64, 96)
RESO_B = (0, 64, 127)


def drone(note=0, length=16):
    """One held note: slide on every step means the 303 never retriggers."""
    return [G.step(note=note, slide=True) for _ in range(length)]


def knobs(cutoff, reso, waveform=0):
    # Env mod and accent at zero so nothing modulates the corner. Decay is
    # irrelevant on a drone and is parked mid-scale rather than at an extreme.
    return dict(tune=64, cutoff=cutoff, reso=reso, envmod=0, decay=64,
                accent=0, waveform=waveform)


def cases():
    # A DENOMINATOR MUST DIFFER FROM ITS NUMERATOR IN EXACTLY ONE THING. The
    # first run of this batch had only the note-0 reference and measured the
    # note-7 fixtures against it, so the reference's harmonic grid was not the
    # fixture's and the ratio meant nothing: the corner pinned at 320 Hz and the
    # peak read 74 to 84 dB at every setting. Second reference-design error of
    # the week, after a PCF reference that had the unit on at zero depth.
    out = [("b1_ref", None, 0, "unfiltered denominator at note 0"),
           ("b1_n7_ref", None, NOTE_B, "unfiltered denominator at note 7")]
    for c in CUTOFF:
        for r in RESO:
            out.append(("b1_c%03d_r%03d" % (c, r), (c, r), 0,
                        "static response at cutoff %d, resonance %d" % (c, r)))
    for c in CUTOFF_B:
        for r in RESO_B:
            out.append(("b1_n7_c%03d_r%03d" % (c, r), (c, r), NOTE_B,
                        "the same at note 7, to test note-dependence"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "b1-303-filter"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, cr, note, why in cases():
        k = knobs(127, 0) if cr is None else knobs(cr[0], cr[1])
        data = G.build_song(k, drone(note), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
        print("  %s.rbs  %d bytes" % (name, len(data)))

    man = ["# 303 batch B1, 2026-08-29", "",
           "The 303 filter's STATIC response over the cutoff x resonance plane.",
           "One 303, no effects, a held drone on note 0 (and note 7 for the",
           "note-dependence subset). Env mod and accent at zero so nothing",
           "modulates the corner.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt`: parity is the native",
           "voice, not Open303, per Luke on 2026-08-29.", "",
           "Every measurement is a ratio against `b1_ref`, which is the same",
           "song with the filter wide open and no resonance, so the oscillator",
           "and the VCA divide out and what is left is the filter.", "",
           "| fixture | what it pins down |", "|---|---|"]
    man += ["| `%s` | %s |" % (n, w) for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print("\n%d fixtures + MANIFEST.md in %s" % (len(rows), a.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
