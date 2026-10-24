# Status, 2026-08-27, end of the PCF supertask

> **SUPERSEDED by `STATUS_2026-08-28.md`.** Kept for the record. What it got
> wrong: it presents supertask 4 as closed out and awaiting nothing but a
> signature. Luke's ear check reopened it the next morning, and the day that
> followed also found a 19 dB hole in the distortion, rebuilt the effect rack as
> per-section chains after measuring that ReBirth's sections do not meet inside
> a shared unit, and closed the two extrapolated laws it lists as open.

Supersedes `STATUS_2026-08-27.md`, which was written earlier the same day before
this work started and is marked as such at its head.

## Where the work is

**Supertask 4, the PCF, is measured end to end and closed out**, on branch
`pcf-supertask`, seven commits, **not merged and awaiting Luke's sign-off**.
Nothing is on `main` that was not there this morning.

Read in this order: `PARITY.md` for the settled laws, `FINDINGS_AUDIT.md` for
the accepted residuals with magnitudes, `PCF_FINDINGS.md` for the full working,
and `BACKLOG.md` for what was found and deliberately not fixed.

Verified at close: the 126-fixture set regenerates byte-identical, core tests
pass, `byte_neutral` baseline retaken at pooled hash `b318273e`.

**The old byte_neutral baseline will not match and should not.** The PCF now
runs in songs where it never did, and the distortion routing was corrected in
fifteen of sixteen. That is the change, not a regression.

## What the PCF turned out to be

Entirely unwired. Its chunk was never parsed, `song_.pcf` was an empty bank
whose default pattern has `gate = false` on every step, and the unit ran in
parallel with a glide standing in for the envelope. It had never modulated in
any render this project had made.

Now: chunk and routing decoded, cutoff and resonance and level laws measured,
mode switch implemented, envelope replaced with a real AD, all 55 preset
patterns measured off ReBirth rather than transcribed from Appendix D, and the
unit moved to a series insert after the distortion. Static filter 0.49 dB mean
rms; envelope 0.40 octaves rms with no systematic offset. 124 exports over six
rounds.

## Three things that were not the PCF

**The distortion was routed by the compressor's byte.** `MIXR` +1 is the
compressor's target and the loader read it as the distortion's, so every song
sent the distortion to whichever section the compressor was on. Wrong in fifteen
of sixteen demo songs. Fixed, because the PCF's own target is the adjacent byte
and could not be added without touching it.

**The effect TRAK order is DELY, DIST, PCF, COMP**, not what this repo claimed.
Harmless until now only because the one effect track being read was the
compressor's, which sits at index 8 either way.

**A 3.3 dB whole-mix stereo skew**, diagnosed to the delay's ping-pong and left
for supertask 5 on Luke's call. `BACKLOG.md` entry 1 has the isolation steps and
the fix.

## Open, in priority order

1. **Supertask 5 is the delay**, and backlog entry 1 lives there. Do the
   ping-pong, the discarded pan and triplet bytes, and the wet return's pan
   together so the return path is fitted once.
2. **The compressor** (supertask 6): ratio and threshold still undecoded, so
   ours defaults OFF while every demo song switches ReBirth's ON.
3. **The distortion** (supertask 7): its bus still skips the pan law, and
   whether ReBirth's unit is shared or per-section is unmeasured and now
   matters, because the PCF's section needs its own instance.
4. **The 808 bus linearisation and its refitted accent depth** rode along on
   measurement alone and have still never been eared. `listen-808-trakka/` is
   built and waiting.
5. Five extrapolated PCF parameter bytes and the pattern-length column, all
   named with their uncertainty in `FINDINGS_AUDIT.md`.

## The whole-mix number, and how to read it

Six demo songs exported at full length with the rack on sit at **2.06 dB mean
octave-band error and 2.01 dB mean level error** against our renders.

That is a baseline, not a parity figure. All six switch the master compressor ON
and ours defaults OFF, so every row compares an uncompressed render against a
compressed one, which is the exact confound this project spent months inside
before 2026-08-26. All six use the delay; four distort sections whose bus skips
the pan law. Re-measure after supertasks 5, 6 and 7 and expect most of it to
come from them.

`tools/rbsgen/wholemix_song.py` regenerates those references. Export renders the
transport LOOP, not the arrangement, so a demo song has to have its loop widened
first or the export is a few seconds long.

## Two lessons worth carrying, both from this session

**Change the SOURCE before the statistics.** Round 1 learned that a 303 drone
constrains a filter only between its fundamental and where its harmonics sink
into the export's dither. Round 5 forgot it and spent three estimators on the
same limitation before switching to a broadband probe, which settled it
immediately. A source limit looks exactly like estimator noise.

**Read the runbook before searching the disk.** A session hit ReBirth's CD modal,
went hunting for the installation ISO, found a second copy elsewhere and nearly
"corrected" the documented path to point at it. The documented path was right.

## New tools

| Tool | What it does |
|---|---|
| `tools/rbdrive/rbclick.py` | clicks controls that are not on the MIDI map, which is the only way to reach the mixer's DIST/PCF/COMP switches |
| `tools/rbsgen/wholemix_song.py` | widens a song's loop so an export covers the whole arrangement |
| `tools/measure/pcf_response.py` | static filter response from a fixture render |
| `tools/measure/pcf_envelope.py` | cutoff against time, for the envelope |
| `tools/measure/pcf_patterns.py` | reads the 55 preset patterns off ReBirth |
