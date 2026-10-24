# PCF findings

The live worklist for supertask 4, the Pattern Controlled Filter. Settled laws
graduate to `PARITY.md` at close-out; retired ones go to `FINDINGS_AUDIT.md`.

Started 2026-08-27. The PCF was entirely unwired before this: its chunk was
never parsed, its 55 patterns were never transcribed, and `song_.pcf` was an
empty bank, so `Bank::at()` handed the engine a default pattern whose every
step has `gate = false`. The filter has therefore never modulated in any
render we have ever made.

## 1. Inventory, from the manual

Sources: chapter 8 pages 61 to 66, chapter 15 pages 159 to 160, Appendix C page
198, Appendix D pages 205 to 220.

| Control | Range | What it does |
|---|---|---|
| On/Off | 0/1 | The unit's own bypass. Off means sound passes unfiltered. |
| Pattern | 0..54 | Selects one of 55 preset envelope patterns. Not editable. |
| Mode | LP/BP | Two-position switch, 12 dB/oct lowpass or bandpass. |
| Freq | 0..127 | The filter's base cutoff. |
| Q | 0..127 | Resonance. Described as output fed back to input. |
| Amt | 0..127 | How far the envelope moves the cutoff. Zero means no pattern effect. |
| Decay | 0..127 | The envelope's decay time. Attack comes from the pattern. |

Documented behaviours that are not controls:

- **The PCF is an insert on exactly one section.** Only one section at a time
  can have it, and selecting it on another takes it away from the first
  (page 61, page 64, page 62 step 16).
- **It comes after the distortion**, in series, when a section has both
  (page 64).
- **Freq at maximum makes Amt inert**, because the filter is already fully
  open (pages 65 and 160). Whatever cutoff law we fit has to saturate, not
  wrap.
- **Amt at zero disables the pattern entirely** and the PCF becomes a static
  tone control. This is the fixture setting that isolates the filter from the
  envelope.
- **Decay interacts with the pattern**: low is a pumping retrigger, high
  "smears", and very high never lets the envelope fall before the next trigger,
  which the manual likens to sample-and-hold (page 160).
- **The envelope is attack/decay**, with the attack per-step from the pattern
  and the decay from the panel (page 63).

Pattern categories, page 65:

| Patterns | Character |
|---|---|
| 0 to 5 | Even rhythms, no dynamics |
| 6 to 11 | Simple rhythms, no dynamics |
| 12 to 17 | Simple sweeps, slow dynamics |
| 18 to 21 | Complex sweeps, more or less random |
| 22 to 33 | Complex rhythms, dynamics and varying attacks |
| 34 to 45 | 32nd note patterns |
| 46 and up | Mixed variations |

**Appendix C's "LP/HP" for CC 95 is an error in the table.** Pages 63, 65 and
160 all say lowpass and bandpass, the block diagram on page 63 draws a bandpass
curve, and the panel legend reads BP over LP. Measured in section 3 and it is
lowpass and bandpass, so the prose is right and the table is wrong.

## 2. The file format, measured 2026-08-27

Measured by driving each control over its MIDI CC and diffing the saved song,
against a copy of the Silent Default Song whose PCF chunk is all zeros. Three
passes with distinct values per control, so no byte is ambiguous.

### The `PCF ` chunk, 12 bytes

| Offset | Field | Range |
|-------:|---|---|
| +0 | version (1; the ReBirth 1.0 default song writes 0) | |
| +1 | **Freq** | 0..127 |
| +2 | **Q** | 0..127 |
| +3 | **Amt** | 0..127 |
| +4 | **Pattern** | 0..54 |
| +5 | **Decay** | 0..127 |
| +6 | **Mode**, 0 = BP, 1 = LP (see section 3) | 0/1 |
| +7..+11 | zero in all 18 shipped songs and in every save we made | |

The panel order is Pattern and Mode on top, then the four sliders Freq, Q, Amt,
Dec left to right. The chunk is NOT in that order: it stores the four sliders
first but breaks them apart to put Pattern between Amt and Dec, and puts Mode
last. Confirmed by three saves at distinct values, so this is not inference.

Mode byte 0 draws the switch handle low, beside the LP legend, and this
document originally read that as LP. **It is BP**, measured in section 3: the
handle is drawn low when the switch is up. The screenshot settled that the two
modes are lowpass and bandpass rather than Appendix C's "LP/HP", and nothing
more.

The pattern CC clamps rather than scales: CC 0 gave pattern 0, CC 64 gave 54
and CC 127 also gave 54. So CC value maps 1:1 to pattern number and saturates
at the top of the range, and the corpus agrees, its highest observed value
being 52.

### The effect TRAK order was wrong in our decoder

`TRKL` holds one `TRAK` per device. FORMAT_RBS.md and the comments in
`rbs.cpp` both said

> MIXR, 303, 303, 808, 909, DELY, PCF, DIST, COMP

and the last three are not in that order. The file's order is

> MIXR, 303, 303, 808, 909, **DELY, DIST, PCF**, COMP

Two independent proofs. By parameter count: track 6 carries exactly params 0..2
and the distortion has two knobs plus its on/off, while track 7 carries params
0..6 and the PCF has six knobs plus its on/off. By value: track 7's tick-0 dump
reproduces the `PCF ` chunk byte for byte in Mutilated Species II and in
KiloMix, and across the corpus track 7 matches the PCF chunk on 27 of 31
never-ridden params where the next best candidate matches 6.

This was harmless only by luck. The one effect track we read, the compressor's
on/off, is at index 8 either way.

As with the drums, **a PCF TRAK's param N is chunk byte N**, and param 0 is the
unit's on/off.

### The routing switches live in `MIXR`, and we were reading the wrong byte

Measured by clicking each switch in ReBirth and diffing the save, since none of
them is on the MIDI map.

| Where | Field |
|---|---|
| `MIXR` +1 | **Compressor target**, exclusive |
| `MIXR` +2 | **PCF target**: 0 = none, 1..4 = section |
| `MIXR` +16+12c +4 | **Distortion on/off for section c** |

Clicking PCF on section 1 wrote 1 to +2, then clicking it on section 3 wrote 3,
not a second bit, and clicking section 3 again wrote 0. That is the exclusivity
the manual describes, stored as a selector.

Clicking DIST on section 2 toggled the channel-group byte at +32 and left +1 and
+2 untouched, so the distortion is a per-section flag and any number of sections
can carry it.

**`MIXR` +1 is not the distortion target.** `rbs.cpp` read it as one, mapping
`sel - 1` onto an engine channel, so every song has been routing the distortion
to whatever section the compressor was assigned to. Clicking COMP on section 1
wrote 2 and on section 4 wrote 5, so the encoding is offset by one from the
1-based section index, which leaves 1 for the master strip's own COMP button.
That last step is inference, not measurement: the master click did not register
and has not been retried. Treat "1 = master" as unconfirmed.

`MIXR` +2 is zero in all 16 demo songs while the mixer TRAK's param 2 at tick 0
is non-zero in 11 of them, so as everywhere else in this format **the tick-0
dump is the song's real starting state and the chunk is the save-time panel.**
Reading only the chunk would leave the PCF routed nowhere in every shipped song.

### The mixer TRAK, fully enumerated

| Param | Field |
|---|---|
| 1 | compressor target |
| 2 | PCF target |
| 6+6c | section c volume |
| 6+6c+1 | section c pan |
| 6+6c+2 | section c echo send |
| 6+6c+3 | section c distortion on/off |

Params 3, 4, 5 and 6+6c+4, +5 never appear. The per-channel `[on/off]` at the
head of each `MIXR` channel group has no TRAK param, which matches the manual:
the mute is "not part of the Song automation".

## 3. The filter, measured 2026-08-27

Two fixture rounds, 49 ReBirth exports, all with **Amt = 0** so the envelope is
out of the path and what is left is a static filter. Every number is the power
spectrum of a render divided by `pcf_ref`, the identical song with the PCF
routed nowhere, so the source cancels exactly.

Two sources, because one was not enough. A 303 drone (one note, slide on every
step, its own filter wide open) is steady and has 111 dB of range, but it only
constrains the filter between its 64.6 Hz fundamental and about 2.5 kHz, where
its harmonics sink into the export's dither. A closed hat on every sixteenth
covers 3.8 to 8.2 kHz and nothing else. Between them they cover bytes 48 to 127,
and they agree to 0.28 dB where they overlap at byte 112.

### The mode byte is the other way round from the panel

**Mode 0 is BANDPASS and mode 1 is LOWPASS.** Mode 1 fits a 2-pole lowpass at
0.01 to 0.5 dB rms across the whole sweep; mode 0 fits a 2-pole bandpass at 0.3
to 0.6 dB. Neither fits the other shape at all.

This contradicts what section 2 of this document originally recorded. That
reading came from a panel screenshot taken with the mode byte at 0, where the
switch handle is drawn low, next to the LP legend. The handle is drawn low when
the switch is UP. Appendix C's "LP/HP" is still wrong, but so was the
screenshot, and the audio is what settled it.

### Cutoff

Fitted over bytes 48 to 127, where at least one source has data either side of
the corner:

    fc = 9.01 * 2 ^ (0.08388 * byte)  Hz

fc(0) = 9.0 Hz, fc(64) = 372 Hz, fc(127) = 14.5 kHz, a span of 10.65 octaves.
Worst residual 0.45 dB, and 0.29 dB over the five drone points alone.

| Freq byte | 48 | 64 | 80 | 96 | 112 | 120 | 127 |
|---|---|---|---|---|---|---|---|
| measured fc, Hz | 148.8 | 373.0 | 935.2 | 2314.7 | 5877 / 6265 | 10176 | 14181 |
| law, Hz | 146.9 | 372.4 | 944.1 | 2393.6 | 6068 | 9662 | 14516 |

Bytes 0, 16 and 32 are NOT measured. Their corner lies below the drone's
fundamental, so the fit sees only rolloff and trades cutoff against gain: the
three fitted at 67, 68 and 71 Hz with gains of -29, -14 and +1 dB, which is one
degenerate answer three times, not a flattening law. Extrapolating gives 9.0,
23.6 and 61.9 Hz. Measuring them needs a source with content below 60 Hz.

**The bandpass shares the same corner.** At Freq 64 it fits 363.6 Hz against the
lowpass's 373.0, and at 96 it fits 2374.6 against 2314.7, both inside 0.2 dB.
That is what one state-variable filter with two outputs gives, and it means the
mode switch picks an output rather than retuning anything.

### Resonance

    1/Q = 0.1050 + 1.3385 * (1 - byte/127) ^ 1.77

Q(0) = 0.693, which is Butterworth to within a percent, so minimum resonance is
maximally flat rather than damped. Residuals are within 0.18 dB over bytes 0 to
96.

| Q byte | 0 | 16 | 32 | 48 | 64 | 80 | 96 |
|---|---|---|---|---|---|---|---|
| Q | 0.696 | 0.856 | 1.098 | 1.468 | 2.074 | 2.971 | 4.562 |

**Bytes 112 and 127 are not measured.** Both fitted to exactly 7.10, which is
the drone's 64.6 Hz harmonic grid failing to resolve a peak that has become
narrower than the spacing. The law extrapolates to Q(127) = 9.5; treat that as a
guess until a denser source measures it.

### Level

**Routing a section through the PCF costs nothing.** With the unit switched off
but the section still routed to it, the render is 0.00 dB from the unrouted
reference, flat across every bin. So the level changes below are the filter's,
not the routing's.

**An open filter at minimum resonance is +4.33 dB.** Flat across the spectrum
and identical at Freq 96, 112 and 127. This is a real makeup gain, not a
measurement artefact.

**That gain falls as resonance rises**, and then stops falling:

| Q byte | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|---|
| passband gain, dB | +4.42 | +3.23 | +1.93 | +0.38 | -1.65 | -4.04 | -4.05 | -4.15 | -4.15 |

Over bytes 0 to 64 this tracks the damping as roughly `k^0.56`, which is the
shape of an input scaling meant to keep the resonant peak in bounds. From byte
80 up it plateaus near -4.1 dB while Q keeps climbing, in both fixture rounds
and across four settings, so the plateau is recorded as measured rather than
explained.

The bandpass output peaks 0.9 to 1.7 dB above the lowpass passband at minimum
resonance (+5.2 to +6.0 dB against +4.33).

## 4. The filter against ReBirth

Our renders of the same fixtures, measured with the same script, compared on
the frequencies both renders keep:

| Fixture | mean err dB | rms dB |
|---|---|---|
| bypass | -0.00 | 0.00 |
| LP Freq 48 | -0.18 | 0.37 |
| LP Freq 64 | -0.19 | 0.27 |
| LP Freq 80 | -0.08 | 0.11 |
| LP Freq 96 | -0.09 | 0.12 |
| LP Freq 112 | -0.03 | 0.05 |
| LP Q 32 | +0.74 | 0.95 |
| LP Q 64 | -0.15 | 0.28 |
| LP Q 96 | -1.19 | 2.15 |
| BP Freq 64 | +0.51 | 0.52 |
| BP Freq 96 | -0.50 | 0.51 |

Mean 0.49 dB rms, worst 2.15 dB.

Three things had to be fixed to get here, and two of them were not the filter.

**The insert bus skipped the pan law.** Channels were summed into `pcfBus_`
without the pan gains the dry path applies, and `panGains` is 0.707 at centre,
so every PCF render came out 3.01 dB hot. The distortion bus still has this and
is left alone: it belongs to supertask 7.

**Our comparison indexed two different frequency grids against each other.**
`transfer()` keeps a different set of bins for each file, because each file's
own noise floor decides where its data ends, so `ha[i] - hb[i]` was subtracting
values at different frequencies. That reported 1.9 dB of error where there was
0.3 dB. Compare on the intersection, not by position.

**The TPT bandpass tap is 20*log10(k) below the analogue prototype**, verified
against an impulse response at 3.19 dB for k = 1.4435 and constant across the
band. Scaling by k fixes it. ReBirth's bandpass then still sits above its own
lowpass passband, by 0.68 dB at Freq 64 and 1.70 dB at Freq 96; the difference
is the drone sampling a narrow peak with a 64.6 Hz grid, so we take the mean.

### Accepted residuals

