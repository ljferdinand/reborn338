#!/usr/bin/env python3
"""Objective function for parameter optimisation against a real ReBirth export.

The octave-band computation here mirrors tools/analyze/measure_ab.py (same band
edges, same window and hop), so the numbers line up with the A/B report you
already trust. The difference: measure_ab.py prints a human report, whereas this
module returns values and a single scalar an optimiser can minimise.

This is the linchpin of the whole harness. Everything else (the render adapter,
the driver, any Claude-side tool) depends on the contract here: given a candidate
WAV and a reference WAV, return a distance.

Note on channels: measure_ab.py's load() downmixes when `channels == 2 OR the
sample count is even`, which would wrongly fold a mono file with an even sample
count into fake stereo. That is exactly the "mono render treated as stereo"
failure the methodology warns about, so load_wav() below keys off the real
channel count only. If you later refactor measure_ab.py to import from here, that
latent bug goes away for free.
"""
from __future__ import annotations

import wave
from typing import Callable, Optional

import numpy as np

# Octave-ish band edges, identical to measure_ab.py.
BAND_EDGES = [20, 40, 80, 160, 315, 630, 1250, 2500, 5000, 10000, 20000]
BAND_LABELS = [f"{lo}-{hi}" for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:])]


def load_wav(path: str, seconds: Optional[float] = None) -> tuple[np.ndarray, int]:
    """Load a 16-bit WAV as a mono float64 array in [-1, 1], plus its sample rate.

    Downmix is decided by the real channel count, not by a size heuristic.
    """
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        ch = w.getnchannels()
        n = w.getnframes() if seconds is None else min(w.getnframes(), int(seconds * sr))
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, sr


