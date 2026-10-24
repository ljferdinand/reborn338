#!/usr/bin/env python3
"""The 303 accent knob, swept. Task 60.

WHY NOW. The accent knob has only ever been tested ON or OFF; COMPONENT_MATRIX
records it as "on/off only, gap, no depth sweep" and the constants derived from
it were flagged provisional twice, once for resting on the harmonic instrument
that was miscounting and once for being entangled with ACC_TIE.

Today's accent-rate round removed most of that entanglement and left a clean,
isolated number. On a plain retrigger at env mod 0, with a matched no-accent
control, ReBirth lifts brightness +5.95 dB and we lift +3.03. The error is
-2.92 dB, and after the slide-out-of-silence probe the same error appears in
that cell too at -2.86, agreeing to 0.06 dB. So the depth deficit is one
number, it shows up identically wherever the accent fires, and it is now
separable from the question of WHICH steps fire.

What is missing is the LAW. One point at accent 96 cannot distinguish a scale
error from an offset from a curvature error, and fitting a depth from a single
operating point is what left the earlier accent constants provisional.

THE DESIGN.

Group 1, the law: accent flag ON, knob swept over nine values, at one filter
setting. This is the curve to fit.

Group 2, IS THE KNOB INERT WITHOUT THE FLAG? Three cells with the accent flag
OFF and the knob at 0, 64 and 127. For the 808 the equivalent is established:
the accent knob alone does nothing and it scales only steps whose accent row is
set. For the 303 it has never been checked, and the manual is explicit that
accent "does not only affect volume, but also the shape and character of the
sound" (p49), which is a claim about the flag rather than the knob. If these
three differ, the knob is not a per-step scaler and the whole law changes shape.

Group 3, DOES THE LAW DEPEND ON THE OPERATING POINT? Five knob values at a
second cutoff and resonance. The accent moves the filter, so its audible depth
must depend on where the filter already sits; what matters is whether the
underlying law does. Fitting group 1 alone and applying it everywhere is the
batch N failure mode, which this project has now repeated twice.

EVERY CELL IS A PLAIN RETRIGGER: a rest, then the note. That is deliberate.
Today's work showed ReBirth's accent reaches the same place whether the step is
a retrigger, a tied rest or a plain rest, within 0.31 dB, so the retrigger is
the cleanest carrier and it avoids re-entangling this with ACC_TIE.

ENV MOD 0 throughout, so the accent is the only thing that can move the filter.
Decay 64, waveform 0, tune 64, matching the accent-rate batch so the two merge.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "accentdepth")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
KNOBS = (0, 16, 32, 48, 64, 80, 96, 112, 127)
spec = []


def emit(name, acc_knob, flag, cut=64, reso=60):
    # rest, then the note: a plain retrigger with a full step of silence in
    # front of it so nothing is ringing through. Rests after, for the tail.
    pat = [step(rest=True), step(note=NOTE, accent=flag)] + \
          [step(rest=True) for _ in range(14)]
    knobs = dict(cutoff=cut, reso=reso, envmod=0, decay=64, accent=acc_knob,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# Group 1: the law.
for k in KNOBS:
    emit(f"d303_acc{k:03d}_on_c064_r060", k, True)

# Group 2: is the knob inert with no accent flag?
for k in (0, 64, 127):
    emit(f"d303_acc{k:03d}_off_c064_r060", k, False)

# Group 3: a second operating point.
for k in (0, 32, 64, 96, 127):
    emit(f"d303_acc{k:03d}_on_c100_r030", k, True, cut=100, reso=30)

# Duplicates, spread across the groups.
emit("d303_acc000_on_c064_r060_dup",   0, True)
emit("d303_acc096_on_c064_r060_dup",  96, True)
emit("d303_acc064_on_c100_r030_dup",  64, True, cut=100, reso=30)

json.dump(spec, open(os.path.join(OUT, "accentdepth.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  group 1, the law:            {len(KNOBS)} knob values, accent flag ON")
print( "  group 2, knob inert?:        3 values, accent flag OFF")
print( "  group 3, second op point:    5 values at cutoff 100 resonance 30")
print( "  duplicates:                  3")
