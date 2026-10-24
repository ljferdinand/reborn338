#!/usr/bin/env python3
"""Localise the tour's treble deficit to the KNOB that causes it.

song_highs.py showed the tour saw is a broadband treble rolloff over the whole
song (fine below 1 kHz, growing to -12 dB by 7 kHz), but the tour walks one knob
at a time across eight four-bar sections, so the full-song number averages the
reference riff, the cutoff sweep, the resonance sweep, the env-mod-on-slides
section, the decay sweep, the accent sweep and two payoff sections together.

This windows each section and prints the per-band deficit (ours minus ReBirth, dB,
each side self-normalised over that section) so the deficit can be attributed:
a deficit that lives in the CUTOFF-open section points at the oscillator/filter
letting too few highs through; one in the ENV-MOD or DECAY sections points at the
envelope; one in the ACCENT section points at the accent. Same metric as
regress_ab/song_highs, stereo, rack off, TPT voice.

USAGE
    python tour_sections.py
    python tour_sections.py --after "REBORN_OSC_HS_GAIN=3 REBORN_OSC_HS_FC=1500"
    python tour_sections.py --wave square
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

BAR = 60.0 / 130.0 * 4.0            # 1.84615 s at 130 BPM
# start bar, label (from TIMELINE.md / gen_303_song TOUR)
SECTIONS = [
    (0,  "reference riff"),
    (4,  "CUTOFF 12->118"),
    (8,  "RESONANCE 0->127 @cut60"),
    (12, "ENVMOD 0->127 slides @reso85"),
    (16, "DECAY 0->127 @envmod90"),
    (20, "ACCENT 0->127 @decay55"),
    (24, "octaves/slides held"),
    (28, "everything up"),
]

EDGES = 50.0 * 2.0 ** (np.arange(0, 24) / 3.0)
CENTERS = np.sqrt(EDGES[:-1] * EDGES[1:])


def read_stereo(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def band_shape_range(x, sr, t0, t1):
    a0, a1 = int(t0 * sr), int(t1 * sr)
    seg = x[a0:min(a1, len(x))]
    if len(seg) < 8192:
        return None
    F = 8192
    acc = np.zeros(F // 2 + 1)
    win = np.hanning(F)
    frames = 0
    for a in range(0, len(seg) - F, F // 2):
        for ch in range(seg.shape[1]):
            acc += np.abs(np.fft.rfft(seg[a:a + F, ch] * win)) ** 2
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
    r = subprocess.run([EXE, "--song", rbs, "--solo", "bass1", "--no-fx",
                        "--seconds", "60", "--out", out], env=env, capture_output=True)
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wave", default="saw", choices=["saw", "square"])
    ap.add_argument("--after", default="")
    ap.add_argument("--tmp", default=os.path.join(WS, "_song_highs_tmp"))
    a = ap.parse_args()
    os.makedirs(a.tmp, exist_ok=True)

    stem = "t303_tour_" + a.wave
    prefix = "02 " if a.wave == "saw" else "03 "
    rbs = os.path.join(SET, stem + ".rbs")
    rb = glob.glob(os.path.join(SET, prefix + "*REBIRTH.wav"))[0]
    xr, sr = read_stereo(rb)

    kb = hashlib.md5(b"").hexdigest()[:8]
    ob = os.path.join(a.tmp, f"{stem}__{kb}.wav")
    render(rbs, ob, "")
    xb, _ = read_stereo(ob)
    xa = None
    if a.after:
        ka = hashlib.md5(a.after.encode()).hexdigest()[:8]
        oa = os.path.join(a.tmp, f"{stem}__{ka}.wav")
        render(rbs, oa, a.after)
        xa, _ = read_stereo(oa)

    print(f"tour {a.wave}: ours minus ReBirth in dB per third-octave, per section.")
    print("NEGATIVE = we are darker than ReBirth in that band, for that knob.\n")
    print("    Hz       " + "".join(f"{int(c):>6d}" for c in CENTERS) + "   rms")
    for bar0, label in SECTIONS:
        t0, t1 = bar0 * BAR, (bar0 + 4) * BAR
        dr = band_shape_range(xr, sr, t0, t1)
        db = band_shape_range(xb, sr, t0, t1)
        if dr is None or db is None:
            continue
        diff = db - dr
        rms = float(np.sqrt((diff ** 2).mean()))
        print(f"{label[:20]:<20}" + "".join(f"{v:>6.1f}" for v in diff) + f"  {rms:5.2f}")
        if xa is not None:
            da = band_shape_range(xa, sr, t0, t1)
            diff2 = da - dr
            rms2 = float(np.sqrt((diff2 ** 2).mean()))
            print(f"{'  after':<20}" + "".join(f"{v:>6.1f}" for v in diff2) + f"  {rms2:5.2f}")


if __name__ == "__main__":
    main()
