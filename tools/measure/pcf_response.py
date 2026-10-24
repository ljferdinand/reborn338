#!/usr/bin/env python3
"""Measure the PCF's magnitude response from a fixture render.

The fixtures hold Amt = 0, so the envelope is out of the path and what is left
is a static filter. Each render is divided by `pcf_ref`, the identical song with
the PCF routed nowhere, which cancels the 303 source exactly and leaves the
filter's magnitude response.

Power spectra only. ReBirth's exports are not sample-deterministic, so a null
test or any coherent average across renders is meaningless here.

    python pcf_response.py <render-dir>
    python pcf_response.py <render-dir> --fit          # fit fc and Q per file
"""
import argparse
import glob
import os
import sys
import wave

import numpy as np

NPERSEG = 8192


def load(path):
    w = wave.open(path, "rb")
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    w.close()
    return x.reshape(-1, ch), sr


def psd(x, sr, skip=0.5, nper=NPERSEG):
    """Welch PSD, power-averaged. `skip` drops the first note's attack."""
    x = x[int(skip * sr):]
    h = np.hanning(nper)
    acc = None
    k = 0
    for s in range(0, len(x) - nper, nper // 2):
        X = np.fft.rfft(x[s:s + nper] * h)
        p = X.real ** 2 + X.imag ** 2
        acc = p if acc is None else acc + p
        k += 1
    return acc / max(1, k), np.fft.rfftfreq(nper, 1.0 / sr)


def noise_floor_db(p, freqs, halfwidth=3, lo=18500.0, hi=21500.0):
    """A file's own broadband floor, measured the SAME WAY a harmonic is.

    16-bit dither puts a hard bottom under every export. Once a filtered band
    sinks into it the ratio stops falling and flattens, which reads as a filter
    that stops rolling off: the minimum-cutoff render appears to roll off to
    -66 dB by 580 Hz and then climb back to 0 dB by 14 kHz, which is not a
    filter, it is two dither floors dividing by each other.

    The window matters. A harmonic is the sum of 2*halfwidth+1 bins, and
    summing that many bins of noise sits about 8.5 dB above the per-bin median,
    so comparing a summed harmonic against a per-bin floor silently spends most
    of the margin. Sum the same width here.
    """
    n = 2 * halfwidth + 1
    band = np.where((freqs >= lo) & (freqs <= hi))[0]
    if band.size < n:
        return -300.0
    sums = np.convolve(p[band], np.ones(n), mode="valid")
    return 10 * np.log10(np.median(sums) + 1e-30)


def fundamental(pref, freqs, lo=30.0, hi=200.0):
    """The drone's f0, taken as the strongest reference bin in the bass."""
    band = (freqs >= lo) & (freqs <= hi)
    return float(freqs[band][np.argmax(pref[band])])


def transfer(pfix, pref, freqs, floor_db=-55.0, fmin=40.0, fmax=18000.0,
             margin_db=10.0, halfwidth=3):
    """H(f) in dB where the reference still has real signal to divide by.

    Deliberately simple, after three cleverer gates each failed differently.

    Widening the relative cut from 55 dB to 100 dB kept more bandwidth but
    swept in the gaps BETWEEN harmonics, where the reference is at its own
    dither floor, and the rms error went from 0.3 dB to 30. Measuring the floor
    locally, halfway to each neighbouring harmonic, threw the STRONG harmonics
    away instead: beside a strong partial those midpoints hold window leakage,
    not noise. Subtracting the floor and trimming residuals one-sided ran away,
    because the gain is estimated from the surviving points and the trim then
    ate the passband that set it.

    So this keeps the plain cut, and the honest limitation with it: a 303 drone
    constrains the filter only between its fundamental and wherever its
    harmonics sink out of range, which for the round-1 fixtures is roughly
    65 Hz to 2.5 kHz. Settings whose corner lies outside that window are not
    measured by this source, and the answer is a better source rather than a
    better estimator.
    """
    ref_db = 10 * np.log10(pref + 1e-30)
    fix_db = 10 * np.log10(pfix + 1e-30)
    keep = ((freqs >= fmin) & (freqs <= fmax)
            & (ref_db > ref_db.max() + floor_db)
            & (fix_db > noise_floor_db(pfix, freqs, halfwidth) + margin_db)
            & (ref_db > noise_floor_db(pref, freqs, halfwidth) + margin_db))
    return freqs[keep], fix_db[keep] - ref_db[keep]


def fit_svf(f, h_db, mode):
    """Fit fc, Q and a flat gain by grid search. Returns (mse, fc, q, gain)."""
    best = None
    for fc in np.exp(np.linspace(np.log(8), np.log(60000), 700)):
        for q in np.exp(np.linspace(np.log(0.4), np.log(25), 300)):
            m = svf_db(f, fc, q, mode, 0.0)
            g = float(np.median(h_db - m))
            e = float(np.mean((h_db - (m + g)) ** 2))
            if best is None or e < best[0]:
                best = (e, fc, q, g)
    return best


def svf_db(f, fc, q, mode, gain_db):
    """Magnitude of an analogue 2-pole state-variable filter, in dB.

    The manual calls the PCF a 12 dB/octave 2-pole design switchable between
    lowpass and bandpass, which is exactly this pair.
    """
    r = np.asarray(f, float) / max(fc, 1e-6)
    den = np.sqrt((1.0 - r * r) ** 2 + (r / max(q, 1e-6)) ** 2)
    h = (1.0 / den) if mode == "lp" else ((r / max(q, 1e-6)) / den)
    return 20 * np.log10(h + 1e-30) + gain_db


def fit(f, h_db, mode, gain_db=None):
    """Grid-then-refine fit of fc, Q and (optionally) a flat gain."""
    best = None
    fcs = np.exp(np.linspace(np.log(10.0), np.log(24000.0), 260))
    qs = np.exp(np.linspace(np.log(0.3), np.log(30.0), 120))
    gs = np.linspace(-12, 18, 121) if gain_db is None else np.array([gain_db])
    for fc in fcs:
        for q in qs:
            model = svf_db(f, fc, q, mode, 0.0)
            for g in (gs if gain_db is not None else [np.median(h_db - model)]):
                err = float(np.mean((h_db - (model + g)) ** 2))
                if best is None or err < best[0]:
                    best = (err, fc, q, g)
    return best      # (mse, fc, q, gain_db)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("render_dir")
    ap.add_argument("--fit", action="store_true")
    ap.add_argument("--ref", default="pcf_ref.wav")
    a = ap.parse_args(argv)

    refx, sr = load(os.path.join(a.render_dir, a.ref))
    pref_l, freqs = psd(refx[:, 0], sr)
    pref_r, _ = psd(refx[:, 1], sr)

    files = sorted(glob.glob(os.path.join(a.render_dir, "*.wav")))
    print(f"{'file':16s} {'gain':>7s} {'fc Hz':>9s} {'Q':>7s} {'rms err':>8s}  "
          f"{'L/R skew':>8s}")
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        if os.path.basename(path) == a.ref:
            continue
        x, _ = load(path)
        pl, _ = psd(x[:, 0], sr)
        pr, _ = psd(x[:, 1], sr)
        f, h = transfer(pl, pref_l, freqs)
        _, hr = transfer(pr, pref_r, freqs)
        skew = float(np.mean(h - hr))
        if not a.fit:
            print(f"{name:16s} {np.median(h):7.2f} {'':>9s} {'':>7s} {'':>8s}  {skew:8.2f}")
            continue
        # The filename records which mode byte the fixture carried, but the
        # BEHAVIOUR is what we fit, so try both and keep the better.
        cands = [fit(f, h, m) for m in ("lp", "bp")]
        modes = ("lp", "bp")
        i = int(np.argmin([c[0] for c in cands]))
        mse, fc, q, g = cands[i]
        print(f"{name:16s} {g:7.2f} {fc:9.1f} {q:7.2f} {np.sqrt(mse):8.2f}  "
              f"{skew:8.2f}   best fit as {modes[i].upper()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