**Resonance 96 is the worst part of the static filter. RESOLVED and
re-characterised 2026-09-05, see section 11.** Round 11's broadband probe settled
it: ReBirth's corner drift with resonance is REAL (not the fit trading fc for Q),
about 13 to 16 percent of the corner, and OURS already models it (the "ours holds
at 372" below was the drone missing our own drift). What is actually left at high
Q is a ~0.1-octave base-cutoff offset at Freq 64 (ours ~5 to 8 percent low) and an
unmodelled resonance-vs-cutoff interaction (ReBirth's Q at a given knob rises with
cutoff); Q saturates at byte 112. The pre-round-11 reading is kept below for
history.

ReBirth's fitted
corner drifts with resonance, from 373 Hz at Q byte 0 down to 350 at byte 32
and back up to 440 at byte 112, while ours holds at 372. That U shape may be
real or may be the fit trading fc against Q; it needs a fixture that separates
them before it is worth modelling.

## 5. The envelope and the 55 patterns, measured 2026-08-27

### The source had to change twice

The static rounds used a 303 drone, and it is the wrong instrument for a moving
filter. Its power sits at a 64.6 Hz fundamental with a 6 dB per octave rolloff,
so frame by frame it resolves a cutoff only between about 60 Hz and 1 kHz, which
is 0.38 of the parameter range. An envelope sweeping the whole range spends most
of its life outside that window, and per-frame fits came back with r-squared
between 0.00 and 0.88. Worse, its 64.6 Hz period forces an analysis window of at
least 46 ms before band energy stops fluctuating within one cycle of the source,
and 46 ms is longer than the fastest decay settings.

The fix was a **broadband probe**: a kick, a crash and a closed hat on every
sixteenth, which covers 43 Hz to 18 kHz densely enough that no harmonic grid
constrains the window. At 11.6 ms it is stable, and pattern 0 then reads exactly
three triggers at thirty-seconds 0, 32 and 64 with velocities 0.49, 0.49, 0.49.

The lesson is the one round 1 already taught and I did not carry forward: when a
measurement will not settle, change the source before changing the statistics.
Three estimators were tried on the drone data and all three failed differently.

### The envelope

**Attack is instant and comes from the pattern; decay is exponential.** Freq and
the envelope sum in PARAMETER space and the sum is clamped, which is exactly why
the manual says Amt does nothing once Freq is at maximum:

    cutoff_param = freq + amt * env,   clamped to [0, 1]

**Depth is 1:1.** Peak parameter against Amt, at Freq 0 and Decay 64: measured
0.460, 0.556, 0.604, 0.720, 0.836, 0.953 for Amt 0.378, 0.504, 0.630, 0.756,
0.882, 1.000. The residual sits 0.03 to 0.05 below unity, consistent with the
window averaging across the peak. Amt below 0.38 is not measured: the bands
cannot resolve a cutoff that low.

**Decay: tau = 11.1 ms * e^(5.338 * knob)**, so 11 ms at the bottom and 2.31 s
at the top, doubling every 16.5 bytes. **SUPERSEDED 2026-08-29: this smooth
exponential was replaced by the measured quantised 16-step table (`tau = 88.6 ms
/ rate`, see `pcf.cpp` and §9), re-confirmed 2026-09-05. The table below is the
pre-refit fit.**

| Decay byte | 0 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---|---|---|---|---|---|---|---|---|
| tau, ms | 11.0 | 19.4 | 40.1 | 98.3 | 192.6 | 197.8 | 581.2 | 1154 | >1514 |

Measured as the time each trigger takes to carry the cutoff down past a fixed
frequency, which is proportional to tau with a known constant. Residuals reach
15 percent and byte 80 is a clear outlier; a trajectory fit would tighten both.
Byte 127 never crosses within a whole note, which is the manual's own
description of the top of the range: the envelope "will not be given a chance to
decay very much until it is retriggered".

### The 55 patterns

Measured, not transcribed from Appendix D. Each pattern is one fixture, the set
is one export batch, and what comes back is the thing itself rather than a
reading of a bar chart. The table is `src/effects/pcf_presets.inc`.

Every category the manual states is confirmed:

| Patterns | Manual says | Measured |
|---|---|---|
| 0 to 5 | even rhythms, no dynamics | every 32, 16, 8, 6, 4, 2 thirty-seconds, all velocity 1.00 |
| 6 to 11 | simple rhythms, no dynamics | irregular positions, all velocity 1.00 |
| 12 to 17 | simple sweeps, slow dynamics | smooth velocity ramps |
| 18 to 21 | complex sweeps | irregular in both position and velocity |
| 22 to 33 | complex rhythms with dynamics | as described |
| 34 to 45 | 32nd note patterns | several trigger on consecutive thirty-seconds |
| 46 and up | mixed variations | as described |

So pattern 0 retriggers on whole notes and 1 on half notes, exactly as page 64
says. **Pattern 5 triggers on sixteenths, not quarter notes.** The tutorial on
page 62 says "you will now hear a repeating effect occurring on quarter notes"
of pattern 5, and it does not: 0 to 5 is a clean halving series ending at
sixteenths. Measurement wins and the disagreement is recorded rather than
resolved.

**Not all of 34 to 45 are thirty-second patterns.** Patterns 34, 36 and 45
trigger on sixteenths. The detector resolves consecutive thirty-seconds in 37,
40, 41, 43 and 44, so this is not the analysis failing to separate them.

**Pattern length is the weakest column.** It is inferred by finding the smallest
shift under which the trigger set repeats over four bars. The manual says most
patterns are one or two bars, which is 32 or 64 thirty-seconds, so the entries
reading 96 are suspect and may be 64 or 128 that missed the velocity tolerance.

### Where we stand

Our renders of the same pattern fixtures, compared frame by frame as cutoff
parameter against ReBirth's:

| Pattern | 0 | 2 | 5 | 12 | 16 | 37 |
|---|---|---|---|---|---|---|
| parameter rms | 0.027 | 0.031 | 0.043 | 0.035 | 0.038 | 0.053 |
| mean error | -0.000 | +0.005 | +0.025 | -0.008 | -0.012 | +0.010 |

Mean 0.038 in a parameter that spans 10.65 octaves, so **0.40 octaves rms with
no systematic offset**. The densest patterns are the worst, which is expected:
back-to-back triggers into a 22 ms decay are the most sensitive thing here to
trigger timing.

## 6. Routing, automation, and a whole-mix baseline

### The PCF is a series insert now

The manual puts it after the distortion in the signal chain (p64), and we ran
both inserts as parallel sends into the master. Five of the sixteen demo songs
route the PCF to a section that is ALSO distorted, so this is not a corner case.

The chain is now: the PCF's section goes to its own bus, is distorted if that
section is distortion-routed, then filtered, then panned into the master, with
its echo send tapped after the inserts. Everything else that is distorted shares
the main distortion bus as before.

**That needs a second distortion instance**, and it is worth being explicit
about why. The unit carries per-channel filter state, so one instance cannot
process two different buses within a block. But a shared unit and two instances
are not the same thing: with one, two distorted sections intermodulate inside
the waveshaper, and with two they do not. **Whether ReBirth's distortion is one
shared unit or one per section is unmeasured**, and it decides which of these is
right. Recorded in `BACKLOG.md` for supertask 7.

### Timed automation

Each effect track's rides are now decoded and dispatched for the PCF. Mutilated
Species II carries 1242 events on its Q alone and 13 on its on/off switch, and
KiloMix 930; held flat, any song that performs its filter rendered with the
opening setting. The unit's on/off is a bypass rather than an unrouting: the
section keeps its pan and its post-insert send either way, which is what page
159 describes.

### A whole-mix baseline, and why it is not a PCF result

Six demo songs exported from ReBirth at full length with the rack on, chosen to
put the PCF on each kind of section plus one control that does not use it.
Compared against our renders, octave bands normalised to each file's own total
so a level offset cannot masquerade as a spectral error:

| Song | level dB | skew ours | skew ReBirth |
|---|---|---|---|
| Calm | -2.78 | +0.00 | +0.00 |
| G-Spot | +2.48 | +0.22 | +0.02 |
| Metallicon City | -1.15 | +1.52 | +0.50 |
| SuperNova | -2.80 | +3.79 | -1.18 |
| Trakka Rekka | +0.40 | +3.10 | -0.62 |
| Transfusion | +2.46 | +0.15 | +0.55 |

Mean absolute octave-band error 2.06 dB, worst 6.3 dB. Mean absolute level error
2.01 dB.

**Read this as a baseline, not as parity.** All six songs switch the master
compressor ON, and ours defaults OFF because its ratio and threshold are still
undecoded, so every row compares an uncompressed render against a compressed
one. That is the exact confound this project spent months inside before
2026-08-26. All six also use the delay, whose ping-pong is wrong, whose pan and
triplet switch we decode and discard. Four have distorted sections whose bus
still skips the pan law.

The stereo skew tracks the delay, as backlog entry 1 predicts: the three songs
sending to it from three or four channels skew +3.79, +3.10 and +1.52, while the
three sending from one or two skew +0.22, +0.15 and +0.00.

So the number to carry forward is: **with the PCF finished and the compressor,
delay and distortion still open, six full songs sit at 2.06 dB mean octave error
and 2.01 dB mean level error.** Re-measure after each of those three, and expect
most of it to come from them rather than from here.

## 7. What we do wrong today, before any DSP measurement

1. ~~No PCF pattern data exists.~~ All 55 measured; see `pcf_presets.inc`.
2. ~~`PcfStep` cannot hold a 32-step pattern.~~ The presets are a sparse
   (step, velocity) list on a thirty-second grid, and the unit keeps its own
   thirty-second clock re-synced on every sixteenth.
3. ~~The PCF is wired in parallel.~~ It is a series insert after the
   distortion now, with its own distortion instance when its section has both.
4. ~~There is no mode switch.~~ Done: both taps, byte 0 = BP and 1 = LP.
5. ~~The envelope is not an envelope.~~ It is an AD now, retriggered per step
   with the pattern's velocity, decay from the measured law.
6. ~~The cutoff span is a guess.~~ Cutoff and Q are measured (section 3). The
   DECAY law is still a guess.
7. ~~`satState` is a saturator with no measurement behind it.~~ Removed. The
   measured damping never goes below 0.105, so the linear core cannot run away
   and needs no limiter. The manual's warning that high Q can overload the PCF
   into distortion is about the OUTPUT stage, and nothing measured so far shows
   one, so nothing was put back.

## 8. Open

- The bottom three cutoff bytes and the top two resonance bytes, both needing a
  source the current pair does not provide.
- Why the resonance makeup gain plateaus at -4.1 dB.
- Everything still open in section 7: the envelope, the 55 patterns,
  the series routing after the distortion, and the exclusivity.
- The unconfirmed "compressor target 1 = master".

---

## 9. The dull drums Luke heard, 2026-08-28, and what they turned out to be

**RESOLVED 2026-08-29. Songs PERFORM the PCF's routing and we were holding it at
its opening value, so our filter ran over the whole song where ReBirth's runs
over part of it.** The diagnosis first written here, that the decay law was too
short, was wrong, and it was wrong because it read its settings off the wrong
row. That working is kept below the fix, marked, because two of the traps in it
are worth not falling into twice.

### What Luke heard

The drums are "muted, under-saturated, like the high has been cut down way too
far", and he suspected the filter rather than the drum synthesis and suggested
rendering with ReBirth's own samples to take synthesis out of the comparison.
Both halves were right, and the sample suggestion is what made the isolation
clean: G-Spot scores 1.50 dB mean octave error with our synthesised drums and
1.51 with ReBirth's own set, so the deficit was never in the voices.

### The mixer track's param 2 is the PCF's routing, and it is automated

`MIXR` +2 is the PCF's target and its equivalent on the mixer TRACK is param 2.
We read it at tick 0 and dropped every event after, because the track loop only
looks at params 6 and up (`base = 6 + 6*ch`) and params 1 and 2 fall below that.

G-Spot moves it **thirty-five times**. Decoded, at 32 ticks to the bar and
150 BPM:

| ticks | seconds | PCF |
|---|---|---|
| 0 to 767 | 0 to 38 | on the 909 |
| 767 to 4098 | 38 to 205 | **off** |
| 4098 to 4608 | 205 to 230 | on |
| 4608 to 5913 | 230 to 296 | **off** |
| 5913 on | 296 to the end | alternating every few beats |

Dividing ReBirth's filtered export by its dry one in the 2.8 kHz band, in five
second buckets, reproduces that timetable exactly: 14 to 39 dB of cut over the
first 38 seconds, **0.0 dB and bit-identical** from 39 to 204, cut again from
204 to 228. A filter cannot be transparent at 11 kHz and 44 Hz at once, so a
median of 0.00 in every band was never "sitting open". It was "not in the path".

**Twelve of the sixteen demo songs ride this**, with on-time from 8 percent
(RSI) through 27 (G-Spot) to 100. Six also ride the compressor's target on
param 1, and three ride the PCF unit's own bypass on its own track.

### What it was worth

G-Spot's 909 alone, everything else muted and every other effect off, rendered
with ReBirth's samples, octave-band error against ReBirth:

| | 44 | 88 | 176 | 353 | 707 | 1414 | 2828 | 5656 | 11313 | mean |
|---|---|---|---|---|---|---|---|---|---|---|
| PCF out of the path | -0.31 | -0.24 | +0.20 | -1.64 | -0.45 | -0.10 | -0.08 | -0.57 | -0.65 | **0.47** |
| PCF in, before | -0.1 | +0.1 | +0.7 | -0.4 | +2.8 | +1.0 | **-6.5** | **-14.1** | **-21.6** | 5.3 |
| PCF in, after | -0.32 | -0.26 | +0.19 | -1.72 | -0.76 | -0.43 | -0.29 | -0.62 | -0.67 | **0.59** |

So the filter now costs 0.12 dB where it cost nearly 5, and the worst band left
(-1.7 at 353 Hz) is present with the filter out too, so it is not the PCF's.
Full-mix RMS agrees to 0.26 dB and the envelope correlates at 0.991 linear,
0.952 log.

The whole-mix figure across six songs barely moves, 1.77 to 1.76 dB octave error
with level going 1.82 to 1.94. That is the expected non-result: the PCF is one
section of four in those, and all six still compare our uncompressed render
against ReBirth's compressed one. The level rising is what removing an erroneous
high-frequency loss does against a reference we are already louder than.

### Two things this cost, both about reading the right row

**The settings quoted below came off the automation track, and the fixture came
off the chunk.** They disagree, and both are real: `PCF ` says Freq 70, Q 64,
Amt 40, Pattern 24, Decay 90 and the track's tick-0 dump says Freq 53, Q 82,
Amt 68, Pattern 32, Decay 67. The chunk is the panel at SAVE time; the track is
the song's START and is what plays. "Decay 67" was neither wrong nor the
decay of the render being measured, which makes it the worst kind of number.

**"Param N is chunk byte N does not extend to the mixer track" was too strong.**
Params 1 and 2 really are the compressor's and the PCF's targets, matching
`MIXR` +1 and +2 exactly. They do not move when you CLICK the switch and save,
which is what that finding measured, because a click writes the chunk and only
recording writes the track. Two different things, one of which was mistaken for
evidence about the other.

### The decay law is still the weakest fit in the component

**SUPERSEDED 2026-08-29 and re-confirmed 2026-09-05.** The trajectory fit was
done (rounds 7 and 8): the decay is a QUANTISED 16-step table, `tau = 88.6 ms /
rate`, rate a 1-2-3-4-5-7 series, in seconds, applied in `pcf.cpp` (`kDecayRate`).
It is no longer the weakest fit. An independent trajectory fit on the round-7
ReBirth renders on 2026-09-05 recovered the table to under 1% on 12 of 13
resolvable bytes, and our own engine measured the same to under 1% (one method
both sides), so byte 80 reads 293 ms, not the old outlier 198. The paragraph
below is the pre-refit state, kept for history.

It is no longer suspected of anything. It came from a threshold-crossing metric
with 15 percent residuals and a clear outlier at byte 80, and a trajectory fit
would tighten both. That is worth doing on its own merits and is not urgent.

---

## 9a. The 2026-08-28 diagnosis, kept and RETIRED

> Everything below is superseded by section 9. It concluded that the decay law
> was too short, from a filtered-over-dry median of -11.47 dB against ReBirth's
> 0.00. Both numbers were real; the reading of them was not. ReBirth's 0.00 was
> the filter being switched out for most of the song, and our -11.47 was ours
> running throughout. The supporting argument used Decay 67 and pattern 32 from
> the automation track against a 200 ms trigger gap, and the arithmetic was
> sound on numbers that did not describe the render being measured.

Luke on the whole-mix excerpts: the 303 is right on all three, but the drums are
"muted, under-saturated, like the high has been cut down way too far", and he
suspected the filter rather than the drum synthesis. He suggested rendering with
ReBirth's own samples to take the synthesis out of the comparison. Both halves
of that were right.

### It is not the drum voices

G-Spot with our synthesised drums against G-Spot with ReBirth's own sample set:
**1.50 dB mean octave error against 1.51 dB.** Switching to the reference
samples changes essentially nothing, so the deficit is downstream of the voices.

### It is the PCF, and only when it is running

G-Spot's 909 alone, every other device muted and every other effect switched
off, our render using ReBirth's samples:

| | 44 | 88 | 176 | 353 | 707 | 1414 | 2828 | 5656 | 11313 |
|---|---|---|---|---|---|---|---|---|---|
| PCF off, error | -0.1 | 0.0 | +0.5 | -1.5 | -0.3 | +0.1 | +0.1 | -0.3 | -0.4 |
| PCF on, error | -0.1 | +0.1 | +0.7 | -0.4 | +2.8 | +1.0 | **-6.5** | **-14.1** | **-21.6** |

With the filter out of the path we match ReBirth to **0.39 dB mean**. Switch it
in and the top three octaves collapse. That is exactly what Luke heard.

### How far out, precisely

Dividing the filtered render by the dry one gives the PCF's own effect over
time. In the 2 kHz band:

| | median | 10th pct | 90th pct |
|---|---|---|---|
| ReBirth | **0.00 dB** | -13.11 | 0.00 |
| ours | **-11.47 dB** | -22.18 | +0.89 |

ReBirth's filter sits open almost all the time and dips between triggers. Ours
sits 11.5 dB down. The static response is not the problem: that measures 0.31 dB
mean. **The envelope is not staying open the way ReBirth's does.**

### The likely cause is my own weakest fit

G-Spot runs Decay byte 67. The decay law was fitted by a threshold-crossing
metric with 15 percent residuals and one clear outlier at byte 80, which
measured 197.8 ms where the trend says about 380. Byte 67 sits right beside it,
and the law gives 185 ms there.

Pattern 32 has 24 triggers across 96 thirty-seconds, and at G-Spot's 150 BPM
that is a 200 ms average gap. With tau at 185 ms the envelope falls most of the
way between triggers. With tau twice that it would stay largely open, which is
what ReBirth does.

So the first thing to re-measure is the decay law, properly this time: fit the
trajectory rather than one crossing time, over a source that resolves it, and
check byte 80 specifically.

### A second thing this casts doubt on

The **+4.33 dB open-filter gain** does not appear here. Round 2 measured that
routing a section through an open PCF adds 4.33 dB flat, with the bypass at
exactly 0.00. On G-Spot, switching the unit on changes the whole render by
-0.07 dB, and the filtered-over-dry median is 0.00 rather than +4.33. The
difference between the two situations is Amt: round 2 held it at 0 throughout.
That law may be conditional on something the round-2 fixtures held fixed.

---

## 10. The preset table disagrees with Appendix D, and the audio round is the
## one that is wrong

Found 2026-08-29 while looking for the decay law's fixtures. Not yet verified
against ReBirth, so `pcf_presets.inc` is UNCHANGED and still carries the audio
round's table. This section is the case for replacing it and the design of the
round that would settle it.

### Reading the diagrams by pixel

`tools/measure/pcf_appendixd.py` reads Appendix D's 55 bar charts off the page
images. Bars are solid black on white sitting on a horizontal axis, so their
positions and heights come out as numbers with no eye in the loop. Calibrated on
page 207, where patterns 3, 4, 5 and 6 have known step positions and no
dynamics: the grid starts at x = 71 with 37.25 px per step and a full-height bar
is 238 px. **Every bar across all 55 patterns lands within 0.13 of a grid
point**, and every bar in patterns 0 to 11 measures 237 or 238 px, which is what
the manual means when it says those have no dynamics.

**The grid unit is not the same on every diagram**, and reading it as sixteenths
throughout is the trap. Patterns 0 to 33 are drawn on an axis labelled 1..16
twice, two bars of sixteenths. Patterns 34 to 45 are drawn on an axis labelled
1..32, one bar of THIRTY-SECONDS. The mixed group uses both: 46, 47 and 52 are
thirty-second diagrams and the other six are sixteenth. The unit is detected
rather than assumed, from the width of the label at grid position 17, which is a
bare "1" on a doubled axis and "17" on a thirty-second one: 5 px against 21 px
with nothing in between across all 55.

### Where the two sources agree, and where they do not

`tools/measure/pcf_presets_ab.py` rolls both out over twelve bars and compares.

**Patterns 0 to 8 and 10 agree exactly**: identical step sets, largest velocity
difference 0.01. They also agree that pattern 3 is twelve steps and that pattern
5 triggers on sixteenths, which is the point where the manual's own tutorial
prose (p62) is wrong. So this is not the manual overruling a measurement.

Three kinds of disagreement, each with a mechanism:

**Velocities are compressed on every pattern with dynamics.** The fixtures were
sound: they used the broadband probe, not the drone, whatever `pcf_patterns.py`
says about itself in its own docstring. **The ESTIMATOR saturated.** It held Amt
at 64/127 and read the cutoff out of an 88..354 Hz band, and at that Amt a
velocity-1.0 trigger puts the corner at 372 Hz, right at the top of the band:

| true velocity | 0.1 | 0.3 | 0.5 | 0.7 | 0.9 | 1.0 |
|---|---|---|---|---|---|---|
| corner, Hz | 13 | 28 | 58 | 122 | 257 | 372 |
| band attenuation, dB | -40.9 | -28.0 | -15.1 | -3.1 | +3.3 | +4.0 |
| sensitivity, dB per unit v | 64.6 | 64.5 | 63.6 | 52.0 | 12.6 | - |

Sensitivity falls five to one across the range, and velocity 1.0 lands at
+4.04 dB where the estimator's own rejection threshold is +3.93, so the top of
the range is not merely compressed but discarded. The round normalised against
patterns 0 to 11, which have no dynamics and therefore only ever sit at ONE
point on that curve, so nothing in the normalisation could reveal it.

Pattern 12 is the clean case: the manual draws a symmetric 0.03 to 0.90 triangle
and the audio round read a noisy 0.14 to 0.34.

**Phase is lost.** The audio round took its origin from the first trigger it
found in its analysis window, so every pattern was rotated to start on step 0.
Pattern 9 is the proof: the same rhythm in both sources, offset by exactly one
sixteenth, sharing not a single step.

**The 32nd-note group is half-read.** Patterns 34 to 45 are drawn with a trigger
on every thirty-second, alternating full height with 0.04. The detector's
prominence threshold was 0.045, so it saw only the full ones and read pattern 34
as sixteenths.

### What it is worth, and why that does not settle it

Rendering G-Spot's 909 with each table in turn, against ReBirth's export:
**0.59 dB mean octave error either way, identical to two decimals.** That song
cannot discriminate them. Its PCF is in the path for 27 percent of the song and
its Freq is ridden 289 times, so a whole-song band total averages the difference
away. A fixture is needed.

### The round that would settle it

Broadband probe throughout: kick, crash and closed hat on every sixteenth, which
covers 43 Hz to 18 kHz densely enough that no harmonic grid constrains the
analysis window. That is the source the envelope round already established, and
using the drone instead is what caused all three problems above.

| Batch | Fixtures | Settings | Answers |
|---|---|---|---|
| P, velocities | 12 patterns where the tables differ most, plus a reference | Freq 0, Amt 127, Q 0, LP | with Freq at 0 and Amt at full the cutoff parameter IS the velocity, and the probe resolves it from 0.21 to 1.00 |
| T, the tiny bars | patterns 34 and 41 | Freq 64, Amt 64 | whether a 0.04 bar is a real trigger or the drawing's way of writing zero, and whether a retrigger RESETS the envelope or takes the max, which is what decides if it matters |
| D, decay | pattern 0 at thirteen decay bytes, plus two tempos at byte 64 | Freq 0, Amt 127, Q 0, LP, slow tempo | the decay law by trajectory fit rather than one crossing time, byte 80 specifically, and whether the law is in seconds or in beats |

Batch D is the job this session was picked up to do. It is unchanged by all of
the above except that it is no longer urgent, since the decay law is no longer
suspected of causing anything.

---

## 11. Round 7: the retrigger rule, the decay law, and what the patterns are

51 ReBirth exports, 2026-08-29, all on the broadband probe. Three questions.
Two are settled, one needed a second round because the first got a fixture
setting wrong.

### The estimator, and why this round can be trusted where round 5 could not

`tools/measure/pcf_cutoff.py` recovers the cutoff parameter frame by frame by
FITTING the whole spectrum to a 2-pole shape, rather than inverting one band's
attenuation. A corner is where a curve bends and the bend is visible from either
side, so one estimator covers the range and reports a residual. Frames whose
best fit is poor, or whose corner is pinned at an end of the search grid, come
back as nothing rather than as a number.

**It is calibrated against a law we already know.** Rendering the same fixtures
with our own engine, whose decay law is exact, and fitting them identically:
the fitted tau is high by 1.046 with a standard deviation of 0.007 over two
decades, no trend, so it is a constant and divides out. After correction the
method recovers a known tau to within 1.2 percent. Round 5's crossing-time
metric had no equivalent number because a single crossing cannot produce one.

Two bugs in the round's own design, both found by the data and both worth not
repeating. **Frames must be stamped at their CENTRE**: stamped at their start, a
frame's window reaches forward past its own label, so the peak search found the
onset of the NEXT trigger and every decay looked like it peaked just before it.
And **the second pass wasted nine fixtures on a reference that had the PCF ON
with Amt at zero**, which divides one 2-pole response by another rather than
producing a denominator. The unfiltered reference is the only denominator.

### A retrigger RESETS the envelope. Settled.

Pattern 34 is drawn as a trigger on every thirty-second alternating full height
with 0.04. Rendered at Freq 64, Amt 63, Decay 96, ReBirth's cutoff parameter
alternates **1.012 and 0.525** on consecutive thirty-seconds, which back out to
velocities of **1.024 and 0.042**, and it does so for the whole render.

An 886 ms decay cannot fall from 1.0 to 0.04 in the 83 ms of one thirty-second.
Nothing but a hard reset can. Scoring the whole trajectory against both models
confirms it: reset 0.067 rms in the parameter, max-style 0.286.

This also settles the tiny bars. **They are real triggers, at velocity 0.04**,
and under a reset they matter a great deal, because they cut a running envelope
down to nothing every other thirty-second. Our engine already resets, on the
strength of a comment that reasoned from traces of EQUAL velocities and
therefore had no evidence either way. It is right, and now for a reason.

### The decay is exponential in the PARAMETER, and its knob is in SECONDS

Fitting each trigger's whole trajectory against both an exponential in the
parameter and a straight line in it: exponential wins at every setting, 0.005
against 0.020 rms, which is the same shape our engine already has.

**Absolute, not tempo-synced.** The same bytes at 30 BPM and at 90 BPM:

| byte | 32 | 64 | 80 | 96 | 112 |
|---|---|---|---|---|---|
| tau at 90 BPM, ms | 29.2 | 177.9 | 292.8 | 886.1 | 1772.8 |
| tau at 30 BPM, ms | 28.8 | 177.3 | 292.9 | 880.9 | 1763.8 |

Within 0.5 percent at every setting, over a threefold tempo change. Nothing had
established this before and it was a real fork: a tempo-synced law would have
made every fixture round's tempo a hidden variable.

### tau multiplies by exactly TEN every 48 bytes

| bytes | 16 to 64 | 24 to 72 | 32 to 80 | 40 to 88 | 48 to 96 | 56 to 104 | 64 to 112 |
|---|---|---|---|---|---|---|---|
| ratio | 9.99 | 9.95 | 9.79 | 10.06 | 10.06 | 10.03 | 9.97 |

Seven independent confirmations. Our law implies a decade every 54.8 bytes, so
its exponent is 14 percent wrong.

**And a pure exponential is not the whole law.** Against tau = 8.81 ms times
10^(b/48) the residuals are 0.938, 0.804, 0.731, 0.735, 1.000, 0.977, 0.937
over bytes 16 to 64, and 0.937, 0.800, 0.716, 0.739, 1.006, 0.980 over bytes 64
to 112. The two lists are the same list. A 29 percent structure that repeats
exactly at 48-byte offsets is not noise, and the control says it is not the
method either: the identical estimator on our own renders, whose law is a pure
exponential by construction, has a 2.6 percent spread with no periodicity.

So the law is a mantissa repeating once per decade. Round 8 maps one decade at
two-byte resolution, which is all that is needed because it repeats.

Bytes 0 and 8 still do not resolve: tau there is around 9 and 13 ms against an
11.6 ms analysis window. A 5.8 ms window reaches byte 16 and no further.

### The preset table: Appendix D has the positions, and the scale is still open

Three things are settled and all three go against the shipped table.

**Phase.** Pattern 9's first trigger lands at thirty-second step 2, where
Appendix D draws it. The shipped table has it at step 0, because round 5 took
its origin from the first trigger it found.

**The 32nd grid.** Pattern 34 triggers on every thirty-second, as Appendix D
draws it and as the shipped table does not.

**Shape and period.** Pattern 12 traces a clean symmetric triangle peaking at
thirty-second step 16 and repeating every 32, which is Appendix D exactly. The
shipped table has it peaking at steps 5 and 12.

Scoring whole trajectories, Appendix D beats the shipped table on every fixture
that resolves: 0.072 against 0.217 on pattern 44, 0.067 against 0.288 on
pattern 34, 0.209 against 0.276 on pattern 12.

**But the velocity SCALE is not confirmed.** Pattern 34 reads 1.024 and 0.042
against drawn 1.00 and 0.05, and pattern 44 reads a constant 0.915 of its drawn
heights. Pattern 12 reads **0.60 where the drawing says 0.90**, with the right
shape and the right mean but two thirds of the drawn slope. The y-axis is 239 px
on every diagram so that is not a scale difference between them, and patterns 42
and 44 draw bars ABOVE the height patterns 0 to 11 use as full, so full height is
not simply 1.0 either.

Round 7's batch P could not settle it because Decay 8 puts tau at about 13 ms
against an 11.6 ms window, so its peaks smear into the floor: pattern 12
resolved not one frame. The configuration that works is the one `t_p34_tiny`
used, Freq 64 with Amt 63 and a long decay, which resolved 93 percent of frames
and read velocities directly. Round 8 applies it to twelve patterns.

---

## 12. Rounds 8 to 10: the decay law solved, and the preset table rebuilt

150 more ReBirth exports on 2026-08-29. Three results.

### The decay knob is SIXTEEN QUANTISED STEPS, and its rates are 1-2-3-4-5-7

Round 8 swept one whole decade at two-byte resolution. tau comes back
bit-identical inside every group of eight bytes and steps at every multiple of
eight: 88.6 ms over bytes 48 to 55, 126.6 over 56 to 63, 178.0 over 64 to 71,
222.9 over 72 to 79, 293.0 over 80 to 87, 443.5 over 88 to 95. So the knob is a
sixteen-entry table indexed by `byte >> 3`.

The sixteen rates are the classic logarithmic series, and tau = 88.6 ms / rate:

| index | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| rate | 10 | 7 | 5 | 4 | 3 | 2 | 1 | 0.7 | 0.5 | 0.4 | 0.3 | 0.2 | 0.1 | 0.07 | 0.05 | 0.04 |
| tau, ms | 8.9 | 12.7 | 17.7 | 22.1 | 29.5 | 44.3 | 88.6 | 126.6 | 177.2 | 221.5 | 295.3 | 443.0 | 886.0 | 1265.7 | 1772.0 | 2215.0 |

Thirteen of the sixteen are measured directly and the series reproduces them to
1.2 percent worst, 0.6 rms. Indices 0, 1 and 15 sit outside what a fixture can
resolve, below the analysis window at the bottom and longer than the render at
the top, and come from the series that the other thirteen establish.

This also retires round 7's "fine structure". Sampling every eight bytes landed
exactly on the table's own boundaries, so a 29 percent wobble against a smooth
exponential was the table itself.

**Against ReBirth's own exports our decay now measures 0.2 percent mean error
and 1.9 percent worst over thirteen settings.** The old law ran 43 percent long
at byte 32 and 41 percent short at byte 96.

### Two engine bugs the pattern A/B found

**The PCF's clock was bar-local.** `Engine::fireStep` passed `step_`, which wraps
at sixteen, so the preset restarted at every bar line. Every pattern whose
length is not a divisor of 32 played wrong: pattern 51 is 18 thirty-seconds and
repeated its first nine. Twenty of the 55 have such a length. It takes
`songTicks_ / 2` now, which is free-running.

**The first tick of a render does not trigger.** ReBirth's step 0 reads velocity
0.10 where every trigger after it reads 1.03, on patterns 0, 1 and 2 whose every
trigger is full velocity, and the same on 40, 49 and 51. The envelope fires on
the transition INTO a step and the first step is entered without one.

Those two took the 55-pattern trajectory error from 0.078 to 0.046 and then to
0.021 in the cutoff parameter.

### The preset table, rebuilt from two rounds

One decay setting cannot answer both questions, which is why round 9 alone was
not enough. At Decay 96 (tau 886 ms) the envelope falls only to 0.911 between
thirty-seconds, so a reading taken 5.8 ms after a trigger IS that trigger's
velocity, but a descending ramp that retriggers every step and one that decays
freely from a single trigger look almost identical: pattern 44 came back with
one trigger where it has thirty-two. At Decay 64 (tau 178 ms) the envelope falls
to 0.627 and the same ramp departs from free decay by 0.16 against a noise of
0.014, so the POSITIONS are unambiguous there.

So round 10 gives positions and length, round 9 gives velocities, and Appendix D
is the cross-check on the positions the audio cannot see: a ramp's low end
converges with free decay however long the render, because a velocity-0.03
trigger and no trigger both leave the envelope on the floor.

**The drawn length is checked, not trusted.** 54 of the 55 fold at Appendix D's
length to under 0.03 in the parameter. **Pattern 53 does not, at 0.134**: it
triggers every FOUR thirty-seconds with its velocities forming a triangle over
128 of them, where the drawing has it every two over 62. Folding at the drawn
length averaged its whole ramp into a flat 0.47. There the audio supplies both.

**And the drawing's velocities are only right on the 1/32 axis.** Fitting
measured against drawn, the patterns on the thirty-second axis come out at slope
0.96 to 1.04 with correlations of 0.985 to 1.000, and the ones on the sixteenth
axis with dynamics come out at 0.46 to 0.74. That is not the estimator: running
the identical extraction over OUR OWN render, where the shipped table's
velocities are known exactly, recovers them at slope 0.948 with a correlation of
1.000.

### Where the PCF now stands

| | before today | now |
|---|---|---|
| 55 patterns, cutoff trajectory vs ReBirth, Decay 96 | not measured this way | **0.021 param, 0.226 octaves** |
| the same at Decay 64, an independent setting | not measured | **0.020 param, 0.216 octaves** |
| decay law vs ReBirth, 13 settings | 43 percent long to 41 short | **0.2 percent mean, 1.9 worst** |
| G-Spot 909 alone, PCF in the path | 5.3 dB mean octave error | **0.57 dB** |
| the same with the PCF out of the path | 0.47 dB | 0.47 dB |

So the filter now costs 0.10 dB on a real song where it cost nearly five.

The whole-mix figure over six songs is 2.22 dB octave and 1.68 dB level, against
2.13 and 1.69 before this change and 1.77 and 1.94 before the distortion switch
was honoured. It is not moving because it cannot: the PCF is one section of
four, all six songs switch ReBirth's master compressor on while ours defaults
off, all six use the delay whose ping-pong is wrong, and four distort sections
through a unit that BACKLOG 2 measures as 25 to 38 dB short in the top three
octaves. SuperNova is the one row that moved much, 2.46 to 3.02, and it runs the
PCF in BANDPASS on pattern 25; that is worth a look when the compressor stops
masking it.

---

## 13. Status at the end of 2026-08-29, and what is left

**Supertask 4 is measured end to end and waiting on Luke's ear.** Ten fixture
rounds, roughly 300 ReBirth exports.

| | where it stands |
|---|---|
| all 55 preset patterns, cutoff trajectory vs ReBirth | **0.226 octaves** at Decay 96, **0.216** at Decay 64, the second an independent setting and so a check on table, retrigger rule and decay law together |
| decay law, 13 settings | **0.2 percent mean error, 1.9 worst** |
| static filter, 11 settings | 0.31 dB mean, 0.52 worst |
| G-Spot's 909 alone, PCF in the path | **0.57 dB** mean octave error against 0.47 with it out, so the filter costs 0.10 dB |

**What is NOT the PCF and is still in every whole-mix number:** the master
compressor, which every demo song switches on and ours defaults off because its
ratio and threshold are undecoded and its chunk is not parsed; the delay's
ping-pong, `BACKLOG` 1; and the distortion's character, `BACKLOG` 2, which is 25
to 38 dB short in the top three octaves and now runs where ReBirth runs it.

**Open, small:** the +4.33 dB open-filter gain does not appear on G-Spot and may
be conditional on Amt (section 9); SuperNova is the one whole-mix row that moved
much on 2026-08-29, 2.46 to 3.02, and it runs the PCF in BANDPASS on pattern 25.

**Before sign-off:** a listening set cut per the protocol in
`docs/research/PLAN_ISOLATION_TESTS.md`. The 2026-08-29 sets used the whole demo songs
with the rack on, which is what that protocol exists to stop. Batch P should be
the 909 bed with the PCF alone, opening with the null pair.

## 11. Round 11, 2026-09-05: the corner drift is real, the byte extremes resolved, a high-Q residual remains

Round 11 (17 fixtures, `fixtures-2026-09-05/pcf-round11`, exported to
`renders/pcf-round11-2026-09-05`) took the two static-filter gaps from section 8
with the BROADBAND probe (kick + crash + closed hat every sixteenth) that
resolves a narrow high-Q peak and the sub-bass a nearly-closed filter passes,
both of which the round-2 303 drone could not. A 2-pole (fc, Q) fit to the whole
transfer curve (`pcf_response.py --fit`) separates the corner from the resonance.

**The corner drift with resonance is REAL, not the drone fit trading fc for Q.**
The broadband fit reproduces it at a tighter 0.3 to 0.4 dB rms (the drone was
0.5+). At Freq 64 the fitted corner rises 379 -> 441 Hz as Q climbs 0 -> 112; at
Freq 96 it rises 2445 -> 2758 Hz. So the drift is roughly a fixed FRACTION of the
corner, about 13 to 16 percent, not a fixed Hz offset. And OURS already drifts
(347 -> 415 Hz at Freq 64), so section 4's "ours holds at 372" is retired: our
quadratic corner-drift model tracks the shape.

**Q saturates at byte 112.** At Freq 64, q112, q120 and q127 fit bit-identically
(fc 441, Q 6.14), so the top two resonance bytes carry no information above 112.
That closes the "top two resonance bytes" gap: there is nothing there to measure.

**The bottom cutoff bytes stay source-limited.** Freq 0, 1, 2 fit to 30 to 38 Hz,
which is the broadband probe's own low-frequency floor (the kick has little below
about 40 Hz), not a resolved corner. No departure from the cutoff law appears;
the law's own values there (9 to 10 Hz) are simply below what any drum probe
reaches. Resolving them would need a sub-40 Hz sustained source, and the payoff
is two bytes at the very bottom of the knob.

