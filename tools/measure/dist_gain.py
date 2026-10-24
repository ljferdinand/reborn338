#!/usr/bin/env python3
"""Distortion output-LEVEL surface for the gain-law fit (clean-room, supertask 7,
item 3, 2026-09-18).

The distortion's output trim is still the constant kOutTrim (0.6105), a
centering value, not a fitted law (BACKLOG 2). This measures the ABSOLUTE output
level ReBirth produces across the Amount x Shape grid so the walled implementer
can fit outGain(Amount, Shape).

Method, per cell (b1 = Shape byte, b2 = Amount byte):
  L_rb   = broadband RMS of ReBirth's distorted render, steady 2-6 s window.
  L_ours = broadband RMS of d_off (ReBirth's OWN clean drone) fed through OUR
           distortion via the dist_wav harness at amount=b2/127, shape=b1/127.
           dist_wav already applies the current outGain = kOutTrim, so L_ours is
           our CURRENT output level.
Because both sides distort the identical input (ReBirth's clean drone), the ratio
L_rb / L_ours is purely the output-gain discrepancy, free of any source confound.
The gain law the implementer must fit is therefore:

  outGain_target(cell) = kOutTrim * (L_rb / L_ours)

Reports L_rb, L_ours, their dB gap (what the current constant gets wrong), and
outGain_target, as a 7x7 surface plus CSV for the fit. No binary-analysis value.

Usage: python dist_gain.py [renders_dir] [--dist-wav path] [--csv out.csv]

renders_dir holds ReBirth's exports of the dist-grid2 fixtures, made from your
own copy of ReBirth; they are not in this repository. It defaults to
`renders/dist-grid2-2026-09-17` under REBORN_WORKSPACE (default: the directory
that contains this checkout, the same rule as tools/rbsgen/rbsgen.py).
"""
import argparse, os, subprocess, sys, tempfile, wave
import numpy as np

WORKSPACE = os.environ.get("REBORN_WORKSPACE") or os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
KOUTTRIM = 0.6105
BYTES = (0, 21, 43, 64, 85, 106, 127)   # the dense-grid points, byte1=Shape byte2=Amount

def load_mono(p):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    return x.reshape(-1, ch).mean(1), sr

def rms_steady(p, lo=2.0, hi=6.0):
    x, sr = load_mono(p)
    seg = x[int(lo * sr):int(hi * sr)]
    if seg.size == 0:
        seg = x
    return float(np.sqrt(np.mean(seg * seg)) + 1e-30)

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("renders", nargs="?",
                    default=os.path.join(WORKSPACE, "renders", "dist-grid2-2026-09-17"))
    # Relative to the CURRENT checkout so it measures THIS build, not another
    # worktree's. Run from the checkout root after building.
    ap.add_argument("--dist-wav", default="build/Release/dist_wav.exe")
    ap.add_argument("--csv", default="")
    a = ap.parse_args(argv)

    doff = os.path.join(a.renders, "d_off.wav")
    if not os.path.exists(doff):
        sys.exit(f"no {doff}: pass the directory of ReBirth's dist-grid2 exports, "
                 "or set REBORN_WORKSPACE")
    l_clean = rms_steady(doff)
    tmpdir = tempfile.mkdtemp(prefix="distgain_")
    rows = []
    for b1 in BYTES:            # Shape
        for b2 in BYTES:        # Amount
            rb = os.path.join(a.renders, f"h_b1{b1:03d}_b2{b2:03d}.wav")
            if not os.path.exists(rb):
                continue
            l_rb = rms_steady(rb)
            out = os.path.join(tmpdir, f"o_{b1}_{b2}.wav")
            amt = b2 / 127.0
            shp = b1 / 127.0
            subprocess.run([a.dist_wav, doff, out, f"{amt:.5f}", f"{shp:.5f}"],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            l_ours = rms_steady(out)
            ratio = l_rb / l_ours
            outgain = KOUTTRIM * ratio
            rows.append((b1, b2, amt, shp, l_rb, l_ours, 20 * np.log10(ratio), outgain))

    # Clean level for reference (ReBirth's own d_off through nothing).
    print(f"clean d_off RMS = {l_clean:.5f}\n")
    print(f"{'Shp':>4} {'Amt':>4} {'L_rb':>8} {'L_ours':>8} {'gap dB':>7} {'outGain*':>9}")
    print("  (gap dB = how far the current constant kOutTrim is off; +ve = we are too quiet)")
    for b1, b2, amt, shp, l_rb, l_ours, gapdb, outgain in rows:
        print(f"{b1:>4} {b2:>4} {l_rb:8.5f} {l_ours:8.5f} {gapdb:7.2f} {outgain:9.4f}")

    # Amount-marginal (median across Shape): the primary trend a low-parameter
    # law targets, like the drive-law fit did.
    print("\nAmount-marginal (median outGain* across Shape):")
    for b2 in BYTES:
        og = [r[7] for r in rows if r[1] == b2]
        if og:
            print(f"  Amount {b2:3d} (amt {b2/127.0:.3f}): median outGain* = {np.median(og):.4f}"
                  f"  [min {min(og):.4f}, max {max(og):.4f}]")
    print("\nShape-marginal (median outGain* across Amount):")
    for b1 in BYTES:
        og = [r[7] for r in rows if r[0] == b1]
        if og:
            print(f"  Shape {b1:3d} (shp {b1/127.0:.3f}): median outGain* = {np.median(og):.4f}"
                  f"  [min {min(og):.4f}, max {max(og):.4f}]")

    if a.csv:
        import csv
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["shape_byte", "amount_byte", "amount01", "shape01",
                        "L_rb", "L_ours_current", "gap_dB", "outGain_target"])
            w.writerows(rows)
        print(f"\nCSV -> {a.csv}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
