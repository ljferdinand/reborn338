#!/usr/bin/env python3
"""Sub-bin pitch of a step-locked drum hit, power-averaged across hits.

score909's dominant-frequency column reads a Welch PSD whose bins are 5.4 Hz
apart, which is fine for "is this the right partial" and useless for fitting a
pitch law: a tom at 91.5 Hz could be anywhere from 88.8 to 94.2. This averages
the per-hit PSDs in the POWER domain (never coherently, see the never-null-test
rule in docs/MEASUREMENT_PROTOCOL.md), then parabolic-interpolates the peak in log magnitude,
which resolves well inside a bin.
"""
import math, struct, sys
import numpy as np

SR, BPM, BARS = 44100, 120.0, 4
STEP = 60.0 / BPM / 4.0
HITS = [0, 4, 8, 12]


def load_left(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i+4:i+8])[0]
    a = np.frombuffer(d[i+8:i+8+n], dtype="<i2").astype(np.float64) / 32768.0
    return a[0::2]


def hit_pitch(x, lo, hi, win=0.30, nfft=1 << 17, skip=0.0):
    """skip: seconds to drop from the front of each window. A drum with a pitch
    envelope reads sharp on a window that starts at the transient, so a settled
    pitch needs the sweep excluded on BOTH sides."""
    """Peak frequency in [lo, hi), from the power-averaged spectrum of the hits."""
    w = int(win * SR)
    off = int(skip * SR)
    acc = None
    k = 0
    for bar in range(BARS):
        for st in HITS:
            t = int((bar * 16 + st) * STEP * SR) + off
            if t + w > len(x):
                continue
            seg = x[t:t+w] * np.hanning(w)
            P = np.abs(np.fft.rfft(seg, nfft)) ** 2
            acc = P if acc is None else acc + P
            k += 1
    if not k:
        return 0.0
    acc /= k
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    band = (f >= lo) & (f < hi)
    idx = np.where(band)[0]
    j = idx[np.argmax(acc[idx])]
    if j <= 0 or j + 1 >= len(acc):
        return float(f[j])
    # parabolic interpolation on log magnitude
    a, b, c = (math.log(max(acc[j-1], 1e-30)), math.log(max(acc[j], 1e-30)),
               math.log(max(acc[j+1], 1e-30)))
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return float(f[j] + d * (f[1] - f[0]))


if __name__ == "__main__":
    lo, hi = (float(sys.argv[1]), float(sys.argv[2]))
    for p in sys.argv[3:]:
        print("  %-60s %8.2f Hz" % (p.split("\\")[-1], hit_pitch(load_left(p), lo, hi)))


def pitch_track(x, f_lo, f_hi, times_ms, hits=None, win_cycles=6.0, win_s=None):
    """Instantaneous frequency of the dominant partial, at each time offset.

    Short-window FFT cannot do this: 40 ms at 90 Hz is under four cycles, and
    the peak picking wanders by hundreds of cents. This band-passes around the
    partial, takes the analytic signal, and differentiates the unwrapped phase,
    which stays stable down to a couple of cycles. Averaged across hits in the
    FREQUENCY domain (a mean of per-hit estimates), not by summing waveforms.
    """
    from scipy.signal import butter, sosfiltfilt, hilbert
    sos = butter(4, [f_lo, f_hi], btype="band", fs=SR, output="sos")
    y = sosfiltfilt(sos, x)
    ana = hilbert(y)
    ph = np.unwrap(np.angle(ana))
    inst = np.diff(ph) * SR / (2 * math.pi)
    env = np.abs(ana)
    out = []
    for ms in times_ms:
        vals = []
        for bar in range(BARS):
            for st in (hits or HITS):
                t = int((bar * 16 + st) * STEP * SR) + int(ms / 1000.0 * SR)
                w = int((win_s if win_s else win_cycles / max(f_lo, 1.0)) * SR)
                if t + w >= len(inst):
                    continue
                seg = inst[t:t+w]
                wt = env[t:t+w]
                m = (seg > f_lo * 0.5) & (seg < f_hi * 1.5) & (wt > wt.max() * 0.05)
                if m.sum() > 8:
                    vals.append(float(np.average(seg[m], weights=wt[m])))
        out.append(float(np.median(vals)) if vals else 0.0)
    return out
