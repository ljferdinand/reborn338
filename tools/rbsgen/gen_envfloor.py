#!/usr/bin/env python3
"""The env mod law measured where its FLOOR is visible. One octave down.

WHY THIS FIXTURE EXISTS. On 2026-08-02 the env mod knob was found to be BIPOLAR:
ReBirth's envelope lowers the filter's floor as the knob rises (1.72 octaves per
unit knob) as well as raising its onset (2.25). Ours only ever opens the filter,
from a floor pinned at 903 Hz whatever the knob does.

That law was fitted on THREE floor points, because above env mod 64 ReBirth's
corner sweeps below the analysis band and the measurement returns the guard
rather than the filter. The guard sits at 1.8 x f0 so it can bracket the peak
with a harmonic either side, and every envelope fixture in this project so far
uses note 9 at 110 Hz, putting it at 198 Hz.

THE FIX IS THE NOTE, NOT THE GUARD. Dropping an octave to 55 Hz moves the band's
floor to 99 Hz, and the predicted floor at cutoff 64 env mod 127 is 157 Hz, which
lands inside it. Widening the guard instead would just let the estimator report
numbers it cannot support: below 1.8 x f0 the peak has no harmonic beneath it and
the parabola has nothing to fit.

READ THE BAND WHEN YOU ANALYSE THIS. The observable range is 1.8 x f0 up to
kmax x f0, so halving the note halves the TOP as well. env_traj.py's default kmax
of 40 would cap this at 2.2 kHz and silently truncate every onset above it, which
is the same failure at the other end. Use

    python tools/analyze/env_traj.py --roots envfloor_rb --kmax 110 --fmax 6000

giving a band of 99 Hz to 6 kHz, wide enough for the whole sweep at cutoff 64.

WHAT IS HELD OUT, and it is the point of the design. The scalar fit this replaces
scored 0.071 octaves on its own six cells and 0.542 on cells it had not seen, a
factor of eight, and the held-out failure is what revealed the bipolar law in the
first place. So group A is the fit and B, C and D are not: a different cutoff
below, a different cutoff above, and a different resonance. A law that only holds
at the operating point it was fitted at has been the failure mode three times
this week.

GROUP D IS THERE FOR A SPECIFIC DOUBT. The instrument tracks the RESONANT PEAK,
which stands in for the corner, and that relationship is a function of resonance.
If the fitted law changes between resonance 60 and 90, what has been measured is
partly the estimator and not the filter.

TIED THROUGHOUT, so the gate never closes and one envelope runs the whole file
with no VCA release or filter ring mixed into it. Resonance 60 is where batch P's
tied-versus-plucked difference is spent. No accent anywhere: it is downstream.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "envfloor")
os.makedirs(OUT, exist_ok=True)

NOTE, OCTAVE = 9, -1          # A, one octave down: 55 Hz
spec = []


def emit(name, cut, envmod, decay, reso=60):
    pat = [step(note=NOTE, slide=True, octave=OCTAVE) for _ in range(16)]
    knobs = dict(cutoff=cut, reso=reso, envmod=envmod, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


def nm(c, em, d, r=60):
    return f"g303_c{c:03d}_em{em:03d}_dec{d:03d}_r{r:03d}"


# A: THE FIT SET. Cutoff 64, resonance 60, the full env mod sweep at two slow
#    decays. Ten cells, and with both endpoints per cell that is twenty
#    observations for three parameters.
for em in (0, 32, 64, 96, 127):
    for d in (96, 127):
        emit(nm(64, em, d), 64, em, d)

# B: HELD OUT, a lower cutoff. The floor may still go under the band here at
#    full env mod; the onset will not, so this scores the upward half.
for em in (0, 64, 127):
    emit(nm(40, em, 96), 40, em, 96)

# C: HELD OUT, a higher cutoff. The floor stays well inside the band, so this is
#    the cleanest check of the downward half.
for em in (0, 64, 127):
    emit(nm(90, em, 96), 90, em, 96)

# D: HELD OUT, a different RESONANCE. If the fitted law moves between 60 and 90
#    then part of what was measured is the peak-to-corner relation, not the
#    envelope.
for em in (0, 64, 127):
    emit(nm(64, em, 96, 90), 64, em, 96, reso=90)

# Duplicates, one in the fit set and one held out, to carry the noise floor.
emit(nm(64, 64, 96) + "_dup", 64, 64, 96)
emit(nm(90, 127, 96) + "_dup", 90, 127, 96)

json.dump(spec, open(os.path.join(OUT, "envfloor.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print( "  note 9 octave -1, so f0 is 55 Hz and the analysis band starts at 99")
print( "  A  cutoff 64 reso 60, 5 env mods x 2 decays   THE FIT SET")
print( "  B  cutoff 40 reso 60, 3 env mods              held out, low cutoff")
print( "  C  cutoff 90 reso 60, 3 env mods              held out, high cutoff")
print( "  D  cutoff 64 reso 90, 3 env mods              held out, resonance")
print( "  2 duplicates.  ANALYSE WITH --kmax 110: a low note needs a high kmax")
