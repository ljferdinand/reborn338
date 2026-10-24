#!/usr/bin/env python3
"""Batch S: make the resonant SKIRT measurable at high cutoff.

The problem this exists to solve. The skirt above the corner is one of the three
remaining 303 residuals, and at cutoff 90 and above it cannot be measured on
batch H at all: ReBirth's own harmonics sink into its dither floor at 7615 Hz
(cutoff 127 resonance 127) and 5959 Hz (cutoff 90 resonance 127), while the fit
window runs to about 16 kHz. A floor is flat and flat reads as a shallow slope,
which is how a wrong slope finding got into a commit message. See
tools/analyze/floor_check.py and docs/research/FINDINGS_AUDIT.md.

The lever. A 303's filter does NOT track the keyboard, so the corner stays put
when the note moves. Raising the note therefore samples the SAME filter with
more energy at any given frequency: a sawtooth harmonic at frequency f has
amplitude proportional to f0/f, so doubling f0 puts 6 dB more signal at every
frequency in the skirt. Two octaves is 12 dB, which is the difference between
measuring a filter and measuring dither.

Stage 1 (this file, --probe) tests that claim before a full batch is built on
it. Three fixtures at the single worst cell, cutoff 127 resonance 127, at f0 =
110, 220 and 440 Hz. If the last real harmonic climbs roughly in proportion,
the lever works and stage 2 is worth exporting. If it does not, the floor is
somewhere else and a different fixture is needed.

Pitch is reached two ways because only one octave is available per step: the
step octave flag gives +12 semitones, and the TUNE knob gives another +12 at
knob 127. Tune is a 32-slot table (see COMPONENT_MATRIX) and 127 sits inside
the top plateau, so it is exactly +12 and not an interpolated value.

Stage 2 (--grid) crosses cutoff {64, 90, 110, 127} with resonance {60, 90, 127}
at the pitch stage 1 says is usable. Resonance 0 and 30 are excluded on purpose:
they have no resonant peak, so they have no skirt to measure, and including them
is what let flat cells pollute an rms in the last round.

Usage:
    python gen_batchS.py --probe [outdir]
    python gen_batchS.py --grid  [outdir] [--octave N] [--tune T]
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rbsgen import build_song, step, workspace_path

NOTE_A = 9          # same note as batch H, so the two are comparable


def emit(out, name, knobs, pat, spec, n_bars=2):
    with open(os.path.join(out, name + ".rbs"), "wb") as f:
        f.write(build_song(knobs, pat, tempo=120.0, title=name,
                           author="rbsgen", n_bars=n_bars, device="bass1"))
    spec.append({"song": os.path.join(out, name + ".rbs"), "out": name + ".wav"})


def longprobe(out, octv, tune, bars):
    """Does a LONGER arrangement lower the noise floor enough to reach cutoff 110+?

    The skirt at cutoff 110 and above is unmeasurable because neither side's
    harmonics clear the dither floor more than an octave above the peak. The
    pitch lever (f0 = 220) helped and was not enough.

    Averaging is the untried lever. The dither floor is not variance that
    averaging cannot touch: for a stationary signal, averaging N Welch frames
    drops the noise POWER by N while the harmonics stay put. ReBirth exports the
    whole arrangement in song mode, and these fixtures are 2 bars at 120 BPM,
    which is 4 seconds and only about 21 frames at NFFT 16384. 32 bars is 64
    seconds and 16 times the frames, worth roughly 12 dB of floor.

    Tested on one cell before any batch is built on it, the same way the pitch
    lever was. The comparison cell is cutoff 127 resonance 127, which is where
    the measurement failed worst.
    """
    spec = []
    for c in (110, 127):
        for r in (90, 127):
            pat = [step(note=NOTE_A, octave=octv) for _ in range(16)]
            knobs = dict(cutoff=c, reso=r, envmod=0, decay=64,
                         accent=0, waveform=0, tune=tune)
            emit(out, f"s303_lg_cut{c:03d}_reso{r:03d}", knobs, pat, spec,
                 n_bars=bars)
    return spec


def probe(out):
    """Does raising the pitch lift the skirt out of the floor?"""
    spec = []
    # (label, octave flag, tune knob) -> nominal f0
    cases = [("f0_110", 0, 64), ("f0_220", 1, 64), ("f0_440", 1, 127)]
    for label, octv, tune in cases:
        pat = [step(note=NOTE_A, octave=octv) for _ in range(16)]
        knobs = dict(cutoff=127, reso=127, envmod=0, decay=64,
                     accent=0, waveform=0, tune=tune)
        emit(out, f"s303_probe_{label}", knobs, pat, spec)
    return spec


def grid(out, octv, tune):
    spec = []
    for c in (64, 90, 110, 127):
        for r in (60, 90, 127):
            pat = [step(note=NOTE_A, octave=octv) for _ in range(16)]
            knobs = dict(cutoff=c, reso=r, envmod=0, decay=64,
                         accent=0, waveform=0, tune=tune)
            emit(out, f"s303_cut{c:03d}_reso{r:03d}", knobs, pat, spec)
    return spec


def sweep(out, octv, tune):
    """Resonance sweep at fixed high cutoff, to get the SKIRT LAW.

    Batch S's grid had only three resonance points and they showed ReBirth's
    skirt shallowing hard as resonance rises at cutoff 127: -30.9, -24.4,
    -19.4 dB/oct at reso 60, 90, 127, where ours sits near -25 to -27 flat. At
    its maximum ReBirth is close to a three-pole rolloff while we hold four.

    Three points cannot tell a law from a coincidence, which is the standing
    rule in this project and the reason the tune control was wrong for four
    days. This takes eight, at two cutoffs so the effect can be shown not to be
    unique to the very top of the knob.

    Resonance starts at 45 rather than 0: below about that there is no resonant
    peak, so there is no skirt to measure and the cell would only pollute an
    average. Batch S already established that reso 0 and 30 have no peak.
    """
    spec = []
    for c in (110, 127):
        for r in (45, 60, 75, 90, 100, 110, 118, 127):
            pat = [step(note=NOTE_A, octave=octv) for _ in range(16)]
            knobs = dict(cutoff=c, reso=r, envmod=0, decay=64,
                         accent=0, waveform=0, tune=tune)
            emit(out, f"s303_sw_cut{c:03d}_reso{r:03d}", knobs, pat, spec)
    return spec


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("outdir", nargs="?",
                    default=workspace_path("fixtures-2026-07-30", "batchS"))
    ap.add_argument("--probe", action="store_true")
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("--long", action="store_true",
                    help="long-arrangement probe: more Welch frames, lower floor")
    ap.add_argument("--bars", type=int, default=32)
    # Defaults are what the probe settled on: the step octave flag alone reaches
    # f0 = 220 Hz, whose last usable harmonic is 15857 Hz against 6070 Hz at
    # 110 Hz. That covers two octaves above the cutoff-127 corner. Going on to
    # 440 Hz buys nothing (14966 Hz) because the harmonics get too sparse, and
    # it would need tune = 127, where leaving tune at the 64 centre detent keeps
    # this a single-variable change from batch H.
    ap.add_argument("--octave", type=int, default=1)
    ap.add_argument("--tune", type=int, default=64)
    a = ap.parse_args()

    os.makedirs(a.outdir, exist_ok=True)
    if a.probe:
        spec, tag = probe(a.outdir), "batchS_probe"
    elif a.sweep:
        spec, tag = sweep(a.outdir, a.octave, a.tune), "batchS_sweep"
    elif a.long:
        spec, tag = longprobe(a.outdir, a.octave, a.tune, a.bars), "batchS_long"
    else:
        spec, tag = grid(a.outdir, a.octave, a.tune), "batchS_grid"
    json.dump(spec, open(os.path.join(a.outdir, tag + ".json"), "w"), indent=1)
    print(f"wrote {len(spec)} fixtures to {a.outdir} ({tag}.json)")
