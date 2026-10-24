#!/usr/bin/env python3
"""Widen a song's transport loop so an export covers the whole arrangement.

**Export renders the transport LOOP, not the arrangement.** That is the trap
`solo_song.py` documents, and it bites just as hard for a full-mix reference:
G-Spot's loop is 8 bars against a 209-bar arrangement, so a plain export of it
is 12.8 seconds. `renders/nofxexports/G-Spot.WAV` has only ever been 12.8 s for
exactly this reason.

This copies a song and changes ONE field: the loop length at `GLOB` +11, a
big-endian u16 holding bars times three. Nothing else moves, so ReBirth loads
the copy exactly as it loads the original, and the export is the whole song with
its own rack, its own automation and its own compressor setting intact.

That last point matters when reading the result. A demo song is a full-mix
regression reference, not a fixture: it mixes every component we have and
several we have not measured. Six of them exported this way on 2026-08-27 gave
2.06 dB mean octave-band error against our renders, and all six switch the
master compressor ON while ours defaults OFF, so most of that number belongs to
components whose supertask has not happened. Use these for tracking a trend, not
for tuning a voice.

    python wholemix_song.py "<song>.rbs" out.rbs
    python wholemix_song.py --all <out-dir>

--all reads the demo songs that ship with ReBirth, which are not in this
repository: set REBORN_SONGS_DIR to your own install's "Demo Songs" folder.
"""
import argparse
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# The demo songs ship with ReBirth and are not in this repository. --all reads
# them from REBORN_SONGS_DIR, your own install's "Demo Songs" folder.
DEMOS = os.environ.get("REBORN_SONGS_DIR")
GROUPS = (b"CAT ", b"FORM", b"LIST", b"PROP")


def _find(buf, off, end, want, acc):
    while off + 8 <= end:
        cid = bytes(buf[off:off + 4])
        size = struct.unpack(">I", bytes(buf[off + 4:off + 8]))[0]
        body = off + 8
        if cid in GROUPS:
            _find(buf, body + 4, min(body + size, end), want, acc)
        elif cid == want:
            acc.append((body, size))
        off = body + size + (size & 1)


def _last_tick(p):
    n = struct.unpack(">I", bytes(p[0:4]))[0]
    i, t = 4, 0
    for _ in range(n):
        d = 0
        while True:
            c = p[i]; i += 1
            d = (d << 7) | (c & 0x7F)
            if not (c & 0x80):
                break
        t += d
        i += 2
    return t


def widen(src, dst):
    """Copy src to dst with the loop widened to cover the arrangement."""
    d = bytearray(open(src, "rb").read())
    traks = []
    _find(d, 0, len(d), b"TRAK", traks)
    if not traks:
        raise ValueError(f"{src}: no TRAK chunks")
    bars = max(_last_tick(d[o:o + s]) for o, s in traks) // 32 + 1
    glob_ = []
    _find(d, 0, len(d), b"GLOB", glob_)
    go = glob_[0][0]
    was = struct.unpack(">H", bytes(d[go + 11:go + 13]))[0] // 3
    struct.pack_into(">H", d, go + 11, min(65535, bars * 3))
    open(dst, "wb").write(bytes(d))
    return was, bars


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--all", metavar="OUTDIR",
                    help="widen every shipped demo song into OUTDIR")
    a = ap.parse_args(argv)

    if a.all:
        if not DEMOS:
            ap.error("--all needs REBORN_SONGS_DIR set to your ReBirth "
                     "install's Demo Songs folder")
        os.makedirs(a.all, exist_ok=True)
        for p in sorted(glob.glob(os.path.join(DEMOS, "*.rbs"))):
            was, bars = widen(p, os.path.join(a.all, os.path.basename(p)))
            print(f"{os.path.basename(p)[:30]:30s} loop {was:4d} -> {bars:4d} bars")
        return 0
    if not a.song or not a.out:
        ap.error("give a song and an output, or --all OUTDIR")
    was, bars = widen(a.song, a.out)
    print(f"loop {was} -> {bars} bars")
    return 0


if __name__ == "__main__":
    sys.exit(main())
