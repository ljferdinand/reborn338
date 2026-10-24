#!/usr/bin/env python3
"""Accent rate: the two cases gen_accentprobe.py does not contain.

THE DEFECT. Luke, 2026-08-02: ReBirth's chirp hits every quarter note on
Clodhopper and ours sounds like it hits every bar. Tracing our own logic over
Clodhopper's pattern confirms it exactly: seven accent flags per bar and
EXACTLY ONE fires at full depth. One per bar is 0.583 Hz, a quarter note at 140
BPM is 2.333 Hz, and that is his factor of four.

WHY ACC_TIE IS NOT THE THING TO TUNE. It was fitted on two cases and reproduces
both to the decimal: an accent on a slid step, and an accent on a rest that is
TIED INTO, ReBirth giving +2.8 dB on each against +22.4 for a real retrigger.
Those fits stand. The problem is that the suppression is also being applied to
two cases that were never measured, and neither is a non-retriggering step at
all. Raising ACC_TIE would recover the rate by re-breaking the tied run that
0.05 fixed on both measurement and ears. Fix WHICH steps suppress, then
re-measure the depth.

CASE A, Clodhopper's step 10. A note slid into from a rest that was itself
RELEASED. Its step 9 is a rest whose own predecessor does not tie, so our engine
releases the voice, and then step 9's slide flag makes step 10 a Strike with
slideIn set. We glide into a note FROM SILENCE and suppress its retrigger and
its accent. There is nothing to glide from. If ReBirth treats this as a fresh
pluck, we are suppressing an accent that should fire at full depth, and this
alone would restore most of the missing rate.

CASE B, Clodhopper's step 12. An accent on a plain rest while the previous note
is still ringing. Our Release path drops it, because release() takes no accent
argument. The established finding is that ReBirth reads the accent row on a rest
and lands it on whatever is sounding through, and something IS sounding through
here. That finding was measured on a rest that was TIED into; this is a rest
that is not.

Both are interactions between two flags, which is the species behind every
ear-caught defect in this component, and both are absent from the existing probe
BY CONSTRUCTION: its `acc_mid` is a slid step inside a sounding run and its
`rest_acc` is a rest tied into.

THE CONTROLS ARE NOT OPTIONAL. Two cells reproduce exactly what ACC_TIE already
gets right, so that any fix which restores the rate by breaking them is caught in
the same measurement rather than a round later. Two more give the endpoints, a
real retrigger with and without an accent, so +22.4 dB has a local reference
rather than being carried over from another batch. Three cells are duplicated.

PANEL. Cutoff 64, resonance 60, env mod 0, accent 96, matching
gen_accentprobe.py exactly so the two sets merge. Env mod 0 means the accent is
the ONLY thing that can move the filter. Resonance 60 rather than Clodhopper's
125 is deliberate and is copied from that file's reasoning: near self-oscillation
a modest corner movement reads as 30 dB, which is superb for hearing and useless
for fitting. Two cells repeat the decisive shapes at resonance 120 so the
high-resonance case is not left unmeasured.
"""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

OUT = sys.argv[1] if len(sys.argv) > 1 else \
      workspace_path("fixtures-2026-08-02", "accentrate")
os.makedirs(OUT, exist_ok=True)

NOTE = 9          # A throughout, so a slide glides nowhere and only the
                  # envelope can change the timbre
spec = []


def N(acc=False, sl=False):   return step(note=NOTE, accent=acc, slide=sl)
def R(acc=False, sl=False):   return step(rest=True, accent=acc, slide=sl)


def emit(name, head, reso=60):
    """head is the first steps; the rest of the bar is plain rests so the
    envelope has a long clean tail to be measured in."""
    pat = list(head) + [R() for _ in range(16 - len(head))]
    knobs = dict(cutoff=64, reso=reso, envmod=0, decay=64, accent=96,
                 waveform=0, tune=64)
    with open(os.path.join(OUT, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=4, device="bass1"))
    spec.append({"song": os.path.join(OUT, name + ".rbs"), "out": name + ".wav"})


# --- CASE A: slide out of SILENCE into an accented note ---------------------
# N R r A : step1 gates and does not tie, step2 is a plain rest so the voice
# RELEASES, step3 is a rest that ties onward, step4 is the accented note.
# Our engine makes step4 a Strike with slideIn set and suppresses the accent.
emit("r303_A_slide_from_silence",     [N(),      R(),      R(sl=True), N(acc=True)])
emit("r303_A_control_noaccent",       [N(),      R(),      R(sl=True), N()])
# The same shape with NO intervening release, so the slide has something to
# glide from. This is the case ACC_TIE was fitted on and must not change.
emit("r303_A_control_sounding",       [N(sl=True), N(sl=True), N(sl=True), N(acc=True)])

# --- CASE B: accent on a plain rest over a RINGING note ---------------------
# N X : step1 gates and does not tie, step2 is an accented rest. Our Release
# path drops the accent entirely.
emit("r303_B_accent_on_plain_rest",   [N(),      R(acc=True)])
emit("r303_B_control_plain_rest",     [N(),      R()])
# The rest that IS tied into, which ACC_TIE reproduces at +2.8 dB.
emit("r303_B_control_tied_rest",      [N(sl=True), R(acc=True)])

# --- Endpoints, so +22.4 dB has a LOCAL reference --------------------------
emit("r303_ref_retrigger_accent",     [R(),      N(acc=True)])
emit("r303_ref_retrigger_plain",      [R(),      N()])

# --- The decisive shapes at high resonance, where the defect was heard ------
emit("r303_A_slide_from_silence_r120", [N(),     R(),      R(sl=True), N(acc=True)], reso=120)
emit("r303_B_accent_on_plain_rest_r120", [N(),   R(acc=True)], reso=120)

# --- Duplicate controls, per gen_batchL.py ---------------------------------
emit("r303_A_slide_from_silence_dup",  [N(),     R(),      R(sl=True), N(acc=True)])
emit("r303_B_accent_on_plain_rest_dup", [N(),    R(acc=True)])
emit("r303_ref_retrigger_accent_dup",  [R(),     N(acc=True)])

json.dump(spec, open(os.path.join(OUT, "accentrate.json"), "w"), indent=1)
print(f"wrote {len(spec)} fixtures to {OUT}")
print("  CASE A  does a slide out of SILENCE retrigger and take a full accent?")
print("  CASE B  does an accent on a NON-tied rest land on the ringing note?")
print("  4 controls, 2 endpoints, 2 at resonance 120, 3 duplicates")
