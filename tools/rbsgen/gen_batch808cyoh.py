#!/usr/bin/env python3
"""808 CY/OH/CH completeness batch (supertask 2, phase 2 continued).

The 808complete batch (2026-08-17) swept BD/SD tone/snappy and the tom tunes and
covered the CY *tone* knob, but never swept the two remaining timbre-shaping
controls: CY Decay and OH Decay. It also lacked a solo CH reference and a solo CB
reference. This batch fills them so CY and OH can be fitted decay-before-trim (the
BD lesson) and the CH+OH one-step hi-hat can be modelled against isolated CH/OH.

Measured against the SAMPLE path (ReBirth's built-in 808), effect rack and the
master compressor OFF. One voice per file, 4-bar loop at 120 BPM, one hit every
four steps so each decays into silence and step-locked windows stay clean.

Usage:  python gen_batch808cyoh.py [outdir]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-18", "808cyoh")
os.makedirs(OUT, exist_ok=True)

TEMPO, BARS, HITS4 = 120.0, 4, [0, 4, 8, 12]
# CY and OH ring much longer than the 0.5 s gap between HITS4 steps, so a decay
# sweep on that grid has its tail masked by the next hit. The decay sweeps use
# ONE hit per bar (step 0 only = 2 s apart) so each hit decays fully in isolation.
HITS1 = [0]

def emit(name, hits, knobs):
    path = os.path.join(OUT, name + ".rbs")
    data = build_song({}, [], tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="808", drum_knobs=knobs, drum_hits=hits)
    with open(path, "wb") as f:
        f.write(data)
    print("  ", name + ".rbs")

BASE = dict(accent=0, bd_level=0, sd_level=0, lt_level=0, mt_level=0,
            ht_level=0, rs_level=0, cp_level=0, cb_level=0, cy_level=0,
            oh_level=0, ch_level=0)

def only(voice, **extra):
    k = dict(BASE); k[voice + "_level"] = 100; k.update(extra); return k

FIVE = (0, 32, 64, 96, 127)

print("CY decay sweep (tone held at 64 so the sweep isolates decay; 1 hit/bar)")
for v in FIVE: emit(f"d808_cyd_{v:03d}", {"cy": HITS1}, only("cy", cy_decay=v, cy_tune=64))

print("OH decay sweep (1 hit/bar so the long tail is not masked)")
for v in FIVE: emit(f"d808_ohd_{v:03d}", {"oh": HITS1}, only("oh", oh_decay=v))

print("Level-law spot check for the two new voices (predict from level^2 * trim)")
emit("d808_cyl_064", {"cy": HITS4}, only("cy", cy_level=64, cy_decay=64, cy_tune=64))
emit("d808_ohl_064", {"oh": HITS4}, only("oh", oh_level=64, oh_decay=64))

print("Solo references: CH and CB alone (level 100), for trim + the hi-hat model")
emit("d808_ch_ref", {"ch": HITS4}, only("ch"))
emit("d808_cb_ref", {"cb": HITS4}, only("cb"))

man = os.path.join(OUT, "MANIFEST.md")
with open(man, "w", encoding="utf-8") as f:
    f.write("""# 808 CY/OH/CH batch (supertask 2, phase 2 continued)

Open each in ReBirth, **verify the master compressor is OFF**, export in song
mode, name the WAV after the .rbs. All 4-bar loops at 120 BPM, one voice per
file, one hit every four steps. Measured against ReBirth's built-in 808 (the
sample path). 14 files.

| files | pins down |
|---|---|
| `d808_cyd_000..127` | cymbal Decay (tone fixed at 64). Fit decay before trim. |
| `d808_ohd_000..127` | open-hat Decay. Fit decay before trim. |
| `d808_cyl_064` | cymbal level 64: confirm the level^2 fader law |
| `d808_ohl_064` | open-hat level 64: confirm the level^2 fader law |
| `d808_ch_ref` | closed hat alone (level 100): CH trim + hi-hat reference |
| `d808_cb_ref` | cowbell alone (level 100): confirm CB trim on the sample path |

The CH+OH one-step interaction fixture (`d808_choh_same`) is already exported in
the 2026-08-17 808complete batch; measure it against `d808_ch_ref` and
`d808_ohd_064`.
""")
print("\nwrote", man)
print("\n14 fixtures ->", OUT)
