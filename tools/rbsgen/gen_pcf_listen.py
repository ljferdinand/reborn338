#!/usr/bin/env python3
"""PCF listening set, 2026-09-05: the 909 sample-mode groove through the PCF, a
large sample of patterns and levels, for Luke's ear against ReBirth.

Standard testing scenario: the 909 in SAMPLE mode (the one clean single-device
null, 0.13 dB), rack off except the PCF, master compressor off. Every fixture is
ONE engine (909) plus ONE effect (the PCF) and nothing else, per the listening
protocol; the set opens with the NULL PAIR (the same groove, PCF routed nowhere)
so the same-source baseline is heard first. Render OURS with
`--samples samples-standard-909` so the source matches ReBirth and the ear judges
the PCF, not 909 synthesis.

    python gen_pcf_listen.py --out-dir ../../../fixtures-2026-09-05/pcf-listen
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

MODE_LP, MODE_BP = 1, 0
TEMPO, BARS = 130.0, 4

# A plain house groove: four-on-the-floor kick, backbeat snare, eighth-note
# closed hats, offbeat open hats for the sustained content the filter sweeps.
HITS = {
    "bd": [0, 4, 8, 12],
    "sd": [4, 12],
    "ch": [0, 2, 4, 6, 8, 10, 12, 14],
    "oh": [2, 6, 10, 14],
}
KNOBS = dict(bd_level=110, bd_decay=90, sd_level=100, sd_tone=64, sd_snappy=80,
             hh_level=100, ch_decay=60, oh_decay=90, accent=0)

# A large sample of patterns spanning every category the manual names, at one
# musical resonant level so the rhythmic filtering is clearly audible.
PAT_LEVEL = dict(on=1, freq=70, q=90, amt=90, decay=64, mode=MODE_LP)
PATTERNS = [0, 2, 5, 9, 12, 16, 20, 24, 28, 34, 40, 44, 50, 54]

# A spread of levels at one smooth-sweep pattern (16), across cutoff, resonance
# and mode, so the ear hears the filter's tone as well as its rhythm.
LVL_PAT = 16
LEVELS = [
    ("f040_q060", dict(freq=40,  q=60,  mode=MODE_LP)),
    ("f070_q060", dict(freq=70,  q=60,  mode=MODE_LP)),
    ("f100_q060", dict(freq=100, q=60,  mode=MODE_LP)),
    ("f070_q020", dict(freq=70,  q=20,  mode=MODE_LP)),
    ("f070_q110", dict(freq=70,  q=110, mode=MODE_LP)),
    ("f070_q090_bp", dict(freq=70, q=90, mode=MODE_BP)),
]


def cases():
    out = [("null", None, "NULL PAIR: the groove, PCF routed nowhere (calibration)")]
    for n in PATTERNS:
        out.append((f"pat{n:02d}", dict(PAT_LEVEL, pattern=n),
                    f"pattern {n} at Freq 70 Q 90 Amt 90 Decay 64 LP"))
    for name, lv in LEVELS:
        out.append((f"lvl_{name}", dict(on=1, amt=90, decay=64, pattern=LVL_PAT, **lv),
                    f"pattern {LVL_PAT}, {name.replace('_',' ')}"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-09-05", "pcf-listen"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=KNOBS, drum_hits=HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
        print(f"  {name}.rbs  {len(data):,} bytes")

    man = ["# PCF listening set, 2026-09-05", "",
           "The 909 sample-mode house groove through the PCF, a large sample of",
           "patterns and levels, ours against ReBirth. ONE engine, ONE effect,",
           "master compressor off. Opens with the NULL PAIR. Render ours with",
           "`--samples samples-standard-909`; export ReBirth's side from your own copy.",
           "Tempo 130, 4 bars (about 7.4 s each).", "",
           "| fixture | what it is |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
