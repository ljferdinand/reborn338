#!/usr/bin/env python3
"""The 303 accent knob as a LAW, not a single point. Task 60.

Two statistics, because the manual is explicit that accent "does not only affect
volume, but also the shape and character of the sound" (p49): a level-independent
BRIGHTNESS ratio that reads the filter, and a LEVEL that reads the VCA. A model
that gets one and not the other is not right.

Controls run first: the duplicates, and the three cells with the accent flag OFF
which test whether the knob does anything on its own.
"""
import os, sys, wave
import numpy as np

WS = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "..", "..", ".."))
RB = os.path.join(WS, "renders", "accentdepth_rb")
OU = os.path.join(WS, "renders", "accentdepth_ours")
KNOBS = (0, 16, 32, 48, 64, 80, 96, 112, 127)
STEP = 0.125


def rd(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), sr


def stats(root, name, idx=1, dur=0.060):
    """(brightness dB, level dBFS) over the window after the note, 4 bars pooled."""
    p = os.path.join(root, name + ".wav")
    if not os.path.exists(p):
        return None, None
    x, sr = rd(p)
    bs, ls = [], []
    for bar in range(4):
        a = int((bar * 2.0 + idx * STEP) * sr)
        b = a + int(dur * sr)
        seg = x[a:b]
        if len(seg) < 256:
            continue
        n = 1 << int(np.ceil(np.log2(len(seg))))
        P = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2
        f = np.fft.rfftfreq(n, 1.0 / sr)
        hi = (f >= 1000) & (f <= 5000)
        lo = (f >= 100) & (f <= 400)
        bs.append(10 * np.log10((P[hi].sum() + 1e-20) / (P[lo].sum() + 1e-20)))
        ls.append(20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-20))
    return (float(np.mean(bs)), float(np.mean(ls))) if bs else (None, None)


def main():
    print("=" * 70)
    print("CONTROLS")
    print("=" * 70)
    print("\n1. DUPLICATES, this batch's own noise floor:")
    for base in ("d303_acc000_on_c064_r060", "d303_acc096_on_c064_r060",
                 "d303_acc064_on_c100_r030"):
        a = stats(RB, base); b = stats(RB, base + "_dup")
        if a[0] is None or b[0] is None:
            continue
        print(f"   {base:<30} bright {a[0]-b[0]:+7.3f}   level {a[1]-b[1]:+7.3f} dB")

    print("\n2. IS THE KNOB INERT WITH NO ACCENT FLAG? If these differ, the knob")
    print("   is not a per-step scaler and the law below is the wrong shape.")
    print(f"   {'knob':>6}{'RB bright':>12}{'RB level':>11}{'ours bright':>13}{'ours level':>12}")
    for k in (0, 64, 127):
        r = stats(RB, f"d303_acc{k:03d}_off_c064_r060")
        o = stats(OU, f"d303_acc{k:03d}_off_c064_r060")
        if r[0] is None:
            continue
        print(f"   {k:>6}{r[0]:>12.2f}{r[1]:>11.2f}{o[0]:>13.2f}{o[1]:>12.2f}")

    for cut, reso, ks in ((64, 60, KNOBS), (100, 30, (0, 32, 64, 96, 127))):
        print()
        print("=" * 70)
        print(f"THE LAW at cutoff {cut} resonance {reso}. Lift is against knob 0.")
        print("=" * 70)
        base_r = stats(RB, f"d303_acc000_on_c{cut:03d}_r{reso:03d}")
        base_o = stats(OU, f"d303_acc000_on_c{cut:03d}_r{reso:03d}")
        print(f"  {'knob':>6}{'RB bright':>11}{'ours':>9}{'err':>8}   "
              f"{'RB level':>10}{'ours':>9}{'err':>8}")
        for k in ks:
            r = stats(RB, f"d303_acc{k:03d}_on_c{cut:03d}_r{reso:03d}")
            o = stats(OU, f"d303_acc{k:03d}_on_c{cut:03d}_r{reso:03d}")
            if r[0] is None or o[0] is None:
                print(f"  {k:>6}   MISSING"); continue
            rb_b, ou_b = r[0] - base_r[0], o[0] - base_o[0]
            rb_l, ou_l = r[1] - base_r[1], o[1] - base_o[1]
            print(f"  {k:>6}{rb_b:>+11.2f}{ou_b:>+9.2f}{ou_b-rb_b:>+8.2f}   "
                  f"{rb_l:>+10.2f}{ou_l:>+9.2f}{ou_l-rb_l:>+8.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
