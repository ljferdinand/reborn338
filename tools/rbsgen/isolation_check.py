#!/usr/bin/env python3
"""Prove a fixture isolates what it claims to, by reading the file.

The A/B protocol says a fixture sounds ONE device with NO effects, and the
project has broken that rule by accident more than once in ways that were
invisible until a whole round had been fitted on the result. Three from this
week alone: a reference render made with the PCF ON at zero depth, which divides
one filter response by another and is not a denominator; a two-section fixture
used to measure a filter, where the unfiltered section held the band up; and a
listening comparison against a solo export whose section turned out to run
through the distortion, the delay, the compressor and the PCF.

Every one of those would have failed here in a second. So this is not a
convenience, it is the step that was missing.

It reads the arrangement, not the panel. ReBirth's Export plays the song, and
the `MIXR`/`DELY`/`DIST`/`PCF `/`COMP` chunks are the SAVE-TIME panel, which can
and does disagree with what the song actually starts with. Everything below is
checked on the tick-0 track dumps, and timed events after tick 0 are reported
too, because a fixture whose knobs move is not isolated in time.

    python isolation_check.py fixture.rbs --device 909 --effect pcf
    python isolation_check.py fixture.rbs --device 909 --effect none
    python isolation_check.py *.rbs --device 909 --effect pcf --quiet
"""
import argparse
import glob
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "analyze"))
from rbs_diff import leaves

DEVICES = ["bass1", "bass2", "808", "909"]
AUDIBLE_DB = -70.0          # an idle engine floor sits near -97 dBFS
EFFECTS = ["none", "delay", "dist", "pcf", "comp"]
# TRKL order: MIXR, 303a, 303b, 808, 909, DELY, DIST, PCF, COMP
TRK_MIXER, TRK_DELY, TRK_DIST, TRK_PCF, TRK_COMP = 0, 5, 6, 7, 8


def parse_trak(p):
    """(tick, param, value) events. The VLQ delta encoding rbs.cpp uses."""
    if len(p) < 4:
        return []
    count = struct.unpack(">I", p[:4])[0]
    i, t, out = 4, 0, []
    for _ in range(count):
        dt = 0
        for _k in range(4):
            if i >= len(p):
                return out
            c = p[i]
            i += 1
            dt = (dt << 7) | (c & 0x7F)
            if not (c & 0x80):
                break
        if i + 2 > len(p):
            return out
        t += dt
        out.append((t, p[i], p[i + 1]))
        i += 2
    return out


def tracks(path):
    return [b for tag, b in leaves(path) if "TRKL/TRAK" in tag]


def at0(ev, param, default=0):
    for t, p, v in ev:
        if t == 0 and p == param:
            return v
    return default


def timed(ev, params):
    """Timed events that CHANGE a value. A song may restate a setting at a bar
    line without riding it, and rbsgen does exactly that: it repeats each
    channel's volume at the end of the arrangement, identical value, purely to
    mark the length. That is not a ride and flagging it cries wolf."""
    cur = {p: at0(ev, p, None) for p in params}
    out = []
    for t, p, v in ev:
        if t > 0 and p in params and cur.get(p) != v:
            out.append((t, p, v))
            cur[p] = v
    return out


