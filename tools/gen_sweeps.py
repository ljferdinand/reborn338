#!/usr/bin/env python3
"""
gen_sweeps.py: regenerate the Reborn RB-338 filter-probe test patches, via rbsgen.
Written for the 2026-07-22/23 filter investigation on the since-absorbed
exp/filter-probes branch; the patches themselves are still valid.
Run from the repo root:  python3 tools/gen_sweeps.py

NOTE: these are FUNCTIONALLY-EQUIVALENT regenerations (same knobs + patterns). The exact
.rbs files that were hand-exported from ReBirth for the A/B live with Luke; if you need
byte-exact ReBirth correspondence, use those. All patches are scratch mode -> open in ReBirth.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "rbsgen"))
import rbsgen
S = rbsgen.step
os.makedirs("sweeps", exist_ok=True)

def rep(note=9):                 return [S(note) for _ in range(16)]                 # repeated note
def held_low():                  return [S(0, octave=-1, slide=(i > 0)) for i in range(16)]  # low held (sysid)
def held_a():                    return [S(9, slide=(i > 0)) for i in range(16)]     # A held
def detroit():                   # A-minor 128-BPM roller (octave bounces, slides, accents)
    return [S(9,accent=True), S(9,slide=True), S(rest=True), S(9,octave=1,accent=True),
            S(rest=True), S(7), S(9,slide=True), S(rest=True), S(9,accent=True), S(4),
            S(9,octave=1), S(9,slide=True), S(rest=True), S(12,accent=True), S(11,slide=True), S(9)]
def w(name, knobs, pat, tempo=140.0, bars=8):
    rbsgen.write_song(f"sweeps/{name}.rbs", knobs, pat, tempo=tempo, n_bars=bars)
    print("wrote sweeps/%s.rbs" % name)

# --- static resonance-shape probes (sysid): reso 127, envmod 0, low held note, cutoff sweep
for c in (20, 45, 70, 95, 120):
    w(f"sysid_c{c:03d}", dict(tune=64, cutoff=c, reso=127, envmod=0, decay=20, accent=0, waveform=0), held_low(), 34.0, 1)
# --- env-swept resonance probes: cutoff 90, envmod 70, resonance sweep
for r in (20, 30, 60, 100, 127):
    w(f"env_r{r}", dict(tune=64, cutoff=90, reso=r, envmod=70, decay=64, accent=0, waveform=0), rep(9), 140.0, 8)
# --- static max-reso reference
w("static_r127", dict(tune=64, cutoff=120, reso=127, envmod=0, decay=64, accent=0, waveform=0), rep(9), 140.0, 8)
# --- whistle stress patch (max everything, long decay)
w("stress_303", dict(tune=64, cutoff=60, reso=127, envmod=100, decay=127, accent=0, waveform=0), rep(9), 140.0, 8)
# --- single long held note (max reso) for clean ring-vs-whistle
w("held_303", dict(tune=64, cutoff=60, reso=127, envmod=100, decay=127, accent=0, waveform=0), held_a(), 34.0, 1)
# --- musical test song
w("detroit_303", dict(tune=64, cutoff=55, reso=115, envmod=95, decay=80, accent=70, waveform=0), detroit(), 128.0, 8)
print("done.")
