#!/usr/bin/env python3
"""CALIBRATION fixtures for tools/analyze/env_traj.py. No ReBirth export needed.

WHAT THIS IS FOR, and it is not parity. env_traj.py answers a question that has
no independent check in the audio: is ReBirth's filter envelope exponential in
frequency or in octaves. An instrument answering an unfalsifiable question is
worth nothing, so it has to be calibrated against a trajectory whose domain is
KNOWN. We have one: our own engine, whose envelope domain is switched by
REBORN_ENV_DOMAIN. Render these twice, once in each domain, and the tool must
recover which is which. If it cannot, its verdict on ReBirth means nothing.

This is why there are no ReBirth exports here and why there should not be. The
whole point is that the truth is known a priori.

WHY THE FIRST ATTEMPT AT THIS FAILED, since the settings below are a direct
response to it. Calibrating on the envdomain fixtures with the env mod depth
pushed up to match ReBirth's travel put the corner hard against our filter's
6 kHz ceiling: the corner pinned at the top for the first part of the note and
only then decayed. The tool read that flat-then-falling shape as the frequency
domain and answered "hz" for renders that were octave-domain by construction.
The measurement was of the clamp.

So every cell here is built to keep the whole sweep strictly inside the band
the estimator can see:

  CUTOFF 0 to 24        a low base corner, so even a 3.4 octave sweep starts
                        well under the ceiling. Cutoff 32 and up does not fit
  ENV MOD 96 and 127    large travel, because under about an octave the two
                        models are the same curve and the answer is a tie
  DECAY 64, 96, 127     taus of 0.28, 0.81 and 2.14 s, so the shape is resolved
                        against a 93 ms analysis frame at three speeds
  RESONANCE 60          the peak has to sing for the estimator to find it, and
                        60 is where batch P's tie-versus-pluck effect is spent
  TIED                  the gate never closes, so one envelope runs the whole
                        file with no VCA release or filter ring mixed into it

RENDER IT LIKE THIS, with the env mod depth raised so the travel reaches the
range where discrimination is possible at all:

  for d in oct hz; do
    REBORN_ENV_DOMAIN=$d REBORN_EM_OCT=3.4 python tools/render/render_batch.py \
      ../fixtures-2026-08-02/envcal ../renders/envcal_$d --solo bass1 --seconds 8
  done
  python tools/analyze/env_traj.py --roots envcal_oct envcal_hz

Every envcal_oct row must read oct or tie, every envcal_hz row hz or tie, and
there must be enough non-ties to be worth anything. A single crossed verdict
condemns the instrument.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "envcal")
os.makedirs(OUT, exist_ok=True)

NOTE = 9
CUTS = (0, 8, 16, 24)
ENVMODS = (96, 127)
DECAYS = (64, 96, 127)
spec = []


def emit(name, cut, envmod, decay, reso=60):
    pat = [step(note=NOTE, slide=True) for _ in range(16)]
    knobs = dict(cutoff=cut, reso=reso, envmod=envmod, decay=decay, accent=0,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


for c in CUTS:
    for em in ENVMODS:
        for d in DECAYS:
            emit(f"c303_c{c:03d}_em{em:03d}_dec{d:03d}", c, em, d)

json.dump(spec, open(os.path.join(OUT, "envcal.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print(f"  {len(CUTS)} cutoffs x {len(ENVMODS)} env mods x {len(DECAYS)} decays, TIED")
print( "  CALIBRATION ONLY: render ours in both domains, no ReBirth export")
