#!/usr/bin/env python3
"""The filter envelope as a CORNER TRAJECTORY, and which domain it moves in.

WHY THIS IS A TOOL. The finding it implements is the centre of the 2026-08-02
round, that ReBirth's filter envelope is exponential in FREQUENCY where ours is
exponential in OCTAVES, and the instrument that established it was written in a
scratchpad and thrown away. Steps 2 and 3 of docs/research/PLAN_303_ENVELOPE.md both score
on this measurement, so it needs to be repeatable by someone who was not there.

WHAT IT MEASURES, and why not brightness. A band ratio says the filter is lower
without saying where the corner is, and it moves with resonance and with the
oscillator's own spectrum. This tracks the RESONANT PEAK, which for a resonant
ladder sits at the corner, by sampling the filter's transfer function at the
harmonics of the note:

    |H(k f0)| = sqrt(PSD at k f0) * k          for an ideal 1/k sawtooth

The 2026-07-27 wave fit established that ReBirth's saw IS ideal 1/k and that the
path is linear at these settings, which is what makes the division legitimate.
The peak is then interpolated by a parabola through the three harmonics around
the largest, IN LOG FREQUENCY, which is what stops the estimate quantising to
the harmonic comb. Quantisation is not a small effect here: at 110 Hz the comb
is a semitone wide by the third harmonic and much coarser below.

WHY TIED FIXTURES. The gate closes at half a step, so on a plucked note the
later trajectory mixes the filter envelope with the VCA release and the filter's
own ring. A tied note holds the gate open for the whole file, so one envelope is
visible from onset to floor. See tools/rbsgen/gen_envdomain.py for the rest of
the fixture design, including why resonance 60 is safe for a tie.

THE TWO MODELS, and the one thing that makes the comparison fair:

    octaves     log2 f(t) = a + b exp(-t/tau)
    frequency        f(t) = A + B exp(-t/tau)

BOTH are fitted by minimising the SAME objective, rms residual in OCTAVES, so
the only difference scored is the functional form. Fitting each in its own
natural domain would hand each model a home-ground advantage and the comparison
would mean nothing. The octave model is linear in its parameters and solves in
closed form; the frequency model is solved by iteratively reweighted least
squares on 2^y, which is the same objective to first order and converges in a
handful of passes.

The two curves are only distinguishable over a LARGE excursion. Under about an
octave of travel they agree to within the scatter and this tool will report a
tie, correctly. Do not read a tie as evidence for octaves.

MONO. The spectra are computed on the L+R mean. That is stated because this
project has been bitten by silent mono-summing before; it is safe here because a
soloed 303 is one source and the pan law cannot cancel it.

USAGE
    python env_traj.py                       # ReBirth vs ours, both domains
    python env_traj.py --roots envdomain_rb envdomain_ours_hz
    python env_traj.py --cell f303_em127_dec127_tie_c016 --dump
"""
import argparse
import math
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
RENDERS = os.path.join(WS, "renders")

FRAME = 4096          # 92.9 ms at 44.1 kHz; short against the 0.81 s and 2.14 s taus
HOP = 512             # 11.6 ms
KMAX = 40             # harmonics to sample; 40 * 110 Hz is above the 6 kHz ceiling


