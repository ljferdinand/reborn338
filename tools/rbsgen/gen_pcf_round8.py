#!/usr/bin/env python3
"""PCF round 8: one decade of the decay law, and the velocities properly.

Round 7 answered three things and left two half-answered. This finishes them.

BATCH F, the decay's fine structure. Round 7 established that tau multiplies by
exactly 10 every 48 bytes, confirmed seven times over from 9.79 to 10.06, and
that a PURE exponential through that decade is wrong by up to 29 percent in a
way that repeats exactly at 48-byte offsets. The same estimator on our own
renders, whose law is a pure exponential by construction, has a 2.6 percent
spread, so the structure belongs to ReBirth. Because it repeats, ONE decade
characterises the whole knob: bytes 48 to 96 every two bytes.

BATCH V, the velocities. Round 7's batch P used Decay 8, which was a design
error: tau there is about 13 ms against an 11.6 ms analysis window, so the
peaks are smeared into the floor and most fixtures resolved nothing. Its one
long-decay fixture, `t_p34_tiny`, resolved 93 percent of frames and read
ReBirth's alternating velocities as 1.012 and 0.042 against Appendix D's drawn
1.00 and 0.05. That configuration is what this batch uses everywhere:

    Freq 64, Amt 63, Decay 96, Q 0, LP

Freq 64 puts velocity 0 at 372 Hz rather than at 9 Hz, so the BOTTOM of the
velocity range stays inside what the probe can resolve, which is what the
Amt-127 pass could not do. Decay 96 is about 886 ms against an 83 ms
thirty-second, so the envelope barely moves between triggers and each reading
is the velocity itself rather than a point on a decay.

That works because round 7 settled the retrigger rule: a trigger RESETS the
envelope to its velocity. Pattern 34 proved it, dropping from 1.012 to 0.042 in
one thirty-second against an 886 ms decay, which no max-style or smoothed rule
can do.

    python gen_pcf_round8.py --out-dir ../../../fixtures-2026-08-29/pcf-round8
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

MODE_LP = 1

PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100,
                   ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

TEMPO, BARS = 90.0, 4

# One decade, every two bytes. Pattern 0 fires on the whole note, the longest
# unobstructed decay any preset offers.
F_BYTES = tuple(range(48, 97, 2))

# The twelve patterns round 7 could not read, plus the three it could, so the
# ones already answered come back through the same estimator as a check.
V_SET = (9, 12, 13, 16, 17, 24, 25, 32, 34, 42, 44, 52)
V_PCF = dict(on=1, freq=64, q=0, amt=63, decay=96, mode=MODE_LP)


def cases():
    out = [("r8_ref", None, "broadband denominator, PCF routed nowhere")]
    for b in F_BYTES:
        out.append((f"f_{b:03d}", dict(on=1, freq=0, q=0, amt=127, pattern=0,
                                       decay=b, mode=MODE_LP),
                    f"decay fine structure at byte {b}"))
    for n in V_SET:
        out.append((f"v_pat{n:02d}", dict(V_PCF, pattern=n),
                    f"pattern {n} velocities, in the configuration that resolves"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "pcf-round8"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, pcf, why in cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS,
                            pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why))
        print(f"  {name}.rbs  {len(data):,} bytes")

    man = ["# PCF round 8, 2026-08-29", "",
           "One decade of the decay law at two-byte resolution (F), and the",
           "preset velocities in the only configuration round 7 found that",
           "resolves them (V: Freq 64, Amt 63, Decay 96).", "",
           "All at 90 BPM, 4 bars, broadband probe. Every measurement is a ratio",
           "against `r8_ref`, which has the PCF routed NOWHERE. Round 7's second",
           "pass wasted nine fixtures on a reference that had the PCF ON with Amt",
           "at zero, which divides one 2-pole response by another and is not a",
           "denominator at all.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
