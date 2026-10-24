#!/usr/bin/env python3
"""Compressor surface analyzer (clean-room, supertask 6, 2026-09-16).

Reads the batch-2 lines (gen_comp_law2.py) and reports each swept law:

  ts_r127_tTTT : threshold sweep, fader 127, ratio 127
                 -> output dBFS = threshold(byte)  (output pins to threshold)
  rs_rRRR_t000 : ratio sweep, fader 127, threshold 0
                 -> GR(byte) = slope(byte) * (input - threshold)
  bt_r127_tTTT : below-threshold probe, fader 60, ratio 127
                 -> is a quiet signal boosted toward the threshold?

Same method as comp_curve.py: steady-state RMS over 2-6 s (phase-independent).
GR = level(on) - level(off_at_same_fader), dB. Reference faders: off_v127 (hot),
off_v060 (quiet).
"""
import argparse, glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comp_curve import level

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders")
    ap.add_argument("--win", nargs=2, type=float, default=[2.0, 6.0], metavar=("LO", "HI"))
    a = ap.parse_args(argv); lo, hi = a.win
    R = a.renders
    def lv(stem):
        f = os.path.join(R, stem + ".wav")
        return level(f, lo, hi)[0] if os.path.exists(f) else None

    off127, off060 = lv("off_v127"), lv("off_v060")
    print(f"=== comp surface (window {lo}-{hi}s, dBFS power) ===")
    print(f"  refs: off_v127={off127:.2f}  off_v060={off060:.2f} dBFS\n")

    # Threshold law: output pins to threshold at ratio 127.
    print("-- threshold sweep (ratio 127, fader 127, in={:.2f}) --".format(off127))
    print("   thrByte   outLvl(dBFS)   GR(dB)")
    for f in sorted(glob.glob(os.path.join(R, "ts_r127_t*.wav"))):
        t = int(re.search(r"_t(\d{3})", f).group(1))
        out = level(f, lo, hi)[0]
        print(f"   {t:5d}     {out:9.2f}     {out-off127:7.2f}")

    # Ratio law: GR at fixed hot input above threshold 0.
    print("\n-- ratio sweep (threshold 0, fader 127, in={:.2f}) --".format(off127))
    print("   ratByte   outLvl(dBFS)   GR(dB)")
    for f in sorted(glob.glob(os.path.join(R, "rs_r*_t000.wav"))):
        r = int(re.search(r"_r(\d{3})", f).group(1))
        out = level(f, lo, hi)[0]
        print(f"   {r:5d}     {out:9.2f}     {out-off127:7.2f}")

    # Below-threshold probe: quiet input, does it get boosted toward threshold?
    print("\n-- below-threshold probe (ratio 127, fader 60, in={:.2f}) --".format(off060))
    print("   thrByte   outLvl(dBFS)   GR(dB)")
    for f in sorted(glob.glob(os.path.join(R, "bt_r127_t*.wav"))):
        t = int(re.search(r"_t(\d{3})", f).group(1))
        out = level(f, lo, hi)[0]
        print(f"   {t:5d}     {out:9.2f}     {out-off060:7.2f}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
