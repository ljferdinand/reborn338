#!/usr/bin/env python3
"""P2 delay-tap probe (clean-room measurement, 2026-09-10).

One short 909 rimshot on step 0 of each bar, routed to the delay with a strong
echo send, everything else off. A brief dry transient plus its echo train lets
us read, by measuring ReBirth's exported audio:

  - delay length versus Steps, the triplet switch, and tempo (first-echo offset);
  - feedback versus its byte (successive tap amplitude ratios);
  - whether the echo path filters or interpolates (per-echo spectrum, integer
    sample offsets);
  - where the pan acts and whether the first echo is split across channels
    (ping-pong) or not (L/R energy per echo).

These are open questions with decision rules, answered from the exports. No value
from any binary analysis is used here; the sweep just varies the controls. Ratio
each against `dl_ref` (same hit, delay send zero) for the isolation denominator.
Tempo 120 unless named; 909 rimshot source; comp/dist/PCF off.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

HIT = {"rs": [0]}          # one rimshot, step 0
SEND = 100                 # strong echo send so the wet train is well above noise

def D(steps=4, triplet=0, feedback=64, pan=64, send=SEND):
    return dict(on=1, steps=steps, triplet=triplet, feedback=feedback, pan=pan, send=send)

def cases():
    out = [("dl_ref", 120, dict(on=1, steps=4, triplet=0, feedback=64, pan=64, send=0),
            "denominator: the dry rimshot, delay send zero")]
    # length versus Steps (first-echo offset gives the delay time)
    for s in (1, 2, 3, 4, 6, 8):
        out.append((f"dl_s{s:02d}", 120, D(steps=s), f"length probe, Steps {s}"))
    # triplet switch at Steps 4
    out.append(("dl_trip", 120, D(steps=4, triplet=1), "triplet switch on, Steps 4"))
    # tempo (Steps 4): delay time versus BPM
    out.append(("dl_t090", 90,  D(steps=4), "tempo 90, Steps 4"))
    out.append(("dl_t144", 144, D(steps=4), "tempo 144, Steps 4"))
    # feedback byte (successive tap ratios)
    for f in (32, 96, 127):
        out.append((f"dl_fb{f:03d}", 120, D(steps=4, feedback=f), f"feedback probe, byte {f}"))
    # pan byte (return pan law; is the first echo split = ping-pong?)
    for p in (0, 32, 96, 127):
        out.append((f"dl_pan{p:03d}", 120, D(steps=4, pan=p), f"pan probe, byte {p}"))
    # straight grid (DELY byte 2 = 1) length sweep: pin the straight base and a
    # large Steps value the triplet sweep could not reach cleanly at 120 bpm.
    for s in (1, 2, 8, 16):
        out.append((f"dss_s{s:02d}", 120, D(steps=s, triplet=1), f"straight-grid length, Steps {s}"))
    # non-round tempos to expose the integer truncation in the length arithmetic.
    out.append(("dss_t100", 100, D(steps=4, triplet=1), "straight, Steps 4, tempo 100"))
    out.append(("dss_t123", 123, D(steps=4, triplet=1), "straight, Steps 4, tempo 123"))
    # echo-send level law (triplet grid, Steps 4): wet return level vs send byte,
    # with dl_ref (send 0) and dl_s04 (send 100) completing the curve.
    for s in (16, 48, 96):
        out.append((f"dsn_{s:03d}", 120, D(steps=4, send=s), f"send-level law, echo send {s}"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-09-10", "delay-taps"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, tempo, delay, why in cases():
        data = G.build_song({}, [], tempo=float(tempo), n_bars=4, title=name, author="rbsgen",
                            device="909", drum_hits=HIT, delay=delay)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    man = ["# P2 delay-tap probe, 2026-09-10", "",
           "One 909 rimshot on step 0, routed to the delay (send 100), rest off.",
           "Measure the echo train for length/feedback/filtering/pan from ReBirth",
           "exports. Ratio vs `dl_ref`. Tempo 120 unless named.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
