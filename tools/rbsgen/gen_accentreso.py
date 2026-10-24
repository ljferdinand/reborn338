#!/usr/bin/env python3
"""The 303 accent knob at HIGH resonance. Task 72, the reso axis accentdepth lacks.

WHY. accentdepth (task 60) swept the accent knob at reso 60 and reso 30, and the
2026-08-06 corner measurement showed two defects against ReBirth: our push has no
front-loading (ReBirth reaches +1.9 st by knob 16 and +7.2 by knob 48 where ours
is flat until knob 64), and our reso-scaling is wrong in a way that FLIPS SIGN:
at reso 60 ours under-pushes (+8.5 st vs ReBirth's +16.1 at knob 127) but at reso
127 (Clodhopper, task 76) ours over-pushes. Our accOct = accKnob*accEnv_*ACC_SWEEP
*(1+reso) grows too fast with resonance. To fit a reso-scaling law rather than
guess, we need ReBirth's corner push at high resonance, which accentdepth stops
short of.

DESIGN. The accentdepth group-1 panel exactly (cutoff 64, envmod 0, decay 64,
saw, tune centre, note A, plain retrigger, tempo 120) so the sets merge into one
knob x reso surface. Knob {0,32,64,96,127} crossed with reso {90, 110}: two new
points on the reso axis to sit alongside accentdepth's 60, below self-oscillation
so env_traj's peak tracer reads a clean corner. Knob 0 is the per-reso baseline.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-06", "accentreso")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
spec = []
def emit(name, acc_knob, reso):
    pat = [step(rest=True), step(note=NOTE, accent=True)] + \
          [step(rest=True) for _ in range(14)]
    knobs = dict(cutoff=64, reso=reso, envmod=0, decay=64, accent=acc_knob,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})

for reso in (90, 110):
    for k in (0, 32, 64, 96, 127):
        emit(f"a303_acc{k:03d}_on_c064_r{reso:03d}", k, reso)

# Duplicates for the export noise floor: the extreme knob at each new reso.
emit("a303_acc127_on_c064_r090_dup", 127, 90)
emit("a303_acc127_on_c064_r110_dup", 127, 110)

json.dump(spec, open(os.path.join(OUT, "accentreso.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print("  accent knob {0,32,64,96,127} x reso {90,110}, + 2 dups, merges with accentdepth reso 60")
