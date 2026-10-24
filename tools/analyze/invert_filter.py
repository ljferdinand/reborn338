#!/usr/bin/env python3
"""Invert ReBirth's filter: recover its CORNER and its FEEDBACK, cell by cell.

Every previous round fitted our constants to ReBirth's spectrum and read the
residual as "the resonance span is too small". That framing hides the shape of
the error. This asks the direct question instead: at each batch H cell, what
corner frequency and what feedback would ReBirth need if it were OUR filter?

Method. Our own engine is the forward model, which is better than an analytic
transfer function because it carries the real source spectrum, the oversampling
and the saturators. Take ONE batch H fixture as the test signal, then sweep the
corner and the feedback independently through environment overrides:

    REBORN_CUT_SCALE  multiplies the base cutoff  -> sets fc
    REBORN_K_A        a constant added to k, with K_B and K_CUT zeroed so the
                      fixture's own resonance knob no longer moves it

That builds F(fc, k) -> (85% rolloff, peak prominence) on a grid. Then for each
ReBirth cell, find the (fc, k) whose (rolloff, prominence) matches both, and
report how ReBirth's implied corner moves as its resonance knob travels.

The prediction being tested: ReBirth's corner RISES with resonance. Ours falls,
by the diode-detune term fc *= (1 - 0.012*kDet*kDet). The evidence for it is
that at cutoff 127 resonance 127 our resonant peak is MORE prominent than
ReBirth's (-3.5 dB against -7.2) while our spectrum is DARKER (2859 Hz against
3903). A taller peak at a lower frequency cannot be a Q error.

Usage:  python invert_filter.py [cutoff ...]     default 127 64
"""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from batchH_filter import psd, rolloff, prominence, RESO      # noqa: E402

EXE = os.path.join(REPO, "build", "Release", "reborn_render.exe")
FIX = os.path.join(WS, "fixtures-2026-07-27", "batchH")
RBDIR = os.path.join(WS, "renders", "batchH_rb")
SCRATCH = os.path.join(WS, "renders", "invert")

# kDet = min(2.45, 2.05 + reso); at the reso-0 fixture that is 2.05, so the
# detune factor is fixed at (1 - 0.012*2.05^2) and fc is exactly proportional
# to CUT_SCALE. Using one fixture keeps it that way.
KDET0 = 2.05
DETUNE = 1.0 - 0.012 * KDET0 * KDET0
# MUST match the base corner in src/devices/bass_synth.cpp. It went stale once:
# this said 582.0 for the whole life of the 303-corner-rise branch, which moved
# the engine to 407.0, so every "implied fc" printed below was 1.430x too high.
# Ratios cancelled it and absolute values did not, which is exactly the failure
# that had already been recorded once before, against fit_trim.py.
BASE = 407.0
CUT_SLOPE = 2.5

# READ THIS BEFORE TRUSTING ANY NUMBER FROM THIS SCRIPT.
#
# Audited 2026-07-30 and found unable to support the conclusion it was used for.
# Three defects, all measured, see docs/research/FINDINGS_AUDIT.md:
#   1. the FC_REL grid clips at cutoff 127 (needs 1.727x, tops out at 1.70x) and
#      the KS grid pins at 3.92 for reso 110 and 127 there;
#   2. it is not identifiable: single cells are fitted equally well by up to
#      seven corners spanning 0.90 octaves, and 6 of 24 cells have no fit inside
#      tolerance at all;
#   3. the corner rise it reports is corner(reso127)/corner(reso0), and at
#      reso 0 the filter has NO resonant peak, so the baseline is inferred where
#      the corner is not observable.
# Use tools/analyze/harm_response.py instead, which reads the peak off the
# harmonic-domain transfer function directly and excludes cells with no peak.

# The corner grid is ADAPTIVE, spanning 0.25x to 1.7x our own corner at that
# cutoff knob. A fixed grid pinned every cutoff-64 cell to its lower edge on the
# first run, which reports "the corner does not move" for the uninformative
# reason that the search could not go lower.
FC_REL = (0.25, 0.32, 0.40, 0.50, 0.62, 0.75, 0.90, 1.05, 1.20, 1.40, 1.70)
KS = (0.0, 0.5, 1.0, 1.5, 2.0, 2.4, 2.8, 3.1, 3.4, 3.6, 3.8, 3.92)


def base_cut(cutoff_knob):
    return BASE * 2.0 ** (CUT_SLOPE * cutoff_knob / 127.0)


