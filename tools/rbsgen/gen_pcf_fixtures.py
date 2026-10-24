#!/usr/bin/env python3
"""Fixtures for the PCF, supertask 4.

Round 1 measures the FILTER with the envelope out of the way, which the manual
says is exactly what Amt = 0 does: "If the Amount slider is set to zero, there
will be no pattern effect at all. The filter can then be used as a fixed tone
control" (p65, p160). So every fixture here sets Amt = 0 and the pattern is
irrelevant.

The source is a 303 drone: one note held across all sixteen steps by putting
SLIDE on every step, which is the manual's own recipe for auditioning the PCF
(p61 step 3). Its filter is wide open and its envelope modulation is zero, so
the spectrum is a steady saw. Crucially the source does not have to be flat or
even known, because every measurement is the RATIO of a PCF-on render to the
`pcf_ref` render made from the identical song with the PCF routed nowhere. The
303's own contribution divides out exactly.

A low note is deliberate: the harmonic grid is the frequency resolution we get,
and note 0 puts partials every ~65 Hz, which is dense enough to fit a 2-pole
curve anywhere in the audible range.

Exports are not sample-deterministic, so compare power spectra and never
subtract waveforms. See docs/MEASUREMENT_PROTOCOL.md.

    python gen_pcf_fixtures.py --out-dir ../../../fixtures-2026-08-27/pcf-static
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rbsgen as G

TEMPO = 140.0
BARS = 4

# The 303 under the filter: wide open, no envelope, no accent, so it is a plain
# steady saw and nothing about it moves during the render.
SOURCE_KNOBS = dict(tune=64, cutoff=127, reso=0, envmod=0, decay=64,
                    accent=0, waveform=0)

def drone(note=0, length=16):
    """One held note. Slide on every step means the 303 never retriggers, so
    the VCA and the filter envelope stay put and the output is a drone."""
    return [G.step(note=note, slide=True) for _ in range(length)]

# Round 1 used five points per sweep. Round 2 doubles that, because the cutoff
# law came out at 0.075, 0.082 and 0.071 octaves per byte over three intervals,
# which is close enough to constant to suspect a plain exponential and far
# enough from it that five points cannot tell.
SWEEP = (0, 32, 64, 96, 127)
DENSE = (0, 16, 32, 48, 64, 80, 96, 112, 127)
MID = 64

# Mode byte 0 is BANDPASS and 1 is LOWPASS, measured 2026-08-27. Naming them
# after the byte rather than after a guess at the panel, because the panel
# screenshot was read the other way round and the audio settled it.
MODE_LP, MODE_BP = 1, 0

# The 303 drone constrains the filter only between its fundamental and where
# its harmonics sink into the export's dither, which for round 1 was about
# 65 Hz to 2.5 kHz. Above that the fully-open setting had no data to fit, so
# the top of the cutoff range needs a source with real high-frequency content:
# a closed hat on every sixteenth, which is broadband and dense.
BRIGHT_KNOBS = dict(hh_level=110, ch_decay=64, accent=0)
BRIGHT_HITS = {"ch": list(range(16))}


def cases():
    """(name, pcf spec or None, what it determines)."""
    out = [("pcf_ref", None,
            "the denominator: identical song, PCF routed nowhere"),
           ("pcf_bypass", dict(on=0, freq=MID, q=0, amt=0, pattern=0, mode=MODE_LP),
            "PCF ROUTED but the unit switched off. Round 1 measured a flat "
            "+4.3 dB whenever the filter was open, and this says whether that "
            "is the filter's gain or the cost of being routed at all")]
    for f in DENSE:
        out.append((f"pcf_lp_f{f:03d}",
                    dict(on=1, freq=f, q=0, amt=0, pattern=0, mode=MODE_LP),
                    f"LP cutoff law at Freq={f}, Q=0"))
    for q in DENSE:
        if q == 0:
            continue              # identical to lp_f064
        out.append((f"pcf_lp_q{q:03d}",
                    dict(on=1, freq=MID, q=q, amt=0, pattern=0, mode=MODE_LP),
                    f"LP resonance law at Q={q}, Freq={MID}"))
    for f in SWEEP:
        out.append((f"pcf_bp_f{f:03d}",
                    dict(on=1, freq=f, q=0, amt=0, pattern=0, mode=MODE_BP),
                    f"BP cutoff law at Freq={f}, Q=0: does BP share LP's corner?"))
    return out


# Round 3: the ENVELOPE. Freq sits low so the envelope has the whole range to
# sweep into, Q at 0 so the filter shape stays easy to fit, and pattern 0
# because the manual says it retriggers on whole notes, which is the simplest
# trigger figure the unit has (p64).
ENV_FREQ, ENV_Q, ENV_PATTERN = 32, 0, 0


def envelope_cases():
    out = []
    for a in DENSE:
        out.append((f"env_amt{a:03d}",
                    dict(on=1, freq=ENV_FREQ, q=ENV_Q, amt=a, pattern=ENV_PATTERN,
                         decay=64, mode=MODE_LP),
                    f"envelope DEPTH at Amt={a}, Decay=64"))
    for d in DENSE:
        if d == 64:
            continue          # identical to env_amt096
        out.append((f"env_dec{d:03d}",
                    dict(on=1, freq=ENV_FREQ, q=ENV_Q, amt=96, pattern=ENV_PATTERN,
                         decay=d, mode=MODE_LP),
                    f"envelope DECAY at Decay={d}, Amt=96"))
    return out


# Is the decay a time in seconds or a musical length? The same settings at
# three tempi answer it: a fixed time keeps its shape in seconds and a synced
# one keeps it in beats. Each tempo needs its own reference.
TEMPI = (90.0, 200.0)


# Round 4: a BROADBAND PROBE for the envelope.
#
# The drone cannot do this job. Its power sits at a 64.6 Hz fundamental with a
# 6 dB/octave rolloff, so frame by frame it can only resolve a cutoff between
# about 60 Hz and 1 kHz, which is 0.38 of the parameter range out of 1.0. An
# envelope that sweeps the whole range spends most of its life outside that
# window, and the fits came back with r-squared between 0.00 and 0.88.
#
# A kick, a crash and a closed hat on every sixteenth cover roughly 30 Hz to
# 15 kHz between them, densely enough that no harmonic grid constrains the
# analysis window. That buys fine time resolution, which is what a decay needs:
# the drone's 46 ms window smooths the fastest decay settings away entirely.
PROBE_KNOBS = dict(bd_level=100, bd_decay=110, cr_level=100, hh_level=100,
                   ch_decay=90, accent=0)
PROBE_HITS = {"bd": list(range(16)), "cr": list(range(16)), "ch": list(range(16))}


def probe_cases():
    """Envelope fixtures against the broadband probe. Freq sits at the bottom
    so the whole excursion is upward and nothing is lost under the base."""
    out = [("probe_ref", None, "broadband denominator: PCF routed nowhere")]
    for d in DENSE:
        out.append((f"probe_dec{d:03d}",
                    dict(on=1, freq=0, q=0, amt=96, pattern=0, decay=d, mode=MODE_LP),
                    f"envelope decay law at Decay={d}, Amt=96"))
    for amt in DENSE:
        out.append((f"probe_amt{amt:03d}",
                    dict(on=1, freq=0, q=0, amt=amt, pattern=0, decay=64, mode=MODE_LP),
                    f"envelope depth law at Amt={amt}, Decay=64"))
    return out


# Round 5: all 55 patterns, read off ReBirth rather than off Appendix D.
#
# The BROADBAND PROBE, not the drone. Resolving a thirty-second note needs an
# analysis window well under its 53.6 ms, and the drone's 64.6 Hz fundamental
# forces a window of at least 46 ms before the band energy stops fluctuating
# within one period of the source. At 46 ms the trigger positions come out
# right but the window eats the peak, so the velocities read 0.66 to 0.89 where
# a pattern with no dynamics should be flat. The probe is broadband, so an
# 11.6 ms window is stable: pattern 0 then reads exactly three triggers at
# thirty-seconds 0, 32 and 64 with velocities 0.49, 0.49, 0.49.
#
# Amt sits where the peaks land inside the measurable band rather than above
# it, and Decay is short enough that each trigger dies before the next
# thirty-second note.
PAT_FREQ, PAT_AMT, PAT_DECAY, PAT_Q = 0, 64, 16, 0


def pattern_cases():
    out = [("pat_ref", None, "denominator for the pattern sweep")]
    for n in range(55):
        out.append((f"pat_{n:02d}",
                    dict(on=1, freq=PAT_FREQ, q=PAT_Q, amt=PAT_AMT,
                         pattern=n, decay=PAT_DECAY, mode=MODE_LP),
                    f"pattern {n}: trigger positions and velocities"))
    return out


def bright_cases():
    """The same filter, measured with a broadband source for the top octaves."""
    out = [("hat_ref", None, "bright denominator: PCF routed nowhere")]
    for f in (96, 104, 112, 120, 127):
        out.append((f"hat_lp_f{f:03d}",
                    dict(on=1, freq=f, q=0, amt=0, pattern=0, mode=MODE_LP),
                    f"LP cutoff at Freq={f} against a source that reaches 15 kHz"))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out-dir", default=G.workspace_path(
        "fixtures-2026-08-27", "pcf-static"))
    a = ap.parse_args(argv)
    os.makedirs(a.out_dir, exist_ok=True)

    rows = []
    for name, pcf, why in cases():
        data = G.build_song(SOURCE_KNOBS, drone(), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1",
                            pcf=pcf)
        path = os.path.join(a.out_dir, name + ".rbs")
        open(path, "wb").write(data)
        rows.append((name, why, len(data)))
        print(f"  {name}.rbs  {len(data):,} bytes")
    for name, pcf, why in envelope_cases():
        data = G.build_song(SOURCE_KNOBS, drone(), tempo=TEMPO, n_bars=BARS,
                            title=name, author="rbsgen", device="bass1", pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why, len(data)))
        print(f"  {name}.rbs  {len(data):,} bytes")
    for t in TEMPI:
        for tag, pcf in (("ref", None),
                         ("env", dict(on=1, freq=ENV_FREQ, q=ENV_Q, amt=96,
                                      pattern=ENV_PATTERN, decay=64, mode=MODE_LP))):
            name = f"t{int(t):03d}_{tag}"
            data = G.build_song(SOURCE_KNOBS, drone(), tempo=t, n_bars=BARS,
                                title=name, author="rbsgen", device="bass1", pcf=pcf)
            open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
            rows.append((name, f"decay time base at {t:g} BPM", len(data)))
            print(f"  {name}.rbs  {len(data):,} bytes")
    for name, pcf, why in probe_cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why, len(data)))
        print(f"  {name}.rbs  {len(data):,} bytes")
    for name, pcf, why in pattern_cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=PROBE_KNOBS, drum_hits=PROBE_HITS, pcf=pcf)
        open(os.path.join(a.out_dir, name + ".rbs"), "wb").write(data)
        rows.append((name, why, len(data)))
    print(f"  (55 pattern fixtures + pat_ref)")
    for name, pcf, why in bright_cases():
        data = G.build_song({}, [], tempo=TEMPO, n_bars=BARS, title=name,
                            author="rbsgen", device="909",
                            drum_knobs=BRIGHT_KNOBS, drum_hits=BRIGHT_HITS,
                            pcf=pcf)
        path = os.path.join(a.out_dir, name + ".rbs")
        open(path, "wb").write(data)
        rows.append((name, why, len(data)))
        print(f"  {name}.rbs  {len(data):,} bytes")

    man = [
        "# PCF static-filter fixtures, 2026-08-27",
        "",
        "Round 1 of supertask 4. Every fixture holds **Amt = 0**, which the",
        "manual says removes the pattern from the signal path entirely, so",
        "these measure the filter alone: the cutoff law, the resonance law,",
        "and the difference between the two modes.",
        "",
        "The source is a 303 drone (one note, slide on every step, filter wide",
        "open, no envelope modulation) routed to the PCF. Measure every file as",
        "a power-spectrum RATIO against `pcf_ref.wav`, which is the same song",
        "with the PCF routed nowhere: the source divides out and what is left",
        "is the filter's magnitude response.",
        "",
        f"Tempo {TEMPO:g}, {BARS} bars, master compressor off, no other effects.",
        "",
        "| File | What it determines |",
        "|---|---|",
    ]
    for name, why, _n in rows:
        man.append(f"| `{name}.rbs` | {why} |")
    man += [
        "",
        "## Traps",
        "",
        "**Do not null-test.** ReBirth's exports are not sample-deterministic,",
        "so two renders of one song are spectrally identical and sample-wise",
        "uncorrelated. Compare power spectra.",
        "",
        "**Measure stereo.** The PCF's two cores are fed the same signal here,",
        "but confirm that rather than assuming it: a mono-sum would hide a",
        "stereo asymmetry the way it once hid the pan law.",
        "",
        "**Q may ring rather than settle.** At high Q the 'static' filter is",
        "still a resonator being excited by a harmonic that may or may not sit",
        "on its peak. Read the response at the harmonic grid, not at the peak.",
    ]
    open(os.path.join(a.out_dir, "MANIFEST.md"), "w",
         encoding="utf-8").write("\n".join(man) + "\n")
    print(f"\n{len(rows)} fixtures + MANIFEST.md in {a.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
