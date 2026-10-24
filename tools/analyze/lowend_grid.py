#!/usr/bin/env python3
"""The low-end surface across the WHOLE resonance knob, plucked. Task 57.

WHAT THIS ANSWERS. ReBirth loses low end as resonance rises and we do not. Batch
N fitted that to a one-pole highpass near 100 Hz, switched on by resonance and
saturating by resonance 60, to 0.45 dB. Batch L then showed the law is wrong
BELOW the knee: at resonance 30 ReBirth's loss depends on CUTOFF, running -2.36
dB at cutoff 16 to -6.03 at cutoff 127, and the law has no cutoff term.

Batch L2 fills in resonance 15, 45 and 60 at the same six cutoffs, so batch L and
L2 merge into a 6 x 6 grid with FOUR points at or below the knee instead of one.
This reads that grid.

WHY THIS DOES NOT REPEAT BATCH P. Batch P asked the same question with TIED
notes and got a confident wrong answer, because ReBirth behaves differently on a
held note at low resonance: its tied and plucked curves differ by 2.45 dB at
resonance 0 where ours differ by 0.02. These fixtures are PLUCKED, and plucked
batch L agrees with plucked batch H to 0.12 dB at every frequency their combs
share, which is a validated methodology rather than an assumed one.

WHAT IS REPORTED, AND IN WHAT ORDER. The controls come first and the surface
second, because batch P proved that a fixture set can reproduce perfectly and
still be measuring the wrong thing. If the duplicates disagree the export chain
drifted; if the tie controls agree at low resonance then the tie effect is not
what batch P thought; and both must be read before any number below them.

THE STATISTIC. f0 is 55 Hz, so the harmonic comb gives 55 / 110 / 165 / 220 /
275 / 330 Hz. The headline is h1 minus h4, which is 55 Hz against 220 Hz, the
same statistic every existing finding on this question quotes. The whole k = 1..6
curve is reported alongside it, because a single ratio cannot tell a shelf from a
pole and the runbook's rule is to widen what is measured rather than refine the
metric.

THE CONTAMINATION GUARD, which is the reason batch P's header exists. At cutoff
16 our nominal corner is about 550 Hz, so 220 Hz sits only 2.5x below it and the
LOWPASS is already tilting the band the highpass is being measured in. The
cutoff-16 column is therefore reported and flagged, never silently pooled. Read
it as a lower bound on the real effect, not as a measurement of it.

Usage:
    python lowend_grid.py                 controls, surface, recovered corner
    python lowend_grid.py --checks-only   stop after the controls
    python lowend_grid.py --ours DIR      score a different render directory
"""
import os
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
sys.path.insert(0, HERE)
from harm_response import psd, find_f0, harmonics        # noqa: E402

F0 = 55.0                      # note A, octave -1
CUT = (16, 40, 64, 90, 110, 127)
RESO = (0, 15, 30, 45, 60, 127)
NK = 6                         # harmonics 1..6, i.e. 55 Hz to 330 Hz
REF_K = 4                      # 220 Hz, the reference the existing findings use

# Where each batch's cells live. Batch L carries resonance 0, 30 and 127; batch
# L2 carries 15, 45 and 60. Nothing is duplicated between them by design, so the
# merge is a union rather than a reconciliation.
DIRS = {
    0:   ("batchL_rb",  "batchL_ours"),
    30:  ("batchL_rb",  "batchL_ours"),
    127: ("batchL_rb",  "batchL_ours"),
    15:  ("batchL2_rb", "batchL2_ours"),
    45:  ("batchL2_rb", "batchL2_ours"),
    60:  ("batchL2_rb", "batchL2_ours"),
}


def cell(rb_or_ours, c, r, suffix=""):
    d = DIRS[r][0 if rb_or_ours == "rb" else 1]
    base = d if os.path.isabs(d) else os.path.join(WS, "renders", d)
    return os.path.join(base, f"l303_cut{c:03d}_reso{r:03d}{suffix}.wav")


def point_ours(dir_l, dir_l2):
    """Score a CANDIDATE render of our side instead of the stored one.

    Batch L and L2 live in separate directories, so a candidate needs both.
    This is how a proposed law gets scored on the whole grid: render both
    batches under the REBORN_* overrides, then point this at the results.
    """
    for r in (0, 30, 127):
        DIRS[r] = (DIRS[r][0], dir_l)
    for r in (15, 45, 60):
        DIRS[r] = (DIRS[r][0], dir_l2)


