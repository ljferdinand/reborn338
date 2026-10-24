#!/usr/bin/env python3
"""PCF resonator-surface analysis, one method for the ReBirth exports and for
ours (docs/MEASUREMENT_PROTOCOL.md). For each static LP fixture srf_q<QQQ>_f<FFF> in DIR it
reports the resonant peak's PROMINENCE (dB above the passband) and CORNER (peak
Hz), as the transfer against srf_ref. This is the surface the re-derived resonator
must match: prominence and corner over (Q band byte) x (Freq).

Usage: python pcf_surface.py <renders_dir>

Note: Freq 64 is a low-cutoff control whose peak sits near the passband, so its
prominence/corner are not reliable; the defect and the fit live at Freq 96-122.
"""
import os, sys, glob, re, numpy as np, wave
from scipy.signal import welch
SR = 44100

def psd(path):
    w = wave.open(path); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    x = x.reshape(-1, w.getnchannels()).mean(1) if w.getnchannels() == 2 else x
    f, P = welch(x, SR, nperseg=8192, noverlap=4096); return f, P

def main(argv):
    d = argv[0]
    fr, Pref = psd(os.path.join(d, "srf_ref.wav"))
    pb = (fr >= 200) & (fr <= 800); band = fr > 1000
    cells = {}
    for p in glob.glob(os.path.join(d, "srf_q*_f*.wav")):
        m = re.search(r"srf_q(\d+)_f(\d+)", os.path.basename(p))
        q, f = int(m.group(1)), int(m.group(2))
        _, Pf = psd(p)
        T = 10 * np.log10((Pf + 1e-20) / (Pref + 1e-20))
        pk = T[band].max(); pf = fr[band][int(np.argmax(T[band]))]; prom = pk - np.median(T[pb])
        cells[(q, f)] = (prom, pf)
    QB = sorted({q for q, _ in cells}); FR = sorted({f for _, f in cells})
    print("PROMINENCE (dB above passband), Q band byte x Freq:")
    print("%-6s" % "Qbyte", " ".join("%7d" % f for f in FR))
    for q in QB:
        print("%-6d" % q, " ".join("%7.1f" % cells[(q, f)][0] for f in FR))
    print("\nCORNER (resonant peak Hz):")
    print("%-6s" % "Qbyte", " ".join("%7d" % f for f in FR))
    for q in QB:
        print("%-6d" % q, " ".join("%7.0f" % cells[(q, f)][1] for f in FR))

if __name__ == "__main__":
    main(sys.argv[1:])
