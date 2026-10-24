#!/usr/bin/env python3
"""WHERE in the spectrum does CUT_SCALE 0.78 win? That names what it cancels.

THE PUZZLE. `env_traj.py`, which tracks the resonant peak and so measures the
corner directly, says our corner sits 0.10 to 0.18 octaves LOW at both envelope
endpoints with `CUT_SCALE` 0.78, and within 0.09 without it. Yet every aggregate
spectral measure prefers 0.78. So something else in our spectrum is too bright
and a low corner is paying for it. `docs/research/PLAN_303_ENVELOPE.md` item 3.

WHY A PER-BAND VIEW ANSWERS IT. An aggregate rms says 0.78 is better without
saying where, and "better on average" is exactly how one error pays for another.
Split the residual by frequency and the two hypotheses separate:

  if 0.78 wins in a HIGH band while losing near the corner, there is excess HF
  and the corner is being dragged down to hide it. That is a shelf, and it is
  task 40's flat HF shelf if the excess is flat with frequency.

  if 0.78 wins everywhere, the corner instrument is wrong and the corner really
  is where 0.78 puts it.

  if 0.78 wins only near the corner, the two instruments disagree about the
  corner itself and neither hypothesis is about a shelf.

METHOD. Third-octave bands, each render normalised to its OWN total power before
comparison, per the rule in docs/MEASUREMENT_PROTOCOL.md that a level offset
must not masquerade as a spectral error. The level offset is reported separately. Both sides get the
identical treatment and the bands are fixed, so nothing is gated on the data.

MONO. A soloed 303 is one source; stated because this project has been bitten by
silent mono-summing.
"""
import argparse
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)


def read_mono(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def spectrum(p, nfft=8192):
    x, sr = read_mono(p)
    win = np.hanning(nfft)
    acc = np.zeros(nfft // 2 + 1)
    cnt = 0
    for i in range(0, len(x) - nfft, nfft // 2):
        acc += np.abs(np.fft.rfft(x[i:i + nfft] * win)) ** 2
        cnt += 1
    return acc / max(1, cnt), np.fft.rfftfreq(nfft, 1.0 / sr)


def thirds(P, f, lo=80.0, hi=12000.0):
    """Third-octave band powers, and the band centres."""
    centres, vals = [], []
    fc = lo
    while fc < hi:
        a, b = fc / 2 ** (1 / 6), fc * 2 ** (1 / 6)
        m = (f >= a) & (f < b)
        if m.any():
            centres.append(fc)
            vals.append(P[m].sum())
        fc *= 2 ** (1 / 3)
    return np.array(centres), np.array(vals)


def render(fixture, out, scale, seconds):
    env = dict(os.environ)
    env["REBORN_CUT_SCALE"] = str(scale)
    exe = os.path.join(REPO, "build", "Release", "reborn_render.exe")
    r = subprocess.run([exe, "--song", fixture, "--out", out, "--seconds", str(seconds),
                        "--solo", "bass1", "--no-fx"],
                       env=env, capture_output=True, text=True)
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cell", default="g303_c064_em000_dec096_r060")
    ap.add_argument("--fixtures", default=os.path.join(WS, "fixtures-2026-08-02", "envfloor"))
    ap.add_argument("--ref-dir", default=os.path.join(WS, "renders", "envfloor_rb"))
    ap.add_argument("--scales", nargs="*", type=float, default=[0.78, 0.90, 1.00])
    ap.add_argument("--seconds", type=float, default=8.0)
    a = ap.parse_args()

    ref = os.path.join(a.ref_dir, a.cell + ".wav")
    if not os.path.exists(ref):
        sys.exit(f"no reference: {ref}")
    fixture = os.path.join(a.fixtures, a.cell + ".rbs")

    Pr, f = spectrum(ref)
    fc, br = thirds(Pr, f)
    br_n = br / br.sum()

    cols = {}
    for s in a.scales:
        out = os.path.join(WS, "renders", f"_cs_{s}.wav")
        if not render(fixture, out, s, a.seconds):
            print(f"render failed at {s}")
            continue
        Po, _ = spectrum(out)
        _, bo = thirds(Po, f)
        cols[s] = (10 * np.log10(bo / bo.sum() / br_n), 10 * np.log10(bo.sum() / br.sum()))

    print(f"cell {a.cell}")
    print("Third-octave residual, ours minus ReBirth, dB. Each render normalised")
    print("to its OWN total first, so this is SHAPE. Level offset is separate.\n")
    hdr = "  Hz   " + "".join(f"{s:>9.2f}" for s in cols)
    print(hdr)
    print("  " + "-" * (len(hdr) - 2))
    for i, c in enumerate(fc):
        row = f"{c:7.0f}" + "".join(f"{cols[s][0][i]:9.2f}" for s in cols)
        print(row)
    print("\n  level offset dB " + "".join(f"{cols[s][1]:9.2f}" for s in cols))
    print("\n  rms over all bands " + "".join(
        f"{np.sqrt(np.mean(cols[s][0] ** 2)):7.2f}  " for s in cols))
    # Split at 2 kHz: the corner on these cells sits near 500 to 1000 Hz, so
    # above 2 kHz is skirt and shelf, below is passband and corner.
    lowm = fc < 2000
    print("  rms below 2 kHz    " + "".join(
        f"{np.sqrt(np.mean(cols[s][0][lowm] ** 2)):7.2f}  " for s in cols))
    print("  rms above 2 kHz    " + "".join(
        f"{np.sqrt(np.mean(cols[s][0][~lowm] ** 2)):7.2f}  " for s in cols))


if __name__ == "__main__":
    main()
