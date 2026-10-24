#!/usr/bin/env python3
"""909 A/B scorer. One method, both sides (see docs/MEASUREMENT_PROTOCOL.md).

For each fixture it renders OUR output (the sample path by default, the clean-room
synthesis path under --synth) and loads the matching ReBirth export, extracts the
step-locked hits (a fixture is a 4-bar 120 BPM loop with hits at steps 0/4/8/12, so
16 identical hits at 0.5 s spacing), POWER-averages them, and reports:

  level_off  RMS(ours) - RMS(ReBirth), dB, reported SEPARATELY so a level offset
             cannot masquerade as a spectral error
  dom        dominant frequency (FFT argmax in --domband), for pitch / tune
  centroid   magnitude-weighted spectral centroid (Welch PSD)
  rolloff85  85% magnitude-energy rolloff

Band energies are normalised to each file's own total. Left channel by default
(these fixtures are mono-panned drum voices); --stereo reports both sides.

Usage:
  score909.py d909_ltt_000 d909_ltt_064 d909_ltt_127        # a sweep
  score909.py --domband 60,300 d909_mtt_000 d909_mtt_127
"""
import os, sys, subprocess, struct, argparse, math
import numpy as np
from scipy.signal import welch

SR      = 44100
BPM     = 120.0
STEP    = 60.0 / BPM / 4.0            # 16th note seconds (0.125 s)
HITS    = [0, 4, 8, 12]
BARS    = 4
WORKDIR = os.path.dirname(os.path.abspath(__file__))
# The workspace, inferred from this file's location. That inference is wrong when
# the repo is checked out somewhere else (a git worktree, say), so every path
# derived from it is overridable by environment variable:
#   SCORE909_ROOT     the workspace holding fixtures-*/ and renders/
#   SCORE909_REBORN   the reborn_render binary to drive
#   SCORE909_SAMPLES  the 909 sample set (sample path only; --synth ignores it)
#   SCORE909_FXDIR    the .rbs fixture directory for this batch
#   SCORE909_RBEXP    the matching ReBirth export directory
ROOT    = os.environ.get("SCORE909_ROOT") or os.path.abspath(os.path.join(WORKDIR, "..", "..", ".."))
REBORN  = os.environ.get("SCORE909_REBORN") or os.path.join(ROOT, "reborn", "build", "Release", "reborn_render.exe")
SAMPLES = os.environ.get("SCORE909_SAMPLES") or os.path.join(ROOT, "samples-standard-909")
# Default to the 2026-08-17 completeness batch; override for a later batch.
FXDIR   = os.environ.get("SCORE909_FXDIR") or os.path.join(ROOT, "fixtures-2026-08-25", "909complete")
RBEXP   = os.environ.get("SCORE909_RBEXP") or os.path.join(ROOT, "renders", "fixtures-2026-08-25", "909complete")

def load_wav_left(path):
    d = open(path, "rb").read()
    i = d.find(b"data")
    n = struct.unpack("<I", d[i+4:i+8])[0]
    a = np.frombuffer(d[i+8:i+8+n], dtype="<i2").astype(np.float64) / 32768.0
    return a[0::2], a[1::2]           # L, R (stereo interleaved)

def render_ours(fixture, out, synth=False):
    """Render our side. With synth=True the sample set is NOT passed, so every
    voice falls through to the clean-room synthesis model (a loaded sample wins
    over it, so the only way to exercise the synth path is to load nothing).
    That is the mode for supertask 2's synth pass; the ReBirth side is the same
    export either way, since ReBirth is always its sample player."""
    cmd = [REBORN, "--song", os.path.join(FXDIR, fixture + ".rbs"), "--solo", "909"]
    if not synth:
        cmd += ["--samples", SAMPLES]
    cmd += ["--no-fx", "--seconds", "8", "--out", out]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def hit_windows(x, win=0.45, positions=None):
    """The step-locked hit windows, kept SEPARATE (not summed). ReBirth's hits are
    not sample-aligned, so summing them coherently cancels a tonal or HF voice's
    energy and reads it far too quiet (the "never null-test" trap in
    docs/MEASUREMENT_PROTOCOL.md). Every
    metric here averages in the POWER domain across these windows instead."""
    w = int(win * SR)
    if positions is None:
        positions = [(bar * 16 + st) * STEP for bar in range(BARS) for st in HITS]
    out = []
    for t0 in positions:
        t = int(t0 * SR)
        if t + w <= len(x):
            out.append(x[t:t+w])
    return out or [x[:w]]

