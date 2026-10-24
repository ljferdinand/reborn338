#!/usr/bin/env python3
"""Make a solo-one-machine, effects-off, compressor-off copy of a shipped song.

Whole-song A/B needs a reference that sounds ONE device with nothing else in the
path, and a demo song is not that: it mixes four sections through a rack we have
only partly wired. This edits a copy in place, changing no chunk size, so
ReBirth accepts it exactly as it accepts the original:

  - every other device muted for the whole song, by forcing its track's param 0
    (the mute toggle, 0 = muted) to 0 at every event rather than just at tick 0,
  - DELY / PCF / DIST / COMP switched off at each effect track's own param 0,
  - MIXR left with one channel on, the distortion selector off, and that
    channel's echo send and switch cleared.

The chosen machine's own track and the mixer track are NOT touched, so its
arrangement, its knob rides and its volume/pan rides are what ReBirth authored.

TWO TRAPS, both of which have cost real time.

**The master compressor is ON in most shipped songs** (the COMP track sets
param 0 to 1), so an export made without clearing it is compressed and is not a
valid reference. renders/02b-Trakka-Rekka-RB338-909.wav was made that way.

**Export renders the transport LOOP, not the arrangement.** The loop length
lives at GLOB+11 as a big-endian u16 holding bars times three. G-Spot's loop is
8 bars against a 209-bar arrangement, so a plain export of it is 12.8 seconds,
which is exactly why renders/nofxexports/G-Spot.WAV has only ever been 12.8 s.
Pass loop="fit" to widen the loop to cover the arrangement.

Usage:
    python solo_song.py "<song>.rbs" out.rbs --machine 909 --master 50 --loop fit
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

# TRAK order: MIXR, 303a, 303b, 808, 909, DELY, DIST, PCF, COMP. Corrected
# 2026-08-27; it used to name PCF before DIST. Harmless here, since this file
# only switches all four off, but the names were wrong.
T_MIXR, T_303A, T_303B, T_808, T_909, T_DELY, T_DIST, T_PCF, T_COMP = range(9)
_MACHINE = {"808": (T_808, 2), "909": (T_909, 3)}


def _walk(data, off, size):
    """Yield (value_index, param) for every event in a TRAK payload."""
    count = G._rdU32BE(data, off)
    i, end, t = off + 4, off + size, 0
    for _ in range(count):
        d = 0
        while i < end:
            c = data[i]; i += 1
            d = (d << 7) | (c & 0x7F)
            if not (c & 0x80): break
        if i + 2 > end: return
        t += d
        yield i + 1, data[i], t
        i += 2


def set_param(data, off, size, param, value):
    n = 0
    for vpos, p, _t in _walk(data, off, size):
        if p == param:
            data[vpos] = value; n += 1
    return n


def arrangement_bars(data):
    """Last tick on ANY track, in bars. One tick is a 1/32 note, 32 to the bar."""
    last = 0
    for cid, o, s in G.iter_leaves(bytes(data)):
        if cid != b"TRAK": continue
        for _vpos, _p, t in _walk(data, o, s):
            if t > last: last = t
    return last // 32 + 1


def solo(src, dst, machine="909", master=None, loop=None):
    keep_trak, keep_chan = _MACHINE[machine]
    base = open(src, "rb").read()
    data = bytearray(base)
    traks = [(o, s) for cid, o, s in G.iter_leaves(base) if cid == b"TRAK"]
    if len(traks) != 9:
        raise ValueError("expected 9 tracks, found %d" % len(traks))
    log = []
    for name, ti in (("303a", T_303A), ("303b", T_303B), ("808", T_808), ("909", T_909)):
        if ti == keep_trak: continue
        log.append("mute %-4s: %d events" % (name, set_param(data, traks[ti][0], traks[ti][1], 0, 0)))
    for ti in (T_DELY, T_DIST, T_PCF, T_COMP):
        set_param(data, traks[ti][0], traks[ti][1], 0, 0)
    log.append("delay, PCF, distortion and the master compressor off")

    mo = [(o, s) for cid, o, s in G.iter_leaves(base) if cid == b"MIXR"][0][0]
    for c in range(4):
        data[mo + 16 + 12 * c] = 1 if c == keep_chan else 0
    data[mo + 16 + 12 * keep_chan + 3] = 0        # echo send
    data[mo + 16 + 12 * keep_chan + 4] = 0        # echo switch
    data[mo + 1] = 0                              # distortion input selector
    log.append("MIXR: only channel %d on, distortion routed nowhere" % keep_chan)

    if master is not None:
        log.append("master %d -> %d, for export headroom" % (data[mo], master))
        data[mo] = master

    if loop is not None:
        go = [(o, s) for cid, o, s in G.iter_leaves(base) if cid == b"GLOB"][0][0]
        was = struct.unpack_from(">H", data, go + 11)[0]
        bars = arrangement_bars(data) if loop == "fit" else int(loop)
        struct.pack_into(">H", data, go + 11, max(1, bars) * 3)
        log.append("loop %d bars -> %d bars (Export renders the LOOP)" % (was // 3, bars))

    if len(data) != len(base):
        raise AssertionError("patch changed the file size")
    open(dst, "wb").write(bytes(data))
    return log


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("song"); ap.add_argument("out")
    ap.add_argument("--machine", choices=("808", "909"), default="909")
    ap.add_argument("--master", type=int, default=None,
                    help="MIXR master byte; 50 gives about 18 dB of headroom")
    ap.add_argument("--loop", default=None,
                    help="'fit' to cover the arrangement, or a bar count")
    a = ap.parse_args(argv)
    for line in solo(a.song, a.out, a.machine, a.master, a.loop):
        print("  ", line)
    print("  wrote", a.out)


if __name__ == "__main__":
    main()
