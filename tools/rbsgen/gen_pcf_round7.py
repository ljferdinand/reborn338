#!/usr/bin/env python3
"""PCF round 7: the preset velocities, the retrigger rule, and the decay law.

Three questions, one export batch, all on the broadband probe.

WHY THE PROBE AND NOT THE DRONE. A kick, a crash and a closed hat on every
sixteenth cover roughly 30 Hz to 15 kHz, densely enough that no harmonic grid
constrains the analysis window. Round 5 already established that. What round 5
then did wrong was the ANALYSIS: it read the cutoff out of an 88..354 Hz band
with Amt at 64/127, and at that Amt a velocity-1.0 trigger puts the corner at
372 Hz, the very top of that band. Its sensitivity ran 64.6 dB per unit velocity
at v = 0.1 and 12.6 at v = 0.9, and v = 1.0 landed where its own rejection
threshold threw it away. So the top of the velocity range came back compressed
five to one and its very top not at all.

Batch P fixes that by construction. **Freq 0 and Amt 127 make the cutoff
parameter equal to the velocity**, so the whole range is measured on one scale
with no inversion of a band ratio, and the probe resolves the parameter from
about 0.21 (43 Hz) to 1.00 (14.7 kHz).

    python gen_pcf_round7.py --out-dir ../../../fixtures-2026-08-29/pcf-round7
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

MODE_LP, MODE_BP = 1, 0        # byte 0 is BANDPASS, measured 2026-08-27

# The broadband probe, unchanged from round 4 so the two rounds are comparable.
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100,
                   ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}

# --------------------------------------------------------------------------
# Batch P: the velocities, and the phase.
#
# Twelve patterns, chosen so that each one answers something the shipped table
# and Appendix D disagree about:
#
#   9   PHASE. Same rhythm in both sources, offset by exactly one sixteenth.
#       Round 5 took its origin from the first trigger it found, so every
#       pattern came out rotated to start on step 0. This is the clean case.
#   12  The largest velocity disagreement. Drawn as a symmetric 0.03 to 0.90
#       triangle, read by round 5 as a noisy 0.14 to 0.34.
#   13  The same shape over two bars, so a longer pattern is covered too.
#   16  Four steps, length 4. Short patterns exercise the length column, which
#       round 5 called its own weakest.
#   17  Length 3, the shortest of the 55.
#   24  G-Spot's pattern per its PCF chunk, with two of the disputed tiny bars.
#   25  Sixteen triggers with dynamics on every sixteenth.
#   32  G-Spot's pattern per its automation track. Round 5 read length 96 where
#       the drawing says 54, and shares only 15 percent of its steps.
#   34  The first of the 32nd-note group: drawn as a trigger on EVERY
#       thirty-second, alternating 1.00 with 0.04, and read by round 5 as plain
#       sixteenths because its prominence threshold was 0.045.
#   42  A 32-step ramp on the 32nd grid, 0.03 to 1.00. Velocity and grid unit
#       in one fixture.
#   44  The same ramp descending, which is also the cleanest retrigger test in
#       the set: every trigger after the first is BELOW the envelope it lands on.
#   52  In the mixed group, and one of the three there drawn on the 32nd grid.
PAT_SET = (9, 12, 13, 16, 17, 24, 25, 32, 34, 42, 44, 52)

# Freq 0 and Amt 127: cutoff parameter IS velocity. Q 0 so the corner does not
# ride the resonance. Decay 8 (tau about 15 ms by the current law, which is the
# law under test, so this is deliberately at the fast end) so each trigger dies
# before the next thirty-second even at 90 BPM, where a thirty-second is 83 ms.
P_TEMPO, P_BARS = 90.0, 4
P_PCF = dict(on=1, freq=0, q=0, amt=127, decay=8, mode=MODE_LP)

# AND A SECOND PASS THAT BRACKETS IT FROM BELOW, because Amt 127 walks into the
# mirror image of round 5's own trap. With Freq 0 and Amt 127 a velocity-1.0
# trigger puts the corner at 14.7 kHz, at the very top of what the probe can
# resolve, and a velocity-0.04 one puts it at 12 Hz, below the bottom. So that
# setting is linear across the middle and saturates at BOTH ends.
#
# Freq 40 with Amt 63 maps velocity 0 to 1 onto 82 Hz to 3.1 kHz, every bit of
# which the probe resolves comfortably. It compresses the scale, so it is the
# worse measurement of the top; it is the better one of everything else, and
# where the two overlap they have to agree or neither is trusted.
P2_PCF = dict(on=1, freq=40, q=0, amt=63, decay=8, mode=MODE_LP)
P2_SET = (9, 12, 13, 24, 32, 34, 44, 52)


def batch_p():
    out = [("p_ref", None, "broadband denominator for batch P")]
    for n in PAT_SET:
        out.append((f"p_pat{n:02d}", dict(P_PCF, pattern=n),
                    f"pattern {n}: velocities where param = velocity exactly, "
                    f"resolved over the middle and top of the range"))
    out.append(("p2_ref", dict(on=1, freq=40, q=0, amt=0, decay=8, mode=MODE_LP),
                "denominator for the low pass: same Freq, Amt at zero"))
    for n in P2_SET:
        out.append((f"p2_pat{n:02d}", dict(P2_PCF, pattern=n),
                    f"pattern {n} again over 82 Hz to 3.1 kHz, which resolves the "
                    f"bottom of the velocity range the first pass cannot"))
    return out


# --------------------------------------------------------------------------
# Batch T: does a retrigger RESET the envelope or take the MAX of it?
#
# This is not a detail. Around a hundred steps across the 32nd-note patterns are
# drawn at velocity 0.04 to 0.05, standing apart from an otherwise continuous
# distribution with a gap between 0.07 and 0.09. Under a reset they slam a
# decaying envelope down to nothing every other thirty-second, which is a loud,
# distinctive sound. Under a max they are inaudible. Our engine resets today,
# on the strength of a comment in `fireTick` that says the traces showed every
# trigger reaching the same height, which is evidence about EQUAL velocities
# and says nothing about unequal ones.
#
# The discriminator is a LONG decay against a pattern whose velocities fall.
# Pattern 44 ramps 1.00 down to 0.03 over 32 thirty-seconds, so after the first
# trigger every one lands below the envelope already running:
#
#   reset -> the trace follows the ramp down in steps
#   max   -> the trace decays smoothly on its own and ignores every trigger
#
# Pattern 34 asks the same question about the tiny bars specifically, and
# pattern 12's descending half asks it a third way.
T_TEMPO, T_BARS = 90.0, 4
T_LONG = 96          # tau about 580 ms by the current law: many thirty-seconds


def batch_t():
    return [
        ("t_ref", None, "broadband denominator for batch T"),
        ("t_p44_long", dict(on=1, freq=0, q=0, amt=127, pattern=44,
                            decay=T_LONG, mode=MODE_LP),
         "descending ramp into a long decay: reset steps down, max decays through"),
        ("t_p12_long", dict(on=1, freq=0, q=0, amt=127, pattern=12,
                            decay=T_LONG, mode=MODE_LP),
         "triangle into a long decay: the falling half is the discriminator"),
        ("t_p34_tiny", dict(on=1, freq=64, q=0, amt=63, pattern=34,
                            decay=T_LONG, mode=MODE_LP),
         "are the 0.04 bars real triggers? Freq 64 keeps a 0.04 step resolvable"),
    ]


# --------------------------------------------------------------------------
# Batch D: the decay law, by trajectory rather than by one crossing time.
#
# Pattern 0 triggers once every 32 thirty-seconds, a whole note, which is the
# longest unobstructed decay any preset offers. Freq 0 and Amt 127 again, so the
# trace runs the full parameter range from 1.0 downward and the fit has the
# whole span to work with.
#
# The existing law came from the time each trigger took to carry the cutoff past
# ONE fixed frequency. Residuals reached 15 percent and byte 80 read 197.8 ms
# where the trend said 380, and a single crossing cannot tell a wrong time
# constant from a wrong shape.
#
# TWO TEMPI, because nothing has established whether the knob is in seconds or
# in beats. At 90 BPM a whole note is 2.67 s and at 30 BPM it is 8 s. If the law
# is absolute the fitted tau is the same at both; if it is tempo-synced the
# 30 BPM taus are three times the 90 BPM ones. The slow tempo also buys the long
# settings room to actually decay: the current law puts byte 127 at 2.31 s, which
# 2.67 s of whole note cannot resolve and 8 s can.
D_BYTES = (0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 127)
D_SLOW = (32, 64, 80, 96, 112, 127)     # the tempo check, and the long tail
D_FAST_TEMPO, D_FAST_BARS = 90.0, 4
D_SLOW_TEMPO, D_SLOW_BARS = 30.0, 3


def batch_d():
    out = [("d_ref90", None, "broadband denominator at 90 BPM")]
    for b in D_BYTES:
        out.append((f"d_t90_{b:03d}", dict(on=1, freq=0, q=0, amt=127,
                                           pattern=0, decay=b, mode=MODE_LP),
                    f"decay trajectory at byte {b}, 90 BPM"))
    return out


def batch_d_slow():
    out = [("d_ref30", None, "broadband denominator at 30 BPM")]
    for b in D_SLOW:
        out.append((f"d_t30_{b:03d}", dict(on=1, freq=0, q=0, amt=127,
                                           pattern=0, decay=b, mode=MODE_LP),
                    f"decay at byte {b}, 30 BPM: absolute or tempo-synced, "
                    f"and the long tail"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-29", "pcf-round7"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    groups = ((batch_p(), P_TEMPO, P_BARS),
              (batch_t(), T_TEMPO, T_BARS),
              (batch_d(), D_FAST_TEMPO, D_FAST_BARS),
              (batch_d_slow(), D_SLOW_TEMPO, D_SLOW_BARS))
    for cases, tempo, bars in groups:
        for name, pcf, why in cases:
            data = G.build_song({}, [], tempo=tempo, n_bars=bars, title=name,
                                author="rbsgen", device="909",
                                drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS,
                                pcf=pcf)
            open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
            rows.append((name, why, tempo, bars))
            print(f"  {name}.rbs  {len(data):,} bytes")

    man = [
        "# PCF round 7, 2026-08-29",
        "",
        "Three questions in one batch: the preset velocities (P), whether a",
        "retrigger resets the envelope or takes the max of it (T), and the decay",
        "law by trajectory fit (D). Broadband probe throughout: a kick, a crash",
        "and a closed hat on every sixteenth.",
        "",
        "Every measurement is a RATIO against the matching `_ref` render, made",
        "from the identical song with the PCF routed nowhere, so the probe's own",
        "spectrum divides out. Exports are not sample-deterministic: compare",
        "power spectra, never subtract waveforms.",
        "",
        "| fixture | BPM | bars | what it answers |",
        "|---|---|---|---|",
    ]
    man += [f"| `{n}` | {t:g} | {b} | {w} |" for n, w, t, b in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write(
        "\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
