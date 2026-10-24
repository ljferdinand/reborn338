#!/usr/bin/env python3
"""The accent envelope as a SHAPE IN TIME, not a number in a window.

Luke, 2026-08-02, on the accent-rate ear check: "in the rebirth versions there's
a roundness to each time the accent hits, that wah / filtered sound. it's almost
as if on the rebirth they're a shape that's CLOSING, but on ours they're a shape
that's OPENING."

Every accent measurement in this project so far reduced the event to one number
over one window, so a filter that reaches the right place by the wrong route
scores identically to one that is correct. This tracks brightness through the
note instead.

It isolates the ACCENT by differencing: the accented render minus its matched
no-accent control, on both sides, so the note's own filter envelope cancels and
what is left is the accent's own contribution over time.
"""
import os, sys, wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
RB = os.path.join(WS, "renders", "accentdepth_rb")
OU = os.path.join(WS, "renders", "accentdepth_ours")
FRAME, HOP = 1024, 128            # 23.2 ms window, 2.9 ms hop at 44.1 kHz
STEP = 0.125                      # 120 BPM sixteenths; the note is on step 2


def rd(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def trace(root, name, dur=0.60):
    """Brightness every HOP samples from the note onset, 4 bars averaged."""
    p = os.path.join(root, name + ".wav")
    if not os.path.exists(p):
        return None, None
    x, sr = rd(p)
    win = np.hanning(FRAME)
    f = np.fft.rfftfreq(FRAME, 1.0 / sr)
    hi = (f >= 1000) & (f <= 5000)
    lo = (f >= 100) & (f <= 400)
    nsteps = int(dur * sr / HOP)
    acc = np.zeros(nsteps)
    for bar in range(4):
        base = int((bar * 2.0 + STEP) * sr)
        for i in range(nsteps):
            a = base + i * HOP
            seg = x[a:a + FRAME]
            if len(seg) < FRAME:
                break
            P = np.abs(np.fft.rfft(seg * win)) ** 2
            acc[i] += 10 * np.log10((P[hi].sum() + 1e-20) / (P[lo].sum() + 1e-20))
    return acc / 4.0, sr / HOP


def main():
    knob = sys.argv[1] if len(sys.argv) > 1 else "127"
    cut, reso = "064", "060"
    print("THE ACCENT'S OWN CONTRIBUTION over time: accented minus its matched")
    print("no-accent control, so the note's own filter envelope cancels.")
    print(f"Accent knob {knob}, cutoff {cut}, resonance {reso}. dB.\n")

    out = {}
    for root, lbl in ((RB, "ReBirth"), (OU, "ours")):
        a, fs = trace(root, f"d303_acc{knob}_on_c{cut}_r{reso}")
        b, _ = trace(root, f"d303_acc000_off_c{cut}_r{reso}")
        if a is None or b is None:
            print(f"  {lbl}: MISSING"); return 1
        out[lbl] = a - b
    fs = 44100.0 / HOP

    print(f"  {'ms':>6}{'ReBirth':>10}{'ours':>9}    shape")
    n = len(out["ReBirth"])
    for i in range(0, n, 7):                       # every ~20 ms
        t = i / fs * 1000.0
        if t > 520: break
        r, o = out["ReBirth"][i], out["ours"][i]
        bar = "#" * max(0, int(round(r / 1.2)))
        print(f"  {t:>6.0f}{r:>10.2f}{o:>9.2f}    {bar}")

    print()
    for lbl in ("ReBirth", "ours"):
        v = out[lbl]
        pk = int(np.argmax(v))
        t_pk = pk / fs * 1000.0
        # where has it fallen to half its peak?
        half = np.where(v[pk:] < v[pk] / 2.0)[0]
        t_half = (pk + half[0]) / fs * 1000.0 if len(half) else float("nan")
        print(f"  {lbl:<8} peak {v[pk]:+6.2f} dB at {t_pk:6.1f} ms, "
              f"half-peak at {t_half:6.1f} ms, "
              f"value at 500 ms {v[min(n-1,int(0.5*fs))]:+6.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
