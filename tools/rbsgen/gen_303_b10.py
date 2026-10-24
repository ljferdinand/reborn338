#!/usr/bin/env python3
"""Batch B10: the "round chirp", the resonant filter sweep on a note.

WHY THIS BATCH EXISTS. 2026-09-04, Luke on the EM_RESO ear check: even with the
env-mod corner brightness matched to ReBirth, the saw is missing a "roundness"
around each held note as it heads into the chirp, a "high-end round chirp" that
ReBirth has and we do not (BASS303_FINDINGS section 28). That is not the corner
POSITION (we matched centroid and it was still absent); it is the RESONANT
CHARACTER of the sweep, the peak riding up as the filter opens. The env-mod
corner thread is closed; this batch characterizes the resonant sweep itself.

THE GESTURE. A dramatic per-note filter sweep: cutoff low so the env has room,
env mod high, decay high so the sweep is long and audible, saw. Each note the
filter snaps open and rings down through the resonant peak, which is the chirp.
Crossed against RESONANCE, because the roundness is a resonance-character claim
and resonance is the axis it must live on.

    reso 50 / 75 / 100 / 127, retriggered quarter notes (steps 0,4,8,12)

Two more, because Luke named the HELD note specifically and the waveform:
    reso 100 SLID  (notes 4,8,12 slide from the previous, so the envelope rides
                    across without retriggering: the held-note chirp)
    reso 100 SQUARE (waveform contrast at the same gesture)

One 303, no effects. Accent OFF, to isolate the env-mod chirp from the accent
push. Render OURS with `REBORN_BASS_ENGINE=tpt`.

    python gen_303_b10.py --out-dir ../../../fixtures-2026-09-04/b10-303-chirp
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO, BARS, NOTE = 130.0, 4, 0
STEPS = (0, 4, 8, 12)          # quarter notes, room for each chirp to ring
# Low cutoff, high env mod, long decay: a big, slow filter sweep per note.
BASE = dict(tune=64, cutoff=20, envmod=115, decay=95, accent=0)
RESOS = (50, 75, 100, 127)


def pattern(reso_note_slide=False):
    p = []
    for i in range(16):
        if i in STEPS:
            # slide on every note after the first, so the envelope rides across
            slide = reso_note_slide and i != 0
            p.append(G.step(note=NOTE, slide=slide))
        else:
            p.append(G.step(rest=True))
    return p


def knobs(reso, waveform=0):
    return dict(BASE, reso=reso, waveform=waveform)


def cases():
    for r in RESOS:
        yield ("b10_r%03d_saw" % r, knobs(r, 0), pattern(False),
               "reso %d, retriggered saw chirp" % r)
    yield ("b10_r100_saw_slide", knobs(100, 0), pattern(True),
           "reso 100, SLID saw (held-note chirp, envelope rides across)")
    yield ("b10_r100_square", knobs(100, 1), pattern(False),
           "reso 100, retriggered SQUARE chirp (waveform contrast)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-09-04", "b10-303-chirp"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, k, pat, why in cases():
        data = G.build_song(k, pat, tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1")
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
    man = ["# 303 batch B10, the round chirp, 2026-09-04", "",
           "The resonant filter sweep on a note (BASS303_FINDINGS 28). A big",
           "per-note env-mod sweep (cutoff 20, env mod 115, decay 95) crossed",
           "against resonance, plus a slid (held-note) and a square variant. One",
           "303, no effects, accent off. Render OURS with `REBORN_BASS_ENGINE=tpt`.",
           "", "| fixture | what it is |", "|---|---|"]
    for name, why in rows:
        man.append("| `%s` | %s |" % (name, why))
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w").write("\n".join(man) + "\n")
    print("wrote %d fixtures to %s" % (len(rows), a.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
