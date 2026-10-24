#!/usr/bin/env python3
"""Which STEPS retrigger, ours against ReBirth. A rhythm instrument, not a tone one.

This exists because the same defect family has now surfaced three times and every
time it was Luke's ear that found it, never a metric:

  1. a tied rest treated as a note-off, a 32 dB hole
  2. the accent row read on a rest, landing on whatever sounds through it
  3. this one: our Clodhopper plays MORE notes than ReBirth late in the loop

All three are interactions between two step flags, which is why no
one-control-at-a-time sweep could reach them, and all three are RHYTHM defects.
docs/research/GOALS.md ranks sound and rhythm together, so these outrank any amount of
spectral polish.

WHAT IT MEASURES. For each 16th-note step, the ratio between the loudest point
early in the step and the quietest point at the end of the previous one. A
retrigger closes the VCA and reopens it, giving a large positive ratio. A note
held through the step boundary gives roughly zero. The two sides are compared
step by step, so the output names the exact step index where they disagree, which
is what turns "more notes near the end" into a pattern position that can be read
out of the .rbs.

WHY NOT AN ONSET DETECTOR. Standard spectral-flux onset detection would fire on
the filter envelope's attack as well as the VCA's, and the 303's filter sweeps on
every note. Ratio-of-envelope across the step boundary asks the narrower question
that actually matters here: did the amplitude gate close and reopen.

ALIGNMENT is measured, not assumed. Our renders and ReBirth's exports sit within
a few ms of each other, which is a fraction of a step but enough to bias a window
placed at a step boundary. The lag is recovered by cross-correlating the two
envelopes and reported, so a bad alignment announces itself instead of quietly
shifting every reading by one window.

Mono-summed deliberately: this asks about timing, not about stereo placement, and
the check below reports side energy so a genuinely stereo input cannot pass
unnoticed.

Usage:
  python onset_ab.py <rebirth.wav> <ours.wav> --bpm 140 [--bars 7] [--thresh 4]
"""
import argparse
import os
import wave

import numpy as np


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    side = 0.0
    if ch == 2:
        s = x[:, 0] - x[:, 1]
        side = 20 * np.log10(np.sqrt((s ** 2).mean()) + 1e-12)
    return x.mean(axis=1), sr, side


def envelope(x, sr, ms=3.0):
    n = max(1, int(ms * 1e-3 * sr))
    return np.convolve(np.abs(x), np.ones(n) / n, mode="same")


def align(a, b, sr, max_ms=60.0):
    """Lag in samples that best aligns b onto a, from the envelopes."""
    m = min(len(a), len(b))
    ea = a[:m] - a[:m].mean()
    eb = b[:m] - b[:m].mean()
    n = 1 << int(np.ceil(np.log2(2 * m)))
    r = np.fft.irfft(np.fft.rfft(ea, n) * np.conj(np.fft.rfft(eb, n)), n)
    lim = int(max_ms * 1e-3 * sr)
    cand = np.concatenate([r[:lim], r[-lim:]])
    idx = int(np.argmax(cand))
    return idx if idx < lim else idx - 2 * lim


def retrigger_db(env, sr, step, nsteps, lag=0):
    """Per step: dB from the quiet point before the boundary to the peak after."""
    out = np.full(nsteps, np.nan)
    for i in range(nsteps):
        s = int(round(i * step * sr)) + lag
        pre_lo = s - int(0.25 * step * sr)
        if pre_lo < 0 or s + int(0.45 * step * sr) >= len(env):
            continue
        floor_ = float(env[pre_lo:s].min())
        peak = float(env[s:s + int(0.45 * step * sr)].max())
        out[i] = 20 * np.log10((peak + 1e-9) / (floor_ + 1e-9))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rebirth")
    ap.add_argument("ours")
    ap.add_argument("--bpm", type=float, required=True)
    ap.add_argument("--bars", type=int, default=0)
    ap.add_argument("--thresh", type=float, default=4.0,
                    help="dB of retrigger difference that counts as a disagreement")
    a = ap.parse_args()

    xa, sr, sida = load(a.rebirth)
    xb, sr2, sidb = load(a.ours)
    step = 60.0 / a.bpm / 4.0
    nst = a.bars * 16 if a.bars else int(min(len(xa), len(xb)) / sr / step)

    ea, eb = envelope(xa, sr), envelope(xb, sr2)
    lag = align(ea, eb, sr)
    print(f"{os.path.basename(a.rebirth)}  vs  {os.path.basename(a.ours)}")
    print(f"  {a.bpm:g} BPM, step {step*1000:.2f} ms, {nst} steps "
          f"({nst/16:.2f} bars).  alignment {lag/sr*1000:+.1f} ms")
    print(f"  side energy (L-R): ReBirth {sida:.0f} dB, ours {sidb:.0f} dB "
          f"(mono-summed for timing)")

    ra = retrigger_db(ea, sr, step, nst, 0)
    rb = retrigger_db(eb, sr2, step, nst, lag)
    d = rb - ra
    ok = np.isfinite(d)
    print(f"\n  retrigger strength, dB. High = the gate closed and reopened.")
    print(f"  mean ReBirth {np.nanmean(ra):5.1f}, ours {np.nanmean(rb):5.1f}\n")

    bad = [i for i in range(nst) if ok[i] and abs(d[i]) >= a.thresh]
    if not bad:
        print(f"  no step differs by {a.thresh:g} dB or more.")
        return
    print(f"  {len(bad)} of {int(ok.sum())} steps differ by >= {a.thresh:g} dB:")
    print(f"  {'bar':>4s} {'step':>5s} {'idx':>5s} | {'ReBirth':>8s} {'ours':>8s} "
          f"{'diff':>7s} | reading")
    for i in bad:
        tag = "OURS RETRIGGERS, ReBirth holds" if d[i] > 0 else \
              "ReBirth retriggers, ours holds"
        print(f"  {i//16+1:4d} {i%16+1:5d} {i:5d} | {ra[i]:8.1f} {rb[i]:8.1f} "
              f"{d[i]:+7.1f} | {tag}")
    extra = sum(1 for i in bad if d[i] > 0)
    print(f"\n  ours retriggers where ReBirth holds: {extra}")
    print(f"  ReBirth retriggers where ours holds:  {len(bad)-extra}")


if __name__ == "__main__":
    main()
