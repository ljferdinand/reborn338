#!/usr/bin/env python3
"""Compressor BALLISTICS via drum transients (clean-room, supertask 6, 2026-09-16).

The fader-step ballistics probe was confounded by ReBirth gliding fader
automation over ~100 ms. A drum transient has an INSTANT onset (no glide), so it
is the clean stimulus.

  ATTACK: a loud 808 stack hit (near 0 dBFS, one hit per bar). Comparing the
  comp-ON and comp-OFF renders of the same hit gives gain(t) = on - off (dB): at
  the onset the gain starts at unity and clamps down over the attack time.

  RELEASE (the pumping method): a loud kick every bar over a steady stream of
  quiet closed hats. The kick slams the gain down; each successive hat after it
  is less reduced as the gain releases, so the hat levels between kicks trace the
  release curve. The hats sit below threshold so they read the gain without
  driving much reduction themselves.

One 808 (drum machines are one device even with several voices), master
compressor, no other effect. hit at bar downbeats, 120 BPM (2 s/bar of decay and
release room). No binary-analysis value is used.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# Loud, long-tailed stack for the attack probe.
STACK_KNOBS = dict(accent=127,
                   bd_level=127, bd_decay=127, sd_level=127,
                   lt_level=127, mt_level=127, ht_level=127,
                   cy_level=127, cy_decay=127, oh_level=127, oh_decay=127,
                   cb_level=127)
STACK_HITS = {"bd": [0], "sd": [0], "lt": [0], "mt": [0], "ht": [0],
              "cy": [0], "oh": [0], "cb": [0]}

# Loud kick over quiet steady hats for the release/pumping probe.
PUMP_KNOBS = dict(accent=127, bd_level=127, bd_decay=110, ch_level=45)
PUMP_HITS  = {"bd": [0], "ch": list(range(16))}

def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

def cases():
    return [
        ("hit_off",        STACK_KNOBS, STACK_HITS, [0], None,       "loud stack, comp OFF (attack ref)"),
        ("hit_r127_t064",  STACK_KNOBS, STACK_HITS, [0], C(127, 64), "loud stack, ratio127 thr64 (attack)"),
        ("hit_r096_t064",  STACK_KNOBS, STACK_HITS, [0], C(96, 64),  "loud stack, ratio96 thr64 (attack x-check)"),
        ("pump_off",       PUMP_KNOBS,  PUMP_HITS,  [0], None,       "kick+hats, comp OFF (release ref)"),
        ("pump_r096_t048", PUMP_KNOBS,  PUMP_HITS,  [0], C(96, 48),  "kick+hats, ratio96 thr48 (release/pumping)"),
    ]

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-ballistics2"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, dk, hits, acc, comp, why in cases():
        data = G.build_song({}, [], tempo=120.0, n_bars=8, title=name, author="rbsgen",
                            device="808", drum_knobs=dk, drum_hits=hits,
                            drum_accents=acc, comp=comp)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor BALLISTICS via drum transients (2026-09-16)", "",
           "Loud 808 stack (attack) and kick-over-quiet-hats (release/pumping)",
           "through the master compressor. One 808, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
