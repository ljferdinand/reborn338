"""Merge the C1 and E1 combs into one 18-point curve and ask what shape the
resonance-0 gap has. This is what batch L could not do with six points."""
import os, sys
import numpy as np
sys.path.insert(0, "tools/analyze")
from harm_response import psd, find_f0, harmonics

WS  = os.path.abspath("..")
RB  = os.path.join(WS, "renders", "batchQ_rb")
OU  = os.path.join(WS, "renders", "batchQ_ours")
F0  = {"C1": 32.703, "E1": 41.203}
NK  = {"C1": 10, "E1": 8}          # up to ~330 Hz on each
REF_LO, REF_HI = 230.0, 340.0      # the shared flat reference band
CUT  = (40, 64, 90, 110, 127)      # cutoff 16 reported separately, contaminated
RESO = (0, 15, 30)

def comb(root, lab, c, r):
    """Harmonic freqs and dB for one file, referenced to its own 230-340 level."""
    p = os.path.join(root, f"q303_{lab}_cut{c:03d}_reso{r:03d}_bpm060.wav")
    if not os.path.exists(p): return None, None
    P, f, sr = psd(p)
    if P is None: return None, None
    f0 = find_f0(P, f, guess=F0[lab], tol=0.10)
    ks, m = harmonics(P, f, f0, kmax=NK[lab])
    if len(ks) < NK[lab]: return None, None
    fr = ks * f0
    db = 20*np.log10(m + 1e-30)
    sel = (fr >= REF_LO) & (fr <= REF_HI)
    if sel.sum() < 2: return None, None
    return fr, db - db[sel].mean()

def merged(root, c, r):
    fs, ds = [], []
    for lab in ("C1", "E1"):
        fr, db = comb(root, lab, c, r)
        if fr is None: return None, None
        fs.append(fr); ds.append(db)
    f = np.concatenate(fs); d = np.concatenate(ds)
    o = np.argsort(f)
    return f[o], d[o]

# --- models ------------------------------------------------------------
def fit(f, d, order, tilt):
    ref = 220.0
    oct_rel = np.log2(f/ref)
    fcs = np.concatenate([[0.0], np.geomspace(1.0, 900.0, 3000)])
    bb = float(oct_rel @ oct_rel)
    best = (1e9, np.nan, 0.0)
    for fc in fcs:
        h = (f/np.sqrt(f*f + fc*fc))**order
        m = 20*np.log10(h)
        m = m - np.interp(ref, f, m)
        s = float((d - m) @ oct_rel)/bb if tilt else 0.0
        e = float(np.sqrt(np.mean((d - m - s*oct_rel)**2)))
        if e < best[0]: best = (e, fc, s)
    return best

print("18-POINT MERGED COMB, 32.7 to 330 Hz. Gap = ReBirth minus ours, dB.")
print("Residual rms of each model, dB. Duplicate floor 0.02 to 0.04 (0.23 at")
print("the darkest corner). Cutoff 16 excluded: the lowpass contaminates it.\n")
hdr = f"{'cell':<16}{'npts':>5}{'gap@33Hz':>10}"
for lbl in ("1-pole", "1p+tilt", "2p+tilt", "3p+tilt"):
    hdr += f"{lbl:>19}"
print(hdr)
print(f"{'':<16}{'':>5}{'':>10}" + "".join(f"{'rms':>8}{'fc':>7}{'tl':>4}" for _ in range(4)))
rows = {}
for r in RESO:
    for c in CUT:
        fr, dr = merged(RB, c, r); fo, do = merged(OU, c, r)
        if fr is None or fo is None:
            print(f"  cut{c:>3} reso{r:>3}   MISSING"); continue
        d = dr - do
        line = f"  cut{c:>3} reso{r:>3} {len(d):>5}{d[0]:>+10.2f}"
        for order, tilt in ((1, False), (1, True), (2, True), (3, True)):
            e, fc, s = fit(fr, d, order, tilt)
            line += f"{e:>8.3f}{fc:>7.1f}{s:>+4.0f}"
        print(line)
        rows[(c, r)] = (fr, d)
    print()
