#!/usr/bin/env python3
"""Is the 303 output-level deficit a SCALAR yet, or does it still tilt?

docs/COMPONENT_MATRIX.md records our output running 2.2 to 4.8 dB below
ReBirth's with the deficit GROWING with cutoff, and says plainly not to correct
it with a scalar, because the cutoff-dependent part belongs to the filter and
calibrating before that is fitted means calibrating twice.

The mechanism was understood: opening a lowpass passes more of the source, so
ReBirth's peak rises 3.54 dB across its cutoff sweep where ours rose 0.98. That
was round-3 evidence for the filter defect, measured with a completely different
instrument from the spectral ones, which is why it counted.

Now that the filter is refitted, this asks the question that decides what to do
next. Report the level offset per cell, then split it into

    MEAN    a constant, which a single makeup gain fixes
    TILT    the part that varies with cutoff, which is still filter error

A flat offset means the level is now a one-number calibration. A surviving tilt
means the gain would be absorbing filter error again, which is the mistake this
project has already made with K_CUT and with the drive shaper.

Level is measured two ways because they fail differently. Peak is what the
earlier finding used and is sensitive to a single sample. RMS over the sounding
part is more stable but moves if the envelope shape differs. They should agree
on the TILT even if they disagree on the mean.

Stereo note: the batch H fixtures are bit-identically dual-mono on both sides,
checked across all 72 files, so channel averaging here is a free 3 dB of SNR
rather than the blindness that hid the pan law.

Usage:  python level_ab.py [ours-dir ...]
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
RB = os.path.join(WS, "renders", "batchH_rb")


def levels(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch).mean(axis=1)
    x = x[int(0.2 * sr):]
    if not len(x):
        return None
    peak = float(np.max(np.abs(x)))
    # RMS over the sounding part only, so a longer silent tail cannot dilute it.
    thr = peak * 0.02
    sounding = x[np.abs(x) > thr]
    rms = float(np.sqrt(np.mean(sounding ** 2))) if len(sounding) else 0.0
    db = lambda v: 20.0 * np.log10(v + 1e-30)
    return db(peak), db(rms)


def measure(d):
    out = {}
    for c in CUT:
        for r in RESO:
            p = os.path.join(d, f"h303_cut{c:03d}_reso{r:03d}.wav")
            if os.path.exists(p):
                v = levels(p)
                if v:
                    out[(c, r)] = v
    return out


def report(tag, A, B):
    ks = sorted(set(A) & set(B))
    if not ks:
        print(f"  {tag}: no common cells")
        return
    for idx, name in ((0, "peak"), (1, "rms ")):
        d = {k: B[k][idx] - A[k][idx] for k in ks}
        allv = np.array(list(d.values()))
        bycut = [np.mean([d[(c, r)] for r in RESO if (c, r) in d]) for c in CUT]
        tilt = bycut[-1] - bycut[0]
        # Spread of the per-cutoff means around their own average is the part a
        # scalar cannot remove.
        resid = float(np.std(bycut))
        print(f"  {tag:<22s} {name}  mean {allv.mean():+6.2f} dB   "
              f"by cutoff " + " ".join(f"{v:+6.2f}" for v in bycut) +
              f"   tilt {tilt:+5.2f}  spread {resid:4.2f}")


def rb_own_rise(A):
    """ReBirth's own level rise across the cutoff knob, the thing we must match."""
    for idx, name in ((0, "peak"), (1, "rms ")):
        bycut = [np.mean([A[(c, r)][idx] for r in RESO if (c, r) in A]) for c in CUT]
        print(f"  ReBirth absolute      {name}  " +
              " ".join(f"{v:+6.2f}" for v in bycut) +
              f"   rises {bycut[-1]-bycut[0]:+5.2f} dB across the knob")


if __name__ == "__main__":
    A = measure(RB)
    print(f"ReBirth cells: {len(A)}\n")
    rb_own_rise(A)
    print()
    dirs = sys.argv[1:] or ["probe_baseline_verify", "probe_fitted", "probe_nodrive"]
    print("ours minus ReBirth, dB (negative = we are quiet):")
    for d in dirs:
        p = d if os.path.isabs(d) else os.path.join(WS, "renders", d)
        if os.path.isdir(p):
            report(os.path.basename(p), A, measure(p))
        else:
            print(f"  missing: {p}")
    print("\nTILT is what matters. A flat offset is one makeup gain; a surviving")
    print("tilt is filter error that a gain would only absorb.")
