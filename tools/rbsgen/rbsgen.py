#!/usr/bin/env python3
"""
rbsgen - generate controlled .rbs test songs for A/B testing Reborn vs ReBirth.

There are two modes:

  scratch (recommended): author a fresh RB40 file from the documented layout
         with no baseline and no bytes copied from the original program. Confirmed
         to open in ReBirth using a neutral HEAD string, so it is clean-room pure
         and the recommended path for A/B. See docs/MEASUREMENT_PROTOCOL.md.

  patch  (in-context alternative): take a REAL .rbs you can already open in
         ReBirth and overwrite parameter bytes IN PLACE (no size or structure
         changes) to produce a controlled variant. Useful for testing a voice
         inside a real song's context, or as a fallback if a future ReBirth
         build is pickier about a synthesised file.

Why controlled songs at all
---------------------------
Real songs automate their knobs and route through effects Reborn does not yet
wire (PCF, compressor), so measuring a voice against them mixes the voice under
test with variables we do not control. A controlled song plays one 303 with
STATIC knobs and no automation, other sections muted, effects off, so a measured
difference is the voice, not the surrounding song.

Patch mode, the reliable path
-----------------------------
    python3 rbsgen.py --patch /path/to/RealSong.rbs --out res_hi.rbs \
        --voice bass2 --cutoff 90 --reso 115 --envmod 70 --note 9

This overwrites, inside a copy of RealSong.rbs:
  * the chosen 303's 9-byte knob header (your cutoff/reso/etc.),
  * that 303's pattern record 0 (a repeated note by default),
  * that 303's automation track, in place: pattern-select events -> 0, knob
    events -> your static values, mute events -> unmuted, so the knobs hold and
    the arrangement stays on pattern 0,
  * the MIXR channel on/off so only the chosen section is on.
File size and every chunk boundary are unchanged, so ReBirth loads it like the
original. On the Reborn side render it with `reborn_render --song res_hi.rbs
--solo bass2 --no-dist --no-delay`. On the ReBirth side open it, Pattern mode,
master compressor OFF, and export the loop.

Library use (scripting a sweep):
    import rbsgen
    base = open("RealSong.rbs","rb").read()
    for reso in (20, 60, 100, 127):
        data = rbsgen.patch_song(base, knobs=dict(cutoff=90, reso=reso, envmod=70),
                                 pattern=rbsgen.repeated_note(9), voice="bass2")
        open(f"res_{reso:03d}.rbs","wb").write(data)

Knobs are 0..127: tune (64 = centre), cutoff, reso, envmod, decay, accent,
waveform (0 = saw, 1 = square). note is a semitone 0..12 (9 = A).
"""

import struct
import argparse
import os
import sys


# ---------------------------------------------------------------------------
# Where the non-git material lives
# ---------------------------------------------------------------------------

def workspace_path(*parts):
    """A path inside the WORKSPACE, the directory that contains this repo.

    The workspace holds everything deliberately not in git: the manual page
    images, the generated fixtures, and ReBirth's exports. Defaulting to the
    repo's parent means one command resolves correctly on Windows and on macOS
    with no edit, as long as the layout is mirrored.

    Set `REBORN_WORKSPACE` to override, e.g. when the repo lives somewhere else
    or the fixtures are on an external drive.
    """
    root = os.environ.get("REBORN_WORKSPACE")
    if not root:
        here = os.path.dirname(os.path.abspath(__file__))       # tools/rbsgen
        root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    return os.path.join(root, *parts)


# ---------------------------------------------------------------------------
# IFF primitives
# ---------------------------------------------------------------------------

def _u32be(n): return struct.pack(">I", n)
def _rdU32BE(b, off): return (b[off] << 24) | (b[off+1] << 16) | (b[off+2] << 8) | b[off+3]

def chunk(cid, payload):
    assert len(cid) == 4, cid
    out = cid + _u32be(len(payload)) + payload
    if len(payload) & 1:
        out += b"\x00"
    return out

def group(cid, gtype, children):
    assert len(cid) == 4 and len(gtype) == 4
    return chunk(cid, gtype + children)

_GROUPS = (b"CAT ", b"FORM", b"LIST", b"PROP")

def iter_leaves(buf):
    """Return a list of (id, body_offset, size) for every leaf chunk, in tree
    order, descending into group chunks."""
    leaves = []
    def walk(off, end):
        while off + 8 <= end:
            cid = buf[off:off+4]
            size = _rdU32BE(buf, off+4)
            body = off + 8
            if body + size > end:
                return
            if cid in _GROUPS and size >= 4:
                walk(body + 4, body + size)
            else:
                leaves.append((cid, body, size))
            off = body + size + (size & 1)
    walk(0, len(buf))
    return leaves

def find_chunk(buf, want_id):
    for cid, off, size in iter_leaves(buf):
        if cid == want_id:
            return buf[off:off+size]
    return None

def _find_nth(leaves, cid, n):
    k = 0
    for c, off, size in leaves:
        if c == cid:
            if k == n:
                return off, size
            k += 1
    return None

# ---------------------------------------------------------------------------
# 303 pattern helpers
# ---------------------------------------------------------------------------

# Octave bits 2-3 are independent signed flags that SUM (proven by controlled
# A/B + FFT of solo exports 2026-07-23): 0x04 = +1, 0x08 = -1, 0x0C = net 0.
# SLIDE = 0x01, ACCENT = 0x02, CORRECTED 2026-07-24 (previously swapped).
# A controlled export with 0x01 on every 4th step produced steps that did NOT
# retrigger (47-49 onsets from 64 programmed steps) and read darker, identical
# at accent knob 0 and 110: tie/slide behaviour, not accent. See rbs.cpp.
_SLIDE, _ACCENT, _OCTUP, _OCTDN, _REST = 0x01, 0x02, 0x04, 0x08, 0x10

