#!/usr/bin/env python3
"""Measure the 303 filter as a TRANSFER FUNCTION, sampled at the harmonics.

Why this exists. The metric steering the filter fit, rolloff() in
tools/analyze/batchH_filter.py, is a cumulative-magnitude threshold crossing on
a 110 Hz sawtooth. The cumulative curve climbs in STEPS at each harmonic, so the
crossing frequency snaps to the comb: moving the threshold by +/-2% moves the
answer 0.137 octaves on average and up to 0.356, against a fitted residual of
only 0.237 octaves rms. Worse, five of the six reso-0 cells, which are the
DENOMINATOR of the span diagnostic that the whole corner-rise mechanism was
derived from, sit between harmonics where that jump is 0.27 to 0.36 octaves.

The fix is not a finer threshold. It is to stop reducing a spectrum to one
scalar and measure the filter directly.

Method. Batch H plays note A on a sawtooth with env mod 0, so the source is a
harmonic series at f0 with amplitude proportional to 1/k, and the same source on
both sides. wave_fit.py already established that ReBirth's saw is an ideal 1/k
sawtooth (it had to be, for the pulse-width fit to close to 0.03 dB). So

    |H(k*f0)|  proportional to  sqrt(PSD at harmonic k) * k

recovers the filter's magnitude response, sampled every f0 Hz, with the source
divided out. That is immune to threshold quantisation by construction, and it
yields the three things that were previously entangled in one number:

    passband   level of the flat region below the corner
    peak       where the resonance sits, and how far it stands above passband
    slope      dB per octave well above the corner

The corner-rise question the branch is about is then a DIRECT reading (where is
the peak) rather than an ill-posed inversion. The existing inversion is not
usable for it: audited on the same grid, up to seven distinct corners spanning
0.90 octaves fit a single cell within tolerance, six of 24 cells have no fit at
all, and the search clips at cutoff 127.

READ ONLY. Writes nothing but stdout.
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
CUT = (16, 40, 64, 90, 110, 127)
RESO = (0, 30, 60, 90, 110, 127)
NFFT = 16384          # 2.69 Hz bins: harmonics 110 Hz apart are ~41 bins apart


def psd(path, skip=0.2):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    # These fixtures are bit-identically dual-mono (checked: all 72 files,
    # side/mid at -600 dB), so averaging is a free 3 dB of SNR, not a blind spot.
    x = x.mean(axis=1)
    x = x[int(skip * sr):]
    win = np.hanning(NFFT)
    acc = np.zeros(NFFT // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - NFFT, NFFT // 2):
        acc += np.abs(np.fft.rfft(x[i:i + NFFT] * win)) ** 2
        cnt += 1
    if not cnt:
        return None, None, sr
    return acc / cnt, np.fft.rfftfreq(NFFT, 1.0 / sr), sr


def refine_f0(P, f, f0, kmax=40, span=1.5, rounds=4, grid=201):
    """Sub-bin f0, by finding the value that best aligns the WHOLE comb.

    Locating the fundamental by picking the loudest bin is only accurate to half
    a bin, which is 1.35 Hz here. That error is multiplied by the harmonic
    number, and harmonics() searches a fixed +/-4 bins, so the two cross at
    about k = 15: above that the search window has walked off the harmonic
    entirely and reads the leakage skirt beside it instead of the peak. Since
    the miss grows with k, the recovered response falls faster than the filter
    does, and every slope measured above harmonic 15 comes back too steep.

    Measured on a synthetic sawtooth through an EXACTLY known 4-pole (see
    skirt_calib.py): the snapping estimator reported -31.1 dB/oct over 2x..4x
    the corner where the true answer is -21.4, and -43.4 where the truth was
    -21.6. Both sides of an A/B snap to the same wrong bin, so the error is
    largely shared and DIFFERENCES survived it, but no absolute slope did.

    Scoring interpolates the PSD at each harmonic rather than taking a windowed
    maximum, which makes the score smooth in f0 instead of piecewise constant
    and takes the accuracy from 0.067 Hz to about 0.001 Hz. Logs, so that a
    harmonic near the floor cannot dominate the alignment.
    """
    lo, hi = f0 - span * (f[1] - f[0]), f0 + span * (f[1] - f[0])
    best_f = f0
    for _ in range(rounds):
        cand = np.linspace(lo, hi, grid)
        best = -np.inf
        for c in cand:
            ks = np.arange(1, kmax + 1) * c
            ks = ks[ks < f[-1]]
            s = float(np.sum(np.log10(np.interp(ks, f, P) + 1e-300)))
            if s > best:
                best, best_f = s, c
        step = cand[1] - cand[0]
        lo, hi = best_f - step, best_f + step
    return float(best_f)


def find_f0(P, f, guess=110.0, tol=0.06, refine=True):
    """Locate the fundamental rather than assuming it, so a tune error cannot
    silently shift every harmonic window.

    `refine=False` reproduces the bin-snapping behaviour this had before
    2026-07-31. It is kept only so that a historical number can be regenerated
    and compared; it is not a measurement. See refine_f0() for what it costs.
    """
    band = (f > guess * (1 - tol)) & (f < guess * (1 + tol))
    if not band.any():
        return guess
    idx = np.where(band)[0]
    snapped = float(f[idx[np.argmax(P[idx])]])
    return refine_f0(P, f, snapped) if refine else snapped


def harmonics(P, f, f0, kmax=90, halfwin=4):
    """Amplitude at each harmonic, source 1/k divided out -> |H(k*f0)|."""
    ks, mag = [], []
    for k in range(1, kmax + 1):
        fc = k * f0
        if fc > f[-1] * 0.95 or fc > 16000.0:
            break
        i = int(round(fc / (f[1] - f[0])))
        lo, hi = max(0, i - halfwin), min(len(P), i + halfwin + 1)
        if hi <= lo:
            break
        a = np.sqrt(P[lo:hi].max())       # amplitude, peak of the harmonic lobe
        ks.append(k)
        mag.append(a * k)                 # divide out the saw's 1/k source
    ks = np.array(ks, float)
    m = np.array(mag, float)
    return ks, m


def describe(ks, m, f0):
    """passband dB, peak freq, peak prominence dB, HF slope dB/oct.

    The peak is INTERPOLATED. Harmonic sampling puts one step at 110 Hz, which
    near 440 Hz is a quarter of an octave, so snapping the peak to the nearest
    harmonic would reintroduce exactly the comb quantisation this metric exists
    to escape. A parabola through the three points around the maximum in
    (log2 f, dB) recovers the vertex to well under one harmonic spacing.

    `has_peak` is False when the maximum lands on the first harmonic, which is
    what a filter with no resonant rise looks like. At reso 0 and 30 there IS no
    peak, and reporting the fundamental as "the corner" there is meaningless.
    That is the single most important caveat here, because reso 0 is precisely
    the cell invert_filter.py used as the baseline for its corner-rise claim.
    """
    if len(ks) < 12:
        return None
    db = 20 * np.log10(m / m.max() + 1e-300)
    fr = ks * f0
    # Passband: the first three harmonics sit below the corner in every batch H
    # cell (lowest corner here is ~470 Hz = h4.3), so they define the flat region.
    pb = float(np.mean(db[:3]))
    pi = int(np.argmax(db))
    if 0 < pi < len(db) - 1:
        y0, y1, y2 = db[pi - 1], db[pi], db[pi + 1]
        denom = (y0 - 2 * y1 + y2)
        delta = 0.5 * (y0 - y2) / denom if denom != 0 else 0.0
        delta = float(np.clip(delta, -1.0, 1.0))
        lx = np.log2(fr[pi]) + delta * (np.log2(fr[pi + 1]) - np.log2(fr[pi - 1])) / 2.0
        peak_f = float(2.0 ** lx)
        peak_db = float(y1 - 0.25 * (y0 - y2) * delta)
    else:
        peak_f = float(fr[pi])
        peak_db = float(db[pi])
    peak_p = float(peak_db - pb)
    # A peak has to STAND ABOVE the passband to be a resonance. Testing only
    # that the maximum is not the first harmonic (the original test here) is too
    # weak: at reso 30 the response is nearly flat, so argmax lands on whichever
    # harmonic noise favours, and comparing that position between two builds
    # produced spurious errors of 1.6 to 2.6 octaves. Those cells then dominated
    # the rms and read as a large regression that was entirely an artifact.
    # 1.5 dB is above the flat-response scatter and far below any real
    # resonance (reso 60 runs 3 to 5 dB, reso 127 runs 12 to 18).
    has_peak = bool(pi > 0 and peak_p >= 1.5)
    # HF slope: fit dB against log2 f over the octaves above the peak, so the
    # resonance itself does not set the slope.
    #
    # FLOOR GUARD, added after this metric produced a wrong finding. A spectrum
    # that has reached its dither floor goes flat, and a flat tail reads as a
    # SHALLOW slope. Measured at cutoff 127 resonance 127, ReBirth's harmonics
    # stop standing above the inter-harmonic floor at 7615 Hz while the fit
    # window runs to about 16 kHz, so most of the window was floor. That is
    # where "ReBirth is shallower than us above the corner" came from, and it
    # was dither. Harmonics below `floor_db` relative to the peak are dropped,
    # and the slope is reported as nan rather than as a number when too few
    # real points remain. A missing slope is a better answer than a confounded
    # one: this project has retired findings for less.
    floor_db = -55.0
    lo = peak_f * 2.0
    hi = min(lo * 4.0, fr[-1])
    sel = (fr >= lo) & (fr <= hi) & (db > peak_db + floor_db)
    if sel.sum() >= 4:
        A = np.vstack([np.log2(fr[sel] / lo), np.ones(sel.sum())]).T
        slope = float(np.linalg.lstsq(A, db[sel], rcond=None)[0][0])
    else:
        slope = float("nan")
    slope_pts = int(sel.sum())
    # -3 dB corner relative to passband, first crossing after the peak
    corner = float("nan")
    for j in range(pi, len(db)):
        if db[j] <= pb - 3.0:
            if j > 0:
                y0, y1 = db[j - 1], db[j]
                t = (pb - 3.0 - y0) / (y1 - y0) if y1 != y0 else 0.0
                corner = float(fr[j - 1] + t * (fr[j] - fr[j - 1]))
            else:
                corner = float(fr[j])
            break
    return dict(passband=pb, peak_f=peak_f, peak_p=peak_p, slope=slope,
                corner=corner, has_peak=has_peak, slope_pts=slope_pts,
                level=float(20 * np.log10(m.max() + 1e-300)))


def cells_in(d):
    """Discover the (cutoff, reso) cells present, whatever the batch prefix.

    Batch H names them h303_cutNNN_resoNNN and batch S names them s303_*, and a
    future batch will name them something else. Parsing the filenames rather
    than hardcoding one prefix lets the same metric read any of them, which is
    what makes two batches comparable at all.
    """
    import glob
    import re
    out = []
    for p in glob.glob(os.path.join(d, "*_cut*_reso*.wav")):
        m = re.search(r"_cut(\d+)_reso(\d+)\.wav$", os.path.basename(p))
        if m:
            out.append((int(m.group(1)), int(m.group(2)), p))
    return sorted(out)


def measure_dir(d, discover=False):
    out = {}
    if discover:
        pairs = [(c, r, p) for c, r, p in cells_in(d)]
    else:
        pairs = [(c, r, os.path.join(d, f"h303_cut{c:03d}_reso{r:03d}.wav"))
                 for c in CUT for r in RESO]
    for c, r, p in pairs:
            if not os.path.exists(p):
                continue
            P, f, sr = psd(p)
            if P is None:
                continue
            f0 = find_f0(P, f)
            ks, m = harmonics(P, f, f0)
            desc = describe(ks, m, f0)
            if desc:
                desc["f0"] = f0
                out[(c, r)] = desc
    return out


def table(name, A, B, key, fmt="{:8.0f}", diff=False):
    print(f"--- {name} ---")
    print("cut\\reso" + "".join(f"{r:>9d}" for r in RESO) + "   (RB / ours)")
    for c in CUT:
        ra, rb_ = f"{c:8d}", " " * 8
        for r in RESO:
            if (c, r) in A and (c, r) in B:
                ra += fmt.format(A[(c, r)][key]).rjust(9)
                rb_ += fmt.format(B[(c, r)][key]).rjust(9)
            else:
                ra += f"{'-':>9s}"
                rb_ += f"{'-':>9s}"
        print(ra + "  RB")
        print(rb_ + "  ours")
    print()


def main():
    rb = os.path.join(WS, "renders", "batchH_rb")
    ours = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WS, "renders", "batchH_ours")
    if not os.path.isabs(ours):
        ours = os.path.join(WS, "renders", ours)
    A, B = measure_dir(rb), measure_dir(ours)
    common = sorted(set(A) & set(B))
    print(f"harmonic-domain filter response, {len(common)} cells")
    print(f"ours = {ours}")
    f0s = [A[k]["f0"] for k in common]
    print(f"ReBirth f0 {min(f0s):.2f}..{max(f0s):.2f} Hz  "
          f"(ours {min(B[k]['f0'] for k in common):.2f}.."
          f"{max(B[k]['f0'] for k in common):.2f})\n")

    print("=== WHERE THE RESONANT PEAK SITS (the corner question, measured "
          "directly rather than inverted) ===")
    table("peak frequency Hz", A, B, "peak_f")
    print("=== HOW FAR THE PEAK STANDS ABOVE THE PASSBAND ===")
    table("peak prominence dB", A, B, "peak_p", fmt="{:8.1f}")
    print("=== SLOPE ABOVE THE CORNER (dB/oct) ===")
    table("slope dB/oct", A, B, "slope", fmt="{:8.1f}")

    print("=== PEAK FREQUENCY, ours/ReBirth as octaves (0 = match) ===")
    print("cut\\reso" + "".join(f"{r:>9d}" for r in RESO))
    for c in CUT:
        row = f"{c:8d}"
        for r in RESO:
            if (c, r) in A and (c, r) in B:
                row += f"{np.log2(B[(c,r)]['peak_f'] / A[(c,r)]['peak_f']):9.2f}"
            else:
                row += f"{'-':>9s}"
        print(row)
    print()

    # Only cells where BOTH sides actually have a resonant peak can speak to the
    # corner. At reso 0 and 30 there is none, and the maximum is just the
    # fundamental.
    peaked = [k for k in common if A[k]["has_peak"] and B[k]["has_peak"]]
    print(f"=== cells with a REAL peak on both sides: {len(peaked)} of {len(common)} ===")
    print("    reso 0 and 30 have no resonant rise at all, so 'where the corner is'")
    print("    is not observable there. invert_filter.py nonetheless used reso 0 as")
    print("    the BASELINE of its corner-rise claim, inferring it from rolloff and")
    print("    prominence at the one setting where the filter shows neither.\n")

    print("=== corner rise across the resonance knob, reso 60 -> 127 ===")
    print("    (measured where a peak exists, rather than inverted from where it does not)")
    print(f"    {'cut':>4s} | {'RB 60':>8s} {'RB 127':>8s} {'RB oct':>7s} "
          f"| {'our 60':>8s} {'our 127':>8s} {'our oct':>8s} | {'excess':>7s}")
    rb_o, our_o = [], []
    for c in CUT:
        ka, kb = (c, 60), (c, 127)
        if ka in A and kb in A and ka in B and kb in B:
            a0, a1 = A[ka]["peak_f"], A[kb]["peak_f"]
            b0, b1 = B[ka]["peak_f"], B[kb]["peak_f"]
            ao, bo = np.log2(a1 / a0), np.log2(b1 / b0)
            rb_o.append(ao)
            our_o.append(bo)
            print(f"    {c:4d} | {a0:8.0f} {a1:8.0f} {ao:7.2f} "
                  f"| {b0:8.0f} {b1:8.0f} {bo:8.2f} | {bo-ao:7.2f}")
    if rb_o:
        print(f"    mean ReBirth {np.mean(rb_o):+.2f} oct, ours {np.mean(our_o):+.2f} oct, "
              f"excess {np.mean(our_o)-np.mean(rb_o):+.2f}")
    print()

    if peaked:
        err = np.sqrt(np.mean([np.log2(B[k]["peak_f"] / A[k]["peak_f"]) ** 2 for k in peaked]))
        perr = np.sqrt(np.mean([(B[k]["peak_p"] - A[k]["peak_p"]) ** 2 for k in peaked]))
        byc = {c: np.mean([np.log2(B[(c, r)]["peak_f"] / A[(c, r)]["peak_f"])
                           for r in RESO if (c, r) in peaked])
               for c in CUT if any((c, r) in peaked for r in RESO)}
        print(f"SCORE (peaked cells only)  peak-frequency {err:.4f} oct rms   "
              f"peak-prominence {perr:.2f} dB rms")
        print("  peak-frequency error by cutoff (octaves, + = ours too high):")
        print("   " + "  ".join(f"c{c}:{v:+.2f}" for c, v in byc.items()))


if __name__ == "__main__":
    main()
