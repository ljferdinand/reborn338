#!/usr/bin/env python3
"""Accent probe: does the accent envelope RE-FIRE on a slid step?

The question, from task 60. On clodhopper_303-2, steps 13/14/15 carry identical
flags (note, accent, slide) and ReBirth spans 30 dB across them, monotonically
darkening, while ours sits flat at +10.3 dB. That is one envelope firing at the
retrigger and decaying through the tied run, against our reading the flag afresh
on every step. Slide means DO NOT RETRIGGER.

But it cannot simply be that we ignore accents on non-retriggered steps, because
a separate finding, also caught by Luke's ear and also measured, is that ReBirth
DOES read the accent row on a REST and lands it on whatever is sounding through.
`holdStep()` implements that, and deleting the re-arm would re-break it.

So the distinction to establish is:

    accent on a REST tied into        -> fires (established, re-confirmed here)
    accent on a SLID NOTE mid-run     -> ??? this is what the batch is for

No existing fixture can separate them. Batch G has rests and slides but no rest
preceded by a slid step, which is the same blind spot that hid the original
tied-rest bug, and Clodhopper has both cases tangled together in one pattern.

THE DESIGN. Every fixture is a run over steps 1 to 8 followed by eight rests, so
each bar gives one clean event and a long decay to watch it in. Slide sits on the
step you slide FROM, so steps 1 to 7 carry it and step 8 does not, which makes
steps 1-8 a single continuous note that fires its envelope exactly once.

  acc_none     tied run, no accents                     the baseline decay
  acc_first    tied run, accent on step 1 only          one fire, then decay
  acc_mid      tied run, accent on step 5 only          THE DECISIVE CELL
  acc_all      tied run, accent on every step           re-fire would hold it up
  retrig_none  eight separate notes, no accent          isolates the tie itself
  retrig_all   eight separate notes, accent on each     every step legitimately fires
  rest_acc     note, then rests, accent on rest 5       re-confirms the rest case

`acc_mid` is the one that settles it. If ReBirth's brightness JUMPS at step 5,
the accent re-fires on a slid step and our behaviour is right in kind and wrong
only in degree. If it keeps decaying through step 5 as though nothing happened,
the accent is bound to the RETRIGGER and `holdStep()` is over-applying it.

PANEL SETTINGS. Env mod 0, so the accent is the only thing that can move the
filter. Cutoff 64 and resonance 60 deliberately, NOT Clodhopper's 45/125: at near
self-oscillation the measurement band sits on a very steep skirt and a modest
corner movement reads as 30 dB, which is superb for hearing and useless for
fitting. Accent knob 96, high enough to be unmissable.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-01", "accentprobe")
os.makedirs(OUT, exist_ok=True)

NOTE = 9          # A, same note throughout so a slide glides nowhere
RUN = 8           # steps 1..8 are the run, 9..16 are rests
spec = []


def emit(name, tied, accents, rests=()):
    """tied: steps 1..RUN-1 carry slide, so the run is one continuous note.
       accents: 1-based step numbers carrying the accent flag.
       rests: 1-based step numbers that are rests rather than notes."""
    pat = []
    for i in range(1, 17):
        if i > RUN:
            pat.append(step(rest=True))
        elif i in rests:
            pat.append(step(rest=True, accent=(i in accents),
                            slide=(tied and i < RUN)))
        else:
            pat.append(step(note=NOTE, accent=(i in accents),
                            slide=(tied and i < RUN)))
    knobs = dict(cutoff=64, reso=60, envmod=0, decay=64, accent=96,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


emit("a303_acc_none",    True,  ())
emit("a303_acc_first",   True,  (1,))
emit("a303_acc_mid",     True,  (5,))          # <- the decisive cell
emit("a303_acc_all",     True,  tuple(range(1, RUN + 1)))
emit("a303_retrig_none", False, ())
emit("a303_retrig_all",  False, tuple(range(1, RUN + 1)))
emit("a303_rest_acc",    True,  (5,), rests=tuple(range(2, RUN + 1)))
# controls, because batch P proved that dropping them costs a whole round
emit("a303_acc_mid_dup", True,  (5,))
emit("a303_acc_none_dup", True, ())

json.dump(spec, open(os.path.join(OUT, "accentprobe.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print("  run = steps 1-8, rests 9-16, 4 bars. env mod 0, cutoff 64, reso 60, accent 96")
print("  a303_acc_mid is the decisive cell: does brightness jump at step 5?")
