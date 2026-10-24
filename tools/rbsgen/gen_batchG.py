#!/usr/bin/env python3
"""Batch G: the 303 fixture set for supertask 1.

Every earlier 303 round chased the interesting residual on a control that was
already roughly right, and left whole controls unmeasured. This set covers the
303's full panel, plus the per-step behaviours and the two interactions the
manual describes. See docs/COMPONENT_MATRIX.md section 1.

Design rules (docs/MEASUREMENT_PROTOCOL.md):
  * one thing varies per fixture, everything else held at a stated reference
  * sweeps, never pairs, because two points cannot tell a linear law from an
    exponential one and this project has fitted the wrong curve from two points
    more than once
  * a cross design wherever controls interact, rather than moving two things

Exports were batched by this point, so breadth is cheap. What is NOT cheap is
discovering later that a control was never measured. The exports must be made
from your own copy of ReBirth.

Usage:  python gen_batchG.py [outdir]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-27", "batchG")
os.makedirs(OUT, exist_ok=True)

TEMPO = 120.0          # 16th steps land 125 ms apart
BARS = 4
A = 9                  # note A, the register every previous 303 fit used

files = []


def emit(name, knobs, pattern, note):
    path = os.path.join(OUT, name + ".rbs")
    data = build_song(knobs, pattern, tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="bass1")
    with open(path, "wb") as f:
        f.write(data)
    files.append((name, note))


def notes(n=A, length=16, accents=(), rests=(), slides=(), octaves=None):
    """A repeated note with per-step flags."""
    oct_of = octaves or {}
    return [step(note=n, accent=(i in accents), slide=(i in slides),
                 rest=(i in rests), octave=oct_of.get(i, 0))
            for i in range(length)]


# ---------------------------------------------------------------------------
# G1  ACCENT: depth law AND whether accent accumulates across a run
# ---------------------------------------------------------------------------
# The manual (p49) says accent "does not only affect volume, but also the shape
# and character of the sound", and our accent has only ever been measured
# on/off. Separately, a real TB-303's accent circuit charges a capacitor that
# does not fully discharge between steps, so consecutive accents grow
# progressively stronger. Reviewers say ReBirth's accent is weaker than the
# hardware's, which may mean that behaviour is absent. Nobody has checked.
#
# The cross answers both at once. Comparing run4 against none at each knob
# setting gives the depth law; comparing step 0 against steps 1, 2 and 3 WITHIN
# run4 gives accumulation, since a per-note accent makes all four identical.
ACC_REF = dict(cutoff=64, reso=90, envmod=64, decay=64, waveform=0)

for knob in (0, 32, 64, 96, 127):
    emit(f"g303_acc{knob:03d}_none", dict(ACC_REF, accent=knob), notes(), A)
    emit(f"g303_acc{knob:03d}_run4", dict(ACC_REF, accent=knob),
         notes(accents=(0, 1, 2, 3)), A)

# Intermediate run lengths at one depth, to see the SHAPE of any accumulation
# rather than just its presence.
emit("g303_acc096_single", dict(ACC_REF, accent=96), notes(accents=(0,)), A)
emit("g303_acc096_run2", dict(ACC_REF, accent=96), notes(accents=(0, 1)), A)

# ---------------------------------------------------------------------------
# G2  REGISTER: every 303 fit to date is at note A and nowhere else
# ---------------------------------------------------------------------------
# Env mod 0 so the filter is not sweeping: the cutoff law can then be checked
# against pitch without the envelope confounding it.
REG_REF = dict(cutoff=90, reso=120, envmod=0, decay=64, accent=0, waveform=0)

for n in (0, 3, 6, 9, 12):
    emit(f"g303_note{n:02d}", dict(REG_REF), notes(n), n)

emit("g303_octdown", dict(REG_REF), notes(A, octaves={i: -1 for i in range(16)}), A)
emit("g303_octup", dict(REG_REF), notes(A, octaves={i: +1 for i in range(16)}), A)

# ---------------------------------------------------------------------------
# G3  TUNE: the manual says +/-1 octave in SEMITONE STEPS (p48)
# ---------------------------------------------------------------------------
# Our decoder uses an asymmetric law, 12/64 going down and 12/63 going up
# (rbs.cpp:435), which is not obviously a symmetric +/-12 semitones. The coarse
# sweep gives the overall law and tests symmetry; the fine points around centre
# test whether it is quantised to semitones at all.
TUNE_REF = dict(cutoff=90, reso=60, envmod=0, decay=64, accent=0, waveform=0)

for t in (0, 16, 32, 48, 64, 80, 96, 112, 127):
    emit(f"g303_tune{t:03d}", dict(TUNE_REF, tune=t), notes(), A)
for t in (62, 66, 68, 70):
    emit(f"g303_tunefine{t:03d}", dict(TUNE_REF, tune=t), notes(), A)

# ---------------------------------------------------------------------------
# G4  SLIDE: never measured
# ---------------------------------------------------------------------------
# Slide is a portamento with NO envelope retrigger, so the pair of fixtures
# gives both the glide time and whether the filter envelope restarts. Rests
# after the pair let the tail decay into silence for a clean measurement.
SLIDE_REF = dict(cutoff=70, reso=90, envmod=70, decay=64, accent=0, waveform=0)


def two_note(n0, n1, slide, length=16):
    p = [step(note=n0, rest=(i not in (0, 1))) for i in range(length)]
    p[0] = step(note=n0)
    p[1] = step(note=n1, slide=slide)
    return p


emit("g303_slide_off_up", dict(SLIDE_REF), two_note(A, A + 7, False), A)
emit("g303_slide_on_up", dict(SLIDE_REF), two_note(A, A + 7, True), A)
emit("g303_slide_on_down", dict(SLIDE_REF), two_note(A + 7, A, True), A)
emit("g303_slide_on_oct", dict(SLIDE_REF), two_note(A, A, True), A)

# ---------------------------------------------------------------------------
# G5  GATE and REST: never measured
# ---------------------------------------------------------------------------
# A real 303's VCA leaks rather than gating cleanly, which is a large part of
# why its lines feel connected. sparse isolates one note's full tail.
GATE_REF = dict(cutoff=70, reso=90, envmod=70, decay=64, accent=0, waveform=0)

emit("g303_gate_all", dict(GATE_REF), notes(), A)
emit("g303_gate_alt", dict(GATE_REF), notes(rests=tuple(range(1, 16, 2))), A)
emit("g303_gate_sparse", dict(GATE_REF), notes(rests=tuple(range(1, 16))), A)

# ---------------------------------------------------------------------------
# G6  WAVEFORM x CUTOFF
# ---------------------------------------------------------------------------
# Saw has all harmonics, square only odd ones (p48). Crossing with cutoff shows
# whether the difference is in the oscillator alone or in how the filter loads
# it. Resonance low so the filter is not adding a peak of its own.
for wave in (0, 1):
    for cut in (32, 64, 96):
        emit(f"g303_wave{wave}_cut{cut:03d}",
             dict(cutoff=cut, reso=20, envmod=0, decay=64, accent=0, waveform=wave),
             notes(), A)

# ---------------------------------------------------------------------------
# G7  ENV MOD x DECAY, the interaction the manual describes
# ---------------------------------------------------------------------------
# Env mod sets how far the envelope opens the filter; decay sets how fast it
# closes (p49). They have only ever been measured one at a time, which cannot
# separate "opens further" from "stays open longer".
for em in (0, 64, 127):
    for dec in (0, 64, 127):
        emit(f"g303_em{em:03d}_dec{dec:03d}",
             dict(cutoff=50, reso=100, envmod=em, decay=dec, accent=0, waveform=0),
             notes(), A)

# ---------------------------------------------------------------------------

man = os.path.join(OUT, "MANIFEST.md")
with open(man, "w") as f:
    f.write("# Batch G: 303 full-panel fixture set\n\n")
    f.write(f"{len(files)} fixtures, generated by `tools/rbsgen/gen_batchG.py`.\n")
    f.write(f"Tempo {TEMPO:g}, {BARS} bars, note A unless stated. One 303 only;\n")
    f.write("all other sections silent, effects off, compressor-off snapshot baked in.\n\n")
    f.write("Export each one from your own copy of ReBirth, in song mode, as "
            "`<fixture>.wav` (`batchG.json` lists the pairs).\n\n")
    f.write("| fixture | note | what it pins down |\n|---|---|---|\n")
    groups = [
        ("g303_acc", "accent depth law, and whether accent accumulates over a run"),
        ("g303_note", "does the cutoff law hold outside note A"),
        ("g303_oct", "octave up/down"),
        ("g303_tune", "tune law: is it symmetric, is it quantised to semitones"),
        ("g303_slide", "glide time, and whether the envelope retriggers"),
        ("g303_gate", "note length and VCA release/leak"),
        ("g303_wave", "saw vs square harmonic content across cutoff"),
        ("g303_em", "env mod x decay interaction"),
    ]
    for name, note in files:
        why = next((w for p, w in groups if name.startswith(p)), "")
        f.write(f"| `{name}` | {note} | {why} |\n")

print(f"wrote {len(files)} fixtures + MANIFEST.md to {OUT}")

# A job list pairing each fixture with the export file name the analysis expects.
import json
spec = [{"song": os.path.join(OUT, n + ".rbs"), "out": n + ".wav"} for n, _ in files]
with open(os.path.join(OUT, "batchG.json"), "w") as f:
    json.dump(spec, f, indent=1)
print(f"wrote batchG.json ({len(spec)} jobs)")
