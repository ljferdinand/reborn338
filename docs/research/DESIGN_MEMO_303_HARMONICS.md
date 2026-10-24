> **PARTLY SUPERSEDED 2026-08-04.** Kept for its reasoning trail. Round 2's
> `satAsym` drive-shaper fit is recorded here as "fitted and shipped", and it was
> later VOIDED: it rested on the harmonic instrument found on 2026-07-31 to be
> miscounting, whose `find_f0()` located the fundamental only to half an FFT bin
> so every slope above harmonic 15 read too steep on both sides. `satAsym()`
> nonetheless stays in the build, on a different basis entirely: the ear check
> ran on 2026-07-31 and drive ON won all four cases. The trail for both is in
> `OPEN_DECISIONS.md` under "Resolved calls" section 1, and the one live thread,
> a level-matched rerun, is in `PLAN_303_ENVELOPE.md` section 8. Do not treat the
> numbers in Round 2 as evidence for anything without regenerating them.
>
> What survives untouched is the linearity proof, because it is a ratio over
> twelve harmonics and does not read above the instrument's crossover: ReBirth's
> 303 path is linear to 0.03 dB. That is still a real constraint on any
> explanation.

# 303 harmonic richness: where the "buzzy, farty" gap actually is

**2026-07-26.** Luke's ear-check of the 303 was that it is not *wrong*, but still
"a far cry from `detroit_303`'s buzzy, farty bass". That reframed task #29 away
from the filter's brightness *trajectory* and toward harmonic saturation. This
memo is the measurement that followed, and it relocates the problem twice.

## Method

Render the `detroit_303` fixture with our engine, compare against ReBirth's own
export of the same file. Per note, take a steady-state window 15 ms after onset,
measure the harmonic ladder h1..h9 relative to the fundamental, and take the
median over 24 notes. Alongside it: the ladder's slope in dB/octave, the crest
factor (peak/RMS), and the *inter-harmonic* floor from 2–10 kHz with ±3% masked
around every harmonic (which separates genuine harmonics from aliasing hash).

## The three numbers that matter

|  | slope dB/oct | crest | inter-harm. floor |
|---|---|---|---|
| **ReBirth** | **−4.81** | **6.99** | **−38.3 dB** |
| ours, shipping | −6.47 | 4.14 | −35.7 dB |

## Finding 1: our oscillator is textbook correct

Rendered with the filter wide open and the input drive bypassed, our sawtooth is
a mathematically ideal saw:

| | h2 | h3 | h4 | h5 | h6 | h7 | h8 | h9 |
|---|---|---|---|---|---|---|---|---|
| ideal saw (1/n) | −6.0 | −9.5 | −12.0 | −14.0 | −15.6 | −16.9 | −18.1 | −19.1 |
| ours, open + linear | −6.0 | −9.6 | −12.1 | −14.1 | −15.7 | −17.2 | −18.3 | −19.5 |

Nothing to fix in the waveform generator.

## Finding 2: our own input drive was destroying the second harmonic

The same oscillator with the drive stage engaged:

| | h2 | h3 | h4 | slope |
|---|---|---|---|---|
| ours, open + linear | −6.0 | −9.6 | −12.1 | −6.12 |
| ours, open + **drive** | **−12.1** | −13.1 | −16.4 | −6.98 |

The mechanism is not subtle. The drive stage is

```cpp
const double xin = satTanh(driveGain * DRIVE_MUL * osc);   // driveGain = 1.3 + 4.0*knob
```

`tanh` is an **odd** function. It preserves the waveform's antisymmetry, so it
cannot create even harmonics, and by flattening the saw toward a square it
actively *suppresses* the ones the saw already had. At the default drive knob
(0.20, `driveGain` = 2.1) that costs **6 dB of h2**.

So the headline deficit was self-inflicted. Backing the drive off almost closes
the ladder on its own:

