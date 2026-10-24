#!/usr/bin/env python3
"""Per-band diagnostic of the net's own metric on the 303 listening songs.

WHY. Section 15 settled that the 102 Hz highpass is the wrong fix and that our
saw is 2 to 4 dB too dark on real music, missing HIGHS rather than carrying
excess lows. It left one number per song (groove saw 3.68, tour saw 4.00) and a
direction, but not the SHAPE of the deficit: which bands, how much, and whether a
candidate fix moves each band toward ReBirth or just shuffles the ratio. This
prints that shape.

WHAT IT IS. The metric is exactly regress_ab.py's `band_shape`: third-octave
energies 50 Hz to 10 kHz, each side normalised by its OWN total, in dB, read over
the first 8 s of the render (that 8 s cap is the net's, kept here so the overall
number reproduces section 15). Stereo, pooled in power, never mono-summed. A
per-band value is ours minus ReBirth in dB: POSITIVE means we have more of our
energy in that band than ReBirth has of its own, NEGATIVE means we are darker
there. The overall score is the rms across the 23 bands, which is the net number.

SCOPE CAVEAT. The 8 s cap means the tour rows describe only its reference riff
and the start of the cutoff sweep; the env-mod-on-slides and accent regimes live
past 8 s and this metric never sees them. Pass --full to score the whole song
too; the two are printed side by side so the difference is visible, not hidden.

ISOLATION. These songs are a single centre-panned bass1 with the rack off, the
same renders section 15 used. Rendered here with REBORN_BASS_ENGINE=tpt (the
active parity voice; the shipped default Open303 cannot see a TPT constant) and
--no-fx --solo bass1.

USAGE
    python song_highs.py
    python song_highs.py --after "REBORN_OSC_HP=102"
    python song_highs.py --after "REBORN_OSC_HS_GAIN=3 REBORN_OSC_HS_FC=1500" --full
"""
import argparse
import glob
import hashlib
import os
import subprocess
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
EXE = os.path.join(REPO, "build", "Release", "reborn_render.exe")
SET = os.path.join(WS, "listen-303-songs-2026-08-30")

# rbs stem, REBIRTH wav filename prefix, render seconds, label
SONGS = [
    ("t303_groove_saw",    "00 ", 30.0, "groove saw"),
    ("t303_groove_square", "01 ", 30.0, "groove square"),
    ("t303_tour_saw",      "02 ", 60.0, "tour saw"),
    ("t303_tour_square",   "03 ", 60.0, "tour square"),
]

EDGES = 50.0 * 2.0 ** (np.arange(0, 24) / 3.0)      # third-octaves, 50 Hz .. 10 kHz
CENTERS = np.sqrt(EDGES[:-1] * EDGES[1:])


def read_stereo(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def band_shape(p, seconds=8.0):
    """regress_ab.py's metric, with the read length exposed. dB per band,
    normalised by the render's own total band energy."""
    x, sr = read_stereo(p)
    n = min(len(x), int(seconds * sr))
    if n < 4096:
        return None
    F = 8192
    acc = np.zeros(F // 2 + 1)
    win = np.hanning(F)
    frames = 0
    for a in range(0, n - F, F // 2):
        for ch in range(x.shape[1]):
            acc += np.abs(np.fft.rfft(x[a:a + F, ch] * win)) ** 2
        frames += 1
    if frames == 0 or acc.sum() <= 0:
        return None
    f = np.fft.rfftfreq(F, 1.0 / sr)
    e = np.array([acc[(f >= EDGES[i]) & (f < EDGES[i + 1])].sum()
                  for i in range(len(EDGES) - 1)])
    if e.sum() <= 0:
        return None
    return 10.0 * np.log10(e / e.sum() + 1e-12)


def render(rbs, out, overrides):
    if os.path.exists(out):
        return True
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for kv in overrides.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v
    secs = next(s for stem, _, s, _ in SONGS if stem in rbs)
    r = subprocess.run([EXE, "--song", rbs, "--solo", "bass1", "--no-fx",
                        "--seconds", "%g" % secs, "--out", out],
                       env=env, capture_output=True)
    return r.returncode == 0


def rebirth_wav(prefix):
    hits = glob.glob(os.path.join(SET, prefix + "*REBIRTH.wav"))
    return hits[0] if hits else None


def score_and_bands(rb, ours, seconds):
    a, b = band_shape(rb, seconds), band_shape(ours, seconds)
    if a is None or b is None:
        return None, None
    diff = b - a
    return float(np.sqrt((diff ** 2).mean())), diff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--after", default="", help="override string to compare against shipped")
    ap.add_argument("--tmp", default=os.path.join(WS, "_song_highs_tmp"))
    ap.add_argument("--full", action="store_true", help="also score the whole song, not just the net's 8 s")
    a = ap.parse_args()
    os.makedirs(a.tmp, exist_ok=True)

    windows = [("8 s (net)", 8.0)] + ([("full song", 999.0)] if a.full else [])
    kb = hashlib.md5(b"").hexdigest()[:8]
    ka = hashlib.md5(a.after.encode()).hexdigest()[:8]

    print("metric: regress_ab band_shape, ours minus ReBirth in dB per third-octave.")
    print("POSITIVE = we sit brighter/more-energy in that band; NEGATIVE = darker.\n")

    for stem, prefix, _secs, label in SONGS:
        rbs = os.path.join(SET, stem + ".rbs")
        rb = rebirth_wav(prefix)
        if not (os.path.exists(rbs) and rb):
            print(f"{label}: missing rbs or ReBirth wav"); continue
        ob = os.path.join(a.tmp, f"{stem}__{kb}.wav")
        render(rbs, ob, "")
        oa = None
        if a.after:
            oa = os.path.join(a.tmp, f"{stem}__{ka}.wav")
            render(rbs, oa, a.after)

        print(f"=== {label} ===")
        for wname, secs in windows:
            sb, db_b = score_and_bands(rb, ob, secs)
            line = f"  {wname:<10} shipped rms {sb:5.2f}"
            db_a = None
            if oa:
                sa, db_a = score_and_bands(rb, oa, secs)
                line += f"   after rms {sa:5.2f}   change {sa - sb:+5.2f}"
            print(line)
            print("    Hz    " + "".join(f"{int(c):>6d}" for c in CENTERS))
            print("    ship  " + "".join(f"{v:>6.1f}" for v in db_b))
            if db_a is not None:
                print("    after " + "".join(f"{v:>6.1f}" for v in db_a))
        print()


if __name__ == "__main__":
    main()