**What remains is a high-resonance residual, and it is the worst part of the
static filter.** Ours against ReBirth on the same broadband fixtures, same method
both sides (fits, which smooth the noisy per-bin drum ratio):

- Ours sits about 5 to 8 percent LOW in fc at Freq 64 across the whole Q sweep
  (347 vs 379 at Q 0, 415 vs 441 at Q 112), a base-cutoff gap of roughly 0.1
  octave that is stable with Q. The drone missed it because its harmonics sit
  mostly in the flat passband below the corner, where an 8 percent fc shift is
  nil.
- The resonant-peak STRENGTH diverges at high Q. ReBirth's fitted Q reaches about
  6 at Freq 64, byte 112; ours reads lower and the fit destabilises there. And a
  resonance-vs-cutoff INTERACTION is visible: for the same Q byte, ReBirth's Q is
  higher at Freq 96 than at Freq 64, which the current single Q(knob) law does not
  capture. Separating that properly needs a Freq x Q plane, not the one-cutoff Q
  sweep here. Logged in `BACKLOG.md` as a PCF refinement.

So the export round CLOSED its two targets (corner drift confirmed real and
already modelled; byte extremes resolved) and characterised the remaining
residual: a ~0.1-octave base-cutoff offset at Freq 64 and an unmodelled
resonance-vs-cutoff interaction at high Q. Both live at high resonance, which
demo songs use sparingly. Recorded as accepted residuals pending a decision on
whether a Freq x Q refinement round is worth it before the whole-path assessment.

