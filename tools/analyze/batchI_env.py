#!/usr/bin/env python3
"""Batch I: the 303's two envelopes, measured as laws rather than as three points.

I1  Is the VCA gate a fraction of the STEP or a fixed time? Fits gate and
    release at five tempos. Proportional predicts a constant gate/step ratio and
    a gate in ms that halves as the tempo doubles; fixed predicts a constant
    gate in ms. The release should be constant in ms either way, because a time
    constant is a time constant.

I2  The env mod depth law, nine settings against three decays. Reported as
    octaves of centroid above the env-mod-0 centroid, which is the quantity our
    EM_OCT constant actually sets, rather than as raw Hz.

I3  What the decay knob reaches when env mod is 0. If it is the amp envelope the
    gate or release will move with it; if it is something else, neither will.

Usage:  python batchI_env.py <rebirth-dir> [ours-dir]
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

TEMPOS = (60, 90, 120, 150, 180)
EMS = (0, 16, 32, 48, 64, 80, 96, 112, 127)
DECS = (0, 16, 32, 48, 64, 80, 96, 112, 127)
SLICE_MS = 1.25


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



def load(p):
    with wavemod.open(p, "rb") as w:
        nf, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(nf)
    return np.frombuffer(raw, "<i2").astype(np.float64).reshape(-1, ch).mean(axis=1) / 32768.0, sr


def envelope(p, tempo, n=8, first=2, nsl=64):
    """nsl points spread evenly across ONE step, so the x axis is the same
    fraction of a step at every tempo and the two hypotheses separate."""
    x, sr = load(p)
    pe = period_env(x, sr)
    step = sr * 60.0 / tempo / 4.0
    acc = np.zeros(nsl)
    for s in range(first, first + n):
        for j in range(nsl):
            i = int(s * step + j * step / nsl)
            if i < len(pe):
                acc[j] += pe[i]
    acc /= n
    step_ms = step / sr * 1000.0
    return np.arange(nsl) * step_ms / nsl, acc / acc.max(), step_ms


def fit_gate(t, e, step_ms):
    y = 20 * np.log10(np.maximum(e, 1e-6))
    # skip the attack (the first slice is mid-ramp) and the far tail
    use = (t >= 0.08 * step_ms) & (y > -32.0)
    if use.sum() < 8:
        return float("nan"), float("nan"), float("nan")
    best = (0.0, 0.0, 1e9)
    for gate in np.arange(0.15 * step_ms, 0.95 * step_ms, 0.25):
        for tau in np.arange(5.0, 60.0, 0.25):
            pred = np.where(t < gate, 0.0, -8.6859 * (t - gate) / tau)
            r = float(np.sqrt(np.mean((y[use] - pred[use]) ** 2)))
            if r < best[2]:
                best = (gate, tau, r)
    return best


def centroid(p, lo=40.0, hi=15000.0, skip=0.2):
    x, sr = load(p)
    x = x[int(skip * sr):]
    N, hop = 8192, 4096
    acc = np.zeros(N // 2 + 1)
    w_ = np.hanning(N)
    c = 0
    for i in range(0, len(x) - N, hop):
        acc += np.abs(np.fft.rfft(x[i:i + N] * w_)) ** 2
        c += 1
    if not c:
        return float("nan")
    f = np.fft.rfftfreq(N, 1.0 / sr)
    m = np.sqrt(acc / c)
    k = (f >= lo) & (f <= hi)
    return float((f[k] * m[k]).sum() / m[k].sum())


def group_tempo(dirs):
    print("=== I1  is the gate a fraction of the STEP, or a fixed time? ===")
    print()
    print("  proportional -> gate/step constant, gate ms halves as tempo doubles")
    print("  fixed        -> gate ms constant, gate/step climbs with tempo")
    print()
    for name, d in dirs:
        rows = []
        for bpm in TEMPOS:
            p = os.path.join(d, f"i303_tempo{bpm:03d}.wav")
            if not os.path.exists(p):
                continue
            t, e, step_ms = envelope(p, bpm)
            g, tau, r = fit_gate(t, e, step_ms)
            rows.append((bpm, step_ms, g, g / step_ms, tau, r))
        if not rows:
            continue
        print(f"  {name}")
        print(f"  {'BPM':>5s} {'step ms':>8s} {'gate ms':>8s} {'gate/step':>10s} "
              f"{'release ms':>11s} {'resid dB':>9s}")
        for bpm, sm, g, fr, tau, r in rows:
            print(f"  {bpm:5d} {sm:8.1f} {g:8.1f} {fr:10.3f} {tau:11.2f} {r:9.2f}")
        fr = np.array([r[3] for r in rows]); gm = np.array([r[2] for r in rows])
        tt = np.array([r[4] for r in rows])
        print(f"        gate/step spread {fr.max()-fr.min():.3f}   "
              f"gate ms spread {gm.max()-gm.min():.1f}   "
              f"release spread {tt.max()-tt.min():.2f} ms")
        verdict = "PROPORTIONAL" if (fr.max() - fr.min()) < 0.06 else \
                  ("FIXED" if (gm.max() - gm.min()) < 8.0 else "neither, look at the table")
        print(f"        -> {verdict}")
        print()


def group_envmod(dirs):
    print("=== I2  env mod depth law ===")
    print()
    print("  octaves of centroid above the same fixture at env mod 0")
    print()
    for name, d in dirs:
        base = {}
        for dec in (0, 64, 127):
            p = os.path.join(d, f"i303_em000_dec{dec:03d}.wav")
            if os.path.exists(p):
                base[dec] = centroid(p)
        if not base:
            continue
        print(f"  {name}")
        print(f"  {'envmod':>7s}" + "".join(f"{'dec'+str(k):>12s}" for k in sorted(base)))
        for em in EMS:
            row = f"  {em:7d}"
            for dec in sorted(base):
                p = os.path.join(d, f"i303_em{em:03d}_dec{dec:03d}.wav")
                row += f"{np.log2(centroid(p)/base[dec]):12.3f}" if os.path.exists(p) else f"{'-':>12s}"
            print(row)
        print()


def group_decay(dirs):
    print("=== I3  what does DECAY reach at env mod 0? ===")
    print()
    for name, d in dirs:
        rows = []
        for dec in DECS:
            p = os.path.join(d, f"i303_dec{dec:03d}.wav")
            if not os.path.exists(p):
                p = os.path.join(d, f"i303_em000_dec{dec:03d}.wav")
            if not os.path.exists(p):
                continue
            t, e, step_ms = envelope(p, 120.0)
            g, tau, r = fit_gate(t, e, step_ms)
            rows.append((dec, centroid(p), g, tau))
        if not rows:
            continue
        print(f"  {name}")
        print(f"  {'decay':>6s} {'centroid Hz':>12s} {'gate ms':>9s} {'release ms':>11s}")
        for dec, c, g, tau in rows:
            print(f"  {dec:6d} {c:12.1f} {g:9.1f} {tau:11.2f}")
        c = np.array([r[1] for r in rows]); g = np.array([r[2] for r in rows])
        tt = np.array([r[3] for r in rows])
        print(f"         centroid moves {c.max()-c.min():.1f} Hz, "
              f"gate {g.max()-g.min():.1f} ms, release {tt.max()-tt.min():.2f} ms")
        print()


if __name__ == "__main__":
    dirs = [("ReBirth", sys.argv[1])]
    if len(sys.argv) > 2:
        dirs.append(("ours", sys.argv[2]))
    group_tempo(dirs)
    group_envmod(dirs)
    group_decay(dirs)
