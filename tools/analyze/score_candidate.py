#!/usr/bin/env python3
"""Score one filter-refit candidate on the static net and the songs, as JSON.

The scoring harness for the reso-aware corner refit (BASS303_FINDINGS section 20).
One candidate is a space-separated override string like
"REBORN_FC_MAX=9000 REBORN_CUT_SCALE=1.0 REBORN_RESO_OCT=0.15". This runs
regress_ab (the static fixture net) and song_highs (the four 303 songs) for it,
both with REBORN_BASS_ENGINE=tpt on both sides, and prints one JSON object:

  net_before, net_after, net_change   the 373-cell (or --only subset) net
  worst_sets                          sets that regressed by > 0.05, name:change
  songs                               per song {ship, after, change}, full length

A shippable candidate LOWERS the songs (especially tour_saw, tour_square) while
net_change stays at or below zero. Level cancels in both metrics.

USAGE
  python score_candidate.py "REBORN_FC_MAX=9000 REBORN_EM_CENTRE=0.40" <tmpbase>
  python score_candidate.py "<ov>" <tmpbase> --only batchH,batchI,batchQ,envfloor,envdomain
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO)
    return r.stdout + r.stderr


def score_net(ov, tmp, only):
    cmd = [sys.executable, os.path.join(HERE, "regress_ab.py"),
           "--before", "REBORN_BASS_ENGINE=tpt",
           "--after", "REBORN_BASS_ENGINE=tpt " + ov,
           "--tmp", tmp]
    if only:
        cmd += ["--only", only]
    out = run(cmd)
    net_before = net_after = net_change = None
    worst = []
    for line in out.splitlines():
        m = re.match(r"^ALL\s+\d+\s+([\d.]+)\s+([\d.]+)\s+([+-][\d.]+)", line)
        if m:
            net_before, net_after, net_change = (float(m.group(1)),
                                                 float(m.group(2)), float(m.group(3)))
        # per-set: name n before after change better worse
        m2 = re.match(r"^(\w[\w.-]*)\s+\d+\s+[\d.]+\s+[\d.]+\s+([+-][\d.]+)\s+\d+\s+\d+", line)
        if m2 and m2.group(1) != "ALL":
            ch = float(m2.group(2))
            if ch > 0.05:
                worst.append([m2.group(1), ch])
    worst.sort(key=lambda x: -x[1])
    return net_before, net_after, net_change, worst


def score_songs(ov, tmp):
    cmd = [sys.executable, os.path.join(HERE, "song_highs.py"),
           "--full", "--after", ov, "--tmp", tmp]
    out = run(cmd)
    songs = {}
    cur = None
    for line in out.splitlines():
        m = re.match(r"^=== (.+) ===", line)
        if m:
            cur = m.group(1).strip().replace(" ", "_")
        m2 = re.search(r"full song\s+shipped rms\s+([\d.]+)\s+after rms\s+([\d.]+)\s+change\s+([+-][\d.]+)", line)
        if m2 and cur:
            songs[cur] = {"ship": float(m2.group(1)), "after": float(m2.group(2)),
                          "change": float(m2.group(3))}
    return songs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("overrides")
    ap.add_argument("tmpbase")
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.tmpbase, exist_ok=True)
    nb, na, nc, worst = score_net(a.overrides, os.path.join(a.tmpbase, "net"), a.only)
    songs = score_songs(a.overrides, os.path.join(a.tmpbase, "songs"))
    print(json.dumps({
        "overrides": a.overrides,
        "net_before": nb, "net_after": na, "net_change": nc,
        "worst_sets": worst[:6],
        "songs": songs,
    }))


if __name__ == "__main__":
    main()
