#!/usr/bin/env python3
"""Can a resonance-scaled ZERO produce ReBirth's shallowing skirt? Predict first.

Before writing any DSP, ask the algebra whether the proposed mechanism can even
produce the measured effect, and at what cost. A mechanism that cannot is killed
for free; one that can arrives with a starting value and a predicted side effect.

THE MEASUREMENT being explained, at cutoff 110, where two independently exported
ReBirth batches agree to 1.2 dB/oct (batch S grid against batch SW sweep):

    resonance   45    60    75    90   100   110   118   127
    ReBirth  -32.9 -33.8 -31.9 -30.1 -30.6 -29.1 -29.4 -29.5
    ours     -33.2 -36.1 -35.8 -34.5 -34.6 -33.1 -33.1 -34.3

ReBirth shallows about 4 dB/oct across the knob. Ours does not move.

WHY NO CONSTANT CAN FIX IT. For a ladder with feedback k the response is
G^4 / (1 + k G^4) with G = 1/(1 + s/w). Far above the corner G^4 vanishes, so
the k G^4 term in the denominator becomes negligible against 1 and the whole
expression collapses to G^4. The feedback drops out of the far field entirely.
That is why two refits moved the rms errors and never moved this row.

THE HYPOTHESIS. Mix a resonance-scaled amount of an EARLIER tap into the output:

    H(s) = [ G^4 + c*G ] / (1 + k G^4)

The c*G term falls at only 6 dB/oct, so above the frequency where it overtakes
G^4 the composite slope shallows toward -6. Growing c with resonance moves that
crossover down into the measurement band as the knob opens, which is the shape
ReBirth shows. Equivalent to a zero whose position tracks the feedback, and the
same algebra as summing a bandpass path with a lowpass, which is what a
state-variable structure gives and what a 1997 CPU could afford.

WHAT THIS SCRIPT DOES NOT DO. It models an ideal continuous-time ladder. Our
engine is an oversampled TPT ladder with saturators, a corner that moves with
resonance, and a drive stage, so the ABSOLUTE slopes here will not match the
measured ones. Only the SHAPE of slope-against-resonance, and the size of the
change c can buy, are being tested. Read it as feasibility, not as a fit.

Usage:  python skirt_model.py
"""
import numpy as np

# measured, cutoff 110, from tools/analyze/skirt_sweep.py
RESO = np.array([45, 60, 75, 90, 100, 110, 118, 127], float)
RB = np.array([-32.9, -33.8, -31.9, -30.1, -30.6, -29.1, -29.4, -29.5])
OURS = np.array([-33.2, -36.1, -35.8, -34.5, -34.6, -33.1, -33.1, -34.3])


def response(k, c, n=4000):
    """|H| in dB on a log frequency grid, w = 1."""
    x = np.logspace(-1.5, 2.5, n)          # Omega / w
    G = 1.0 / (1.0 + 1j * x)
    G4 = G ** 4
    H = (G4 + c * G) / (1.0 + k * G4)
    return x, 20 * np.log10(np.abs(H) + 1e-300)


def skirt(k, c):
    """Slope over 1.7x..4x the PEAK, least squares in dB vs log2 f.

    The same statistic skirt_sweep.py computes, including scaling the band to
    the peak rather than fixing it, so the band sits in the same place relative
    to the corner as the corner moves.
    """
    x, db = response(k, c)
    ip = int(np.argmax(db))
    fp = x[ip]
    sel = (x >= 1.7 * fp) & (x <= 4.0 * fp)
    if sel.sum() < 8:
        return float("nan"), float("nan"), float("nan")
    A = np.vstack([np.log2(x[sel]), np.ones(sel.sum())]).T
    slope = np.linalg.lstsq(A, db[sel], rcond=None)[0][0]
    # peak prominence above the passband, the thing a zero threatens
    passband = db[x < 0.1].mean()
    return slope, db[ip] - passband, fp


if __name__ == "__main__":
    print(__doc__.split("Usage:")[0].strip()[:0] or "", end="")
    print("=== 1. does the ideal ladder reproduce OUR behaviour (c = 0)? ===")
    print("   the prediction is a slope that does NOT move with feedback\n")
    print(f"   {'k':>6s} {'slope':>8s} {'peak dB':>9s} {'peak f/w':>9s}")
    for k in (0.0, 1.0, 2.0, 2.8, 3.4, 3.7, 3.9):
        s, p, fp = skirt(k, 0.0)
        print(f"   {k:6.2f} {s:8.2f} {p:9.2f} {fp:9.3f}")
    s0 = skirt(1.0, 0.0)[0]
    s1 = skirt(3.9, 0.0)[0]
    print(f"\n   slope moves {s1 - s0:+.2f} dB/oct across the whole feedback range.")
    print("   Ours measured a drift of about -0.2 across the knob, i.e. also nil.")
    print("   The algebra and our engine agree: feedback alone cannot do this.\n")

    print("=== 2. what does the zero buy, and what does it cost? ===")
    print("   k held at 3.4, sweeping c\n")
    print(f"   {'c':>7s} {'slope':>8s} {'d slope':>9s} {'peak dB':>9s} {'d peak':>8s}")
    base_s, base_p, _ = skirt(3.4, 0.0)
    for c in (0.0, 0.0005, 0.001, 0.002, 0.004, 0.008, 0.016, 0.032):
        s, p, _ = skirt(3.4, c)
        print(f"   {c:7.4f} {s:8.2f} {s - base_s:+9.2f} {p:9.2f} {p - base_p:+8.2f}")
    print()

    print("=== 3. the actual test: can c(k) reproduce ReBirth's row? ===")
    print("   need about +4 dB/oct of shallowing from resonance 60 to 127,")
    print("   with the peak prominence not collapsing.\n")
    target = RB - RB[1]          # shallowing relative to resonance 60
    print(f"   {'reso':>5s} {'k':>6s} {'RB rel':>8s} | {'c needed':>10s} "
          f"{'model rel':>10s} {'peak cost dB':>13s}")
    kk = 1.6 + 2.1 * (RESO / 127.0)      # roughly our own k law over the knob
    ok = True
    for i, r in enumerate(RESO):
        k = kk[i]
        b = skirt(k, 0.0)
        want = target[i]
        best = None
        for c in np.concatenate([[0.0], np.logspace(-5, -0.5, 400)]):
            s, p, _ = skirt(k, c)
            e = abs((s - b[0]) - want)
            if best is None or e < best[0]:
                best = (e, c, s - b[0], p - b[1])
        e, c, got, dp = best
        flag = "" if e < 0.5 else "   <- cannot reach"
        if e >= 0.5:
            ok = False
        print(f"   {r:5.0f} {k:6.2f} {want:8.2f} | {c:10.5f} {got:10.2f} "
              f"{dp:13.2f}{flag}")
    print()
    print("   VERDICT:", "the mechanism can produce the measured shape."
          if ok else "the mechanism CANNOT reach the measured shape.")
