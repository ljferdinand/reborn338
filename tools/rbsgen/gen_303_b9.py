#!/usr/bin/env python3
"""Batch B9: the accent knob CROSSED with ENV MOD, at the tour's ACCENT corner.

WHY THIS BATCH EXISTS. On 2026-09-03 Luke heard the tour saw's ACCENT sweep as
less "chirpy" than ReBirth, and the measurement on the tour (BASS303_FINDINGS
section 21) put numbers on it: as the accent knob ramps 0 to 127, ReBirth's
centroid climbs to ~2600-2800 Hz where ours reaches ~2100-2400, so ours is about
0.2 to 0.3 octaves short at PEAK accent. Level tracked within a dB, so it is
brightness, not loudness.

That was measured on a SONG, which cannot isolate anything, so it identified the
gap but cannot be tuned against. Batch B7 (2026-08-29) is the proper isolated
accent grid, and it found `ACC_SWEEP` 3.30 already optimal on its 25-cell
surface. The two disagree, and the reason is the one axis B7 held fixed:

    B7 crossed accent x cutoff and accent x resonance, with ENV MOD = 64 fixed.

The tour's ACCENT sweep does NOT run at env mod 64. Tracing the tour knob state
into bars 20-24 (the ACCENT section, ~36.9 to 44.3 s at 130 BPM), the knobs carry
in from the previous sweeps at:

    cutoff 60, resonance 85, ENV MOD 90, decay 55, accent sweeping 0 -> 127.

So the accent's corner push is stacking on a HIGH env-mod corner push, an
interaction B7 could not see because it held env mod at 64. This batch crosses
the accent knob against env mod at that fixed corner to answer one question:

    does the accent's brightness shortfall OPEN UP as env mod rises from B7's 64
    to the tour's 90 and beyond? If yes, the gap is an env-mod x accent
    interaction and the fix needs an env-mod term, not a bigger ACC_SWEEP. If the
    shortfall is already there at env mod 64, then B7 should have caught it and
    the conflict is in the metric, not the operating point.

THE DESIGN. Fixed at the tour's ACCENT corner (cutoff 60, resonance 85, decay 55,
saw). The accent knob at five points crossed against env mod at three levels:

    env mod 64   (B7's level, the bridge)
    env mod 90   (the tour's ACCENT-sweep level, the reproduction)
    env mod 127  (the extreme, to see the trend)

Every env-mod level gets its OWN no-accent control with the identical pattern, so
the denominator differs from the numerator in exactly one thing: the accents on
steps 4 and 12. Same accent placement as B7 and B3, so the differential centroid
bridges directly to those batches.

    python gen_303_b9.py --out-dir ../../../fixtures-2026-09-04/b9-303-accent-corner
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# 130 BPM and saw, matching the tour ACCENT section this reproduces, not B7's
# 120/note-0 defaults. The differential (accented minus plain) cancels tempo,
# note and pattern, so the bridge to B7 survives the change.
TEMPO, BARS, NOTE, WAVE = 130.0, 4, 0, 0
KNOBS = (0, 32, 64, 96, 127)
ACC_STEPS = (4, 12)
# The tour's ACCENT-sweep corner, held fixed.
CUT, RESO, DEC = 60, 85, 55
# The new axis: env mod. 64 bridges to B7, 90 reproduces the tour, 127 is the
# extreme that shows which way the shortfall trends.
EMS = (64, 90, 127)


def pattern(accents=()):
    return [G.step(note=NOTE, accent=(i in accents)) for i in range(16)]


def knobs(em, accent):
    return dict(tune=64, cutoff=CUT, reso=RESO, envmod=em, decay=DEC,
                accent=accent, waveform=WAVE)


def cases():
    for em in EMS:
        yield ("b9_em%03d_plain" % em, knobs(em, 0), (),
               "no-accent control for env mod %d" % em)
        for a in KNOBS:
            yield ("b9_em%03d_k%03d" % (em, a), knobs(em, a), ACC_STEPS,
                   "accent knob %d at env mod %d" % (a, em))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-09-04", "b9-303-accent-corner"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, k, acc, why in cases():
        data = G.build_song(k, pattern(acc), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
    print("wrote %d fixtures to %s" % (len(rows), a.out_dir))
    man = ["# 303 batch B9, 2026-09-04", "",
           "The accent knob CROSSED with ENV MOD, at the tour's ACCENT-sweep",
           "corner (cutoff %d, resonance %d, decay %d, saw). Batch B7 held env" % (CUT, RESO, DEC),
           "mod at 64 and found ACC_SWEEP optimal; the tour ACCENT sweep runs at",
           "env mod 90, where the accent's corner push stacks on a high env-mod",
           "push. This batch tests whether the accent brightness shortfall Luke",
           "heard on the tour (section 21) opens up as env mod rises from 64 to",
           "90 to 127, which would make it an env-mod x accent interaction rather",
           "than a bigger ACC_SWEEP.", "",
           "One 303, no effects, accents on steps 4 and 12, same placement as B7",
           "and B3 so the differential centroid bridges directly. Every env-mod",
           "level has its own no-accent control with the identical pattern.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt`. Tempo %g." % TEMPO, "",
           "| fixture | what it pins down |", "|---|---|"]
    for name, why in rows:
        man.append("| `%s` | %s |" % (name, why))
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
