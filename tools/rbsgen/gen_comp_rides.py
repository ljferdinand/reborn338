#!/usr/bin/env python3
"""Compressor RIDE probe (clean-room, 2026-10-01; Luke's decision 3 of 2026-09-27).

Does ReBirth's Song mode play a timed COMP track ride, the way it plays the
DELY track's (fixtures-2026-09-27/delay-rides)? The sixteen demo songs carry 156
such rides, 65 on Ratio (param 1) and 91 on Threshold (param 2), none on the
unit switch or the meter. When this probe was made we carried them and did not
apply them (BACKLOG 5); ReBirth plays them, and so does the engine since.

Source and laws are batch 2's (gen_comp_law2.py): one 303 drone, fader 127
(-15.8 dBFS), the master compressor, nothing else. There, ratio 0 is an exact
bypass and ratio 127 at threshold 0 takes about 16.5 dB off, so each ride below
moves the output by many dB if it is played and by nothing if it is not.

  ride fixtures, one control each, the ride at tick 64 (bar 3, 4.0 s at 120 BPM):
    cr_ratio_up   threshold 0,   ratio   0 -> 127
    cr_ratio_dn   threshold 0,   ratio 127 -> 0
    cr_thr_up     ratio 127,     threshold   0 -> 127
    cr_thr_dn     ratio 127,     threshold 127 -> 0
  static references, no ride (the two ends of every ride, in the same batch):
    cr_ref_r000_t000, cr_ref_r127_t000, cr_ref_r127_t127

isolation_check.py --effect comp fails each ride fixture for exactly one reason,
the timed COMP event under test, and passes every reference. No binary-analysis
value is used.

The first seven showed a second thing: our STATIC compressed level is about 3 dB
low against ReBirth while our compressor DSP alone (comp_wav) is not, because the
engine's master compressor runs before its 0.72 output headroom. Fixing that needs
to know where ReBirth's MASTER LEVEL fader sits relative to the compressor, so a
second group varies only the master byte (MIXR byte 0; Master Level is never
automated, manual p72, so it lives in the chunk alone):

    mt_off_mMMM          compressor off, master byte MMM (the fader's own gain)
    mt_r127_t000_mMMM    ratio 127, threshold 0, master byte MMM
    mt_r127_t127_mMMM    ratio 127, threshold 127, master byte MMM

for MMM in 70 and 115. Fader BEFORE the compressor: the ratio-127 outputs barely
move with the master byte (the compressor pins them). Fader AFTER it: they move
by the fader's full gain, about -11.1 dB at 70 and +5.6 dB at 115.

A third group routes the compressor to the 303's own SECTION instead of the
master (MIXR +1 = 2), the path BACKLOG 16 records as never measured, and varies
the section's pan and fader to place the section compressor relative to each:

    st_off_pPPP_vVVV        compressor off
    st_rRRR_tTTT_pPPP_vVVV  compressor on the section

with pan PPP 64 (centre) or 0 (hard left) and channel fader VVV 127 or 90. Pan
BEFORE the compressor: hard left reaches it about 3 dB hotter than centre, and a
pinned output does not follow. Fader BEFORE it: the pinned output barely moves
between faders 127 and 90; after it, it falls by the fader's ~13.7 dB.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

SRC = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64, accent=0, waveform=0)
RIDE_TICK = 64

def drone(length=16):
    return [G.step(note=0, slide=True) for _ in range(length)]

def C(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=-1)   # -1 = MASTER

_orig_comp_trak = G._comp_trak
EXTRA = []   # absolute (tick, param, value) appended after the tick-0 dump

def _comp_trak_with_rides(comp=None):
    """rbsgen's COMP track with EXTRA appended as timed events. Same tick-0 dump
    as _comp_trak (meter 0x7F, threshold, ratio, on), so a fixture with no EXTRA
    is byte-identical to what rbsgen writes."""
    if not EXTRA:
        return _orig_comp_trak(comp)
    s = G._comp_spec(comp)
    ev = [(0, 3, 0x7F), (0, 2, G._kb(s["threshold"])), (0, 1, G._kb(s["ratio"])), (0, 0, 1)]
    last = 0
    for tick, p, v in sorted(EXTRA):
        ev.append((tick - last, p, v)); last = tick
    return G._trak_events(ev)
G._comp_trak = _comp_trak_with_rides

_orig_chunk_mixr = G._chunk_mixr
MASTER = [100]   # MIXR byte 0; rbsgen always writes 100

PAN = [64]       # the 303 section's pan; rbsgen always writes 64

def _chunk_mixr_with_master(*args, **kw):
    """rbsgen's MIXR chunk with byte 0 (the master level) and the first section's
    pan (payload 16 + 2) replaced. The chunk is id, u32 size, payload, so payload
    byte N is chunk byte 8 + N."""
    data = bytearray(_orig_chunk_mixr(*args, **kw))
    data[8] = MASTER[0]
    data[8 + 16 + 2] = PAN[0]
    return bytes(data)
G._chunk_mixr = _chunk_mixr_with_master

_orig_mixer_trak = G._mixer_trak

def _mixer_trak_with_pan(*args, **kw):
    """rbsgen's mixer track with the first section's pan (param 6 + 1) set to
    PAN. The track, not the chunk, is what Song mode plays."""
    orig_abs = G._trak_abs
    G._trak_abs = lambda ev: orig_abs([(t, p, PAN[0] if p == 7 else v) for t, p, v in ev])
    try:
        return _orig_mixer_trak(*args, **kw)
    finally:
        G._trak_abs = orig_abs
G._mixer_trak = _mixer_trak_with_pan

def S(ratio, threshold):
    return dict(on=1, ratio=ratio, threshold=threshold, target=0)    # 0 = the 303's section

def cases():
    """(name, comp spec, extra COMP events, master byte, pan, fader, what it answers)."""
    T = RIDE_TICK
    out = [
        ("cr_ratio_up", C(0, 0),     [(T, 1, 127)], 100, 64, 127, "threshold 0, ratio 0 -> 127 at tick 64"),
        ("cr_ratio_dn", C(127, 0),   [(T, 1, 0)],   100, 64, 127, "threshold 0, ratio 127 -> 0 at tick 64"),
        ("cr_thr_up",   C(127, 0),   [(T, 2, 127)], 100, 64, 127, "ratio 127, threshold 0 -> 127 at tick 64"),
        ("cr_thr_dn",   C(127, 127), [(T, 2, 0)],   100, 64, 127, "ratio 127, threshold 127 -> 0 at tick 64"),
        ("cr_ref_r000_t000", C(0, 0),     [], 100, 64, 127, "reference: ratio 0, threshold 0, no ride"),
        ("cr_ref_r127_t000", C(127, 0),   [], 100, 64, 127, "reference: ratio 127, threshold 0, no ride"),
        ("cr_ref_r127_t127", C(127, 127), [], 100, 64, 127, "reference: ratio 127, threshold 127, no ride"),
    ]
    for m in (70, 115):
        out += [
            (f"mt_off_m{m:03d}",       None,         [], m, 64, 127, f"compressor off, master byte {m}"),
            (f"mt_r127_t000_m{m:03d}", C(127, 0),    [], m, 64, 127, f"ratio 127, threshold 0, master byte {m}"),
            (f"mt_r127_t127_m{m:03d}", C(127, 127),  [], m, 64, 127, f"ratio 127, threshold 127, master byte {m}"),
        ]
    for pan, vol in ((64, 127), (0, 127), (64, 90)):
        tag = f"p{pan:03d}_v{vol:03d}"
        out += [
            (f"st_off_{tag}",       None,        [], 100, pan, vol, f"compressor off, pan {pan}, fader {vol}"),
            (f"st_r127_t000_{tag}", S(127, 0),   [], 100, pan, vol, f"section compressor, ratio 127, threshold 0, pan {pan}, fader {vol}"),
            (f"st_r127_t127_{tag}", S(127, 127), [], 100, pan, vol, f"section compressor, ratio 127, threshold 127, pan {pan}, fader {vol}"),
        ]
    out.append(("st_r000_t000_p064_v127", S(0, 0), [], 100, 64, 127,
                "section compressor at ratio 0: is the section path an exact bypass"))
    # The section group showed the fader comes AFTER the section compressor and
    # left one unknown, the gain between the compressor and the output. Only the
    # threshold-0 cell constrains it, so sweep the threshold (each pinned point
    # reads it again, against batch 2's master-path ts_r127_tTTT) and add a
    # mid-ratio point.
    for t in (16, 32, 48, 64, 96):
        out.append((f"st_r127_t{t:03d}_p064_v127", S(127, t), [], 100, 64, 127,
                    f"section compressor, ratio 127, threshold {t}, fader 127"))
    out.append(("st_r064_t000_p064_v127", S(64, 0), [], 100, 64, 127,
                "section compressor, ratio 64, threshold 0, fader 127"))
    # The section sits ~6 dB above the master path when both pin. A master
    # detector that sums L and R would read a centred mono source 6 dB hot and
    # produce exactly that, so pan the source hard left into the MASTER
    # compressor: same left-channel level as centre (the near side is unity),
    # half the channels. A max-of-channels detector gives the centred result, a
    # power mean ~3 dB above it, an L+R sum ~6 dB above it.
    for t in (0, 32, 64):
        out.append((f"mp_r127_t{t:03d}_p000", C(127, t), [], 100, 0, 127,
                    f"master compressor, ratio 127, threshold {t}, source hard left"))
    return out

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=G.workspace_path("fixtures-2026-10-01", "comp-rides"))
    a = ap.parse_args(argv); os.makedirs(a.out_dir, exist_ok=True)
    rows = []
    for name, comp, extra, master, pan, vol, why in cases():
        EXTRA[:] = extra
        MASTER[0] = master
        PAN[0] = pan
        data = G.build_song(SRC, drone(), tempo=120.0, n_bars=4, title=name,
                            author="rbsgen", device="bass1", comp=comp, chan_vol=vol)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why)); print(f"  {name}.rbs  {len(data):,} bytes")
    EXTRA[:] = []
    MASTER[0] = 100
    PAN[0] = 64
    man = ["# Compressor ride probe (2026-10-01)", "",
           "Does Song mode play a timed COMP track ride? One 303 drone at fader 127,",
           "the master compressor, nothing else, 120 BPM, 4 bars. Each ride lands at",
           "tick 64 (bar 3, 4.0 s). The `mt_` group holds the compressor setting and",
           "varies only the master level byte, to place ReBirth's master fader before",
           "or after the compressor. The `st_` group routes the compressor to the 303's",
           "section and varies its pan and fader. Generated by",
           "`tools/rbsgen/gen_comp_rides.py`.", "",
           "| fixture | what it answers |", "|---|---|"]
    man += [f"| `{n}` | {w} |" for n, w in rows]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w", encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
