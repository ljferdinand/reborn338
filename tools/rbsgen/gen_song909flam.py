#!/usr/bin/env python3
"""A playable 909 song for ear-checking FLAM, ReBirth against Reborn.

Not a sterile fixture: this is meant to be listened to. The pattern is built so
the flam is unmistakable rather than subtle, by putting the SAME voice straight
and flammed inside one bar, so there is a direct reference a beat away:

  step  0 . . . 4 . . . 8 . . . 12. . .
  BD    X       .       X       .          kick on 1 and 3
  SD    .       N       .       F          snare STRAIGHT on 2, FLAMMED on 4
  CH    x   x   x   x   x   x   x   x      straight sixteenths, unflammed anchor
  OH    .       .       .       . x        open hat lift into the fill
  toms  .       .       .       . L M H    a flammed tom fill, all three flammed

Everything else is held still: no shuffle, no automation, effects off, so the
only thing that differs between two renders is the flam.

Two files are written. `flam_on` sets the amount to 96, where the gap is wide
enough to hear as two distinct hits; `flam_off` is byte-identical except the
amount is 0, which collapses the gap so a flam cell sounds like a single hit.
Comparing those two tells you what flam is doing at all; comparing either one
against ReBirth tells you whether we do it the same way.

Usage:  python gen_song909flam.py [outdir]

Then, in ReBirth: open the file, make sure the master compressor is OFF, and play
or export it. In Reborn:
    reborn_render --song 909flam_on.rbs --solo 909 --samples <909 sample dir> \
        --no-fx --seconds 16 --out ours.wav
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else workspace_path("listen-909-flam")
os.makedirs(OUT, exist_ok=True)

TEMPO, BARS = 120.0, 8

HITS = {
    "bd": [0, 8],
    "sd": [4, 12],
    "ch": [0, 2, 4, 6, 8, 10, 12, 14],
    "oh": [15],
    "lt": [13], "mt": [14], "ht": [15],
}
# Flam only the SECOND snare and the tom fill. The first snare stays straight so
# the ear has a reference for the same voice one beat earlier.
FLAMS = [(12, "sd"), (13, "lt"), (14, "mt"), (15, "ht")]

# Musical levels, and decays chosen so the fill reads clearly.
KNOBS = dict(
    accent=70,
    bd_level=110, bd_tune=48, bd_attack=90, bd_decay=70,
    sd_level=104, sd_tune=64, sd_tone=76, sd_snappy=88,
    lt_level=100, lt_tune=40, lt_decay=76,
    mt_level=100, mt_tune=64, mt_decay=72,
    ht_level=100, ht_tune=88, ht_decay=68,
    rs_level=0, cp_level=0,
    hh_level=88, ch_decay=34, oh_decay=84,
    cr_level=0, cr_tune=64, rd_level=0, rd_tune=64,
)
# Accent the downbeats so the groove has some life.
ACCENTS = (0, 8)

for name, flam in (("909flam_on", 96), ("909flam_off", 0)):
    k = dict(KNOBS); k["flam"] = flam
    data = build_song({}, [], tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="909", drum_knobs=k,
                      drum_hits=HITS, drum_accents=ACCENTS, drum_flams=FLAMS)
    path = os.path.join(OUT, name + ".rbs")
    with open(path, "wb") as f:
        f.write(data)
    print(f"  {name}.rbs   flam amount {flam}")

print(f"\nWrote the flam ear-check songs to {OUT}")
