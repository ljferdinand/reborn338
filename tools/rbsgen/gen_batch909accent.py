#!/usr/bin/env python3
"""909 accent batch (supertask 3, phase 3). The accent law is the last 909
control with no measurement behind it.

Design follows the 808's, which is what caught the real behaviour there. Each
file puts four kick hits at steps 0/4/8/12 and accents only steps 0 and 8, so the
accented and unaccented hits sit in the SAME render: their ratio needs no level
matching and cannot be confounded by a trim or a knob difference between files.
The knob is then swept across files.

The `norow` pair is the control that matters. On the 808 the accent knob alone
turned out to do nothing at all, moving only steps whose accent row is set, and
an earlier round got that wrong by varying knob and row together. These two files
hold the row clear at both knob extremes, so if they differ, the knob has a global
effect and the 808's law does not carry over.

Usage:  python gen_batch909accent.py [outdir]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-25", "909accent")
os.makedirs(OUT, exist_ok=True)
TEMPO, BARS, HITS4 = 120.0, 4, [0, 4, 8, 12]

BASE = dict(bd_level=100, sd_level=0, lt_level=0, mt_level=0, ht_level=0,
            rs_level=0, cp_level=0, hh_level=0, cr_level=0, rd_level=0,
            bd_tune=64, bd_attack=64, bd_decay=64)

def emit(name, knobs, accents):
    data = build_song({}, [], tempo=TEMPO, title=name, author="rbsgen",
                      n_bars=BARS, device="909", drum_knobs=knobs,
                      drum_hits={"bd": HITS4}, drum_accents=accents)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(data)
    print("  ", name + ".rbs")

print("Accent knob sweep, row SET on steps 0 and 8 (4 and 12 stay unaccented)")
for v in (0, 32, 64, 96, 127):
    k = dict(BASE); k["accent"] = v
    emit(f"d909_acc_{v:03d}", k, (0, 8))

print("Control: row CLEAR at both knob extremes (does the knob alone do anything?)")
for v in (0, 127):
    k = dict(BASE); k["accent"] = v
    emit(f"d909_acc_norow_{v:03d}", k, ())

print(f"\nWrote the 909 accent batch to {OUT}")