def band_energies(x: np.ndarray, sr: int, W: int = 8192, hop_mult: int = 4) -> np.ndarray:
    """Normalised energy fraction in each octave band (sums to 1)."""
    win = np.hanning(W)
    acc = np.zeros(W // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - W, W * hop_mult):
        acc += np.abs(np.fft.rfft(x[i:i + W] * win)) ** 2
        cnt += 1
    psd = acc / max(1, cnt)
    freqs = np.fft.rfftfreq(W, 1 / sr)
    e = np.array([psd[(freqs >= lo) & (freqs < hi)].sum()
                  for lo, hi in zip(BAND_EDGES[:-1], BAND_EDGES[1:])])
    s = e.sum()
    return e / s if s > 0 else e


def band_delta_db(cand: np.ndarray, ref: np.ndarray, sr: int) -> np.ndarray:
    """Per-band level difference (candidate vs reference) in dB."""
    bc = band_energies(cand, sr)
    br = band_energies(ref, sr)
    return 10.0 * np.log10((bc + 1e-12) / (br + 1e-12))


def band_distance(cand: np.ndarray, ref: np.ndarray, sr: int,
                  weights: Optional[list[float]] = None) -> float:
    """Weighted RMS of the per-band dB deltas. Zero means identical balance.

    Weight the low-mid bands (80-160, 160-315, 315-630 -> indices 2, 3, 4) to
    focus the search on the 303 shelf region for Component A.
    """
    delta = band_delta_db(cand, ref, sr)
    w = np.ones(len(delta)) if weights is None else np.asarray(weights, dtype=float)
    if w.shape[0] != delta.shape[0]:
        raise ValueError(f"band_weights must have {delta.shape[0]} entries, got {w.shape[0]}")
    return float(np.sqrt(np.sum(w * delta ** 2) / w.sum()))


def env_swept_centroid_distance(cand: np.ndarray, ref: np.ndarray, sr: int,
                                weights: Optional[list[float]] = None) -> float:
    """Placeholder for the env-swept spectral-centroid metric you use for 303
    brightness. This is the objective your methodology actually favours for the
    brightness gap, but its implementation lives in your own tooling and is not
    reproduced here rather than guessed at. Drop your implementation in and it
    becomes selectable via objective.type in the config."""
    raise NotImplementedError(
        "env_swept_centroid_distance is a stub. Paste your env-swept centroid "
        "metric here (or import it), then set objective.type to "
        "'env_swept_centroid'. Until then use 'band_distance'."
    )


# --- harmonic transfer-function objective -------------------------------------
#
# Why this exists alongside band_distance. Ten octave bands is ten numbers, and
# docs/research/RUNBOOK.md section 7 records what summary statistics cost here: the old
# plucky VCA matched ReBirth on envelope coefficient-of-variation AND silence
# fraction and was still the wrong shape entirely. Two scalars cannot
# distinguish two shapes, and ten is not obviously enough either.
#
# For a filter there is a better instrument. The 303 fixtures play a sawtooth at
# f0 on both sides, and tools/analyze/wave_fit.py established that ReBirth's saw
# is an ideal 1/k series, so
#
#     |H(k*f0)|  proportional to  sqrt(PSD at harmonic k) * k
#
# recovers the filter's magnitude response sampled every f0 Hz with the source
# divided out. Comparing those curves point by point is thousands of constraints
# instead of ten, and it cannot be gamed by trading one band against another.
#
# Each curve is normalised to its OWN passband before comparison, per the
# measurement-protocol rule (docs/MEASUREMENT_PROTOCOL.md) that spectral shape
# is reported relative to each file's own
# total so a level offset cannot masquerade as a spectral error. The level
# offset is returned separately by curve_diagnostics().

HARM_NFFT = 16384        # 2.69 Hz bins; harmonics 110 Hz apart are ~41 apart
HARM_FLOOR_DB = -55.0    # below this, relative to passband, both sides are dither


def _harmonic_curve(x: np.ndarray, sr: int, f0_guess: float = 0.0):
    """(frequencies, dB relative to own passband) for one signal."""
    W = HARM_NFFT
    if len(x) < W * 2:
        return None
    win = np.hanning(W)
    acc = np.zeros(W // 2 + 1)
    cnt = 0
    for i in range(int(0.2 * sr), len(x) - W, W // 2):
        acc += np.abs(np.fft.rfft(x[i:i + W] * win)) ** 2
        cnt += 1
    if not cnt:
        return None
    P = acc / cnt
    f = np.fft.rfftfreq(W, 1.0 / sr)
    # Locate the fundamental rather than assuming it, so a tune error cannot
    # silently shift every harmonic window.
    if f0_guess <= 0:
        lo_i = int(80.0 / (f[1] - f[0]))
        hi_i = int(900.0 / (f[1] - f[0]))
        f0 = float(f[lo_i + int(np.argmax(P[lo_i:hi_i]))])
    else:
        band = (f > f0_guess * 0.94) & (f < f0_guess * 1.06)
        idx = np.where(band)[0]
        f0 = float(f[idx[np.argmax(P[idx])]]) if len(idx) else f0_guess
    if f0 <= 0:
        return None
    binw = f[1] - f[0]
    ks, mags = [], []
    for k in range(1, 200):
        fc = k * f0
        if fc > min(16000.0, f[-1] * 0.95):
            break
        i = int(round(fc / binw))
        if i + 4 >= len(P):
            break
        # * k divides out the sawtooth's own 1/k envelope
        mags.append(np.sqrt(P[max(0, i - 3):i + 4].max()) * k)
        ks.append(k)
    if len(ks) < 12:
        return None
    db = 20.0 * np.log10(np.asarray(mags) + 1e-300)
    return np.asarray(ks, float) * f0, db - float(np.mean(db[:3]))


def harmonic_curve_distance(cand: np.ndarray, ref: np.ndarray, sr: int,
                            weights: Optional[list[float]] = None) -> float:
    """RMS dB difference of the two normalised transfer-function curves.

    `weights` is ignored: every harmonic above the floor counts once. There is
    no band weighting to tune, which is deliberate. A weight vector is a free
    parameter, and this project has a standing rule against buying small gains
    with free parameters.
    """
    A = _harmonic_curve(ref, sr)
    if A is None:
        return float("inf")
    B = _harmonic_curve(cand, sr, f0_guess=A[0][0])
    if B is None:
        return float("inf")
    fa, da = A
    fb, db_ = B
    n = min(len(da), len(db_))
    # Only where the REFERENCE has real signal. Fixed threshold, not an SNR
    # gate: a gate whose pass condition depends on the noise realisation makes
    # the score itself unstable, which cost this project a retracted finding on
    # 2026-07-30.
    sel = da[:n] > HARM_FLOOR_DB
    if sel.sum() < 8:
        return float("inf")
    return float(np.sqrt(np.mean((db_[:n][sel] - da[:n][sel]) ** 2)))


def curve_diagnostics(cand: np.ndarray, ref: np.ndarray, sr: int) -> dict:
    """Where the curves differ, and the level offset, for the run log."""
    A = _harmonic_curve(ref, sr)
    B = _harmonic_curve(cand, sr, f0_guess=A[0][0]) if A is not None else None
    if A is None or B is None:
        return {}
    fa, da = A
    fb, db_ = B
    n = min(len(da), len(db_))
    sel = da[:n] > HARM_FLOOR_DB
    d = db_[:n][sel] - da[:n][sel]
    fr = fa[:n][sel]
    return {
        "harmonics_used": int(sel.sum()),
        "f0_hz": round(float(fa[0]), 2),
        "worst_dev_db": round(float(np.max(np.abs(d))), 2),
        "worst_at_hz": int(fr[int(np.argmax(np.abs(d)))]),
        "mean_dev_db": round(float(np.mean(d)), 3),
    }


# Objective registry: name -> f(cand_x, ref_x, sr, weights) -> float.
OBJECTIVES: dict[str, Callable[..., float]] = {
    "band_distance": band_distance,
    "env_swept_centroid": env_swept_centroid_distance,
    "harmonic_curve": harmonic_curve_distance,
}


def noise_floor_many(refs: list, kind: str = "band_distance",
                     seconds: Optional[float] = None,
                     weights: Optional[list[float]] = None) -> dict:
    """The floor from N exports of ONE fixture, over every pair. USE THIS ONE.

    A single pair is not enough, and the reason is specific rather than
    statistical caution. Measured 2026-07-31: six exports of one fixture
    produced only FOUR distinct outputs, because ReBirth does not start from a
    random phase but from a small set of discrete states. For N equally likely
    states the expected number of distinct values in six draws is
    N(1-(1-1/N)^6), which is 3.99 at N=6, matching what was observed. It also
    predicts about one collision in six independent pairs, which is exactly what
    the batch S duplicates showed and what looked so puzzling.

    The consequence is sharp. Two exports collide outright roughly one time in
    six, and a colliding pair scores ZERO. So a single-pair floor is a draw from
    a distribution that contains zeros, and it will sometimes report that the
    objective is perfectly precise when it is not. That happened: the first
    floor measured for cut110 reso127 was 0.000, while the true spread over
    fifteen pairs of that same cell is a median of 1.43 and a max of 1.58.

    Returns min, median and max. Use the MEDIAN as the working floor and the max
    as the pessimistic one. Never use a single pair.
    """
    import itertools
    vals = [objective(a, b, kind=kind, seconds=seconds, weights=weights)["objective"]
            for a, b in itertools.combinations(refs, 2)]
    if not vals:
        raise ValueError("need at least two reference exports")
    arr = np.asarray(vals, dtype=float)
    return {
        "min": float(arr.min()),
        "median": float(np.median(arr)),
        "max": float(arr.max()),
        "pairs": len(vals),
        "collisions": int((arr <= 1e-9).sum()),
    }


def average_reference(refs: list, seconds: Optional[float] = None):
    """Average the SPECTRA of N exports into one lower-noise reference curve.

    Do not average the waveforms: the states are sample-wise uncorrelated
    (measured r between -0.16 and +0.25), so summing them cancels signal rather
    than noise. Averaging the harmonic curves is safe, because each state has
    the same spectrum plus independent scatter.

    Measured benefit, per-harmonic spread across the six states: 0.46 dB from
    one export, falling as 1/sqrt(N) to 0.32 at two, 0.27 at three and 0.19 at
    six. Three exports more than halves the floor for about 30 extra seconds of
    ReBirth time, which is the best return on offer anywhere in this loop.

    Returns (frequencies, mean dB curve, per-harmonic standard deviation).
    """
    curves = []
    for p in refs:
        x, sr = load_wav(p, seconds)
        c = _harmonic_curve(x, sr)
        if c is not None:
            curves.append(c)
    if not curves:
        return None
    n = min(len(c[1]) for c in curves)
    stack = np.vstack([c[1][:n] for c in curves])
    return curves[0][0][:n], stack.mean(axis=0), stack.std(axis=0)


def noise_floor(ref_a: str, ref_b: str, kind: str = "band_distance",
                seconds: Optional[float] = None,
                weights: Optional[list[float]] = None) -> float:
    """The objective's own resolution, from two exports of ONE fixture.

    This is the most important guard in the file and it is the one that was
    missing when it mattered. On 2026-07-30 an estimator that looked perfectly
    healthy in a single run was found to swing 8.8 dB/oct between two ReBirth
    exports of identical settings, while their octave-band energies agreed to
    0.029 dB. A finding had already been committed on it and had to be
    retracted. See docs/research/FINDINGS_AUDIT.md.

    ReBirth's exports are not sample-deterministic: oscillator and noise phase
    are not reset per render. So scoring two exports of the SAME fixture against
    each other gives the objective's floor. A candidate that scores at or below
    that number is not "nearly perfect", it is indistinguishable from a second
    export of the reference, and any search that keeps descending past it is
    fitting the reference's noise realisation.

    An optimiser makes this worse rather than better, because it will find and
    exploit that noise far faster and more confidently than a human sweeping by
    hand. Calibrate before you search.
    """
    return objective(ref_a, ref_b, kind=kind, seconds=seconds,
                     weights=weights)["objective"]


def objective(candidate_path: str, reference_path: str, kind: str = "band_distance",
              seconds: Optional[float] = None,
              weights: Optional[list[float]] = None) -> dict:
    """Load both WAVs and compute the scalar objective plus diagnostics.

    Returns a dict with `objective` (the scalar to minimise), `delta_db`
    (per-band, for logging), and the band vectors, so a run log is useful for
    seeing where the difference sits, not just how large it is.
    """
    if kind not in OBJECTIVES:
        raise ValueError(f"unknown objective '{kind}'; options: {sorted(OBJECTIVES)}")
    cand, sr = load_wav(candidate_path, seconds)
    ref, sr_ref = load_wav(reference_path, seconds)
    if sr != sr_ref:
        raise ValueError(f"sample-rate mismatch: candidate {sr} Hz vs reference {sr_ref} Hz")
    # Compare like-for-like lengths.
    n = min(len(cand), len(ref))
    cand, ref = cand[:n], ref[:n]

    value = OBJECTIVES[kind](cand, ref, sr, weights)
    out = {
        "objective": value,
        "kind": kind,
        "delta_db": [round(float(d), 3) for d in band_delta_db(cand, ref, sr)],
        "bands": BAND_LABELS,
    }
    if kind == "harmonic_curve":
        # Where the curve misses matters more than by how much, and a run log
        # that only records the scalar cannot tell a fit that fixed the shape
        # from one that traded one region against another.
        out.update(curve_diagnostics(cand, ref, sr))
    return out
