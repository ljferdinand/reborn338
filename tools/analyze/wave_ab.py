#!/usr/bin/env python3
"""Saw vs square: separate the WAVEFORM from the ENVELOPE before fitting either.

The whole-file peak and rms in batchG_303.py's `wave` group mix the two. Crest
factor of a whole file is (envelope crest + waveform crest), so a decay that is
too fast reads as a waveform-shape error and vice versa. That is how "square is
0.7 dB above saw" got written down as the whole finding.

This measures both separately on a step-locked note:

  ENVELOPE   peak per 5 ms slice through one step, normalised to its own max.
             Identical between the saw and square fixtures by construction, so
             any saw/square difference here is a red flag, and the ReBirth/ours
             difference is the envelope error on its own.

  WAVEFORM   harmonic amplitudes at k * f0 in a window inside the note, plus the
             crest factor of that window. Envelope-normalised, so this is the
             oscillator and the filter and nothing else.

Usage:  python wave_ab.py <rebirth-dir> <ours-dir>
"""
import os
import sys
import wave as wavemod

import numpy as np

# Mono-summing is SAFE here and was checked, not assumed. The workspace rule is
# to measure stereo, because a pan law seven times too wide once hid behind
# mono-summed metrics. These fixtures are a single centre-panned 303 with the
# effect rack off: L minus R is exactly 0 LSB on both sides, ReBirth's and ours.
# Re-check that if this is ever pointed at a fixture with two voices or any pan.

TEMPO = 120.0
F0 = 110.0            # note A
CUTS = (32, 64, 96)
NHARM = 24


def load(path):
    with wavemod.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def step_frames(sr):
    return int(round(sr * 60.0 / TEMPO / 4.0))


def db(v):
    return -999.0 if v <= 0 else 20.0 * np.log10(v)


def envelope(x, sr, step, slice_ms=5.0, n_steps=8, first=1):
    """Mean per-slice peak across n_steps notes, in dB relative to the loudest."""
    w = int(slice_ms * 0.001 * sr)
    n_sl = step // w
    acc = np.zeros(n_sl)
    for s in range(first, first + n_steps):
        base = s * step
        for j in range(n_sl):
            seg = x[base + j * w: base + (j + 1) * w]
            if len(seg):
                acc[j] += np.abs(seg).max()
    acc /= n_steps
    return acc


def harmonics(x, sr, step, t0_ms=20.0, dur_ms=60.0, n_steps=8, first=1):
    """Harmonic amplitudes at k*F0 inside the note, averaged over n_steps.

    The window sits after the attack and well before the step boundary, and each
    note is normalised by its own rms first so that a decaying envelope does not
    weight the early notes more than the late ones.
    """
    a = int(t0_ms * 0.001 * sr)
    d = int(dur_ms * 0.001 * sr)
    N = 1 << int(np.ceil(np.log2(d)))
    win = np.hanning(d)
    acc = np.zeros(N // 2 + 1)
    crest = []
    for s in range(first, first + n_steps):
        seg = x[s * step + a: s * step + a + d]
        if len(seg) < d:
            break
        r = np.sqrt((seg ** 2).mean())
        if r <= 0:
            continue
        crest.append(db(np.abs(seg).max() / r))
        y = np.zeros(N)
        y[:d] = (seg / r) * win
        acc += np.abs(np.fft.rfft(y))
    f = np.fft.rfftfreq(N, 1.0 / sr)
    out = []
    for k in range(1, NHARM + 1):
        i = np.argmin(np.abs(f - k * F0))
        out.append(acc[max(0, i - 1):i + 2].max())
    return np.array(out), (float(np.mean(crest)) if crest else float("nan"))


def measure(d, wave_i, cut):
    p = os.path.join(d, f"g303_wave{wave_i}_cut{cut:03d}.wav")
    if not os.path.exists(p):
        return None
    x, sr = load(p)
    step = step_frames(sr)
    return envelope(x, sr, step), harmonics(x, sr, step), x, sr, step


def main(RB, US):
    print("=== ENVELOPE: per-5 ms peak through one step, dB below the note's max")
    print("    (the saw and square fixtures share an envelope, so the two rows")
    print("     per side are a self-check; the sides differ = envelope error)")
    print()
    hdr = "        " + "".join(f"{int(j*5):>6d}" for j in range(0, 25, 2))
    print(hdr + "   ms")
    for tag, d in (("RB  ", RB), ("ours", US)):
        for wave_i, wname in ((0, "saw"), (1, "sqr")):
            m = measure(d, wave_i, 64)
            if not m:
                continue
            e = m[0]
            e = e / e.max()
            print(f"{tag} {wname} " + "".join(f"{db(e[j]):6.1f}" for j in range(0, 25, 2)))
    print()

    print("=== WAVEFORM: harmonic amplitudes inside the note, dB rel. fundamental")
    print()
    for cut in CUTS:
        print(f"--- cutoff {cut} ---")
        print("            " + "".join(f"{k:>6d}" for k in range(1, 13)))
        rows = {}
        for tag, d in (("RB  ", RB), ("ours", US)):
            for wave_i, wname in ((0, "saw"), (1, "sqr")):
                m = measure(d, wave_i, cut)
                if not m:
                    continue
                h, crest = m[1]
                rows[(tag.strip(), wname)] = (h, crest)
                rel = h / h[0]
                print(f"{tag} {wname}   " + "".join(f"{db(v):6.1f}" for v in rel[:12]))
        print()
        print("            odd/even dB   crest dB   H2-H8 vs H1 dB   H1 abs (env-norm)")
        for tag in ("RB", "ours"):
            for wname in ("saw", "sqr"):
                if (tag, wname) not in rows:
                    continue
                h, crest = rows[(tag, wname)]
                odd = np.sqrt((h[0::2] ** 2).sum())
                even = np.sqrt((h[1::2] ** 2).sum())
                upper = np.sqrt((h[1:8] ** 2).sum())
                print(f"{tag:>4s} {wname}   {db(odd/even):11.1f}{crest:11.2f}"
                      f"{db(upper/h[0]):17.1f}{db(h[0]):19.1f}")
        print()

    print("=== SQUARE minus SAW, the sign question ===")
    print("            (a pure gain difference moves peak and rms together;")
    print("             anything else is waveform shape)")
    print()
    print(f"{'cut':>5s} {'side':>5s} {'H1 dB':>8s} {'crest dB':>10s} "
          f"{'note-peak dB':>13s} {'note-rms dB':>12s}")
    for cut in CUTS:
        for tag, d in (("RB", RB), ("ours", US)):
            vals = {}
            for wave_i, wname in ((0, "saw"), (1, "sqr")):
                m = measure(d, wave_i, cut)
                if not m:
                    continue
                h, crest = m[1]
                x, sr, step = m[2], m[3], m[4]
                seg = x[step: 9 * step]
                vals[wname] = (db(h[0]), crest, db(np.abs(seg).max()),
                               db(np.sqrt((seg ** 2).mean())))
            if len(vals) == 2:
                s, q = vals["saw"], vals["sqr"]
                print(f"{cut:5d} {tag:>5s} {q[0]-s[0]:8.2f} {q[1]-s[1]:10.2f} "
                      f"{q[2]-s[2]:13.2f} {q[3]-s[3]:12.2f}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
