#!/usr/bin/env python3
"""Is the high-frequency slope reading real, or is it each file's noise floor?

harm_response.py fits the slope over the octaves above the peak, which at
cutoff 127 lands around 7 to 16 kHz. A spectrum that has reached its dither
floor flattens, and a flattened tail reads as a SHALLOW slope. So "ReBirth is
shallower than us up there" has an innocent explanation that must be excluded
before it is treated as a topology difference.

The discriminant needs no reference. A harmonic that is really there stands
above the spectrum BETWEEN harmonics; one that has sunk into the floor does not.
So for each harmonic, compare its level against the inter-harmonic level either
side. Call it in-floor when that margin drops below 6 dB.

Then report, per side, the frequency at which the harmonics stop being real, and
whether the slope-fit window sits above or below it.
"""
import os
import sys

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "tools", "analyze"))
from harm_response import psd, find_f0, WS      # noqa: E402

MARGIN_DB = 6.0


def floor_freq(path):
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f)
    binw = f[1] - f[0]
    last_real, first_dead = None, None
    for k in range(1, 140):
        fc = k * f0
        if fc > 18000.0:
            break
        i = int(round(fc / binw))
        if i + 20 >= len(P):
            break
        h = np.sqrt(P[max(0, i - 3):i + 4].max())
        # inter-harmonic: halfway to the neighbours on both sides
        lo = int(round((fc - 0.5 * f0) / binw))
        hi = int(round((fc + 0.5 * f0) / binw))
        between = []
        for j in (lo, hi):
            if 0 < j < len(P) - 3:
                between.append(np.sqrt(P[j - 2:j + 3].mean()))
        if not between:
            continue
        b = float(np.mean(between))
        marg = 20.0 * np.log10(h / (b + 1e-30) + 1e-30)
        if marg >= MARGIN_DB:
            last_real = fc
        elif first_dead is None and last_real is not None:
            first_dead = fc
    return last_real, first_dead


if __name__ == "__main__":
    cells = [("cut127_reso127", "h303_cut127_reso127.wav"),
             ("cut127_reso060", "h303_cut127_reso060.wav"),
             ("cut090_reso127", "h303_cut090_reso127.wav")]
    dirs = [("ReBirth", "batchH_rb"), ("branch", "probe_baseline_verify"),
            ("refit", "probe_fitted"), ("nodrive", "probe_nodrive")]
    print(f"Harmonics stop standing {MARGIN_DB:.0f} dB above the inter-harmonic")
    print("floor at this frequency. The slope window used by harm_response is")
    print("roughly 2x to 8x the peak, so for cut127 that is about 7 to 16 kHz.\n")
    print(f"{'cell':<16s} {'source':<10s} {'last real harmonic':>20s}")
    for label, fn in cells:
        for name, d in dirs:
            p = os.path.join(WS, "renders", d, fn)
            if not os.path.exists(p):
                continue
            r = floor_freq(p)
            if r is None:
                continue
            lr, _ = r
            print(f"{label:<16s} {name:<10s} {(lr or 0):>17.0f} Hz")
        print()