def step(note=0, accent=False, slide=False, octave=0, rest=False):
    return dict(note=note, accent=accent, slide=slide, octave=octave, rest=rest)

def repeated_note(note=9, length=16, accents=()):
    return [step(note=note, accent=(i in accents)) for i in range(length)]

def _encode_step(s):
    flags = 0
    if s.get("accent"): flags |= _ACCENT
    if s.get("slide"):  flags |= _SLIDE
    o = s.get("octave", 0)
    if o > 0:   flags |= _OCTUP               # 0x04 = +1
    elif o < 0: flags |= _OCTDN               # 0x08 = -1
    if s.get("rest"): flags |= _REST
    return bytes([s.get("note", 0) & 0xFF, flags])

def _record_303(steps, length=16, shuffle=False):
    rec = bytes([1 if shuffle else 0, length & 0xFF])
    for i in range(16):
        rec += _encode_step(steps[i]) if i < len(steps) else _encode_step(step(rest=True))
    assert len(rec) == 34
    return rec

def _rest_record_303():
    return _record_303([step(rest=True)] * 16)

def _kb(v): return max(0, min(127, int(round(v))))

_KNOB_HEADER_ORDER = ["tune", "cutoff", "reso", "envmod", "decay", "accent"]  # bytes 2..7
# TRAK knob params: 2=tune 3=cutoff 4=reso 5=envmod 6=decay 7=accent 8=waveform
_TRAK_KNOB_PARAM = {2: "tune", 3: "cutoff", 4: "reso", 5: "envmod", 6: "decay", 7: "accent"}

def _header_303(knobs):
    return bytes([
        1, 0,
        _kb(knobs.get("tune", 64)), _kb(knobs.get("cutoff", 64)),
        _kb(knobs.get("reso", 0)), _kb(knobs.get("envmod", 0)),
        _kb(knobs.get("decay", 64)), _kb(knobs.get("accent", 0)),
        1 if knobs.get("waveform", 0) else 0,
    ])

# ---------------------------------------------------------------------------
# Drum (808/909) pattern helpers
# ---------------------------------------------------------------------------
#
# Drum record = [shuffle u8][length u8] + 16 steps x 12 bytes (194 total).
# Column 0 is the GLOBAL ACCENT row; columns 1..11 are voices in panel order:
#   808: BD SD LT MT HT RS/CL CP/MA CB CY OH CH   (cell value 0/1)
#   909: BD SD LT MT HT RS  CP    CH OH CR RD     (1=soft, 2=normal, 3=accent;
#        note the file stores CH BEFORE OH)
# Ground truth: src/io/rbs.cpp decodeDrums / k808VoiceMap / k909VoiceMap.

_808_COL = {"bd": 1, "sd": 2, "lt": 3, "mt": 4, "ht": 5, "rs": 6, "cl": 6,
            "cp": 7, "ma": 7, "cb": 8, "cy": 9, "oh": 10, "ch": 11}
_909_COL = {"bd": 1, "sd": 2, "lt": 3, "mt": 4, "ht": 5, "rs": 6, "cp": 7,
            "ch": 8, "oh": 9, "cr": 10, "rd": 11}

def drum_grid(hits, is909=False, accents=(), flams=()):
    """hits: dict voice-name -> iterable of step indices (0..15), or an
    iterable of (step, voice) pairs. accents: steps whose global-accent cell
    is set. flams: 909 only, either steps (every voice on that step flams) or
    (step, voice) pairs for one voice. Returns a 16x12 grid of cell bytes.

    909 cell values are 1 SOFT, 2 normal, 3 and up FLAM (measured 2026-08-25 by
    isolating each factor against ReBirth: soft sits 7.96 dB under normal, and a
    flam cell fires the voice twice, the second after a gap the flam knob sets, up
    to one 32nd note). ACCENT is column 0 on BOTH machines, not a cell value.

    Flam is written as 3, which is what ReBirth's own songs use (281 such cells
    across the shipped set, and no cell of value 4 anywhere). Value 4 decodes as a
    flam too but reads about 2.7 dB louder, and is not emitted here.
    """
    grid = [[0] * 12 for _ in range(16)]
    cols = _909_COL if is909 else _808_COL
    if isinstance(hits, dict):
        pairs = [(s, v) for v, steps in hits.items() for s in steps]
    else:
        pairs = list(hits)
    for s, v in pairs:
        col = cols[v.lower()]
        grid[s][col] = 2 if is909 else 1          # 909: 2 = normal hit
    # Column 0 is the accent row on BOTH machines, and it carries a VALUE that
    # each machine writes consistently: the 808 always 1, the 909 always 2.
    # Verified 2026-08-26 across all 17 shipped songs: 2544 cells of value 1
    # on 808 tracks, 1442 of value 2 on 909 tracks, and not one song mixing the
    # two on one machine. ReBirth does NOT treat them alike: a 909 accented with
    # value 1 spans 13.93 dB from knob 0 to 127 where value 2 spans 18.93, so
    # writing 1 here for a 909 fixture measures a cell no real 909 part contains.
    for s in accents:
        grid[s][0] = 2 if is909 else 1
    if is909:
        for f in flams:
            if isinstance(f, (tuple, list)):      # (step, voice): just that voice
                st, v = f
                c = cols[v.lower()]
                if grid[st][c]:
                    grid[st][c] = 3
            else:                                 # a bare step: every voice on it
                for c in range(1, 12):
                    if grid[f][c]:
                        grid[f][c] = 3
    return grid

def _record_drum(grid, length=16, shuffle=False):
    rec = bytes([1 if shuffle else 0, length & 0xFF])
    for s in range(16):
        rec += bytes(grid[s] if grid else [0] * 12)
    assert len(rec) == 194
    return rec

