#!/usr/bin/env python3
"""Per-harmonic-band filter residual on batchH, ours minus ReBirth, dB.

Reproduces the audit's k1-5 / k5-10 / k10-20 / k20-40 / k40-90 residual table
(FINDINGS_AUDIT 2026-07-31) on whatever build is rendered into `ours`, using the
CORRECTED instrument (harm_response.refine_f0), so a stale find_f0 cannot inflate
the HF bands. Each cell's curve is normalised to its own passband (first three
harmonics) before differencing, per docs/MEASUREMENT_PROTOCOL.md, so a level offset does
not enter. Pooled over all batchH cells that clear the floor.

The point: it separates a systematic HF deficit (a mean offset that grows with k)
from scatter (rms with no mean). The 2026-07-31 refit drove the k20-40 mean from
-2.41 to +0.05; this checks where the CURRENT build sits.

Usage:  python kband.py <ours-render-dir>   (default renders/batchH_ours)
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics, cells_in   # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
RB = os.path.join(WS, "renders", "batchH_rb")
FLOOR_DB = -55.0
BANDS = [(1, 5), (5, 10), (10, 20), (20, 40), (40, 90)]


def curve(path):
    P, f, sr = psd(path)
    if P is None:
        return None
    f0 = find_f0(P, f)
    ks, m = harmonics(P, f, f0)
    if len(ks) < 12:
        return None
    db = 20 * np.log10(m + 1e-300)
    pb = float(np.mean(db[:3]))
    return ks, db - pb


def main():
    ours = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WS, "renders", "batchH_ours")
    if not os.path.isabs(ours):
        ours = os.path.join(WS, "renders", ours)

    diffs = {b: [] for b in BANDS}
    ncells = 0
    for c, r, p in cells_in(RB):
        op = os.path.join(ours, os.path.basename(p))
        if not os.path.exists(op):
            continue
        ca, cb = curve(p), curve(op)
        if ca is None or cb is None:
            continue
        ka, da = ca
        kb, db_ = cb
        n = min(len(ka), len(kb))
        ka, da, db_ = ka[:n], da[:n], db_[:n]
        sel = da > FLOOR_DB          # ReBirth above its own floor
        ncells += 1
        for (lo, hi) in BANDS:
            m = sel & (ka >= lo) & (ka < hi)
            if m.any():
                diffs[(lo, hi)].extend(list(db_[m] - da[m]))

    print(f"ours = {ours}")
    print(f"k-band residual over {ncells} batchH cells, ours minus ReBirth, dB")
    print(f"{'band':>10s}{'mean':>9s}{'rms':>9s}{'n':>7s}")
    for b in BANDS:
        v = np.array(diffs[b])
        if len(v):
            print(f"  k {b[0]:2d}-{b[1]:<3d}{np.mean(v):>9.2f}{np.sqrt(np.mean(v**2)):>9.2f}{len(v):>7d}")
        else:
            print(f"  k {b[0]:2d}-{b[1]:<3d}{'.':>9s}")


if __name__ == "__main__":
    main()