def metrics(x, domband, win=0.45, positions=None):
    wins = hit_windows(x, win, positions)
    # Level: power-average the per-hit RMS (no coherent cancellation).
    ms = [np.mean(h**2) for h in wins]
    rms = math.sqrt(np.mean(ms)) + 1e-12
    peak = max(np.max(np.abs(h)) for h in wins) + 1e-12
    # Spectrum: average the per-hit PSDs in the power domain, then read metrics.
    P = None
    for h in wins:
        f, p = welch(h, SR, nperseg=8192, noverlap=4096)
        P = p if P is None else P + p
    P /= len(wins)
    ptot = np.sum(P) + 1e-20
    centroid = float(np.sum(f * P) / ptot)
    rolloff = float(f[np.searchsorted(np.cumsum(P), 0.85 * ptot)])
    lo, hi = domband
    band = (f >= lo) & (f <= hi)
    dom = float(f[band][np.argmax(P[band])]) if np.any(band) else 0.0
    return dict(peak_db=20*math.log10(peak), rms_db=10*math.log10(np.mean(ms) + 1e-20),
                dom=dom, centroid=centroid, rolloff=rolloff)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fixtures", nargs="+")
    ap.add_argument("--domband", default="40,400", help="Hz lo,hi for dominant-freq search")
    ap.add_argument("--stereo", action="store_true")
    ap.add_argument("--wide", action="store_true", help="fixtures with one hit per bar (step 0), 2 s apart")
    ap.add_argument("--win", type=float, default=0.45, help="per-hit window seconds")
    ap.add_argument("--synth", action="store_true",
                    help="render OUR side with no sample set, exercising the synthesis path")
    a = ap.parse_args()
    lo, hi = (float(v) for v in a.domband.split(","))
    tmp = os.environ.get("TEMP", "/tmp")
    positions = ([bar * 16 * STEP for bar in range(BARS)] if a.wide else None)

    print(f"{'fixture':22} {'side':4} | {'RB dom':>8} {'our dom':>8} | "
          f"{'RB cent':>8} {'our cent':>8} | {'RB roll':>8} {'our roll':>8} | {'lvl_off':>8}")
    print("-" * 108)
    for fx in a.fixtures:
        # Export extension case varies by batch (the later batches were saved as
        # .wav, the older hand-made ones as .WAV), and Windows is case-insensitive but
        # this also has to work if the tree is ever read on a case-sensitive FS.
        rb_path = next((p for p in (os.path.join(RBEXP, fx + e) for e in (".wav", ".WAV"))
                        if os.path.exists(p)), None)
        if rb_path is None:
            print(f"{fx:22}  (no ReBirth export)"); continue
        ourwav = os.path.join(tmp, "score909_" + ("syn_" if a.synth else "") + fx + ".wav")
        render_ours(fx, ourwav, synth=a.synth)
        rbL, rbR = load_wav_left(rb_path)
        ouL, ouR = load_wav_left(ourwav)
        sides = [("L", rbL, ouL)] + ([("R", rbR, ouR)] if a.stereo else [])
        for side, rb, ou in sides:
            m_rb = metrics(rb, (lo, hi), a.win, positions)
            m_ou = metrics(ou, (lo, hi), a.win, positions)
            print(f"{fx:22} {side:4} | {m_rb['dom']:8.1f} {m_ou['dom']:8.1f} | "
                  f"{m_rb['centroid']:8.0f} {m_ou['centroid']:8.0f} | "
                  f"{m_rb['rolloff']:8.0f} {m_ou['rolloff']:8.0f} | "
                  f"{m_ou['rms_db']-m_rb['rms_db']:+8.2f}")

if __name__ == "__main__":
    main()
