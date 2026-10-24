#!/usr/bin/env python3
"""At what RATE does the filter move? The modulation-rate instrument.

WHY THIS EXISTS. Luke, listening to Clodhopper on 2026-08-02: ReBirth's chirp
or wah hits every QUARTER NOTE, and ours sounds like it hits every BAR. That is
a factor of four in the rate of the filter envelope, and no metric in this
project could have seen it. Every existing instrument averages over time:
harm_response, curve_fit, batchH_filter and skirt_* all reduce a whole render to
one spectrum, so a filter that moves at the wrong RATE and the right depth scores
identically to one that is correct. Even onset_ab, which is the rhythm
instrument, asks which steps RETRIGGER, not how often the timbre moves.

WHAT IT MEASURES. A brightness trajectory over time, then the spectrum OF THAT
TRAJECTORY. The peaks in the modulation spectrum are the rates at which the
timbre moves, in Hz, and at a known tempo those map onto note values.

BRIGHTNESS IS A BAND RATIO, NOT A LEVEL. high band over low band in dB, per
frame. This is deliberate and load-bearing: the notes themselves fire at the
16th-note rate, so any measure that responds to LOUDNESS is dominated by the
amplitude envelope and says nothing about the filter. A ratio is immune to
overall level, so what survives is timbre movement.

BOTH SIDES GO THROUGH THE IDENTICAL PROCEDURE, including whatever the quiet
frames do to the ratio. A shared artifact cancels in the comparison; it would
only mislead if it were applied to one side. There is deliberately NO gating of
quiet frames, per the rule that a gate whose pass condition depends on the noise
realisation manufactures findings.

CALIBRATE BEFORE TRUSTING: run with --selftest, which feeds the pipeline a
signal whose modulation rate is known exactly and checks that it comes back.
The rule that cost a round on 2026-07-31 was to build the thing that checks an
estimator in the same sitting as the estimator.

Usage:
    python mod_rate.py --selftest
    python mod_rate.py --bpm 140 a.wav b.wav [more...]
"""
import argparse
import os
import sys
import wave

import numpy as np

LO_BAND = (100.0, 400.0)     # the fundamental region, where a 303 always has energy
HI_BAND = (1000.0, 5000.0)   # where the filter sweep actually shows
FRAME = 1024                 # 23.2 ms at 44.1 kHz
HOP = 128                    # 2.9 ms, so the 16th rate at 9.33 Hz is oversampled 37x


def read(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    return x.mean(axis=1), sr


def brightness(x, sr):
    """dB ratio of a high band to a low band, one value per hop."""
    win = np.hanning(FRAME)
    f = np.fft.rfftfreq(FRAME, 1.0 / sr)
    lo = (f >= LO_BAND[0]) & (f <= LO_BAND[1])
    hi = (f >= HI_BAND[0]) & (f <= HI_BAND[1])
    out = []
    for i in range(0, len(x) - FRAME, HOP):
        P = np.abs(np.fft.rfft(x[i:i + FRAME] * win)) ** 2
        out.append(10.0 * np.log10((P[hi].sum() + 1e-20) / (P[lo].sum() + 1e-20)))
    return np.array(out), sr / HOP


def mod_spectrum(b, fs):
    """Spectrum of the brightness trajectory. Mean removed, Hann windowed."""
    b = b - b.mean()
    n = 1 << int(np.ceil(np.log2(len(b))))
    B = np.abs(np.fft.rfft(b * np.hanning(len(b)), n))
    return B, np.fft.rfftfreq(n, 1.0 / fs)


def at(B, f, target, tol=0.12):
    """Peak magnitude within +/- tol (fractional) of a target rate."""
    sel = (f > target * (1 - tol)) & (f < target * (1 + tol))
    return float(B[sel].max()) if sel.any() else 0.0


def selftest():
    """Feed it a known rate and check it comes back. No trust without this."""
    print("SELF TEST. A 110 Hz saw whose brightness is swept by a filter at a")
    print("KNOWN rate. The instrument must recover that rate, not a harmonic.")
    sr, dur = 44100, 12.0
    t = np.arange(int(sr * dur)) / sr
    ok = True
    for true_rate in (0.583, 2.333, 4.667):
        # sawtooth at 110 Hz through a one-pole whose corner is swept
        ph = np.cumsum(np.full_like(t, 110.0 / sr)) % 1.0
        saw = 2.0 * ph - 1.0
        fc = 400.0 * (2.0 ** (2.0 * np.sin(2 * np.pi * true_rate * t)))
        y = np.zeros_like(saw)
        z = 0.0
        for i in range(len(saw)):
            a = 1.0 - np.exp(-2 * np.pi * fc[i] / sr)
            z += a * (saw[i] - z)
            y[i] = z
        b, fs = brightness(y, sr)
        B, f = mod_spectrum(b, fs)
        peak = f[1:][np.argmax(B[1:])]
        err = abs(peak - true_rate) / true_rate
        good = err < 0.05
        ok &= good
        print(f"  true {true_rate:6.3f} Hz -> recovered {peak:6.3f} Hz  "
              f"({err*100:4.1f}% off)  {'ok' if good else 'FAIL'}")
    print("  " + ("PASS, the instrument is calibrated" if ok else "*** FAILED ***"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--bpm", type=float, default=140.0)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.files:
        ap.error("give some wavs, or --selftest")

    q = a.bpm / 60.0
    rates = [("bar", q / 4), ("half", q / 2), ("QUARTER", q),
             ("8th", q * 2), ("16th", q * 4)]
    print(f"Tempo {a.bpm:.1f} BPM. Modulation-spectrum magnitude at each note")
    print("rate, normalised so the strongest rate in each file reads 1.000.")
    print("This is WHERE THE TIMBRE MOVES, not how loud anything is.\n")
    print(f"  {'file':<34}" + "".join(f"{n:>9}" for n, _ in rates) + "   dominant")
    for p in a.files:
        x, sr = read(p)
        b, fs = brightness(x, sr)
        B, f = mod_spectrum(b, fs)
        vals = [at(B, f, r) for _, r in rates]
        m = max(vals) or 1.0
        dom = rates[int(np.argmax(vals))][0]
        print(f"  {os.path.basename(p)[:34]:<34}"
              + "".join(f"{v/m:>9.3f}" for v in vals)
              + f"   {dom}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
