#!/usr/bin/env python3
"""Measure the 303 filter as a 2-D map: cutoff x resonance, envelope held out.

Batch G found our filter 1.45x too bright at resonance 20 but the cutoff law was
fitted at resonance 120, so the two laws are entangled and one sweep cannot tell
them apart. Env mod is 0 in these fixtures and the note is held, so the filter is
static: what this measures is the filter itself, not the envelope driving it.

Metrics are the ones that survived the earlier rounds (see tools/measure/
score303.py): peak-picking is unreliable at note A because the 110/220 Hz
harmonics dominate, so use rolloff, centroid and a band-limited prominence.

Usage:  python batchH_filter.py <rebirth-dir> <ours-dir>
"""
import os
import sys
import wave

import numpy as np

CUT = (16, 40, 64, 90, 110, 127)
RESO = (0, 30, 60, 90, 110, 127)
FUND = 110.0          # note A


def psd(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(axis=1)
    # Welch, skipping the first 200 ms so the attack does not colour the average
    x = x[int(0.2 * sr):]
    N, hop = 8192, 4096
    acc = np.zeros(N // 2 + 1)
    w_ = np.hanning(N)
    cnt = 0
    for i in range(0, len(x) - N, hop):
        acc += np.abs(np.fft.rfft(x[i:i + N] * w_)) ** 2
        cnt += 1
    if not cnt:
        return None, None
    return acc / cnt, np.fft.rfftfreq(N, 1.0 / sr)


def rolloff(P, f, frac=0.85):
    """Frequency below which `frac` of the magnitude lies."""
    m = np.sqrt(P)
    c = np.cumsum(m)
    if c[-1] <= 0:
        return float("nan")
    return float(f[np.searchsorted(c, frac * c[-1])])


def centroid(P, f, lo=40.0, hi=15000.0):
    m_ = (f >= lo) & (f <= hi)
    mag = np.sqrt(P[m_])
    return float((f[m_] * mag).sum() / mag.sum()) if mag.sum() > 0 else float("nan")


def prominence(P, f, lo=800.0, hi=6000.0):
    """Strongest peak in the resonance band, dB relative to the fundamental."""
    fi = np.argmin(np.abs(f - FUND))
    fund = P[max(0, fi - 2):fi + 3].max()
    band = (f >= lo) & (f <= hi)
    if not band.any() or fund <= 0:
        return float("nan")
    return float(10 * np.log10(P[band].max() / fund + 1e-30))


def measure(d):
    out = {}
    for c in CUT:
        for r in RESO:
            p = os.path.join(d, f"h303_cut{c:03d}_reso{r:03d}.wav")
            if not os.path.exists(p):
                continue
            P, f = psd(p)
            if P is None:
                continue
            out[(c, r)] = (rolloff(P, f), centroid(P, f), prominence(P, f))
    return out


def grid(title, A, B, idx, fmt="{:7.0f}", ratio=False):
    print(f"--- {title} ---")
    head = "cut\\reso" + "".join(f"{r:>9d}" for r in RESO)
    print(head)
    for c in CUT:
        row = f"{c:8d}"
        for r in RESO:
            if (c, r) not in A or (c, r) not in B:
                row += f"{'-':>9s}"
                continue
            a, b = A[(c, r)][idx], B[(c, r)][idx]
            v = (b / a) if ratio else (b - a)
            row += f"{v:9.3f}" if ratio else f"{v:9.1f}"
        print(row)
    print()


if __name__ == "__main__":
    RB, US = sys.argv[1], sys.argv[2]
    A, B = measure(RB), measure(US)
    common = sorted(set(A) & set(B))
    print(f"{len(common)} of {len(CUT)*len(RESO)} cells measured on both sides\n")

    print("=== ReBirth, absolute ===")
    for name, idx, f_ in (("85% rolloff Hz", 0, "{:9.0f}"), ("centroid Hz", 1, "{:9.0f}"),
                          ("peak prominence dB", 2, "{:9.1f}")):
        print(f"--- {name} ---")
        print("cut\\reso" + "".join(f"{r:>9d}" for r in RESO))
        for c in CUT:
            print(f"{c:8d}" + "".join(
                (f"{A[(c,r)][idx]:9.1f}" if (c, r) in A else f"{'-':>9s}") for r in RESO))
        print()

    print("=== ours / ReBirth ===")
    grid("85% rolloff, ratio", A, B, 0, ratio=True)
    grid("centroid, ratio", A, B, 1, ratio=True)
    grid("peak prominence, dB difference", A, B, 2)

    # Is the error a function of cutoff, of resonance, or of both?
    rat = np.array([[B[(c, r)][0] / A[(c, r)][0] for r in RESO if (c, r) in A and (c, r) in B]
                    for c in CUT], dtype=object)
    flat = np.array([B[k][0] / A[k][0] for k in common])
    print(f"rolloff ratio: mean {flat.mean():.3f}  spread {flat.min():.3f}..{flat.max():.3f}")
    byc = {c: np.mean([B[(c, r)][0] / A[(c, r)][0] for r in RESO if (c, r) in common]) for c in CUT}
    byr = {r: np.mean([B[(c, r)][0] / A[(c, r)][0] for c in CUT if (c, r) in common]) for r in RESO}
    print("  by cutoff:   " + "  ".join(f"{c}:{v:.2f}" for c, v in byc.items()))
    print("  by resonance:" + "  ".join(f"{r}:{v:.2f}" for r, v in byr.items()))
