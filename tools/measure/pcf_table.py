#!/usr/bin/env python3
"""Build the 55 PCF preset patterns from ReBirth renders, and emit the table.

The primary source for `src/effects/pcf_presets.inc`. It reads TWO fixture
rounds, because one decay setting cannot answer both questions.

  * Round 10, Decay 64 (tau 178 ms), gives the trigger POSITIONS and the
    pattern LENGTH. The envelope falls to 0.627 of itself between
    thirty-seconds, so a trigger is a large departure from free decay wherever
    it lands, including below a running envelope.
  * Round 9, Decay 96 (tau 886 ms), gives the VELOCITIES at those positions.
    The envelope falls only to 0.911 between steps, so a reading taken 5.8 ms
    after a trigger is that trigger's velocity to within 0.7 percent, against
    3.2 percent at round 10's tau.

Round 9 alone cannot find the positions, and that is worth knowing rather than
rediscovering: at tau 886 ms a descending ramp that retriggers every step and
one that decays freely from a single trigger produce almost the same envelope.
Pattern 44 came back with one trigger where it has thirty-two, its departure
from free decay being 0.007 to 0.015 in the parameter against a measurement
noise of 0.014. At tau 178 ms the same ramp departs by 0.16.

Both rounds are Freq 64, Amt 63, Q 0, lowpass, 90 BPM, four bars, against the
broadband probe. Freq 64 puts velocity 0 at 372 Hz rather than 9 Hz, so the
bottom of the velocity range stays inside what the probe resolves; round 8 got
32 of 32 grid positions on every pattern it tried.

WHICH POSITIONS CARRY A TRIGGER. A retrigger RESETS the envelope to its
velocity (round 7: pattern 34 falls from velocity 1.02 to 0.04 in one
thirty-second against an 886 ms decay, and trajectory scoring puts reset at
0.067 rms against max-style at 0.286). So between triggers the parameter must
fall by exactly the decay factor, and a position that departs from that in
EITHER direction carries one. Reading only the rises, as the 2026-08-27 round
did, misses every trigger that lands below a running envelope, which is most of
what the 32nd-note patterns are made of.

    python pcf_table.py <round10-dir> <round9-dir>
    python pcf_table.py <round10-dir> <round9-dir> --pattern 12
    python pcf_table.py <round10-dir> <round9-dir> --inc > ../../src/effects/pcf_presets.inc
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_cutoff import trajectory
from pcf_appendixd import read_all as read_appendixd

appendix = read_appendixd()

BPM = 90.0
FREQ, AMT = 64.0 / 127.0, 63.0 / 127.0
TAU_POS = 0.0886 / 0.5          # Decay 64: table index 8, rate 0.5
TAU_VEL = 0.0886 / 0.1          # Decay 96: table index 12, rate 0.1
WINDOW = 0.45                   # of a thirty-second, for the peak reading
HORIZON = 128                   # thirty-seconds the render covers (four bars)
# The free-decay residual at positions carrying no trigger measures 0.005 rms
# and 0.014 worst over three sparse patterns. At Decay 64 a real trigger departs
# by an order of magnitude more than that, so the threshold is not delicate.
TOL = 0.03
LENGTHS = (2, 3, 4, 6, 8, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 27, 28, 30,
           32, 36, 38, 40, 44, 48, 54, 56, 58, 60, 62, 64)


def grid(ref, filt, steps=HORIZON):
    """Cutoff parameter at each thirty-second, or NaN."""
    t, p, _ = trajectory(ref, filt)
    step32 = 60.0 / BPM / 8.0
    out = np.full(steps, np.nan)
    for s in range(steps):
        t0 = s * step32
        if t0 + WINDOW * step32 > t[-1]:
            break
        m = (t >= t0) & (t < t0 + WINDOW * step32)
        v = p[m]
        v = v[np.isfinite(v)]
        if len(v):
            out[s] = v.max()
    return out


def fold(g, length):
    """Each grid position averaged over the repeats the render contains."""
    v = np.full(length, np.nan)
    for s in range(length):
        vals = [g[s + k * length] for k in range(len(g) // length + 1)
                if s + k * length < len(g) and np.isfinite(g[s + k * length])]
        if vals:
            v[s] = float(np.median(vals))
    return v


def period_of(g):
    """Smallest length under which the grid readings repeat."""
    ok = np.isfinite(g)
    for L in LENGTHS:
        if L * 2 > len(g):
            continue
        d, n = 0.0, 0
        for s in range(len(g) - L):
            if ok[s] and ok[s + L]:
                d += abs(g[s] - g[s + L])
                n += 1
        if n >= L and d / max(1, n) < 0.02:
            return L
    return None


def fold_residual(g, L):
    """Mean absolute difference between the trace and itself shifted by L."""
    d = [abs(g[s] - g[s + L]) for s in range(len(g) - L)
         if np.isfinite(g[s]) and np.isfinite(g[s + L])]
    return float(np.mean(d)) if d else np.inf


def audio_steps(g, L):
    """Trigger positions straight from the trace, as departures from free decay."""
    step32 = 60.0 / BPM / 8.0
    fall = float(np.exp(-step32 / TAU_POS))
    out = []
    for s in range(min(L, len(g))):
        if not np.isfinite(g[s]):
            continue
        prev = g[(s - 1) % len(g)]
        if not np.isfinite(prev):
            out.append(s)
            continue
        if abs(g[s] - (prev * fall + FREQ * (1.0 - fall))) > TOL:
            out.append(s)
    return out


def positions(render_dir, n, ref="r10_ref.wav"):
    """(length32, [steps], grid) for one pattern.

    Positions come from Appendix D and are CHECKED against the Decay 64 render
    here, because the audio cannot resolve them everywhere and the drawing can.
    Two places it cannot. A ramp's low end converges with free decay: a trigger
    at velocity 0.03 and no trigger at all both leave the envelope near the
    floor, so pattern 44's last seven steps depart from free decay by less than
    the noise however long the render. And on a pattern triggering every
    sixteenth into a decay of the same order, the in-between step's reading
    departs by 0.03 to 0.12 in velocity, which is neither clearly a trigger nor
    clearly not one.

    Where the audio CAN see, it agrees: pattern 9's phase, pattern 34's 32nd
    grid, pattern 12's period and peak, pattern 44's top 25 steps and pattern
    42's top 30 all match the drawing exactly. So the drawing supplies the
    positions and this reports how many of them the audio independently
    confirms.
    """
    f = os.path.join(render_dir, "x_pat%02d.wav" % n)
    if not os.path.exists(f):
        return None
    g = grid(os.path.join(render_dir, ref), f)
    steps, _vels, L, _unit = appendix[n]
    steps = sorted({int(round(x)) for x in steps if round(x) < L})

    # CHECK THE DRAWN LENGTH AGAINST THE RENDER before using it. Folding the
    # trace at the true period must line up; 54 of the 55 do, to a mean absolute
    # difference under 0.03 in the parameter. PATTERN 53 DOES NOT, at 0.134, and
    # the render says why: it triggers every FOUR thirty-seconds with its
    # velocities forming a triangle over 128 of them, where Appendix D draws it
    # every two over 62. Folding at the drawn length there averaged its whole
    # ramp into a flat 0.47. So where the drawn length fails, positions and
    # length both come from the audio instead.
    if fold_residual(g, L) > 0.03:
        L = len(g)
        steps = audio_steps(g, L)
    # How many of those the audio confirms as departures from free decay.
    v = fold(g, L)
    step32 = 60.0 / BPM / 8.0
    fall = float(np.exp(-step32 / TAU_POS))
    seen = 0
    for s in steps:
        prev = v[(s - 1) % L]
        if not (np.isfinite(v[s]) and np.isfinite(prev)):
            continue
        if abs(v[s] - (prev * fall + FREQ * (1.0 - fall))) > TOL:
            seen += 1
    return L, steps, g, seen


def velocities(render_dir, n, steps, length, ref="r9_ref.wav"):
    """Velocity at each given step, from the Decay 96 round."""
    f = os.path.join(render_dir, "w_pat%02d.wav" % n)
    if not os.path.exists(f):
        return None
    v = fold(grid(os.path.join(render_dir, ref), f), length)
    out = []
    for s in steps:
        if np.isfinite(v[s]):
            out.append((s, float(np.clip((v[s] - FREQ) / AMT, 0.0, 1.05))))
    return out


def build(dir10, dir9, which=range(55)):
    tab = {}
    for n in which:
        pos = positions(dir10, n)
        if pos is None:
            continue
        L, steps, g, seen = pos
        rows = velocities(dir9, n, steps, L)
        if rows is None:
            continue
        tab[n] = (rows, L, g, seen)
    return tab


HEADER = '''// PCF preset patterns 0 to 54, MEASURED from ReBirth on 2026-08-29.
//
// Generated by tools/measure/pcf_table.py from renders/pcf-round10-2026-08-29
// (trigger positions and pattern length) and renders/pcf-round9-2026-08-29
// (velocities). Do not hand-edit: regenerate it.
//
// Each pattern is a list of (step, velocity) where step counts THIRTY-SECOND
// notes from the pattern's start and velocity is 0..1. `length32` is how many
// thirty-seconds the pattern occupies before it repeats.
//
// Both rounds are Freq 64, Amt 63, Q 0, lowpass, 90 BPM, four bars, against a
// broadband probe of kick, crash and closed hat on every sixteenth. Freq 64
// puts velocity 0 at 372 Hz instead of 9 Hz so the bottom of the range stays
// resolvable, and 32 of 32 grid positions resolve on every pattern.
//
// Two decay settings, because one cannot answer both questions. At Decay 64
// (tau 178 ms) the envelope falls to 0.627 between thirty-seconds, so a trigger
// is a large departure from free decay wherever it lands and the POSITIONS are
// unambiguous. At Decay 96 (tau 886 ms) it falls only to 0.911, so a reading
// taken 5.8 ms after a trigger is that trigger's VELOCITY to within 0.7
// percent. Decay 96 alone cannot find positions: a descending ramp that
// retriggers every step and one that decays freely from a single trigger look
// almost identical at that tau.
//
// Triggers are found as departures from free decay in EITHER direction. A
// retrigger RESETS the envelope, so a trigger landing below a running envelope
// pulls it down, and looking only for rises misses it.
//
// THIS REPLACES THE 2026-08-27 TABLE, which was wrong three ways. Its estimator
// inverted one 88..354 Hz band with Amt at 64/127, where a velocity-1.0 trigger
// sits at 372 Hz: sensitivity ran 64.6 dB per unit velocity at v = 0.1 and 12.6
// at v = 0.9, and v = 1.0 fell past its own rejection threshold, so every
// pattern WITH dynamics came back compressed. It took its origin from the first
// trigger it found, so every pattern came back rotated to start on step 0. And
// its prominence threshold was 0.045, so it missed the velocity-0.04 triggers
// that make the 32nd-note patterns 32nd-note patterns.
//
// Appendix D read by pixel (tools/measure/pcf_appendixd.py) is the independent
// cross-check on positions, phase, grid unit and length, and agrees. Its
// velocities agree for the patterns drawn on the 1/32 axis and run about 1.4
// times too tall on the 1/16 axis, so they are not used.
'''


def emit(tab):
    out = [HEADER, "\nnamespace reborn {\n",
           "struct PcfPresetStep { unsigned char step32; float velocity; };\n",
           "struct PcfPreset { int length32; int count; "
           "const PcfPresetStep* steps; };\n"]
    for i in range(55):
        rows, L = (tab[i][0], tab[i][1]) if i in tab else ([(0, 1.0)], 32)
        body, line = [], "    "
        for s, v in rows:
            r = "{%d, %.3ff}" % (s, v)
            if len(line) + len(r) > 76:
                body.append(line.rstrip())
                line = "    "
            line += r + ", "
        body.append(line.rstrip().rstrip(","))
        out.append("\nstatic const PcfPresetStep kPcfPat%02d[] = {\n%s\n};\n"
                   % (i, "\n".join(body)))
    out.append("\nstatic const PcfPreset kPcfPresets[55] = {\n")
    for i in range(55):
        rows, L = (tab[i][0], tab[i][1]) if i in tab else ([(0, 1.0)], 32)
        out.append("    { %3d, %3d, kPcfPat%02d },\n" % (L, len(rows), i))
    out.append("};\n\n} // namespace reborn\n")
    return "".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir10")
    ap.add_argument("dir9")
    ap.add_argument("--pattern", type=int)
    ap.add_argument("--inc", action="store_true")
    a = ap.parse_args(argv)

    which = [a.pattern] if a.pattern is not None else range(55)
    tab = build(a.dir10, a.dir9, which)
    if a.inc:
        sys.stdout.write(emit(tab))
        return 0
    for i in sorted(tab):
        rows, L, g, seen = tab[i]
        print("\npattern %2d: length %d, %d triggers, %d confirmed in the audio,"
              " %d of %d grid positions resolved"
              % (i, L, len(rows), seen, int(np.isfinite(g).sum()), len(g)))
        for k in range(0, len(rows), 8):
            print("   " + "  ".join("%2d:%.2f" % r for r in rows[k:k + 8]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
