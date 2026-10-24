#!/usr/bin/env python3
"""Driven high-resonance fixtures to settle the comb-vs-peak / diode-fork question.

The static test (batchH reso 127, env mod 0) already showed neither side is a
dense comb. This tests the DRIVEN regime the design doc's comb claim is about and
the one Luke hears the muted chirp in: resonance 127, env mod 127, TIED so the
gate never closes and the corner sweeps several octaves through the harmonics at
maximum resonance. If ReBirth's coupled diode ladder intermodulates into a comb,
it shows here as inter-harmonic fill during the sweep that our buffered ladder
lacks. Tie is safe at reso 127 (the tied/plucked difference is spent by reso 60,
per gen_envdomain).

    python gen_combtest.py            # writes to fixtures-2026-09-03/combtest
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-09-03", "combtest")
os.makedirs(OUT, exist_ok=True)

NOTE = 9            # A, 110 Hz
spec = []


def emit(name, cut, reso, envmod, decay):
    pat = [step(note=NOTE, slide=True) for _ in range(16)]   # tied
    knobs = dict(cutoff=cut, reso=reso, envmod=envmod, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# Driven high-reso sweeps: reso 127, env mod 127, slow decay, three cutoffs.
for cut in (64, 90, 110):
    emit(f"cb_c{cut:03d}_r127_em127_d127", cut, 127, 127, 127)
# A faster decay at one cutoff, so the sweep speed is a variable not a constant.
emit("cb_c090_r127_em127_d096", 90, 127, 127, 96)
# Static max-everything held note, the steady-state self-oscillation regime.
emit("cb_c127_r127_em000_d064", 127, 127, 0, 64)

json.dump(spec, open(os.path.join(OUT, "combtest.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
