#!/usr/bin/env python3
"""Does the filter envelope act in FREQUENCY or in OCTAVES? A gap in gen_envshape.

THE QUESTION. Our envelope moves the corner in octaves: log2(fc) gets the
envelope added to it. If ReBirth's instead adds to fc in Hz, the observed
trajectory differs in a specific way, and it is the way the envshape grid
measured on 2026-08-02: as a fraction of its own total fall ReBirth is 31.6
percent done at 50 ms where we are 41.0. An envelope acting on fc in Hz has
d(log fc)/de = B/(fc0 + B e), which is SMALL while e is large, so the dB
trajectory falls slowly at first and faster later. That is the observed sign.

WHY envshape CANNOT ANSWER IT, which is a design gap and not a measurement
failure. Its tied group sits at ENV MOD 0, so the corner travels only about 0.9
octaves, from roughly 1000 Hz to 530. Over less than an octave an exponential in
Hz and an exponential in log2(Hz) are very nearly the same curve: fitting the
resonant peak trajectory both ways gave 3.6% against 3.5% residual for ReBirth
and 6.2% against 5.4% for us. Those differences are far below the scatter. The
models are not distinguishable there and no amount of care with the estimator
changes that.

WHAT IS NEEDED IS A BIGGER EXCURSION OVER A LONG WINDOW, and those two pull
against each other in the obvious fixture. Env mod raises the excursion; a
plucked note only holds its gate for half a step, which at 120 BPM is 62 ms and
far too short for a slow decay. So: TIED, so the gate never closes, at HIGH env
mod, so the corner sweeps several octaves, with SLOW decays so a 93 ms analysis
frame is short against the time constant.

TIE SAFETY. Batch P's lesson is respected: a tied note measures something
different from a plucked one AT LOW RESONANCE, where ReBirth's tied and plucked
curves differ by 2.45 dB. That effect decays with resonance, +2.21 dB at 15 and
+0.74 at 45, and is spent by 60. Everything here is resonance 60.

DECAY 96 AND 127 ONLY, taus of 0.81 s and 2.14 s. Faster decays finish inside a
couple of analysis frames and cannot resolve a trajectory shape.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "envdomain")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
ENVMODS = (32, 64, 96, 127)
DECAYS = (96, 127)
spec = []


def emit(name, envmod, decay, cut=64, reso=60):
    # TIED: every step the same note with slide, so the gate never closes and
    # the pitch never moves. What is left is the filter envelope alone.
    pat = [step(note=NOTE, slide=True) for _ in range(16)]
    knobs = dict(cutoff=cut, reso=reso, envmod=envmod, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


for em in ENVMODS:
    for d in DECAYS:
        emit(f"f303_em{em:03d}_dec{d:03d}_tie", em, d)

# A LOW cutoff cell at the highest env mod, so the corner starts near the
# bottom of its travel and the excursion is as large as the panel allows. The
# two models are most separable where the corner moves furthest.
emit("f303_em127_dec127_tie_c016", 127, 127, cut=16)
emit("f303_em127_dec096_tie_c016", 127, 96, cut=16)

# Duplicates.
emit("f303_em127_dec127_tie_dup", 127, 127)
emit("f303_em064_dec096_tie_dup", 64, 96)

json.dump(spec, open(os.path.join(OUT, "envdomain.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  {len(ENVMODS)} env mods x {len(DECAYS)} slow decays, TIED, resonance 60")
print( "  2 low-cutoff cells at env mod 127, where the corner travels furthest")
print( "  2 duplicates")
