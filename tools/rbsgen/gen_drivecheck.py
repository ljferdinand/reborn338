#!/usr/bin/env python3
"""Fixtures for the drive-shaper EAR CHECK. See docs/research/OPEN_DECISIONS.md under
"Resolved calls" section 1 for how it went, and PLAN_303_ENVELOPE.md section 8
for the one live thread it left (the level-matched rerun).

The question is not "which measures better", which is settled: with the filter
refitted alongside it, the low-drive build wins on seven instruments at once.
The question is whether taking `satAsym()` out loses character that no metric
here has ever captured, because the shaper exists in the first place because of
an ear judgement about ReBirth's `detroit_303` being buzzy and farty.

So these are built to be LISTENED to, not measured, and the design follows from
where a pre-filter saturator can and cannot be heard. It adds harmonics ahead of
the filter, so its contribution is audible only where the filter passes what it
added. That gives a natural three-point test:

  drv_open    cutoff open, resonance LOW. The shaper's added harmonics pass
              through and are not masked by a resonant peak. If the two builds
              differ anywhere, they differ most here.
  drv_acid    the classic squelch, and the exact panel of the `detroit_303`
              patch the shaper was fitted against. Hot, resonant, sweeping.
  drv_closed  the CONTROL: cutoff low enough that everything the shaper adds is
              filtered back out. The two builds should be indistinguishable
              here. If they are not, the audible difference is NOT the shaper
              and the whole framing is wrong.

One 303, no drums, effect rack off, same musical line in all three so the panel
is the only variable. The line deliberately mixes held and slid notes, where
timbre is easy to hear, with accented stabs, where the transient is.

Also regenerates `detroit_303.rbs` itself, which was lost: only its renders
survived. Its panel is recovered exactly from `tools/gen_sweeps.py`, which is
the generator that originally wrote it.

Usage:  python gen_drivecheck.py [outdir]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-31", "drivecheck")
os.makedirs(OUT, exist_ok=True)

A = 9
files = []


def emit(name, knobs, pattern, tempo, bars, note):
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pattern, tempo=tempo, title=name, author="rbsgen",
                           n_bars=bars, device="bass1"))
    files.append((name, note))


def S(note=0, accent=False, slide=False, octave=0, rest=False):
    return step(note=note, accent=accent, slide=slide, octave=octave, rest=rest)


# The listening line. Held and slid pairs let timbre ring long enough to judge;
# the accented stabs expose the transient, which is where a saturator is most
# audible. Rests keep it from turning into a drone.
def line():
    return [S(9, accent=True), S(9, slide=True), S(9),          S(rest=True),
            S(4),              S(4, slide=True), S(7),          S(rest=True),
            S(9, octave=1, accent=True), S(9, slide=True), S(11), S(12, slide=True),
            S(9),              S(rest=True),     S(7, accent=True), S(4, slide=True)]


# The detroit roller, recovered verbatim from tools/gen_sweeps.py
def detroit():
    return [S(9, accent=True), S(9, slide=True), S(rest=True), S(9, octave=1, accent=True),
            S(rest=True), S(7), S(9, slide=True), S(rest=True), S(9, accent=True), S(4),
            S(9, octave=1), S(9, slide=True), S(rest=True), S(12, accent=True),
            S(11, slide=True), S(9)]


emit("drv_open",
     dict(tune=64, cutoff=100, reso=40, envmod=30, decay=70, accent=60, waveform=0),
     line(), 130.0, 4,
     "filter open, resonance LOW: where a pre-filter saturator is most audible")

emit("drv_acid",
     dict(tune=64, cutoff=55, reso=115, envmod=95, decay=80, accent=70, waveform=0),
     line(), 130.0, 4,
     "the detroit panel on the listening line: hot, resonant, sweeping")

emit("drv_closed",
     dict(tune=64, cutoff=25, reso=30, envmod=10, decay=60, accent=0, waveform=0),
     line(), 130.0, 4,
     "CONTROL: the shaper's harmonics are filtered out, so the builds must match")

emit("detroit_303",
     dict(tune=64, cutoff=55, reso=115, envmod=95, decay=80, accent=70, waveform=0),
     detroit(), 128.0, 8,
     "the original patch the shaper was fitted against, regenerated exactly")

with open(os.path.join(OUT, "MANIFEST.md"), "w") as f:
    f.write("# Drive-shaper ear check\n\n")
    f.write("Export each with the effect rack OFF and the master compressor\n")
    f.write("verified off in the panel. One 303, no drums.\n\n")
    f.write("**Re-export `detroit_303` rather than reusing the old\n")
    f.write("`renders/detroit_303.WAV`.** The design memo records that the\n")
    f.write("original comparison did not have distortion matched on both sides,\n")
    f.write("and that confound is the reason the shaper's fit is in question at\n")
    f.write("all. A clean re-export removes it.\n\n")
    f.write("| fixture | what it is for |\n|---|---|\n")
    for name, note in files:
        f.write(f"| `{name}.rbs` | {note} |\n")

print(f"wrote {len(files)} fixtures to {OUT}")
for name, note in files:
    print(f"  {name:14s} {note}")
