#!/usr/bin/env python3
"""Compressor BALLISTICS campaign (clean-room, supertask 6, 2026-09-16).

Attack / release timing from a level-STEP source. A steady 303 drone is stepped
between loud (fader 127, -15.8 dBFS) and quiet (fader 40, -48 dBFS) at two-bar
boundaries via fader automation (rbsgen vol_events). At 120 BPM a bar is 2 s, so
each level block is 4 s, long enough to see the gain reach steady state.

  step_off        : the stepped source, comp OFF. VALIDATION: its level must step
                    SHARPLY. If ReBirth smooths the fader, the ballistics read is
                    confounded and a different step source is needed.
  step_r127_t000  : comp ON, ratio 127, threshold 0 (biggest GR swing -> clearest
                    attack on the quiet->loud onset and release on loud->quiet).
  step_r096_t032  : a second setting; attack/release time constants should be
                    the same (they are timing, not amount).

Transitions (n_bars=8, 32 ticks/bar): loud 0-63, quiet 64-127, loud 128-191,
quiet 192-223, then the song-end marker restores loud at 224. Read attack at the
tick-128 onset (4 s loud follows) and release at the tick-64 onset (4 s quiet
follows). One 303, master compressor, no other effect.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]
def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)

STEPS = [(64, 40), (128, 127), (192, 40)]   # loud/quiet/loud/quiet by 2-bar blocks

def cases():
    return [
        ("step_off",       None,        "stepped source, comp OFF (validate sharp step)"),
        ("step_r127_t000", C(127, 0),   "ratio 127 thr 0 (biggest GR swing)"),
        ("step_r096_t032", C(96, 32),   "ratio 96 thr 32 (time-constant cross-check)"),
    ]

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-16", "comp-ballistics"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, why in cases():
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=8, title=name,
                            author="rbsgen", device="bass1", comp=comp,
                            chan_vol=127, vol_events=STEPS)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# Compressor BALLISTICS campaign (2026-09-16)", "",
           "Level-step 303 drone (fader 127<->40 by 2-bar blocks) through the",
           "master compressor. Read attack at the tick-128 onset, release at the",
           "tick-64 onset. One 303, no other effect.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
