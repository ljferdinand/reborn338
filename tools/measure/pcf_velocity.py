#!/usr/bin/env python3
"""Read a PCF preset's trigger positions and velocities off a round-7 render.

Replaces `pcf_patterns.py`, which is retired in place. Two things are different
and both of them are the point.

**The grid is known, so it is sampled and not searched.** Round 5 found triggers
by looking for upward jumps with a prominence above 0.045, then set step 0 to
the first one it found. That rotated every pattern whose first trigger is not on
the downbeat, and it silently dropped every trigger too small to clear the
threshold, which is what hid the 0.04 steps that make the 32nd-note patterns
32nd-note patterns. Here the fixture's tempo is known, so the thirty-second grid
is known, and the render starts at the pattern's start. Each grid position is
looked at whether or not anything happens there.

**The cutoff comes from a spectrum fit** (`pcf_cutoff`), which is linear over
the whole range rather than over the bottom third of it.

With Freq 0 and Amt 127 the cutoff parameter IS the velocity, so no inversion is
needed at all. With the second pass (Freq 40, Amt 63) it is
`freq + amt * velocity`, which this undoes.

    python pcf_velocity.py <render-dir> --pattern 12
    python pcf_velocity.py <render-dir> --prefix p2_ --freq 40 --amt 63
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_cutoff import trajectory

# A trigger's peak is the highest resolved parameter in a window that starts at
# the grid position and runs a fraction of a thirty-second. It cannot run the
# whole step or a dense pattern's neighbours bleed in.
WINDOW = 0.45


def read_pattern(ref, filt, bpm, length32, freq=0.0, amt=1.0, n=512, hop=128):
    """(step, velocity, resolved) for every thirty-second in one period."""
    t, p, _ = trajectory(ref, filt, n, hop)
    step32 = 60.0 / bpm / 8.0
    out = []
    for s in range(length32):
        # Average what the same grid position gives across every repeat in the
        # render, so one noisy frame cannot become a velocity.
        peaks = []
        k = 0
        while True:
            t0 = (s + k * length32) * step32
            if t0 + WINDOW * step32 > t[-1]:
                break
            m = (t >= t0) & (t < t0 + WINDOW * step32)
            v = p[m]
            v = v[np.isfinite(v)]
            if len(v):
                peaks.append(v.max())
            k += 1
        if not peaks:
            out.append((s, np.nan, 0))
        else:
            out.append((s, (np.median(peaks) - freq) / amt, len(peaks)))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("--prefix", default="p_")
    ap.add_argument("--ref", help="reference wav name; default <prefix>ref.wav")
    ap.add_argument("--pattern", type=int)
    ap.add_argument("--bpm", type=float, default=90.0)
    ap.add_argument("--length", type=int, default=64,
                    help="thirty-seconds to report, not the pattern's own length")
    ap.add_argument("--freq", type=float, default=0.0, help="Freq byte")
    ap.add_argument("--amt", type=float, default=127.0, help="Amt byte")
    a = ap.parse_args(argv)

    ref = os.path.join(a.render_dir, a.ref or (a.prefix + "ref.wav"))
    if not os.path.exists(ref):
        print("no reference at %s" % ref)
        return 1
    pats = ([a.pattern] if a.pattern is not None
            else sorted(int(f[len(a.prefix) + 3:-4])
                        for f in os.listdir(a.render_dir)
                        if f.startswith(a.prefix + "pat") and f.endswith(".wav")))
    for n in pats:
        f = os.path.join(a.render_dir, "%spat%02d.wav" % (a.prefix, n))
        if not os.path.exists(f):
            continue
        rows = read_pattern(ref, f, a.bpm, a.length,
                            a.freq / 127.0, a.amt / 127.0)
        hits = [(s, v, k) for s, v, k in rows if np.isfinite(v)]
        print("\npattern %d: %d of %d grid positions resolved"
              % (n, len(hits), len(rows)))
        for i in range(0, len(hits), 8):
            print("   " + "  ".join("%2d:%5.2f" % (s, v)
                                    for s, v, _ in hits[i:i + 8]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
