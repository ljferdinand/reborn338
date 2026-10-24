#!/usr/bin/env python3
"""Purpose-built SONGS for judging the 303 by ear, with nothing else in the path.

Luke, 2026-08-30: "write a song and output it to RBS format, one 303, no 808, no
909... the only goal is for me to test our 303 against ReBirth using only the
filters and knobs that are assigned to the ReBirth 303 engine before they hit
the fx chain. It should have all effects disabled."

WHY THESE ARE NOT FIXTURES. Every 303 fixture this project has made holds still
so a number can be read off it: one note, one pitch, one knob setting, a drone
or a plucked sixteenth. That is right for measuring and useless for listening,
and the last two days showed why it matters. Our filter, envelope, level and
accent all measure within a dB or two of ReBirth on a real bass line, and the
line still does not sound the same. These are written to be PLAYED, so an ear
has something to judge.

WHAT IS IN THE PATH. One 303. The other 303 silent, both drum machines silent,
the distortion off at the unit and at every section switch, the delay send zero
on every channel, the PCF routed nowhere, and the compressor track carrying an
explicit off snapshot. `isolation_check.py` proves that rather than this
docstring asserting it.

THE SET

  tour     32 bars that walk one knob at a time across its whole range while the
           line keeps playing: cutoff, then resonance, then env mod, then decay,
           then accent. Each sweep is four bars and rides on every sixteenth, so
           it is heard as a gesture and not as a series of steps.
  groove   16 bars at one good static patch with no automation at all. This is
           the one for judging the VOICE, with nothing moving to distract.

Both come in saw and square, because the waveform is the one 303 control that
changes the character rather than the shape of it.

    python gen_303_song.py --out-dir ../../../fixtures-2026-08-30/303-songs
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO = 130.0
TICKS_PER_BAR = 32
TICKS_PER_STEP = 2


def S(n, o=0, a=False, sl=False):
    return G.step(note=n, octave=o, accent=a, slide=sl)


def R():
    return G.step(rest=True)


# P0: the riff. Root A, a minor-third lift, rests that let the filter ring out.
P0 = [S(9, 0, True), R(), S(9, 0), S(9, 0, False, True),
      R(), S(0, 1), S(9, 0), R(),
      S(9, 0, True), R(), S(7, 0), S(9, 0, False, True),
      R(), S(5, 0), S(9, 0), R()]

# P1: the same shape accented hard, for the accent sweep to bite on.
P1 = [S(9, 0, True), R(), S(9, 0, True), S(9, 0, False, True),
      S(0, 1, True), S(0, 1), S(9, 0, True), R(),
      S(9, 0, True), S(7, 0, True), S(7, 0), S(9, 0, False, True),
      S(5, 0, True), S(5, 0), S(9, 0, True), R()]

# P2: octave jumps, which is what makes a 303 line read as a 303 line.
P2 = [S(9, -1, True), R(), S(9, 1), S(9, 0),
      S(9, -1), S(0, 1), S(9, 1, True), R(),
      S(9, -1, True), R(), S(7, 1), S(7, 0),
      S(9, -1), S(5, 1), S(9, 0, True), R()]

# P3: slides throughout. No retrigger, so the filter envelope rides across the
# phrase instead of restarting, which is the hardest thing in the instrument.
P3 = [S(9, 0, True), S(9, 0, False, True), S(0, 1, False, True), S(9, 0, False, True),
      S(7, 0, False, True), S(9, 0, False, True), R(), S(9, 0),
      S(9, 0, True), S(9, 0, False, True), S(5, 0, False, True), S(7, 0, False, True),
      S(9, 0, False, True), R(), S(4, 0), S(5, 0, False, True)]

# P4: driving sixteenths, the payoff pattern.
P4 = [S(9, 0, True), S(9, 0), S(9, 0), S(0, 1),
      S(9, 0, True), S(9, 0), S(7, 0), S(9, 0),
      S(9, 0, True), S(9, 0), S(0, 1), S(9, 1),
      S(7, 0, True), S(5, 0), S(9, 0), S(9, 0, False, True)]

PATTERNS = [P0, P1, P2, P3, P4]

# The patch the tour starts from. Mid-scale everywhere so each sweep has room
# to move in both directions.
BASE = dict(tune=64, cutoff=45, reso=45, envmod=55, decay=60, accent=75)

# (first bar, bars, pattern, what it is for, sweeps). A sweep is
# (param, from, to); 3 cutoff, 4 resonance, 5 env mod, 6 decay, 7 accent.
# A sweep with from == to is a hold, used to park a knob somewhere the NEXT
# sweep needs it rather than leaving it wherever the last one finished.
TOUR = [
    (0,  4, 0, "the patch as it stands, so the ear has a reference", []),
    (4,  4, 1, "CUTOFF across the whole knob", [(3, 12, 118)]),
    (8,  4, 2, "RESONANCE across the whole knob", [(3, 60, 60), (4, 0, 127)]),
    (12, 4, 3, "ENV MOD across the whole knob, on a slid line", [(4, 85, 85), (5, 0, 127)]),
    (16, 4, 4, "DECAY across the whole knob, env mod held high", [(5, 90, 90), (6, 0, 127)]),
    (20, 4, 1, "ACCENT across the whole knob", [(6, 55, 55), (7, 0, 127)]),
    (24, 4, 2, "octaves and slides, every knob held still", [(4, 70, 70), (7, 90, 90)]),
    (28, 4, 4, "everything up", [(3, 40, 95), (4, 100, 118), (5, 95, 95), (6, 45, 45)]),
]

GROOVE = dict(tune=64, cutoff=52, reso=88, envmod=78, decay=52, accent=95)


def tour_events():
    """Pattern selects on bar lines, knob rides on every sixteenth."""
    ev = []
    for bar0, nbars, pat, _why, sweeps in TOUR:
        t0 = bar0 * TICKS_PER_BAR
        ev.append((t0, 1, pat))
        steps = nbars * TICKS_PER_BAR // TICKS_PER_STEP
        for param, a, b in sweeps:
            for i in range(steps):
                t = t0 + i * TICKS_PER_STEP
                v = a + (b - a) * (i / max(1, steps - 1))
                ev.append((t, param, int(round(v))))
    return ev


def groove_events():
    """Pattern changes every four bars and nothing else moves at all."""
    order = [0, 2, 3, 4]
    return [(bar * TICKS_PER_BAR, 1, order[(bar // 4) % len(order)])
            for bar in range(0, 16, 4)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-30", "303-songs"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    made = []
    for wave, wname in ((0, "saw"), (1, "square")):
        k = dict(BASE)
        k["waveform"] = wave
        data = G.build_song(k, PATTERNS[0], tempo=TEMPO, n_bars=32,
                            title="303 tour " + wname, author="rbsgen",
                            device="bass1", patterns=PATTERNS,
                            bass_events=tour_events())
        n = "t303_tour_" + wname
        open(os.path.join(a.out_dir, n + ".rbs"), "wb").write(data)
        made.append((n, "32 bars, one knob at a time across its range, " + wname))

        g = dict(GROOVE)
        g["waveform"] = wave
        data = G.build_song(g, PATTERNS[0], tempo=TEMPO, n_bars=16,
                            title="303 groove " + wname, author="rbsgen",
                            device="bass1", patterns=PATTERNS,
                            bass_events=groove_events())
        n = "t303_groove_" + wname
        open(os.path.join(a.out_dir, n + ".rbs"), "wb").write(data)
        made.append((n, "16 bars at one static patch, no automation, " + wname))

    man = ["# 303 listening songs, 2026-08-30", "",
           "Written so the 303 can be judged BY EAR with nothing else in the",
           "path. One 303, the other silent, both drum machines silent, the",
           "distortion off at the unit and at every section switch, the delay",
           "send zero on every channel, the PCF routed nowhere and the",
           "compressor explicitly off. Proved by `isolation_check.py`.", "",
           "Render OURS with `REBORN_BASS_ENGINE=tpt` and `--no-fx`.",
           "Export ReBirth's side from your own copy of ReBirth. Tempo %g." % TEMPO, "",
           "| song | what it is |", "|---|---|"]
    for n, why in made:
        man.append("| `%s` | %s |" % (n, why))
    man += ["", "## What the tour walks through", ""]
    for bar0, nb, pat, why, _sw in TOUR:
        man.append("- **bars %d to %d**, pattern %d: %s"
                   % (bar0 + 1, bar0 + nb, pat, why))
    man += ["", "## The patterns", "",
            "- **0** the riff, rests that let the filter ring out",
            "- **1** the same shape accented hard",
            "- **2** octave jumps",
            "- **3** slides throughout, so the envelope rides across the phrase",
            "- **4** driving sixteenths"]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    print("wrote %d songs to %s" % (len(made), a.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
