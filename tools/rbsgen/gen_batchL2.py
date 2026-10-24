#!/usr/bin/env python3
"""Batch L2: resolve the resonance knee, PLUCKED, because batch P could not.

Batch P asked this question with tied notes and the answer was wrong. ReBirth
behaves differently on a held note at low resonance: its tied and plucked curves
differ by 2.45 dB at resonance 0 where ours differ by 0.02. The tied fixture
measures that state rather than the filter, and constants derived from it took
batch H from 2.627 to 8.886 dB rms. Batch P reproduced BIT-IDENTICALLY across
sessions while being wrong, which is the sharpest example this project has of
reproducibility not being validity. See docs/research/FINDINGS_AUDIT.md for 2026-08-01.

So this extends BATCH L, whose methodology is validated rather than assumed:
plucked at f0 55 Hz, it agrees with plucked batch H at f0 110 Hz to 0.12 dB at
every frequency the two combs share, checked three separate times now.

Batch L already covers resonance 0, 30 and 127 at cutoff 16/40/64/90/110/127.
The knee sits between 0 and 60 and is sampled by exactly one point there, which
is why the cutoff dependence below it could be seen but not fitted. This adds
resonance 15, 45 and 60 at the same six cutoffs, giving six resonance points from
0 to 127 with four of them at or below the knee.

Identical to gen_batchL.py in every other respect (note A octave down, waveform 0,
env mod 0, decay 64, tempo 120, 2 bars, plucked) so the two sets merge into one
grid rather than needing to be reconciled.

CONTROLS, which batch P dropped and should not have. Three cells are emitted twice
under different names as duplicates. Two more are emitted TIED as well as plucked,
at resonance 15 and 45, so the tie effect can be tracked ACROSS the knee instead
of only at resonance 0 and 127 where it is currently known. That second control
is the one batch P needed and did not have.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-01", "batchL2")
os.makedirs(OUT, exist_ok=True)

NOTE_A = 9
CUT = (16, 40, 64, 90, 110, 127)
RESO = (15, 45, 60)                 # 0, 30 and 127 already exist in batch L
DUPES = ((16, 15), (90, 45), (127, 60))
TIE_CONTROL = ((64, 15), (64, 45))  # same cells, tied, to track the tie effect

spec = []


def emit(name, c, r, tied=False):
    pat = [step(note=NOTE_A, octave=-1, slide=tied) for _ in range(16)]
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
for c, r in TIE_CONTROL:
    emit(f"l303_cut{c:03d}_reso{r:03d}_tie", c, r, tied=True)

json.dump(spec, open(os.path.join(OUT, "batchL2.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  {len(CUT)} cutoffs x {len(RESO)} new resonances = {len(CUT)*len(RESO)}")
print(f"  {len(DUPES)} duplicate controls, {len(TIE_CONTROL)} tie controls")
print("  merges with batch L into resonance 0/15/30/45/60/127 at six cutoffs")
