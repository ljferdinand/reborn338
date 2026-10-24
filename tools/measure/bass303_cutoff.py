#!/usr/bin/env python3
"""Recover the 303 filter's corner frame by frame, by fitting a ladder response.

The counterpart to `pcf_cutoff.py`, and built for the same reason: a summary
statistic is not a corner. The first attempt at batch B2 used the spectral
centroid over time, which needs no filter model and is therefore attractive, and
it does not work here. A saw's residual harmonics dominate the centroid, so the
whole env-mod range moved it only from 284 Hz to 646 Hz while the corner itself
travels several octaves. It measured the source, not the filter.

So this fits. Given a fixture and the reference render of the identical pattern
with the filter wide open, the ratio of their short-time spectra IS the filter's
response at that instant, with the oscillator, the VCA, the note and the output
stage all divided out exactly. What is left is fitted to a four-pole ladder:

    |H| = 1 / |(1 + j f/fc)^4 + k|

which expands to

    |H| = 1 / sqrt((1 - 6r^2 + r^4 + k)^2 + (4r - 4r^3)^2),   r = f/fc

with `k` the resonance feedback. Both fc and k are fitted per frame on a grid,
and the overall gain is solved in closed form as the mean offset, so a change in
level cannot masquerade as a change in corner. That matters here: a ladder's
passband gain is 1/(1+k), so feedback and level are entangled unless the fit
separates them.

A frame whose fit is poor, or whose corner lands at an end of the search grid,
returns NaN rather than a number. Reporting the edge of a grid as a measurement
is how the PCF's preset velocities came out compressed.

    python bass303_cutoff.py <ref.wav> <fixture.wav>
    python bass303_cutoff.py <ref.wav> <fixture.wav> --dump
"""
import argparse
import sys
import wave

import numpy as np

N, HOP = 1024, 128
BAND_LO, BAND_HI, PER_OCT = 60.0, 16000.0, 3
FC = np.exp2(np.linspace(np.log2(50.0), np.log2(18000.0), 240))
K = np.linspace(0.0, 4.5, 46)
MIN_BANDS = 8
MAX_RESID = 4.5          # dB rms
FLOOR_DB = -40.0         # a band this far under its own peak says nothing


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def centres():
    n = int(np.log2(BAND_HI / BAND_LO) * PER_OCT)
    return BAND_LO * 2.0 ** (np.arange(n + 1) / float(PER_OCT))


def band_track(x, sr):
    cs = centres()
    half = 2.0 ** (0.5 / PER_OCT)
    win = np.hanning(N)
    f = np.fft.rfftfreq(N, 1.0 / sr)
    masks = [(f >= c / half) & (f < c * half) for c in cs]
    nf = max(0, (len(x) - N) // HOP)
    out = np.zeros((nf, len(cs)))
    for i in range(nf):
        seg = x[i * HOP:i * HOP + N, :]
        p = np.zeros(len(f))
        for c in range(seg.shape[1]):
            p += np.abs(np.fft.rfft(seg[:, c] * win)) ** 2
        for b, m in enumerate(masks):
            out[i, b] = p[m].sum() if m.any() else 0.0
    return out, cs


def model_table(cs):
    """dB response for every (fc, k), one row per pair, one column per band."""
    r = cs[None, :] / FC[:, None]                       # (nfc, nband)
    re = 1.0 - 6.0 * r ** 2 + r ** 4
    im = 4.0 * r - 4.0 * r ** 3
    out = np.empty((len(FC) * len(K), len(cs)))
    for j, k in enumerate(K):
        mag = 1.0 / np.sqrt((re + k) ** 2 + im ** 2)
        out[j * len(FC):(j + 1) * len(FC)] = 20 * np.log10(mag + 1e-30)
    return out


def trajectory(ref_path, fix_path):
    """(times, fc Hz, k, residual dB). NaN where the frame cannot say."""
    r, sr = load(ref_path)
    w, _ = load(fix_path)
    m = min(len(r), len(w))
    br, cs = band_track(r[:m], sr)
    bw, _ = band_track(w[:m], sr)
    nf = min(len(br), len(bw))
    br, bw = br[:nf], bw[:nf]

    ref_pk = br.max(axis=0, keepdims=True) + 1e-30
    live = 10 * np.log10(br / ref_pk + 1e-30) > FLOOR_DB
    ratio = 10 * np.log10((bw + 1e-30) / (br + 1e-30))
    table = model_table(cs)

    t = (np.arange(nf) * HOP + N * 0.5) / float(sr)
    fc = np.full(nf, np.nan)
    kk = np.full(nf, np.nan)
    rs = np.full(nf, np.nan)
    for i in range(nf):
        sel = live[i]
        if sel.sum() < MIN_BANDS:
            continue
        d = table[:, sel] - ratio[i, sel][None, :]
        d -= d.mean(axis=1, keepdims=True)      # gain solved in closed form
        rms = np.sqrt((d * d).mean(axis=1))
        j = int(np.argmin(rms))
        fi, ki = j % len(FC), j // len(FC)
        if fi == 0 or fi == len(FC) - 1 or rms[j] > MAX_RESID:
            continue
        fc[i], kk[i], rs[i] = FC[fi], K[ki], rms[j]
    return t, fc, kk, rs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref")
    ap.add_argument("fixture")
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args(argv)
    t, fc, k, rs = trajectory(a.ref, a.fixture)
    ok = np.isfinite(fc)
    print("%d frames, %d resolved (%.0f%%), residual %.2f dB rms"
          % (len(fc), ok.sum(), 100.0 * ok.mean(), np.nanmean(rs)))
    if ok.any():
        print("corner: min %.0f  median %.0f  max %.0f Hz   k: median %.2f"
              % (np.nanmin(fc), np.nanmedian(fc), np.nanmax(fc), np.nanmedian(k)))
    if a.dump:
        for i in range(len(fc)):
            if np.isfinite(fc[i]):
                print("%8.4f %8.0f %6.2f %6.2f" % (t[i], fc[i], k[i], rs[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