# Knob headers. Byte layout from src/io/rbs.cpp decode808Knobs/decode909Knobs:
#   808 (30): [ver][?][accent] BD[L,tone,decay] SD[L,tone,snappy]
#             LT[L,tune,sw] MT[L,tune,sw] HT[L,tune,sw] RS[L,sw] CP[L,sw]
#             CB[L] CY[L,tune,decay] OH[L,decay] CH[L] [?]
#   909 (31): [ver][?][accent] BD[L,tune,attack,decay] SD[L,tune,tone,snappy]
#             LT[L,tune,decay] MT[L,tune,decay] HT[L,tune,decay] RS[L] CP[L]
#             HH[L,chDecay,ohDecay] CR[L,tune] RD[L] [?..]
# The sw bytes select the alternate voice for a shared column (LT/LC, RS/CL,
# CP/MA). Defaults: levels 100, tunes/decays 64, accent 64, switches 0.

def _header_808(dk):
    g = lambda key, d=64: _kb(dk.get(key, d))
    return bytes([1, 0, g("accent"),
        g("bd_level", 100), g("bd_tone"), g("bd_decay"),
        g("sd_level", 100), g("sd_tone"), g("sd_snappy"),
        g("lt_level", 100), g("lt_tune"), _kb(dk.get("lt_conga", 0)),
        g("mt_level", 100), g("mt_tune"), _kb(dk.get("mt_conga", 0)),
        g("ht_level", 100), g("ht_tune"), _kb(dk.get("ht_conga", 0)),
        g("rs_level", 100), _kb(dk.get("rs_claves", 0)),
        g("cp_level", 100), _kb(dk.get("cp_maracas", 0)),
        g("cb_level", 100),
        g("cy_level", 100), g("cy_tune"), g("cy_decay"),
        g("oh_level", 100), g("oh_decay"),
        g("ch_level", 100), 0])

def _header_909(dk):
    g = lambda key, d=64: _kb(dk.get(key, d))
    return bytes([1, 0, g("accent"),
        g("bd_level", 100), g("bd_tune"), g("bd_attack"), g("bd_decay"),
        g("sd_level", 100), g("sd_tune"), g("sd_tone"), g("sd_snappy"),
        g("lt_level", 100), g("lt_tune"), g("lt_decay"),
        g("mt_level", 100), g("mt_tune"), g("mt_decay"),
        g("ht_level", 100), g("ht_tune"), g("ht_decay"),
        g("rs_level", 100), g("cp_level", 100),
        g("hh_level", 100), g("ch_decay"), g("oh_decay"),
        g("cr_level", 100), g("cr_tune"),
        # RD mirrors CR as a [Level, Tune] pair: the shipped-song header scan
        # (2026-08-25) shows bytes 25/27 tracking together as levels and 26/28 as
        # tunes, so byte 28 is the ride tune (MIDI CC 91), confirmed empirically by
        # the d909_rdt sweep. Default 64 so a ride-only fixture is not detuned to 0.
        g("rd_level", 100), g("rd_tune"),
        # Byte 29 is the FLAM AMOUNT (MIDI CC 92), CONFIRMED 2026-08-25 by export.
        # Sweeping it alone changes nothing at all, exactly like the accent knob:
        # it only affects steps marked as flam, which are drum cells with the value
        # 4 (the cell values being 1 soft, 2 normal, 3 accent, 4 flam). With flam
        # cells present the knob moves the grace note's LEVEL by about 2.7 dB of
        # whole-file RMS between its extremes.
        # Byte 30 is NOT a control: it mirrors bytes 25 and 27 (the crash and ride
        # levels) exactly in every one of the 16 shipped songs.
        g("flam"), g("rd_level", 100)])

def _chunk_drum_pattern(cid, header, grid, length=16):
    body = header + _record_drum(grid, length=length)
    body += _record_drum(None) * 31
    return chunk(cid, body)

def _drum_song_trak(header, n_bars=4, pattern=0, active=1):
    """Song-start snapshot for a drum track: t=0 dump of every knob byte as
    param N = header byte N (correspondence verified across the shipped songs:
    Fast Forward's 808 dump matches its header 27/27, its 909 dump params run
    2..29), then pattern select and the channel-active flag, in the same
    descending-order convention as the 303 track."""
    ev = [(0, p, header[p]) for p in range(len(header) - 2, 1, -1)]
    ev.append((0, 1, pattern & 0x7F))
    ev.append((0, 0, 1 if active else 0))
    return _trak_abs(ev)

# ===========================================================================
# PATCH MODE  (recommended for ReBirth A/B)
# ===========================================================================

def _neutralize_trak(data, off, size, knobs):
    """In place, over a TRAK payload at [off, off+size): set pattern-select
    (param 1) values to 0, mute (param 0) values to 1 (unmuted), and knob
    (params 2..8) values to the static targets in knobs. Sizes unchanged."""
    count = _rdU32BE(data, off)
    i = off + 4
    end = off + size
    for _ in range(count):
        # VLQ delta time (skip)
        for _k in range(4):
            if i >= end: return
            c = data[i]; i += 1
            if not (c & 0x80): break
        if i + 2 > end: return
        param = data[i]; vpos = i + 1
        if param == 1:
            data[vpos] = 0
        elif param == 0:
            data[vpos] = 1
        elif param in _TRAK_KNOB_PARAM:
            data[vpos] = _kb(knobs.get(_TRAK_KNOB_PARAM[param], data[vpos]))
        elif param == 8:  # waveform
            data[vpos] = 1 if knobs.get("waveform", 0) else 0
        i += 2