def stereo_check(path):
    """Return peak |L-R| in dB relative to full scale, or None if not stereo.

    The rule is measure stereo or check that L minus R is zero and SAY that you
    checked. psd() averages the channels, which is only free SNR if the file
    really is dual-mono, so this is what makes that legitimate here.
    """
    with wave.open(path, "rb") as w:
        n, ch = w.getnframes(), w.getnchannels()
        raw = w.readframes(n)
    if ch != 2:
        return None
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, 2)
    side = np.abs(x[:, 0] - x[:, 1]).max()
    return 20.0 * np.log10(side + 1e-12)


def curve(path):
    """dB at harmonics 1..NK, source 1/k divided out, referenced to harmonic 4.

    Returns (dB array, absolute dB of the reference harmonic). The second value
    is the contamination guard: a reference that is itself being attenuated by
    the lowpass makes every number above it a ratio of two moving things.
    """
    if not os.path.exists(path):
        return None, None
    P, f, sr = psd(path)
    if P is None:
        return None, None
    f0 = find_f0(P, f, guess=F0, tol=0.10)
    ks, m = harmonics(P, f, f0, kmax=NK)
    if len(ks) < NK:
        return None, None
    db = 20.0 * np.log10(m + 1e-30)
    return db - db[REF_K - 1], db[REF_K - 1]


def h1_minus_h4(path):
    d, ref = curve(path)
    return (None, None) if d is None else (d[0], ref)


# --------------------------------------------------------------------------
# Controls. These run first and unconditionally.
# --------------------------------------------------------------------------
def controls():
    print("=" * 74)
    print("CONTROLS. Read these before any number below them.")
    print("=" * 74)

    ok = True

    print("\n1. STEREO. psd() averages L and R, which is only free SNR if the")
    print("   files are dual-mono. Peak |L-R| over a sample of cells:")
    worst = -999.0
    for c, r in ((16, 0), (90, 30), (127, 127), (64, 45)):
        for side in ("rb", "ours"):
            p = cell(side, c, r)
            if not os.path.exists(p):
                continue
            s = stereo_check(p)
            if s is not None:
                worst = max(worst, s)
    print(f"   worst peak |L-R| = {worst:.1f} dBFS", end="")
    print("   (dual-mono, averaging is legitimate)" if worst < -90
          else "   *** NOT dual-mono: do not average ***")
    if worst >= -90:
        ok = False

    print("\n2. DUPLICATES. Same fixture, exported twice. This is the export")
    print("   chain's own noise floor on this statistic.")
    dupes = ((16, 0), (90, 30), (127, 127), (16, 15), (90, 45), (127, 60))
    ds = []
    for c, r in dupes:
        a, _ = h1_minus_h4(cell("rb", c, r))
        b, _ = h1_minus_h4(cell("rb", c, r, "_dup"))
        if a is None or b is None:
            continue
        ds.append(abs(a - b))
        print(f"   cut {c:3d} reso {r:3d}   {a:+7.3f} vs {b:+7.3f}   "
              f"delta {a - b:+.3f} dB")
    if ds:
        print(f"   max |delta| = {max(ds):.3f} dB, mean = {np.mean(ds):.3f}")

    print("\n3. TIE vs PLUCK at resonance 15 and 45, cutoff 64. Batch P died on")
    print("   this: at resonance 0 ReBirth's tied and plucked curves differ by")
    print("   2.45 dB where ours differ by 0.02. If that gap is still open")
    print("   ACROSS the knee, a tied fixture cannot be used here at all.")
    for r in (15, 45):
        rp, _ = h1_minus_h4(cell("rb", 64, r))
        rt, _ = h1_minus_h4(cell("rb", 64, r, "_tie"))
        op, _ = h1_minus_h4(cell("ours", 64, r))
        ot, _ = h1_minus_h4(cell("ours", 64, r, "_tie"))
        if None in (rp, rt, op, ot):
            print(f"   reso {r:3d}   missing")
            continue
        print(f"   reso {r:3d}   ReBirth tie-minus-pluck {rt - rp:+7.3f} dB    "
              f"ours {ot - op:+7.3f} dB")

    print()
    return ok


