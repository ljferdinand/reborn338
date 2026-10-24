#!/usr/bin/env python3
"""Recover the PCF's cutoff parameter frame by frame, by fitting the spectrum.

The shared primitive for round 7. Given a filtered render and the reference
render of the identical song with the PCF routed nowhere, this returns the
cutoff PARAMETER (0..1, the thing Freq and Amt sum into) against time.

WHY A FIT AND NOT A BAND RATIO. Round 5 took one 88..354 Hz band and inverted
the filter's magnitude at that band's centre. That works only while the corner
sits inside the band's sensitive region: outside it the curve flattens, a small
error in the ratio becomes a large error in the parameter, and past the top the
inversion has no solution at all and the sample is thrown away. Its sensitivity
ran 64.6 dB per unit velocity at the bottom of the range and 12.6 at the top,
which is what compressed every dynamic pattern in the shipped preset table.

Fitting instead uses the whole spectrum at once. A 2-pole lowpass has a shape,
not just a value: the corner is where the shape bends, and the bend is visible
from either side. So one estimator covers the range, and it reports a residual,
which a single-band inversion cannot.

The model is the one measured in PCF_FINDINGS section 3, with resonance at zero
throughout these fixtures so the corner does not ride the Q knob:

    fc(param) = 9.01 * 2^(10.653 * param)      Q = 0.693, gain +4.33 dB

Bands whose reference energy is low carry no information and are weighted out.
A frame whose best fit is poor, or whose corner is pinned at an end of the
range, is returned as NaN rather than as a number, because a saturated
estimator that returns numbers is exactly what went wrong last time.
"""
import argparse
import sys
import wave

import numpy as np

FC0, OCT, Q, GAIN = 9.01, 10.653, 0.693, 4.33
PARAM = np.linspace(0.0, 1.05, 421)          # the search grid, 0.0025 apart
BAND_LO, BAND_HI, PER_OCT = 60.0, 17000.0, 3
MIN_BANDS = 6            # fewer live bands than this and the frame says nothing
MAX_RESID = 4.0          # dB rms; above this the frame is not a 2-pole shape


def load(path):
    with wave.open(path, "rb") as w:
        sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def centres():
    n = int(np.log2(BAND_HI / BAND_LO) * PER_OCT)
    return BAND_LO * 2.0 ** (np.arange(n + 1) / float(PER_OCT))


def band_energy(x, sr, n, hop):
    """Per-frame energy per band, power-summed across channels."""
    cs = centres()
    half = 2.0 ** (0.5 / PER_OCT)
    win = np.hanning(n)
    f = np.fft.rfftfreq(n, 1.0 / sr)
    masks = [(f >= c / half) & (f < c * half) for c in cs]
    nf = max(0, (len(x) - n) // hop)
    out = np.zeros((nf, len(cs)))
    for i in range(nf):
        seg = x[i * hop:i * hop + n, :]
        p = np.zeros(len(f))
        for c in range(seg.shape[1]):
            p += np.abs(np.fft.rfft(seg[:, c] * win)) ** 2
        for b, m in enumerate(masks):
            out[i, b] = p[m].sum() if m.any() else 0.0
    return out, cs


def model_table(cs):
    """Model response in dB, one row per candidate parameter, one column per band."""
    fc = FC0 * 2.0 ** (OCT * PARAM)
    r = cs[None, :] / fc[:, None]
    mag = 1.0 / np.sqrt((1 - r * r) ** 2 + (r / Q) ** 2)
    return 20 * np.log10(mag) + GAIN


def trajectory(ref_path, filt_path, n=512, hop=128, floor_db=-45.0):
    """(times, parameter, residual dB). NaN where the frame cannot say."""
    r, sr = load(ref_path)
    w, _ = load(filt_path)
    m = min(len(r), len(w))
    br, cs = band_energy(r[:m], sr, n, hop)
    bw, _ = band_energy(w[:m], sr, n, hop)
    nf = min(len(br), len(bw))
    br, bw = br[:nf], bw[:nf]

    # A band is live in a frame when the REFERENCE has energy there, judged
    # against that band's own loudest moment so a quiet band is not written off
    # for being quiet.
    ref_pk = br.max(axis=0, keepdims=True) + 1e-30
    live = 10 * np.log10(br / ref_pk + 1e-30) > floor_db

    ratio = 10 * np.log10((bw + 1e-30) / (br + 1e-30))
    table = model_table(cs)

    # Frames are stamped at their CENTRE, not their start. A frame starting at
    # t covers t to t + n/sr, so it can see a trigger that has not happened yet
    # by its own label, which put every peak one window early and made a decay
    # look like it peaked just before the NEXT trigger. Centre stamps also make
    # the window's averaging harmless to a decay fit: averaging an exponential
    # over a window is that exponential at the centre times a constant, and a
    # constant does not move tau. The same is exactly true of a straight line.
    times = (np.arange(nf) * hop + n * 0.5) / float(sr)
    param = np.full(nf, np.nan)
    resid = np.full(nf, np.nan)
    for i in range(nf):
        k = live[i]
        if k.sum() < MIN_BANDS:
            continue
        d = table[:, k] - ratio[i, k][None, :]
        rms = np.sqrt((d * d).mean(axis=1))
        j = int(np.argmin(rms))
        # Pinned at either end means the corner is outside what this frame can
        # see. Report nothing rather than the edge of the grid.
        if j == 0 or j == len(PARAM) - 1 or rms[j] > MAX_RESID:
            continue
        param[i] = PARAM[j]
        resid[i] = rms[j]
    return times, param, resid


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref")
    ap.add_argument("filtered")
    ap.add_argument("--n", type=int, default=512)
    ap.add_argument("--hop", type=int, default=128)
    ap.add_argument("--dump", action="store_true", help="print every frame")
    a = ap.parse_args(argv)
    t, p, r = trajectory(a.ref, a.filtered, a.n, a.hop)
    ok = np.isfinite(p)
    print("%d frames, %d resolved (%.0f%%), residual %.2f dB rms"
          % (len(p), ok.sum(), 100.0 * ok.mean(), np.nanmean(r)))
    print("parameter: min %.3f  median %.3f  max %.3f"
          % (np.nanmin(p), np.nanmedian(p), np.nanmax(p)))
    if a.dump:
        for i in range(len(p)):
            if np.isfinite(p[i]):
                print("%8.4f  %.4f  %.2f" % (t[i], p[i], r[i]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
