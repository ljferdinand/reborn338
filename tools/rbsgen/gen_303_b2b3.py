#!/usr/bin/env python3
"""Batches B2 and B3: the 303's filter envelope, and its accent.

B1 measured the filter standing still. These two measure it moving, which is
where the instrument's character actually lives and where the matrix is thinnest:
env mod is recorded as a four-point fit, decay as three points, the accent knob
as "on/off only", and accent ACCUMULATION across consecutive accented steps as
never tested at all.

TARGET IS THE NATIVE TPT VOICE (`REBORN_BASS_ENGINE=tpt`), per Luke 2026-08-29.
One 303 per fixture, never two. No effects. Every fixture passes
`isolation_check.py --device bass1 --effect none`.

THE SOURCE IS A PLUCKED NOTE, not B1's drone: one note on every sixteenth with
no slide, so the instrument retriggers each step and the filter envelope fires
each time. Same note throughout, so the harmonic grid the response is read off
never moves.

THE DENOMINATOR IS THE FILTER WIDE OPEN, playing the identical pattern. Not "the
same fixture with env mod at zero", which is a ratio of one filter response to
another and not a response at all. That mistake has now been made twice on this
project, on a PCF reference with the unit on at zero depth and on B1's note-7
subset, so it is worth stating as a rule: a denominator differs from its
numerator in exactly one thing, and that thing is the whole effect under test.

B2, the envelope. Env mod crossed with decay, five points each, with the cutoff
parked LOW so the whole excursion is upward and nothing is lost above the top of
the knob, and resonance low so the corner is a clean rolloff rather than a peak.

B3, the accent. Two questions the matrix says are open. The knob's DEPTH, swept
five ways against a pattern with a fixed accent placement. And ACCUMULATION:
whether consecutive accented steps stack, measured with runs of one, two, three
and four accents in a row against the same knob setting. If they stack, the
fourth is louder or brighter than the first; if they do not, all four match.

    python gen_303_b2b3.py --out-dir ../../../fixtures-2026-08-29/b2b3-303-env
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS = 120.0, 4
NOTE = 0

ENVMOD = (0, 32, 64, 96, 127)
DECAY = (0, 32, 64, 96, 127)
# Cutoff low so the envelope's sweep is entirely upward and never clips against
# the top of the range; resonance low so B1's peak defect does not contaminate a
# measurement about the envelope.
B2_CUT, B2_RESO = 24, 16

ACCENT_KNOB = (0, 32, 64, 96, 127)
# Mid-scale everything else, so the accent is the only thing moving.
B3_CUT, B3_RESO, B3_EM, B3_DEC = 48, 48, 64, 64
# Runs of consecutive accents. Each entry is the accented step indices in a
# sixteen-step bar, spaced so a run always starts after four clear steps.
RUNS = {
    "run1": (0,),
    "run2": (0, 1),
    "run3": (0, 1, 2),
    "run4": (0, 1, 2, 3),
}


def plucked(note=NOTE, length=16, accents=()):
    return [G.step(note=note, accent=(i in accents)) for i in range(length)]


def knobs(cut, reso, envmod, decay, accent):
    return dict(tune=64, cutoff=cut, reso=reso, envmod=envmod, decay=decay,
                accent=accent, waveform=0)


def cases():
    out = []
    # One reference for both batches: same pattern, filter wide open, no
    # resonance, no env mod, no accent.
    out.append(("b23_ref", knobs(127, 0, 0, 64, 0), (), "wide-open denominator"))

    for e in ENVMOD:
        for d in DECAY:
            out.append(("b2_e%03d_d%03d" % (e, d),
                        knobs(B2_CUT, B2_RESO, e, d, 0), (),
                        "envelope at env mod %d, decay %d" % (e, d)))

    # B3 needs its own reference: the accented pattern with the accent knob at
    # zero is NOT the same thing as an unaccented pattern, because the accent
    # row may do something even at knob zero. Both are measured.
    out.append(("b3_ref_plain", knobs(B3_CUT, B3_RESO, B3_EM, B3_DEC, 0), (),
                "the B3 patch with no accented steps at all"))
    for a in ACCENT_KNOB:
        out.append(("b3_k%03d" % a, knobs(B3_CUT, B3_RESO, B3_EM, B3_DEC, a),
                    (4, 12), "accent knob %d, two accented steps" % a))
    for name, acc in RUNS.items():
        out.append(("b3_%s" % name,
                    knobs(B3_CUT, B3_RESO, B3_EM, B3_DEC, 96), acc,
                    "accent accumulation: %d consecutive accents" % len(acc)))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "b2b3-303-env"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, k, acc, why in cases():
        data = G.build_song(k, plucked(accents=acc), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
        print("  %s.rbs  %d bytes" % (name, len(data)))

    man = ["# 303 batches B2 and B3, 2026-08-29", "",
           "The filter envelope (B2) and the accent (B3). One 303, no effects,",
           "a plucked note on every sixteenth so the envelope fires each step.",
           "", "Render OURS with `REBORN_BASS_ENGINE=tpt`.", "",
           "`b23_ref` is the denominator for both: the identical pattern with",
           "the filter wide open, no resonance, no env mod and no accent.", "",
           "| fixture | what it pins down |", "|---|---|"]
    man += ["| `%s` | %s |" % (n, w) for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print("\n%d fixtures + MANIFEST.md in %s" % (len(rows), a.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