def patch_song(baseline, knobs, pattern, voice="bass2", mute_others=True):
    """Return a patched copy of `baseline` (bytes) as a controlled test song.
    `voice` is 'bass1' or 'bass2'. Only parameter VALUES change; the file size
    and every chunk boundary are preserved, so ReBirth accepts it as it accepts
    the baseline."""
    data = bytearray(baseline)
    leaves = iter_leaves(baseline)
    vidx = 0 if voice == "bass1" else 1

    loc = _find_nth(leaves, b"303 ", vidx)
    if not loc:
        raise ValueError("baseline has no %s 303 chunk" % voice)
    off, size = loc
    if size < 9 + 34:
        raise ValueError("303 chunk too small")
    data[off:off+9] = _header_303(knobs)                      # knob header
    data[off+9:off+9+34] = _record_303(pattern, length=knobs.get("length", 16))  # pattern 0

    trak = _find_nth(leaves, b"TRAK", 1 + vidx)               # MIXR=0, bass1=1, bass2=2
    if trak:
        _neutralize_trak(data, trak[0], trak[1], knobs)

    mixr = _find_nth(leaves, b"MIXR", 0)
    if mixr and mixr[1] >= 64:
        moff = mixr[0]
        for c in range(4):
            if c == vidx:
                data[moff + 16 + 12 * c] = 1
            elif mute_others:
                data[moff + 16 + 12 * c] = 0
        data[moff + 1] = 0                                    # distortion selector off
    return bytes(data)

def write_patched(out_path, baseline_path, knobs, pattern, voice="bass2"):
    base = open(baseline_path, "rb").read()
    data = patch_song(base, knobs, pattern, voice=voice)
    assert len(data) == len(base), "patch changed file size"
    open(out_path, "wb").write(data)
    return len(data)

# ===========================================================================
# SCRATCH MODE  (Reborn-only; ReBirth rejects from-scratch files)
# ===========================================================================

def _chunk_303(knobs, pattern, patterns=None):
    """One 303 bank. `pattern` fills slot 0; `patterns` fills as many slots as
    it has, which is what a song with an arrangement needs. The rest are rests.
    With `patterns` None the output is byte-identical to before it existed."""
    body = _header_303(knobs)
    pats = patterns if patterns is not None else [pattern]
    for i in range(32):
        if i < len(pats) and pats[i] is not None:
            body += _record_303(pats[i], length=knobs.get("length", 16))
        else:
            body += _rest_record_303()
    assert len(body) == 1097
    return chunk(b"303 ", body)

def _chunk_303_silent():
    body = bytes([1, 0, 64, 0, 0, 0, 64, 0, 0]) + _rest_record_303() * 32
    assert len(body) == 1097
    return chunk(b"303 ", body)

def _chunk_drum(cid, header_len):
    record = bytes([0, 16]) + bytes(192)
    body = bytes(header_len) + record * 32
    return chunk(cid, body)

def _chunk_mixr(chan_on=(1, 0, 0, 0), pcf_target=None, dist_on=(0, 0, 0, 0),
                comp_target=None, echo_send=(0, 0, 0, 0)):
    """MIXR +1 is the COMPRESSOR's target and +2 the PCF's, both exclusive and
    both 1-based with the compressor offset a further one so 1 can mean the
    master strip. The distortion is a per-section flag in each channel group at
    +4. Measured 2026-08-27 by clicking each switch in ReBirth and diffing the
    save; see docs/FORMAT_RBS.md section 6. This used to write a `dist_sel`
    into +1, which is the compressor's byte."""
    m = bytearray(64)
    m[0] = 100
    m[1] = 0 if comp_target is None else comp_target + 2
    m[2] = 0 if pcf_target is None else pcf_target + 1
    for c in range(4):
        base = 16 + 12 * c
        m[base+0] = 1 if chan_on[c] else 0
        m[base+1] = 100; m[base+2] = 64
        m[base+3] = _kb(echo_send[c])
        m[base+4] = 1 if dist_on[c] else 0
    return chunk(b"MIXR", bytes(m))

DELAY_DEFAULTS = {"on": 1, "steps": 4, "triplet": 0, "feedback": 0, "pan": 64,
                  "send": 0, "target": None}

def _delay_spec(delay):
    """Normalise a delay fixture spec. The DELY chunk is [ver][steps 1..16]
    [triplet][feedback][pan], decoded from the shipped files (rbs.cpp,
    FORMAT_RBS.md); the manual calls byte 2 the triplet mode. `send` is the
    echo-send level and `target` the 0-based channel it routes from (None = the
    sounding channel). The defaults reproduce the old always-written delay chunk
    byte for byte, so a fixture that does not ask for the delay is unchanged."""
    d = dict(DELAY_DEFAULTS)
    d.update(delay or {})
    return d

def _chunk_dely(delay=None):
    s = _delay_spec(delay)
    d = bytearray(8)
    d[0] = 1 if s["on"] else 0
    d[1] = max(1, min(16, int(s["steps"])))
    d[2] = _kb(s["triplet"])
    d[3] = _kb(s["feedback"])
    d[4] = _kb(s["pan"])
    return chunk(b"DELY", bytes(d))

def _dely_trak(delay=None):
    """The delay's track, position 6 in TRKL (order DELY, DIST, PCF, COMP).
    Param N is chunk byte N, param 0 the unit on/off. Restates the chunk so the
    tick-0 dump is the song's real starting state, mirroring _dist_trak/_pcf_trak."""
    s = _delay_spec(delay)
    return _trak_events([(0, 4, _kb(s["pan"])), (0, 3, _kb(s["feedback"])),
                         (0, 2, _kb(s["triplet"])),
                         (0, 1, max(1, min(16, int(s["steps"])))),
                         (0, 0, 1 if s["on"] else 0)])
DIST_DEFAULTS = {"on": 0, "b1": 0, "b2": 64, "sections": (0, 0, 0, 0)}

def _dist_spec(dist):
    """Normalise a distortion fixture spec.

    Byte 1 and byte 2 are deliberately NOT named amount and tone here. Appendix
    C lists the distortion's controls as Shape 104 then Amount 105, and the file
    order is not the MIDI order elsewhere in this format, so which byte is which
    is a measurement rather than a reading. `sections` is a per-section flag
    tuple: the distortion is the one rack unit any number of sections can share.
    """
    d = dict(DIST_DEFAULTS)
    d.update(dist or {})
    return d

