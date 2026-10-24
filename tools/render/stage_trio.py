#!/usr/bin/env python3
"""Stage a level-matched listening trio: ReBirth, ours before, ours after.

WHY IT IS A TOOL. This is the fourth ad hoc ear-check builder in the project and
the level match is the part that keeps going wrong. Leaving level unmatched
confounded the drive A/B badly enough that the result had to be set aside;
matching on the wrong band cancels the very thing being judged.

THE MATCH BAND IS THE WHOLE DESIGN, so it is a required argument. Pick a band the
change CANNOT reach:

  a filter-corner change   match LOW, under the corner's whole range. The
                           fundamental is in the passband wherever the corner
                           goes, so it normalises the source without touching
                           the sweep
  a low-end change         match HIGH, above it (300 Hz to 5 kHz was used for
                           the highpass and lowend checks)
  a change with no such
  band, e.g. an accent     `--band 0 0` for whole-file rms. It equalises
                           loudness so the ear compares the pattern rather than
                           which file is louder. Approximate by construction

AND THE OPERATING POINT IS THE OTHER HALF. On 2026-08-02 an envelope check was
staged on a patch whose decay gave a 1.63 s time constant against a 54 ms gate,
so the envelope moved three percent inside a note. Luke correctly reported no
difference. Check that the effect can EXPRESS itself in the material, not just
that the parameter under test is set.

USAGE
    python stage_trio.py OUTDIR --ref RBDIR --a OURSDIR --b OURSDIR2 \
        --band 40 120 --cells stem1 stem2 ...

Writes OUTDIR/<stem>__1rebirth.wav, __2before.wav, __3after.wav so a directory
sort plays them in order. Prints the gain applied to each, because a match that
needed 20 dB is telling you something about the render, not about the change.
"""
import argparse
import os
import wave

import numpy as np


def rd(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def wr(p, x, sr):
    y = np.clip(x, -1.0, 1.0)
    with wave.open(p, "wb") as w:
        w.setnchannels(y.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((y * 32767.0).astype("<i2").tobytes())


def band_rms(x, sr, lo, hi):
    """Stereo, never mono-summed: the two channels are measured and pooled in
    power, so a pan cannot cancel the thing being levelled against."""
    if lo <= 0 and hi <= 0:
        return float(np.sqrt((x ** 2).mean()))
    n = 1 << int(np.ceil(np.log2(len(x))))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    sel = (f >= lo) & (f <= hi)
    acc = 0.0
    for ch in range(x.shape[1]):
        P = np.abs(np.fft.rfft(x[:, ch], n)) ** 2
        acc += P[sel].sum()
    return float(np.sqrt(acc / (len(x) * x.shape[1])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir")
    ap.add_argument("--ref", required=True, help="the ReBirth export directory")
    ap.add_argument("--a", required=True, help="ours BEFORE")
    ap.add_argument("--b", required=True, help="ours AFTER")
    ap.add_argument("--band", nargs=2, type=float, required=True,
                    metavar=("LO", "HI"),
                    help="match band in Hz; 0 0 means whole-file rms")
    ap.add_argument("--cells", nargs="+", required=True)
    ap.add_argument("--labels", nargs=2, default=["before", "after"],
                    metavar=("A", "B"),
                    help="names for the two comparison tracks. The default reads "
                         "as a before/after, which is wrong and misleading when "
                         "the trio is two CANDIDATES rather than a change.")
    a = ap.parse_args()
    lo, hi = a.band
    os.makedirs(a.outdir, exist_ok=True)

    print(f"match band {lo:.0f} to {hi:.0f} Hz" if hi > 0 else "whole-file rms")
    print(f"\n{'cell':<34}{'track':<10}{'gain dB':>9}{'peak':>8}")
    for cell in a.cells:
        rp = os.path.join(a.ref, cell + ".wav")
        if not os.path.exists(rp):
            print(f"{cell:<34}  MISSING reference, skipped")
            continue
        ref, sr = rd(rp)
        r0 = band_rms(ref, sr, lo, hi)
        wr(os.path.join(a.outdir, f"{cell}__1rebirth.wav"), ref, sr)
        print(f"{cell:<34}{'rebirth':<10}{0.0:>9.2f}{np.abs(ref).max():>8.3f}")
        for src, tag in ((a.a, "2" + a.labels[0]), (a.b, "3" + a.labels[1])):
            p = os.path.join(src, cell + ".wav")
            if not os.path.exists(p):
                print(f"{'':<34}{tag:<10}  MISSING")
                continue
            x, sr2 = rd(p)
            g = r0 / max(band_rms(x, sr2, lo, hi), 1e-12)
            y = x * g
            wr(os.path.join(a.outdir, f"{cell}__{tag}.wav"), y, sr2)
            print(f"{'':<34}{tag:<10}{20*np.log10(g):>9.2f}{np.abs(y).max():>8.3f}")
    print(f"\nwrote to {a.outdir}")


if __name__ == "__main__":
    main()
