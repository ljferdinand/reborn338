#!/usr/bin/env python3
"""How much TREBLE the filter delivers THROUGH a driven note, ours vs ReBirth.

env_traj.py tracks WHERE the corner goes (the resonant-peak frequency) through a
tied note. This tracks HOW MUCH TREBLE comes through, which is a different
question: our corner can sit at ReBirth's frequency while the filter passes less
of the band above it (a steeper skirt, a weaker peak, less resonance under
drive). Section 18 of BASS303_FINDINGS localised the song "saw too dark" to the
envelope-driven regime; this decomposes it in time.

METHOD, reusing env_traj's validated per-frame harmonic sampling. For a tied
note (gate held open the whole file, so one envelope from onset to floor), each
frame samples |H(k f0)| = sqrt(PSD at k f0) * k for the ideal 1/k saw. Per frame:

  corner   the interpolated resonant-peak frequency (same as env_traj)
  treble   mean |H| over harmonics in [TLO, THI] Hz, in dB RELATIVE to the
           passband (first three harmonics), so a level offset cancels and what
           is left is how much of the treble band the filter passes at that
           instant.

Both sides are the same note and tie, so they align by onset. The readout is the
treble deficit (ours minus ReBirth, dB) as a function of time through the note,
which says WHETHER the deficit is at the attack transient, the sustain, or the
decay, and whether the corner tracks while it happens.

ReBirth renders are not sample-deterministic, but the envelope and the spectral
trajectory are, so onset alignment is valid (the same assumption env_traj makes).

USAGE
  python treble_traj.py                       # all envdomain cells, RB vs ours-tpt dir
  python treble_traj.py --ours <dir> --cell f303_em127_dec127_tie
"""
import argparse
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from env_traj import read_mono, estimate_f0, onset, FRAME, HOP   # noqa: E402

WS = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
RENDERS = os.path.join(WS, "renders")
TLO, THI = 2000.0, 7000.0     # the treble band the songs are dark in
KMAX = 90


def trace(path):
    """Per-frame (t, corner_hz, treble_rel_pb_db). Corner and treble share the
    same |H(k f0)| frames."""
    x, sr = read_mono(path)
    f0 = estimate_f0(x, sr)
    a0 = onset(x, sr)
    win = np.hanning(FRAME)
    binw = sr / FRAME
    kmax = min(KMAX, int((sr * 0.45) / f0) - 1)
    nfr = (len(x) - a0 - FRAME) // HOP + 1
    if nfr < 4:
        return None
    idx = a0 + np.arange(nfr)[:, None] * HOP + np.arange(FRAME)[None, :]
    P = np.abs(np.fft.rfft(x[idx] * win, axis=1)) ** 2
    ks = np.arange(1, kmax + 1)
    h = np.empty((nfr, kmax))
    for i, k in enumerate(ks):
        c = int(round(k * f0 / binw))
        lo, hi = max(0, c - 2), min(P.shape[1], c + 3)
        h[:, i] = P[:, lo:hi].max(axis=1)
    db = 10.0 * np.log10(h + 1e-30) + 20.0 * np.log10(ks)[None, :]   # |H(k f0)| dB
    fr = ks * f0
    # corner = interpolated peak, in log f
    j = np.argmax(db, axis=1)
    lf = np.log2(fr)
    r = np.arange(nfr)
    y0 = db[r, np.maximum(j - 1, 0)]; y1 = db[r, j]; y2 = db[r, np.minimum(j + 1, kmax - 1)]
    d = y0 - 2 * y1 + y2
    frac = np.where(d != 0, 0.5 * (y0 - y2) / np.where(d != 0, d, 1.0), 0.0)
    stepj = 0.5 * (lf[np.minimum(j + 1, kmax - 1)] - lf[np.maximum(j - 1, 0)])
    corner = 2.0 ** (lf[j] + frac * stepj)
    # passband and treble band
    pb = db[:, :3].mean(axis=1)
    tsel = (fr >= TLO) & (fr <= THI)
    treble = db[:, tsel].mean(axis=1) - pb if tsel.any() else np.full(nfr, np.nan)
    t = (r * HOP + FRAME / 2.0) / sr
    return t, corner, treble, f0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ours", default=os.path.join(
        WS, "scratchpad", "envdomain_tpt"))
    ap.add_argument("--rb", default=os.path.join(RENDERS, "envdomain_rb"))
    ap.add_argument("--cell", default=None)
    a = ap.parse_args()

    cells = sorted(os.path.splitext(n)[0] for n in os.listdir(a.rb)
                   if n.endswith(".wav") and "dup" not in n)
    if a.cell:
        cells = [c for c in cells if c == a.cell]

    print(f"treble band {TLO:.0f}-{THI:.0f} Hz, dB relative to passband. "
          f"ours minus ReBirth is the deficit.\n")
    for c in cells:
        pr = os.path.join(a.rb, c + ".wav")
        po = os.path.join(a.ours, c + ".wav")
        if not (os.path.exists(pr) and os.path.exists(po)):
            continue
        tr = trace(pr); to = trace(po)
        if tr is None or to is None:
            print(f"{c}: too short"); continue
        tR, cornR, trebR, f0R = tr
        tO, cornO, trebO, f0O = to
        # common time grid (both onset-zeroed already share t start ~0)
        n = min(len(tR), len(tO))
        tt = tR[:n]
        dCorn = np.log2(cornO[:n] / cornR[:n])         # ours/RB corner, octaves
        dTreb = trebO[:n] - trebR[:n]                   # ours-RB treble, dB
        print(f"=== {c}  (f0 RB {f0R:.1f} / ours {f0O:.1f} Hz) ===")
        print(f"  {'t ms':>6}{'RB corner':>10}{'our corner':>11}{'corner oct':>11}"
              f"{'RB treb':>9}{'our treb':>9}{'deficit':>9}")
        step = max(1, n // 10)
        for i in range(0, n, step):
            print(f"  {tt[i]*1000:6.0f}{cornR[i]:10.0f}{cornO[i]:11.0f}{dCorn[i]:+11.2f}"
                  f"{trebR[i]:9.1f}{trebO[i]:9.1f}{dTreb[i]:+9.1f}")
        # where is the deficit worst?
        k = int(np.argmin(dTreb))
        print(f"  worst treble deficit {dTreb[k]:+.1f} dB at {tt[k]*1000:.0f} ms "
              f"(corner ours {cornO[k]:.0f} vs RB {cornR[k]:.0f}, "
              f"{dCorn[k]:+.2f} oct); mean deficit {np.mean(dTreb):+.1f} dB\n")


if __name__ == "__main__":
    main()
