#!/usr/bin/env python3
"""Characterize the "round chirp": the resonant peak riding a note's filter
sweep (BASS303_FINDINGS 28). Batch B10.

Per note (quarter notes at steps 0,4,8,12, over 4 bars), take an STFT across the
pluck and track the resonant peak in [180, 9000] Hz per frame:
  prominence  peak magnitude above the local spectral floor (dB), level-invariant
  center      peak frequency (Hz), its trajectory is the chirp sweep
  Q           center / -6 dB bandwidth, the "roundness" (low Q = broad/round,
              high Q = narrow/whistly)
At the frame of MAX prominence (the top of the chirp) it records center,
prominence, bandwidth and Q, averaged over the notes; and a prominence trajectory
at fixed times after onset. ReBirth vs ours, one method both sides.

    python tools/analyze/chirp_peak.py \
        --rb ../renders/b10-303-chirp-2026-09-04 \
        --ours ../renders/b10-303-chirp-2026-09-04/ours
"""
import argparse
import os
import numpy as np
from scipy.io import wavfile
from scipy.signal import stft

SR = 44100
BAR_S = 4 * 60.0 / 130.0
STEP_S = BAR_S / 16.0
NOTE_STEPS = (0, 4, 8, 12)
BARS = 4
WIN = 0.34                    # analysis window per note, seconds
TRAJ_MS = (20, 60, 120, 240)  # prominence trajectory sample points
FIXTURES = ["b10_r050_saw", "b10_r075_saw", "b10_r100_saw",
            "b10_r127_saw", "b10_r100_saw_slide", "b10_r100_square"]


def load_mono(path):
    _, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    return x / 32768.0


def onset_abs(x, thr=0.004):
    return int(np.argmax(np.abs(x) > thr))


def peak_frame(mag, f):
    """Given a magnitude spectrum, return (center Hz, prominence dB, bw Hz, Q)
    for the resonant peak in [180, 9000] Hz."""
    band = (f >= 180) & (f <= 9000)
    fb, mb = f[band], mag[band]
    if mb.max() <= 0:
        return np.nan, np.nan, np.nan, np.nan
    i = int(np.argmax(mb))
    pk_db = 20 * np.log10(mb[i] + 1e-12)
    # local floor: median of the band excluding +/- a third octave around peak
    fc = fb[i]
    mask = (fb < fc / 1.26) | (fb > fc * 1.26)
    floor_db = 20 * np.log10(np.median(mb[mask]) + 1e-12) if mask.any() else -120
    prom = pk_db - floor_db
    # -6 dB bandwidth around the peak
    thr = mb[i] * 10 ** (-6 / 20.0)
    lo = i
    while lo > 0 and mb[lo] > thr:
        lo -= 1
    hi = i
    while hi < len(mb) - 1 and mb[hi] > thr:
        hi += 1
    bw = max(fb[hi] - fb[lo], fb[1] - fb[0])
    return fc, prom, bw, fc / bw


def analyze(path):
    x = load_mono(path)
    off = onset_abs(x)
    nwin = int(WIN * SR)
    per_note_top = []      # (center, prom, bw, Q) at max-prominence frame
    traj = {t: [] for t in TRAJ_MS}
    for bar in range(BARS):
        for st in NOTE_STEPS:
            t0 = bar * BAR_S + st * STEP_S
            a = off + int(t0 * SR)
            seg = x[a:a + nwin]
            if len(seg) < nwin // 2:
                continue
            f, t, Z = stft(seg, SR, nperseg=1024, noverlap=768)
            M = np.abs(Z)
            proms = []
            frames = []
            for fi in range(M.shape[1]):
                fc, prom, bw, Q = peak_frame(M[:, fi], f)
                proms.append(prom if not np.isnan(prom) else -1)
                frames.append((fc, prom, bw, Q))
            j = int(np.argmax(proms))
            if frames[j][1] > 0:
                per_note_top.append(frames[j])
            # trajectory: prominence at fixed times after onset
            for ms in TRAJ_MS:
                fi = int(np.searchsorted(t, ms / 1000.0))
                if 0 <= fi < len(frames) and frames[fi][1] == frames[fi][1]:
                    traj[ms].append(frames[fi][1])
    top = np.array(per_note_top)
    return {
        "center": np.nanmean(top[:, 0]), "prom": np.nanmean(top[:, 1]),
        "bw": np.nanmean(top[:, 2]), "Q": np.nanmean(top[:, 3]),
        "traj": {t: (np.nanmean(v) if v else np.nan) for t, v in traj.items()},
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rb", required=True)
    ap.add_argument("--ours", required=True)
    a = ap.parse_args(argv)
    print("Resonant peak at the top of the chirp (max prominence frame), avg over notes")
    print("  fixture               | side |  center |  prom |   bw  |   Q  ")
    for fx in FIXTURES:
        for side, root in (("RB", a.rb), ("OUR", a.ours)):
            p = os.path.join(root, fx + ".wav")
            if not os.path.exists(p):
                continue
            r = analyze(p)
            print("  %-21s | %-4s | %6.0f | %5.1f | %5.0f | %4.1f"
                  % (fx, side, r["center"], r["prom"], r["bw"], r["Q"]))
        print()
    print("Prominence trajectory (dB above floor) at ms after onset, RB vs OUR")
    print("  fixture               | side | " + " | ".join("%4dms" % t for t in TRAJ_MS))
    for fx in FIXTURES:
        for side, root in (("RB", a.rb), ("OUR", a.ours)):
            p = os.path.join(root, fx + ".wav")
            if not os.path.exists(p):
                continue
            r = analyze(p)
            print("  %-21s | %-4s | " % (fx, side)
                  + " | ".join("%5.1f" % r["traj"][t] for t in TRAJ_MS))
        print()


if __name__ == "__main__":
    main()
