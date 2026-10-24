#!/usr/bin/env python3
"""Batch N: resolve the SHAPE of the low-frequency loss, not just its size.

The finding this exists to explain, from 2026-07-31: ReBirth loses low end as
resonance rises and we do not. At resonance 127 its 55 Hz harmonic sits 6.3 dB
below its own 220 Hz harmonic while ours sits 0.2 dB below, and the gap tracks
the resonance knob from 0.08 dB at resonance 0 to 6.1 at 127. A ladder cannot
produce that: both frequencies are deep in the passband where the response is
flat at 1/(1+k), so feedback must attenuate them equally and their ratio cannot
move.

Three mechanisms fit the sign and size: a resonance-scaled low shelf, a lowpass
in the resonance feedback path, and a frequency-shaped makeup gain. Batch L
cannot separate them, because it samples only 55, 110, 165 and 220 Hz and any
smooth curve fits four points.

TWO THINGS MAKE THIS BATCH DIFFERENT.

**A low note is its own dense comb.** C1 at 32.703 Hz puts a harmonic every 32.7
Hz, so ONE fixture yields ten points below 330 Hz. Crossing it with E1 at 41.203
interleaves a second comb and gives eighteen points below 340 Hz, enough to fit a
shelf against a pole and tell them apart. The two combs nearly collide at 163.5
and 164.8 Hz, which is a free consistency check: those two must agree.

**The notes are TIED, not plucked.** Every step carries the slide flag with the
same pitch, so the pitch does not glide and the gate never closes. That matters
enormously down here. At 32.7 Hz a plucked half-step gate at 120 BPM is 2.0
cycles of the fundamental, which is not a measurement; tying gives a continuous
tone and about 65 cycles per bar. Verified on our side before generating: no
sample in a 2 s window falls below 20% of the median envelope, and f0 comes back
32.704 against a target of 32.703.

Two PLUCKED control cells are included at C1 anyway. If tied and plucked disagree
about the size of the effect, the tie handling is doing something on one side
that it is not doing on the other, and that must be known before the curve is
read. It is not a hypothetical: two real bugs this month were in the tie path.

Duplicate control cells are included, as in batch L and for the same reason.

Env mod 0 and a static filter throughout, so nothing sweeps while it is measured.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-31", "batchN")
os.makedirs(OUT, exist_ok=True)

# (note index, octave, label, f0). Note 9 octave 0 measures 110.002 Hz, so
# note 0 octave 0 is C2 and octave -1 drops it to C1.
NOTES = ((0, -1, "C1", 32.703), (4, -1, "E1", 41.203))
CUT = (64, 127)
RESO = (0, 60, 90, 127)
PLUCK_CONTROL = ((0, -1, "C1", 127, 0), (0, -1, "C1", 127, 127))   # note, oct, label, cut, reso
DUPES = (("C1", 127, 127), ("E1", 64, 0))

spec = []


def emit(name, note, octv, c, r, tied):
    pat = [step(note=note, octave=octv, slide=tied) for _ in range(16)]
    knobs = dict(cutoff=c, reso=r, envmod=0, decay=64, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        # 4 bars rather than batch L's 2: at 32.7 Hz the extra averaging is
        # worth more than the export time, and an export's cost is per-file
        # overhead rather than song length.
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


for note, octv, lab, _f0 in NOTES:
    for c in CUT:
        for r in RESO:
            emit(f"n303_{lab}_cut{c:03d}_reso{r:03d}_tie", note, octv, c, r, True)
for note, octv, lab, c, r in PLUCK_CONTROL:
    emit(f"n303_{lab}_cut{c:03d}_reso{r:03d}_pluck", note, octv, c, r, False)
for lab, c, r in DUPES:
    note, octv = next((n, o) for n, o, l, _ in NOTES if l == lab)
    emit(f"n303_{lab}_cut{c:03d}_reso{r:03d}_tie_dup", note, octv, c, r, True)

json.dump(spec, open(os.path.join(OUT, "batchN.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  {len(NOTES)} notes x {len(CUT)} cutoffs x {len(RESO)} resonances tied, "
      f"plus {len(PLUCK_CONTROL)} plucked controls and {len(DUPES)} duplicates")
print("  combined comb below 340 Hz: 18 points from 32.7 Hz up")
