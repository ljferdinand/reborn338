#!/usr/bin/env python3
"""The 303's amplitude envelope, measured as a SHAPE rather than as a statistic.

The shipped plucky VCA (decay 60 ms toward a 0.08 floor while the gate is held)
was fitted on two summary statistics of a whole musical patch, envelope
coefficient-of-variation and silence fraction. Two numbers cannot distinguish a
decay from a hold-then-release, and they did not: sliced per 5 ms, ReBirth holds
essentially flat for the length of the gate and then releases off a cliff.

Measured on the batch G WAVE fixtures, at resonance 20, because a resonant
filter goes on ringing after the gate closes and that ring lands in exactly the
window the release has to be fitted from.

Fits  env(t) = 1 for t < gate, exp(-(t-gate)/tau) after  to each side, and
reports the residual so a bad fit cannot pass as a good one.

Usage:  python vca_ab.py <rebirth-dir> <ours-dir>
"""
import os
import sys
import wave as wavemod

import numpy as np

# Mono-summing is SAFE here and was checked, not assumed. The workspace rule is
# to measure stereo, because a pan law seven times too wide once hid behind
# mono-summed metrics. These fixtures are a single centre-panned 303 with the
# effect rack off: L minus R is exactly 0 LSB on both sides, ReBirth's and ours.
# Re-check that if this is ever pointed at a fixture with two voices or any pan.

TEMPO = 120.0
SLICE_MS = 2.5
FIXTURES = [f"g303_wave{w}_cut{c:03d}" for w in (0, 1) for c in (32, 64, 96)]


# Envelope estimator: rectify, then average over EXACTLY one oscillator period.
#
# The obvious estimator, peak per short slice, has a trap that cost a round here.
# At 150 BPM a 16th step is exactly 11 cycles of 110 Hz, so every note samples the
# waveform at the same phases and averaging across notes does NOT smooth the
# ripple; the envelope came out ragged and two of five tempo fits collapsed. An
# analytic (Hilbert) envelope has the same problem, because its residual ripple
# also sits at multiples of f0. A boxcar of exactly one period integrates any
# ripple at a multiple of f0 to its mean, so it is immune by construction. It
# costs one period (9.1 ms at note A) of time smearing, symmetric, so it blurs a
# knee without moving it.
def period_env(x, sr, f0=110.0):
    w = max(1, int(round(sr / f0)))
    return np.convolve(np.abs(x), np.ones(w) / w, mode="same")



def env(path, n_steps=8, first=1):
    with wavemod.open(path, "rb") as w:
        nf, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(nf)
    x = np.frombuffer(raw, "<i2").astype(np.float64).reshape(-1, ch).mean(axis=1) / 32768.0
    pe = period_env(x, sr)
    step = int(round(sr * 60.0 / TEMPO / 4.0))
    sw = int(SLICE_MS * 0.001 * sr)
    ns = step // sw
    acc = np.zeros(ns)
    for s in range(first, first + n_steps):
        b = s * step
        for j in range(ns):
            i = b + j * sw + sw // 2
            if i < len(pe):
                acc[j] += pe[i]
    acc /= n_steps
    t = (np.arange(ns) + 0.5) * SLICE_MS
    return t, acc / acc.max()


def fit(t, e, floor_db=-30.0):
    """Least squares in dB for (gate ms, release tau ms), ignoring the attack."""
    y = 20 * np.log10(np.maximum(e, 1e-6))
    use = (t >= 10.0) & (y > floor_db)
    best = (None, None, 1e9)
    for gate in np.arange(20.0, 110.0, 0.5):
        for tau in np.arange(2.0, 60.0, 0.25):
            pred = np.where(t < gate, 0.0, -8.6859 * (t - gate) / tau)
            r = float(np.sqrt(np.mean((y[use] - pred[use]) ** 2)))
            if r < best[2]:
                best = (gate, tau, r)
    return best


def main(RB, US):
    print(f"envelope fit: flat while gated, then exp release. slice {SLICE_MS} ms,")
    print("resonance 20 so the filter ring does not sit on top of the release.")
    print()
    print(f"{'fixture':>20s} {'side':>6s} {'gate ms':>9s} {'tau ms':>8s} "
          f"{'resid dB':>9s} {'-3dB at':>9s} {'-20dB at':>9s}")
    agg = {}
    for name in FIXTURES:
        for tag, d in (("RB", RB), ("ours", US)):
            p = os.path.join(d, name + ".wav")
            if not os.path.exists(p):
                continue
            t, e = env(p)
            g, tau, r = fit(t, e)
            y = 20 * np.log10(np.maximum(e, 1e-6))
            def cross(db):
                i = np.argmax(y < db)
                return t[i] if (y < db).any() else float("nan")
            agg.setdefault(tag, []).append((g, tau))
            print(f"{name:>20s} {tag:>6s} {g:9.1f} {tau:8.2f} {r:9.2f} "
                  f"{cross(-3.0):9.1f} {cross(-20.0):9.1f}")
    print()
    for tag, v in agg.items():
        g = np.array([a for a, _ in v]); tt = np.array([b for _, b in v])
        print(f"  {tag:>4s}: gate {g.mean():6.1f} ms (spread {g.max()-g.min():.1f})   "
              f"tau {tt.mean():5.2f} ms (spread {tt.max()-tt.min():.2f})")
    print()
    print("  a step at 120 BPM is 125.0 ms; gate as a fraction of the step:")
    for tag, v in agg.items():
        g = np.mean([a for a, _ in v])
        print(f"  {tag:>4s}: {g/125.0:.3f}")
    print()
    print("per-slice envelope, dB below the note max (first fixture only)")
    cols = list(range(0, 50, 4))
    print("        " + "".join(f"{c*SLICE_MS:6.0f}" for c in cols) + "   ms")
    for tag, d in (("RB  ", RB), ("ours", US)):
        p = os.path.join(d, FIXTURES[0] + ".wav")
        if not os.path.exists(p):
            continue
        t, e = env(p)
        y = 20 * np.log10(np.maximum(e, 1e-6))
        print(f"{tag}    " + "".join(f"{y[c]:6.1f}" for c in cols if c < len(y)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