def _chunk_dist(dist=None):
    s = _dist_spec(dist)
    d = bytearray(8); d[0] = 1; d[1] = _kb(s["b1"]); d[2] = _kb(s["b2"])
    return chunk(b"DIST", bytes(d))

def _dist_trak(dist=None):
    """The distortion's track, position 6 in TRKL. Param N is chunk byte N and
    param 0 is the unit's on/off. The tick-0 dump is the song's real starting
    state, so it has to restate the chunk."""
    s = _dist_spec(dist)
    return _trak_events([(0, 2, _kb(s["b2"])), (0, 1, _kb(s["b1"])),
                         (0, 0, 1 if s["on"] else 0)])
COMP_DEFAULTS = {"on": 0, "ratio": 70, "threshold": 64, "target": None}

def _comp_spec(comp):
    """Normalise a compressor fixture spec. The COMP chunk's byte 1 and byte 2
    are provisionally Ratio then Threshold: a shipped song (Standard Components/
    Demo.rbs) carries [1, 70, 64, 127, ...], byte 3 = 127 looks like a meter
    default, and the manual lists exactly two controls (Ratio 106, Threshold 107)
    in that order. Which byte is which is a MEASUREMENT (the compressor probe),
    not settled by this reading. `target` is a 0-based channel index, `-1` for the
    master strip, or None for routed-nowhere."""
    d = dict(COMP_DEFAULTS)
    d.update(comp or {})
    return d

def _chunk_comp(comp=None):
    # Default (comp=None) is byte-for-byte the old chunk so non-comp fixtures are
    # unchanged; a spec writes the candidate Ratio/Threshold bytes.
    d = bytearray(8); d[0] = 1; d[3] = 0x7F
    if comp is not None:
        s = _comp_spec(comp); d[1] = _kb(s["ratio"]); d[2] = _kb(s["threshold"])
    return chunk(b"COMP", bytes(d))

def _comp_trak(comp=None):
    """COMP track, position 9 (last) in TRKL. Param N = chunk byte N, param 0 the
    on/off. comp=None keeps the OFF snapshot the fixtures have always carried, so
    ReBirth's master compressor stays disabled unless a fixture asks for it."""
    if comp is None or not _comp_spec(comp)["on"]:
        return _trak_events([(0, 2, 0), (0, 1, 0), (0, 0, 0)])
    s = _comp_spec(comp)
    return _trak_events([(0, 3, 0x7F), (0, 2, _kb(s["threshold"])),
                         (0, 1, _kb(s["ratio"])), (0, 0, 1)])
PCF_DEFAULTS = {"on": 0, "target": None, "freq": 64, "q": 0, "amt": 0,
                "pattern": 0, "decay": 64, "mode": 0}

def _pcf_spec(pcf):
    """Normalise a PCF fixture spec. `target` is a 0-based channel index or
    None for routed-nowhere; `mode` is 0 = LP, 1 = BP."""
    s = dict(PCF_DEFAULTS)
    s.update(pcf or {})
    return s

def _chunk_pcf(pcf=None):
    """PCF (12): [ver][freq][q][amt][pattern 0..54][decay][mode 0=LP/1=BP].
    The panel's slider row is Freq Q Amt Dec but the chunk wedges Pattern
    between Amt and Dec. Mapped 2026-08-27 over MIDI CC; see FORMAT_RBS.md."""
    s = _pcf_spec(pcf)
    d = bytearray(12)
    d[0] = 1
    d[1] = _kb(s["freq"]); d[2] = _kb(s["q"]); d[3] = _kb(s["amt"])
    d[4] = max(0, min(54, int(s["pattern"])))
    d[5] = _kb(s["decay"]); d[6] = 1 if s["mode"] else 0
    return chunk(b"PCF ", bytes(d))

def _pcf_trak(pcf=None):
    """The PCF's own track, position 7 in TRKL (the order is DELY, DIST, PCF,
    COMP). Param N is chunk byte N and param 0 is the unit's on/off. This has
    to restate the chunk: the tick-0 dump is what the song actually starts
    from, and a bare `param 1 = 0` reads back as Freq = 0."""
    s = _pcf_spec(pcf)
    return _trak_events([(0, 6, 1 if s["mode"] else 0), (0, 5, _kb(s["decay"])),
                         (0, 4, max(0, min(54, int(s["pattern"])))),
                         (0, 3, _kb(s["amt"])), (0, 2, _kb(s["q"])),
                         (0, 1, _kb(s["freq"])), (0, 0, 1 if s["on"] else 0)])

def _chunk_usri(author, title):
    a = author.encode("latin-1", "replace")[:40]
    body = (a + b"\x00" * (41 - len(a))) + ('"%s"' % title).encode("latin-1", "replace") + b"\x00"
    return chunk(b"USRI", body.ljust(712, b"\x00"))  # ReBirth expects a fixed 712-byte USRI

# HEAD prefix observed in default-skin songs: "[T[T" signature, four version
# bytes (the default-skin variant ends 01 00), then a NUL. The remainder is a
# free string padded to 256. We synthesise it; whether ReBirth validates the
# string is exactly what the two test files below determine.
_HEAD_PREFIX = bytes([0x5B, 0x54, 0x5B, 0x54, 0xBC, 0x04, 0x01, 0x00, 0x00])

def _build_head(note="Reborn generated test song", raw=None):
    if raw is not None:
        return chunk(b"HEAD", bytes(raw))
    body = _HEAD_PREFIX + note.encode("latin-1", "replace") + b"\x00"
    return chunk(b"HEAD", body.ljust(256, b"\x00"))

