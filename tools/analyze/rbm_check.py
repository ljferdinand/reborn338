#!/usr/bin/env python3
"""Check which part of each file embedded in a .rbm mod is stored in the clear.

Measured 2026-10-04 on MSM2, PBE2 and Metallicon: each EMBF payload holds the
file name and then the file, at exactly the file's own length, but only about
the first 1024 bytes of the payload are the file as written. Past that the
bytes are near-random (about 8 bits of entropy per byte, against 4.6 to 7.6 for
the readable start), a 16-bit drum sample turns to full-scale noise, and a
skin JPEG never reaches its end-of-image marker. A same-length transformation,
not compression. The program plays these samples as drums, so it reverses it;
our loader reads the whole payload as plain PCM and plays noise.

For every embedded AIFF this prints the stored length against the AIFF's own
FORM size, where the sample first turns noisy (bytes from the payload start),
and the byte entropy of the readable start against the rest; for JPEGs,
whether the payload ends with the FFD9 end marker.

    python rbm_check.py <mod.rbm> [more.rbm ...]
"""
import collections
import math
import re
import struct
import sys

import numpy as np


def entropy(s):
    if not s:
        return 0.0
    c = collections.Counter(s)
    n = len(s)
    return -sum(v / n * math.log2(v / n) for v in c.values())


def noise_onset(x):
    """First 16-sample window with 9 or more sign changes: a level a drum
    sample's own waveform does not reach right after its attack."""
    s = np.sign(x)
    for k in range(0, len(x) - 16, 2):
        if np.sum(np.abs(np.diff(s[k:k + 16])) > 0) >= 9:
            return k
    return None


def check(path):
    b = open(path, "rb").read()
    aiffs, jpegs = [], []
    for m in re.finditer(rb"EMBF", b):
        e = m.start()
        size = struct.unpack(">I", b[e + 4:e + 8])[0]
        pay = e + 8
        nul = b.find(b"\x00", pay, pay + 64)
        if nul < 0:
            continue
        name = b[pay:nul].decode("latin1")
        body = b[nul + 1:pay + size]
        if body[:4] == b"FORM" and body[8:11] == b"AIF":
            want = 8 + struct.unpack(">I", body[4:8])[0]
            onset = None
            j, end = 12, min(want, len(body))
            while j + 8 <= end:
                cid = body[j:j + 4]
                sz = struct.unpack(">I", body[j + 4:j + 8])[0]
                if cid == b"SSND":
                    d0 = j + 16
                    x = np.frombuffer(body[d0:j + 8 + sz][:(sz - 8) // 2 * 2], ">i2").astype(float)
                    k = noise_onset(x)
                    onset = None if k is None else (nul + 1 - pay) + d0 + 2 * k
                    break
                j += 8 + sz + (sz & 1)
            aiffs.append((name, len(body), want, onset,
                          entropy(body[:900]), entropy(body[1100:1100 + 20000])))
        elif body[:2] == b"\xff\xd8":
            jpegs.append((name, body[-2:] == b"\xff\xd9"))
    print(f"== {path}")
    same = sum(1 for a in aiffs if abs(a[1] - a[2]) <= 1)
    print(f"   {len(aiffs)} AIFF payloads, {same} stored at exactly their FORM size")
    onsets = [a[3] for a in aiffs if a[3] is not None]
    if onsets:
        print(f"   noise onset, bytes from the payload start: median {int(np.median(onsets))}, "
              f"range {min(onsets)} to {max(onsets)} over {len(onsets)} samples")
    if aiffs:
        print(f"   byte entropy, readable start / rest: "
              f"{np.mean([a[4] for a in aiffs]):.2f} / {np.mean([a[5] for a in aiffs if a[1] > 1200]):.2f} bits")
    if jpegs:
        print(f"   {len(jpegs)} JPEG payloads, {sum(ok for _, ok in jpegs)} end with the FFD9 marker")


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    for p in argv:
        check(p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
