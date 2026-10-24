#!/usr/bin/env python3
"""Batch P: the low-end highpass BELOW the resonance knee, crossed with cutoff.

What this is for. Batch N established that ReBirth has a one-pole highpass near
100 Hz that resonance switches on, fitting its curve to 0.45 dB, and that the
corner saturates by resonance 60: its curves at 60, 90 and 127 agree within 0.25
dB. The fitted law is fc = 119.9 * r/(r + 0.141) with r the resonance knob.

Batch L then showed the law is wrong below the knee. At resonance 30 ReBirth's
low-end loss depends on CUTOFF, running -2.36 dB at cutoff 16 to -6.03 at cutoff
127 on the 55-minus-220 Hz statistic, and the law has no cutoff term. Residual at
resonance 30 is 1.4 to 1.9 dB against 0.3 dB at resonance 127.

Batch N could not see this because it sampled resonance 0, 60, 90 and 127, where
the effect had already saturated AND was cutoff-independent. It fitted the easy
region and was then applied to the hard one. This crosses the hard region
properly: five resonance points from 0 to 60 by four cutoffs.

WHY THE ANALYSIS BAND IS CONFOUNDED AT LOW CUTOFF, and what to do about it. At
cutoff 16 our nominal corner is about 550 Hz, so the 230-340 Hz reference band
sits only 1.6x below it and the LOWPASS is already tilting the region the
HIGHPASS is being measured in. Batch L shows this directly: at cutoff 16
resonance 0, ReBirth's 55 Hz harmonic sits 3.45 dB ABOVE its 220 Hz one, which is
a lowpass tilt and not a highpass at all. Fitting a highpass alone there would
charge the lowpass's tilt to the highpass.

So the fit must free BOTH corners. That is well posed here: ten harmonics below
340 Hz from the C1 comb against two corners and an offset. It also has a useful
side effect. The lowpass corner it recovers is a DIRECT reading of where
ReBirth's filter sits at RESONANCE 0, which is the one setting where our corner
law has never been verified, because resonance 0 has no peak to locate and the
retired inversion used exactly that cell as its baseline. See worklist item 8.

TIED NOTES, as batch N and for the same reason: at 32.7 Hz a plucked half-step
gate at 120 BPM is 2.0 cycles of the fundamental, which is not a measurement,
while tying gives about 65 cycles per bar. It also reproduces 100x better,
because ReBirth's roughly six render states differ in how a TRANSIENT lands and a
continuous tone has none. Batch N's duplicates agreed to 0.001 dB.

TWO CELLS DELIBERATELY DUPLICATE BATCH N EXACTLY (C1 at cutoff 127, resonance 0
and 60, same knobs, tempo, bars and tie flags). Re-exported a day later they
validate the whole capture chain across sessions, which is the check
docs/research/RUNBOOK.md section 3 asks for and which nothing else here provides.

E1 is included at resonance 30 only. Its comb interleaves with C1's to double the
point density, and resonance 30 is where the disagreement lives, so that is where
the density is worth the export time.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-01", "batchP")
os.makedirs(OUT, exist_ok=True)

# note index, octave, label. Note 9 octave 0 measures 110.002 Hz, so note 0
# octave 0 is C2 and octave -1 drops it to C1 at 32.703.
C1 = (0, -1, "C1")
E1 = (4, -1, "E1")

CUT = (16, 40, 90, 127)
RESO = (0, 15, 30, 45, 60)      # 0 and 60 anchor to batch N
E1_RESO = 30                    # where the cutoff dependence lives
DUPES = (("C1", 16, 0), ("C1", 90, 30), ("C1", 127, 60))

spec = []


def emit(name, note, octv, c, r):
    # slide on every step ties the pattern into one continuous note; the pitch
    # does not glide because every step is the same note.
    pat = [step(note=note, octave=octv, slide=True) for _ in range(16)]
    knobs = dict(cutoff=c, reso=r, envmod=0, decay=64, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


for c in CUT:
    for r in RESO:
        emit(f"p303_C1_cut{c:03d}_reso{r:03d}_tie", *C1[:2], c, r)
for c in CUT:
    emit(f"p303_E1_cut{c:03d}_reso{E1_RESO:03d}_tie", *E1[:2], c, E1_RESO)
for lab, c, r in DUPES:
    note, octv = (C1[:2] if lab == "C1" else E1[:2])
    emit(f"p303_{lab}_cut{c:03d}_reso{r:03d}_tie_dup", note, octv, c, r)

json.dump(spec, open(os.path.join(OUT, "batchP.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  C1: {len(CUT)} cutoffs x {len(RESO)} resonances = {len(CUT)*len(RESO)}")
print(f"  E1 at resonance {E1_RESO}: {len(CUT)}")
print(f"  duplicate controls: {len(DUPES)}")
print("  cut127 reso000 and cut127 reso060 also duplicate batch N exactly")
