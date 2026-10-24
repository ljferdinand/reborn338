#!/usr/bin/env python3
"""Fit the 909 output trims, all 16 voices in one pass.

A trim is a pure output-stage scalar, so once a voice's SHAPE is right its trim
follows analytically from one number: the level offset between our render and
ReBirth's export. This tool measures that offset for every voice and prints the
corrected table, ready to paste into src/devices/drum909.cpp.

Run it LAST, after any change to a voice's spectrum, decay or filter geometry.
Those move a voice's total energy and therefore invalidate its trim. The lesson
that keeps costing this project time is the reverse case: a level deficit that
was really a decay error, refitted as a trim and thereby baked in. Fit shape
first, always, then run this.

It reuses score909's render and metric code rather than reimplementing them, so
both sides go through one method (see docs/MEASUREMENT_PROTOCOL.md), and it inherits the
POWER averaging that the coherent-averaging bug made necessary.

  fit_trims909.py          # the sample path (ReBirth's own 909)
  fit_trims909.py                  # the sample path
  fit_trims909.py --only BD,SD,CR

Every fixture below is the voice in isolation at its default knob, which is what
makes one number per voice enough.
"""
import os, sys, re, math, argparse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import score909 as S

WS = S.ROOT
BATCH = (os.path.join(WS, "fixtures-2026-08-25", "909complete"),
         os.path.join(WS, "renders", "fixtures-2026-08-25", "909complete"))

# voice -> (fixture, batch, domband). Index order must match the enum in
# drum909.cpp: BD SD LT MT HT LC MC HC RS CP MA CB CY OH CH CL.
VOICES = [
    ("BD", "d909_bdl_064", BATCH, (30, 400)),
    ("SD", "d909_sds_064", BATCH, (100, 400)),
    ("LT", "d909_ltt_064", BATCH, (60, 500)),
    ("MT", "d909_mtt_064", BATCH, (60, 500)),
    ("HT", "d909_htt_064", BATCH, (60, 500)),
    ("RS", "d909_rs_ref",  BATCH, (300, 16000)),
    ("CP", "d909_cp_ref",  BATCH, (300, 16000)),
    ("OH", "d909_ohd_064", BATCH, (300, 16000)),
    ("CH", "d909_chd_064", BATCH, (300, 16000)),
    ("CR", "d909_cr_ref",  BATCH, (300, 16000)),
    ("RD", "d909_rd_ref",  BATCH, (300, 16000)),
]

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                   "src", "devices", "drum909.cpp")


def read_trims(name):
    """Pull the current table out of the C++ so the printed result is a real
    delta rather than a number that has to be reconciled by hand."""
    src = open(os.path.abspath(SRC)).read()
    m = re.search(r"const float " + name + r"\[NV909\] = \{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("could not find %s in %s" % (name, SRC))
    body = re.sub(r"//[^\n]*", "", m.group(1))            # strip trailing comments
    vals = [float(v) for v in re.findall(r"([0-9]*\.[0-9]+)f", body)]
    if len(vals) != 11:
        raise SystemExit("expected 11 values in %s, found %d" % (name, len(vals)))
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
    ap.add_argument("--only", default="", help="comma-separated voice names to measure")
    ap.add_argument("--synth", action="store_true",
                    help="fit kTrimSyn909 by rendering with no sample set")
    a = ap.parse_args()

    table = "kTrimSyn909" if a.synth else "kTrim909"
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

    print("\nconst float %s[NV909] = {" % table)
    print("    " + ", ".join("%.4ff" % v for v in out))
    print("};")
    print("\nA voice whose change is near zero was already right. A large change on a")
    print("voice whose shape you did NOT touch this round deserves suspicion before")
    print("it is applied: check that its fixture really isolates it.")


if __name__ == "__main__":
    main()
