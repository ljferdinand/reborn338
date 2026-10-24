#!/usr/bin/env python3
"""Calibrate the skirt estimator against a filter whose slope is KNOWN exactly.

Why. skirt_bands.py reports ReBirth at about -29 dB/oct and us at about -32 in
a band 2x to 4x the corner, where an ideal 4-pole reads -21.3 and no filter of
that order can read steeper than -24 anywhere. Two readings that are both
impossible for the assumed structure is the signature of a biased instrument,
and this project has been caught by that four times. So before treating "both
filters are steeper than four poles" as physics, feed the estimator a signal
built by an EXACTLY known filter and see what it says.

The synthetic signal reproduces the batch S fixture as closely as it can:
a 220 Hz sawtooth (note A, octave up, waveform 0, env mod 0, decay 64) played as
sixteen steps at 120 BPM, gated for half a step with a 22.2 ms release, written
as 16-bit dual-mono at 44.1 kHz. The filter is applied EXACTLY, per harmonic, in
the frequency domain, so its slope is not an approximation of anything.

Each stage is measured separately so that whatever bias exists is attributed to
the right cause rather than to the filter:

    steady      an ungated tone. Estimator against pure algebra
    gated       the fixture's actual VCA. Does plucking tilt the spectrum
    quantised   16-bit, which is what both sides are actually stored as

If all three return -21.3 for a 4-pole, the estimator is honest and the -29 is
real. If the gated or quantised rows come back steeper, the excess is the
measurement and not the filter, and it is shared by both sides by construction.

Usage:  python skirt_calib.py
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from skirt_bands import curve, band_slope, ideal4, BANDS   # noqa: E402

SR = 44100
F0 = 220.0
BPM = 120.0
STEPS = 16
GATE_FRAC = 0.5          # ReBirth holds the VCA open for half a step
REL_TAU = 0.0222         # measured release, seconds
ATK = 0.0012             # the 1.2 ms re-pluck attack
BARS = 8                 # roughly the length of a batch S export
OUT = os.path.join(os.environ.get("TEMP", "."), "skirt_calib")


def ladder(f, fc, k=0.0):
    """G^4 / (1 + k G^4), the structure our engine implements."""
    G = 1.0 / (1.0 + 1j * (f / fc))
    G4 = G ** 4
    return G4 / (1.0 + k * G4)


def make_saw(fc, k, n):
    """Additive sawtooth with the filter applied exactly, per harmonic."""
    t = np.arange(n) / SR
    x = np.zeros(n)
    kmax = int(0.95 * (SR / 2) / F0)
    for h in range(1, kmax + 1):
        f = h * F0
        H = ladder(f, fc, k)
        # saw: sum (-1)^(h+1) * (2/(pi h)) sin(2 pi f t), phase rotated by the filter
        a = (2.0 / (np.pi * h)) * ((-1.0) ** (h + 1))
        x += a * np.abs(H) * np.sin(2 * np.pi * f * t + np.angle(H))
    return x


def envelope(n):
    """The fixture's VCA: attack, flat for half a step, exponential release."""
    step = 60.0 / BPM / 4.0
    e = np.zeros(n)
    sn = int(round(step * SR))
    gn = int(round(step * GATE_FRAC * SR))
    atk = np.exp(-1.0 / (ATK * SR))
    for s in range(int(np.ceil(n / sn))):
        i0 = s * sn
        seg = np.zeros(min(sn, n - i0))
        if len(seg) == 0:
            break
        # attack ramp then hold
        v = 0.0
        for i in range(min(gn, len(seg))):
            v += (1.0 - v) * (1.0 - atk)
            seg[i] = v
        # release
        for i in range(gn, len(seg)):
            v *= np.exp(-1.0 / (REL_TAU * SR))
            seg[i] = v
        e[i0:i0 + len(seg)] = seg
    return e


def write16(path, x):
    x = np.clip(x, -1.0, 1.0)
    q = np.round(x * 32767.0).astype("<i2")
    st = np.repeat(q[:, None], 2, axis=1).reshape(-1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(st.tobytes())


def truth(fc, k):
    """The exact answer: the same estimator run on the analytic response,
    sampled at the same harmonics. Accounts for the feedback term and for the
    fact that a discrete comb is being fitted, neither of which ideal4() does."""
    ks = np.arange(1, int(16000.0 / F0) + 1)
    fr = ks * F0
    db = 20 * np.log10(np.abs(ladder(fr, fc, k)))
    return fr, db - db[:3].mean()


def report(tag, path, fc, ref):
    c = curve(path)
    if c is None:
        print(f"  {tag:>12s}  unmeasurable")
        return
    fr, db = c
    row = f"  {tag:>12s}"
    for a, b in BANDS[:3]:
        s, n, _ = band_slope(fr, db, a * fc, b * fc)
        t, _, _ = band_slope(ref[0], ref[1], a * fc, b * fc)
        row += f" | {a:g}-{b:g}x {s:7.1f} vs {t:6.1f} = {s-t:+5.1f}"
    print(row)


def main():
    os.makedirs(OUT, exist_ok=True)
    n = int(BARS * STEPS * (60.0 / BPM / 4.0) * SR)
    env = envelope(n)
    print(f"{BARS} bars, {n/SR:.1f} s, f0 {F0:g} Hz, 16-bit dual-mono {SR} Hz\n")
    print("Ideal 4-pole read by this estimator over each band:")
    print("   " + "  ".join(f"{a:g}-{b:g}x {ideal4(a, b):+.1f}" for a, b in BANDS[:3]))
    print()

    for fc, k, name in ((1136.0, 0.0, "cut64 k=0"),
                        (1136.0, 3.4, "cut64 k=3.4"),
                        (2294.0, 3.4, "cut110 k=3.4")):
        print(f"{name}, corner {fc:g} Hz, feedback {k:g}")
        x = make_saw(fc, k, n)
        x = x / np.max(np.abs(x)) * 0.5
        tag = name.replace(" ", "").replace("=", "").replace(".", "")
        p1 = os.path.join(OUT, f"{tag}_steady.wav")
        p2 = os.path.join(OUT, f"{tag}_gated.wav")
        p3 = os.path.join(OUT, f"{tag}_quiet.wav")
        write16(p1, x)
        write16(p2, x * env)
        write16(p3, x * env * 0.1)      # 20 dB quieter: is the floor reachable
        ref = truth(fc, k)
        print("               |      measured vs exact = error, dB/oct")
        report("steady", p1, fc, ref)
        report("gated", p2, fc, ref)
        report("gated -20dB", p3, fc, ref)
        print()


if __name__ == "__main__":
    main()