### RESOLVED 2026-09-05: the "high-Q residual" is a MEASUREMENT ARTIFACT; the static filter matches to ~0.3 dB

Pushing into the Freq x Q refinement, the first step was to confirm the residual
is real before touching the cutoff law. It is not. Two artifacts fooled the
round-11 conclusions above:

1. **The broadband drum probe is unreliable for the STATIC transfer.** Its
   transient, noise-like spectrum makes the filtered-over-dry ratio biased in the
   rolloff (it read ours +6 to +10 dB above ReBirth across 500 to 2200 Hz at
   EVERY resonance including Q 0). The clean 303 drone at the same Freq 64 Q 0
   shows ours matching ReBirth to +/-0.3 dB across the whole rolloff, so the
   broadband +6 to +10 dB is not a filter difference.
2. **The 2-pole (fc, Q) fit absorbs a source/shape difference as a phantom
   corner offset.** It gave ReBirth +2% of our cutoff law and ours -7 to -9%, a
   DIFFERENT bias per side, which is the signature of the fit trading fc against
   shape rather than reading a real corner. The "ours ~0.1 octave low" was that.

The reliable measurement is the drone compared harmonic-by-harmonic, same note
and same harmonics both sides, passband-normalised. On the CURRENT build
(re-rendered 2026-09-05, not the stale 08-27 renders), ours against ReBirth over
the drone's reliable band (below 2 kHz, above which its harmonics go sparse):

    Freq 64 Q 0 :  0.14 dB rms   (max 0.55)
    Freq 64 Q 64:  0.24 dB rms   (max 0.71)
    Freq 64 Q 96:  0.38 dB rms   (max 1.98 at one near-peak bin)

The resonant PEAK matches: at Q 96 the drone harmonic at 382 Hz reads ReBirth
+12.4 dB and ours +12.8. So the corner drift, the peak height and the peak
position all match at high resonance. The old "2.15 dB at Q 96" (section 4) was a
small makeup-level offset (its -1.19 mean) plus the sparse top-of-band drone
points, not a filter-shape error, and it is retired.

CONSEQUENCE: no cutoff-law or Q-law change is warranted; the Freq x Q refinement
(BACKLOG 12) is RETIRED as chasing a measurement artifact. The PCF static filter
matches ReBirth to about 0.3 dB across cutoff and resonance including the high-Q
peak. METHOD NOTE for future static-filter work: use the drone harmonic-by-
harmonic passband-normalised, not the broadband drum probe (transient noise) and
not the 2-pole fit's fc/Q (absorbs shape as a phantom corner).

