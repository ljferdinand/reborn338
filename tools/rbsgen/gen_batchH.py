#!/usr/bin/env python3
"""Batch H: separate the CUTOFF law from the RESONANCE law.

Batch G found our filter 1.45x too bright at resonance 20, consistently across
cutoff. Our cutoff law was fitted at resonance 120, so a single sweep cannot
tell a wrong cutoff mapping from a wrong resonance mapping: they are entangled.
A full 2-D grid separates them.

Env mod 0 and a held note throughout, so the filter is STATIC and neither the
envelope nor the decay knob confounds the reading.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-27", "batchH")
os.makedirs(OUT, exist_ok=True)
CUT  = (16, 40, 64, 90, 110, 127)
RESO = (0, 30, 60, 90, 110, 127)
pat = [step(note=9) for _ in range(16)]
spec = []
for c in CUT:
    for r in RESO:
        name = f"h303_cut{c:03d}_reso{r:03d}"
        knobs = dict(cutoff=c, reso=r, envmod=0, decay=64, accent=0, waveform=0, tune=64)
        with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
            f.write(build_song(knobs, pat, tempo=120.0, title=name,
                               author="rbsgen", n_bars=2, device="bass1"))
        spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})
json.dump(spec, open(os.path.join(OUT, "batchH.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures ({len(CUT)}x{len(RESO)} cutoff x resonance) to {OUT}")
