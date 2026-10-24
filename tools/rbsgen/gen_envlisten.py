#!/usr/bin/env python3
"""LISTENING fixtures for the env-mod-0 filter envelope. Not a parity batch.

WHY THIS EXISTS. The 2026-08-02 envelope ear check used Clodhopper, detroit_303
and drv_closed and Luke reported no meaningful difference between no envelope,
EM_FLOOR 0.60 and 1.00. He was right, and the reason is a design error in the
fixture choice rather than anything about the finding.

The patches were picked for having ENV MOD 0, which is the condition the
envelope exists under, without checking DECAY against note length. Clodhopper
runs decay 115, a 1.63 s time constant, with a 54 ms gate at 140 BPM. The
envelope therefore moves THREE PERCENT within a note, 0.6 dB of a 17 dB
envelope. It is a static brightness offset there, not a gesture, and the
whole-file rms match removes part of even that.

  patch              decay    tau    gate   fall within a note
  Clodhopper 303-2     115   1.63s    54ms      3%   0.6 dB
  drv_closed            60   0.26s    62ms     21%   3.6 dB
  detroit_303           80   0.55s    62ms     11%   1.8 dB, and env mod 95 dominates

WHAT MAKES THE ENVELOPE AUDIBLE is a SHORT decay against a LONG gate, with
enough resonance for a corner movement to be heard. These fixtures are built to
maximise that on purpose. That is legitimate for a listening test and would not
be for a parity measurement, where an exaggerated operating point flatters
whatever is being judged. Nothing fitted may be scored on these.

THE LEVERS, and why each is set where it is:

  DECAY 24 and 48   tau about 0.11 s and 0.22 s, so a note is one to five time
                    constants rather than a thirtieth of one
  TEMPO 90 BPM      a 16th step is 167 ms and the gate is half of it, 83 ms,
                    against 54 ms at Clodhopper's 140. Slower would be clearer
                    still and stops sounding like music
  RESONANCE 115     high, so a corner movement is heard as a sweep rather than
                    inferred from a level change. Not 127, which self-oscillates
  ACCENT 0          the accent moves the same envelope. Removing it means what
                    is heard is the envelope alone
  ENV MOD 0         the condition under which ReBirth has an envelope and we
                    have none, which is the whole subject

ONE TIED FIXTURE. A single held note per bar with a slow decay is the purest
demonstration there is: no retrigger, no rhythm, just the filter closing over
about a second. If the difference is not audible there it is not audible.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "envlisten")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
spec = []


def emit(name, decay, cut, reso, tied=False, tempo=90.0):
    if tied:
        pat = [step(note=NOTE, slide=(i < 15)) for i in range(16)]
    else:
        pat = [step(note=NOTE) for _ in range(16)]
    knobs = dict(cutoff=cut, reso=reso, envmod=0, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=tempo, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# Plucked, fast decay: each note is roughly five time constants, so the whole
# envelope happens inside every note.
emit("v303_pluck_dec024_c064", 24, 64, 115)
# Plucked, medium decay: the note ends partway down, which is the shape a real
# acid line has.
emit("v303_pluck_dec048_c064", 48, 64, 115)
# Darker register, nearer Clodhopper's cutoff, but with a decay that moves.
emit("v303_pluck_dec024_c040", 24, 40, 120)
# The pure demonstration: one held note, closing over about a second.
emit("v303_tied_dec080_c064", 80, 64, 115, tied=True)

json.dump(spec, open(os.path.join(OUT, "envlisten.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print("  short decay against a long gate, resonance 115, accent 0, env mod 0")
print("  LISTENING ONLY: the operating point is exaggerated on purpose and")
print("  nothing fitted may be scored on these")