| | h2 | h3 | h4 | h5 | h6 | h7 | h8 | h9 | slope | mean err |
|---|---|---|---|---|---|---|---|---|---|---|
| ReBirth | −4.1 | −7.1 | −9.3 | −11.1 | −12.3 | −13.4 | −13.9 | −14.8 | −4.81 | n/a |
| ours, drive default | −9.9 | −12.6 | −15.1 | −16.9 | −18.2 | −19.6 | −20.0 | −20.2 | −6.47 | 5.83 dB |
| ours, drive ≈ 0 | −6.0 | −9.3 | −11.5 | −13.1 | −14.1 | −14.6 | −14.8 | −14.8 | −5.07 | **1.52 dB** |

h9 lands exactly; the residual is a roughly flat −2 dB on h2..h6.

**This is not simply "turn the drive down".** The drive exists to add grind, and
at zero it costs us elsewhere (see finding 4). What the measurement says is that
the *shape* of the nonlinearity is wrong for the job: an odd function cannot
produce the even-harmonic content ReBirth has. Note that the overall even/odd
energy ratio still matched (1.30 vs 1.34), which is why this was invisible until
the ladder was measured harmonic by harmonic: the ratio hid a per-harmonic error.

## Finding 3: the real remaining gap is CREST, not spectrum

| | raw oscillator | musical patch | gain |
|---|---|---|---|
| ReBirth | 2.74 | **6.99** | **+8.1 dB** |
| ours | 2.97 | 4.14 | +2.9 dB |

ReBirth's resonance turns a fairly flat oscillator into something very peaky.
Ours barely changes. Nothing anywhere in the existing parameter space (92
renders across seven env-var sweeps) exceeded a crest of 4.48.

This *is* task #29's original question, but it now has a metric. "Prominent,
broad, stable swept resonance" was too vague to fit against. **Crest factor
4.1 → 7.0** is a target, and it is about how the resonant loop generates a
per-cycle transient rather than about the cutoff trajectory.

## Finding 4: the trade is three-way, so no single knob wins

Backing off the drive fixes the ladder but *worsens* the inter-harmonic floor
from −35.7 to −23.0 dB: the `tanh` had been masking oscillator aliasing by
removing the high harmonics that were folding. Any fix to finding 2 has to
address the floor at the same time, probably by oversampling the oscillator.

## Negative result: the RB core is a dead end for this

The forward-Euler core has an in-loop clipper, which is the topology most likely
to shallow the ladder, so it was the obvious candidate. It is not viable:

- 21 combinations of oversampling (4/8/16) × resonance gain (30…250) were
  measured. The inter-harmonic floor never got better than **−12 to −23 dB**
  against ReBirth's −38.3, and **more oversampling did not monotonically help**,
  which means the junk is not purely aliasing; it is the naive Euler structure.
- The ladder never shallowed past −5.79 dB/oct, and crest sat at 2.3–2.7.

Its "grit" is hash, not harmonics. ReBirth's floor is clean at −38.3 dB. Stop
pursuing this core for harmonic richness.

---

# Round 2 (same day): the shaper is fixed, and finding 3 was WRONG

## The asymmetric shaper, fitted and shipped

`satAsym(x, bias) = tanh(x + bias) - tanh(bias)` replaces the symmetric `tanh`
in the drive stage, with the drive law scaled to 0.6x (`0.78 + 2.4*knob`). Both
numbers come from a bias x drive grid fitted against the reference:

| | ladder err | slope | crest | floor |
|---|---|---|---|---|
| ReBirth (target) | n/a | −4.81 | 6.99 | −38.3 |
| before | 5.83 | −6.47 | 4.14 | −35.7 |
| **after** | **2.68** | **−5.68** | **4.82** | −34.4 |

Ladder error more than halves, and crest *improves* at the same time, for 1.3 dB
of floor. h2 goes −9.9 → −6.1 against the reference's −4.1.

## Finding 3 was wrong: the transient is not missing, it is ALIASED AWAY

The claim above was that our resonance produces a continuous ring rather than a
per-cycle transient, and that this needed new filter topology. That is false.
Rendering the identical patch at higher sample rates:

