#!/usr/bin/env python3
"""Score a shipped change across EVERY fixture set we hold a ReBirth export for.

WHY IT EXISTS. Constants in this project get fitted on one batch and applied to
all of them, and the question that decides whether a fit is real is always the
same: what did it do to the fixtures it never saw? That has been answered by hand
each time, on whichever batch was in front of me, which is how a fit that is
right at one operating point and wrong away from it survives. This runs the whole
inventory at once, before and after, and prints where it got worse.

THE METRIC IS SPECTRAL SHAPE, DELIBERATELY NOT LEVEL. Each render is reduced to
its long-term spectrum in third-octave bands from 50 Hz to 10 kHz, each side
normalised by its OWN total band energy, and the score is the rms difference in
dB. So a gain error contributes nothing and what is measured is where the energy
sits, which is what a filter decides. A change that only made things louder will
score zero here, correctly.

It is a blunt instrument on purpose. It will not see a timing error, an envelope
SHAPE that arrives at the right average, or anything that cancels over the length
of a file. It is a regression net, not a parity measurement: use env_traj.py or
the batch-specific tools to understand a number, and this to find out which
numbers to go and understand.

STEREO throughout, pooled in power across channels, never mono-summed.

USAGE
    python regress_ab.py --before "" --after "REBORN_EM_OCT=4.45 REBORN_EM_FLOOR=0.85"

Each of --before and --after is a space-separated list of NAME=VALUE overrides
applied to the render, so the comparison is two settings of one binary rather
than two builds. An empty string means shipped defaults.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
EXE = os.path.join(REPO, "build", "Release", "reborn_render.exe")

# fixture directory -> ReBirth export directory, and which voice it solos.
SETS = [
    ("fixtures-2026-07-27/batchG",  "batchG_rb",  "bass1"),
    ("fixtures-2026-07-27/batchH",  "batchH_rb",  "bass1"),
    ("fixtures-2026-07-27/batchI",  "batchI_rb",  "bass1"),
    ("fixtures-2026-07-27/batchW",  "batchW_rb",  "bass1"),
    ("fixtures-2026-07-31/batchL",  "batchL_rb",  "bass1"),
    ("fixtures-2026-07-31/batchN",  "batchN_rb",  "bass1"),
    ("fixtures-2026-08-01/batchL2", "batchL2_rb", "bass1"),
    ("fixtures-2026-08-01/batchP",  "batchP_rb",  "bass1"),
    ("fixtures-2026-08-01/accentprobe", "accentprobe_rb", "bass1"),
    ("fixtures-2026-08-01/decayprobe",  "decayprobe_rb",  "bass1"),
    ("fixtures-2026-08-02/batchQ",      "batchQ_rb",      "bass1"),
    ("fixtures-2026-08-02/accentdepth", "accentdepth_rb", "bass1"),
    ("fixtures-2026-08-02/accentrate",  "accentrate_rb",  "bass1"),
    ("fixtures-2026-08-02/envshape",    "envshape_rb",    "bass1"),
    ("fixtures-2026-08-02/envdomain",   "envdomain_rb",   "bass1"),
    ("fixtures-2026-08-02/envfloor",    "envfloor_rb",    "bass1"),
]

EDGES = 50.0 * 2.0 ** (np.arange(0, 24) / 3.0)      # third-octaves, 50 Hz to 10 kHz


def read_stereo(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def band_shape(p):
    """Third-octave energies, normalised by their own sum, in dB. Returns None
    for a file with no signal, which is a real case: some fixtures are silent."""
    x, sr = read_stereo(p)
    n = min(len(x), int(8.0 * sr))
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


def score(rb_path, our_path):
    a, b = band_shape(rb_path), band_shape(our_path)
    if a is None or b is None:
        return None
    return float(np.sqrt(((a - b) ** 2).mean()))


def identical(a, b):
    """Are two renders bit-identical?"""
    try:
        with open(a, "rb") as f1, open(b, "rb") as f2:
            return f1.read() == f2.read()
    except OSError:
        return False


def render(song, out, solo, overrides):
    """Renders are CACHED on the override string. A parameter sweep re-renders
    the same baseline once per point otherwise, which doubled the cost of every
    scan and is pure waste: the settings fully determine the output."""
    if os.path.exists(out):
        return True
    env = dict(os.environ)
    for kv in overrides.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v
    r = subprocess.run([EXE, "--song", song, "--solo", solo, "--no-fx",
                        "--seconds", "8", "--out", out],
                       env=env, capture_output=True)
    return r.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--before", default="")
    ap.add_argument("--after", required=True)
    ap.add_argument("--tmp", required=True, help="scratch directory for renders")
    ap.add_argument("--only", default="")
    ap.add_argument("--detail", type=int, default=0,
                    help="print the N worst-regressed cells per set")
    a = ap.parse_args()
    os.makedirs(a.tmp, exist_ok=True)

    if a.before.split() == a.after.split():
        print("REFUSED: --before and --after are the same settings, so this can "
              "only report +0.000 and would not be evidence of anything.")
        return 2

    print(f"before: {a.before or '(shipped defaults)'}")
    print(f"after : {a.after}\n")
    print(f"{'fixture set':<34}{'n':>4}{'before':>9}{'after':>9}{'change':>9}"
          f"{'better':>8}{'worse':>7}")
    print("-" * 80)
    tot_b, tot_a, n_all, w_all = [], [], 0, 0
    n_same = n_pairs = 0        # renders that came out BIT-IDENTICAL
    for fx, rbdir, solo in SETS:
        if a.only and not any(s and s in fx for s in a.only.split(",")):
            continue
        fdir = os.path.join(WS, fx)
        rdir = os.path.join(WS, "renders", rbdir)
        if not (os.path.isdir(fdir) and os.path.isdir(rdir)):
            print(f"{os.path.basename(fx):<34}  missing fixture or export dir")
            continue
        sb, sa, names = [], [], []
        for fn in sorted(os.listdir(fdir)):
            if not fn.endswith(".rbs"):
                continue
            stem = fn[:-4]
            rb = os.path.join(rdir, stem + ".wav")
            if not os.path.exists(rb):
                rb = os.path.join(rdir, stem + ".WAV")
                if not os.path.exists(rb):
                    continue
            kb = hashlib.md5(a.before.encode()).hexdigest()[:8]
            ka = hashlib.md5(a.after.encode()).hexdigest()[:8]
            ob = os.path.join(a.tmp, f"{stem}__{kb}.wav")
            oa = os.path.join(a.tmp, f"{stem}__{ka}.wav")
            if not (render(os.path.join(fdir, fn), ob, solo, a.before) and
                    render(os.path.join(fdir, fn), oa, solo, a.after)):
                continue
            x, y = score(rb, ob), score(rb, oa)
            if x is None or y is None:
                continue
            n_pairs += 1
            if identical(ob, oa):
                n_same += 1
            sb.append(x); sa.append(y); names.append(stem)
        if a.detail and sb:
            order = np.argsort(-(np.array(sa) - np.array(sb)))
            print(f"    worst {min(a.detail, len(order))} cells in this set:")
            for i in order[:a.detail]:
                print(f"      {names[i]:<44}{sb[i]:>8.3f}{sa[i]:>8.3f}"
                      f"{sa[i]-sb[i]:>+8.3f}")
        if not sb:
            print(f"{os.path.basename(fx):<34}  no scorable pairs")
            continue
        sb, sa = np.array(sb), np.array(sa)
        better = int((sa < sb - 0.01).sum()); worse = int((sa > sb + 0.01).sum())
        tot_b += list(sb); tot_a += list(sa); n_all += len(sb); w_all += worse
        flag = "   <-- WORSE" if sa.mean() > sb.mean() + 0.01 else ""
        print(f"{os.path.basename(fx):<34}{len(sb):>4}{sb.mean():>9.3f}"
              f"{sa.mean():>9.3f}{sa.mean()-sb.mean():>+9.3f}{better:>8}{worse:>7}{flag}")
    tb, ta = np.array(tot_b), np.array(tot_a)
    print("-" * 80)
    print(f"{'ALL':<34}{n_all:>4}{tb.mean():>9.3f}{ta.mean():>9.3f}"
          f"{ta.mean()-tb.mean():>+9.3f}{int((ta<tb-0.01).sum()):>8}{w_all:>7}")
    print("\nunits are rms dB of third-octave spectral shape against ReBirth,")
    print("each side energy-normalised, so LOWER IS BETTER and level cancels.")
    # A NO-OP MUST NOT LOOK LIKE A RESULT. A clean +0.000 is exactly what a
    # change this net cannot see looks like, and it is also exactly what a
    # setting that never reached the renderer looks like. This project has been
    # fooled three times: by REBORN_BASS_ENGINE leaving every 303 constant
    # unreadable, by a MIXR chunk patch that the track overrode, and by a shell
    # quoting slip that sent identical overrides to both sides. So this checks
    # the AUDIO rather than trusting the arguments.
    if n_pairs and n_same == n_pairs:
        print()
        print("*** WARNING: all %d rendered pairs are BIT-IDENTICAL." % n_pairs)
        print("*** The setting under test changed NOTHING in the audio, so this")
        print("*** is a no-op and not a result. +0.000 here is not evidence.")
        print("*** The usual cause is a constant the rendered voice never reads:")
        print("*** the default 303 is Open303, so TPT constants need")
        print("*** REBORN_BASS_ENGINE=tpt on BOTH sides.")
    elif n_same:
        print()
        print("note: %d of %d rendered pairs were bit-identical, so the setting"
              % (n_same, n_pairs))
        print("does nothing at all on those fixtures.")


if __name__ == "__main__":
    main()