def _build_glob(tempo_bpm, loop_bars=4, shuffle=0x40):
    """Synthesised default GLOB: ver 01, flag 01, tempo*1000 at +2, LOOP LENGTH
    at +11, shuffle at +14, built-in skin name "Standard ReBirth" (no leading @)
    at +15.

    LOOP LENGTH (+11, u16 BE) = bars * 3. This is the field ReBirth's transport
    shows as "Loop Length" and what an export runs for. Decoded 2026-07-24 from
    three songs of independently known length: a 20-bar song stores 60, Calm
    (172-bar export) stores 516, Trakka Rekka (124-bar export) stores 372; the
    two stock default songs store 3 and 12 (1 and 4 bars). Previously this was
    hardcoded to Calm's 0x0204 = 516, so EVERY generated song inherited Calm's
    172-bar loop and exported 172 bars regardless of its content."""
    g = bytearray(512)
    g[0] = 0x01
    g[1] = 0x01
    struct.pack_into(">I", g, 2, int(round(tempo_bpm * 1000)))
    struct.pack_into(">H", g, 11, max(1, int(loop_bars)) * 3)
    g[14] = shuffle & 0xFF
    skin = b"Standard ReBirth\x00"
    g[15:15 + len(skin)] = skin
    return chunk(b"GLOB", bytes(g))

def _trak_empty(): return chunk(b"TRAK", _u32be(0))
def _trak_select(p=0): return chunk(b"TRAK", _u32be(1) + bytes([0x00, 0x01, p & 0x7F]))

def _trak_events(events):
    """events: list of (dt, param, value). Build a TRAK payload."""
    body = _u32be(len(events))
    for dt, param, value in events:
        # VLQ delta (dt <= 127 here, single byte)
        assert 0 <= dt <= 0x7F
        body += bytes([dt & 0x7F, param & 0xFF, value & 0xFF])
    return chunk(b"TRAK", body)

def _vlq(n):
    """MIDI variable-length quantity."""
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)

def _trak_abs(events_abs):
    """Build a TRAK from absolute-tick events [(tick, param, value), ...]."""
    events_abs = sorted(events_abs, key=lambda e: e[0])
    body = _u32be(len(events_abs))
    prev = 0
    for tick, param, value in events_abs:
        body += _vlq(tick - prev) + bytes([param & 0xFF, value & 0xFF])
        prev = tick
    return chunk(b"TRAK", body)

def _song_end_tick(loop_bars):
    """Tick of the song-end marker for a loop_bars-long song. ReBirth's song
    length is (last event tick // 32) + 1 bars, so the marker sits at the START
    of the final bar. Verified against a real 20-bar song: its last events are
    at tick 613 (bar 19) -> 20 bars."""
    return 32 * max(0, loop_bars - 1)

def _bass_song_trak(knobs, n_bars=4, pattern=0, active=1):
    """Song-start snapshot for a 303 track, matching the layout ReBirth writes
    (verified byte-for-byte against Calm's 303 tracks): a SINGLE tick-0 dump in
    DESCENDING param order 8..0, INCLUDING param 0 (the channel-active flag,
    1 = live). Param 0 is what makes song-mode export honour the knobs; earlier
    versions omitted it and re-dumped every bar, and song mode stayed on defaults
    (all resonance settings exported identically). Real songs keep the INSTRUMENT
    tracks tick-0 only; the song's length comes from the mixer track instead
    (see _mixer_trak)."""
    ev = [(0, 8, 1 if knobs.get("waveform", 0) else 0),
          (0, 7, _kb(knobs.get("accent", 0))),
          (0, 6, _kb(knobs.get("decay", 64))),
          (0, 5, _kb(knobs.get("envmod", 0))),
          (0, 4, _kb(knobs.get("reso", 0))),
          (0, 3, _kb(knobs.get("cutoff", 64))),
          (0, 2, _kb(knobs.get("tune", 64))),
          (0, 1, pattern & 0x7F),
          (0, 0, 1 if active else 0)]
    return _trak_abs(ev)

def _mixer_trak(n_bars=4, pcf_target=None, dist_on=(0, 0, 0, 0), comp_target=None,
                echo_send=(0, 0, 0, 0), chan_vol=100, vol_events=()):
    """Tick-0 per-channel volume/pan/send dump (params base 6+6c), plus a
    SONG-END MARKER re-dumping the same values at the start of the final bar.

    chan_vol is the per-channel fader (0..127, default 100). It is the input
    level into the master compressor: raising or lowering every channel's fader
    moves the summed master level the compressor detects, which is the extra
    dimension supertask 6 needs (source level x threshold x ratio). Default 100
    leaves every non-comp fixture byte-identical.

    The marker is what gives the song its length. ReBirth derives song/export
    length from the MIXER and EFFECT tracks, not the instrument tracks: a real
    20-bar song carries its last events at tick 613 (bar 19 -> 20 bars) on
    MIXR/DELY/DIST/COMP while its 303/808/909 tracks are tick-0 only, exactly
    the shape our files had, which is why ReBirth fell back to its 172-bar
    default. Re-dumping identical values changes no state, it only extends the
    track so (last tick // 32) + 1 == n_bars."""
    ev = []
    # param 1 is the compressor's target and param 2 the PCF's. The chunk is
    # the save-time panel; this dump is what the song starts from, and for the
    # PCF the two disagree in every shipped song.
    ev.append((0, 1, 0 if comp_target is None else comp_target + 2))
    ev.append((0, 2, 0 if pcf_target is None else pcf_target + 1))
    cv = _kb(chan_vol)
    for c in range(4):
        base = 6 + 6 * c
        ev.append((0, base + 0, cv))    # volume
        ev.append((0, base + 1, 64))    # pan centre
        ev.append((0, base + 2, _kb(echo_send[c])))   # echo send
        ev.append((0, base + 3, 1 if dist_on[c] else 0))   # distortion switch
    # Timed fader automation: (tick, value) steps applied to every channel's
    # volume. This is the level-STEP source for compressor ballistics; default
    # empty leaves the track byte-identical.
    for tick, val in vol_events:
        for c in range(4):
            ev.append((tick, 6 + 6 * c, _kb(val)))
    end = _song_end_tick(n_bars)
    if end > 0:
        for c in range(4):
            ev.append((end, 6 + 6 * c, cv))   # same volume, at the last bar
    return _trak_abs(ev)