| render | floor | slope | **crest** | ladder err |
|---|---|---|---|---|
| ours @ 44.1 kHz | −34.4 | −5.68 | **4.82** | 2.68 |
| ours @ 88.2 kHz | −40.9 | −5.70 | **6.01** | 2.76 |
| ours @ 176.4 kHz | −39.6 | −5.76 | **6.65** | 2.90 |
| ReBirth @ 44.1 kHz | −38.3 | −4.81 | **6.99** | n/a |

Crest climbs 4.82 → 6.65 with nothing changed but the rate, and the floor
overshoots the target on the way. A sharp per-cycle spike carries
high-frequency content; at 44.1 kHz that content folds and rounds the peak off.
The resonance was generating the transient all along.

So **oversampling delivers both remaining metrics at once** (the floor of
finding 4 and the crest of finding 3), and task #29 stops being a research
problem about resonance topology. It becomes an implementation task: oversample
the oscillator + drive + ladder path and decimate.

Worth stating what this does NOT explain: ReBirth reaches crest 6.99 at 44.1 kHz
with no oversampling, where we need roughly 4x to approach it. Its filter
structure must produce the spike differently. We do not need to replicate the
structure to match the sound, but the asymmetry is a live clue if the
oversampled version still falls short.

---

# Round 3: oversampling shipped, and what it did not fix

`REBORN_TPT_OS` (default **4**) runs the oscillator, drive and TPT ladder at 4x
the host rate, updating the resonance feedback every substep. Decimation is just
"take the last substep": the ladder is a 4-pole lowpass at fc, typically well
under 5 kHz, so it has already attenuated everything near the host Nyquist by
~80 dB. It *is* the anti-imaging filter.

At a **44.1 kHz output** rate:

| | ladder err | slope | crest | floor |
|---|---|---|---|---|
| ReBirth (target) | n/a | −4.81 | **6.99** | **−38.3** |
| OS=1 | 2.68 | −5.68 | 4.82 | −34.4 |
| OS=2 | 2.75 | −5.71 | 5.89 | −40.8 |
| **OS=4 (ships)** | 2.85 | −5.77 | **6.71** | **−39.3** |
| OS=8 | 2.85 | −5.76 | 7.29 | −36.3 |

Cost is negligible: +2.4% on a full-song render, because the 303 ladder is a
small share of total engine work. It also fixed the reso-127 degeneracy: that
fixture previously produced no detectable note onsets at all.

## What oversampling did NOT fix: the resonance dependence

Oversampling added peakiness roughly **uniformly**, and the shape of the
crest-vs-resonance curve is what actually has to match:

| resonance | ReBirth crestFLAT | before | after OS=4 |
|---|---|---|---|
| 020 | **2.60** | 2.90 | 3.73 |
| 060 | 3.96 | 2.96 | 3.79 |
| 100 | **4.68** | 2.43 | 3.65 |
| 127 | 4.42 | *no notes* | 3.14 |

So low resonance now **overshoots** (3.73 vs 2.60) where it used to be close.
That is a regression introduced by this change, and it is the open item.

## Diagnosis: the resonance knob is crammed into the top of its range

`kBase = K_A + K_B*reso` ships as `2.8 + 1.1*reso`. A 4-pole TPT ladder
self-oscillates at k = 4, so the **entire knob travel sits between 70% and 97%
of the threshold**: every setting is "high resonance", which is why the curve
above is nearly flat. Lowering K_A restores the dependence:

| K_A / K_B | crestFLAT r020 | r100 | spread | detroit crest |
|---|---|---|---|---|
| 2.8 / 1.1 (ships) | 3.73 | 3.65 | −0.08 | 6.71 |
| 1.8 / 2.1 | 3.41 | 3.79 | +0.38 | 7.26 |
| 1.2 / 2.6 | 3.11 | 3.76 | +0.65 | 7.81 |
| 0.2 / 3.6 | **2.63** | 3.73 | +1.10 | 7.89 |
| ReBirth | **2.60** | **4.68** | **+2.08** | **6.99** |

