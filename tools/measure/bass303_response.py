#!/usr/bin/env python3
"""The 303 filter's magnitude response, from a batch B1 render.

Every fixture is a held drone with env mod and accent at zero, so the filter is
static and what changes between fixtures is only the cutoff and resonance knobs.
Dividing a fixture's power spectrum by `b1_ref`, the same song with the filter
wide open and no resonance, cancels the oscillator, the VCA and the output stage
exactly and leaves the filter's response on its own.

Power spectra only. ReBirth's exports are not sample-deterministic, so nothing
here averages coherently or nulls.

Two numbers per fixture, both computed the same way on both sides:

  * CORNER, as the frequency where the response has fallen 3 dB below its own
    passband, found on the harmonic grid the drone provides.

    **DO NOT TRUST THIS ONE.** The passband is taken from the lowest four
    partials of a 55 Hz drone, which are 65, 129, 194 and 231 Hz, so wherever the
    corner comes down near them the "passband" is already in the stopband and the
    statistic stops meaning anything. That is most of the low-cutoff half of the
    plane. On 2026-08-29 it said our corner was 0.284 octaves too low and drove a
    refit the regression net then rejected at +2.094 dB with 327 of 373 cells
    worse, disagreeing about the SIGN. Two passband-free measures in
    `bass303_plane.py` put the corner within 0.08 octaves. Use that tool; this
    one is kept because the peak and the raw response are still useful.
  * PEAK, the resonant prominence: the largest excess over the passband, in dB,
    and where it sits. At high resonance this is the whole story and it is where
    the shipped TPT ladder is known to fall short.

    python bass303_response.py <rebirth-dir> <ours-dir>
    python bass303_response.py <rebirth-dir> <ours-dir> --detail c064_r127
"""
import argparse
import glob
import os
import re
import sys
import wave

import numpy as np

NPERSEG = 8192
BAND_LO, BAND_HI = 40.0, 16000.0
SKIP = 0.5          # seconds, to clear the first attack


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64)
    return x.reshape(-1, ch) / 32768.0, sr


def psd(x, sr, nper=NPERSEG):
    x = x[int(SKIP * sr):]
    h = np.hanning(nper)
    acc, k = None, 0
    for s in range(0, len(x) - nper, nper // 2):
        seg = x[s:s + nper, 0] + x[s:s + nper, 1]
        p = np.abs(np.fft.rfft(seg * h)) ** 2
        acc = p if acc is None else acc + p
        k += 1
    return acc / max(1, k), np.fft.rfftfreq(nper, 1.0 / sr)


def harmonics(pref, f, n=48):
    """The drone's partial frequencies, taken from the REFERENCE render.

    The transfer function is only defined where the source has energy, and the
    source is a saw whose partials are the only places it has any. Reading the
    ratio off the harmonic grid rather than off every bin keeps the noise floor
    out of the answer.
    """
    m = (f >= BAND_LO) & (f <= BAND_HI)
    idx = np.where(m)[0]
    peaks = [i for i in idx[1:-1]
             if pref[i] > pref[i - 1] and pref[i] > pref[i + 1]]
    peaks.sort(key=lambda i: -pref[i])
    return sorted(peaks[:n])


def response(ref_psd, fix_psd, f, idx):
    """(freqs, dB) of fixture over reference, on the harmonic grid."""
    fr = f[idx]
    db = 10 * np.log10((fix_psd[idx] + 1e-30) / (ref_psd[idx] + 1e-30))
    return fr, db


def describe(fr, db):
    """(passband dB, corner Hz, peak excess dB, peak Hz)."""
    if len(fr) < 6:
        return (np.nan,) * 4
    # Passband: the median of the lowest four partials, which sit well below any
    # corner this grid can resolve.
    pb = float(np.median(db[:4]))
    peak_i = int(np.argmax(db))
    peak = float(db[peak_i] - pb)
    # Corner: first partial after the peak that is 3 dB under the passband.
    corner = np.nan
    for i in range(peak_i, len(fr)):
        if db[i] <= pb - 3.0:
            if i == 0:
                corner = float(fr[0])
            else:
                x0, x1 = db[i - 1], db[i]
                t = (pb - 3.0 - x0) / (x1 - x0) if x1 != x0 else 0.0
                corner = float(np.exp(np.log(fr[i - 1]) +
                                      t * (np.log(fr[i]) - np.log(fr[i - 1]))))
            break
    return pb, corner, peak, float(fr[peak_i])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("ours_dir")
    ap.add_argument("--detail", help="print the full response for one fixture")
    a = ap.parse_args(argv)

    def grid(d):
        r, sr = load(os.path.join(d, "b1_ref.wav"))
        p, f = psd(r, sr)
        return p, f, harmonics(p, f)

    pr, fr_, ir = grid(a.ref_dir)
    po, fo_, io = grid(a.ours_dir)

    names = sorted(os.path.basename(x)[:-4]
                   for x in glob.glob(os.path.join(a.ref_dir, "b1_*.wav"))
                   if not x.endswith("b1_ref.wav"))
    if a.detail:
        names = [n for n in names if a.detail in n]
        for n in names:
            for tag, d, p0, f0, idx in (("ReBirth", a.ref_dir, pr, fr_, ir),
                                        ("ours", a.ours_dir, po, fo_, io)):
                x, sr = load(os.path.join(d, n + ".wav"))
                px, _ = psd(x, sr)
                q, db = response(p0, px, f0, idx)
                print("\n%s  %s" % (n, tag))
                for k in range(0, len(q), 4):
                    print("   " + "  ".join("%6.0fHz %+6.1f" % (q[j], db[j])
                                            for j in range(k, min(k + 4, len(q)))))
        return 0

    print("%-18s %19s %19s %19s"
          % ("fixture", "corner Hz (R / ours)", "peak dB (R / ours)",
             "peak Hz (R / ours)"))
    rows = []
    for n in names:
        fa = os.path.join(a.ref_dir, n + ".wav")
        fb = os.path.join(a.ours_dir, n + ".wav")
        if not (os.path.exists(fa) and os.path.exists(fb)):
            continue
        xa, sr = load(fa)
        xb, _ = load(fb)
        pa, _ = psd(xa, sr)
        pb_, _ = psd(xb, sr)
        _, da = response(pr, pa, fr_, ir)
        _, db_ = response(po, pb_, fo_, io)
        A = describe(fr_[ir], da)
        B = describe(fo_[io], db_)
        rows.append((n, A, B))
        print("%-18s %8.0f %8.0f    %8.1f %8.1f    %8.0f %8.0f"
              % (n, A[1], B[1], A[2], B[2], A[3], B[3]))

    ok = [(A, B) for _n, A, B in rows if np.isfinite(A[1]) and np.isfinite(B[1])]
    if ok:
        co = np.array([np.log2(B[1] / A[1]) for A, B in ok])
        pk = np.array([B[2] - A[2] for A, B in ok])
        print("\ncorner error: %.2f octaves mean, %.2f worst  (%d of %d resolved)"
              % (np.abs(co).mean(), np.abs(co).max(), len(ok), len(rows)))
        print("peak error:   %.2f dB mean, %.2f worst" % (np.abs(pk).mean(),
                                                          np.abs(pk).max()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