def build_song(knobs, pattern, tempo=140.0, title="rbsgen test", author="rbsgen",
               template=None, head_note="Reborn generated test song", n_bars=4,
               device="bass1", drum_knobs=None, drum_hits=None, drum_accents=(),
               drum_flams=(), pcf=None, dist=None, delay=None, comp=None, knobs2=None,
               pattern2=None, patterns=None, bass_events=None, chan_vol=100,
               vol_events=()):
    """`patterns` is a list of step-lists filling the 303's bank slots, and
    `bass_events` a list of absolute-tick (tick, param, value) triples for its
    track, which together turn a one-pattern fixture into a SONG with an
    arrangement and knob rides. 303 track params are 0 active, 1 pattern select,
    2 tune, 3 cutoff, 4 resonance, 5 env mod, 6 decay, 7 accent, 8 waveform.
    There are 32 ticks to a bar. Both default to None and the output is then
    byte-identical to before they existed."""
    """device: 'bass1' (default; knobs+pattern used) or '808'/'909'
    (drum_knobs dict + drum_hits used; the 303s stay silent)."""
    # HEAD: synthesised with a free string by default (fully standalone). If a
    # template is given we lift its HEAD verbatim instead (fallback if ReBirth
    # turns out to validate the HEAD string).
    head_raw = None
    if template:
        head_raw = find_chunk(open(template, "rb").read(), b"HEAD")
    head = _build_head(note=head_note, raw=head_raw)
    glob = _build_glob(tempo, loop_bars=n_bars)

    dk = drum_knobs or {}
    is909 = (device == "909")
    # "bass1+808" sounds a 303 AND a drum machine at once. Needed to measure the
    # effect rack's TOPOLOGY: whether the distortion and the PCF are shared buses
    # that several sections pass through, or per-section inserts. That question
    # cannot be asked of a one-device fixture.
    two303 = (device == "bass1+bass2")
    both = (device == "bass1+808")
    if two303:
        c808 = _chunk_drum(b"808 ", 30)
        c909 = _chunk_drum(b"909 ", 31)
        c303a = _chunk_303(knobs, pattern)
        chan_on = (1, 1, 0, 0)
    elif both:
        grid = drum_grid(drum_hits or {}, is909=False, accents=drum_accents)
        c808 = _chunk_drum_pattern(b"808 ", _header_808(dk), grid)
        c909 = _chunk_drum(b"909 ", 31)
        c303a = _chunk_303(knobs, pattern)
        chan_on = (1, 0, 1, 0)
    elif device in ("808", "909"):
        grid = drum_grid(drum_hits or {}, is909=is909, accents=drum_accents,
                         flams=drum_flams)
        hdr808 = _header_808(dk) if not is909 else None
        hdr909 = _header_909(dk) if is909 else None
        c808 = _chunk_drum_pattern(b"808 ", hdr808, grid) if not is909 else _chunk_drum(b"808 ", 30)
        c909 = _chunk_drum_pattern(b"909 ", hdr909, grid) if is909 else _chunk_drum(b"909 ", 31)
        c303a = _chunk_303_silent()
        chan_on = (0, 0, 0, 1) if is909 else (0, 0, 1, 0)
    else:
        c808 = _chunk_drum(b"808 ", 30)
        c909 = _chunk_drum(b"909 ", 31)
        c303a = _chunk_303(knobs, pattern, patterns)
        chan_on = (1, 0, 0, 0)

    # The PCF routes to whichever section this fixture sounds, unless the spec
    # names one. Routed nowhere by default, which is what every non-PCF fixture
    # needs: the parity protocol requires the other effects silent.
    knobs2 = knobs2 or knobs
    pattern2 = pattern2 if pattern2 is not None else pattern
    dist_spec = _dist_spec(dist)
    pcf_spec = _pcf_spec(pcf)
    if pcf is not None and pcf_spec["target"] is None:
        pcf_spec["target"] = chan_on.index(1) if 1 in chan_on else 0
    pcf_target = pcf_spec["target"] if pcf is not None else None

    # Delay is a per-channel SEND effect: route the sounding channel's echo send
    # to it unless the spec names a target. delay=None leaves the delay chunk at
    # its old default and every echo send at zero, so non-delay fixtures are
    # byte-identical.
    delay_spec = _delay_spec(delay) if delay is not None else None
    echo_send = [0, 0, 0, 0]
    if delay is not None:
        tgt = delay_spec["target"]
        if tgt is None:
            tgt = chan_on.index(1) if 1 in chan_on else 0
        echo_send[tgt] = _kb(delay_spec["send"])
    echo_send = tuple(echo_send)

    # Compressor: routed nowhere unless a comp spec asks for it. target -1 = the
    # master strip (MIXR+1 = 1), 0..3 = a section (2..5); None routes to the
    # sounding channel. comp=None leaves the COMP chunk/track at their old OFF
    # state, so non-comp fixtures are byte-identical.
    comp_spec = _comp_spec(comp) if comp is not None else None
    comp_target = None
    if comp is not None and comp_spec["on"]:
        ct = comp_spec["target"]
        comp_target = ct if ct is not None else (chan_on.index(1) if 1 in chan_on else 0)

    devl = group(b"CAT ", b"DEVL",
                 _chunk_mixr(chan_on=chan_on, pcf_target=pcf_target,
                             dist_on=dist_spec["sections"], echo_send=echo_send,
                             comp_target=comp_target) +
                 _chunk_dely(delay_spec) + _chunk_pcf(pcf_spec) + _chunk_dist(dist_spec) + _chunk_comp(comp_spec) +
                 c303a + (_chunk_303(knobs2, pattern2) if two303 else _chunk_303_silent())
                 + c808 + c909)
    # Nine tracks (MIXR, 303a, 303b, 808, 909, DELY, DIST, PCF, COMP). The COMP
    # track defaults to an explicit OFF snapshot (param 0 = 0) so ReBirth's master
    # compressor is disabled on load rather than left on by default; a comp spec
    # turns it on with its ratio/threshold. This keeps the compressor out of every
    # export that does not ask for it.
    comp_trak = _comp_trak(comp_spec)
    # A song's 303 track carries its arrangement and its knob rides on top of
    # the tick-0 snapshot. The snapshot still leads, so the song starts from a
    # known state whatever the rides do afterwards.
    def _bass_trak(kn, nb):
        if bass_events is None:
            return _bass_song_trak(kn, nb)
        base = [(0, 8, 1 if kn.get("waveform", 0) else 0),
                (0, 7, _kb(kn.get("accent", 0))), (0, 6, _kb(kn.get("decay", 64))),
                (0, 5, _kb(kn.get("envmod", 0))), (0, 4, _kb(kn.get("reso", 0))),
                (0, 3, _kb(kn.get("cutoff", 64))), (0, 2, _kb(kn.get("tune", 64))),
                (0, 1, 0), (0, 0, 1)]
        return _trak_abs(base + [(int(t), int(p), _kb(v)) for t, p, v in bass_events])
    if two303:
        t303a = _bass_trak(knobs, n_bars)
        t808 = _trak_select(0)
        t909 = _trak_select(0)
    elif both:
        t303a = _bass_trak(knobs, n_bars)
        t808 = _drum_song_trak(_header_808(dk), n_bars)
        t909 = _trak_select(0)
    elif device == "808":
        t303a = _trak_select(0)
        t808 = _drum_song_trak(_header_808(dk), n_bars)
        t909 = _trak_select(0)
    elif device == "909":
        t303a = _trak_select(0)
        t808 = _trak_select(0)
        t909 = _drum_song_trak(_header_909(dk), n_bars)
    else:
        t303a = _bass_trak(knobs, n_bars)
        t808 = _trak_select(0)
        t909 = _trak_select(0)
    # TRKL order is MIXR, 303a, 303b, 808, 909, DELY, DIST, PCF, COMP. The two
    # middle effects are NOT in the DEVL order, which puts PCF before DIST.
    trkl = group(b"CAT ", b"TRKL",
                 _mixer_trak(n_bars, pcf_target=pcf_target,
                             dist_on=dist_spec["sections"], echo_send=echo_send,
                             comp_target=comp_target, chan_vol=chan_vol,
                             vol_events=vol_events) + t303a
                 + (_bass_song_trak(knobs2, n_bars) if two303 else _trak_select(0)) +
                 t808 + t909
                 + (_dely_trak(delay_spec) if delay is not None else _trak_events([(0, 1, 0)]))
                 + _dist_trak(dist_spec) + _pcf_trak(pcf_spec) + comp_trak)
    return group(b"CAT ", b"RB40", head + glob + _chunk_usri(author, title) + devl + trkl)