And this does **not** conflict with the task #22 emphasis fit; it improves it.
Ladder error against the static exports at low resonance falls from 3.62 (K_A
2.8) to 1.40 (K_A 0.2), while the high-resonance match is unchanged at ~1.56.
The old law was simply over-resonant at low knob settings.

**K_A was deliberately NOT changed in this round.** Every candidate is a
mixed-sign trade (fixing low resonance overshoots the musical patch's crest),
no value reaches 4.68 at high resonance so the residual is structural rather
than a tuning choice, and K_A/K_B were fitted for several other verified
properties (env-mod, accent, static emphasis), of which only two were
re-checked here. It deserves its own pass against the full fixture battery
rather than a point estimate.

## Ranked next steps

1. ~~Replace the odd input shaper.~~ **DONE**, round 2.
2. ~~Oversample the oscillator + drive + ladder.~~ **DONE**, round 3.
3. **Re-fit the resonance law (K_A/K_B) against the full fixture battery.**
   The diagnosis is in hand (see above); what is missing is confirming env-mod,
   accent and emphasis do not regress. Fixes the low-resonance crest overshoot
   this round introduced.
4. **Find what caps crestFLAT at ~3.8 at high resonance.** No K_A reaches
   ReBirth's 4.68, so something else limits the transient. Candidates worth
   measuring: the `kHiCeil = 2.45` feedback cap, the `hfQ` high-frequency Q
   loss, and the `satTanh` on the feedback path: all three deliberately damp
   the resonance and all three are suspects.
5. Residual ladder gap: a roughly flat −2 dB on h2..h6, slope −5.77 against
   −4.81. Lowest priority; may move once (3) lands.

## What would need new fixtures from ReBirth

**Nothing.** This was previously listed as needing a resonance sweep at fixed
cutoff; that turned out to already exist as the eight `scratch_303_STATIC_reso*`
exports from an earlier round, which is what produced the crest-vs-resonance
curve below. No new exports are required for any remaining step.

### Crest vs resonance, from the existing static exports

Env-mod is zero in these, so resonance is the only variable. `crestFLAT` divides
out a 10 ms RMS envelope, isolating waveform spikiness from envelope decay:

| resonance | ReBirth crestFLAT | ours crestFLAT (before oversampling) |
|---|---|---|
| 020 | 2.60 | 2.90 |
| 060 | 3.96 | 2.96 |
| 100 | 4.68 | 2.43 |
| 127 | 4.42 | no notes detected |

Two things to keep in view when (2) lands. At low resonance we are already
spikier than the reference, so oversampling must not simply add peakiness
everywhere; the *slope* of this curve is what has to match. And our reso-127
render produces no detectable note onsets at all, which means the ladder is
self-oscillating into a continuous tone where ReBirth stays note-driven. That is
a separate defect from the crest gap and it needs its own fix.

## Caveat on the rawosc comparison

The `t303_rawosc_*` fixtures are cutoff-max / resonance-0, not a literal
oscillator tap. ReBirth's reads slope −8.56, steeper than any saw, so its "max
cutoff" still filters appreciably and its absolute slope is **not** comparable to
ours. The inference above rests on two things that are safe: our own open+linear
vs open+drive comparison (same fixture, same engine, one variable), and the fact
that each side's h2 is stable between its rawosc and its musical render
(ReBirth −5.7 → −4.1; ours −10.1 → −9.9), which places the deficit before the
musical filtering in both.


---

# Round 4 (2026-07-26): the crest ceiling is STRUCTURAL, and a bigger problem found

## All three named suspects are eliminated

Round 3 named the `kHiCeil = 2.45` feedback cap, the `hfQ` high-frequency Q loss
and the feedback `satTanh` as the likely causes of the crest ceiling. Measured at
reso 100 / cut 090, none of them is:

| change | crestFLAT |
|---|---|
| baseline | 3.65 |
| `kHiCeil` disabled | 3.65 (*identical*) |
| `hfQ` disabled | 3.65 (*identical*) |
| feedback drive x3 | self-oscillates, no notes |
| input saturator headroom 1 → 4 | 3.65 → **3.31** (worse) |

The first two are simply **inactive at this cutoff**: `hfQ = (fc-3000)/2500`
clamps to zero below 3 kHz and fc here is ~1660 Hz, so `kEff == k` already. The
saturator moves it the wrong way. `REBORN_KHI_CEIL` and `REBORN_U_SAT` were added
as probes to establish this and both default to existing behaviour.

## What the ceiling actually is: self-oscillation

Sweeping the loop gain directly (`K_B=0`, so `k` is fixed regardless of the knob):

| k | 0.5 | 1.0 | 1.5 | 2.0 | 2.5 | 3.0 | **3.5** | 3.8 | 3.95 |
|---|---|---|---|---|---|---|---|---|---|
| crestFLAT | 2.49 | 2.78 | 3.03 | 3.33 | 3.59 | 3.73 | **3.78** | 3.44 | 2.94 |

Crest rises to a maximum of **3.78 at k ≈ 3.5** and then FALLS toward
self-oscillation at k = 4, where the ring becomes a continuous sinusoid heading
for a pure sine's 1.41.

**ReBirth's 4.68 is therefore unreachable by this topology at any loop gain.**
Not a damper, not a tuning choice: a structural ceiling. Closing it needs a
resonant loop that can be driven harder without running away.

It also explains the shipping law's failure: `2.8 + 1.1*reso` spans 2.8–3.9,
which sits on the flat top of that curve and over its falling edge. That is why
crest did not track resonance, and why reso 127 degenerated into a continuous
tone.

## A candidate law, measured but NOT shipped

`K_A=0.0, K_B=3.5` maps the knob onto the rising region instead:

| reso | ReBirth crestFLAT | shipping | candidate |
|---|---|---|---|
| 020 | 2.60 | 3.73 | **2.50** |
| 060 | 3.96 | 3.79 | 3.13 |
| 100 | 4.68 | 3.65 | 3.68 |
| 127 | 4.42 | 3.14 | **3.78** |

The shape is right for the first time (monotone rising, capped where the
topology caps), and the low-resonance ladder error falls from 3.62 to **0.83**.

## Why it is not shipped: a 20-35 dB floor problem the musical patch was hiding

| inter-harmonic floor | ReBirth | shipping | candidate |
|---|---|---|---|
| static reso 020 | −34.8 | −15.9 | −10.7 |
| static reso 060 | −38.5 | −15.0 | −13.3 |
| static reso 100 | −43.4 | −12.6 | −15.3 |
| static reso 127 | −51.1 | −16.0 | −13.6 |
| **detroit (musical)** | −38.3 | **−39.3** | −33.9 |

On held static resonant notes our floor is **20 to 35 dB worse than ReBirth's**,
in the shipping build as much as the candidate. Every measurement in this memo
up to now used detroit, which hides it entirely: its notes are short and plucky,
so the ring decays before it accumulates.

The likely mechanism is the same family as the crest ceiling. A resonant ladder
rings at its cutoff, which is **not** harmonically related to the note, so a
sustained ring is by definition inter-harmonic energy. ReBirth's −43 dB at reso
100 says its ring stays locked to the note where ours does not, which is what
the source's existing "the formant QUANTIZES to the note" comment is reaching
for.

**So the resonance law is not re-fitted yet.** Re-fitting `K_A/K_B` now would be
tuning around a defect 20 dB larger than the one being tuned for, and detroit's
floor regresses 5.4 dB under the candidate. The floor comes first; the candidate
law is recorded above and should be re-evaluated after it.

Caveat on the comparison: the static renders used `--no-dist` and detroit did
not. That cannot explain the direction (distortion adds harmonic content, it
does not clean a floor), but the two columns are not otherwise identical.


---

# Round 5 (2026-07-26): CORRECTION to round 4, and the real spectral shape

## The "inter-harmonic floor" metric was mismeasuring

Round 4 reported a 20-35 dB "inter-harmonic floor" problem and attributed it to
an inharmonic resonant ring. **The attribution was wrong, and the metric was not
measuring what its name claimed.**

The metric masks ±3% around each harmonic and sums what survives in 2-10 kHz.
For the 110 Hz fixture note:

- ±3% of 2000 Hz is ±60 Hz against 110 Hz harmonic spacing, so the masks **tile
  the band completely** above ~1833 Hz: nothing should survive at all;
- the mask loop stops at `k = 59`, i.e. 6490 Hz, so **everything above 6.5 kHz is
  never masked**.

So 41% of the band survived, running 6685 Hz to 10 kHz. The number was
"energy above ~6.5 kHz", not an inter-harmonic floor. Direct measurement
confirms it: the loudest genuinely non-harmonic peak is no worse in our render
than in ReBirth's (−22.5 dB vs its −16.7 dB), which is the opposite of what an
excess inharmonic ring would show.

## The real shape, measured against the fundamental

A ratio metric cannot distinguish "more treble" from "less midrange". Band
energies relative to the fundamental, static, reso 100:

| | 0.8-2k | 2-4k | 4-6.5k | 6.5-10k | 10-16k |
|---|---|---|---|---|---|
| ReBirth | −1.3 | **−14.3** | −42.0 | −56.6 | −65.3 |
| ours | +0.4 | **−26.4** | −34.8 | −37.4 | −38.3 |
| difference | +1.7 | **−12.1** | +7.2 | +19.2 | **+27.0** |

It is *both* directions: **12 dB too dark at 2-4 kHz and 7-27 dB too bright above
4 kHz.** ReBirth rolls off steeply and keeps going. Ours drops steeply then
**flattens at about −38 dB and stops rolling off**.

## And the flat shelf does not respond to resonance

| 6.5-10k band | reso 020 | reso 060 | reso 100 | reso 127 |
|---|---|---|---|---|
| ReBirth | −70.9 | −63.0 | −56.6 | −52.6 |
| ours | **−38.7** | **−38.0** | **−37.4** | **−39.8** |

ReBirth's high end tracks resonance, as a resonant filter should. Ours is pinned.
The same is true at 2-4 kHz, where ReBirth spans −36.5 → −3.0 across the knob
while ours barely moves from −27.

That resonance-invariance is the **same signature as the crest ceiling**: with
`kBase = 2.8 + 1.1*reso` the knob never leaves the high-Q region, so neither the
crest nor the spectrum can respond to it. It is more evidence for the round-4
candidate law, from an independent measurement.

## Ruled out as the cause of the flat shelf

Each measured, none of them moves it:

| | 6.5-10k |
|---|---|
| current | −11.9 |
| asymmetric shaper off | −10.9 |
| oversampling off | −12.7 |
| both off | −12.8 |
| drive ≈ 0 | −11.7 |
| old drive law | −10.3 |
| output makeup ×0.5 / ×0.25 / ×0.1 | −11.9 / −11.9 / −11.8 |

(these are the ratio metric, kept for comparability within the table)

So it is **not** the drive path, **not** the asymmetric shaper, **not** the
oversampling, **not** the post-filter clipper, and **not** resonance level. It is
pre-existing and predates everything done today.

### Filter numerical precision: also ruled out

The one mechanism no gain change would alter: the ladder states are declared
`double` but were rounded to `float` every sample by `flushDenormal((float)x)`,
inside a loop with gain ~3.7. Tested by removing the narrowing entirely:

| | 0.8-2k | 2-4k | 4-6.5k | 6.5-10k | 10-16k |
|---|---|---|---|---|---|
| float states | −4.9 | −27.7 | −36.2 | −38.7 | −39.5 |
| double states | −4.9 | −27.7 | −36.2 | −38.7 | −39.5 |

**Identical to 0.1 dB.** Not the cause. (A `double` overload of `flushDenormal`
was added anyway, since silently narrowing a double is a real trap for future
code, but it is currently unused and provably changes nothing here.)

**Cause not yet identified.** What is established: a broadband component at a
roughly fixed level, unresponsive to every gain, every nonlinearity, and the
numerical precision of the filter, sitting where a 4-pole rolloff should have
reached −60 dB or lower.

What has NOT been tried, in rough order of promise:

1. **Measure the ladder output directly**, before the VCA and output stage,
   rather than inferring from the rendered file. That splits "the filter passes
   too much HF" from "something after it adds HF"; every test so far has varied
   a *gain* rather than a *tap point*.
2. Check whether the shelf is present with the oscillator replaced by a pure
   sine at f0. A 4-pole lowpass fed a sine cannot produce a −38 dB shelf at
   6.5 kHz, so if the shelf survives that, it is not coming from the input at all.
3. Check the pitch-glide code path: `freqCur_ *= pow(...)` runs every sample and
   is the only other per-sample transcendental in the voice.


---

# Round 6 (2026-07-26): FOUND IT: the plucky VCA's re-pluck was a step

## The measurement that found it: vary the TAP POINT, not a gain

Every earlier test varied a gain, and the shelf ignored all of them. Emitting the
**ladder output directly**, bypassing makeup, the VCA and the output clipper:

| | 2-4k | 4-6.5k | 6.5-10k | 10-16k |
|---|---|---|---|---|
| ReBirth | −14.3 | −42.0 | −56.6 | −65.3 |
| **raw ladder, saw** | −29.6 | **−52.8** | **−63.6** | **−62.4** |
| full chain, saw | −26.4 | −34.8 | −37.4 | −38.3 |
| **raw ladder, sine** | −70.4 | −70.1 | −69.6 | −66.8 |

The ladder is clean and rolls off properly, and fed a sine it is silent above the
fundamental exactly as a 4-pole lowpass must be. The shelf is added **after** it.

## The cause

`trigger()` snapped the VCA envelope straight to 1.0 on every struck note:

```cpp
if (ampDecSec_ > 0.0) ampEnv_ = 1.0;   // instantaneous
```

From a sustain floor of 0.08 that is a **step discontinuity in an amplitude
envelope**, which is a broadband click. It explains every observation: invariant
to drive, shaper, oversampling, makeup and resonance (because it is not a gain);
present with a pure sine input; absent from the raw ladder tap (which bypasses
the VCA); and flat across frequency, because a step is flat.

Disabling the pluck confirmed it before any fix was written: 6.5-10k went −37.4
to −69.0.

## The fix, and what it cost

A 1.2 ms attack ramp instead of a step, which band-limits the onset to ~1.3 kHz
instead of spraying to Nyquist. The pluck stays percussive.

| 6.5-10k | ReBirth | before | after |
|---|---|---|---|
| reso 020 | −70.9 | −38.7 | **−66.6** |
| reso 100 | −56.6 | −37.4 | **−65.9** |
| reso 127 | −52.6 | −39.8 | **−65.7** |

About 28 dB across the whole sweep, and 10-16 kHz improves comparably.

Cost, on the detroit musical patch: crest 6.71 → 6.43 against a 6.99 target, and
ladder error 2.85 → 2.93. The click had been *inflating* the measured crest,
which is worth remembering: some of round 3's crest win was this artifact.
Trading 0.28 of crest for 28 dB of broadband hash is not a close call.

## What this unblocks, and what remains

Two things are now clear and separable:

1. **The K_A/K_B re-fit is no longer blocked.** The reason for holding it was
   that it would be tuning around a defect 20 dB larger. That defect is fixed.
2. **Above 4 kHz we now overshoot into too-dark** at high resonance (reso 100:
   ours −53.0 / −65.9 against ReBirth's −42.0 / −56.6), and 2-4 kHz is still
   ~15 dB dark. Both are the resonance-response problem (ReBirth's spectrum
   tracks the knob and ours barely does), which is exactly what the candidate
   law addresses. Do that next.

Two probes are kept, both defaulting to off: `REBORN_RAW_TAP` (emit the ladder
output directly) and `REBORN_OSC_SINE` (pure sine input). They are what found
this, and the lesson generalises: when a defect ignores every gain, stop varying
gains and start moving the measurement point.
