import os, sys
import numpy as np
sys.path.insert(0, "tools/analyze")
from harm_response import psd, find_f0, harmonics

WS = os.path.abspath("..")
Q  = os.path.join(WS, "renders", "batchQ_rb")
L  = os.path.join(WS, "renders", "batchL_rb")

def curve(path, f0guess, nk=6):
    P, f, sr = psd(path)
    if P is None: return None, None
    f0 = find_f0(P, f, guess=f0guess, tol=0.10)
    ks, m = harmonics(P, f, f0, kmax=nk)
    if len(ks) < nk: return None, f0
    db = 20*np.log10(m + 1e-30)
    return db - db[3], f0          # referenced to harmonic 4

print("=" * 72)
print("CONTROL 1. CHAIN CHECK. Batch Q's 120 BPM cells duplicate batch L")
print("exactly. They must reproduce batch L's exports or the chain drifted.")
print("=" * 72)
print(f"  {'cell':<12}{'batch L h1-h4':>15}{'batch Q h1-h4':>15}{'delta':>9}   f0 L / f0 Q")
worst = 0.0
for c in (40, 127):
    a, fa = curve(os.path.join(L, f"l303_cut{c:03d}_reso000.wav"), 55.0)
    b, fb = curve(os.path.join(Q, f"q303_A1_cut{c:03d}_reso000_bpm120_CHAINCHECK.wav"), 55.0)
    if a is None or b is None:
        print(f"  cut{c:<9} MISSING"); continue
    d = b[0] - a[0]; worst = max(worst, abs(d))
    print(f"  cut{c:<9}{a[0]:>+15.3f}{b[0]:>+15.3f}{d:>+9.3f}   {fa:.3f} / {fb:.3f}")
print(f"\n  worst |delta| = {worst:.3f} dB against a 0.026 dB duplicate floor")
print("  VERDICT:", "PASS, chain intact" if worst < 0.15 else "*** FAIL, do not interpret the grid ***")

print()
print("=" * 72)
print("CONTROL 2. TEMPO. 60 BPM is a new variable. Same note, same knobs.")
print("A longer note makes ReBirth's env-mod-0 onset a smaller fraction of")
print("what is measured, so this is not assumed neutral, it is measured.")
print("=" * 72)
print(f"  {'cell':<12}{'120 BPM':>12}{'60 BPM':>12}{'delta':>9}")
for c in (40, 127):
    a, _ = curve(os.path.join(Q, f"q303_A1_cut{c:03d}_reso000_bpm120_CHAINCHECK.wav"), 55.0)
    b, _ = curve(os.path.join(Q, f"q303_A1_cut{c:03d}_reso000_bpm060_TEMPOCHECK.wav"), 55.0)
    if a is None or b is None: continue
    print(f"  cut{c:<9}{a[0]:>+12.3f}{b[0]:>+12.3f}{b[0]-a[0]:>+9.3f}")

print()
print("=" * 72)
print("CONTROL 3. DUPLICATES. This batch's own noise floor.")
print("=" * 72)
F0 = {"C1": 32.703, "E1": 41.203}
ds = []
for lab, c, r in (("C1", 16, 0), ("E1", 90, 15), ("C1", 127, 30)):
    base = f"q303_{lab}_cut{c:03d}_reso{r:03d}_bpm060"
    a, _ = curve(os.path.join(Q, base + ".wav"), F0[lab])
    b, _ = curve(os.path.join(Q, base + "_dup.wav"), F0[lab])
    if a is None or b is None:
        print(f"  {lab} cut{c} reso{r}: MISSING"); continue
    d = b[0] - a[0]; ds.append(abs(d))
    print(f"  {lab} cut{c:>3} reso{r:>3}   {a[0]:>+8.3f} vs {b[0]:>+8.3f}   delta {d:>+7.3f} dB")
if ds:
    print(f"\n  max {max(ds):.3f}, mean {np.mean(ds):.3f} dB "
          f"(batch L+L2 was max 0.068, mean 0.026)")
