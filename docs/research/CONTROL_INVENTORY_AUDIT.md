# Control inventory audit (2026-08-25)

A deliberate sweep for ONE class of defect, prompted by Luke after the same shape
turned up three times by accident: **our control inventory drifting from ReBirth's.**
Ground truth is the Appendix C MIDI map (pages 194-204), which enumerates every
externally addressable parameter and is an independent check on the prose chapters.

The defect has two mirror forms:

- **Phantom control.** We apply a modulation or envelope ReBirth has no control for.
- **Dropped control.** ReBirth has a control we never decode, never route, or
  collapse onto a shared knob.

Reproducing a defect that is REBIRTH'S is deliberate here (see the defer-to-ReBirth
ruling in `OPEN_DECISIONS.md`). This audit is only about OUR inventory not matching
theirs.

## Why it was worth doing

Six instances existed before the sweep, every one found by accident while chasing
something else:

| instance | form | found |
|---|---|---|
| 808 tom + closed-hat decay envelopes | phantom | round 8 |
| 808 MT/HT tune bytes unread | dropped | round 7 |
| 909 MT/HT tune and decay bytes unread | dropped | 2026-08-25 |
| 909 ride tune (header byte 28) unread | dropped | 2026-08-25 |
| 909 crash + ride decay envelopes | phantom | 2026-08-25 |
| 909 snare Tone decoded then ignored | dropped | 2026-08-25 |

The sweep then found more, including one that affected the default shipping sound.

## Confirmed and FIXED

**808 synth path: the three tom Tune knobs were inert.** The synthesis path tuned
all six tom and conga voices from a legacy shared knob, `tom_tune`, that nothing in
the tree ever writes (a repo-wide grep finds only its own declaration), so it sat at
its 0.5 default forever. ReBirth has three independent column Tune controls (CC
45/48/51); we decode all three and the engine routes all three, and the SAMPLE path
has honoured them since round 7, but the synth path was never repointed when that
split was made. **This is the shipping default path**, since no ReBirth-derived
samples ship, and 11 of the 16 demo songs move at least one tom column off centre,
6 of them to the +/-2 semitone limit. Both paths now resolve through one shared
`tomTuneEx808()` helper so they cannot drift apart again.

## Confirmed, NOT yet fixed

**Delay: triplet mode and pan are decoded by nobody.** The DELY chunk layout is
documented in our own comment at `src/io/rbs.cpp` as `[ver][steps][unit][feedback]
[pan]`, and the parser reads `steps` (p[1]) and `feedback` (p[3]) then skips `unit`
(p[2], triplet mode, CC 101) and `pan` (p[4], CC 102). Both are real ReBirth
controls sitting one line below the comment that names them.

**Compressor: both forms at once.** `EffectInit` has no compressor fields at all, so
Ratio and Threshold (CC 106/107) are never decoded, carried or routed; the engine
hard-sets `threshold = 1.0`, which is effectively bypass. In the other direction the
device exposes FIVE parameters (threshold, ratio, attack, release, makeup) where
ReBirth's panel has exactly two, so attack, release and makeup have no control
behind them and their values are invented defaults rather than measured constants.
Harmless for parity MEASUREMENT, since the measurement protocol requires the master
compressor be off, which is why it survived unnoticed; it bites when playing songs.

**909 Flam (CC 92) is unimplemented, and byte 29 is the likely home.** The page-197
map lists 28 sound controls; `decode909Knobs` reads bytes 2 to 28, which covers 27 of
them, and Flam is the one left over. Header bytes 0, 1, 29 and 30 are unread: 0 is a
version, 1 looks like panel selection state, byte 30 sits at 0x64 in most songs (the
level default) and byte 29 takes 10 distinct values across the 16 shipped songs,
which is the signature of a live knob. **Byte 29 is therefore the prime Flam
candidate, but this is a hypothesis and needs a fixture sweep to confirm**, not a
guess written into the decoder. Flam is a sequencer timing feature rather than a
voice control, so it also needs sequencer work, not just a decode line.

**~~PCF is entirely unwired.~~ RESOLVED 2026-08-27 by supertask 4.** All six
controls are decoded and applied, the 55 preset patterns are measured, and the
unit is a series insert after the distortion. See `PCF_FINDINGS.md`.

Worth keeping the shape of what this entry missed. It said "only the decode and
routing are missing", and the decode turned out to be the smaller half: the same
`MIXR` bytes that carry the PCF's routing also carry the COMPRESSOR's, and the
loader was reading the compressor's target as the distortion's, so the
distortion was pointed at the wrong section in fifteen of the sixteen demo
songs. An audit entry that scopes a gap to one component can under-read it when
the gap sits in a struct shared with two others.

## Not defects, recorded so they are not re-reported

**The four 808 synth extras** (`tom_decay`, `ch_decay`, `cp_spread`, `cb_tune`) and
the compressor's attack/release/makeup are NOT debugging scaffolding. Git dates every
one of them to the first engine commit, `907b3c1` on 2026-07-14, the original
clean-room groovebox written BEFORE any parity measurement began. They are vestigial
pre-parity design, not temp knobs and not requested features, which is exactly what
the advanced-settings surface decided in `OPEN_DECISIONS.md` is for.

**The ~100 `REBORN_*` environment probes ARE the project's temp controls**, and they
are the right pattern: default-off, gated, documented at their site, and provably
inert because `tools/render/byte_neutral.py` re-verifies the shipped build's pooled
hash (`50d00a6a`) after every change. Most live in the 303. Some may now be dead,
referencing findings since retired; pruning them is maintenance, not a parity bug.

**The 303's `drive` parameter** has no ReBirth control behind it, but it is a
deliberately modelled and ear-validated behaviour (the `satAsym` shaper, see
`OPEN_DECISIONS.md` resolved call 1), held at a fitted constant and not song-driven.
Modelled behaviour with a measurement behind it is not a phantom.

## The pattern worth remembering

Every instance clusters in one of two places: **a control that exists in ReBirth
where our code has two paths** (sample versus synthesis) and only one was updated,
or **a knob that was split into several** and not every caller was repointed. Both
are invisible to audio measurement, because the wrong value is a plausible value.
The cheap standing check is a grep for each param name asking whether anything
writes it, and a diff of which header bytes the decoder touches against how many
controls the MIDI map lists.

## Update 2026-08-26

Two entries move.

**The compressor's on/off is now decoded and honoured** (COMP track `param 0`).
This was worse than "not decoded": the compressor ran on EVERY render regardless
of the song, while every fixture switches ReBirth's off, so it silently
contaminated loud measurements. Its **ratio and threshold are still undecoded**,
and the phantom attack/release/makeup controls are still phantom, so the entry
stays open; the switch defaults off until they are measured.

**A third defect shape** to add to PHANTOM and DROPPED: **UNMEASURED
NONLINEARITY**, a saturator or dynamics stage in our chain with no measurement
behind it. Three were found the same day and all three were wrong (both drum
device buses and the master output). They are harder to spot than a phantom
control because nothing in the UI points at them and no quiet fixture reveals
them. See FINDINGS_AUDIT.md for the test that does.