# --------------------------------------------------------------------------
# The surface.
# --------------------------------------------------------------------------
def surface(ours_dir=None):
    if ours_dir:
        for r in DIRS:
            DIRS[r] = (DIRS[r][0], ours_dir)

    print("=" * 74)
    print("THE SURFACE. h1 minus h4, which is 55 Hz against 220 Hz, in dB.")
    print("More negative = less low end.")
    print("=" * 74)

    RB = np.full((len(CUT), len(RESO)), np.nan)
    OU = np.full((len(CUT), len(RESO)), np.nan)
    REF = np.full((len(CUT), len(RESO)), np.nan)
    for i, c in enumerate(CUT):
        for j, r in enumerate(RESO):
            a, ar = h1_minus_h4(cell("rb", c, r))
            b, _ = h1_minus_h4(cell("ours", c, r))
            if a is not None:
                RB[i, j], REF[i, j] = a, ar
            if b is not None:
                OU[i, j] = b

    def show(title, M):
        print(f"\n{title}")
        print("  cutoff |" + "".join(f"{r:>9d}" for r in RESO))
        print("  -------+" + "-" * (9 * len(RESO)))
        for i, c in enumerate(CUT):
            row = "".join("      ---" if np.isnan(v) else f"{v:>+9.2f}"
                          for v in M[i])
            flag = "   <- LOWPASS CONTAMINATES, see header" if c == 16 else ""
            print(f"  {c:>6d} |{row}{flag}")

    show("ReBirth", RB)
    show("ours", OU)
    show("ReBirth minus ours (the gap the highpass has to close)", RB - OU)

    print("\nReference harmonic (220 Hz) absolute level, dB. If this MOVES with")
    print("resonance the reference is not flat and the ratios above are two")
    print("moving things divided by each other.")
    show("220 Hz absolute", REF)

    return RB, OU


# --------------------------------------------------------------------------
# Recover the implied one-pole corner per cell, rather than fitting a law.
# --------------------------------------------------------------------------
def implied_corner(RB, OU):
    """Solve for the fc that turns our curve into ReBirth's, per cell.

    A one-pole highpass at fc scales frequency f by f/sqrt(f^2+fc^2), so the
    change it makes to h1-h4 is a closed form and inverting it is algebra
    rather than a fit. Doing it per cell and LOOKING at the surface is the
    "widen what is measured" move: a law fitted to the pooled set would hide
    exactly the cutoff dependence this task exists to find.
    """
    print("\n" + "=" * 74)
    print("IMPLIED ONE-POLE CORNER, Hz. The fc that would close the gap.")
    print("A law with no cutoff term predicts each COLUMN is constant.")
    print("=" * 74)

    f1, f4 = F0, F0 * REF_K

    def delta_db(fc):
        h1 = f1 / np.sqrt(f1 * f1 + fc * fc)
        h4 = f4 / np.sqrt(f4 * f4 + fc * fc)
        return 20.0 * np.log10(h1 / h4)

    grid = np.concatenate([[0.0], np.geomspace(1.0, 2000.0, 4000)])
    pred = np.array([delta_db(x) for x in grid])

    FC = np.full_like(RB, np.nan)
    for i in range(RB.shape[0]):
        for j in range(RB.shape[1]):
            d = RB[i, j] - OU[i, j]
            if np.isnan(d):
                continue
            if d > 0:                 # ReBirth has MORE low end here: no highpass
                FC[i, j] = 0.0
                continue
            FC[i, j] = grid[int(np.argmin(np.abs(pred - d)))]

    print("\n  cutoff |" + "".join(f"{r:>9d}" for r in RESO))
    print("  -------+" + "-" * (9 * len(RESO)))
    for i, c in enumerate(CUT):
        row = "".join("      ---" if np.isnan(v) else f"{v:>9.1f}" for v in FC[i])
        flag = "   <- contaminated" if c == 16 else ""
        print(f"  {c:>6d} |{row}{flag}")

    print("\n  column spread, excluding the contaminated cutoff-16 row:")
    for j, r in enumerate(RESO):
        col = FC[1:, j]
        col = col[~np.isnan(col)]
        if len(col) < 2:
            continue
        print(f"    reso {r:3d}   fc {col.min():7.1f} to {col.max():7.1f} Hz"
              f"   spread {col.max() - col.min():7.1f}   mean {col.mean():7.1f}")

    print("\n  The current shipped law is fc = LF_HP_MAX * r/(r + LF_HP_KNEE)")
    print("  with LF_HP_MAX 119.9 and LF_HP_KNEE 0.141, r the knob over 0..1.")
    print("  What it predicts, against the column means above:")
    for j, r in enumerate(RESO):
        rn = r / 127.0
        law = 119.9 * rn / (rn + 0.141)
        col = FC[1:, j]
        col = col[~np.isnan(col)]
        if not len(col):
            continue
        print(f"    reso {r:3d}   law {law:7.1f}   measured mean {col.mean():7.1f}"
              f"   error {col.mean() - law:+7.1f} Hz")
    return FC


