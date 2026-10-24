#!/usr/bin/env python3
"""How OFTEN and how FAR does the filter open? The thing Luke hears as "wah".

WHY THIS EXISTS. On 2026-08-02, after the env mod swing landed, Luke described
ReBirth's filter as "modulating on every beat" where ours was "largely audible
only around the 4th beat, roughly step 12". No instrument in this project could
see that. regress_ab.py averages a whole file into one spectrum and is blind to
it by construction; env_traj.py tracks the corner beautifully but only on a held
note, and this is about a musical pattern.

WHAT IT MEASURES. Brightness through the file, then every upward GESTURE: a local
maximum that rises at least `--thresh` dB above the preceding local minimum. It
reports how many there are, how deep they are, and their total. Count and depth
are separate defects with separate causes, and reporting only one of them hides
half the problem: on Clodhopper's 303-2 ours had 75 percent of ReBirth's gesture
count AND 73 percent of its depth, which multiply to half the movement.

    ReBirth              64 gestures   median 13.5 dB   total 1007
    ours, 2026-08-02     48            median  9.9      total  504

THE MEASUREMENT IS A BAND RATIO AND THAT MATTERS. Brightness here is 800 Hz to
5 kHz over 80 to 400 Hz. If the filter corner sits ABOVE the upper band, real
movement stops registering and this tool will under-report it. That is not
hypothetical: it is why the accent knob law could not be fitted on brightness in
the same session, and it is why this tool's numbers were explicitly ruled out as
a way to choose between two candidate values of CUT_SCALE. Use it to measure a
gap against ReBirth at a FIXED filter setting. Do NOT use it to compare two
settings that move the corner, because it will reward whichever one drags the
corner into its band.

THE THRESHOLD IS A PARAMETER FOR A REASON. At 3 dB it counts articulation; raise
it and it counts only the pronounced ones. Report which you used. A gesture count
without its threshold is meaningless.

MONO for the spectra, stated because this project has been bitten by silent
mono-summing. Safe here: a soloed 303 is one source.

USAGE
    python gesture_rate.py --ref REBIRTH.wav --ours A.wav B.wav --thresh 3
"""
import argparse
import os
import wave

import numpy as np


def read_mono(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def brightness(x, sr, frame=2048, hop=256):
    """A short frame on purpose: 46 ms resolves a per-step gesture, where the
    4096 used for corner tracking would smear two sixteenths together."""
    win = np.hanning(frame)
    f = np.fft.rfftfreq(frame, 1.0 / sr)
    hi, lo = (f >= 800) & (f <= 5000), (f >= 80) & (f <= 400)
    out = []
    for a in range(0, len(x) - frame, hop):
        P = np.abs(np.fft.rfft(x[a:a + frame] * win)) ** 2
        out.append(10 * np.log10((P[hi].sum() + 1e-20) / (P[lo].sum() + 1e-20)))
    return np.array(out), sr / hop


def gestures(b, rate, thresh):
    """Local maxima rising at least `thresh` above the preceding local minimum.
    Returns (rise dB, time s) per gesture. The running minimum resets at each
    accepted peak, so a slow drift upward is one gesture and not many."""
    out, last_min = [], b[0]
    for i in range(1, len(b) - 1):
        if b[i] < last_min:
            last_min = b[i]
        if b[i] >= b[i - 1] and b[i] > b[i + 1] and b[i] - last_min >= thresh:
            out.append((b[i] - last_min, i / rate))
            last_min = b[i]
    return out


def report(label, path, thresh, show_times):
    if not os.path.exists(path):
        print(f"  {label:<34}  MISSING {path}")
        return None
    x, sr = read_mono(path)
    b, rate = brightness(x, sr)
    g = gestures(b, rate, thresh)
    if not g:
        print(f"  {label:<34}   no gestures above {thresh} dB")
        return None
    d = np.array([r for r, _ in g])
    print(f"  {label:<34}{len(g):>6}{np.median(d):>9.1f}{d.mean():>8.1f}{d.sum():>9.0f}")
    if show_times:
        t = " ".join(f"{s:.2f}" for _, s in g[:16])
        print(f"  {'':<34}  first at: {t}")
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, help="the ReBirth export")
    ap.add_argument("--ours", nargs="+", required=True)
    ap.add_argument("--labels", nargs="*", default=None)
    ap.add_argument("--thresh", type=float, default=3.0,
                    help="minimum rise in dB to count as a gesture (default 3)")
    ap.add_argument("--times", action="store_true",
                    help="also print when the first sixteen gestures happen, "
                         "which is how you see WHICH ones are missing")
    a = ap.parse_args()

    print(f"FILTER GESTURES, rise of {a.thresh} dB or more. Count and depth are")
    print("separate defects; a shortfall in both multiplies.\n")
    print(f"  {'':<34}{'count':>6}{'median':>9}{'mean':>8}{'total':>9}")
    r = report("ReBirth", a.ref, a.thresh, a.times)
    for i, p in enumerate(a.ours):
        lbl = (a.labels[i] if a.labels and i < len(a.labels)
               else os.path.basename(p)[:-4])
        d = report(lbl, p, a.thresh, a.times)
        if r is not None and d is not None:
            print(f"  {'':<34}  {100*len(d)/len(r):.0f}% of ReBirth's count, "
                  f"{100*np.median(d)/np.median(r):.0f}% of its depth, "
                  f"{100*d.sum()/r.sum():.0f}% of its total movement")
    print("\nBand ratio, 800 Hz to 5 kHz over 80 to 400. A corner sitting above")
    print("the upper band under-reports: do not use this to compare two settings")
    print("that MOVE the corner, only to measure a gap at a fixed setting.")


if __name__ == "__main__":
    main()
