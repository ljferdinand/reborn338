#!/usr/bin/env python3
"""Fit the 808 output trims, all 16 voices in one pass.

A trim is a pure output-stage scalar, so once a voice's SHAPE is right its trim
follows analytically from one number: the level offset between our render and
ReBirth's export. This tool measures that offset for every voice and prints the
corrected table, ready to paste into src/devices/drum808.cpp.

Run it LAST, after any change to a voice's spectrum, decay or filter geometry.
Those move a voice's total energy and therefore invalidate its trim. The lesson
that keeps costing this project time is the reverse case: a level deficit that
was really a decay error, refitted as a trim and thereby baked in. Fit shape
first, always, then run this.

It reuses score808's render and metric code rather than reimplementing them, so
both sides go through one method (see docs/MEASUREMENT_PROTOCOL.md), and it inherits the
POWER averaging that the coherent-averaging bug made necessary.

  fit_trims808.py --synth          # the clean-room synthesis path
  fit_trims808.py                  # the sample path
  fit_trims808.py --synth --only BD,SD,CY

Every fixture below is the voice in isolation at its default knob, which is what
makes one number per voice enough.
"""
import os, sys, re, math, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import score808 as S

WS = S.ROOT
B17 = (os.path.join(WS, "fixtures-2026-08-17", "808complete"),
       os.path.join(WS, "renders", "fixtures-2026-08-17", "808complete"))
B18 = (os.path.join(WS, "fixtures-2026-08-18", "808cyoh"),
       os.path.join(WS, "renders", "fixtures-2026-08-18", "808cyoh"))

# voice -> (fixture, batch, domband). Index order must match the enum in
# drum808.cpp: BD SD LT MT HT LC MC HC RS CP MA CB CY OH CH CL.
VOICES = [
    ("BD", "d808_bdl_064", B17, (30, 400)),
    ("SD", "d808_sds_064", B17, (100, 400)),
    ("LT", "d808_ltt_064", B17, (60, 500)),
    ("MT", "d808_mtt_064", B17, (60, 500)),
    ("HT", "d808_htt_064", B17, (60, 500)),
    ("LC", "d808_ltc_064", B17, (100, 600)),
    ("MC", "d808_mtc_064", B18, (100, 600)),
    ("HC", "d808_htc_064", B18, (100, 600)),
    ("RS", "d808_rs_ref",  B18, (300, 16000)),
    ("CP", "d808_cp_ref",  B18, (300, 16000)),
    ("MA", "d808_ma_ref",  B18, (300, 16000)),
    ("CB", "d808_cb_ref",  B18, (300, 16000)),
    ("CY", "d808_cyl_064", B18, (300, 16000)),
    ("OH", "d808_ohl_064", B18, (300, 16000)),
    ("CH", "d808_ch_ref",  B18, (300, 16000)),
    ("CL", "d808_cl_ref",  B18, (300, 16000)),
]

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                   "src", "devices", "drum808.cpp")


def read_trims(name):
    """Pull the current table out of the C++ so the printed result is a real
    delta rather than a number that has to be reconciled by hand."""
    src = open(os.path.abspath(SRC)).read()
    m = re.search(r"const float " + name + r"\[NV808\] = \{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("could not find %s in %s" % (name, SRC))
    body = re.sub(r"//[^\n]*", "", m.group(1))            # strip trailing comments
    vals = [float(v) for v in re.findall(r"([0-9]*\.[0-9]+)f", body)]
    if len(vals) != 16:
        raise SystemExit("expected 16 values in %s, found %d" % (name, len(vals)))
    return vals


def offset_db(fixture, batch, domband, synth):
    """Our RMS minus ReBirth's, in dB, power-averaged over the step-locked hits."""
    S.FXDIR, S.RBEXP = batch
    rb_path = next((p for p in (os.path.join(S.RBEXP, fixture + e)
                                for e in (".wav", ".WAV")) if os.path.exists(p)), None)
    if rb_path is None:
        return None
    tmp = os.environ.get("TEMP", "/tmp")
    ours = os.path.join(tmp, "fittrim_%s%s.wav" % ("syn_" if synth else "", fixture))
    S.render_ours(fixture, ours, synth=synth)
    rb, _ = S.load_wav_left(rb_path)
    ou, _ = S.load_wav_left(ours)
    return (S.metrics(ou, domband)["rms_db"] - S.metrics(rb, domband)["rms_db"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--synth", action="store_true",
                    help="fit kTrimSyn808 (the synthesis path) instead of kTrim808")
    ap.add_argument("--only", default="", help="comma-separated voice names to measure")
    a = ap.parse_args()

    table = "kTrimSyn808" if a.synth else "kTrim808"
    cur = read_trims(table)
    want = set(v.strip().upper() for v in a.only.split(",") if v.strip())

    print("%s, fitted from the isolated default-knob fixtures\n" % table)
    print("%-4s %-16s %9s %9s %9s %8s" %
          ("v", "fixture", "off_dB", "current", "fitted", "change"))
    print("-" * 60)
    out = list(cur)
    for i, (name, fx, batch, db) in enumerate(VOICES):
        if want and name not in want:
            continue
        off = offset_db(fx, batch, db, a.synth)
        if off is None:
            print("%-4s %-16s   (no ReBirth export)" % (name, fx))
            continue
        new = cur[i] * (10.0 ** (-off / 20.0))
        out[i] = new
        print("%-4s %-16s %+9.2f %9.4f %9.4f %+8.2f" %
              (name, fx, off, cur[i], new, 20.0 * math.log10(new / cur[i])))

    print("\nconst float %s[NV808] = {" % table)
    print("    " + ", ".join("%.4ff" % v for v in out[:8]) + ",")
    print("    " + ", ".join("%.4ff" % v for v in out[8:]))
    print("};")
    print("\nA voice whose change is near zero was already right. A large change on a")
    print("voice whose shape you did NOT touch this round deserves suspicion before")
    print("it is applied: check that its fixture really isolates it.")


if __name__ == "__main__":
    main()