def shape_check():
    """Is the gap a ONE-POLE at all? Check the shape before fitting a law.

    Inverting a single ratio to a corner assumes the answer. h1 minus h4 can be
    matched by a one-pole, a shelf or a two-pole at three different corners, and
    all three would print a plausible number. The six-point curve tells them
    apart: a one-pole at 110 Hz has a specific, checkable shape across 55 / 110
    / 165 / 220 / 275 / 330 Hz, and nothing else does.

    This is the same discipline that caught the harmonic instrument on
    2026-07-31. Fit nothing until the assumed form has been tested against a
    known one.
    """
    print("\n" + "=" * 74)
    print("SHAPE CHECK. Is the gap a one-pole? dB at 55/110/165/220/275/330,")
    print("ReBirth minus ours, against what a one-pole at the corner that")
    print("matches h1-h4 alone would predict.")
    print("=" * 74)

    def onepole(fc):
        f = F0 * np.arange(1, NK + 1)
        h = f / np.sqrt(f * f + fc * fc)
        return 20.0 * np.log10(h / h[REF_K - 1])

    grid = np.concatenate([[0.0], np.geomspace(1.0, 2000.0, 4000)])
    cells = ((127, 127), (110, 127), (64, 127), (127, 0), (90, 60), (64, 30))
    print(f"\n  {'cell':<18}{'55':>8}{'110':>8}{'165':>8}{'220':>8}"
          f"{'275':>8}{'330':>8}   {'fc':>7} {'rms':>6}")
    for c, r in cells:
        a, _ = curve(cell("rb", c, r))
        b, _ = curve(cell("ours", c, r))
        if a is None or b is None:
            continue
        d = a - b
        fc = grid[int(np.argmin(np.abs(
            np.array([onepole(x)[0] for x in grid]) - d[0])))]
        p = onepole(fc)
        res = float(np.sqrt(np.mean((d - p) ** 2)))
        print(f"  cut{c:>4} reso{r:>4}  " + "".join(f"{v:>+8.2f}" for v in d)
              + f"   {fc:>7.1f} {res:>6.3f}")
        print(f"  {'one-pole predicts':<18}" + "".join(f"{v:>+8.2f}" for v in p))
    print("\n  rms is the residual against the one-pole in dB. The duplicate")
    print("  controls put this statistic's own noise at 0.026 dB mean, so a")
    print("  residual much above about 0.1 dB means the form is wrong.")