## 12. The bandpass FIXED, and the remaining gap is the dynamic resonance ring (2026-09-06)

Luke's ear on the BP-fixed listening set: "on the whole the ReBirth version sounds
more flangy, more like a ring modulator, with a curve on every chirp." Chased it.

**Bandpass: FIXED.** The BP tap was scaled by k = 1/Q (fit at Q 0 only), which
flattened the resonant peak; ReBirth's BP peak GROWS with Q like its lowpass peak
(manual p63 diagram; measured `renders/pcf-bp-2026-09-06`). Replaced with a
constant gain (kBpTrim 1.53). BP peak now matches RB to 0.3 dB across Q 0..96, full
curve 0.28-0.37 dB rms. The listening BP fixture went -17.8 dB -> -3.4 dB.

**The remaining gap is the DYNAMIC resonance during the fast envelope sweep, and it
is NOT the bandpass and NOT a nonlinearity.**

- Ruled OUT a static overload/saturator: RB's between-harmonic energy at high Q
  matches ours to +/-0.2 dB (drone Q 96 and Q 127), so ReBirth is not generating
  extra harmonic/intermodulation content statically. (A prior round removed the
  PCF `satState` on the same grounds; this confirms there is no static nonlinearity
  to restore.)
- The static filter matches ours vs RB at every Q (shape 0.3 dB, resonance peak,
  between-harmonic content). The difference is PURELY dynamic.
- Resonant-chirp measurement (`renders/pcf-chirp-2026-09-06`, broadband probe,
  high Q, big envelope sweep, pattern 0). Peak-center and spectral-centroid
  trajectory over each trigger, ours vs RB, on `lp_q100`:
    RB   centroid: 5259 6527 5409 3797 3138 2216 1541  965  816  (bright at the
                   attack, then a smooth descending curve = the flangy chirp)
    ours centroid: 3794 4613 4463 5017 3990 2574 1883 1180  848  (duller attack,
                   brightness peaks ~200 ms LATE, then descends)
  Same cutoff (both clamp open ~14.5 kHz for the first ~112 ms), same source, so
  the difference is the filter's DYNAMIC response to the snapped/swept cutoff: RB's
  resonance is bright and rides the sweep down as a coherent curve; ours lags and
  is duller. Present in LP AND BP, so it is the resonance, not the tap.

This is the SAME FAMILY as the 303 round-chirp (BASS303_FINDINGS / PLAN_DIODE_CHIRP):
an analog resonance that rings and rides during a fast sweep, which our zero-delay
TPT filter does not reproduce (it settles too cleanly). The exact per-frame
mechanism resists clean measurement (ReBirth non-determinism blocks time-aligned
analysis; the drone cannot resolve the moving corner), but the character is clear
and Luke hears it plainly.

DECISION (Luke, 2026-09-06): a FULL resonance rewrite, replacing the PCF's
zero-delay resonance with an analog-modelled core aimed at the ring/ride. On its
own branch (`exp/pcf-resonance`), behind a flag, A/B against ReBirth's chirp
trajectory as the target. Plan and progress in `docs/research/PLAN_PCF_RESONANCE.md`.

## 13. The swept-resonance investigation, CONCLUDED 2026-09-07: Amt-depth fixed, two residuals

The full investigation lives on `exp/pcf-resonance` (`PLAN_PCF_RESONANCE.md`);
this is the outcome banked to `pcf-supertask` on Luke's sign-off.

**APPLIED: envelope depth 0.95** (`kEnvDepth`, pcf.cpp). ReBirth's Amt opens the
cutoff about 5 percent less than a 1:1 map; the round-6 "1:1" had a real ~0.05
residual (peak param 0.953 at Amt 1.0, 0.836 at 0.882) it wrote off as a window
artifact. A proportional 0.95 matches the reliable high-Amt range. Also confirmed:
the bandpass fix (section 12) and the fixed-BP static filter are the shipped state.

**RESIDUAL A: swept-resonance brightness across mid/high cutoffs.** Luke's ear:
ReBirth is "more flangy, ring-modulator, a curve on every chirp". Every mechanism
was tested and none yields a clean fittable law: makeup (matches Q0/Q96 across
cutoff), cutoff-law top (matches at Q0 through 8 kHz), a marginally-stable
resonance-core REWRITE (Chamberlin; behaves identically to the TPT because the
ride is cutoff POSITION not resonance dynamics: ReBirth's cutoff sits in-band at
the attack, ours over-opens above it), a Freq-dependent envelope DEPTH (does not
converge, residuals 0.16-0.45 oct), and a constant envelope-cutoff OFFSET (fits
low cutoffs, but the Freq-70 patterns clamp and mid-high residuals stay high).
The over-opening on LOW-base big sweeps is real (a ~0.073 cparam offset, ~1.7x
cutoff) but has no single law that also holds the static Amt; it is left as a
documented residual rather than a risky blanket offset.

**RESIDUAL B: pattern level deficit vs trigger density.** On the 909 listening
patterns ours runs ~2-3 dB quieter, and the deficit CORRELATES WITH TRIGGER
DENSITY (pat00 one-trigger +0.96 dB, dense sixteenth/32nd pat05/34/40 -2.8 dB),
while pat05's centroid MATCHES ReBirth. So it is a retrigger/attack effect, not
the sweep. No clean lever found (retrigger-is-a-reset is measured to match RB).

CONCLUSION: bandpass fixed, envelope depth corrected to 0.95, static filter /
decay / patterns / makeup all at parity. The swept-resonance brightness and the
trigger-density level deficit are accepted, documented residuals (FINDINGS_AUDIT,
BACKLOG). PCF measurement-complete pending Luke's overall sign-off. The
`exp/pcf-resonance` branch keeps the full investigation and the falsified rewrite.

## 14. The cutoff LAW bends up above ~4.4 kHz, refit 2026-09-10 (APPLIED)

The log-linear cutoff law (fc = 9.01*2^(byte/127*10.653)) was fitted on the 303
drone, which cannot resolve a corner above its own harmonics (~8 kHz), so its top
(Freq bytes ~106..127) was extrapolation, flagged as such in the code. During the
swept-resonance co-model attempt (`exp/pcf-attack`, PLAN_PCF_ATTACK) a hypothesis
that ReBirth's cutoff tops out LOW (~9-10 kHz) was tested and FALSIFIED by direct
measurement, which incidentally showed the opposite: ReBirth's high-Freq cutoff
is MORE open than ours.

**Measured** (`fixtures/renders pcf-ceiling-2026-09-10`, `gen_pcf_ceiling.py`):
a broadband probe (crash+hat 16ths, kick on beat), STATIC (Amt 0), Q 0 lowpass,
dense Freq sweep. A broadband source resolves the rolloff corner to ~13 kHz (the
crash carries energy that far; SNR falls above ~14 kHz, so bytes 125/127 read
only as "open"). Same ref-normalised -3 dB corner estimator both sides. ReBirth
tracks the log-linear law to byte ~106 and then climbs faster:

| byte | law | ReBirth | old ours | new ours |
|---|---|---|---|---|
| 104 | 3809  | 3825  | 3733  | 3733 |
| 108 | 4807  | 4889  | 4733  | ~4830 |
| 112 | 6065  | 6308  | 6148  | 6148 |
| 116 | 7653  | 8285  | ~7520 | 8185 |
| 120 | 9657  | 11270 | 9507  | 11204 |
| 123 | 11497 | 15116 | 11343 | 14423 |
| 127 | 14508 | open  | 14368 | 19791 |

**APPLIED: a quadratic top-bend on the BASE Freq knob** (`kTopBendKnee` 0.831,
`kTopBendGain` 18.0; extra octaves = gain*(baseFreq-knee)^2 above the knee). Fit
to the reliable corners (byte 108..122) within ~1.2 percent, matching RB to ~2
percent measured. Exactly zero below the knee, so every Freq <= ~106 render is
byte-identical to the previous shipped law (verified). The old power-law and
exponential fits were rejected as boundary/degenerate; the quadratic knees in at
byte ~106 where the divergence actually begins.

**Why the bend is on the base Freq, NOT the summed cparam.** Only the static
Freq->cutoff law was measured (Amt 0). Applying the bend to the envelope-inflated
cparam instead measurably DARKENS the high-Amt patterns (pat05 centroid 1223 ->
991, 8 kHz octave 7.4 -> 6.1 percent, away from RB's 5875 / 27.9 percent): the
patterns' envelope drives cparam to the clamp, and bending the clamp higher
pushes the over-opened resonant peak further out of band. That is the swept-
resonance over-open residual (13, PLAN_PCF_ATTACK) being amplified, not a cutoff-
law effect. There is no evidence the envelope excursion shares the knob's top
curvature, so the bend corrects the measured (static) quantity only and leaves
the patterns and the resonant chirp byte-identical. The static cutoff top and the
dynamic over-open are the SAME cutoff seen two ways; fixing the static top does
not touch the dynamic residual and must not be allowed to worsen it.

RESIDUAL: ours still ~2 percent under RB across byte 96..122 (measurement scatter
plus the fit), and bytes 125/127 extrapolate the quadratic (both essentially open,
which matches RB's unmeasurable-but-open top). Low audible weight (Amt-0 static,
Freq > ~106). This corrects a real static parity gap without disturbing anything
measured elsewhere.

## 15. Clean-room probes P3 (resonance) and P4 (decay), 2026-09-10

Measured from ReBirth exports (`renders/pcf-probes-2026-09-10`,
`fixtures-2026-09-10/pcf-probes`, `tools/rbsgen/gen_pcf_probes.py`). Experiment
selection was informed by third-party binary analysis; every number here is from
the audio, and no analysis constant is used (see `docs/PROVENANCE.md`). These are
STRUCTURAL corrections to the shipped PCF, banked as measured facts; whether and
when to act on them is part of the PCF re-derivation decision, not done here.

### P3: the resonance control is QUANTISED to 8 bands of 16 bytes

Static (Amt 0) LP at Freq 64, Q swept across the suspected boundaries. Adjacent
bytes WITHIN a 16-wide band give an identical transfer (RMS difference 0.008 to
0.017 dB: 8 vs 15, 16 vs 24 vs 31, 48 vs 63, 112 vs 120 vs 127), and there is a
clean STEP at each boundary (15 to 16 = 1.63 dB, 31 to 32 = 1.04, 63 to 64 =
0.83, 96 to 112 = 1.68). Resonant-peak height per band: -1.0 / -0.6 / +0.1 /
+1.4 / +3.6 / +10.6 / +14.9 dB for bands centred at bytes 8 / 24 / 32 / 56 / 64 /
96 / 112. So the knob selects one of 8 discrete resonance settings, not a
continuous curve. The shipped model uses a continuous `1/Q = 0.1055 + 1.3378*
(1-r)^1.77` with saturation from byte 112; the saturation is really just the top
(112-127) band. Correcting this means 8 banded coefficient sets, not a continuous
law.

### P3: the filter is 2-pole (about 12 dB/octave)

Rolloff slope of a low-Q LP (q_008) above its corner: -11.0 / -12.0 / -11.8
dB/oct over 0.5-1 / 1-2 / 2-4 kHz. So the ORDER is two poles, which matches our
current SVF order; a re-derivation changes the resonator structure and the
banking, not the pole count.

### P4: the decay knob is 16 quantised bands, and the TOP band HOLDS (no decay)

Single-trigger sweep (Freq 30, Amt 110, Q 100, pattern 0, tempo 90), decay byte
swept, cutoff decay read as the centroid trajectory after each trigger. Adjacent
bytes in a byte>>3 band are identical (dec 112 = dec 119; dec 120 = dec 124 =
dec 127), re-confirming the 16-step quantisation by measurement including the top,
which the shipped table had only inferred. The TOP band (bytes 120-127) does NOT
decay: its centroid holds ~6500-7000 Hz across the whole 2.667 s bar, where dec
96 and 112 clearly fall. This FALSIFIES the shipped fitted top rate (0.04 ->
tau 2.215 s, which would darken by ~70 percent over that bar). The top decay band
is a hold. APPLIED 2026-09-11: `kDecayRate[15]` -> 0 (hold sentinel; decayTau
returns a tau far longer than any render so envDecay rounds to 1.0). Verified:
our dec_127 now holds flat across the bar like RB, dec_112 and below are
byte-identical (the change touches only byte>>3 == 15). The remaining dec_127
centroid gap (~4500 vs RB ~6500) is the separate, pre-existing swept-resonance
residual, not the decay.

The other P3 correction, the 8 quantised resonance bands, is NOT a quick fix: it
needs each band's coefficients fit to its measured transfer (a proper re-fit of
the resonance/gain/drift), so it belongs to the walled PCF re-derivation, not
this pass. Deferred.

## 16. The swept residual is resonance-during-sweep, NOT cutoff position (2026-09-11)

Measured from ReBirth exports (`renders/pcf-sweep-2026-09-11`,
`fixtures-2026-09-11/pcf-sweep`, `tools/rbsgen/gen_pcf_sweep.py`; clean-room,
experiment informed by analysis, no constant used). This corrects the
centroid-based framing used earlier this session (the FMAX ceiling and the
attack-slew models on `exp/pcf-attack`).

**RB's swept cutoff over-opens to the top, exactly like ours.** Tracking the
dominant resonant peak (not the energy centroid) on the big sweep, RB's peak
cutoff scales with Amt and base and SATURATES at the top: Amt 40/70/90/110/127 at
base 30 gives 517 / 2153 / 6891 / 14341 / 14341 Hz (saturating for Amt >= 110),
and base 10/30/50/70 at Amt 110 gives 6546 / 14341 / 14600 / 14987 Hz. So the
cutoff POSITION is not the residual, and the earlier "RB peaks in-band" was a
centroid artifact (the centroid peaks late when a resonant peak sweeps down from
the top). The whole position family, the FMAX cutoff ceiling and the finite-attack
slew, is ruled out by this direct peak measurement.

