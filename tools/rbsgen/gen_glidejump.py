#!/usr/bin/env python3
"""Glide pitch-jump: isolate task 77 out of Clodhopper, at Clodhopper's own knobs.

THE DEFECT. Task 75 found the largest onset_ab disagreement on Clodhopper 303-2
is a bucket correlating 0.87 with the semitone jump executed DURING a glide;
seven twelve-semitone jumps average 15.4 dB of excess onset energy vs ReBirth.
Task 53 then showed 2.7 dB of that is the DC blocker's floor and the blocker-
invariant core is a PEAK ~7.3 dB hotter than ReBirth at the slid step.

WHAT DECODING CLODHOPPER SHOWED (2026-08-06). bass2 runs cutoff 29, reso 127,
envmod 0, decay 115, accent 96, saw. The hot step (onset_ab idx 1) is a +12
SLIDE in patterns 0 and 1, carrying NO accent flag, and the prior bar's step 15
is a plain unaccented struck note that clears the latch, which is why the task-78
decayonly fix does not move this bucket. So it is neither the accent leak nor,
per a wide ours-side sweep, the glide itself: our tied glide step is never hotter
than the struck step at any jump, reso, cutoff or envmod tried.

THE HYPOTHESIS THIS FIXTURE TESTS. The excess is the known high-resonance
filter-response mismatch at reso 127, not the jump. If ours is uniformly hotter
than ReBirth at ALL jump sizes INCLUDING zero, it is a static resonance mismatch
and task 77 reframes toward the reso-127 filter (tasks 39/40/74/76). If the
excess scales with jump, the jump is real and we match ReBirth's own surge.

DESIGN. Clodhopper's exact panel (cutoff 29, envmod 0, decay 115, accent 0 so the
latch is never involved, saw, tune centre), crossing JUMP {0,3,7,12} with RESO
{110, 127}: 127 is Clodhopper's value, 110 a lower-resonance comparison to see if
the mismatch is resonance-dependent. JUMP 0 is the pivotal control. The gesture
strikes the base note at step 0 and slides to base+jump at step 1 (a tie, so no
VCA re-attack), holds four steps, then rests so the tail is clean. Measure the
PEAK at the slid step per task 53, not the onset_ab ratio, and render ours with
REBORN_DC_HP=0 so the blocker floor cannot add 2.7 dB.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-06", "glidejump")
os.makedirs(OUT, exist_ok=True)

def target(jump):
    if jump == 12:
        return dict(note=0, octave=1)
    return dict(note=jump, octave=0)

def gesture(jump):
    t = target(jump)
    struck = step(note=0, octave=0)
    slid   = step(note=t["note"], octave=t["octave"], slide=True)
    return [struck, slid] + [dict(slid)] * 4 + [step(rest=True)] * 10

spec = []
def emit(name, jump, reso):
    knobs = dict(cutoff=29, reso=reso, envmod=0, decay=115, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, gesture(jump), tempo=140.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})

for reso in (110, 127):
    for jump in (0, 3, 7, 12):
        emit(f"g303_j{jump:02d}_r{reso:03d}", jump, reso)

# Duplicates for the export noise floor: the pivotal control and the extreme, at
# Clodhopper's own resonance.
emit("g303_j00_r127_dup", 0, 127)
emit("g303_j12_r127_dup", 12, 127)

json.dump(spec, open(os.path.join(OUT, "glidejump.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print("  Clodhopper panel (cutoff 29, envmod 0, decay 115), jump {0,3,7,12} x reso {110,127}")
print("  pivotal control is JUMP 0: excess there = static resonance mismatch, not the jump")
