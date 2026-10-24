#!/usr/bin/env python3
"""The filter envelope as a SHAPE IN TIME, crossed with DECAY. Task 59's blocker.

WHY THIS FIXTURE HAS BEEN OWED FOR A WEEK. Every filter fit in this project sits
at decay 64. RUNBOOK section 4 names a decay-crossing grid as the prerequisite
for the joint refit and it has never existed, so the decay knob's effect on the
filter has been assumed rather than measured across its range.

WHY IT IS NOW THE RIGHT THING RATHER THAN JUST AN OWED THING. On 2026-08-02 Luke
described ReBirth's accents as "a shape that's closing" against ours as "a shape
that's opening", and the measurement agreed including the sign: with ENV MOD AT
ZERO and no accent at all, ReBirth's brightness falls 2.14 dB through the note
inside the gate and ours RISES 0.04. So the base filter envelope, the thing every
other modulator scales, does not decay. COMPONENT_MATRIX is explicit that accent
modulates the same envelope env mod and decay drive, and that fitting accent
first bakes their error into it. This is that envelope.

WHAT IS MEASURED. Not a spectrum. The brightness TRAJECTORY through the note, in
two windows that have to be reported separately because they answer different
questions and today they disagreed:

  INSIDE THE GATE, 0 to 39 ms   the filter moving while the note is held
  ACROSS THE TAIL, out to 110   the filter moving through the release, which for
                                a resonant filter includes its ring, and is much
                                of what a "wah tail" is

Scoring one number for "the sweep" produced a retraction the same day it was
written. Any fit off this batch must name its window.

THE TIED GROUP IS THE POINT OF THE DESIGN. A plucked note's gate closes at half a
step, so the envelope's later trajectory is tangled with the VCA release and with
the filter's own ring. Tying the note holds the gate open, so the filter envelope
is visible on its own for the whole bar. Batch P's lesson applies and is
respected: a tied note measures something different from a plucked one AT LOW
RESONANCE, where ReBirth's tied and plucked curves differ by 2.45 dB. The tie
effect decays with resonance, measured at +2.21 dB at resonance 15 and +0.74 at
45, so this group sits at resonance 60 where it is spent. The plucked groups
carry the same cells so the two can be compared rather than assumed equivalent.

NO ACCENT ANYWHERE. Accent is downstream and belongs after this.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "envshape")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
DECAYS = (0, 16, 32, 64, 96, 127)
spec = []


def emit(name, decay, envmod, cut=64, reso=60, tied=False):
    if tied:
        # every step the same note WITH slide: the gate never closes and the
        # pitch never moves, so what is left is the filter envelope alone.
        pat = [step(note=NOTE, slide=True) for _ in range(16)]
    else:
        # one plucked note, then rests, so the tail is clean.
        pat = [step(rest=True), step(note=NOTE)] + \
              [step(rest=True) for _ in range(14)]
    knobs = dict(cutoff=cut, reso=reso, envmod=envmod, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# A: plucked, env mod 0. The base envelope, which is what today's finding says
#    is missing entirely.
for d in DECAYS:
    emit(f"e303_dec{d:03d}_em000_pluck", d, 0)

# B: plucked, env mod 64. Does decay behave the same once env mod is driving it?
for d in DECAYS:
    emit(f"e303_dec{d:03d}_em064_pluck", d, 64)

# C: TIED, env mod 0. The gate stays open, so the envelope is unconfounded by
#    the VCA release and by the filter's ring.
for d in DECAYS:
    emit(f"e303_dec{d:03d}_em000_tie", d, 0, tied=True)

# D: a second operating point, because a law fitted at one and applied
#    everywhere is the failure mode this project has now repeated twice.
for d in (0, 64, 127):
    emit(f"e303_dec{d:03d}_em000_pluck_c100_r030", d, 0, cut=100, reso=30)

# Duplicates, spread across the groups.
emit("e303_dec000_em000_pluck_dup", 0, 0)
emit("e303_dec064_em064_pluck_dup", 64, 64)
emit("e303_dec127_em000_tie_dup", 127, 0, tied=True)

json.dump(spec, open(os.path.join(OUT, "envshape.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  A plucked env mod 0   {len(DECAYS)} decays   the base envelope")
print(f"  B plucked env mod 64  {len(DECAYS)} decays   does decay behave the same when driven")
print(f"  C TIED    env mod 0   {len(DECAYS)} decays   gate held open, envelope unconfounded")
print( "  D second operating point, 3 decays")
print( "  3 duplicates.  No accent anywhere: it is downstream of this.")
