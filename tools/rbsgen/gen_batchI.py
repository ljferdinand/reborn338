#!/usr/bin/env python3
"""Batch I: the three things left open on the 303's envelopes.

Every batch G fixture is at 120 BPM and crosses env mod against decay at only
three points each. That was enough to see that our env mod is far too shallow
and that our decay does nothing at env mod 0, and not enough to fit either.

  I1  TEMPO. The VCA gates for 58 to 62 ms at 120 BPM, where a step is 125 ms.
      Whether that is HALF A STEP or a fixed ~62 ms cannot be told from a single
      tempo, and the two disagree everywhere else: at 180 BPM proportional gives
      41.7 ms and fixed gives 62.5 ms, which is 75% of the step. Same fixtures
      also confirm the 21.9 ms release is a real time constant, i.e. that it
      does NOT scale with tempo.

  I2  ENV MOD, 8 points at three decay settings. At env mod 127 ReBirth's
      centroid is 2624 Hz against our 1217, and the ratio falls the further the
      knob goes, so this is a curve and not a gain.

  I3  DECAY at env mod 0, 8 points. ReBirth's centroid still moves 603 to 648 Hz
      with the filter envelope contributing nothing, so decay reaches something
      else. Eight points across the knob say whether that is the amp envelope,
      and if so with what law.

Held at resonance 20 throughout except where stated: a resonant filter goes on
ringing after the gate closes, and that ring lands in exactly the window an
envelope has to be fitted from.

Usage:  python gen_batchI.py [outdir]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-27", "batchI")
os.makedirs(OUT, exist_ok=True)

BARS = 4
A = 9                  # note A, the register every 303 fit has used

files = []


def emit(name, knobs, pattern, tempo, note):
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pattern, tempo=tempo, title=name, author="rbsgen",
                           n_bars=BARS, device="bass1"))
    files.append((name, tempo, note))


def notes(n=A, length=16):
    return [step(note=n) for i in range(length)]


# ---------------------------------------------------------------------------
# I1  TEMPO: is the gate a fraction of the step, or a fixed time?
# ---------------------------------------------------------------------------
# Resonance 20 and env mod 0 so the only thing shaping the note is the VCA.
TEMPO_REF = dict(cutoff=64, reso=20, envmod=0, decay=64, accent=0, waveform=0)

for bpm in (60, 90, 120, 150, 180):
    emit(f"i303_tempo{bpm:03d}", TEMPO_REF, notes(), bpm,
         f"step is {15000.0/bpm:.1f} ms; proportional gate predicts "
         f"{7500.0/bpm:.1f} ms, a fixed gate predicts ~62 ms")

# ---------------------------------------------------------------------------
# I2  ENV MOD, a real sweep, crossed against decay
# ---------------------------------------------------------------------------
# Cutoff low so the envelope has somewhere to sweep TO; resonance 20 keeps the
# peak from dominating the centroid.
EM_REF = dict(cutoff=32, reso=20, accent=0, waveform=0)

for em in (0, 16, 32, 48, 64, 80, 96, 112, 127):
    for dec in (0, 64, 127):
        emit(f"i303_em{em:03d}_dec{dec:03d}", dict(EM_REF, envmod=em, decay=dec),
             notes(), 120.0, "env mod depth law")

# ---------------------------------------------------------------------------
# I3  DECAY at env mod 0: what does it reach when it cannot reach the filter?
# ---------------------------------------------------------------------------
DEC_REF = dict(cutoff=64, reso=20, envmod=0, accent=0, waveform=0)

for dec in (0, 16, 32, 48, 80, 96, 112, 127):
    emit(f"i303_dec{dec:03d}", dict(DEC_REF, decay=dec), notes(), 120.0,
         "decay with the filter envelope held out")

# ---------------------------------------------------------------------------

with open(os.path.join(OUT, "MANIFEST.md"), "w") as f:
    f.write("# Batch I: 303 envelopes\n\n")
    f.write("Export each with the effect rack OFF and the master compressor\n")
    f.write("verified off in the panel. Song mode, so the arrangement plays and\n")
    f.write("not the panel knobs.\n\n")
    f.write("| fixture | BPM | what it answers |\n|---|---|---|\n")
    for name, tempo, note in files:
        f.write(f"| `{name}.rbs` | {tempo:g} | {note} |\n")

print(f"wrote {len(files)} fixtures to {OUT}")
for group, n in (("I1 tempo", 5), ("I2 env mod x decay", 27), ("I3 decay", 8)):
    print(f"  {group:22s} {n}")
