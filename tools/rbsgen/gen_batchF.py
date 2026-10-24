#!/usr/bin/env python3
"""Batch F: the leftovers from 808 round 3 (see docs/research/DRUM808_FINDINGS.md).

Two questions, both cheap to answer and both currently blocking real numbers:

  1. The TOM rows have never sounded in any fixture. Every tom-column switch
     defaulted to 0, so ReBirth played congas throughout, which means our mid
     and hi tom output levels are inferred from the low tom rather than
     measured, and offsets +14 / +17 are inferred from layout rather than
     confirmed. Setting each tom column to 1 in isolation fixes both.

  2. ReBirth's decay knobs appear to move LEVEL as well as length, and not
     always in the same direction (its cymbal drops 15 dB at full decay while
     its open hat holds). Two points is not a law, so these sweep each decay
     knob across five settings on an isolated voice.

Usage:  python gen_batchF.py [outdir]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-07-23", "batchF")
os.makedirs(OUT, exist_ok=True)

TEMPO  = 120.0
BARS   = 4
# One hit every 4 steps: far enough apart that each decays into silence at the
# short settings, and step-locked measurement stays unambiguous at the long ones.
HITS4  = [0, 4, 8, 12]

def emit(name, hits, knobs):
    path = os.path.join(OUT, name + ".rbs")
    data = build_song({}, [], tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="808",
                      drum_knobs=knobs, drum_hits=hits)
    with open(path, "wb") as f:
        f.write(data)
    print("  ", name + ".rbs")

# Silence everything except the voice under test.
BASE = dict(accent=0, bd_level=0, sd_level=0, lt_level=0, mt_level=0,
            ht_level=0, rs_level=0, cp_level=0, cb_level=0, cy_level=0,
            oh_level=0, ch_level=0)

def only(voice, **extra):
    k = dict(BASE)
    k[voice + "_level"] = 100
    k.update(extra)
    return k

print("Batch F: tom columns")
# Switch at 1 = the PRIMARY voice, i.e. the actual tom. Confirms +14/+17 and
# gives the mid/hi tom levels a direct measurement.
emit("d808_tomsw_mt1", {"mt": HITS4}, only("mt", mt_conga=1))
emit("d808_tomsw_ht1", {"ht": HITS4}, only("ht", ht_conga=1))
# Low tom repeated as the control: we already have it, so it validates the batch.
emit("d808_tomsw_lt1", {"lt": HITS4}, only("lt", lt_conga=1))

print("Batch F: decay-vs-level sweeps")
for val in (0, 32, 64, 96, 127):
    emit(f"d808_cyd_{val:03d}", {"cy": HITS4}, only("cy", cy_decay=val))
for val in (0, 32, 64, 96, 127):
    emit(f"d808_bdd_{val:03d}", {"bd": HITS4}, only("bd", bd_decay=val))
for val in (0, 64, 127):
    emit(f"d808_ohd_{val:03d}", {"oh": HITS4}, only("oh", oh_decay=val))

man = os.path.join(OUT, "MANIFEST.md")
with open(man, "w", encoding="utf-8") as f:
    f.write("""# Batch F: 808 round 3 leftovers

Same protocol as before: open in ReBirth, **verify the master compressor is
OFF**, export in song mode, name the WAV after the .rbs. All are 4-bar loops at
120 BPM, one voice per file, one hit every 4 steps.

## Tom columns (3 files)

The tom ROWS have never sounded in any fixture: every tom-column switch has
been at 0, so ReBirth played congas. These set the switch to 1.

| file | tells us |
|---|---|
| `d808_tomsw_lt1` | control: we already know this one, so it validates the batch |
| `d808_tomsw_mt1` | mid tom's real level, and confirms header offset +14 |
| `d808_tomsw_ht1` | hi tom's real level, and confirms header offset +17 |

## Decay vs level (13 files)

ReBirth's decay knobs seem to move level as well as length, and not consistently
(its cymbal drops ~15 dB at full decay; its open hat does not). These sweep one
knob at a time on an isolated voice so the relationship can be fitted rather
than guessed from two points.

| files | knob |
|---|---|
| `d808_cyd_000/032/064/096/127` | cymbal decay |
| `d808_bdd_000/032/064/096/127` | bass drum decay |
| `d808_ohd_000/064/127` | open hat decay |

16 files total.
""")
print("\nwrote", man)
