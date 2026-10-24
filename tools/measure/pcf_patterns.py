#!/usr/bin/env python3
"""Read the PCF's 55 preset patterns off ReBirth, rather than off Appendix D.

Appendix D draws all 55 as bar charts, and transcribing them by eye is possible
but unverifiable. Measuring them is neither harder nor slower: each pattern is
one fixture, the whole set is one export batch, and what comes back is the
thing itself rather than someone's rendering of it.

How it works. Each fixture holds Freq at 0 and Amt part way up, so every trigger
drives the cutoff from the bottom of its range to `amt * velocity`, and Decay is
short enough that each trigger has died away before the next thirty-second note.
Dividing a narrow band's energy by the same band in `pat_ref` gives that band's
attenuation over time, and inverting the measured static filter turns that into
the cutoff parameter. Triggers are the upward jumps; the height of each one is
its velocity.

RETIRED 2026-08-29, and left in place because its failure is instructive.
Two things this docstring said are wrong. The fixtures do NOT use the 303 drone,
they use the broadband probe (see gen_pcf_fixtures.py, pattern_cases). And the
velocities this produces are compressed, because AMT_PARAM 64/127 with a
88..354 Hz BAND puts a velocity-1.0 trigger's corner at 372 Hz, the top of the
band: sensitivity falls from 64.6 dB per unit velocity at v = 0.1 to 12.6 at
v = 0.9, and v = 1.0 is thrown away by atten_to_param's own rejection threshold.
The trigger POSITIONS it reports are sound and agree with Appendix D exactly on
every pattern without dynamics. Its velocities are not, and its phase is not:
`origin` is the first trigger in the window, so every pattern comes out rotated
to start on step 0. See PCF_FINDINGS section 10, and pcf_appendixd.py.

    python pcf_patterns.py <render-dir>
    python pcf_patterns.py <render-dir> --pattern 12 --detail
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_response import load

FC0, OCT, Q, GAIN = 9.01, 10.653, 0.693, 4.33
BPM = 140.0
N, HOP = 256, 32
BAND = (88.0, 354.0)
AMT_PARAM = 64.0 / 127.0        # the Amt the fixtures were generated with


def band_energy(x, sr, f0, f1, n=N, hop=HOP):
    w = np.hanning(n)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    b = (f >= f0) & (f <= f1)
    return np.array([(np.abs(np.fft.rfft(x[s:s + n] * w)) ** 2)[b].sum()
                     for s in range(0, len(x) - n, hop)])


def atten_to_param(att_db, fb):
    """Invert the measured static lowpass: band attenuation -> cutoff param."""
    g = np.linspace(0.001, 1.05, 8000)
    fc = FC0 * 2.0 ** (OCT * g)
    r = fb / fc
    mag = 20 * np.log10(1.0 / np.sqrt((1 - r * r) ** 2 + (r / Q) ** 2)) + GAIN
    i = np.argsort(mag)
    out = np.interp(att_db, mag[i], g[i], left=np.nan, right=np.nan)
    out[(att_db < mag.min() + 0.3) | (att_db > GAIN - 0.4)] = np.nan
    return out


def triggers(param, dt, prominence=0.045, min_gap_s=0.020):
    """Upward jumps in the cutoff parameter, with their peak height."""
    p = np.where(np.isfinite(param), param, np.nanmin(param))
    out = []
    for i in range(1, len(p) - 1):
        if p[i] >= p[i - 1] and p[i] > p[i + 1]:
            j = i
            while j > 0 and p[j - 1] < p[j]:
                j -= 1
            if p[i] - p[j] < prominence:
                continue
            t = i * dt
            if out and t - out[-1][0] < min_gap_s:
                if p[i] > out[-1][1]:
                    out[-1] = (t, p[i])
                continue
            out.append((t, p[i]))
    return out


def analyse(render_dir, index, ref_energy, sr, detail=False):
    path = os.path.join(render_dir, f"pat_{index:02d}.wav")
    if not os.path.exists(path):
        return None
    x, _ = load(path)
    a = band_energy(x[:, 0], sr, *BAND)
    m = min(len(a), len(ref_energy))
    att = 10 * np.log10((a[:m] + 1e-20) / (ref_energy[:m] + 1e-20))
    param = atten_to_param(att, float(np.sqrt(BAND[0] * BAND[1])))
    dt = HOP / sr

    step32 = 60.0 / BPM / 8.0            # one thirty-second note, seconds
    trig = triggers(param, dt)
    # Two bars from the start of bar 2, so the render's own opening is excluded.
    bar = 60.0 / BPM * 4.0
    t0, t1 = bar, bar + 2 * bar
    sel = [(t, v) for t, v in trig if t0 - 0.01 <= t < t1]
    if not sel:
        return index, 0, [], None

    origin = min(t for t, _ in sel)
    grid = []
    for t, v in sel:
        step = (t - origin) / step32
        grid.append((int(round(step)), v))

    peak = max(v for _, v in sel)
    rows = [(s, v / AMT_PARAM) for s, v in grid]

    # Pattern length: the smallest shift at which the trigger set repeats.
    steps = sorted({s for s, _ in rows})
    length = None
    for L in (4, 6, 8, 12, 16, 20, 24, 28, 32, 48, 64):
        got = {s % L for s in steps}
        if len({s for s in steps if s < L}) == len(got):
            if all(((s + L) in steps) or (s + L) > max(steps) for s in steps if s < L):
                length = L
                break
    if detail:
        print(f"\npattern {index}: {len(rows)} triggers over two bars, "
              f"peak param {peak:.3f}")
        for s, v in rows:
            print(f"    32nd step {s:3d}   velocity {v:.3f}")
    return index, len(rows), rows, length


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("--pattern", type=int)
    ap.add_argument("--detail", action="store_true")
    a = ap.parse_args(argv)

    rx, sr = load(os.path.join(a.render_dir, "pat_ref.wav"))
    ref = band_energy(rx[:, 0], sr, *BAND)

    which = [a.pattern] if a.pattern is not None else range(55)
    print(f"{'pat':>4} {'trigs/2bars':>12} {'len(32nds)':>11}  first steps "
          f"(32nd : velocity)")
    for i in which:
        r = analyse(a.render_dir, i, ref, sr, detail=a.detail)
        if r is None:
            continue
        idx, n, rows, length = r
        head = "  ".join(f"{s}:{v:.2f}" for s, v in rows[:8])
        print(f"{idx:4d} {n:12d} {str(length):>11}  {head}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
