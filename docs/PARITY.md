# Parity roadmap

"Parity" here means two separable things: **functional parity** (the same
features and workflow) and **sonic parity** (each voice sounds like the classic
hardware/emulation it models). Functional parity is largely in place; sonic
parity is an iterative tuning process against reference recordings.

## Functional parity

| Feature | Status |
|---------|--------|
| 2× bass synth, 808, 909 | ✅ |
| 16-step pattern sequencer, variable pattern length, shuffle | ✅ |
| Song mode / pattern chaining | ✅ (engine); UI arrangement editor ⬜ |
| Mixer: per-channel volume/pan/mute + effect routing | ✅ |
| Distortion, tempo-synced delay, compressor, PCF | ✅ |
| Load original `.rbs` (tempo + identity) | ✅ |
| Load original `.rbs` instrument patterns | ✅ all 18 shipped songs verified |
| Pattern/step editing in the UI | ✅ |
| Mods (`.rbm`) | ✅ loader and sample sets; skin rendering blocked, see below |
| Host sync (replaces ReWire) | ✅ VST3/AU transport |

`.rbm` skin rendering is the one piece still open in that row: the sprite
payloads are not decodable JPEGs (no SOS or EOI markers), so the loader reads
the sound sets but cannot draw the artwork.

## Sonic parity: method

Each voice is synthesized from the documented analog design, then tuned
**against reference exports rendered by the original program** (the user
supplies `.WAV` exports; the first is *Trakka Rekka*, in `rb338-exports/`).
The loop:

1. Render the same song with `reborn_render` (use `--solo bass1|bass2|808|909`
   to isolate a machine).
2. Compare octave-band energy distribution, per-element envelopes (kick
   fundamental & decay via 35–120 Hz band onsets), and band ratios (hats =
   6–18 kHz share) between the two files.
3. Adjust synthesis constants (decay laws, pitch-envelope speed/depth, noise
   band-limiting, voice gains) and re-measure.

Reference targets extracted from the original *Trakka Rekka* export: kick
fundamental ≈ 61.5 Hz, kick 90 %-decay ≈ 64–93 ms, kick band (40–80 Hz)
carries ~51 % of total energy; hat band (6–18 kHz) sits ≈ −18.5 dB below the
full mix with ~40 ms discrete hit decays; overall 10–20 kHz share < 1 %.
First findings applied: drum knob headers decoded and applied per song; kick
decay laws tightened and pitch sweeps shortened; snare noise band-passed (raw
high-passed white noise was flooding the top octave); hat/cymbal gains and
decays reduced.

## Sonic parity: per-voice notes

> **This file holds settled cross-component and whole-mix laws. The live 303
> filter laws are maintained in `research/PLAN_303_ENVELOPE.md` and
> `COMPONENT_MATRIX.md`,** and the 303 note below is a structural summary, not a
> law table. Do not fit against the numbers in it.

- **TB-303**: polyBLEP saw/square VCO into a 4-pole ladder VCF with saturating
  resonance feedback (self-oscillates at high resonance). Accent uses a separate
  faster envelope that boosts VCA and sweeps the filter; slide glides with no
  envelope retrigger. *For the current filter laws, the glide time constant, the
  bipolar env-mod law, `CUT_SCALE`, the accent attack and what remains open, see
  `research/PLAN_303_ENVELOPE.md`.* Its §0 states which parts of the filter are a refit
  and which are a parameterisation limit.
- **TR-808**: bridged-T-style resonant sines (BD/toms/congas), dual-osc + noise
  snare, 6-oscillator metallic hats/cymbal/cowbell, multi-burst clap. *Tuning
  targets:* BD decay/tone law, hat oscillator frequency ratios, clap burst
  timing/tail.
- **TR-909**: punchier pitch-enveloped kick with noisy click, tone+noise snare,
  bright metallic hats/cymbals. *Tuning targets:* kick attack transient, snare
  noise band, ride/crash spectra.