def write_song(out_path, knobs, pattern, tempo=140.0, title="rbsgen test", author="rbsgen", template=None, n_bars=4):
    data = build_song(knobs, pattern, tempo, title, author, template, n_bars=n_bars)
    open(out_path, "wb").write(data)
    return len(data)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate a controlled .rbs test song (one 303, static knobs, no automation).")
    ap.add_argument("--out", required=True)
    ap.add_argument("--patch", help="baseline .rbs to patch in place (recommended; ReBirth-compatible)")
    ap.add_argument("--template", help="scratch mode: copy HEAD/GLOB layout from this .rbs (Reborn-only)")
    ap.add_argument("--voice", choices=["bass1", "bass2"], default="bass2", help="which 303 to use (patch mode)")
    ap.add_argument("--tempo", type=float, default=140.0)
    ap.add_argument("--title", default="rbsgen test")
    ap.add_argument("--author", default="rbsgen")
    ap.add_argument("--note", type=int, default=9)
    ap.add_argument("--tune", type=int, default=64)
    ap.add_argument("--cutoff", type=int, default=64)
    ap.add_argument("--reso", type=int, default=0)
    ap.add_argument("--envmod", type=int, default=0)
    ap.add_argument("--decay", type=int, default=64)
    ap.add_argument("--accent", type=int, default=0)
    ap.add_argument("--waveform", type=int, default=0)
    ap.add_argument("--accent-steps", default="")
    ap.add_argument("--bars", type=int, default=4, help="song length in bars (song-mode export)")
    args = ap.parse_args(argv)

    accents = tuple(int(x) for x in args.accent_steps.split(",") if x.strip())
    knobs = dict(tune=args.tune, cutoff=args.cutoff, reso=args.reso, envmod=args.envmod,
                 decay=args.decay, accent=args.accent, waveform=args.waveform)
    pattern = repeated_note(args.note, accents=accents)

    if args.patch:
        n = write_patched(args.out, args.patch, knobs, pattern, voice=args.voice)
        print("patched %s -> %s (%d bytes, size preserved): voice=%s note=%d cutoff=%d reso=%d envmod=%d decay=%d wave=%d"
              % (args.patch, args.out, n, args.voice, args.note, args.cutoff, args.reso, args.envmod, args.decay, args.waveform))
    else:
        n = write_song(args.out, knobs, pattern, tempo=args.tempo, title=args.title, author=args.author, template=args.template, n_bars=args.bars)
        print("wrote %s (%d bytes) [scratch: standalone, opens in ReBirth]: note=%d cutoff=%d reso=%d envmod=%d"
              % (args.out, n, args.note, args.cutoff, args.reso, args.envmod))
    return 0

if __name__ == "__main__":
    sys.exit(main())
