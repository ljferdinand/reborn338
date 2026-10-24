#!/usr/bin/env python3
"""Batch Q: the resonance-0 low-end SHAPE, with enough points to identify it.

WHAT THIS IS FOR. Task 57 established the resonance-engaged low-end highpass at
111.5 Hz plus or minus 0.7 from the six-point shape at resonance 127, to 0.027 dB
against a 0.026 dB duplicate floor, and it is now applied. What it could NOT do
is say what happens at resonance 0, where ReBirth still loses up to 5.7 dB and
our law gives zero. Over batch L's six harmonics and 2.6 octaves, a one-pole, a
one-pole plus tilt and a two-pole plus tilt all fit with corners wandering 70 to
183 Hz. That is non-identifiability, the same class that retired
invert_filter.py, and no amount of refitting the same data escapes it.

The only way out is more points below 340 Hz.

THE COMB. Batch N got eighteen by crossing C1 at 32.703 Hz with E1 at 41.203 so
the two harmonic combs interleave:

    C1   32.7  65.4  98.1 130.8 163.5 196.2 228.9 261.6 294.3 327.0
    E1   41.2  82.4 123.6       164.8 206.0 247.2       288.4 329.6

Ten points and eight points, near-collisions at 163.5/164.8 and 327.0/329.6 that
batch N already uses as a cross-check on the harmonic extraction. Against batch
L's six, this is three times the resolution over the range where the unexplained
loss lives.

WHY BATCH N ITSELF CANNOT BE REUSED. It is TIED. Task 57's tie control measured
ReBirth's tie-minus-pluck at +2.45 dB at resonance 0, +2.21 at 15 and +0.74 at
45, against +0.02 for us throughout. So the tie effect is a monotone decay that
is still 30 percent alive at resonance 45, and it is LARGEST exactly where this
question lives. A tied fixture here would measure the tie, not the filter. That
is what happened to batch P, confidently and self-consistently, and it took an
independent batch three times worse.

WHY NOBODY BUILT THE PLUCKED VERSION, AND THE WAY THROUGH. gen_batchP.py records
the blocker: at 32.7 Hz a plucked half-step gate at 120 BPM is 2.0 cycles of the
fundamental, which is not a measurement. Tempo is the way out and appears not to
have been tried. The VCA gates for HALF A STEP and the gate is confirmed
proportional across 60 to 180 BPM, so the same plucked gate at 60 BPM is:

                        120 BPM        60 BPM
    C1 32.703 Hz       2.0 cycles    4.1 cycles
    E1 41.203 Hz       2.6           5.2
    A1 55.000 Hz       3.4           6.9

That doubles the resolution without touching the articulation, and 60 BPM sits
inside the range the gate law was validated on rather than beyond it.

THE CONTROLS, and this batch lives or dies on them.

TEMPO IS A NEW VARIABLE and must be validated before anything here is trusted.
Two cells are emitted at 120 BPM that duplicate existing batch L cells EXACTLY,
same note, octave, knobs, bars and articulation. They must come back matching
batch L's exports. If they do not, the capture chain drifted and nothing else
here means anything.

Two more cells are A1 at 60 BPM at the same knobs. Comparing those against the
120 BPM pair isolates the tempo change itself, on the note batch L already
covers. This matters because tempo is not obviously neutral: at env mod 0
ReBirth's filter envelope still fires (task 54, the chirp), so a longer note
makes that onset a smaller fraction of what is measured. If the pair disagrees,
the disagreement is quantified rather than discovered later.

And three cells are duplicated outright, per gen_batchL.py, which is the
template and the only generator that bakes controls in.

SETTINGS. Resonance 0, 15 and 30, because that is where the law is wrong and
where the tie effect is worst. All six cutoffs, because the resonance-0 miss
grows with cutoff, -0.46 dB at cutoff 40 to -5.73 at 127. Env mod 0, decay 64,
waveform 0, tune 64, matching batch L and L2 exactly so the three sets merge.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "batchQ")
os.makedirs(OUT, exist_ok=True)

# note index, octave. Note 9 octave 0 measures 110.002 Hz, so octave -1 puts
# note 0 at C1 32.703, note 4 at E1 41.203 and note 9 at A1 55.000.
C1 = (0, -1)
E1 = (4, -1)
A1 = (9, -1)

CUT = (16, 40, 90, 110, 127, 64)
RESO = (0, 15, 30)
SLOW, FAST = 60.0, 120.0

spec = []


def emit(name, note, octv, c, r, tempo):
    # PLUCKED. No slide anywhere: the tie effect is the thing being avoided.
    pat = [step(note=note, octave=octv) for _ in range(16)]
    knobs = dict(cutoff=c, reso=r, envmod=0, decay=64, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=tempo, title=name,
                           author="rbsgen", n_bars=2, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# The grid: the interleaved comb, plucked, slow.
for lab, (n, o) in (("C1", C1), ("E1", E1)):
    for c in sorted(CUT):
        for r in RESO:
            emit(f"q303_{lab}_cut{c:03d}_reso{r:03d}_bpm060", n, o, c, r, SLOW)

# Chain validation: these two must reproduce batch L's exports exactly.
for c in (40, 127):
    emit(f"q303_A1_cut{c:03d}_reso000_bpm120_CHAINCHECK", *A1, c, 0, FAST)

# Tempo isolation: same cells at the new tempo, on the note batch L covers.
for c in (40, 127):
    emit(f"q303_A1_cut{c:03d}_reso000_bpm060_TEMPOCHECK", *A1, c, 0, SLOW)

# Duplicate controls, spread across the grid.
for lab, (n, o), c, r in (("C1", C1, 16, 0), ("E1", E1, 90, 15), ("C1", C1, 127, 30)):
    emit(f"q303_{lab}_cut{c:03d}_reso{r:03d}_bpm060_dup", n, o, c, r, SLOW)

json.dump(spec, open(os.path.join(OUT, "batchQ.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  grid: 2 notes x {len(CUT)} cutoffs x {len(RESO)} resonances = "
      f"{2*len(CUT)*len(RESO)}, plucked at 60 BPM")
print("  2 chain-check cells at 120 BPM that must match batch L exactly")
print("  2 tempo-check cells, same knobs at 60 BPM")
print("  3 duplicate controls")
print("  18 harmonic points below 340 Hz once the C1 and E1 combs are merged")
