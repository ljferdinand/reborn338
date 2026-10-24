#!/usr/bin/env python3
"""808 completeness batch (supertask 2, phase 2). See docs/COMPONENT_MATRIX.md.

The earlier 808 rounds (see docs/research/DRUM808_FINDINGS.md) chased the interesting
residual on the control that was already roughly right. This batch fills the
coverage the deep dive requires: every timbre control as a real sweep rather
than two endpoints, all three tom tunes (only the low tom was ever swept), the
tone/snappy interaction as a cross, and the CH+OH one-step interaction.

Measured against the SAMPLE path (ReBirth's built-in 808), effect rack and the
master compressor OFF. One voice per file, 4-bar loop at 120 BPM, one hit every
four steps so each decays into silence and step-locked windows stay clean.

Usage:  python gen_batch808complete.py [outdir]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-17", "808complete")
os.makedirs(OUT, exist_ok=True)

TEMPO, BARS, HITS4 = 120.0, 4, [0, 4, 8, 12]

def emit(name, hits, knobs):
    path = os.path.join(OUT, name + ".rbs")
    data = build_song({}, [], tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="808", drum_knobs=knobs, drum_hits=hits)
    with open(path, "wb") as f:
        f.write(data)
    print("  ", name + ".rbs")

# Everything silent except the voice under test (accent off so it cannot colour
# the level; other columns muted so nothing else sounds).
BASE = dict(accent=0, bd_level=0, sd_level=0, lt_level=0, mt_level=0,
            ht_level=0, rs_level=0, cp_level=0, cb_level=0, cy_level=0,
            oh_level=0, ch_level=0)

def only(voice, **extra):
    k = dict(BASE); k[voice + "_level"] = 100; k.update(extra); return k

FIVE  = (0, 32, 64, 96, 127)
THREE = (0, 64, 127)

print("Timbre sweeps (5 points each; the interacting control held at its 64 default)")
for v in FIVE: emit(f"d808_bdt_{v:03d}", {"bd": HITS4}, only("bd", bd_tone=v))
for v in FIVE: emit(f"d808_sdt_{v:03d}", {"sd": HITS4}, only("sd", sd_tone=v, sd_snappy=64))
for v in FIVE: emit(f"d808_sds_{v:03d}", {"sd": HITS4}, only("sd", sd_snappy=v, sd_tone=64))
for v in FIVE: emit(f"d808_cyt_{v:03d}", {"cy": HITS4}, only("cy", cy_tune=v, cy_decay=64))

print("Snare tone x snappy cross (the four corners; centre is d808_sd_ref)")
for t in (0, 127):
    for s in (0, 127):
        emit(f"d808_sdx_t{t:03d}s{s:03d}", {"sd": HITS4}, only("sd", sd_tone=t, sd_snappy=s))

print("Tom tunes, TOM side (switch=1): all three, only the low tom was ever swept")
for v in THREE: emit(f"d808_ltt_{v:03d}", {"lt": HITS4}, only("lt", lt_tune=v, lt_conga=1))
for v in THREE: emit(f"d808_mtt_{v:03d}", {"mt": HITS4}, only("mt", mt_tune=v, mt_conga=1))
for v in THREE: emit(f"d808_htt_{v:03d}", {"ht": HITS4}, only("ht", ht_tune=v, ht_conga=1))

print("Tom tune, CONGA side (switch=0): confirms the tune is shared with the conga")
for v in THREE: emit(f"d808_ltc_{v:03d}", {"lt": HITS4}, only("lt", lt_tune=v, lt_conga=0))

print("CH + OH on one step: documented to give a very short open hat (p149)")
k = dict(BASE); k["ch_level"] = 100; k["oh_level"] = 100
emit("d808_choh_same", {"ch": HITS4, "oh": HITS4}, k)

print("Bass-drum level law (does the fader map linearly to output level)")
for v in (32, 64, 96, 127): emit(f"d808_bdl_{v:03d}", {"bd": HITS4}, only("bd", bd_level=v))

man = os.path.join(OUT, "MANIFEST.md")
with open(man, "w", encoding="utf-8") as f:
    f.write("""# 808 completeness batch (supertask 2, phase 2)

Open each in ReBirth, **verify the master compressor is OFF**, export in song
mode, name the WAV after the .rbs. All are 4-bar loops at 120 BPM, one voice per
file, one hit every four steps. Measured against ReBirth's built-in 808 (the
sample path). 41 files.

## Timbre sweeps (20 files)
Each timbre control as a five-point sweep (000/032/064/096/127); two endpoints
cannot tell a curve from a line. The interacting control is held at its 64
default so the sweep isolates one knob.

| files | knob |
|---|---|
| `d808_bdt_*` | bass-drum Tone |
| `d808_sdt_*` | snare Tone (snappy fixed at 64) |
| `d808_sds_*` | snare Snappy (tone fixed at 64) |
| `d808_cyt_*` | cymbal Tone (decay fixed at 64) |

## Snare tone x snappy cross (4 files)
`d808_sdx_t{000,127}s{000,127}`. Round 6 found the tone knob also drives the
noise level, so tone and snappy interact; the corners plus the `d808_sd_ref`
centre pin the interaction.

## Tom tunes (12 files)
Only the low tom's tune was ever swept. These sweep all three on the TOM side
(switch=1) and confirm the tune is shared with the conga on one column.

| files | knob |
|---|---|
| `d808_ltt_*` / `d808_mtt_*` / `d808_htt_*` | low / mid / hi tom Tune, tom side |
| `d808_ltc_*` | low column Tune with the switch on the CONGA side |

## Interactions and level (5 files)
| file | tells us |
|---|---|
| `d808_choh_same` | CH and OH on one step; documented to yield a very short open hat |
| `d808_bdl_032/064/096/127` | whether the level fader maps linearly to output level |
""")
print("\nwrote", man)
print("\n41 fixtures ->", OUT)
