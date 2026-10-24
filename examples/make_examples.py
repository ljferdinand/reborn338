#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Regenerate the example songs in this directory.

Every file here is made by this script from Reborn's own clean-room song
generator (tools/rbsgen/rbsgen.py). No byte comes from ReBirth or from any song
that shipped with it, which is why they may live in the repository when no
other .rbs file may.

    python examples/make_examples.py

writes:

    acid_line.rbs            a 303 line with slides and accents over an 808 beat,
                             the 303 sent to the delay
    beat_909.rbs             a 909 beat through the pattern-controlled filter
    bass_303.rbs             one 303 alone through the distortion
    acid_line_open303.rbsx   acid_line.rbs as a Reborn session whose first 303
                             slot is set to the Open303 voice (open it in the app)

The .rbs files open in ReBirth too, because the generator writes the format's
documented layout. The .rbsx is Reborn's own session format (docs/FORMAT_RBSX.md).
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "tools", "rbsgen"))
import rbsgen  # noqa: E402
from rbsgen import step  # noqa: E402

TITLE_AUTHOR = "Reborn example"


def acid_pattern():
    # Notes are semitones above C (0..12); octave shifts by one octave.
    return [
        step(0, accent=True), step(0), step(3, slide=True), step(0),
        step(rest=True), step(7, octave=-1), step(0, slide=True), step(10),
        step(0, accent=True), step(rest=True), step(0, octave=1), step(7),
        step(5, slide=True), step(3), step(0, accent=True), step(rest=True),
    ]


def acid_line():
    return rbsgen.build_song(
        knobs=dict(tune=64, cutoff=46, reso=104, envmod=86, decay=58, accent=96),
        pattern=acid_pattern(), tempo=128.0, title="Acid line", author=TITLE_AUTHOR,
        head_note="Reborn example song", n_bars=8, device="bass1+808",
        drum_knobs=dict(accent=90, bd_decay=80, sd_snappy=70),
        drum_hits={"bd": (0, 4, 8, 12), "sd": (4, 12), "ch": (2, 6, 10, 14),
                   "cp": (12,)},
        drum_accents=(0, 8),
        delay=dict(steps=3, feedback=45, pan=96, send=50))


def beat_909():
    return rbsgen.build_song(
        knobs={}, pattern=[], tempo=132.0, title="909 beat", author=TITLE_AUTHOR,
        head_note="Reborn example song", n_bars=8, device="909",
        # Levels sit below the defaults so eleven stacked voices do not clip the
        # master, which is a hard clamp as in ReBirth.
        drum_knobs=dict(accent=60, bd_level=74, sd_level=64, rs_level=64, cp_level=64,
                        hh_level=54, bd_decay=70, oh_decay=48),
        drum_hits={"bd": (0, 4, 8, 12), "cp": (4, 12), "ch": (0, 4, 8, 12, 1, 5, 9, 13),
                   "oh": (2, 6, 10, 14), "rs": (7, 15)},
        drum_accents=(0, 4, 8, 12),
        # mode 1 is the lowpass (docs/NON_OBVIOUS_BEHAVIOUR.md)
        pcf=dict(on=1, freq=72, q=70, amt=90, pattern=5, decay=56, mode=1))


def bass_303():
    pattern = [step(n, accent=a, slide=s) for n, a, s in (
        (0, True, False), (0, False, False), (12, False, True), (0, False, False),
        (3, True, False), (0, False, False), (5, False, True), (7, False, False),
        (0, True, False), (0, False, False), (10, False, True), (12, False, False),
        (7, True, False), (5, False, False), (3, False, True), (0, False, False))]
    return rbsgen.build_song(
        knobs=dict(tune=64, cutoff=60, reso=90, envmod=70, decay=70, accent=80, waveform=1),
        pattern=pattern, tempo=120.0, title="303 alone", author=TITLE_AUTHOR,
        head_note="Reborn example song", n_bars=8, device="bass1",
        dist=dict(on=1, b1=90, b2=40, sections=(1, 0, 0, 0)))


def chunk(cid, body):
    return cid + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def rbsx(rbs, open303_slots=(0,)):
    """A CAT(RBSX) session: RVER 1, the song image, an empty environment, and a
    RINS record giving each listed 303 slot engine 1 (Open303)."""
    env = b"sampleDir=\nmodName=\nmodPath=\n"
    ins = struct.pack(">I", 2)
    for slot in range(2):
        engine = 1 if slot in open303_slots else 0xFF   # 0xFF = not recorded
        ins += bytes([engine]) + struct.pack(">I", 0) + struct.pack(">I", 0)
    body = (b"RBSX" + chunk(b"RVER", struct.pack(">I", 1)) + chunk(b"RSNG", rbs)
            + chunk(b"RENV", env) + chunk(b"RINS", ins))
    return chunk(b"CAT ", body)


def main():
    out = {}
    out["acid_line.rbs"] = acid_line()
    out["beat_909.rbs"] = beat_909()
    out["bass_303.rbs"] = bass_303()
    out["acid_line_open303.rbsx"] = rbsx(out["acid_line.rbs"])
    for name, data in out.items():
        with open(os.path.join(HERE, name), "wb") as f:
            f.write(data)
        print("wrote %s (%d bytes)" % (name, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
