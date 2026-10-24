#!/usr/bin/env python3
"""Structured .rbs differ: shows exactly which bytes changed between two song
files, chunk by chunk. The discovery tool for unmapped fields: save a song,
change ONE control in the original program, save again, diff the two files --
the changed bytes ARE that control.

Usage:
    python rbs_diff.py <a.rbs> <b.rbs> [--max N]

Output lines:  <chunk-path>[occurrence] +offset: aa -> bb
"""
import struct
import sys

GROUPS = {b'CAT ', b'FORM', b'LIST', b'PROP'}


def walk(data, off, end, path, out):
    """Collect (path, payload) for every leaf chunk, depth-first."""
    counts = {}
    while off + 8 <= end:
        cid = data[off:off + 4]
        size = struct.unpack('>I', data[off + 4:off + 8])[0]
        body = off + 8
        if body + size > end:
            break
        name = cid.decode('latin1').strip()
        counts[name] = counts.get(name, 0)
        tag = f"{path}/{name}[{counts[name]}]"
        counts[name] += 1
        if cid in GROUPS and size >= 4:
            gtype = data[body:body + 4].decode('latin1').strip()
            walk(data, body + 4, body + size, f"{path}/{name}:{gtype}", out)
        else:
            out.append((tag, data[body:body + size]))
        off = body + size + (size & 1)


def leaves(path):
    d = open(path, 'rb').read()
    out = []
    walk(d, 0, len(d), '', out)
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    max_show = 200
    if '--max' in sys.argv:
        i = sys.argv.index('--max')
        max_show = int(sys.argv[i + 1])
    a, b = leaves(sys.argv[1]), leaves(sys.argv[2])
    if len(a) != len(b):
        print(f"chunk count differs: {len(a)} vs {len(b)}")
    shown = 0
    for (pa, da), (pb, db) in zip(a, b):
        if pa != pb:
            print(f"chunk order mismatch: {pa} vs {pb}")
            continue
        if da == db:
            continue
        if len(da) != len(db):
            print(f"{pa}: SIZE {len(da)} -> {len(db)}")
        n = min(len(da), len(db))
        diffs = [i for i in range(n) if da[i] != db[i]]
        print(f"{pa}: {len(diffs)} byte(s) differ")
        for i in diffs:
            if shown >= max_show:
                print(f"  ... (more suppressed; use --max)")
                return 0
            print(f"  +{i:5d}: {da[i]:02X} -> {db[i]:02X}")
            shown += 1
    if shown == 0:
        print("files are chunk-identical")
    return 0


if __name__ == "__main__":
    sys.exit(main())