**What actually differs is the resonance as it rides down.** On psw_a110 (base 30,
Amt 110) RB's resonant peak descends cleanly and prominently from the top: peak
cutoff 14341 (17 ms) -> 11843 (40) -> 9733 (80) -> 6460 (150) -> 3316 (300), the
audible "chirp". Ours does not reproduce the high part of that descent: our tracked
resonant peak sits ~4400-5900 Hz and is erratic, never showing the 10-14 kHz
sweep, and is 4-8 dB less prominent mid-sweep (28-34 dB vs RB's 36-38). So our
resonance is weak / not prominent at high cutoff while it sweeps, where RB's stays
a strong descending peak. (The 6-17 ms points are transient/ring-up contaminated;
the 40-300 ms descent is the robust part.)

This puts the swept residual squarely on the RESONATOR: its resonance behaviour at
high cutoff during a fast sweep, the same family as the 303 round-chirp. It
reopens the resonator-topology question that the Chamberlin rewrite (exp/pcf-
resonance) closed on the now-corrected position framing.

Next, to split static from dynamic: measure RB's resonant-peak prominence at a
HIGH static cutoff and high Q (e.g. Freq 110, Q 112, Amt 0) with a broadband
source, versus ours. The drone could never resolve a peak above ~8 kHz, so whether
our resonance is already weak at high STATIC cutoff (fixable, static) or only
weak dynamically (the ring wall) is still unmeasured. That measurement should lead
the PCF resonator re-derivation.

## 17. The swept residual is a STATIC high-cutoff under-resonance (2026-09-11)

Measured (`renders/pcf-hires-2026-09-11`, `fixtures-2026-09-11/pcf-hires`,
`tools/rbsgen/gen_pcf_hires.py`; clean-room, broadband probe). This is the split
that section 16 called for, and it lands on the tractable side.

Static (Amt 0), Q 112, resonant-peak prominence (dB above the passband) versus
Freq, read with a broadband source so the peak is resolvable above 8 kHz (the
drone never could):

| Freq | 96 | 108 | 116 | 122 |
|---|---|---|---|---|
| ReBirth | 27.8 | 29.8 | 30.6 | 32.2 |
| ours    | 17.1 | 17.3 | 17.3 | 17.3 |

ReBirth's resonance GROWS more prominent with cutoff (28 -> 32 dB); ours is stuck
at ~17 dB. So our resonator is 10-15 dB UNDER-resonant at high cutoff and high Q,
and it is STATIC, not dynamic. The old "static filter matches to 0.3 dB" was
measured at Freq 64 and Q <= 96 with the drone; the high-Freq high-Q corner it
could not reach is wrong in ours by 10-15 dB.

This resolves the swept "flangy chirp" (16): our resonance is simply weak at high
cutoff, so when the envelope sweeps up there the descending resonant peak is faint
and does not sing, where ReBirth's stays sharp. It is NOT the intractable dynamic
ring wall the 303 round-chirp hit; it is a measurable static resonator defect. Our
TPT SVF loses resonance sharpness toward high fc (the `tan(pi*fc/sr)` prewarping);
ReBirth's resonator holds it.

**This is the target for the PCF resonator re-derivation** and it is tractable:
measure the resonant-peak prominence-versus-cutoff curve for each of the 8 Q bands
(broadband, static), then have the walled implementer fit a resonator whose peak
prominence matches those measured curves across the full cutoff range, high
cutoff included. A secondary note: at Freq 122 Q 112 our peak sits at 15.9 kHz vs
ReBirth's 12.5 kHz, a corner shift at high Q the Q0-fitted cutoff-top bend does
not cover; fold the Q-dependent corner into the same per-band fit.

## 18. The resonator surface and the re-derivation spec (2026-09-11)

The target for the resonator re-derivation is the measured (Q band x cutoff)
surface of resonant-peak prominence and corner: `renders/pcf-surface-2026-09-11`,
`fixtures-2026-09-11/pcf-surface`, `tools/rbsgen/gen_pcf_surface.py`, read by
`tools/measure/pcf_surface.py` (one method for ReBirth and ours). ReBirth's
prominence grows with cutoff (band 7: 27.8 -> 32.2 dB across Freq 96..122) while
ours is flat (~17 dB); the corner runs high in ours at Freq 116-122, Q-dependent.
The coefficient-free implementer brief is `docs/specs/SPEC_PCF_RESONATOR.md`: a 2-pole
resonator, 8 quantised Q bands, fit so per-band prominence and corner match the
surface across the full cutoff range, without regressing the shipped low/mid
static, cutoff law, decay or BP. Walled implementation next.

## 19. Even-band confirmation of the resonator re-derivation (2026-09-11)

The resonator re-derivation (branch `pcf-resonator-cleanroom`) fit the odd bands
1/3/5/7 to the measured surface (18) and INTERPOLATED the even bands 0/2/4/6. This
grid measures the even bands directly (`renders/pcf-surface-even-2026-09-11`,
`fixtures-2026-09-11/pcf-surface-even`, `gen_pcf_surface_even.py`; broadband
static, passband-100-250 normalisation so Freq 64 is resolvable) and checks the
new resonator against them.

Prominence, ReBirth vs new (interpolated), |diff| dB:
- band 0 (byte 8): <= 0.3 across all Freq (near-zero resonance).
- band 2 (byte 40): <= 0.8 (Freq 96+ <= 0.2).
- band 4 (byte 72): <= 1.3 (Freq 96+ <= 0.4).
- band 6 (byte 104): <= 2.0 (Freq 96+ 0.7-0.8; the 2.0 is at Freq 64, low-cutoff).

Corner: bands 2/4/6 within 0-3 percent. Band 0's corner metric is meaningless (no
resonant peak at Q 0-15; the analyzer picks noise) and its true corner follows the
matched cutoff top-bend.

So the interpolation holds within tolerance: the resonator now matches ReBirth's
prominence across ALL EIGHT bands (odd measured to ~0.1 dB in 18, even confirmed
to <= 2 dB here) and corner within ~3 percent. Remaining nits, both minor and
documented: band 6 at Freq 64 runs +2 dB (low cutoff, at the measurement limit,
where the 8-band quantisation shifts the shipped continuous law by up to ~2.5 dB),
and the swept peak's position/over-open wobble is the separate residual of 16.
Role A verdict: the re-derivation is validated across the surface; ready for Luke's
sign-off and merge. It is flag-gated (REBORN_PCF_RESO, default new; off is
byte-identical to shipped).

## 20. Ear verdict on the resonator: prominence fixed, swept trajectory remains (2026-09-11)

Ear check (`listen-pcf-resonator-2026-09-11`; pat16 F70Q90A90 the clearest, null
pair calibrated to -0.13 dB). The re-derived resonator (prominence) is APPROACHING
but does NOT close. Luke's ear: ours' chirp resonant harmonic is off by ~a minor
third from ReBirth's, and ours WAVERS where ReBirth sustains the chirp.

Measured (resonant peak-freq trajectory over the listen snippets): ours' MEDIAN
swept peak is +3.6 semitones (pat16) / +3.9 (chirp) above ReBirth, improved from
the old resonator (+5.5 st on pat16) but still ~a minor third off. The magnitude
matches Luke's ear. Direction nuance: the median runs HIGHER in ours while the ear
placed ours LOWER; consistent with a two-phase mismatch (ours over-opens brighter
and ~40 ms longer at the attack, raising the median, while the sustained tail the
ear tracks settles to a different pitch) rather than one clean offset. Prominence
itself now matches (pat16 32.8 vs 31.8 dB).

So the resonator did its job (prominence) and the remaining audible gap is the
swept cutoff TRAJECTORY: the reach/over-open plateau and its wobble, i.e. the
residual of section 16, now characterised by ear as "pitch ~a minor third off and
wavers vs sustains". That is the next target; it is the envelope/cutoff-reach
dynamics (the 303-family), not the resonance the resonator addressed.

## 21. The swept trajectory residual is the cutoff CLAMP; ReBirth's envelope-to-cutoff is headroom-multiplicative (2026-09-13)

The resonator (20) fixed the resonant-peak PROMINENCE but not the swept cutoff
TRAJECTORY. Measured 2026-09-13 from the existing swept fixtures
(`renders/pcf-sweep-2026-09-11`, `gen_pcf_sweep.py`: pattern 0 one-trigger-per-bar
at tempo 90, so each bar is a single clean envelope event), re-tracked with the
new resonator on (REBORN_PCF_RESO), by `tools/measure/pcf_trajectory.py` (one
method both sides). Clean-room: the finding is the measured peak-cutoff trajectory;
no constant from any binary analysis is used.

**The mechanism is our hard clamp.** Our cutoff parameter is
`cparam = clamp(base + 0.95*amt*env, 0, 1)`, hard-clamped at 1.0. On a high-Amt or
high-base sweep the sum exceeds 1.0 at the trigger, so `fc` PINS at the ceiling
(~15.7 kHz) and HOLDS until `env` decays back under the clamp. The plateau length is
exactly `tau*ln(0.95*amt*vel/(1-base))`: predicted 91 ms (a110), 220 ms (a127),
562 ms i.e. the whole tail (f070), matching the tracked peak-freq to the frame.
ReBirth NEVER plateaus: its peak reaches the top by 17 ms and descends immediately
and monotonically (a110: 14341 -> 13049 -> 9733 -> 6675 -> 3316 Hz over 17-300 ms;
ours pins 15762/15762/15762 through 80 ms then drops). The plateau, and the loss of
the descending resonant chirp while the filter sits wide open, IS the ear's "wavers
where ReBirth sustains" from section 20.

**ReBirth's law is headroom-multiplicative.** Converting RB's peak-cutoff back to
the parameter domain, each fixture's descent is LINEAR in `env` and UNCLAMPED,
`cparam = base + g*env`, with `g` depending on base: g = 0.740 at base 30, g = 0.435
at base 70. The ratio `g/(1-base)` is IDENTICAL, 0.969 both. So RB's envelope drives
the cutoff a fixed fraction of the REMAINING HEADROOM (1-base) toward the ceiling:

    cparam = base + D * Asat(amt) * env * (1 - base),   D ~= 0.97

This asymptotically approaches 1.0 and never clamps, so it descends the instant
`env` does (no plateau), and it COMPRESSES at high base (less headroom), which
matches the base sweep. The Amt knob SATURATES: `psw_a110` and `psw_a127` are the
same trajectory in RB (Asat(byte 110) = Asat(byte 127)), saturating by ~byte 108.
Far from the ceiling the law reduces to about the current additive map (a090:
~0.67*env either way), so it fixes the clamp cases without disturbing the low/mid
ones that already match (logerr 0.10 at Amt <= 90; the clamp only bites at Amt >~99
for base 30, exactly where a090 turns into a110).

The headroom law with the two clean anchors tracks RB across all four clamp
fixtures (a110/a127/f050/f070); a global fit over all eight independently recovers
D ~ 0.97 and the Amt saturation. The global RMS is dragged by the low-Amt fixtures,
where the peak tracker floors out (their true peak descends below the tracker's
1.5 kHz band), but the residual does not live there. The clean coefficient fit
belongs to the walled implementer, against these same fixtures.

This is a change to the envelope->cutoff MAPPING only. The AD envelope (instant
attack to velocity, exponential decay), the measured decay law (the 1-2-3-4-5-7
quantised table), the cutoff law incl. the top-bend, and the resonator are all
unchanged. The coefficient-free brief is `docs/specs/SPEC_PCF_ENVELOPE.md`.

### Implemented 2026-09-13 (branch pcf-envelope-cleanroom)

`pcf.cpp` now applies `cparam = base + D*Asat(amt)*env*(1-base)` with `D = 0.988`
and `Asat = min(1.259*amt, 1)`, fit against these same fixtures with
`pcf_trajectory.py` (one method both sides). D is the headroom fraction at Amt
saturation, read off a110/f050/f070 by inverting RB's tracked peaks through our
own filter's peak-vs-cparam map (that map was calibrated on the non-clamping
a070/a090/f010 renders, whose cparam we know exactly, so the tracker-below-fc
offset divides out); the three bases gave 0.987/0.993/0.998, central 0.988. The
Asat slope 1.259 is not free: `D*1.259*(1-30/127) = 0.950` reproduces the old
additive depth at the fixtures' base Freq 30 to a fraction of a percent, so the
low/mid patterns do not move. The old law is behind `REBORN_PCF_ENVLAW=old`.

Verified (peak cutoff Hz, ours new law vs RB, worst error over 17/40/80/150/300 ms):
a110 +7.1%, a127 +7.1% (a127 trajectory byte-identical to a110, i.e. Amt
saturation reproduced), f050 -2.8%, f070 +1.7%; all four descend immediately with
NO plateau (was 15719 held through 80-300 ms). Low/mid a040/a070/a090 peak-
identical before/after. Resonator surface byte-identical to the old-law render
(all 21 srf_* fixtures are Amt 0, so the mapping is inert) and still matches RB
(band 7 prominence 27.8 -> 32.2, corner within 2%). ctest green; psw_ref (PCF
routed nowhere) byte-identical before/after.

CAVEAT, base 10. `psw_f010` (base Freq 10, Amt 110) is NOT captured by the
headroom law and was outside the base-30/50/70 fit and the acceptance set: RB
reaches only ~6.3 kHz at 17 ms (consistent with section 16's base sweep,
6546 Hz at base 10) where the headroom law, having MORE headroom at low base,
reaches ~14.9 kHz. So RB's envelope reach FALLS at very low base rather than
rising with headroom, a non-monotonic base dependence `base + D*Asat*env*(1-base)`
cannot express; the old additive law was coincidentally closer there (~7.6 kHz).
This is a real residual at the bottom of the Freq range, left unmodelled because
closing it needs a different base law than this one-expression brief allows and
would risk the four required fixtures. Not called final.

## 22. The envelope base law, measured by a dense base sweep: it is DEPTH-CAPPED, not headroom (2026-09-13)

The headroom law (21) fit base 30/50/70 but over-opened at very low base (the base
10 caveat above). A dense base sweep resolves the true law
(`renders/pcf-envsweep-2026-09-13`, `fixtures-2026-09-13/pcf-envsweep`,
`tools/rbsgen/gen_pcf_envsweep.py`; base Freq 5..110 at Amt 110, plus an Amt sweep
at base 15; broadband probe, one-trigger-per-bar, tempo 90, decay 96; read by
`tools/measure/pcf_trajectory.py`, one method both sides). Clean-room; the finding
is the measured peak-cutoff trajectory.

**The envelope's contribution to the cutoff parameter is depth-capped, not
headroom-scaled.** Fitting `cparam = base + g*env` per base (from the descent, the
transient-robust part), the depth `g` at Amt 110 is roughly FLAT at ~0.78 for base
<= ~25 and then falls as `D*(1-base)` for base >= ~30. A single law collapses the
whole base sweep (base 5..80) to RMS 0.008 in cparam:

    cparam = base + g * env,   g = min( gcap(amt), D * (1 - base) )

