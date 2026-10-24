#!/usr/bin/env python3
"""Fit the 303's OUTPUT LEVEL against the cutoff and resonance knobs (B6).

THE TARGET NEEDS NO CROSS-SIDE CALIBRATION, which is the whole reason this
measurement is trustworthy where every spectral one on this batch was not. Each
side's level is expressed relative to ITS OWN cutoff-0 resonance-0 cell, so gain
staging, mixer levels and export calibration cancel exactly. What is left is how
much the knobs move the output, which is a property of the filter alone.

Measured 2026-08-29 on batch B1 and confirmed on B5: at resonance 0 ReBirth's
303 gets **10.76 dB louder** as the cutoff knob opens and ours gets **2.38**. The
gap is 8.4 dB at resonance 0, 1.7 at 32, nil at 64 and 96, and reopens to 4.5 at
127. Luke's ear on the isolated listening set says the same thing in words:
ReBirth is louder and fuller underneath, and the resonant character is right.

WHY OURS BARELY MOVES. A saw's energy is dominated by its fundamental. Our corner
at the bottom of the knob sits at about 237 Hz, well above the drone's 65 Hz, so
closing the cutoff removes harmonics and leaves the fundamental alone, which
costs about 2 dB. For ReBirth to lose 10.8 dB the fundamental itself has to go,
so its corner at the bottom of the knob must be BELOW 65 Hz. The knob's span in
octaves is the suspect, not its top: the resonant peak already matches at both
ends of the knob to within 0.07 octaves at resonance 127.

    python fit_level.py <rebirth-dir> --check CUT_SLOPE=4.6 CUT_SCALE=0.24
    python fit_level.py <rebirth-dir> --grid span
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

WS = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
EXE = os.path.join(WS, "reborn", "build", "Release", "reborn_render.exe")
FIX = os.path.join(WS, "fixtures-2026-08-29", "b1-303-filter")

CUT = (0, 16, 32, 48, 64, 80, 96, 112, 127)
RESO = (0, 32, 64, 96, 127)
NAMES = ["b1_c%03d_r%03d" % (c, r) for r in RESO for c in CUT]
BASE = "b1_c000_r000"
WINDOW = (0.6, 5.5)

# The top of the cutoff knob, in Hz, that the shipped constants produce. Held
# fixed while the SPAN is searched, because the resonant peak says the top is
# already right and only the bottom is in question.
TOP_HZ = 0.78 * 407.0 * 2.0 ** 2.9


def rms_db(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)[int(WINDOW[0] * sr):int(WINDOW[1] * sr)]
    return 20.0 * math.log10(float(np.sqrt((x * x).mean())) + 1e-30)


def surface(d):
    """Level in dB, each side normalised by its OWN MEAN over the whole plane.

    NOT by one cell. The first version of this anchored on `b1_c000_r000`, which
    is the single most anomalous cell in the set: ReBirth's filter is nearly shut
    there and ours is not, so anchoring on it added that one cell's error to all
    45 and read 6.47 dB rms where the plane's real disagreement is 1.84. Same
    class of mistake as anchoring a spectrum on a bin that is not in the
    passband. A mean over the plane cannot be moved by any single cell.
    """
    out = np.full(len(NAMES), np.nan)
    for i, n in enumerate(NAMES):
        q = os.path.join(d, n + ".wav")
        if os.path.exists(q):
            out[i] = rms_db(q)
    if not np.isfinite(out).any():
        return None
    return out - np.nanmean(out)


def render(overrides, out, corner=False):
    env = dict(os.environ)
    env["REBORN_BASS_ENGINE"] = "tpt"
    for k, v in overrides.items():
        env["REBORN_" + k] = str(v)
    names = NAMES + (["b1_ref"] if corner else [])
    for n in names:
        subprocess.run([EXE, "--song", os.path.join(FIX, n + ".rbs"),
                        "--no-fx", "--seconds", "6",
                        "--out", os.path.join(out, n + ".wav")],
                       check=True, env=env, stdout=subprocess.DEVNULL)
    return out


def corner_err(d, ref_dir, _cache={}):
    """Mean |corner error| in octaves, from the RESONANT PEAK frequency.

    Scored alongside the level because NEITHER ALONE DETERMINES THE ANSWER, and
    fitting one at a time is how this went wrong twice. Widening the cutoff
    knob until the level surface is right drags the resonance-96 corner 0.79
    octaves low; leaving the knob alone leaves the level 6.5 dB out. Only a
    coupling that lifts the corner with RESONANCE can satisfy both, so both are
    in the objective.
    """
    from bass303_response import load, psd, harmonics, response
    def base(dd):
        if dd not in _cache:
            r, sr = load(os.path.join(dd, "b1_ref.wav"))
            pw, f = psd(r, sr)
            _cache[dd] = (pw, f, harmonics(pw, f))
        return _cache[dd]
    def pk(dd, name):
        p0, f, idx = base(dd)
        q = os.path.join(dd, name + ".wav")
        if not os.path.exists(q):
            return np.nan
        x, sr = load(q)
        pw, _ = psd(x, sr)
        fr, db = response(p0, pw, f, idx)
        i = int(np.argmax(db))
        return float(fr[i])
    es = []
    for r in (96, 127):
        for c in CUT:
            n = "b1_c%03d_r%03d" % (c, r)
            a_, b_ = pk(ref_dir, n), pk(d, n)
            if np.isfinite(a_) and np.isfinite(b_) and a_ > 0 and b_ > 0:
                es.append(abs(math.log2(b_ / a_)))
    return float(np.mean(es)) if es else np.nan


def score(a, b):
    d = a - b
    ok = np.isfinite(d)
    return float(np.sqrt((d[ok] ** 2).mean())) if ok.any() else np.inf


def table(a, b, label):
    A = a.reshape(len(RESO), len(CUT))
    B = b.reshape(len(RESO), len(CUT))
    print("\n%s: ReBirth / ours, dB relative to each side's own c000_r000" % label)
    print("%-6s %s" % ("reso", " ".join("%13s" % ("cut %d" % c) for c in CUT)))
    for i, r in enumerate(RESO):
        print("%-6d %s" % (r, " ".join("%6.2f/%6.2f" % (A[i, j], B[i, j])
                                       for j in range(len(CUT)))))
    e = np.sqrt(np.nanmean((A - B) ** 2, axis=1))
    print("%-6s %s" % ("rms", "  ".join("reso %d: %.2f" % (RESO[i], e[i])
                                        for i in range(len(RESO)))))


def span_pair(slope):
    """CUT_SCALE that holds the TOP of the knob where it is for a given span."""
    return TOP_HZ / (407.0 * 2.0 ** slope)


def parse(items):
    o = {}
    for it in items or []:
        k, _, v = it.partition("=")
        o[k.strip().upper()] = v.strip()
    return o


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("ref_dir")
    ap.add_argument("--check", nargs="*", default=None, metavar="KEY=VAL")
    ap.add_argument("--grid", choices=("span", "spanfloor"), default=None)
    a = ap.parse_args(argv)

    A = surface(a.ref_dir)
    if A is None:
        print("no ReBirth surface in %s" % a.ref_dir)
        return 1

    def run(o, want_corner=False):
        td = tempfile.mkdtemp()
        try:
            render(o, td, corner=want_corner)
            S = surface(td)
            C = corner_err(td, a.ref_dir) if want_corner else np.nan
        finally:
            import shutil
            shutil.rmtree(td, ignore_errors=True)
        return S, C

    if a.grid:
        cands = []
        if a.grid == "span":
            for s in (2.9, 3.4, 3.9, 4.4, 4.9, 5.4):
                cands.append({"CUT_SLOPE": "%.3f" % s,
                              "CUT_SCALE": "%.4f" % span_pair(s)})
        else:
            for s in (3.9, 4.4, 4.9):
                for f in (0.4, 0.85, 1.3):
                    cands.append({"CUT_SLOPE": "%.3f" % s,
                                  "CUT_SCALE": "%.4f" % span_pair(s),
                                  "EM_FLOOR": "%.3f" % f})
        best = None
        print("%-56s %8s %8s %8s" % ("candidate", "rms dB", "reso0", "oct err"))
        for c in cands:
            B, ce = run(c, want_corner=True)
            s = score(A, B)
            r0 = score(A[:len(CUT)], B[:len(CUT)])
            print("%-56s %8.3f %8.3f %8.3f"
                  % (" ".join("%s=%s" % kv for kv in sorted(c.items())), s, r0, ce))
            sys.stdout.flush()
            if best is None or s < best[0]:
                best = (s, c)
        print("\nbest: %s at %.3f dB rms"
              % (" ".join("%s=%s" % kv for kv in sorted(best[1].items())), best[0]))
        return 0

    o = parse(a.check)
    B, ce = run(o, want_corner=True)
    print("overrides: %s" % (" ".join("%s=%s" % kv for kv in sorted(o.items()))
                             or "(defaults)"))
    print("level-surface error %.3f dB rms over %d cells, corner error %.3f oct"
          % (score(A, B), len(NAMES), ce))
    table(A, B, "level surface")
    return 0


if __name__ == "__main__":
    sys.exit(main())
