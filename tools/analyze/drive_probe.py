#!/usr/bin/env python3
"""Does ReBirth's 303 have a waveshaper at all, and can ours give one up?

The evidence that it does not, from docs/research/FINDINGS_AUDIT.md: dividing the square
harmonic series by the saw's cancels every LINEAR stage the two share, leaving

    R(k) = |sin(k.pi.d)| / sin(pi.d)

which depends only on the duty cycle. ReBirth fits that pure pulse model to
0.03 dB rms over 12 harmonics at three cutoffs, with the SAME duty at every
cutoff. A waveshaper anywhere in the path would break the cancellation. Ours
misses by 2.3 dB and drifts with cutoff, and the residual is the asymmetric
drive stage `satAsym()`.

Why it was not simply turned down: `satAsym` was fitted against the detroit_303
reference on a harmonic-ladder metric, and the whole batch H filter fit was made
with the current drive in place, so moving it invalidates the filter constants
together. docs/research/RUNBOOK.md item 2 asks for a JOINT refit rather than one after
the other.

This sweeps the drive and reports BOTH instruments at each setting:

  linearity   wave_fit's R(k) residual in dB, the direct test. ReBirth 0.03.
  filter      curve_fit's transfer-function rms against ReBirth over batch H.

A drive that fixes linearity while wrecking the filter, or the reverse, is the
signature of a parameter absorbing someone else's error. Both have to move the
right way before the shaper can be called wrong.

Usage:  python drive_probe.py [drive ...]
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)

FIXG = os.path.join(WS, "fixtures-2026-07-27", "batchG")
FIXH = os.path.join(WS, "fixtures-2026-07-27", "batchH")
RENDER = os.path.join(REPO, "tools", "render", "render_batch.py")

# The filter constants fitted 2026-07-30 on the curve metric. The drive is swept
# ON TOP of these, so what is measured is whether the shaper still earns its
# place once the filter is right, rather than against the old filter it was
# entangled with.
FILTER_FIT = {
    "REBORN_CUT_SCALE": "1.05", "REBORN_CUT_SLOPE": "2.8",
    "REBORN_RESO_OCT": "0.325", "REBORN_RESO_OCT_CUT": "0.425",
    "REBORN_K_B": "3.4", "REBORN_HFQ_ON": "4000",
}


def render(fixdir, outdir, env_extra, filt=None):
    env = dict(os.environ)
    env.update(env_extra)
    cmd = [sys.executable, RENDER, fixdir, outdir, "--seconds", "12", "--jobs", "20"]
    if filt:
        cmd += ["--filter", filt]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, cwd=REPO)
    return r.returncode == 0


def wave_residual(d):
    """R(k) residual in dB from wave_fit, the linearity test."""
    r = subprocess.run([sys.executable, os.path.join(HERE, "wave_fit.py"), d],
                       capture_output=True, text=True, cwd=REPO)
    duty = res = None
    for line in r.stdout.splitlines():
        if "best-fit pulse duty" in line:
            parts = line.split()
            try:
                duty = float(parts[parts.index("duty") + 1])
                res = float(parts[parts.index("residual") + 1])
            except (ValueError, IndexError):
                pass
    return duty, res


def filter_rms(d):
    import curve_fit as cf
    A = cf.load_dir(cf.RB)
    B = cf.load_dir(d)
    c = cf.compare(A, B, list(A.keys()))
    return c["rms"] if c else float("nan")


def main():
    drives = [float(a) for a in sys.argv[1:]] or [1.0, 0.75, 0.5, 0.35, 0.25, 0.15]
    print("ReBirth reference: linearity residual 0.03 dB, duty 0.52286\n")
    print(f"{'drive':>7s} {'duty':>9s} {'linearity dB':>13s} {'filter dB rms':>14s}")
    print(f"{'-'*7} {'-'*9} {'-'*13} {'-'*14}")
    for dv in drives:
        env = dict(FILTER_FIT)
        env["REBORN_DRIVE"] = str(dv)
        tag = f"drv{str(dv).replace('.', 'p')}"
        gd = os.path.join(WS, "renders", "drive_" + tag + "_g")
        hd = os.path.join(WS, "renders", "drive_" + tag + "_h")
        if not render(FIXG, gd, env, filt="wave") or not render(FIXH, hd, env):
            print(f"{dv:7.2f}  RENDER FAILED")
            continue
        duty, res = wave_residual(gd)
        frms = filter_rms(hd)
        ds = f"{duty:9.5f}" if duty is not None else f"{'-':>9s}"
        rs = f"{res:13.2f}" if res is not None else f"{'-':>13s}"
        print(f"{dv:7.2f} {ds} {rs} {frms:14.3f}", flush=True)
    print("\nBoth columns must improve together for the shaper to be the defect.")


if __name__ == "__main__":
    main()
