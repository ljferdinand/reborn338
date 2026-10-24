#!/usr/bin/env python3
"""Measure the batch G 303 fixtures, ReBirth side and ours, one method both sides.

Step-locked throughout: the fixtures are generated so the sequencer position is
known exactly, which avoids the onset-detector artifact that once produced a
fictitious "15 dB decay/level trade" (docs/research/FINDINGS_AUDIT.md).

Never null-tests: ReBirth's exports are not sample-deterministic (see
docs/MEASUREMENT_PROTOCOL.md), so everything here is level, spectrum or envelope.

Usage:
    python batchG_303.py <dir-of-wavs> [group]
    group is one of: accent register tune slide gate wave emdec  (default all)
"""
import glob
import os
import sys
import wave

import numpy as np

TEMPO = 120.0
STEPS_PER_BAR = 16
BARS = 4


def load(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    return x, sr


def step_frames(sr):
    """Samples per 16th-note step."""
    return int(round(sr * 60.0 / TEMPO / 4.0))


def db(v):
    return -999.0 if v <= 0 else 20.0 * np.log10(v)


def centroid(seg, sr, lo=40.0, hi=12000.0):
    """Magnitude-weighted spectral centroid: our brightness proxy."""
    if len(seg) < 256:
        return float("nan")
    N = 1 << int(np.ceil(np.log2(len(seg))))
    y = np.zeros(N)
    y[:len(seg)] = seg * np.hanning(len(seg))
    P = np.abs(np.fft.rfft(y))
    f = np.fft.rfftfreq(N, 1.0 / sr)
    m = (f >= lo) & (f <= hi)
    tot = P[m].sum()
    return float((f[m] * P[m]).sum() / tot) if tot > 0 else float("nan")


def dominant(seg, sr, lo=40.0, hi=1500.0):
    """Strongest partial in a band: our pitch proxy."""
    if len(seg) < 256:
        return float("nan")
    N = 1 << 15
    y = np.zeros(N)
    m0 = min(N, len(seg))
    y[:m0] = seg[:m0] * np.hanning(m0)
    P = np.abs(np.fft.rfft(y)) ** 2
    f = np.fft.rfftfreq(N, 1.0 / sr)
    m = (f >= lo) & (f <= hi)
    return float(f[m][P[m].argmax()]) if m.any() else float("nan")


def steps(path, count=STEPS_PER_BAR, bar=0):
    """Per-step (peak dB, rms dB, centroid Hz, dominant Hz) for one bar."""
    x, sr = load(path)
    mono = x.mean(axis=1)
    w = step_frames(sr)
    base = bar * STEPS_PER_BAR * w
    out = []
    for s in range(count):
        seg = mono[base + s * w: base + (s + 1) * w]
        if len(seg) == 0:
            break
        out.append((db(np.abs(seg).max()), db(np.sqrt((seg ** 2).mean())),
                    centroid(seg, sr), dominant(seg, sr)))
    return out


def whole(path):
    """Whole-file peak, rms, centroid, dominant."""
    x, sr = load(path)
    mono = x.mean(axis=1)
    return (db(np.abs(mono).max()), db(np.sqrt((mono ** 2).mean())),
            centroid(mono, sr), dominant(mono, sr))


def find(d, stem):
    hits = glob.glob(os.path.join(d, stem + ".wav"))
    return hits[0] if hits else None


# ---------------------------------------------------------------------------

def group_accent(d):
    print("=== G1 ACCENT ===")
    print()
    print("Depth law: the SAME steps 0-3 in the run4 file vs the none file at")
    print("the same knob. Comparing accented steps against LATER unaccented ones")
    print("in one file conflates accent with position: steps 0-3 sit at the")
    print("pattern start where the filter envelope has not been left part-open")
    print("by preceding notes, and that alone shifts the centroid a long way.")
    print()
    print(f"{'knob':>5s} {'none pk':>8s} {'run4 pk':>8s} {'delta dB':>9s} "
          f"{'none cent':>10s} {'run4 cent':>10s} {'cent ratio':>11s}")
    for knob in (0, 32, 64, 96, 127):
        pr = find(d, f"g303_acc{knob:03d}_run4")
        pn = find(d, f"g303_acc{knob:03d}_none")
        if not (pr and pn):
            continue
        sr_ = steps(pr)[0:4]
        sn = steps(pn)[0:4]
        apk = np.mean([s[0] for s in sr_]); upk = np.mean([s[0] for s in sn])
        ace = np.mean([s[2] for s in sr_]); uce = np.mean([s[2] for s in sn])
        print(f"{knob:5d} {upk:8.2f} {apk:8.2f} {apk - upk:+9.2f} "
              f"{uce:10.0f} {ace:10.0f} {ace / uce:11.3f}")

    print()
    print("Control: does the accent KNOB alone do anything with no accented")
    print("steps? (the 808's knob does not; this is the same question.)")
    base = None
    for knob in (0, 32, 64, 96, 127):
        pn = find(d, f"g303_acc{knob:03d}_none")
        if not pn:
            continue
        pk, rms, c, _dm = whole(pn)
        if base is None:
            base = (pk, c)
        print(f"  knob {knob:3d}: peak {pk:7.2f} ({pk - base[0]:+5.2f})  "
              f"centroid {c:6.0f} Hz ({c - base[1]:+5.0f})")

    print()
    print("Accumulation: the four steps WITHIN a run. A per-note accent makes")
    print("them identical; a charge-storing one builds across the run.")
    print()
    print(f"{'knob':>5s} " + " ".join(f"{'s'+str(i)+' pk':>8s}" for i in range(4))
          + f" {'spread':>7s} | " + " ".join(f"{'s'+str(i)+' cent':>9s}" for i in range(4))
          + f" {'spread':>7s}")
    for knob in (32, 64, 96, 127):
        p = find(d, f"g303_acc{knob:03d}_run4")
        if not p:
            continue
        st = steps(p)[:4]
        pk = [s[0] for s in st]; ce = [s[2] for s in st]
        print(f"{knob:5d} " + " ".join(f"{v:8.2f}" for v in pk)
              + f" {max(pk) - min(pk):7.2f} | "
              + " ".join(f"{v:9.0f}" for v in ce)
              + f" {max(ce) - min(ce):7.0f}")

    print()
    print("Run length at knob 96 (does the FIRST accent differ by run length?):")
    for tag in ("single", "run2", "run4"):
        p = find(d, f"g303_acc096_{tag}")
        if not p:
            continue
        st = steps(p)
        n = {"single": 1, "run2": 2, "run4": 4}[tag]
        acc = st[:n]
        print(f"  {tag:7s} accented peaks " +
              " ".join(f"{s[0]:7.2f}" for s in acc) +
              f"   first-accent centroid {acc[0][2]:.0f} Hz")

    print()
    print("Tail: the note immediately AFTER a run of four, vs the steady level.")
    for knob in (32, 64, 96, 127):
        p = find(d, f"g303_acc{knob:03d}_run4")
        if not p:
            continue
        st = steps(p)
        after = st[4][0]
        steady = np.mean([s[0] for s in st[6:16]])
        print(f"  knob {knob:3d}: step4 {after:7.2f}  steady {steady:7.2f}  "
              f"excess {after - steady:+5.2f} dB")


def group_tune(d):
    print("=== G3 TUNE ===")
    print()
    print("Manual p48: '+/-1 octaves in steps of semitones'. Semitones are")
    print("measured from the dominant partial, relative to knob 64.")
    print()
    ref = None
    rows = []
    for t in (0, 16, 32, 48, 64, 80, 96, 112, 127):
        p = find(d, f"g303_tune{t:03d}")
        if not p:
            continue
        _pk, _rms, _c, dom = whole(p)
        rows.append((t, dom))
        if t == 64:
            ref = dom
    print(f"{'knob':>5s} {'dominant Hz':>12s} {'semitones vs 64':>16s} {'expected':>9s}")
    for t, dom in rows:
        st = 12.0 * np.log2(dom / ref) if ref and dom > 0 else float("nan")
        # a symmetric +/-12 over 0..127 centred at 64
        exp = (t - 64) * (12.0 / 64.0) if t < 64 else (t - 64) * (12.0 / 63.0)
        print(f"{t:5d} {dom:12.2f} {st:16.2f} {exp:9.2f}")
    print()
    print("Fine points around centre: is it quantised to whole semitones?")
    for t in (62, 64, 66, 68, 70):
        p = find(d, f"g303_tunefine{t:03d}") or find(d, f"g303_tune{t:03d}")
        if not p:
            continue
        _pk, _rms, _c, dom = whole(p)
        st = 12.0 * np.log2(dom / ref) if ref and dom > 0 else float("nan")
        print(f"  knob {t:3d}: dominant {dom:8.2f} Hz  = {st:+.3f} semitones")


def group_register(d):
    print("=== G2 REGISTER ===")
    print()
    print(f"{'fixture':>16s} {'dominant Hz':>12s} {'semitones vs note9':>19s} "
          f"{'centroid':>9s} {'peak':>7s}")
    ref = None
    for n in (0, 3, 6, 9, 12):
        p = find(d, f"g303_note{n:02d}")
        if not p:
            continue
        pk, _r, c, dom = whole(p)
        if n == 9:
            ref = dom
    for n in (0, 3, 6, 9, 12):
        p = find(d, f"g303_note{n:02d}")
        if not p:
            continue
        pk, _r, c, dom = whole(p)
        st = 12.0 * np.log2(dom / ref) if ref and dom > 0 else float("nan")
        print(f"{'note%02d' % n:>16s} {dom:12.2f} {st:19.2f} {c:9.0f} {pk:7.2f}")
    for tag in ("octdown", "octup"):
        p = find(d, f"g303_{tag}")
        if not p:
            continue
        pk, _r, c, dom = whole(p)
        st = 12.0 * np.log2(dom / ref) if ref and dom > 0 else float("nan")
        print(f"{tag:>16s} {dom:12.2f} {st:19.2f} {c:9.0f} {pk:7.2f}")


def group_slide(d):
    print("=== G4 SLIDE ===")
    print()
    print("Dominant partial through the first two steps, in 10 ms hops, so the")
    print("glide can be seen directly rather than inferred.")
    for tag in ("slide_off_up", "slide_on_up", "slide_on_down"):
        p = find(d, f"g303_{tag}")
        if not p:
            continue
        x, sr = load(p)
        mono = x.mean(axis=1)
        w = step_frames(sr)
        hop = int(0.010 * sr)
        seq = []
        for i in range(0, 2 * w, hop):
            seg = mono[i:i + int(0.030 * sr)]
            seq.append(dominant(seg, sr))
        print(f"  {tag}:")
        print("    " + " ".join(f"{v:6.0f}" for v in seq[:26]))


def group_gate(d):
    print("=== G5 GATE / REST ===")
    for tag in ("gate_all", "gate_alt", "gate_sparse"):
        p = find(d, f"g303_{tag}")
        if not p:
            continue
        st = steps(p)
        print(f"  {tag:12s} step peaks: " + " ".join(f"{s[0]:6.1f}" for s in st))


def group_wave(d):
    print("=== G6 WAVEFORM x CUTOFF ===")
    print(f"{'fixture':>22s} {'peak':>7s} {'rms':>7s} {'centroid':>9s}")
    for wave_i in (0, 1):
        for cut in (32, 64, 96):
            p = find(d, f"g303_wave{wave_i}_cut{cut:03d}")
            if not p:
                continue
            pk, rms, c, _d = whole(p)
            name = f"{'saw' if wave_i == 0 else 'square'} cut{cut}"
            print(f"{name:>22s} {pk:7.2f} {rms:7.2f} {c:9.0f}")


def group_emdec(d):
    print("=== G7 ENV MOD x DECAY ===")
    print(f"{'envmod':>7s} {'decay':>6s} {'peak':>7s} {'rms':>7s} {'centroid':>9s}")
    for em in (0, 64, 127):
        for dec in (0, 64, 127):
            p = find(d, f"g303_em{em:03d}_dec{dec:03d}")
            if not p:
                continue
            pk, rms, c, _d = whole(p)
            print(f"{em:7d} {dec:6d} {pk:7.2f} {rms:7.2f} {c:9.0f}")


GROUPS = {
    "accent": group_accent, "register": group_register, "tune": group_tune,
    "slide": group_slide, "gate": group_gate, "wave": group_wave,
    "emdec": group_emdec,
}

if __name__ == "__main__":
    d = sys.argv[1]
    want = sys.argv[2:] or list(GROUPS)
    for g in want:
        GROUPS[g](d)
        print()