def two_term(RB=None, OU=None):
    """Fit a one-pole PLUS a broadband tilt, and see whether the tilt is task 58.

    The shape check says the gap is a one-pole to 0.03 dB at resonance 127 and
    is NOT one below the knee: there the curve droops at 275 and 330 Hz where a
    one-pole rises. A droop across the whole band relative to the 220 Hz
    reference is what a LOWPASS CORNER difference looks like locally, and task
    58 has independently measured our resonance-0 corner as running 0.22 octaves
    HIGH at cutoff 127, which puts ReBirth's rolloff earlier and would produce
    exactly this sign.

    So fit two terms and ask three questions:
      1. does the residual collapse to the duplicate noise floor,
      2. is the recovered highpass corner then INDEPENDENT of cutoff,
      3. does the fitted tilt track cutoff the way task 58 says it should.

    The third is the real test, because the tilt is not what this fixture was
    built to measure. A parameter that lands on an independently measured value
    is evidence; one that merely absorbs residual is not.

    The tilt is deliberately parameterised as dB per octave rather than as a
    lowpass corner. gen_batchP.py records why: a two-corner fit calibrated on
    noiseless synthetics recovered a 3187 Hz corner from a band ending at 327 Hz
    and then returned 1158 for a known 3190 on real data carrying 0.5 dB of
    ripple. A corner far above the band is not identifiable from inside it. A
    local tilt is, and it is the honest thing to claim.
    """
    print("\n" + "=" * 74)
    print("TWO-TERM FIT: one-pole highpass + broadband tilt.")
    print("=" * 74)

    f = F0 * np.arange(1, NK + 1)
    oct_ = np.log2(f / (F0 * REF_K))

    def model(fc, slope):
        h = f / np.sqrt(f * f + fc * fc)
        d = 20.0 * np.log10(h / h[REF_K - 1])
        return d + slope * (oct_ - oct_[REF_K - 1])

    # The model is LINEAR in slope at fixed fc, so slope is solved in closed
    # form and only fc is searched. That matters for more than speed: a bounded
    # slope grid rails, and a railed parameter is not a fit. The first version
    # of this hit -2.00 dB/oct in nine cells and had to be thrown away.
    fcs = np.concatenate([[0.0], np.geomspace(1.0, 900.0, 3000)])
    basis = oct_ - oct_[REF_K - 1]
    bb = float(basis @ basis)

    FC = np.full((len(CUT), len(RESO)), np.nan)
    SL = np.full((len(CUT), len(RESO)), np.nan)
    RS = np.full((len(CUT), len(RESO)), np.nan)
    for i, c in enumerate(CUT):
        for j, r in enumerate(RESO):
            a, _ = curve(cell("rb", c, r))
            b, _ = curve(cell("ours", c, r))
            if a is None or b is None:
                continue
            d = a - b
            best = (1e9, np.nan, np.nan)
            for fc in fcs:
                hp = model(fc, 0.0)
                s = float((d - hp) @ basis) / bb
                e = float(np.sqrt(np.mean((d - hp - s * basis) ** 2)))
                if e < best[0]:
                    best = (e, fc, s)
            RS[i, j], FC[i, j], SL[i, j] = best

    def show(title, M, fmt="{:>+9.2f}"):
        print(f"\n{title}")
        print("  cutoff |" + "".join(f"{r:>9d}" for r in RESO))
        print("  -------+" + "-" * (9 * len(RESO)))
        for i, c in enumerate(CUT):
            row = "".join("      ---" if np.isnan(v) else fmt.format(v)
                          for v in M[i])
            print(f"  {c:>6d} |{row}" + ("   <- contaminated" if c == 16 else ""))

    show("recovered HIGHPASS corner, Hz", FC, "{:>9.1f}")
    show("fitted TILT, dB/octave", SL)
    show("residual rms, dB (duplicate floor is 0.026)", RS, "{:>9.3f}")

    print("\n  Q1  residual, excluding the contaminated cutoff-16 row:")
    v = RS[1:, :][~np.isnan(RS[1:, :])]
    print(f"      mean {v.mean():.3f} dB, max {v.max():.3f}, "
          f"against 0.026 duplicate / 0.114 project floor")

    print("\n  Q2  is the corner cutoff-independent now? spread down each column:")
    for j, r in enumerate(RESO):
        col = FC[1:, j][~np.isnan(FC[1:, j])]
        if len(col) < 2:
            continue
        print(f"      reso {r:3d}   {col.min():6.1f} to {col.max():6.1f} Hz"
              f"   spread {col.max() - col.min():5.1f}   mean {col.mean():6.1f}")

    print("\n  Q3  does the tilt track CUTOFF rather than resonance? mean per row:")
    for i, c in enumerate(CUT):
        row = SL[i][~np.isnan(SL[i])]
        if not len(row):
            continue
        print(f"      cutoff {c:3d}   tilt {row.mean():+6.3f} dB/oct"
              f"   (spread over resonance {row.max() - row.min():.3f})")
    return FC, SL, RS


def main():
    args = sys.argv[1:]
    ours_dir = None
    if "--ours" in args:
        ours_dir = args[args.index("--ours") + 1]

    if not controls():
        print("A CONTROL FAILED. Stopping: nothing below it would mean anything.")
        return 1
    if "--checks-only" in args:
        return 0

    RB, OU = surface(ours_dir)
    implied_corner(RB, OU)
    return 0


if __name__ == "__main__":
    sys.exit(main())