def render(fixture, out, env_extra):
    env = dict(os.environ)
    env.update(env_extra)
    r = subprocess.run([EXE, "--song", fixture, "--out", out, "--seconds", "10",
                        "--solo", "bass1", "--no-fx"],
                       env=env, capture_output=True, text=True)
    return r.returncode == 0


def measure_one(path):
    P, f = psd(path)
    if P is None:
        return None
    return rolloff(P, f), prominence(P, f)


def forward_model(cutoff_knob, jobs=6):
    """F(fc, k) -> (rolloff, prominence), measured not derived."""
    import concurrent.futures
    os.makedirs(SCRATCH, exist_ok=True)
    fixture = os.path.join(FIX, f"h303_cut{cutoff_knob:03d}_reso000.rbs")
    bc = base_cut(cutoff_knob) * DETUNE
    jobs_list = [(int(round(bc * rel)), k) for rel in FC_REL for k in KS]

    def one(job):
        fc, k = job
        out = os.path.join(SCRATCH, f"m_{cutoff_knob}_{fc}_{k}.wav")
        if not render(fixture, out, {
                "REBORN_CUT_SCALE": f"{fc / bc:.6f}",
                "REBORN_K_A": f"{k}", "REBORN_K_B": "0", "REBORN_K_CUT": "0"}):
            return None
        m = measure_one(out)
        return (fc, k, m) if m else None

    table = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for r in ex.map(one, jobs_list):
            if r:
                table[(r[0], r[1])] = r[2]
    return table


def invert(table, target_roll, target_prom):
    """Nearest cell in a normalised (log rolloff, prominence) space."""
    best, berr = None, 1e18
    for (fc, k), (r, p) in table.items():
        if not np.isfinite(r) or not np.isfinite(p):
            continue
        e = (np.log2(r / target_roll) / 0.10) ** 2 + ((p - target_prom) / 1.5) ** 2
        if e < berr:
            berr, best = e, (fc, k)
    return best, berr


def main(cutoffs):
    A = {}
    for c in cutoffs:
        for r in RESO:
            p = os.path.join(RBDIR, f"h303_cut{c:03d}_reso{r:03d}.wav")
            if os.path.exists(p):
                A[(c, r)] = measure_one(p)

    for c in cutoffs:
        print(f"=== ReBirth inverted through our filter, cutoff knob {c} ===")
        print(f"    our own corner here is {base_cut(c)*DETUNE:.0f} Hz "
              f"(base {base_cut(c):.0f} Hz, minus the {100*(1-DETUNE):.1f}% diode detune),")
        print(f"    and it moves at most {100*(1-(1-0.012*2.45**2)/DETUNE):.1f}% DOWN "
              f"across the whole resonance knob.")
        print()
        table = forward_model(c)
        if not table:
            print("    forward model empty, check the build\n")
            continue
        fcs = sorted(set(k[0] for k in table))
        ks = sorted(set(k[1] for k in table))
        print(f"    forward model: {len(table)} cells, fc {fcs[0]}..{fcs[-1]} Hz, "
              f"k {ks[0]}..{ks[-1]}")
        print()
        print(f"    {'reso':>5s} {'RB rolloff':>11s} {'RB prom':>8s} | "
              f"{'implied fc':>11s} {'implied k':>10s} {'fit err':>8s}")
        rows = []
        for r in RESO:
            if (c, r) not in A or A[(c, r)] is None:
                continue
            tr, tp = A[(c, r)]
            (fc, k), err = invert(table, tr, tp)
            rows.append((r, fc, k))
            edge = ""
            if fc in (fcs[0], fcs[-1]) or k in (ks[0], ks[-1]):
                edge = "  <- ON GRID EDGE"
            print(f"    {r:5d} {tr:11.0f} {tp:8.1f} | {fc:11d} {k:10.2f} "
                  f"{np.sqrt(err):8.2f}{edge}")
        if len(rows) >= 2:
            f0, f1 = rows[0][1], rows[-1][1]
            print()
            print(f"    ReBirth's implied corner moves {f0} -> {f1} Hz across the "
                  f"resonance knob:")
            print(f"    a factor of {f1/f0:.2f}, i.e. {np.log2(f1/f0):+.2f} OCTAVES, "
                  f"where ours moves {np.log2((1-0.012*2.45**2)/DETUNE):+.2f}.")
        print()


if __name__ == "__main__":
    args = [int(a) for a in sys.argv[1:]] or [127, 64]
    main(args)
