#!/usr/bin/env python3
"""Stage an ear-check trio, level-matched to the ReBirth reference.

Kept as a tool because the ear-check builders have been written ad hoc
three times now and the LEVEL MATCH is the part that is easy to get wrong.
Two matching strategies, and which one is right depends on the change:

BAND-MATCH, on a band ABOVE the change, when the change is confined to one
region. renders/lowend_earcheck and renders/highpass_earcheck matched on
300 Hz to 5 kHz because the change was below it, so the match could not
cancel what was being judged.

WHOLE-FILE RMS, when the change has no such band. An accent moves both
brightness and level, across the spectrum, at particular MOMENTS. There is
no band above it. Whole-file rms equalises loudness so the ear compares the
RHYTHM rather than which file is louder, and it cannot cancel a pattern
that varies over time. It is approximate by construction, since changing
how many accents fire changes the whole-file rms a little.

Leaving level unmatched is not an option: it confounded the drive A/B
badly enough that the result had to be set aside.
"""

import os, sys, wave
import numpy as np

SP  = sys.argv[1]
OUT = os.path.abspath("../renders/accentrate_earcheck")
RBD = os.path.abspath("../renders/Luke_renders_07312026")
os.makedirs(OUT, exist_ok=True)

def rd(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr

def wr(p, x, sr):
    y = np.clip(x, -1.0, 1.0)
    with wave.open(p, "wb") as w:
        w.setnchannels(y.shape[1]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes((y * 32767.0).astype("<i2").tobytes())

print(f"{'track':<9}{'source':<12}{'gain dB':>9}{'peak':>8}")
for n in (1, 2):
    ref, sr = rd(os.path.join(RBD, f"Clodhopper-303-{n}-bars1-7.WAV"))
    tgt = float(np.sqrt(np.mean(ref ** 2)))
    wr(os.path.join(OUT, f"clodhopper_303-{n}__1rebirth.wav"), ref, sr)
    print(f"303-{n:<6}{'rebirth':<12}{0.0:>+9.2f}{np.abs(ref).max():>8.3f}")
    for tag, lbl in (("current", "2current"), ("fixed", "3accentfix")):
        x, sr2 = rd(os.path.join(SP, f"ar_303-{n}_{tag}.wav"))
        g = tgt / max(float(np.sqrt(np.mean(x ** 2))), 1e-12)
        y = x * g
        wr(os.path.join(OUT, f"clodhopper_303-{n}__{lbl}.wav"), y, sr2)
        print(f"303-{n:<6}{lbl:<12}{20*np.log10(g):>+9.2f}{np.abs(y).max():>8.3f}")
