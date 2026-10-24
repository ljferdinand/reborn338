#!/usr/bin/env python3
"""Prove that a change alters NO sound, by rendering the shipped songs twice.

WHY THIS IS A TOOL. Every default-off probe added to this project makes the same
claim, that the shipped build is unchanged, and that claim has been checked by a
hand-written shell loop each time. It is the single most repeated verification
here and the easiest one to do sloppily: render half the songs, or only bass1, or
forget --no-fx, and the check passes for the wrong reason.

WHAT IT RENDERS. All sixteen ReBirth demo songs, each soloed to bass1 AND bass2,
with the effect rack off. Two soloes rather than one because the two 303s carry
different knob settings and different patterns in every song, so the second
doubles the operating points covered for the price of a second render. Song mode
plays the arrangement, so knob automation sweeps the controls through their
ranges rather than sitting at one point.

WHAT IT PROVES AND WHAT IT DOES NOT. It proves that the code paths those songs
exercise produce identical bytes. It cannot prove that a path they never take is
unchanged, so a probe that only fires at, say, env mod 127 with a tie would be
invisible here. Read the diff as well; this is the second check, not the only one.

WHERE THE SONGS COME FROM. The demo songs ship with ReBirth, not with this
repository. Set REBORN_SONGS_DIR to your own install's "Demo Songs" folder, or
pass --songs; the tool stops with a message when neither is given.

USAGE. Snapshot before the change, snapshot after, compare:

    git stash                                     # or check out the base commit
    cmake --build build --config Release
    python tools/render/byte_neutral.py snap /tmp/base
    git stash pop
    cmake --build build --config Release
    python tools/render/byte_neutral.py snap /tmp/head
    python tools/render/byte_neutral.py diff /tmp/base /tmp/head

`snap` prints a pooled hash of the whole set. Two runs that print the same pooled
hash are byte-identical and `diff` has nothing to add. `diff` exists for when
they are NOT, because then the question is immediately which songs moved and by
how much, and "all sixteen" and "one, by 0.0001 dB" are different findings.

Environment REBORN_* overrides are passed through, so this also answers "does
the probe do anything when switched ON", which is worth running too: a probe that
is byte-neutral in both positions is not wired up.
"""
import argparse
import concurrent.futures
import glob
import hashlib
import json
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))

# The demo songs ship with ReBirth and are not in this repository. Point
# REBORN_SONGS_DIR at your own install's "Demo Songs" folder, or pass --songs.
SONGS_DIR = os.environ.get("REBORN_SONGS_DIR")
SOLOS = ("bass1", "bass2")


def default_exe():
    for rel in ("build/Release/reborn_render.exe", "build/reborn_render",
                "build/Release/reborn_render"):
        p = os.path.join(REPO, rel)
        if os.path.exists(p):
            return p
    return None


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read_wav(path):
    """Minimal 16-bit PCM reader. Returns (nchan, [samples interleaved])."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"not a RIFF/WAVE file: {path}")
    pos, nchan, bits, pcm = 12, 0, 0, b""
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        sz = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + sz]
        if cid == b"fmt ":
            nchan = struct.unpack("<H", body[2:4])[0]
            bits = struct.unpack("<H", body[14:16])[0]
        elif cid == b"data":
            pcm = body
        pos += 8 + sz + (sz & 1)
    if bits != 16:
        raise ValueError(f"expected 16-bit PCM, got {bits}: {path}")
    n = len(pcm) // 2
    return nchan, struct.unpack(f"<{n}h", pcm[:n * 2])


def snap(a):
    exe = a.exe or default_exe()
    if not exe or not os.path.exists(exe):
        sys.exit("reborn_render not found; build first, or pass --exe")
    if not a.songs:
        sys.exit("no demo-song folder: set REBORN_SONGS_DIR to your ReBirth "
                 "install's Demo Songs folder, or pass --songs")
    songs = sorted(glob.glob(os.path.join(a.songs, "*.rbs")))
    if not songs:
        sys.exit(f"no .rbs songs under {a.songs}")
    os.makedirs(a.outdir, exist_ok=True)

    jobs = []
    for s in songs:
        stem = os.path.splitext(os.path.basename(s))[0].replace(" ", "_")
        for solo in SOLOS:
            out = os.path.join(a.outdir, f"{stem}__{solo}.wav")
            jobs.append((s, solo, out))

    def run(job):
        song, solo, out = job
        cmd = [exe, "--song", song, "--solo", solo, "--no-fx",
               "--seconds", str(a.seconds), "--out", out]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return (out, None, r.stderr.strip()[:200])
        return (out, md5(out), None)

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for res in ex.map(run, jobs):
            results.append(res)

    fails = [r for r in results if r[1] is None]
    for out, _, err in fails:
        print(f"  FAILED {os.path.basename(out)}: {err}")
    if fails:
        sys.exit(f"{len(fails)} of {len(jobs)} renders failed; snapshot is not usable")

    manifest = {os.path.basename(o): h for o, h, _ in results}
    with open(os.path.join(a.outdir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)

    pooled = hashlib.md5()
    for k in sorted(manifest):
        pooled.update(k.encode()); pooled.update(manifest[k].encode())
    print(f"{len(manifest)} renders ({len(songs)} songs x {len(SOLOS)} soloes)")
    print(f"pooled hash  {pooled.hexdigest()}")
    return 0


def diff(a):
    ma = json.load(open(os.path.join(a.dira, "manifest.json")))
    mb = json.load(open(os.path.join(a.dirb, "manifest.json")))
    keys = sorted(set(ma) | set(mb))
    moved = []
    for k in keys:
        if ma.get(k) != mb.get(k):
            moved.append(k)
    if not moved:
        print(f"0 of {len(keys)} renders differ: byte-identical")
        return 0
    print(f"{len(moved)} of {len(keys)} renders differ\n")
    print(f"{'render':<52}{'peak diff':>12}{'rms diff dB':>14}")
    for k in moved:
        pa, pb = os.path.join(a.dira, k), os.path.join(a.dirb, k)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print(f"{k:<52}{'MISSING ONE SIDE':>26}")
            continue
        _, xa = read_wav(pa)
        _, xb = read_wav(pb)
        n = min(len(xa), len(xb))
        peak = max((abs(xa[i] - xb[i]) for i in range(n)), default=0)
        acc = sum((xa[i] - xb[i]) ** 2 for i in range(n))
        rms = (acc / n) ** 0.5 if n else 0.0
        # dBFS against 16-bit full scale, so "-90" reads as one LSB of rounding
        # and "-20" reads as a change anyone would hear.
        db = 20.0 * math.log10(rms / 32768.0) if rms > 0 else float("-inf")
        print(f"{k:<52}{peak:>12d}{db:>14.2f}")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("snap", help="render the songs and hash them")
    s.add_argument("outdir")
    s.add_argument("--exe", default=None)
    s.add_argument("--songs", default=SONGS_DIR)
    s.add_argument("--seconds", type=float, default=20.0)
    s.add_argument("--jobs", type=int, default=8)
    s.set_defaults(fn=snap)

    d = sub.add_parser("diff", help="compare two snapshots")
    d.add_argument("dira")
    d.add_argument("dirb")
    d.set_defaults(fn=diff)

    a = ap.parse_args()
    sys.exit(a.fn(a))


if __name__ == "__main__":
    main()