def audible(path, exe, seconds=8.0, floor_db=-70.0):
    """Which devices actually make sound, by rendering each one soloed.

    Reading the file cannot answer this: a silent device still carries a
    pattern-select event and a knob header, and a device with a full knob header
    can have an empty pattern. Rendering with `--solo` and measuring the result
    answers it exactly, using the engine we already trust to agree with ReBirth
    on these sources to 0.13 dB.

    ONLY THE OPENING is rendered, which is right for a purpose-built fixture,
    where the device under test plays from bar 1 by construction, and wrong for
    an arrangement that introduces a device later. Trakka Rekka reports its
    bass 1 silent for exactly that reason. That is not a false negative worth
    fixing: a song whose devices enter at different times is not an isolation
    fixture and fails on half a dozen other counts anyway.
    """
    import subprocess
    import tempfile
    import wave
    out = []
    for d in DEVICES:
        with tempfile.TemporaryDirectory() as td:
            w = os.path.join(td, "s.wav")
            r = subprocess.run([exe, "--song", path, "--solo", d, "--no-fx",
                                "--seconds", "%.2f" % seconds, "--out", w],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            if r.returncode != 0 or not os.path.exists(w):
                out.append(None)
                continue
            with wave.open(w, "rb") as f:
                raw = f.readframes(f.getnframes())
            import array
            a = array.array("h")
            a.frombytes(raw)
            if not len(a):
                out.append(-999.0)
                continue
            rms = (sum(float(v) * v for v in a) / len(a)) ** 0.5 / 32768.0
            out.append(20.0 * math.log10(max(rms, 1e-12)))
    return out


def check(path, device, effect, exe=None):
    """[] if the fixture isolates `device` through `effect` alone."""
    bad = []
    trk = tracks(path)
    if len(trk) < 9:
        return ["only %d tracks; not a full song" % len(trk)]
    mixer = parse_trak(trk[TRK_MIXER])
    dev = DEVICES.index(device) if device in DEVICES else -1

    # --- one device audible, measured rather than inferred -------------------
    if exe:
        lv = audible(path, exe)
        for d, v in enumerate(lv):
            if v is None:
                bad.append("could not render %s to test it" % DEVICES[d])
            elif d == dev and v < AUDIBLE_DB:
                bad.append("%s is the device under test and is SILENT (%.0f dBFS)"
                           % (DEVICES[d], v))
            elif d != dev and v >= AUDIBLE_DB:
                bad.append("%s is audible at %.0f dBFS and should be silent"
                           % (DEVICES[d], v))

    # --- the master compressor, which is on by default in ReBirth -----------
    if at0(parse_trak(trk[TRK_COMP]), 0) != 0 and effect != "comp":
        bad.append("the COMPRESSOR unit is ON")
    if at0(mixer, 1) != 0 and effect != "comp":
        bad.append("a section is routed to the compressor (mixer param 1 = %d)"
                   % at0(mixer, 1))

    # --- the PCF ------------------------------------------------------------
    pcf_on = at0(parse_trak(trk[TRK_PCF]), 0) != 0
    pcf_target = at0(mixer, 2)
    if effect == "pcf":
        if not pcf_on:
            bad.append("effect under test is the PCF and its unit is OFF")
        if pcf_target != dev + 1:
            bad.append("PCF routed to %s, not to %s"
                       % (pcf_target or "nowhere", device))
    else:
        if pcf_on and pcf_target:
            bad.append("the PCF is ON and routed to section %d" % pcf_target)

    # --- the distortion -----------------------------------------------------
    dist_on = at0(parse_trak(trk[TRK_DIST]), 0) != 0
    dist_flags = [at0(mixer, 6 + 6 * c + 3) != 0 for c in range(4)]
    if effect == "dist":
        if not dist_on:
            bad.append("effect under test is the distortion and its unit is OFF")
        if not dist_flags[dev]:
            bad.append("the distortion is not switched on for %s" % device)
        if any(f for c, f in enumerate(dist_flags) if c != dev):
            bad.append("the distortion is on for a section that is not %s" % device)
    elif dist_on and any(dist_flags):
        bad.append("the distortion unit is ON for section(s) %s"
                   % [DEVICES[c] for c, f in enumerate(dist_flags) if f])

    # --- the delay ----------------------------------------------------------
    sends = [at0(mixer, 6 + 6 * c + 2) for c in range(4)]
    if effect == "delay":
        if sends[dev] == 0:
            bad.append("effect under test is the delay and %s's send is zero" % device)
        if any(s for c, s in enumerate(sends) if c != dev):
            bad.append("a section other than %s sends to the delay: %s" % (device, sends))
    elif any(sends):
        bad.append("echo sends are not zero: %s" % sends)

    # --- nothing moves ------------------------------------------------------
    moving = timed(mixer, {1, 2} | {6 + 6 * c + k for c in range(4) for k in (0, 1, 2, 3)})
    if moving:
        bad.append("the mixer track has %d timed events after tick 0 (first %s)"
                   % (len(moving), moving[0]))
    for name, idx in (("DELY", TRK_DELY), ("DIST", TRK_DIST),
                      ("PCF", TRK_PCF), ("COMP", TRK_COMP)):
        ev = timed(parse_trak(trk[idx]), set(range(8)))
        if ev and not (effect == name.lower().rstrip() and False):
            bad.append("the %s track has %d timed events after tick 0 (first %s)"
                       % (name, len(ev), ev[0]))
    return bad


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--device", required=True, choices=DEVICES)
    ap.add_argument("--effect", required=True, choices=EFFECTS)
    ap.add_argument("--quiet", action="store_true", help="only print failures")
    ap.add_argument("--no-audio", action="store_true",
                    help="skip the render-based silence test (faster, weaker)")
    ap.add_argument("--exe", default=os.path.join(
        HERE, "..", "..", "build", "Release", "reborn_render.exe"))
    a = ap.parse_args(argv)

    paths = []
    for f in a.files:
        paths.extend(sorted(glob.glob(f)) or [f])
    fails = 0
    for p in paths:
        bad = check(p, a.device, a.effect, None if a.no_audio else a.exe)
        if bad:
            fails += 1
            print("FAIL %s" % os.path.basename(p))
            for b in bad:
                print("       %s" % b)
        elif not a.quiet:
            print("ok   %s  (%s through %s, nothing else)"
                  % (os.path.basename(p), a.device, a.effect))
    print("\n%d of %d isolate %s through %s"
          % (len(paths) - fails, len(paths), a.device, a.effect))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
