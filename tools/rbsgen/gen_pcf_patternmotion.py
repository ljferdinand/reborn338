#!/usr/bin/env python3
"""PCF pattern MOTION probe (clean-room, 2026-09-14).

Luke's ear: on pat16 ReBirth is one wide pendulous sweep where ours is a fast
series of mini-sweep hits. The listen fixtures use the sparse HOUSE GROOVE (kick/
snare/hats), so the filter is only excited during hits and its motion cannot be
tracked cleanly between them. This probe drives the SAME patterns with the
CONTINUOUS broadband source (crash+hat+kick every sixteenth) so the resonant peak
is always excited and the cutoff motion over the bar is directly measurable, RB vs
ours. Same PCF settings as the listen set (F70 Q90 A90 decay 64, tempo 130), so it
diagnoses exactly what Luke heard. Ratio vs `pmo_ref`. Nothing binary-derived.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G
MODE_LP = 1
TEMPO = 130
DECAY = 64
FREQ, Q, AMT = 70, 90, 90
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100, ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}
PATTERNS = [0, 5, 16, 2, 19]     # single-trigger ref, dense full-vel, the problem, and two more

def cases():
    out = [("pmo_ref", None, "broadband denominator, PCF routed nowhere")]
    for n in PATTERNS:
        out.append((f"pmo_p{n:02d}",
                    dict(on=1, freq=FREQ, q=Q, amt=AMT, pattern=n, decay=DECAY, mode=MODE_LP),
                    f"pattern {n} at F{FREQ} Q{Q} A{AMT} decay {DECAY} LP"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-14", "pcf-patternmotion"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=float(TEMPO), n_bars=4, title=name, author="rbsgen",
                            device="909", drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# PCF pattern-motion probe, 2026-09-14", "",
           "Continuous broadband source (crash+hat+kick every 16th) driving the",
           "listen patterns at F70 Q90 A90 decay 64 tempo 130, so the cutoff motion",
           "is trackable over the whole bar. Ratio vs `pmo_ref`.", "",
           "| fixture | what it is |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