with `D ~ 0.97` and, at Amt 110, `gcap ~ 0.79`. There is NO hard clamp: at low base
`g` is gcap-limited (additive, so the reach does not over-open, which is what the
headroom law got wrong), and at high base `g` is headroom-limited `D*(1-base)`, so
`base + g*env < 1` always and the cutoff descends immediately from its peak (no
plateau) everywhere. Measured reach vs base at Amt 110, 40 ms after the trigger:
RB 5.6/7.3/9.7/13.0/13.1/13.1/14.5 kHz at base 10/15/20/25/40/50/64; the depth-cap
model matches within a few percent at every base and time, where the headroom law
sat flat at ~12-13 kHz across all base.

**The Amt knob does NOT saturate.** At base 15 (headroom to spare) the reach keeps
growing with Amt: Amt 110 -> 8.2 kHz, Amt 127 -> 14.3 kHz. The base-30 `a110 == a127`
of section 21 was the HEADROOM LIMIT (`min` picks `D*(1-base)` for both), not an Amt
saturation; `gcap(amt)` keeps rising through 127, mildly compressive at the top
(descent depth ~0.67/0.79/0.85 at Amt 90/110/127). This retires the "Amt saturates
by byte 108" reading, which was an artifact of measuring only at base 30.

The corrected coefficient-free brief is `docs/specs/SPEC_PCF_ENVELOPE.md` (rewritten for
the depth-cap law). Still a change to the one `cparam` expression; the AD envelope,
decay law, cutoff law and resonator are unchanged. The first (headroom) implementation
on this branch is to be REFIT to the depth-cap law.

### Implemented 2026-09-13 (branch pcf-envelope-cleanroom, refit of section 21)

`pcf.cpp` now applies `cparam = base + g*env`, `g = min(gcap(amt), D*(1-base))`, no
clamp. Fit against the dense base sweep and the original Amt sweep with
`pcf_trajectory.py` (one method both sides), inverting RB's tracked peaks through
our own filter's peak-vs-cparam map:

- `D = 0.99`, from the base sweep at base >= 30 (`g/(1-base)` back-out 0.98 to 1.00
  across b030/b050/b064/b080/f050/f070; there is a slight rising trend with base
  that a single D cannot fully capture, so D is a compromise).
- `gcap(amt) = 0.95*amt` at/below Amt 90 (the shipped depth, so the `min` is a
  no-op at base 30 for Amt <= 90 and the low/mid renders are BYTE-identical), then a
  line of slope 0.773 (in amt 0..1) above the Amt-90 knee, hitting ~0.795 at Amt 110
  (the measured flat low-base cap `g`). It keeps rising through 127 (no saturation).

Verified (peak cutoff Hz, ours vs RB, one analyzer both sides):

- Base sweep 5..80 (Amt 110): within ~8.5% at every base and time, no plateau at any
  base. Low base no longer over-opens: b010 6417 vs RB 6288 (headroom law gave 14944),
  b015 8269 vs RB 8183, b020 11499 vs RB 11240. Worst error +7.5% is at the 17 ms
  near-ceiling transient of b025..b040 (40 ms onward within ~5%).
- Clamp fixtures: a110 +7.5% worst, a127 byte-identical trajectory to a110 (the
  headroom limit, not Amt saturation), f050 +5.3% worst, f070 +2.0% worst. All within
  15%, all descend immediately.
- Low-base Amt sweep (base 15): reach grows 3445 -> 8269 -> 14729 at Amt 90/110/127,
  no saturation.
- Low/mid a040/a070/a090 byte-identical to the shipped baseline. Resonator surface
  byte-identical (Amt 0 makes the mapping inert) and still matches RB (band 7
  prominence 27.8 -> 32.2). ctest green; `psw_ref` (non-PCF) byte-identical.

CAVEATS. (1) The base-15 Amt-90 point runs +17.6% high vs RB (ours 3445 vs 2929):
`gcap(90)` is pinned to the shipped `0.95*90/127 = 0.673` to keep `psw_a090`
byte-identical, where RB's own `gcap(90)` is ~0.649. This is the pre-existing
`0.95*amt`-vs-RB mismatch at Amt 90, not a new regression, and the base-15 sweep
point is only a qualitative (grows / no-saturation) acceptance, which passes. (2)
b100/b110 (base > 80) are outside the acceptance range; b110 in particular reads a
flat ~8.8 kHz in RB that neither law reproduces, unexplained and left for later. (3)
The 17 ms +7.5% over-reach at mid base near the ceiling is a ~0.003 cparam
difference amplified by the exponential cutoff; it sits in the transient window the
analyzer flags as least reliable. Not called final.

## 23. The pattern character residual is a COMPRESSIVE VELOCITY curve (2026-09-14)

Luke's ear on the depth-cap (2026-09-13, listen-pcf-envelope): the pat16 harmonic
PITCH is correct now, but the pattern renders as "a fast series of mini-sweep hits"
where ReBirth is "one pendulous wide sweep." Measured with a clean pattern-motion
probe (`renders/pcf-patternmotion-2026-09-14`, `fixtures-2026-09-14/pcf-patternmotion`,
`gen_pcf_patternmotion.py`): the listen patterns at F70 Q90 A90 decay 64 tempo 130,
but driven by the CONTINUOUS broadband source (crash+hat+kick every 16th) instead
of the sparse house groove, so the cutoff motion is trackable over the whole bar.
Clean-room; the finding is the measured peak-cutoff trajectory. One method both sides.

**The single-trigger attack is INSTANT, not a glide** (ruled out). Fine-time tracking
of the existing sweeps shows RB at the ceiling by 3 ms and immediately descending.

**The defect is velocity-specific.** Peak cutoff reached, RB vs ours (depth-cap):
- pat0 (single trigger, velocity 1.025): match (~14-17 kHz both).
- pat5 (dense, velocity 1.025): match (blue sits on green in the motion plot).
- pat16 (dense, velocity 0.652): RB ~7.5 kHz, ours ~4.9 kHz. Ours too low.

So at FULL velocity ours matches RB; at LOW velocity ours falls short. Converting RB's
peak reach to the envelope level env=(cparam-base)/g (g=0.436, the depth-cap g at
F70 A90, validated at full velocity by pat5), per pattern peak velocity:

| velocity | RB env | env/velocity |
|---|---|---|
| 1.025 (pat0/2/5) | ~1.0 | ~0.97 |
| 0.929 (pat19)    | 0.911 | 0.98 |
| 0.652 (pat16)    | 0.796 | 1.22 |

ReBirth applies a COMPRESSIVE velocity->envelope curve: the quiet pattern steps are
boosted (env 0.80 at velocity 0.65, a ~1.2x lift) while full-velocity steps are not.
Our model takes velocity linearly (env = velocity, per section 11's "retrigger
replaces the level"), so the low-velocity steps stay small and the sweep breaks into
mini-hits instead of ReBirth's continuous arc. This is consistent with section 11
(the retrigger still REPLACES, just through the curve f: env = f(velocity), f(1.0)
approx 1.0 so the full-velocity sweeps and single-trigger measurements are unchanged)
and with the decay law (unchanged). It is the temporal character Luke hears.

FIX: apply a compressive f(velocity) where the pattern sets the envelope level, fit
so pat16 reaches RB's ~7.5 kHz without moving pat5/pat0 (full velocity) or the
single-trigger sweeps. Keep the depth-cap reach and the decay. The precise f is the
walled implementer's fit against the pattern-motion renders (pat19 gives ten
near-isolated velocity points from 0.29 to 0.93). Spec: docs/specs/SPEC_PCF_ENVELOPE.md.

### Implemented 2026-09-14 (branch pcf-envelope-cleanroom)

`pcf.cpp` now sets the trigger level through `env_ = f(velocity)`,
`f(v) = 1.025 * (v/1.025)^0.33` (a concave power curve anchored at the preset full
velocity 1.025). The anchor makes `f(1.025) = 1.025` EXACTLY, so every
full-velocity trigger is byte-identical and the entire base sweep, the clamp
fixtures, the low/mid set and the resonator surface (all pattern 0, velocity 1.025)
are byte-identical to the pre-curve depth-cap renders (verified with `cmp`). The
curve lifts only the sub-full-velocity steps. `gamma = 0.33` was fit so pat16
(velocity 0.652) reaches ReBirth's peak; a per-trigger tracker on pat19 (folding
each detected trigger onto its pattern step) supplied the velocity sweep.

Verified (peak cutoff Hz, one method both sides):
- pat16 (velocity 0.652): peak 6761 vs RB 7278 (-7.1%, within 15%), a continuous
  wide arc where before the curve it was 3704 and choppy. Full trajectory
  6761/4953/3273/5426/3144 vs RB 7278/6761/4350/4953/2196 (the 40 ms point runs
  low, -27%, because our first-trigger decay drops faster than RB's; the peak, the
  named acceptance, passes).
- pat0/pat5 (velocity 1.025): byte-identical to pre-curve, still matching RB
  (pat5 10680 == RB 10680; pat0 11542 vs RB 10680, the pre-existing +8% full-
  velocity over-reach of the depth-cap, unchanged by and out of scope for this curve).
- pat19 per-trigger, ours vs RB: v0.451 5491/4500, v0.466 5620/4522, v0.607
  7127/8656, v0.929 10164/10680. Monotonic and a large improvement over the linear
  map (v0.607 was 3402). It tracks the rise but is not within 15% at every point:
  ours over-lifts the lowest velocities (+22% at v0.451) and under-lifts the mid
  (-18% at v0.607), because ReBirth's curve is more S-shaped (a gentler toe, a
  steeper middle) than a power law. RB's per-trigger metric saturates at ~10680
  (env ~1.0) for velocity >= ~0.68, so the mid/high shape cannot be pinned from this
  probe; the power law is the simplest defensible compressive fit to what resolves.
- ctest green; base sweep, clamp fixtures, low/mid and resonator surface byte-identical.

CAVEATS. (1) The pat19 shape residual above: a 1-parameter power law cannot match
ReBirth's S-shaped toe; the low velocities are lifted ~20% high and the mid ~18%
low. A 2-parameter (sigmoid / Naka-Rushton) curve would track pat19 tighter but is
not warranted by this probe's saturated, noisy data and would add a second free
parameter; left open. (2) f(1.025) is set to identity to keep the prior fixtures
byte-identical; ReBirth's env at velocity 1.025 reads ~1.0, but the metric cannot
distinguish 1.0 from 1.025 at the ceiling, and byte-identity of the prior acceptance
is the firmer requirement. (3) pat16's 40 ms tail runs low; the peak passes. Not
called final.

## 24. The pattern envelope needs an ATTACK RAMP: ours jumps, ReBirth glides (2026-09-14)

After the velocity curve fixed the reach, Luke's ear + a spectrogram: on pat16 ours
is a SAWTOOTH (each trigger jumps the cutoff instantly to the top, then a straight
diagonal descent, then resets) where ReBirth is a WAVE (a "longer filter wave across
the snare hit", pendulous). Measured (fine-time cutoff(t) on `pmo_p16`, continuous
source, window 512 hop 32): at every trigger OURS jumps vertically (instant attack,
env_ set to f(velocity) in one sample) while ReBirth RAMPS up over ~30-40 ms into a
rounded top. The rise is a one-pole approach (fast then easing), time constant ~10 ms.
This is the sawtooth-vs-wave: our instant attack gives the vertical leading edge; the
ramp rounds it into ReBirth's wave.

This reconciles the earlier "attack is instant" read (section 23): that was the
high-Amt sweeps CLIPPING the ceiling within the ramp, not a true instant attack. The
sub-ceiling cases (a090 peaked ~11 ms; pmo_p16 rises over ~30 ms) show the ramp.

FIX: apply a short ATTACK GLIDE so the envelope eases to the trigger target instead
of jumping (a one-pole toward the target, time constant fit to RB's pmo_p16 rise,
~10 ms). It affects only the RISE (first ~30 ms after a trigger), so the descent-based
reach law (depth-cap), the decay, and the velocity curve are unaffected in the
descent. Bonus: the ramp CAPS how high fast triggers reach before the decay pulls
them down, which should pull back the ~20% brightness over-lift of the velocity curve
(FINDINGS 23) at the same time. Keep the existing knob de-zippering and the retrigger
rules. Spec: docs/specs/SPEC_PCF_ENVELOPE.md.

### Implemented 2026-09-14 (branch pcf-envelope-cleanroom)

`pcf.cpp` now carries a separate envelope TARGET (`envTgt_`) and LEVEL (`env_`). A
trigger sets the target to `f(velocity)` (retrigger REPLACES, lower trigger pulls
down); the target decays under the measured law; the level glides to it with a
one-pole attack `attackSlew_` of time constant `kAttackTau = 8 ms`. The level meets
the decaying target near the peak and then tracks it down, so the DESCENT is the
measured decay and only the RISE is rounded. `REBORN_PCF_ENVLAW=old` keeps the
shipped instant jump (byte-identical to shipped, verified).

`kAttackTau = 8 ms` was fit to RB's pmo_p16 rise (fine-time, window 512 hop 32, one
method both sides). `kVelGamma` was refit 0.33 -> 0.24: the ramp's rounding caps the
fast-trigger peak, so a little more velocity lift is needed to keep pat16 on RB (the
spec anticipated this). Gamma only affects sub-full-velocity steps; the pattern-0
fixtures (base sweep, clamps, low/mid) use velocity 1.025 -> f = 1.025 and are
unaffected by the gamma change.

Verified:
- pmo_p16 rise (fine-time): 4048 -> 7149 Hz over ~16 ms into a rounded top, where
  before it jumped to ~4600 in one frame and then descended diagonally (a sawtooth).
  It now reads as a wave. Peak 7149 vs RB's rounded top ~7500 (-4.7%; vs RB's single
  40 ms spike 7924, -9.8%). Within 15%. (The coarse pcf_trajectory 1024-window smears
  this fast transient and under-reads it, ~4500; fine-time is the right tool here, as
  the spec directs.) Note ours was NOT over-lifted before the ramp (it was ~4600, a
  sawtooth low), so the ramp brought the fine-time peak UP to RB, not down as the spec
  framing expected.
- pat0/pat5/pat2 (full velocity) now ramp on the attack too (expected); their descent
  and peak still match RB (p05 within ~12%, p00 within ~14% at the 17 ms rise point).
- Base sweep and named clamp fixtures (a110/f050/f070): the DESCENT (40-300 ms) still
  matches RB within ~7-9% (a110 -5.6%, f050 -6.8%, f070 -1.2% worst in the descent).
  The peak cap lowers the full-velocity descent ~5% vs the pre-ramp instant attack,
  which moves it slightly closer to RB, not away.
