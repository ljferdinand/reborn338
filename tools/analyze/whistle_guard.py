#!/usr/bin/env python3
"""Does the 303 filter RING or WHISTLE? A guard for raising resonance feedback.

A resonant ladder near k = 4 self-oscillates: it produces a sustained tone that
ignores the note envelope. That is audibly wrong and it is why an HF Q-loss
damper was added to this filter in the first place. Any change that raises
feedback has to be checked against it, and peak prominence cannot answer the
question because a bright ring and a whistle both raise the peak.

The discriminant is simple and needs no reference: the fixtures play ONE note
then rest, so a filter that rings decays toward silence while one that
oscillates holds level. This reports the decay from the note to the tail, and
whether the tail is still falling.

    python whistle_guard.py <dir> [more dirs...]

Run it on ReBirth's exports too. ReBirth is the target, so if IT sustains on a
patch, sustaining there is correct behaviour rather than a bug.
"""
import glob
import os
import sys
import wave

import numpy as np

FIXTURES = ("g303_w_max", "g303_w_hi_sweep", "g303_w_mid")

# The single-note tail is a WEAK test on this engine, because the VCA closes to
# exact digital zero between notes and would mask a filter that is oscillating
# happily underneath it. The failure actually seen in this project (task #39,
# "reso 127 = no note onsets") is that repeated notes stop articulating and blur
# into one tone. So the primary metric is articulation depth on a repeated-note
# fixture: how far the level dips BETWEEN consecutive notes.
ARTIC = "h303_cut127_reso127"      # 16 repeated notes, max cutoff and resonance


def articulation(path, bpm=120.0):
    """Median dip between consecutive 16th-note onsets, in dB.

    A plucked note shows a deep dip each step. A self-oscillating filter fills
    the gaps and the dip collapses.
    """
    x, sr = load(path)
    step = sr * 60.0 / bpm / 4.0
    W = max(8, int(0.004 * sr))
    n = (len(x) // W) * W
    e = np.sqrt((x[:n].reshape(-1, W) ** 2).mean(axis=1))
    per = step / W
    dips = []
    for k in range(1, 15):                     # skip the first and last step
        a, b = int(k * per), int((k + 1) * per)
        if b > len(e):
            break
        seg = e[a:b]
        if seg.size < 4 or seg.max() <= 0:
            continue
        dips.append(db(seg.min()) - db(seg.max()))
    return float(np.median(dips)) if dips else float("nan")


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def db(v):
    return -999.0 if v <= 0 else 20 * np.log10(v)


def verdict(path):
    x, sr = load(path)
    W = int(0.050 * sr)                       # 50 ms frames
    n = (len(x) // W) * W
    e = np.sqrt((x[:n].reshape(-1, W) ** 2).mean(axis=1))
    if not e.size:
        return None
    # The fixtures are TWO bars at 120 BPM, so a bar is 2.0 s and the single
    # note repeats at 2.0 s. Windowing by a fraction of the file caught that
    # second note and reported it as a rising tail, so use explicit times: the
    # note is at 0, and the tail is late in the FIRST bar only.
    f = 1.0 / 0.050                            # frames per second
    peak = e[:int(0.40 * f)].max()
    tail = e[int(1.20 * f):int(1.90 * f)]
    if tail.size < 4 or peak <= 0:
        return None
    decay = db(tail.mean()) - db(peak)
    # is the tail still falling, or has it settled onto a plateau?
    half = tail.size // 2
    slope = db(tail[half:].mean()) - db(tail[:half].mean())
    return peak, decay, slope


def main():
    dirs = sys.argv[1:]
    if not dirs:
        print(__doc__)
        return 2

    print("PRIMARY: articulation on 16 repeated notes at max cutoff+resonance.")
    print("A deep dip each step means the notes still pluck; a shallow one means")
    print("the filter is filling the gaps, i.e. oscillating.")
    print()
    print(f"{'source':24s} {'median dip':>12s}  verdict")
    ref = {}
    for d in dirs:
        p = os.path.join(d.replace("batchW", "batchH"), ARTIC + ".wav")
        if not os.path.exists(p):
            continue
        a = articulation(p)
        ref[d] = a
        v = ("articulates" if a < -18.0 else
             "SHALLOW, suspect oscillation" if a < -8.0 else
             "WHISTLE (gaps filled)")
        print(f"{os.path.basename(d):24s} {a:11.1f}dB  {v}")
    print()

    print("SECONDARY: single-note tail. Weak on this engine (the VCA closes to")
    print("exact zero and can mask an oscillating filter), kept as a cross-check.")
    print()
    print(f"{'fixture':16s} {'source':22s} {'peak':>8s} {'tail vs peak':>13s} "
          f"{'tail slope':>11s}  verdict")
    for stem in FIXTURES:
        for d in dirs:
            p = os.path.join(d, stem + ".wav")
            if not os.path.exists(p):
                continue
            r = verdict(p)
            if r is None:
                continue
            peak, decay, slope = r
            # A ring keeps falling and ends far below the note. A whistle holds
            # level: little decay AND a flat tail.
            if decay > -18.0 and slope > -3.0:
                v = "WHISTLE (sustains)"
            elif decay > -30.0 and slope > -1.5:
                v = "suspicious plateau"
            else:
                v = "ring (decays)"
            print(f"{stem:16s} {os.path.basename(d):22s} {db(peak):8.1f} "
                  f"{decay:12.1f}dB {slope:10.1f}dB  {v}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