- **Effects**: distortion (a hard triangle WAVEFOLDER with a Shape-driven fold
  threshold and quantizer plus a post low-pass and a fitted output gain law; the
  point-wise aliasing is the "vocoder" character), delay (static feedback high-cut,
  interpolated time glide), compressor (round 5, 2026-10-01: a peak follower on
  the lowpassed L+R mid signal with a hold and a dB-linear release, and a LINEAR
  levelling gain law that boosts below its target; `specs/SPEC_COMP.md`, "Round 5
  result"), PCF (zero-delay-feedback SVF with soft-saturated resonance). All
  measured against ReBirth exports; see the per-effect sections below and
  `docs/research/PLAN_DIST.md` / `research/PLAN_COMP.md`.

## Robustness (already enforced)

Every filter/feedback path is bounded so no parameter combination can produce
NaN/Inf/denormals: cutoffs clamped below Nyquist, feedback strictly < 1,
resonance states soft-saturated, denormals flushed. The master bus is
soft-clipped. These were checked by adversarial review of each DSP module.

## Next milestones

Functional parity is essentially reached: pattern-bank decode, the pattern and
step editor, and `.rbm` loading all landed and are marked above. What is left is
sonic, plus two platform items.

1. **Sonic parity, per component**: DONE for all seven (303, 808, 909, PCF,
   delay, compressor, distortion), each folded to main in the `research/GOALS.md` order,
   the last two (compressor, distortion) on 2026-09-18. Tracked in
   `COMPONENT_MATRIX.md`, with the 303 filter in `research/PLAN_303_ENVELOPE.md`. What
   remains within components is the small BACKLOG residue (accepted
   model-inherent residuals, the ride glide, and the fader's place relative to
   the distortion). The DELY rides play since 2026-09-26, and the COMP rides and
   the section compressor path were measured and fixed on 2026-10-01 (last
   section of this file).
2. ~~**The two whole-mix defects below**, the spectral tilt and the pan law.~~
   RESOLVED (pan law fixed 2026-07-26; spectral tilt CLOSED 2026-09-18, see "The
   whole-song spectral tilt is CLOSED" at the end of this file). End-stage
   whole-mix validation across more demo songs is now the remaining check.
3. `.rbm` skin rendering, blocked on the sprite decode.
4. macOS build + AU validation.


---

## Whole-song parity, first measurement (2026-07-26)

Until now every parity number came from single-voice or single-patch fixtures.
Luke's effects-off exports of Calm, G-Spot, Trakka Rekka and Fonda Workout make
whole-song A/B possible for the first time. `reborn_render --no-fx` matches the
export condition (it zeroes distortion routing and the delay sends, since COMP
and PCF settings are decoded but not applied and effect automation is not
applied at all).

### The headline: a consistent spectral tilt

Band energy as dB relative to each file's own total, so an overall level offset
cannot masquerade as a spectral error:

| song | | 30-160 | 160-640 | 0.6-2.6k | 2.6-20k |
|---|---|---|---|---|---|
| Calm | ReBirth | −1.3 | −6.7 | −18.5 | −16.2 |
| | ours | **−6.9** | −6.8 | **−10.0** | **−3.8** |
| G-Spot | ReBirth | −3.0 | −5.5 | −7.4 | −15.2 |
| | ours | **−7.9** | −4.7 | −5.0 | **−8.0** |
| Trakka Rekka | ReBirth | −1.1 | −7.8 | −15.9 | −14.5 |
| | ours | **−8.1** | −3.5 | −12.0 | **−5.3** |
| Fonda Workout | ReBirth | −1.3 | −7.0 | −16.6 | −13.9 |
| | ours | **−16.6** | −14.5 | −9.8 | **−1.2** |

**We are bass-light and treble-heavy on every song**, by 5-15 dB below 160 Hz and
4-13 dB above 640 Hz. This is now the largest known parity gap, and it is
invisible to the isolated fixtures: every single-voice level was fitted to match
its export exactly, so the error is in how the voices sum, not in any one of them.

Absolute level is inconsistent too: our rms is 10.9 dB quiet on Calm and 6.9 dB
quiet on Trakka Rekka, but 4.8 dB HOT on Fonda Workout. Fonda is the one song
that loads a mod (Massen's MSM 2.0), which makes the mod sample path a suspect
for its share of this.

Timing and arrangement are NOT the problem. The 30-90 Hz envelope of Calm tracks
the export closely, including the silent gaps at 6 s and 30 s, and the envelope
cross-correlation peaks at 0 ms lag on three of the four songs. Fonda Workout is
the exception at +220 ms, which wants its own look.

### Confirmed while investigating: the drum mute polarity is correct

Calm's 808 track sets automation param 0 to 0 at tick 0 and to 1 only at tick
4992 (bar 156, past the exported length), so under our reading its 808 is muted
for the entire piece. That looked like the obvious cause of an 18.3 dB deficit in
Calm's kick band, so the inverted polarity was tested across all four songs.

**Inverting is decisively wrong**: it silences G-Spot completely (−104 dBFS rms)
and Fonda Workout (−57.8), and worsens Trakka Rekka. The current polarity stays,
the finding is recorded in `engine.cpp` so it is not re-litigated, and Calm's
silent 808 is correct behaviour rather than a bug.

### Next

The tilt is the thing to chase, and it is a *summing* problem rather than a
per-voice one. Worth testing in order: the drum sample-set levels as a group
(are the hats collectively hot against the kick?), the 303-to-drums balance, and
whether the per-channel mixer volumes from `MIXR` are being applied with the
right law.


## The pan law is ~7x too wide (2026-07-26)

Found only because Luke reported "a heavy snare in the right channel". **Every
metric in this project up to this point mono-summed L+R**, so no amount of
further spectral fitting could have found it.

Measured against ReBirth's own per-channel stems for Trakka Rekka, whose song
data asks for the extremes (808 pan byte 0, 909 pan byte 127):

| | L−R | implied pan |
|---|---|---|
| ReBirth 909 stem (byte 127) | **−2.0 dB** | **+0.145** |
| ours 909 stem (byte 127) | **−38.0 dB** | **+0.984** |
| ReBirth 303#1 stem (byte 64) | −0.0 dB | +0.003 |

We map the pan byte linearly, `(v − 64)/64`, giving the full ±1. ReBirth's
maximum produces only about ±0.15. The whole-mix consequence:

| song | ReBirth L/R corr | ours | ReBirth side/mid | ours |
|---|---|---|---|---|
| Calm | 1.00 | 0.92 | −224.9 dB | −13.7 dB |
| G-Spot | 1.00 | 0.73 | −30.8 | −8.1 |
| Trakka Rekka | 0.87 | **0.42** | −11.5 | **−3.6** |

ReBirth's Calm is *bit-identically mono*. Ours is not.

Two things to settle before fitting a curve, because the two available points
do not scale consistently (byte 127 → ratio 0.147, byte 50 → ratio 0.265):

1. Whether ReBirth's pan is equal-power or constant-gain. The "implied pan"
   above assumes equal-power, which biases wide pans if that assumption is wrong.
2. Whether the law is a linear scale or a compressed curve.

**This needs one fixture**: a song with a single voice at pan bytes 0, 16, 32,
48, 64, 80, 96, 112, 127, exported from ReBirth. That measures the curve
directly instead of inferring it from two points of a musical mix.

### Process note

This is the second finding in a row that the fixtures could not see (the first
being the whole-song spectral tilt, which is a *summing* error while every
individual voice matches its export). Both were found by widening what was
measured rather than measuring the same thing more precisely. Isolated
single-voice mono fixtures were the right tool for voice calibration and are the
wrong tool for mix parity.

---

## The mixer laws, measured at last (2026-08-26)

The fixture the section above asks for was finally run, along with two more it
did not know to ask for. All three came out of one question: why does Trakka
Rekka's 909 sound so different from its export. Every measurement below is a
solo-909 export of that song with the effect rack off and the **master
compressor verified off in the file** (its `COMP` track sets param 0 to 1, so
the old `renders/02b-Trakka-Rekka-RB338-909.wav` was made with the compressor
ON and is not a valid reference; it is superseded by
`renders/2026-08-26/TrakkaRekka_909solo_hr__RB338.wav`).

### Both faders are exponential, ~0.370 dB per step

Sweeping each fader alone and fitting dBFS against the stored byte gives a
straight line in dB, not a linear gain:

| fader | points | dB per step | max residual | range |
|---|---|---|---|---|
| master | 7 (16..90) | 0.36981 | 0.002 dB | 24 dB |
| channel | 7 (32..127) | 0.37029 | 0.018 dB | 35 dB |

Two independent measurements of one design constant. Reading the byte as
`value/127` (channel) or `value/100` (master) was linear and cost real level
wherever a song sits away from the nominal 100: **Clodhopper 8.0 dB and Fast
Forward 7.0 dB too loud** on the master alone, and on Trakka Rekka, whose 909
channel rides 106 to 124, 3.5 dB at byte 112 rising to 7.9 at 127.

Only the shape is measured; the absolute is absorbed downstream, so each caller
anchors at the gain its own byte 100 used to give. That leaves every trim ever
fitted against a rbsgen fixture, which writes 100 in both places, untouched.

### Pan is not constant power: the near side stays at unity

Fifteen pan bytes, L and R read separately. The near channel is at unity for the
whole sweep and only the far channel moves:

    far_dB = -45.9 * x^2,   x = |byte - 64| / 63

reproducing every point from byte 8 to 60 within 0.2% of that constant, across
36 dB. **Centre therefore sits 3.01 dB above a hard pan in total power**, where
a constant-power law puts it at 0. Two real departures from the curve: bytes 60
to 68 are a centre detent that attenuates nothing, and bytes within 4 of either
end cut the far side to digital silence rather than to the curve's -42 dB.

This is the answer to the open question above. Our law was equal-power, so
panned material was up to 3 dB too loud relative to centred material, and the
error moved with the song's pan rides rather than sitting still.

### The accent column carries a VALUE, and the 909 writes 2

Column 0 is the accent row on both machines, and it is not a flag. Each machine
writes its own constant there and ReBirth scales the accent depth by it. Across
all 17 shipped songs: **the 808 writes 1, in 2544 cells; the 909 writes 2, in
1442; and not one song mixes the two on one machine.**

Measured on fixtures carrying value 2, the 909's accent law is

    gain = 1 + 7.934 * knob

reproducing 2.000 / 3.001 / 5.001 / 6.987 / 8.846 at knobs 16 / 32 / 64 / 96 /
127 to within 0.09%. A value-1 cell halves it, giving exactly the 3.967 that
was fitted on 2026-08-25, because those fixtures wrote 1, which is what an 808
writes and what no 909 part contains. **Every 909 accent in every song was
rendered at half strength.** `rbsgen.drum_grid` now writes each machine's own
value, so a fixture looks like a real part.

### Result on Trakka Rekka's 909

Same file, same window, both sides, sample path:

| | before | after |
|---|---|---|
| level, whole song | -7.24 dB | **-0.83 dB** |
| level, first 20 s | -4.05 dB | **+0.00 dB** |
| envelope correlation | 0.983 | **0.992** |
| widest band-share error | 2.3 pp | 3.6 pp |

The one residual worth naming: our peak is 5.0 dB below ReBirth's (-16.3 against
-11.3 dBFS) while RMS agrees, so the device's output `tanh` is compressing the
loudest accented hits. It costs 1.4 dB at accent knob 127 on a fixture and is
the next thing to look at.

### What made this findable

Every one of the three laws was invisible to the isolated fixtures, because
rbsgen writes 100 into both faders and 64 into pan, which is exactly where all
three laws are anchored, and wrote an accent cell the 909 never uses. A fixture
set that always sits at the same operating point cannot see the shape of a law
that passes through it. The song found them because a song moves.

---

## Our signal path had three saturators ReBirth does not have (2026-08-26)

Following the mixer laws above, the one residual left on Trakka Rekka's 909 was
a peak sitting 5 dB under ReBirth's while RMS agreed. That turned out to be the
tip of a larger problem: **we were compressing in three places ReBirth is
linear**, and none of the three had ever been measured against it.

### Is ReBirth's drum bus nonlinear at all? No.

The test: one voice at level 127 with the accent knob swept, and then **all
eleven voices on the same steps at level 127**, which is the hardest a drum
machine can be driven. The master is lowered to 40 so the export itself keeps
headroom, which separates any bus nonlinearity from converter clipping.

| set | knob | measured gain | linear `1 + 7.934k` | error |
|---|---|---|---|---|
| one voice | 127 | 8.958 | 8.934 | +0.023 dB |
| eleven voices | 127 | 9.026 | 8.934 | +0.089 dB |

Perfectly linear, and the stacked error is on the *loud* side. ReBirth clips at
the converter and nowhere before it: an overdriven export runs to exactly
-32768 with nothing rounded off approaching it.

### What we were doing instead

**The device buses.** `tanh(mix * 0.9) * 2.15` on the 909 and
`tanh(mix * 0.8) * 1.6` on the 808. Both have the right small-signal gain and
both asymptote, so the device output was hard-capped. On the stacked fixture our
909 peak sat at **-23.5 dBFS and would not move** while ReBirth reached -3.3;
the accented hit read 9.49 dB quiet. Replaced by the equivalent linear gains
(1.935 and 1.28), which leaves every quiet fixture bit-identical.

**The master compressor, which ran on every render.** The engine called
`comp_->process()` unconditionally, with the threshold parked at 0 dBFS and a
6 dB soft knee, whatever the song said. Every rbsgen fixture switches ReBirth's
compressor off, so for the whole life of the project we compared a compressed
render against an uncompressed one. It only bites on loud material, which is why
it hid: on the stacked 909 hit it cost 2.3 dB of accent gain and, because it has
release memory, **ducked the following unaccented hits by 2.8 dB**. The COMP
track's `param 0` is the switch and it is now decoded and honoured. Its ratio
and threshold are still undecoded, so the switch defaults off.

**The master output stage.** `softClipUnity` is already 1.07 dB down at
-1 dBFS. ReBirth is straight to full scale and then clips hard. The master now
uses a hard clamp; voices, the delay feedback path and the PCF keep the soft
version, where the bound is character or a safety rail rather than a parity
surface.

### Result

| | before | after |
|---|---|---|
| 909, eleven voices stacked, accent 127 | -9.49 dB | **-0.17 dB** |
| 808, eleven voices stacked, accent 127 | -2.12 dB | **+0.03 dB** |
| 909 accent gain across the full knob | up to -0.62 | **within 0.008** |
| Trakka Rekka 909, whole song | -0.83 dB | **+0.08 dB** |
| Trakka Rekka 909, peak | 5.0 dB under | **0.17 dB under** |
| Trakka Rekka 909, envelope correlation | 0.992 | **0.996** |

Two constants moved with the saturators gone, both having been fitted through
them. The **808 accent depth is 4.02**, not 3.95 (free fit
`1.0121 + 4.0233 * knob`, residuals under 0.0015). The 909's 7.934 re-measured
as 7.9403 on the clean build and stands. The crash and ride, which had carried a
+0.6 and +1.0 dB residual for weeks, came back to +0.01: that was the
compressor.

### The shape of all three

Each was a nonlinearity added for musical safety, never measured against the
thing it was modelling, and invisible at fixture levels because fixtures are
quiet. They only appeared when something was driven hard, and nothing in the
fixture set drives anything hard. **A saturator is a parity surface. If we have
one and cannot point at the measurement that put it there, it is a bug.**


---

## The PCF, measured end to end (2026-08-27)

Supertask 4. Conditions for every number below: `.rbs` fixtures from
`tools/rbsgen/gen_pcf_fixtures.py`, ReBirth exports made by automation (not published), master
compressor off, no other effect in the path, power spectra only. 124 exports
across six rounds. Full working and the residuals are in `research/PCF_FINDINGS.md`.

### The controls, and where they live

`PCF ` chunk, 12 bytes: `[ver][Freq][Q][Amt][Pattern 0..54][Decay][Mode]`. Mode
**0 is BANDPASS and 1 is LOWPASS**. The PCF's exclusive target is `MIXR` +2, and
its own on/off is its TRAK's param 0. See `FORMAT_RBS.md` section 6.

### Cutoff

    fc = 9.01 * 2 ^ (0.08388 * byte)   Hz

9.0 Hz to 14.5 kHz, a span of 10.65 octaves. Fitted over Freq bytes 48 to 127
against two sources that agree to 0.28 dB where they overlap; worst residual
0.45 dB.

**The bottom of the range is measured now, not extrapolated** (2026-08-28). A
drone an octave lower, fundamental 32.3 Hz, finally straddles a corner that sits
between 9 and 60 Hz. Bytes 0, 8, 16, 24, 32, 40 and 48 measure 9.58, 14.91,
23.69, 37.31, 60.05, 95.32 and 150.39 Hz, and the law predicted every one of
them within 0.53 dB. The extrapolation was right; it is simply no longer an
extrapolation.

### Resonance

    1/Q = 0.1050 + 1.3385 * (1 - byte/127) ^ 1.77

with the knob CLAMPED at byte 112. Q(0) = 0.693, Butterworth to within a
percent, so minimum resonance is maximally flat and not damped. Within 0.18 dB
over bytes 0 to 112.

**The knob saturates at byte 112**, measured 2026-08-28. Round 2 read bytes 112
and 127 fitting to the same value as its own failure to resolve a peak narrower
than its harmonic grid. With the denser drone they are BIT-IDENTICAL: 0.00 dB
rms between the transfer curves of 112, 120 and 127, where 104 to 112 differ by
5.72 dB at the peak. The old law extrapolated to Q = 9.5 at byte 127; the answer
is 7.35 from byte 112 upward, and two shipped songs sit at 127.

**The corner moves with resonance**, which a TPT state-variable filter does not
do by construction:

    fc_scale = 1 - 0.33441 * r + 0.62565 * r^2      r = resonance byte / 127

Fitting ReBirth's corner across the resonance sweep at a fixed Freq gives 373,
354, 350, 359, 373, 393, 413 and 440 Hz for bytes 0 to 112: a shallow dip and
then a rise to +1.44 dB. Same estimator on both sides, so it is the filter and
not the method. Constrained to 1.0 at zero resonance so the cutoff law keeps the
anchor it was fitted at; worst residual 0.16 dB.

The bandpass shares the corner to within 0.2 dB, so the mode switch picks an
output rather than retuning anything.

### Level

Routing a section through the PCF with the unit switched off is **0.00 dB**,
flat across every bin, so the filter's gain is its own and not the routing's. An
open filter at minimum resonance is **+4.33 dB**, falling with resonance as
about `k^0.56` and plateauing near -4.1 dB from Q byte 80 up. The gain is an
interpolated table rather than a fit: no simple curve reproduces both halves.

### Envelope

Attack is instant and comes from the pattern; decay is exponential. Freq and the
envelope sum in PARAMETER space and the sum is clamped, which is why Amt goes
inert once Freq is at maximum:

    cutoff_param = Freq + Amt * env,   clamped to [0, 1]
    tau = 11.1 ms * e ^ (5.338 * decay knob)      11 ms .. 2.31 s

Depth is 1:1. Decay residuals reach 15 percent, which is the threshold-crossing
metric rather than scatter in ReBirth.

### The 55 patterns

Measured, not transcribed from Appendix D; the table is
`src/effects/pcf_presets.inc` on a thirty-second grid with per-trigger velocity.
Every category the manual states is confirmed. Two disagreements with the manual
stand: pattern 5 triggers on sixteenths where page 62 says quarter notes, and
patterns 34, 36 and 45 trigger on sixteenths where page 65 puts all of 34 to 45
on thirty-seconds.

### Topology

The PCF is a series insert AFTER the distortion, on exactly one section. Its bus
carries that section's pan and its echo send is tapped after both inserts.

### Where we stand

Static filter **0.31 dB mean rms** over eleven settings, worst 0.52 dB, and
**0.54 dB mean** over thirteen more at the bottom of the cutoff range and the
top of the resonance range, worst 1.34 dB. Envelope **0.40 octaves rms with no
systematic offset** over six patterns from whole notes to consecutive
thirty-seconds.

Those first figures were 0.49 mean and 2.15 worst before the corner drift was
modelled; the 2.15 was resonance 96, and it is 0.52 now.

## The master compressor, measured and fitted (2026-09-17)

Supertask 6. Six export batches (`fixtures-2026-09-16/comp-law*`,
`comp-ballistics*`; renders alongside), clean-room, fitted by a walled implementer
and verified by Role A with the `comp_wav` harness (ReBirth's own uncompressed
render fed through our compressor, so there is no source confound). Conditions:
one 303 drone (or an 808 stack for ballistics) through the MASTER (stereo)
compressor, no other effect, steady-state RMS over 2-6 s; every measurement voice
is TPT, never Open303 (which is 15.65 dB quiet).

**This section records the 2026-09-17 sign-off (round 2). Round 5 (2026-10-01,
branch `comp-detector-cleanroom`, awaiting Luke's sign-off) replaced its model;
the current one is in `specs/SPEC_COMP.md`, "Round 5 result", and summarised under
"Structure now" below.** The measurements here still stand; the round-2 model
does not.

**What was measured (ReBirth's output, still true).** The same gain goes to both
channels. ReBirth is a LEVELLER: below threshold it BOOSTS, contradicting the
manual's "levels below pass unprocessed". Ratio byte 0 is an exact bypass. Ratio
byte 0->127: gain reduction 0 -> -16.5 dB at a hot input above threshold.
Threshold byte 0->127: the level the output is pinned to rises from about -32 to
-19 dBFS, ratio-independent. The boost below threshold scales with ratio (0 at
ratio 0) and falls with threshold. The channel fader is dB-linear at ~0.37
dB/byte (as the mixer fader, PARITY above). From drum transients: full reduction
within ~30 ms, recovered by ~250 ms (a fader step is confounded: ReBirth glides
fader automation over ~100 ms).

**The round-2 model (a fit, superseded; not a measurement).** A soft-knee
feed-forward downward compressor with automatic make-up, `GR_dB =
makeup(ratio,threshold) - slope(ratio)*softKnee(level - threshold)`, an RMS
detector on ((l + r) / 2)^2, and attack ~10-20 ms and release ~100-200 ms as
internal constants. It was within ~1 dB across the threshold/ratio/plane
surface, with an ACCEPTED RESIDUAL at the threshold-byte-0 / quietest-input
corner, -4.17 dB, proven unclosable in THAT model (input-independent make-up is
pinned by the ratio-127 loud cell). Full record in `docs/research/PLAN_COMP.md`.

**Structure now (round 5, fit to the measurements above and to `comp-material`,
`comp-ballistics2` and `comp-rides`).** The detector is a PEAK follower on
`(l + r) / 2` through two cascaded one-pole lowpasses (24.26 us each; -3 dB at
about 4.45 kHz at 44.1 kHz, 4.2 kHz being the analog prototype), with an 8.94 ms
one-pole attack, a 26.29 ms hold, a 150.9 dB/s release and a floor at F. There
is no smoother on the gain. The gain law is LINEAR, `g = (1 - k) + k * T /
max(env, F)`, five constants fit to the 146 compressed static cells. Residuals
through `comp_wav`: all 152 static cells within 0.03 dB (the threshold-0 corner
-0.03 dB), all 28 comp-material cells within 0.17 dB. Those two are IN-SAMPLE
(the constants were fit to those cells); the out-of-sample checks are the
comp-rides cells and the demo songs, both in "The compressor's rides and
topology" at the end of this file, and a HOLD-OUT set of five new sources at
three new settings (`fixtures-2026-10-01/comp-holdout`): every one of its 15
cells within 0.22 dB, where round 2 missed by up to 7.23 dB (`specs/SPEC_COMP.md`,
"Verification notes"). Among the demo songs only the stock ones are evidence
(our mod drums are noise, BACKLOG 17): on KiloMix, over the bars the compressor
work moved, mean |ours - ReBirth| per bar goes 8.59 / 4.18 / 2.98 dB for the
build before the topology fix, the topology build and round 5 (same note). The
fit is valid
at 44.1 kHz: the timing constants are rate-invariant, but a peak detector's
reading of a high-frequency source moves with the rate (cm_hats_r127_t000
through the engine gains +3.34 / +3.61 / +5.25 dB at 44.1 / 48 / 96 kHz, a 1.91
dB spread; round 2's was 1.27).

The 2026-09-17 sign-off verified the DSP alone (`comp_wav`), not its place in
the engine: the rides, the master input level, the detector's pan response and
the section path were measured on 2026-10-01 and are in "The compressor's rides
and topology" at the end of this file.

## The distortion is a wavefolder, and its gain law (2026-09-18, supertask 7)

Signed off and folded to main (`7fb54dc`). Measured entirely from ReBirth exports;
the shipped coefficients were fit clean-room by a walled implementer from spec +
measured data only (`docs/specs/SPEC_DIST.md`, `specs/SPEC_DIST_GAIN.md`).

**Mechanism.** `in -> pre-gain(Amount) -> triangle wavefold(Shape sets the fold
threshold) -> level-quantize(Shape sets the step count) -> post one-pole low-pass
-> output gain(Amount,Shape)`. A wavefolder is the only memoryless mechanism that
reaches ReBirth's 4-8% HF on the drone; a plain hard clip tops out near 0.8%.

**Byte map (corrected).** DIST chunk/track: byte 1 = Shape (`distTone`), byte 2 =
Amount (`distAmount`): the file order IS Appendix C's MIDI order (Shape 104,
Amount 105); the loader had them swapped. Amount 0 is an exact bypass.

**Character.** Ear-signed off 2026-09-18: the inharmonic (aliased) fraction is the
"vocoder" texture Luke's ear identified, matched to ReBirth's 38-69% on the dense
grid (`dist_inharm.py`). Band energy within ~3-4 dB. Trend-level fit; the surface
is bumpy cell-to-cell (a hard-driven fold is chaotic in the alias
pattern), so the fit targets the per-Amount marginal, not individual cells.

**Output gain law.** `outGain(amount,shape) = clamp(kOutBase*exp(kOutAmt*amount +
kOutShp*shape))`, fit from the measured output-level surface (`dist_gain.py`,
`dist_gain_target.csv`). Replaced the old `kOutTrim` constant, which ran up to
13 dB too loud at high drive. Median level gap across the driven grid 4.6 -> 2.0 dB,
the systematic overshoot gone; ~2 dB residual scatter is the same aliasing chaos.

**Topology + automation.** Per-section chains (four instances, distortion before
the PCF, each panned once; there is no distortion bus), and the DIST track's
Amount/Shape/on-off timed rides are decoded (`AutoTarget::Dist`) and dispatched;
songs ride them heavily (Mutilated Species II: 1241 Amount rides).

**Whole-mix parity.** A Taste of Haste (no mod, distortion on both 303 basses):
our full mix tracks ReBirth within -1.5 dB and ~1.5 dB per octave band; the soloed
distorted bass lifts 2-16 kHz by +15.4 dB with the gain law taking -2.7 dB.

**Open:** where the channel fader sits relative to the distortion is unmeasured
(BACKLOG 21). The COMP rides and the section compressor path, once listed here,
were measured and fixed on 2026-10-01 (last section of this file).

## The whole-song spectral tilt is CLOSED (2026-09-18)

The 2026-07-26 measurement above found us bass-light and treble-heavy by 5-15 dB
on every song, the largest known parity gap and flagged as a summing error. Re-run
now that all seven components are wired (`--no-fx`, instrument summing, the same
condition it was found in; window per song; band dB rel own total; delta = ours -
ReBirth):

| song | 30-160 | 160-640 | 640-2.6k | 2.6-20k |
|---|---|---|---|---|
| Calm         | -5.6 -> **+0.2** | -0.1 -> -1.2 | +8.5 -> **+1.2** | +12.4 -> **+0.7** |
| G-Spot       | -4.9 -> **-0.7** | +0.8 -> 0.0  | +2.4 -> +1.7 | +7.2 -> **+2.2** |
| Trakka Rekka | -7.0 -> **-0.2** | +4.3 -> +0.3 | +3.9 -> +2.0 | +9.2 -> **+1.8** |

Every band is now within ~2 dB, most within 1. The bass-light hole and the
treble-heavy lift are gone. No single fix closed it; the component campaign did
(the exponential mixer/pan laws, removing the two drum-bus saturators and the
master soft-clip that ReBirth does not have, the per-voice level fits, the drum
knob/automation decode). The old cross-cutting "summing tilt" and "pan law ~7x
too wide" items are resolved.

**Full-rack whole-mix** (effects on, vs `rb338-exports/wholemix-2026-08-27`, same
window): Calm level -1.9 dB and bands within ~3 dB, Trakka Rekka -1.7 dB within
~2 dB, G-Spot -3.9 dB with the mids/highs +3.4 dB hot (it rides the PCF target 35
times, the heaviest of any song, the likely source; a per-song residual, not a
summing error). The fully assembled instrument tracks ReBirth within a few dB.

## The delay plays its DELY track in Song mode, not its chunk (2026-09-26)

Review E3, approved by Luke 2026-09-26, fixed in `57c7cfb`.

**The law.** In Song mode ReBirth plays the DELY TRACK, param N being chunk byte
N: 0 the unit's on/off, 1 Steps, 2 the grid (nonzero = straight), 3 Feedback, 4
the wet-return pan. The `DELY` chunk is the panel as it stood at save time, the
Pattern-mode panel (a saved song holds the Pattern-mode settings beside the Song,
manual p137), and Song mode does not play it. The mixer, PCF, DIST and COMP
tracks already worked like this; the delay was the last effect still playing its
chunk. Three parts, of different standing:

- **MEASURED.** The track's tick-0 dump is the Song-mode start, and a song naming
  a mod plays it the same way (the four E3 fixtures, 2026-09-26, below). A timed
  pan ride moves the delay (dr_even, 2026-09-27, below). Switching the unit off
  with param 0 CLEARS the delay: no echoes while it is off, and nothing from
  before the switch-off comes back (dr_p0, below). Ours matches all three, the
  far side of a hard pan aside (-46.8 dB in ReBirth, silent in ours; BACKLOG 19).
- **MEASURED, NOT YET MATCHED.** ReBirth applies a ride at its 1/32 tick, and
  the control GLIDES to the new value rather than stepping (dr_odd40, 41 and 42,
  below). The glide's duration and shape are not measured: the two readings fit no
  single simple shape (a fast move, about 50 ms linear in amplitude or a one-pole
  near 30 ms, fits the tick-41 reading; the tick-40 residual needs a longer tail),
  and the "roughly 100 ms" once written here was the compressor work's fader
  observation, not these exports. We apply a ride at the next 16th step, up to
  62.5 ms late at 120 BPM, and instantly. Engine-wide, BACKLOG 18.
- **ASSUMED.** That Steps, grid and feedback rides behave like the pan ride. No
  fixture rides them, and what happens to echoes in flight when Steps or the grid
  moves is unmeasured (ours moves the read head at once, with no crossfade).
- **UNMEASURED, the unit switch's edges.** dr_p0 switched off on a downbeat for a
  whole bar. It does not settle an off and on at the same tick or within one 16th
  (ours clears the delay; ReBirth might ignore an off that lasts no time), a short
  off inside one delay period, whether a switch-off hard-cuts an echo in flight or
  fades it, rides arriving while the unit is off (ours applies them silently and
  uses them on resume), or rapid toggling. dr_p0 also separates "cleared" from "the
  ring keeps cycling unheard" by only 0.27 dB; ReBirth matches "cleared" to 0.01 dB,
  so the finding stands, but a looser tolerance could not tell them apart.

**Measurement.** Four isolated fixtures, `fixtures-2026-09-26/delay-chunk-vs-track`:
one 909 rimshot on each downbeat, 120 BPM, echo send 100, every other device and
effect off. rbsgen writes chunk and track identically, then three copies patch the
DELY chunk to disagree with the track on steps, feedback and pan at once. Side A
is 4 triplet steps, feedback 96, pan 64: a centred echo every 0.667 s, repeating.
Side B is 2 steps, feedback 0, pan 127: one echo at 0.333 s, hard right. Exports in
`renders/delay-chunk-vs-track-2026-09-26`. The reading is echo energy in a 20 ms
window at each candidate echo time, per channel (no mono sum), relative to the dry
hit's own channel, bars 2 and 3; entries are L / R in dB.

| fixture | chunk | track | 0.333 s | 0.667 s | 1.333 s | ReBirth played |
|---|---|---|---|---|---|---|
| dct_ref | A | A | none | -9.1 / -9.1 | -11.5 / -11.5 | A |
| dct_track | B | A | none | -9.1 / -9.1 | -11.5 / -11.5 | A, the track |
| dct_mirror | A | B | -56.6 / -10.0 | none | none | B, the track |
| dct_mod (names MSM2) | B | A | none | -10.1 / -10.1 | -12.5 / -12.5 | A, the track |

The dct_mod copy loaded MSM2's rimshot (its dry fingerprint differs from
dct_track's) and still played the track's delay, with the same -2.4 dB per repeat.

**Ours**, `reborn_render` with the standard 909 samples, full rack, same reading.
Before the fix dct_track played B (one echo at 0.333 s, L silent, R -10.1) and
dct_mirror played A (-8.2 / -8.2, -10.6 / -10.6): the chunk in both. After it,
dct_ref, dct_track and dct_mod read -8.2 / -8.2 at 0.667 s and -10.6 / -10.6 at
1.333 s, nothing at 0.333 or 1.000 s, and dct_mirror reads one echo at 0.333 s,
L silent, R -10.1, and nothing after. Echo times, balance and the per-repeat ratio
(-2.4 dB against ReBirth's -2.4) all match. With its mod resolved instead of the
standard samples (`--mods` in place of `--samples`; given both, the standard 909
set loads over the mod's), dct_mod loads MSM2's samples and reads -9.6 and
-12.0, the same train.

**The echo gain matches to 0.01 dB. The "0.9 dB excess" this section used to
carry is ReBirth's hit timing, and larger than 0.9 dB.** On the repeating fixtures
our echoes read 0.9 dB above ReBirth's in the table's method, which averages bars 1
and 2 against a dry reference taken from those same downbeats. Each downbeat
carries the previous bar's third echo (3 x 29,400 samples is one bar), and each
echo it reads carries an earlier hit's later echo too. That overlap is not a
reading error: the audio differs there. ReBirth's hits sit on 64-sample blocks, up
to a block early, and its first hit one or two blocks late (BACKLOG 20), while its
echoes are exact to each hit, so an echo lands beside the later hit and sums with
it out of phase; ours lands on it and adds. Per bar, in 30 ms windows against each
file's own bar-0 hit, the gap is 1.7 to 3.3 dB in bar 1 and 1.0 to 1.8 dB in bars 2
and 3 (`research/FINDINGS_AUDIT.md`, 2026-09-27), and a model built from ReBirth's own clean
bar-0 hit and echo gains reproduces both files' readings from the onsets alone.
The delay itself, read against bar 0's hit, which nothing precedes, as the
least-squares gain of each echo on it (2026-09-27):

| fixture | ReBirth echo 1, 2 | ours echo 1, 2 |
|---|---|---|
| dct_ref (L) | -9.98, -12.42 dB | -9.98, -12.41 dB |
| dct_track (L) | -9.98, -12.42 dB | -9.98, -12.41 dB |
| dct_mirror (R, its one echo) | -9.98 dB | -9.98 dB |

**The far side at pan 127 is not silent in ReBirth.** Over dct_mirror's echo the
left channel is a coherent copy of the right at -46.8 dB (least-squares gain,
correlation 0.99, bars 1 to 3 alike), and dr_even's bars 2 and 3 read the same.
Ours is digital zero there, because `panGains` cuts the far side to silence at
the ends. Inaudible under the near side, and a pan-law change, so it waits for
sign-off: BACKLOG 19.

### The ride fixtures (2026-09-27)

`fixtures-2026-09-27/delay-rides` (its `MANIFEST.md`), exported in Song mode to
`renders/delay-rides-2026-09-27`. One 909 rimshot, echo send 100, 120 BPM, 4 bars,
pan 64 at tick 0, everything else off; `isolation_check.py` fails each file for
exactly one reason, the timed DELY event under test. The reading is energy in a
30 ms window per channel, relative to that channel's bar-0 dry hit, entries L / R
in dB. Ours: `reborn_render` with the standard 909 samples, full rack, 8 s.

| fixture | what it does | window | ReBirth | ours |
|---|---|---|---|---|
| dr_even | steps 4 triplet, fb 0; pan to 127 at tick 64 (bar 2) | echo in bars 0 and 1 | -10.0 / -10.0 | -10.0 / -10.0 |
| | | echo in bars 2 and 3 | -56.7 / -10.0 | silent / -10.0 |
| dr_odd40 | hit at step 2, steps 14 (echo at 2.583 s); pan to 127 at tick 40 | echo at 2.583 s | -52.4 / -10.0 | silent / -10.0 |
| dr_odd41 | the same ride at tick 41 (2.5625 s) | echo at 2.583 s | -16.4 / -10.0 | -10.0 / -10.0 |
| dr_odd42 | the same ride at tick 42 (2.625 s) | echo at 2.583 s | -10.0 / -10.0 | -10.0 / -10.0 |
| dr_odd40 to 42 | | echo at 4.583 s | -56.7 / -10.0 | silent / -10.0 |

dr_even: a timed ride plays, and ours matches it on the near side (the far side is
BACKLOG 19). The odd-tick set: ReBirth's tick-41 ride reaches an echo that leaves
the delay before tick 42, so it applies the tick, where ours waits for the 16th at
tick 42 and the echo stays centred. And it glides: 21 ms after the tick-41 ride the
far side is only 6.4 dB down, and 83 ms after the tick-40 one it is still 4.3 dB
short of full, where ours has already jumped. The compressor work met a glide on
faders too, of roughly 100 ms; these two readings do not pin the delay's duration
or shape (BACKLOG 18).

dr_p0: steps 4 triplet, feedback 96, the unit off at tick 32 (bar 1) and on at
tick 64 (bar 2), one hit per bar. Windows at each downbeat and at the two echoes
after it:

| bar | ReBirth +0, +0.667, +1.333 s | ours before (ring frozen) | ours now (ring cleared) |
|---|---|---|---|
| 0 | 0.0, -10.0, -12.4 | 0.0, -10.0, -12.4 | 0.0, -10.0, -12.4 |
| 1 (off) | 0.0, none, none | 0.0, none, none | 0.0, none, none |
| 2 (on again) | 0.0, -10.0, -12.4 | +1.4, -6.9, -9.3 | 0.0, -10.0, -12.4 |
| 3 | +1.0, -7.7, -10.1 | +2.0, -5.8, -8.3 | +1.4, -6.9, -9.3 |

Bar 2 is exactly a fresh hit's in ReBirth: switching off clears the delay, and
what was sent while it was off does not echo later. Ours froze the ring and
replayed bar 0's tail two seconds late; it now empties the ring as the switch-off
is applied, and bars 1 and 2 match. Bar 3's 0.4 to 0.8 dB is not the delay. Its
echoes land on bar 2's fourth and fifth, and ReBirth's bar-3 hit sits 8 samples
less than a bar after bar 2's (both on the 64-sample block grid, 16 and 24 samples
early against the exact grid; -80 and -88 against ReBirth's own bar-0 hit, which is
a block late). Built from ReBirth's own clean bar-0 hit and echo gains, the two
trains 8 samples short of a bar apart read +0.9, -7.7 and -10.1 dB, against its
measured +1.0, -7.7 and -10.1; a whole bar apart, which is our grid, they read
+1.4, -6.9 and -9.3, exactly ours. So it is BACKLOG 20's hit timing. (This
paragraph first said 7 samples; the extra sample was cross-correlation jitter.)

**The corpus.** The chunk and the track's tick-0 dump disagree in 8 of the 16
demo songs, 6 of them naming no mod: A Taste of Haste (steps, feedback, pan),
Alloy Cowboy Resurrection (steps, feedback, pan), Calm and G-Spot (feedback),
Multiball Challenge (steps, feedback), SuperNova (pan 127 against 85), Trakka Rekka
(feedback 0 against 36, pan 127 against 63) and Transfusion (feedback, pan). The
track carries 4,435 timed rides, all on params 1 to 4; param 0 is ON at tick 0 in
all sixteen and never moves. Full-rack 90 s renders (TPT) change in 12 songs: those
8, and Groove It Up, Metallicon City, Mutilated Species II and KiloMix, whose rides
fall inside the 90 s. The whole-mix exports in `rb338-exports/wholemix-2026-08-27`
are not a fixture, so only as a sanity check: over the same 90 s their L-R
balance moved our way in the two songs where the change is large, SuperNova
-2.03 dB in ReBirth against ours -4.70 before and -3.03 after, Trakka Rekka -0.69
against -1.26 and -0.99. The other changed songs in that set barely moved, and
not always our way: Transfusion +0.72 in ReBirth against ours +0.27 and +0.23,
Metallicon City +0.02 against +0.03 and +0.09, G-Spot and Calm within 0.01 dB.

**Unmeasured.** What happens to echoes in flight when Steps or the grid is ridden
(ours moves the read head at once, with no crossfade; G-Spot rides Steps 10 times
inside 90 s, Mutilated Species II 39). And Pattern mode, which has no export: that
the chunk is what Pattern mode plays is the manual's statement plus this negative
result, not a measurement. (The ring while the unit is off was on this list until
dr_p0 measured it, above.)

## The compressor's rides and topology, measured (2026-10-01)

`fixtures-2026-10-01/comp-rides` (its `MANIFEST.md`; generator
`tools/rbsgen/gen_comp_rides.py`, reader `tools/measure/comp_rides.py`), exported
to `renders/comp-rides-2026-10-01`. One 303 drone (saw, cutoff 127, slid),
channel fader 127, 120 BPM, 4 bars, the compressor the only effect; RMS per
channel over 1.0-3.9 s and 4.5-7.9 s, L and R read separately. Ours with
`REBORN_BASS_ENGINE=tpt`, full rack, 8 s. The earlier master-path references
(`renders/comp-law2-2026-09-16`) reproduce today to 0.01 dB. The TPT drone itself
is 0.94 dB hot against ReBirth's at bypass (-14.85 against -15.79): a 303 level
residual, not the compressor, and it shows in every uncompressed cell below.

**MEASURED.**
- *The COMP track's rides play.* A Ratio or Threshold ride at tick 64 lands within
  0.03 dB of the static export at its end setting (cr_*). Ours ignored them; it
  plays params 0 to 2 now (BACKLOG 5). ReBirth reaches 90 percent of the step in
  15 ms (25 ms for a threshold ride down). Round 2 took 15 ms when the ride
  added cut and 85 to 95 ms when it removed it. Round 5 (no gain smoother,
  params decoded once per 512-sample block) takes 5 / 15 / 15 / 5 ms (ratio
  down / ratio up / threshold down / threshold up) against ReBirth's 15 / 15 /
  25 / 15: faster on three of the four, by one 10 ms analysis window each. The
  shape of ReBirth's glide is unmeasured (BACKLOG 18); measure it before adding
  a param ramp.
- *The master fader follows the master compressor, which sees the output level.*
  Master byte 70 to 115 moves the compressed output by the fader's full 16.65 dB
  at threshold 0 and 127 alike, as it does uncompressed (mt_). Ours ordered the
  fader after it already, but applied the fixed 0.72 headroom (-2.85 dB) after it
  too, so the compressor saw the bus 2.85 dB hotter than the output and cut 2.99
  dB more than `comp_wav` (the DSP alone, fed the output level) says it should.
  The headroom now comes first.
- *The master detector sums L and R.* A hard-left drone (near side at unity, so L
  is the same as centred) reads -26.47 / -20.61 / -17.14 dBFS at ratio 127,
  threshold 0 / 32 / 64, against -32.20 / -26.48 / -23.06 centred: +5.73 / +5.87
  / +5.92 dB (mp_). A power mean would give about 3 dB and an amplitude sum about
  6, less what ratio 127 takes off a 6 dB step. Ours took the louder channel
  squared and could not see pan; round 2 detected ((l + r) / 2)^2, and round 5
  takes the peak of the lowpassed (l + r) / 2. A hard-panned source cannot tell
  a signed sum from |l| + |r|, so only "L+R" is measured (UNMEASURED below).
- *The section compressor comes BEFORE the channel fader and the pan.* Compressed
  on the 303's section, fader 127 to 90 drops the output by the full 13.69 dB the
  uncompressed path drops (r127_t000 -26.47 -> -40.16, r127_t127 -18.29 ->
  -31.95), and hard left reads the same as centred (-26.47 both at r127_t000,
  -18.27 and -18.29 at r127_t127). Every pinned section cell at fader 127 equals
  the master compressor's hard-left cell to 0.01 dB (-26.47, -20.61, -17.14 at
  threshold 0, 32 and 64). Ratio 0 is an exact bypass on the section too
  (-15.79). Read as a mechanism, that equality says the section compressor sees
  the section at the level a fader of 127 gives it, as one channel of the L+R
  detector; the mechanism is the fit described under ASSUMED, not a measurement.

**ASSUMED.** That the section compressor's input is exactly the fader-127,
centre-pan, master-headroom level times 0.5: a model that fits every section
cell, not a measured mechanism. Checked with `comp_wav` (the uncompressed drone
scaled by -g dB, +g added back): g = 6.0 dB fits within 0.10 (t000), 0.56 (t016),
0.26 (t032), 0.14 (t048), 0.25 (t064), 0.45 (t096), 0.62 (t127) and 0.65
(r064_t000), and 6.02 dB is the 0.5. That the master fader does not reach the
section compressor (every st_ cell is at master 100). That a section compressor
plays its rides like the master's (every cr_ ride is on the master). That the
channel MUTE, like the fader, comes after the section compressor: ours keeps the
compressor running on the section at fader 0 and on mute and silences only its
output, so the section comes back without a burst (it used to run on silence
there, release to full make-up and come back clipped).

**UNMEASURED.** Whether the detector sums signed samples or |l| + |r|: it needs
an antiphase source; ours uses the signed sum. Where the channel fader sits
relative to the DISTORTION (BACKLOG 21). Where the delay send taps relative to
the section compressor. Where the channel mute sits relative to it. The shape
of ReBirth's ride glide (BACKLOG 18).

**The cells, ours before and after** (L; R is the same, or silent for the
hard-left cells). "Before" is the build at `5f1fe15`.

| Fixture | ReBirth | ours before | ours after |
|---|---:|---:|---:|
| cr_ref_r000_t000 | -15.79 | -14.85 | -14.85 |
| cr_ref_r127_t000 | -32.21 | -35.85 | -32.86 |
| cr_ref_r127_t127 | -18.75 | -22.22 | -19.23 |
| mp_r127_t000_p000 | -26.47 | -35.85 | -26.55 |
| mp_r127_t032_p000 | -20.61 | -30.14 | -20.84 |
| mp_r127_t064_p000 | -17.14 | -26.57 | -17.27 |
| mt_off_m070 / m115 | -26.88 / -10.24 | -25.95 / -9.30 | -25.95 / -9.30 |
| mt_r127_t000_m070 / m115 | -43.31 / -26.66 | -46.97 / -30.30 | -43.97 / -27.31 |
| mt_r127_t127_m070 / m115 | -29.86 / -13.21 | -33.32 / -16.67 | -30.33 / -13.68 |
| st_off_p064_v127 / v090 | -15.79 / -29.47 | -14.85 / -28.54 | -14.85 / -28.54 |
| st_r000_t000_p064_v127 | -15.79 | -14.85 | -14.85 |
| st_r064_t000_p064_v127 | -19.64 | -21.99 | -18.31 |
| st_r127_t000_p064_v127 / v090 | -26.47 / -40.16 | -39.01 / -38.35 | -26.55 / -40.25 |
| st_r127_t000_p000_v127 | -26.47 | -39.01 | -26.55 |
| st_r127_t016 / t032 / t048 (v127) | -23.05 / -20.61 / -18.75 | -36.03 / -33.30 / -31.31 | -23.58 / -20.84 / -18.86 |
| st_r127_t064 / t096 (v127) | -17.14 / -17.75 | -29.72 / -27.25 | -17.27 / -17.25 |
| st_r127_t127_p064_v127 / v090 | -18.29 / -31.95 | -25.37 / -31.65 | -17.96 / -31.65 |
| st_r127_t127_p000_v127 | -18.27 | -25.37 | -17.96 |

Every compressed cell is within 0.66 dB now but one: st_r064_t000 at +1.33 dB
raw. How much of the drone's +0.94 dB residual a cell passes on depends on
where its detector sits against the knee, so it is taken out with `comp_wav` run
on both drones at the cell's setting (ReBirth's uncompressed drone and ours,
scaled by -g dB, g = 0 on the master and 6.02 dB on a section or hard left):
- ratio 127 at threshold 0 to 64 flattens it (0.00 to 0.10 dB passes) in the
  round-2 build only; round 5 passes +1.27 to +2.29 dB there (below);
- st_r064_t000 passes 0.66 dB (`comp_wav` -18.98 on ReBirth's drone, -18.31 on
  ours), so its corrected miss is +0.66 dB, not 0.39 as first written;
- the section t096 and t127 cells at ratio 127 pass the whole 0.94 dB, because
  the section detector sits 6 dB lower, below the knee; their corrected misses
  are -0.44 and -0.62 dB.

The master cells' remaining 0.47 to 0.66 dB (round 2) is the DSP's own, inside
the signed-off ~1 dB. The ride steps read +18.01 / -18.01 / +13.63 / -13.63 dB
against ReBirth's +16.42 / -16.44 / +13.43 / -13.47. The ratio rides' 1.57 to
1.59 dB raw miss is not a breach of the 1 dB bar: the ratio-0 end carries the
whole 0.94 dB residual and the ratio-127 end none of it, so corrected the step is
17.07 dB and the miss 0.63 to 0.65. The threshold rides miss by 0.16 to 0.20 raw
and 0.06 to 0.10 corrected.

**Round 5 (2026-10-01).** Everything above in this subsection is the round-2
build. With round 5's peak detector the RAW misses on the compressed cells are
+0.93 to +2.33 dB (round 2: -0.67 to +1.33): ratio 127 threshold 0 to 48 at
+2.22 to +2.33, threshold 64 at +1.32, st_r064_t000 at +1.28, threshold 96 and
127 at +0.93 to +0.95. The cause is the TPT drone, not the compressor: at bypass
(st_off_p064_v127, 1.0 to 3.9 s) ReBirth's drone has RMS -15.79, peak -7.58,
crest 8.21 dB, and ours RMS -14.85, peak -9.00, crest 5.85 dB. A peak detector
reads ours 1.42 dB low and cuts it less, so the cells that used to flatten the
+0.94 dB RMS residual now pass about +2.24 dB of source difference. Corrected
the same way as above, every compressed cell is within 0.07 dB (round 2: 0.66;
table in `specs/SPEC_COMP.md`, "Round 5 result"). Until BACKLOG 22 (the TPT saw's
crest) is fixed, a TPT 303 through a compressor at ratio 127 and threshold 0 to
64 comes out about 1.3 dB hotter against ReBirth than it did in round 2.

**The DSP's own misses, now reaching song output.** The engine now matches
`comp_wav` (the DSP alone) to 0.031 dB or better on all 152 comp-law cells
(`renders/comp-law*-2026-09-16`), so the DSP's signed-off residual reaches song
output exactly as `comp_wav` predicts; before, the engine's extra 2.85 dB of cut
masked part of it. Corrected for the drone residual, 129 of the 152 are within
1.0 dB. The 23 over it, all at threshold 0 to 16 and most at a quiet input:
- comp-law: r064_t000_v060 -2.16, r127_t000_v040 +1.46, r127_t000_v060 +1.09;
- comp-law2: bt_r127_t000 +1.09, ts_r127_t016 -1.01;
- comp-law4: gp_r096_t000 v040 -4.19, v060 -1.62, v100 +1.03;
- comp-law5: lo_r096_t000 v040 -4.19, v060 -1.61, v100 +1.02; lo_r096_t008
  v040 -2.44, v060 -3.03; lo_r096_t016 v040 -1.07, v060 -1.10, v080 -1.48;
- comp-law6: lo_r112_t000_v040 -2.13, lo_r112_t008 v040 -1.50 and v060 -1.80,
  lo_r127_t000 v040 +1.47 and v060 +1.09, lo_r127_t008_v127 -1.42,
  lo_r127_t016_v127 -1.00.

The ratio-96 threshold-0 corner is the accepted residual of the sign-off; the
others are the same low-threshold region at ratio 96 threshold 8 and 16 and at
ratio 64, 112 and 127, which the sign-off's corner wording covers only in part.
Round 5 brings all 152 within 0.03 dB through `comp_wav` (in-sample; see "The
master compressor, measured and fitted"). No DSP constant changed in the
topology work, so these are the DSP's own misses and were there before it; only
the engine's level offset that hid part of them went.

**The demo songs.** The byte-neutral set (`byte_neutral.py`, `--no-fx` bass
solos, 20 s, TPT) moves, because `--no-fx` leaves the compressor as the song sets
it. The ride commit is byte-neutral IN THAT SET only; the master-level and
detector commits move five renders (A Taste of Haste both basses, Mutilated
Species II bass 2, Trakka Rekka both, at -41 to -118 dB of rms difference) and
the section commit one more (Transfusion bass 1). New pooled hash
`6802b5aeaa82d56e02bc4ee9fe784996` (was `8a0ff5bd5c0ccd87437edc88c22483fc`); the
fader-0 / mute commit leaves it there. Thirteen of those 32 renders are silent
in their 20 s window, so the set covers less than its count suggests.

The first whole-mix figures (full rack, 90 s, 11 of 16 songs changed, most by
under 0.7 dB, Groove It Up 1.0 dB, Mutilated Species II +3.5 to +3.8 dB) were
rendered WITHOUT `--mods`, so eight songs (Alloy, Clodhopper, Fonda, Groove,
Metallicon, Mutilated, RSI, SuperNova) played on stock samples. With the songs'
mods, the first 90 s move RSI +1.06 dB (L and R), Groove It Up +1.24 L / +0.96
R and Mutilated Species II +3.44 L / +3.29 R.

Over WHOLE songs (full rack, `--mods`, TPT, per-bar RMS, L and R separately,
`5f1fe15` against this branch) the moves are much larger. The largest come from
the section compressor now compressing at ReBirth's level and its rides playing:
- Alloy Cowboy Resurrection: up to +14.03 L / +16.24 R, with 61 bars over 6 dB
  in L, 20 in R and 68 in either (the 808 section, compressor ridden; the rides
  alone give up to +9.7 dB);
- TGV's KiloMix '98: up to +12.46 dB in bars 537 to 544 (bass 1 section, ratio
  127, threshold 10, the section path), 18 bars over 6 dB in all; the rides
  alone move bars 309 to 314 (from 518 s) by up to +7.9 dB;
- Clodhopper: +9.12 dB in bars 275 to 282, from 471 s (the 909 section, ratio
  127, threshold 26: +4.8 from the rides, +4.3 from the section path);
- Groove It Up: +6.12 dB in bars 144 and 145 (the 909 section, the section
  path; its rides move nothing).
The fader-0 / mute commit moves Alloy by at most -1.41 dB in a bar, Groove It Up
by -0.36 and Clodhopper by -0.04, and leaves the rest byte-identical or within
0.01 dB.

Whole mixes now clip more where the compressor was over-cutting. Samples at
full scale over the whole song, `5f1fe15` then this branch: Mutilated Species II 0 ->
50, RSI 4 -> 36, G-Spot 733 -> 1125, Trakka Rekka 55716 -> 69360, Alloy 15903 ->
20158, TGV 46259 -> 49450, A Taste of Haste 171211 -> 193405. As a sanity check only (not a fixture), the three changed songs
in `rb338-exports/wholemix-2026-08-27` all moved toward ReBirth over 90 s: G-Spot
-24.35 -> -23.72 dBFS against ReBirth's -20.18, Trakka Rekka L -16.22 -> -15.70
against -14.52, Transfusion L -14.94 -> -14.80 against -13.87.

**The four largest movers checked against ReBirth (2026-10-01).** Fresh exports of
Alloy Cowboy, Groove It Up and KiloMix (each its own Loop) and of a scratch copy
of Clodhopper whose Loop was lengthened to 290 bars to reach its compressed
stretch (`fixtures-2026-10-01/demo-comp-check`, exports
`renders/demo-comp-check-2026-10-01`; GLOB bytes 11 and 12 are the Loop length as
a big-endian count of bars times 3, which ReBirth honoured exactly). Per-bar RMS,
L and R separately, ours full rack with `--mods` and TPT, over the bars that moved
by more than 1 dB:
- Alloy Cowboy, 125 bars: mean |ours - ReBirth| 4.94 -> 1.91 dB, worst 17.61 ->
  7.40; its 808 bars 73 to 79 were about 15 dB under ReBirth and are about 2 under.
- Groove It Up, 162 bars: 4.15 -> 2.22 dB, 150 of the 162 closer.
- Clodhopper bars 275 to 282: 13.28 -> 4.16 dB under.
- KiloMix bars 537 to 544: 13.31 -> 6.14 dB under.
Every moved bar in Clodhopper and KiloMix got closer. What is left in those two
is mostly not the topology: exported again with the compressor routed nowhere,
the uncompressed sources differ by up to 8 dB in places (other components), and
where they are close (KiloMix bars 539 and 540, Clodhopper 280 to 282) our
compressor still cuts 3 to 3.6 dB harder than ReBirth's on a source 1.6 to 2 dB
quieter. That is the detector, measured in `research/PLAN_COMP.md` "Material probe" and
open as BACKLOG 16.

**Round 5 against the same exports (verifier's measurement, 2026-10-01).** On
the KiloMix bars whose uncompressed sources match ReBirth's within 0.5 dB, the
mean |gain miss| (comp minus nocomp, ours against ReBirth's) went from 0.69 to
0.44 dB and the worst from 5.11 to 1.06. Five bar-channels got 0.3 to 0.4 dB
worse (bars 505 R, 525 R, 531 L and R, 536 R); the worst of them is now +0.93.
This is out-of-sample: no demo song was a fit input. Neither caused nor fixed by
round 5: ReBirth compresses Clodhopper bars 268 to 274 (gain -9.33 dB R at bar
268, -6.3 L / -8.0 R at bars 272 to 274) where ours is at about -0.3 / -0.4 dB,
and ours starts compressing at about bar 275 (471 s). The round-2 and round-5
builds are identical there (BACKLOG 23). Clodhopper names MSM2, so that figure
falls under the correction below.

**Correction (2026-10-04): the modded songs above are not evidence.** Alloy
Cowboy names PBE2, Clodhopper and Groove It Up name MSM2, and Reborn plays every
mod's drums as noise past the first kilobyte of each sample (BACKLOG 17;
`FORMAT_RBM.md`, "Past the first kilobyte"). So every figure in this section for
a song that names a mod (Alloy Cowboy, Clodhopper, Groove It Up, and also
Mutilated Species II, RSI, Fonda Workout, Metallicon City and SuperNova in the
whole-song lists) compares ReBirth's drums with our noise, and says nothing
about the compressor. The evidence for the topology is the fixture campaign
(`fixtures-2026-10-01/comp-rides`); among the demo songs only the stock ones
count, and of the four checked here only KiloMix, whose bars 538 to 543 are
bass 1 alone. Note too that these renders played the stock songs' drums
through our synthesis path, since `--samples` was not passed.

**Re-measured with the mods' real voices (2026-10-09).** Since the readers
unscramble mods (BACKLOG 17), the four songs were rendered again (full rack,
TPT, each song's own mod, KiloMix on `--samples samples-original`) and scored
per bar with `tools/measure/perbar.py`. With the compressor routed nowhere, ours
and ReBirth agree within 0.54 dB, L and R, in Alloy Cowboy bars 73 to 79, Groove
It Up 144 to 145, Clodhopper 275 to 282 and KiloMix 537 to 544. With it on, ours
sits 2.6 to 5.2 dB under in those bars, so our compressor cuts 2.5 to 5.7 dB
harder than ReBirth's there (the detector, BACKLOG 16). Clodhopper's bars 264 to
275 compress in both programs, ours 0.2 to 1.1 dB harder; its bars 276 to 283
take 18.7 to 21.1 dB in ReBirth and 24.4 to 27.8 in ours. These are this
branch's compressor, before round 5's detector.
