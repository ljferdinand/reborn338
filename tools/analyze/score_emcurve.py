#!/usr/bin/env python3
"""Score one env-mod-corner candidate on the B9 high-env-mod corner AND the
guardrails (static net + tour songs), as JSON.

The objective this adds over score_candidate.py is the B9 plain-note corner
(BASS303_FINDINGS section 26): at the tour's ACCENT corner (cutoff 60, reso 85,
saw) our env-mod corner runs darker than ReBirth and the shortfall grows with env
mod. A shippable candidate CLOSES that gap at env mod 90 and 127 (the tour region)
while the net stays at or below zero and the tour songs do not regress.

One candidate is a space-separated override like "REBORN_EM_OCT=4.85
REBORN_EM_CURVE=0.4". Renders the three B9 plain fixtures OURS with that override
(REBORN_BASS_ENGINE=tpt), measures the centroid/rolloff gap against the ReBirth
B9 exports on disk, then shells out to score_candidate.py for the net and songs.
Prints one JSON object.

USAGE
  python score_emcurve.py "REBORN_EM_OCT=4.85 REBORN_EM_CURVE=0.4" <tmpbase>
"""
import argparse
import json
import os
import subprocess
import sys
import numpy as np
from scipy.io import wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
WS = os.path.dirname(REPO)
RENDER = os.path.join(REPO, "build", "Release", "reborn_render.exe")
RB_B9 = os.path.join(WS, "renders", "b9-303-accent-corner-2026-09-04")
FX_B9 = os.path.join(WS, "fixtures-2026-09-04", "b9-303-accent-corner")

SR = 44100
BAR_S = 4 * 60.0 / 130.0
STEP_S = BAR_S / 16.0
ACC_STEPS = (4, 12)
BARS = 4
EMS = (64, 90, 127)


def load_mono(path):
    _, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim == 2:
        x = x.mean(axis=1)
    return x / 32768.0


def onset_abs(x, thr=0.004):
    return int(np.argmax(np.abs(x) > thr))


def metrics(seg):
    w = np.hanning(len(seg))
    X = np.abs(np.fft.rfft(seg * w))
    f = np.fft.rfftfreq(len(seg), 1.0 / SR)
    m = (f >= 20) & (f <= 20000)
    f, X = f[m], X[m]
    tot = X.sum()
    if tot <= 0:
        return np.nan, np.nan
    cent = (f * X).sum() / tot
    roll = f[np.searchsorted(np.cumsum(X), 0.85 * tot)]
    return cent, roll


def plain_metrics(path):
    x = load_mono(path)
    off = onset_abs(x)
    vals = []
    for bar in range(BARS):
        for st in ACC_STEPS:
            t0 = bar * BAR_S + st * STEP_S
            a = off + int((t0 + 0.002) * SR)
            b = off + int((t0 + STEP_S) * SR)
            if b <= len(x):
                vals.append(metrics(x[a:b]))
    return np.nanmean(np.array(vals), axis=0)


def b9_corner(ov, tmp):
    """Render our B9 plain fixtures with the override, return gaps ours-minus-RB
    (negative = ours darker) per env mod, centroid and rolloff."""
    os.makedirs(tmp, exist_ok=True)
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for kv in ov.split():
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v
    gaps = {}
    for em in EMS:
        fx = os.path.join(FX_B9, "b9_em%03d_plain.rbs" % em)
        outp = os.path.join(tmp, "b9_em%03d_plain.wav" % em)
        subprocess.run([RENDER, "--song", fx, "--solo", "bass1", "--no-fx",
                        "--seconds", "8", "--out", outp], env=env,
                       capture_output=True)
        oc, orl = plain_metrics(outp)
        rc, rrl = plain_metrics(os.path.join(RB_B9, "b9_em%03d_plain.wav" % em))
        gaps[em] = {"cent_gap": round(oc - rc, 1), "roll_gap": round(orl - rrl, 1),
                    "our_cent": round(oc, 1), "rb_cent": round(rc, 1)}
    return gaps


def score_net_songs(ov, tmp, only=""):
    cmd = [sys.executable, os.path.join(HERE, "score_candidate.py"), ov, tmp]
    if only:
        cmd += ["--only", only]
    out = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO)
    for line in out.stdout.splitlines():
        line = line.strip()
        if line.startswith("{"):
            return json.loads(line)
    return {"error": "score_candidate produced no json", "raw": out.stdout[-400:] + out.stderr[-400:]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("overrides")
    ap.add_argument("tmpbase")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.tmpbase, exist_ok=True)
    gaps = b9_corner(a.overrides, os.path.join(a.tmpbase, "b9"))
    ns = score_net_songs(a.overrides, os.path.join(a.tmpbase, "ns"), a.only)
    # tour-region objective: mean centroid deficit at em90 and em127 (smaller is
    # better; positive means ours still darker).
    obj = round(-(gaps[90]["cent_gap"] + gaps[127]["cent_gap"]) / 2.0, 1)
    print(json.dumps({
        "overrides": a.overrides,
        "b9_obj_darkHz": obj,
        "b9": gaps,
        "net_change": ns.get("net_change"),
        "worst_sets": ns.get("worst_sets"),
        "songs": ns.get("songs"),
    }))


if __name__ == "__main__":
    main()
