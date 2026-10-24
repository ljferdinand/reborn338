#!/usr/bin/env python3
"""Batch L: put the first three harmonics UNAMBIGUOUSLY below the corner.

Why this exists. The level comparison reports the passband as the mean of
harmonics 1 to 3, and that is only a passband measurement while the corner sits
well above the third harmonic. Batch H plays note A at f0 = 110 Hz, so harmonics
1 to 3 are 110, 220 and 330 Hz. At cutoff knob 16 our own corner is about 550 Hz
and ReBirth's is unknown and probably lower, which means on ReBirth's side those
harmonics may be sitting on the skirt rather than in the passband.

That ambiguity is load-bearing. Measured on batch H, ReBirth's passband rises
+10.9 dB from cutoff 16 to 127 at resonance 0 while ours rises +3.1, and it is
not currently possible to say how much of that is a real gain difference and how
much is ReBirth's corner sweeping up past the three harmonics being averaged.
The same statistic is flat and in agreement at resonance 90 and above.

Dropping an octave to f0 = 55 Hz puts them at 55, 110 and 165 Hz. Our lowest
corner is about 550 Hz and ReBirth's lowest cannot plausibly be below 165, so
the three harmonics are in the passband in every cell of the grid and the
statistic means the same thing everywhere.

RESONANCE COVERAGE is deliberately not the full batch H set. Resonance 0 and 30
are the two families that diverge and 127 is the family that agrees, so all three
behaviours are represented; adding 60, 90 and 110 would triple the export time to
re-confirm a row that is already flat.

DUPLICATE CONTROL CELLS are included, which batch H did not have and should have.
Three cells are emitted twice under different names, so every batch carries its
own reproducibility check and a floor can be computed without a separate
`batchH_floor`-style excursion. A ReBirth export is not deterministic (about six
discrete render states), so a pair that collides scores zero and a single pair is
not a floor. Three pairs is thin but it is enough to catch a chain that has
drifted between sessions.

Env mod 0 and a held note throughout, so the filter is static and neither the
envelope nor the decay knob confounds the reading. Same as batch H in every
respect except the octave.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-31", "batchL")
os.makedirs(OUT, exist_ok=True)

CUT = (16, 40, 64, 90, 110, 127)
RESO = (0, 30, 127)
# Cells emitted a second time as their own control. One per resonance family,
# spread across the cutoff knob rather than clustered.
DUPES = ((16, 0), (90, 30), (127, 127))

pat = [step(note=9, octave=-1) for _ in range(16)]
spec = []


def emit(name, c, r):
    knobs = dict(cutoff=c, reso=r, envmod=0, decay=64, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=2, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


for c in CUT:
    for r in RESO:
        emit(f"l303_cut{c:03d}_reso{r:03d}", c, r)
for c, r in DUPES:
    emit(f"l303_cut{c:03d}_reso{r:03d}_dup", c, r)

json.dump(spec, open(os.path.join(OUT, "batchL.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  {len(CUT)}x{len(RESO)} cutoff x resonance at f0 = 55 Hz, "
      f"plus {len(DUPES)} duplicate controls")