- pat19 tracks RB across velocity (monotonic, similar to the pre-ramp velocity-curve
  result; the ramp's cap is near-uniform across its triggers).
- Surface byte-identical (Amt 0, no triggers); non-PCF byte-identical; ctest green.

CAVEATS. (1) The 17 ms RISE point under-reads RB by up to ~16% on the two
highest-base fixtures (b080, f070): RB reaches its near-ceiling peak by 17 ms (a fast
rise) while pat16 rises over ~40 ms (a slow rise), so RB's effective attack looks
LEVEL-dependent, which a single env-domain time constant cannot capture. 8 ms is the
compromise that keeps pat16 a clear wave while holding every fixture's DESCENT within
tolerance; the high-base 17 ms rise is the cost. A level-dependent attack would close
it but is beyond a one-expression change and unmeasured in shape. (2) The very-low-base
fixtures (b005/b010) read ~15-20% low at 80 ms in the coarse pcf_trajectory, partly
because the gentler ramped onset shifts that tool's surge-based trigger detection later
on weak-surge low-base fixtures; the decay law itself is untouched. Not called final.

## 25. Attack-then-decay completed and measured: fixes pmo_p05, trades pmo_p16 (2026-09-16)

Section 24's target-decays-from-the-trigger-instant model under-reached on dense
patterns (`pmo_p05` peak 14 kHz -> 10.9 kHz). `docs/specs/SPEC_PCF_ENVELOPE.md` addendum
2026-09-14 (3) specified the fix: on a trigger the target HOLDS at `f(velocity)`
while the level glides up to it; once the level reaches the held target (within
`kReachEps`, a fraction of the target), the target starts to decay under the
measured law and the level tracks it down. The code for this (an `attacking_`
flag) was written but left uncommitted and did not compile: `attacking_` and
`kReachEps` were used but never declared, and `fireTick()` never set
`attacking_ = true`, so the attack phase could never re-arm after the first
trigger. Completed 2026-09-16: declared `bool attacking_ = false;` next to
`env_`/`envTgt_`, declared `kReachEps`, and set `attacking_ = true` in
`fireTick()`'s new-law branch on every trigger.

**`kReachEps` had to move from the spec's seed value.** At the seed `0.02` (hold
time `-tau*ln(eps) ~ 31 ms` before decay is allowed to start), `pmo_p05` matched
RB closely (+5.2%) and `pmo_p19` was the closest of any value tried (-13.3%), but
the base-sweep and clamp fixtures, which were matching RB within ~7-9% under
section 24's model, now overshot badly: `esw_b030` +26.8% at 80 ms, `psw_a110`
+26.8%, several base points in the teens-to-20s percent. The mechanism: holding
the target undecayed for the ~31 ms it takes the level to reach it eats a
material fraction of decay tau (177 ms at decay 64) out of every subsequent
inter-trigger gap, so patterns and even single-trigger sweeps read brighter than
RB at 40-150 ms after the trigger. This is a genuine cost of the
attack-THEN-decay model, not a bug in the completion: the model directly
trades "reach the peak" against "start decaying on time," and a small
`kReachEps` buys the first at the expense of the second.

Swept `kReachEps` from 0.02 to 0.5 (hold time 31 ms down to 5.5 ms) and measured
three families every time: the `pmo_p05/p16/p19` p95 peak metric (per-frame
ratio = |signal spectrum| / |time-averaged ref spectrum|, window 1024 hop 64,
argmax in f>1500 Hz, p95 over the whole 8 s render), the base-sweep/clamp
`pcf_trajectory.py` descent (window 1024 hop 128, trigger-locked, 17/40/80/150/
300 ms), and byte-identity of the `REBORN_PCF_ENVLAW=old` path and non-PCF
renders. Larger `kReachEps` moved every one of `pmo_p05`, `pmo_p16`, `pmo_p19`
and the base-sweep/clamp points DOWN (less hold time, more decay time, darker),
which fixes the base-sweep/clamp overshoot and `pmo_p16`'s overshoot in the same
direction, but pulls `pmo_p05` back toward the very regression this fix exists
to correct (at `kReachEps = 0.5`, `pmo_p05` reads 10896 Hz, i.e. back to
"the bug") and pulls `pmo_p19` further from RB at every value tried. There is no
single `kReachEps` that satisfies the base-sweep/clamp regression requirement
(explicitly "must still hold"), `pmo_p05`'s "must return to ~14 kHz", AND
`pmo_p16`'s "within ~15%" simultaneously; the last two pull in opposite
directions on this one knob.

**Settled on `kReachEps = 0.13`**, chosen to bring the base-sweep and the four
named clamp fixtures (`psw_a110/a127/f050/f070`) back within the ~15% regression
tolerance (worst 13.5%, at `esw_b030`/`psw_a110` 80 ms) while keeping `pmo_p05`
solidly improved and within tolerance. This prioritises the "must still hold"
regression requirement over the two softer addendum-3 acceptance numbers, per
the instruction to iterate on `kReachEps` and say what was changed.

Measured (p95 peak, ours vs RB, `pcf_p95_peak.py`, one method both sides):

| pattern | before (section 24 bug) | ours (eps=0.13) | RB | ours vs RB |
|---|---|---|---|---|
| pmo_p05 (dense, v1.025) | ~10.9 kHz | 12662 Hz | 13997 Hz | -9.5% (within 15%) |
| pmo_p16 (dense, v0.652 desc.) | ~7.6 kHz | 8829 Hz | 7364 Hz | +19.9% (OUT, was +2.7% by accident pre-fix) |
| pmo_p19 (isolated, v0.29-0.93) | ~6.7 kHz | 8786 Hz | 10680 Hz | -17.7% (OUT, but much closer than -37% pre-fix) |

Base sweep (`esw_b005..b110` vs RB, `pcf_trajectory.py`, 17-300 ms): within ~15%
at every point for `b010` through `b100`; `esw_b005` misses at 300 ms only
(-27.5%, a pre-existing low-base trigger-detection artifact documented in
section 24's caveat 2, not newly introduced); `esw_b110` misses across 17-150 ms
(+15-18%), the one base point outside the law's fitted domain (section 22 fit
base 5..80 at Amt 110), so some of that residual likely predates this fix and
some is this fix's general brightening; not separated. Named clamp fixtures
`psw_a110/a127/f050/f070`: all four within tolerance, worst 13.5%.

Rise shape confirmed unchanged: fine-time (window 512 hop 32) on `pmo_p16` still
shows 6.5-10 ms multi-step ramps into each local peak on both sides, a wave, not
a vertical jump, at `kReachEps = 0.13` same as at 0.02.

Byte-identity confirmed against the pre-completion commit (`eea4ff9`, built in
an isolated worktree): `REBORN_PCF_ENVLAW=old` renders byte-identical (`cmp`);
a PCF-routed-nowhere fixture byte-identical; a static resonator-surface fixture
(no triggers) byte-identical. `ctest` 100% green throughout.

CAVEAT, not closed. `pmo_p16`'s p95 peak sits 19.9% over RB even at the tuned
`kReachEps`, and `pmo_p19` sits 17.7% under; both are outside the stated ~15%.
The likely cause is that `kVelGamma = 0.24` (section 23/24) was fit against the
UNDER-reaching section-24 model, where part of its compressive lift was silently
absorbed by the reach shortfall; now that the reach is fixed, the same gamma
over-lifts patterns whose triggers sit close together at moderate-to-low
velocity (`pmo_p16`'s four triggers are 115 ms apart against a 177 ms decay
tau, and its compressed targets all land at 0.78-0.92, near the ceiling).
Refitting `kVelGamma` under the corrected attack-then-decay model is the
likely next lever, but it is out of scope for this minimal completion (the task
was declaration + wiring + `kReachEps`, not a re-derivation of the velocity
curve) and is left for Luke's call. Quantified, not overstated: the wave shape
and the primary regression set (base sweep, clamp, byte-identity) hold; two of
three named pattern-motion targets are substantially improved (`pmo_p05` now
within tolerance, `pmo_p19` much closer though still outside it); `pmo_p16`
alone is worse in this specific p95 statistic than its accidental pre-fix
match, though it remains audibly and measurably a wave rather than a
sawtooth.

## 26. The velocity curve must be S-SHAPED: quiet steps return the filter LOW (2026-09-16)

After the attack ramp + held-peak, Luke's ear: the sweep is better but ReBirth's
pat16 has a "toy/compacted, click-and-clack" character under the chirp, "the filter
starting low and returning with every beat": the quiet pattern steps drop the
cutoff nearly shut (a percussive clack), which ours misses. Measured (pmo_p16,
continuous source, resonant peak percentiles): trough / median / peak, RB
1378 / 3273 / 6546 Hz vs ours 3015 / 4910 / 8096 Hz. Ours sits HIGHER across the
board and its dynamic range is COMPRESSED (2.7x vs RB's 4.7x). RB swings from nearly
closed (1.4 kHz) up to the chirp and back; ours never returns below ~3 kHz.

Cause: the compressive velocity curve `f(v) = 1.025*(v/1.025)^0.24` (a power law,
FINDINGS 23/section-24 addendum) OVER-BOOSTS the low velocities. It lifts velocity
0.34 to f=0.785 where ReBirth's env at the trough implies f(0.34) ~ 0.53, so the
quiet pattern steps keep the cutoff half-open where ReBirth drops it low. A power law
has too much lift at the toe. ReBirth's velocity->env curve is S-SHAPED: a gentle toe
(quiet steps stay quiet, so they close the filter = the clack) with the boost only in
the middle, converging to ~1 at full velocity.

The gamma=0.24 was fit against the earlier under-reaching model and its lift was
partly compensating for that bug; with the reach now correct the same power law both
over-lifts the peak (+20%, FINDINGS 25) AND fills in the trough that should return
low. One S-curve fixes both.

FIX: replace the power law with an S-shaped f(velocity) (gentle toe, mid boost,
f(1.025)=1.025), fit so ours' pat16 AND pat19 cutoff(t) trajectory matches RB's
trough/median/peak (the wide dynamic range). Targets, pmo_p16: trough ~1.4 kHz,
median ~3.3 kHz, peak ~6.5 kHz. Keep the attack ramp, held-peak, depth-cap reach and
decay. Spec: docs/specs/SPEC_PCF_ENVELOPE.md addendum (4).

### Implemented 2026-09-16 (branch pcf-envelope-cleanroom)

Replaced the power law with a Naka-Rushton / Hill curve, `h(v) = v^n/(v^n+K^n)`,
`f(v) = Vmax*h(v)` with `Vmax` solved so `f(kVelFull) == kVelFull` exactly. Fit
by direct iteration against the rendered pmo_p16/pmo_p19 percentile metric
(`pcf_percentiles.py`: same primitive as the p95 peak tool, window 1024 hop 64,
argmax f>1500Hz, p10/p50/p90 over the whole 8s render), not a single-trigger
inversion. Two rounds: an initial guess anchored at `f(0.337)~0.55` and
`f(0.652)~0.86` (worked back from the measured Hz targets through the static
cutoff law at this fixture's base/Amt) landed close but left the trough
6-percent-of-full too high (measured trough 1981 Hz vs RB 1593); lowering the
low-velocity anchor to `f(0.337)~0.485` and re-solving closed it. Settled on
`kVelHillN = 2.08`, `kVelHillK = 0.397`.

**A floating-point gotcha cost the first build its byte-identity.** The
algebra guarantees `f(kVelFull) == kVelFull` exactly, but evaluating it through
`vmax*vn/(vn+kn)` does not round to the bit-exact float in practice (the old
power law's `pow(1.0, gamma) = 1.0` path was exact almost by accident; this
one is not), so pmo_p05 and pmo_p00 differed from the pre-S-curve renders by a
few samples. Fixed with an explicit `if (v >= kVelFull) return v;` fast path
before the Hill computation (no preset velocity exceeds 1.025, so this only
ever fires exactly at the anchor); confirmed byte-identical after.

Measured (`pcf_percentiles.py`, one method both sides):

| pattern | stat | before (power law) | after (S-curve) | RB | after vs RB |
|---|---|---|---|---|---|
| pmo_p16 | trough p10 | 3101 Hz | 1766 Hz | 1593 Hz | +10.9% |
| pmo_p16 | median p50 | 4953 Hz | 3445 Hz | 3316 Hz | +3.9% |
| pmo_p16 | peak p90 | 8096 Hz | 6718 Hz | 6675 Hz | +0.6% |
| pmo_p19 | trough p10 | 1507 Hz | 1507 Hz | 1507 Hz | 0.0% |
| pmo_p19 | median p50 | 1981 Hz | 1766 Hz | 1852 Hz | -4.6% |
| pmo_p19 | peak p90 | 6718 Hz | 5728 Hz | 6718 Hz | -14.7% |

All six within the ~15% target; pmo_p19's peak is the closest to the edge.
pmo_p16's dynamic range (peak/trough) went from 2.6x to 3.8x, RB's is 4.2x
(measured through this same pipeline, not the committed finding's slightly
different numbers, which likely used a different hop or percentile
interpolation; the two are close, e.g. RB trough 1593 here vs 1378 quoted in
the addendum, and the direction and magnitude of the fix are the same either
way). Not fully closed to RB's widest swing, but materially closer (was 2.6x)
and within tolerance at every named point.

Verified unchanged, all by direct measurement: `ctest` green; a non-PCF
fixture and `REBORN_PCF_ENVLAW=old` byte-identical against the pre-addendum-3
baseline (`eea4ff9`); the base sweep (`esw_b005..b110`) and all four named
clamp fixtures (`psw_a110/a127/f050/f070`) byte-identical to the pre-S-curve
(section 25) renders, since every one of those fixtures is pattern 0 at
velocity 1.025 and the fast path makes that exact; `pmo_p05`/`pmo_p00`
byte-identical to their pre-S-curve renders for the same reason. `pmo_p16`'s
fine-time rise (window 512 hop 32) still shows 6.5-10 ms multi-step ramps into
each local peak, unchanged in character: a wave, not a sawtooth.

CAVEAT, not called final. The fit used two hand-picked anchor points
(velocities 0.337 and 0.652, pattern 16's extremes) worked back from the
rendered Hz targets through an approximate constant-headroom assumption; it was
not a least-squares fit across all ten of pattern 19's velocity steps, so the
curve's shape between the anchors (0.4-0.6) is interpolated by the Hill
function's form rather than independently verified against measured
data at those velocities. pmo_p19's peak sits at -14.7%, within tolerance but
close to the 15% line; a small further adjustment to `kVelHillK` might close
it further at some cost to pmo_p16's already-good fit, given the two patterns
share one curve and pulled in the same direction as the kReachEps tension in
section 25 did. `kReachEps` (0.13) and `kAttackTau` (8 ms) were not touched;
the velocity curve alone closed the trough gap.
