#!/usr/bin/env python3
"""Phase-invert null (ours shipped vs candidate) plus magnitude diff vs ReBirth.

Two different comparisons, because they are not equally valid:

1. NULL, ours shipped MINUS ours candidate, in the SAMPLE DOMAIN. Our TPT voice
   resets phase deterministically, so two renders of the same settings are
   bit-identical and the inverted sum of shipped and candidate is EXACTLY what the
   fix changed, with everything they share cancelled to zero. This is the
   meaningful phase-invert-and-sum. The tool verifies determinism first (renders
   shipped twice and checks the null is silent) so the residual is trusted.

2. MAGNITUDE vs ReBirth. A sample-domain null against ReBirth is meaningless:
   ReBirth does not reset oscillator/noise phase per render, so its exports are
   spectrally identical but sample-wise uncorrelated, and inverting-and-summing
   nulls nothing (docs/MEASUREMENT_PROTOCOL.md, "Never null-test against
   ReBirth"). The only
   valid cross-ReBirth view is magnitude: per-band energy, each side normalised to
   its own total, ours minus ReBirth, for shipped and candidate side by side.

So (1) says what the fix DID, in isolation; (2) says whether it moved us TOWARD
ReBirth. Stereo, pooled in power.

USAGE
  python null_diff.py --song t303_groove_saw --after "REBORN_FC_MAX=9000 REBORN_EM_CENTRE=0.45"
  python null_diff.py --song t303_tour_saw  --after "..." --sections
"""
import argparse
import os
import subprocess
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
EXE = os.path.join(REPO, "build", "Release", "reborn_render.exe")
RB = os.path.join(WS, "renders", "303-songs-2026-08-30")
FX = os.path.join(WS, "fixtures-2026-08-30", "303-songs")
TMP = os.path.join(WS, "_null_tmp")

EDGES = 50.0 * 2.0 ** (np.arange(0, 24) / 3.0)
CENTERS = np.sqrt(EDGES[:-1] * EDGES[1:])
BAR = 60.0 / 130.0 * 4.0
TOUR_SECT = [(0, "riff"), (4, "CUTOFF"), (8, "RESO"), (12, "ENVMOD"),
             (16, "DECAY"), (20, "ACCENT"), (24, "octaves"), (28, "all-up")]


def read(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def render(stem, secs, ov, tag=None):
    os.makedirs(TMP, exist_ok=True)
    env = dict(os.environ); env["REBORN_BASS_ENGINE"] = "tpt"
    for kv in ov.split():
        if "=" in kv:
            k, v = kv.split("=", 1); env[k] = v
    if tag is None:
        tag = "s" if not ov else "c" + str(abs(hash(ov)) % 10000)
    out = os.path.join(TMP, f"{stem}_{tag}.wav")
    subprocess.run([EXE, "--song", os.path.join(FX, stem + ".rbs"), "--no-fx",
                    "--seconds", "%g" % secs, "--out", out], env=env,
                   capture_output=True)
    return out


def bands(x, sr, a=0, b=None):
    seg = x[a:b] if b else x[a:]
    F = 8192; win = np.hanning(F); acc = np.zeros(F // 2 + 1); fr = 0
    for i in range(0, len(seg) - F, F // 2):
        for ch in range(seg.shape[1]):
            acc += np.abs(np.fft.rfft(seg[i:i + F, ch] * win)) ** 2
        fr += 1
    if fr == 0 or acc.sum() <= 0:
        return None
    f = np.fft.rfftfreq(F, 1.0 / sr)
    e = np.array([acc[(f >= EDGES[i]) & (f < EDGES[i + 1])].sum() for i in range(len(EDGES) - 1)])
    return e


def rms(x):
    return float(np.sqrt(np.mean(x ** 2)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--song", default="t303_groove_saw")
    ap.add_argument("--after", required=True)
    ap.add_argument("--sections", action="store_true")
    a = ap.parse_args()
    secs = 60.0 if "tour" in a.song else 30.0

    ship = render(a.song, secs, "", tag="s")
    ship2 = render(a.song, secs, "", tag="sdup")   # same settings, second render
    cand = render(a.song, secs, a.after)
    xs, sr = read(ship); xc, _ = read(cand)
    n = min(len(xs), len(xc)); xs, xc = xs[:n], xc[:n]

    # 1. determinism: two independent shipped renders must be bit-identical, or the
    # sample-domain null below is not valid.
    import filecmp
    det = filecmp.cmp(ship, ship2, shallow=False)
    print(f"=== {a.song}   candidate: {a.after} ===")
    print(f"determinism check (shipped rendered twice, bit-identical): {det}")

    # 2. NULL residual, ours shipped minus candidate
    resid = xs - xc
    lvl = 20 * np.log10(max(rms(resid), 1e-12) / max(rms(xs), 1e-12))
    print(f"\nNULL  ours shipped MINUS candidate (this is exactly what the fix changed):")
    print(f"  residual level {lvl:+.1f} dB relative to the signal "
          f"({'a large change' if lvl > -20 else 'a small change' if lvl > -40 else 'tiny'})")
    er = bands(resid, sr); es = bands(xs, sr)
    if er is not None:
        rel = 10 * np.log10(er / es.sum() + 1e-20)   # residual band energy vs total signal
        print("  where the change sits, residual band energy dB below the whole signal:")
        print("   Hz " + "".join(f"{int(c):>6d}" for c in CENTERS))
        print("      " + "".join(f"{v:6.0f}" for v in rel))

    # 3. MAGNITUDE vs ReBirth (sample null against ReBirth is invalid)
    rbp = os.path.join(RB, a.song + ".wav")
    if os.path.exists(rbp):
        xr, _ = read(rbp)
        def norm(x):
            e = bands(x, sr); return 10 * np.log10(e / e.sum() + 1e-20)
        dr, ds, dc = norm(xr), norm(xs[:len(xr)] if len(xr) < n else xs), norm(xc[:len(xr)] if len(xr) < n else xc)
        print("\nMAGNITUDE vs ReBirth (ours minus ReBirth per band, self-normalised):")
        print("   Hz " + "".join(f"{int(c):>6d}" for c in CENTERS))
        print("  ship" + "".join(f"{v:6.1f}" for v in (ds - dr)))
        print("  cand" + "".join(f"{v:6.1f}" for v in (dc - dr)))
        print("  gain" + "".join(f"{abs(s - r) - abs(c - r):+6.1f}" for s, c, r in zip(ds, dc, dr))
              + "   (+ = candidate closer to ReBirth in that band)")

    # 4. section-resolved null for the tour
    if a.sections and "tour" in a.song:
        print("\nNULL residual by tour section (dB below that section's signal):")
        for bar0, name in TOUR_SECT:
            i0, i1 = int(bar0 * BAR * sr), int((bar0 + 4) * BAR * sr)
            rr = resid[i0:min(i1, n)]; ss = xs[i0:min(i1, n)]
            if len(ss):
                print(f"  {name:<8} {20*np.log10(max(rms(rr),1e-12)/max(rms(ss),1e-12)):+6.1f} dB")


if __name__ == "__main__":
    main()