def read_mono(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def estimate_f0(x, sr):
    """Autocorrelation pitch on a mid-file second. Robust to heavy filtering,
    which a spectral-peak pick is not: at cutoff 16 the loudest partial is the
    resonant one, not the fundamental."""
    a = int(2.0 * sr)
    seg = x[a:a + sr]
    if len(seg) < sr // 2:
        seg = x[: sr]
    seg = seg - seg.mean()
    # autocorrelation via FFT. A direct np.correlate here is O(n^2) on a full
    # second of audio and was, by a wide margin, the slowest thing in the tool.
    n = 1 << int(np.ceil(np.log2(2 * len(seg))))
    S = np.fft.rfft(seg, n)
    ac = np.fft.irfft(S * np.conj(S), n)[:len(seg)]
    lo, hi = int(sr / 400.0), int(sr / 40.0)
    if hi >= len(ac):
        hi = len(ac) - 2
    i = lo + int(np.argmax(ac[lo:hi]))
    # parabolic refinement on the lag
    y0, y1, y2 = ac[i - 1], ac[i], ac[i + 1]
    d = y0 - 2 * y1 + y2
    frac = 0.5 * (y0 - y2) / d if d != 0 else 0.0
    return sr / (i + frac)


def onset(x, sr):
    """First sample above 2 percent of peak, backed off one millisecond."""
    pk = np.max(np.abs(x))
    if pk <= 0:
        return 0
    idx = np.argmax(np.abs(x) > 0.02 * pk)
    return max(0, int(idx) - int(0.001 * sr))


def peak_trace(path, fmin_mult=1.8, fmax=5200.0):
    """Interpolated resonant-peak frequency per frame, from the note onset.

    Returns (t seconds, f_peak Hz, f0). Frames whose peak falls on the first or
    last sampled harmonic are dropped: the parabola needs a neighbour on each
    side, and a peak pinned at harmonic 1 means the corner has swept below the
    fundamental and is no longer observable by this method at all.

    THE BAND GUARD IS NOT OPTIONAL AND HERE IS WHAT IT COST TO LEARN. The first
    version of this tool had none, and its ground-truth test failed: given our
    own renders with the envelope KNOWN to be in octaves, it answered "hz" on a
    third of them and "oct" on none. The cause was our filter's 6 kHz ceiling.
    Driven hard, the corner pins at the ceiling for the first part of the note
    and only then decays, and a trajectory that is flat early and falling later
    is exactly the frequency-domain signature. The instrument was reading a
    clamp as a finding.

    So: keep only frames where the peak is inside a band it can actually be
    measured in, above fmin_mult * f0 so the parabola has harmonics either side,
    and below fmax so a ceiling cannot masquerade as a shape. Time is re-zeroed
    on the first retained frame, which leaves the exponential family unchanged
    and only reparametrises its amplitude."""
    x, sr = read_mono(path)
    f0 = estimate_f0(x, sr)
    a0 = onset(x, sr)
    win = np.hanning(FRAME)
    freqs = np.fft.rfftfreq(FRAME, 1.0 / sr)
    binw = freqs[1]
    kmax = min(KMAX, int((sr * 0.45) / f0) - 1)
    # All frames at once. A per-frame Python loop made a parameter sweep too slow
    # to run, and this is the same arithmetic batched, not an approximation.
    nfr = (len(x) - a0 - FRAME) // HOP + 1
    if nfr < 2:
        return np.array([]), np.array([]), f0
    idx = a0 + np.arange(nfr)[:, None] * HOP + np.arange(FRAME)[None, :]
    P = np.abs(np.fft.rfft(x[idx] * win, axis=1)) ** 2          # nfr x bins
    ks = np.arange(1, kmax + 1)
    h = np.empty((nfr, kmax))
    for i, k in enumerate(ks):
        c = int(round(k * f0 / binw))
        lo, hi = max(0, c - 2), min(P.shape[1], c + 3)
        # max over a +/-2 bin neighbourhood: the note is not bin-centred and the
        # window leaks, so the peak bin is the honest estimate of the harmonic.
        h[:, i] = P[:, lo:hi].max(axis=1)
    # |H(k f0)| for an ideal 1/k saw, in dB
    db = 10.0 * np.log10(h + 1e-30) + 20.0 * np.log10(ks)[None, :]
    j = np.argmax(db, axis=1)
    ok = (j > 0) & (j < kmax - 1)      # the parabola needs a neighbour each side
    lf = np.log2(ks * f0)
    r = np.arange(nfr)
    y0, y1, y2 = db[r, np.maximum(j - 1, 0)], db[r, j], db[r, np.minimum(j + 1, kmax - 1)]
    d = y0 - 2 * y1 + y2
    frac = np.where(d != 0, 0.5 * (y0 - y2) / np.where(d != 0, d, 1.0), 0.0)
    # harmonics are equally spaced in log f only approximately; use the local
    # spacing, which is what the parabola is actually fitted on
    stepj = 0.5 * (lf[np.minimum(j + 1, kmax - 1)] - lf[np.maximum(j - 1, 0)])
    t = (r * HOP + FRAME / 2.0) / sr
    f = 2.0 ** (lf[j] + frac * stepj)
    t, f = t[ok], f[ok]
    keep = (f >= fmin_mult * f0) & (f <= fmax)
    # a single contiguous run: the corner sweeps monotonically, so re-entering
    # the band later means the estimate has jumped a harmonic, not that the
    # filter turned round
    if keep.any():
        i0 = int(np.argmax(keep))
        i1 = i0
        while i1 < len(keep) and keep[i1]:
            i1 += 1
        t, f = t[i0:i1], f[i0:i1]
    else:
        t, f = t[:0], f[:0]
    return (t - t[0] if len(t) else t), f, f0


def fit_oct(t, y):
    """log2 f = a + b exp(-t/tau). Linear in (a, b) at fixed tau."""
    best = None
    for tau in np.exp(np.linspace(math.log(0.02), math.log(8.0), 400)):
        u = np.exp(-t / tau)
        A = np.column_stack([np.ones_like(u), u])
        coef, *_ = np.linalg.lstsq(A, y, rcond=None)
        r = y - A @ coef
        s = float(np.sqrt((r ** 2).mean()))
        if best is None or s < best[0]:
            best = (s, tau, coef[0], coef[1])
    s, tau, a, b = best
    return dict(rms=s, tau=tau, span=abs(b), model="oct")


def fit_hz(t, y):
    """f = A + B exp(-t/tau), scored in OCTAVES so the comparison is fair.

    Minimising sum (y - log2(A + B u))^2 is, to first order, a weighted linear
    least squares on z = 2^y with weight 1/g where g is the current model value.
    Four IRLS passes are ample; the residuals here are a few percent."""
    z = np.exp2(y)
    best = None
    for tau in np.exp(np.linspace(math.log(0.02), math.log(8.0), 400)):
        u = np.exp(-t / tau)
        A = np.column_stack([np.ones_like(u), u])
        g = z.copy()
        coef = None
        for _ in range(4):
            w = 1.0 / np.maximum(g, 1e-9)
            coef, *_ = np.linalg.lstsq(A * w[:, None], z * w, rcond=None)
            g = np.maximum(A @ coef, 1e-9)
        r = y - np.log2(np.maximum(A @ coef, 1e-9))
        s = float(np.sqrt((r ** 2).mean()))
        if best is None or s < best[0]:
            best = (s, tau, coef[0], coef[1])
    s, tau, Ac, Bc = best
    span = math.log2(max(Ac + Bc, 1e-9) / max(Ac, 1e-9)) if Ac > 0 else float("nan")
    return dict(rms=s, tau=tau, span=span, model="hz")


def score(path, fmin_mult=1.8, fmax=5200.0):
    t, f, f0 = peak_trace(path, fmin_mult, fmax)
    if len(t) < 20:
        return None
    y = np.log2(f)
    travel = float(y.max() - y.min())
    # Leading-flat frame count, reported as a diagnostic and NOT as a verdict.
    # A flat start is what a clamp looks like, and it is also what the frequency
    # domain legitimately looks like, so the two cannot be told apart by this
    # number alone. It is here because it is the fingerprint of the failure the
    # band guard exists to prevent: if this is large AND the window runs up to
    # the filter ceiling, suspect a clamp before believing a shape.
    lead = int(np.argmax(y < y.max() - 0.02)) if (y < y.max() - 0.02).any() else len(y)
    o, h = fit_oct(t, y), fit_hz(t, y)
    return dict(f0=f0, n=len(t), travel=travel, dur=float(t[-1]),
                fhi=float(f.max()), flo=float(f.min()),
                flat=lead, oct=o, hz=h,
                pct_oct=100.0 * o["rms"] / travel if travel > 0 else float("nan"),
                pct_hz=100.0 * h["rms"] / travel if travel > 0 else float("nan"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roots", nargs="*", default=["envdomain_rb",
                                                   "envdomain_ours_oct",
                                                   "envdomain_ours_hz"])
    ap.add_argument("--cell", default=None, help="one fixture stem, else all")
    ap.add_argument("--dump", action="store_true",
                    help="print the raw trajectory for the chosen cell")
    ap.add_argument("--fmin-mult", type=float, default=1.8,
                    help="low edge of the trackable band, in units of f0")
    ap.add_argument("--fmax", type=float, default=5200.0,
                    help="high edge; keep it below the filter ceiling")
    ap.add_argument("--kmax", type=int, default=None,
                    help="harmonics to sample. The observable band tops out at "
                         "kmax * f0, so a LOW note needs a HIGH kmax: at 55 Hz "
                         "the default 40 would cut the band off at 2.2 kHz and "
                         "silently truncate every onset above it.")
    a = ap.parse_args()
    if a.kmax:
        global KMAX
        KMAX = a.kmax

    roots = [r if os.path.isabs(r) else os.path.join(RENDERS, r) for r in a.roots]
    for r in roots:
        if not os.path.isdir(r):
            sys.exit(f"no such render directory: {r}")

    cells = sorted(os.path.splitext(n)[0] for n in os.listdir(roots[0])
                   if n.endswith(".wav"))
    if a.cell:
        cells = [c for c in cells if c == a.cell] or sys.exit(f"no cell {a.cell}")

    if a.dump:
        for r in roots:
            p = os.path.join(r, cells[0] + ".wav")
            if not os.path.exists(p):
                continue
            t, f, f0 = peak_trace(p, a.fmin_mult, a.fmax)
            print(f"\n{os.path.basename(r)}  {cells[0]}  f0 {f0:.2f} Hz  "
                  f"{len(t)} frames in band")
            for i in range(0, len(t), max(1, len(t) // 24)):
                print(f"  {t[i]:6.3f} s   {f[i]:8.1f} Hz   {np.log2(f[i]):7.3f}")
        return

    print("FILTER ENVELOPE TRAJECTORY: the resonant peak through a tied note.")
    print("travel is the measured octave span; resid is rms error in OCTAVES as")
    print("a percent of that span, both models scored on the same objective.\n")
    hdr = f"{'cell':<30}{'source':<18}{'band Hz':>14}{'win s':>7}{'travel':>8}" \
          f"{'tau oct':>9}{'tau hz':>8}{'r oct%':>8}{'r hz%':>7}  verdict"
    print(hdr)
    print("-" * len(hdr))
    wins = {}
    for c in cells:
        for r in roots:
            p = os.path.join(r, c + ".wav")
            if not os.path.exists(p):
                continue
            s = score(p, a.fmin_mult, a.fmax)
            lbl = os.path.basename(r)
            if s is None:
                print(f"{c:<30}{lbl:<18}   trajectory too short to fit")
                continue
            # a tie is a real answer: under about an octave of travel the two
            # models are not distinguishable and saying so is the honest result
            ratio = s["pct_oct"] / s["pct_hz"] if s["pct_hz"] > 0 else 1.0
            verdict = "hz" if ratio > 1.25 else ("oct" if ratio < 0.8 else "tie")
            if s["flat"] > 3:
                verdict += f"  (flat start {s['flat']})"
            wins.setdefault(lbl, []).append(verdict.split()[0])
            band = f"{s['fhi']:.0f}-{s['flo']:.0f}"
            print(f"{c:<30}{lbl:<18}{band:>14}{s['dur']:>7.2f}{s['travel']:>8.2f}"
                  f"{s['oct']['tau']:>9.2f}{s['hz']['tau']:>8.2f}"
                  f"{s['pct_oct']:>8.2f}{s['pct_hz']:>7.2f}  {verdict}")
        print()
    print("verdict tally, by source:")
    for lbl, vs in wins.items():
        print(f"  {lbl:<22}hz {vs.count('hz'):>2}   oct {vs.count('oct'):>2}"
              f"   tie {vs.count('tie'):>2}")


if __name__ == "__main__":
    main()
