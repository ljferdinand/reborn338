# Backlog: findings outside the current supertask

This is the register of known defects and open items as the project closed
(2026-10). `docs/HANDOVER.md` puts the most useful of them in order.

The deep-dive rule is that one component changes at a time, so a defect found
while measuring something else must not be fixed on the spot. This is where it
goes instead, so that "we know about it" survives the session that found it.

**What belongs here.** Anything measured or reasoned to a conclusion that we are
not acting on now: a defect in another component, a control we decode and
discard, an unconfirmed inference, a code hazard that cost time.

**What does not.** Anything already fixed (that lives in the component's own
findings doc), and anything we merely suspect and have not looked at. A hunch is
not a backlog item; write it down when it has a number attached.

**Who clears it.** Each entry names the supertask that owns it. Luke decides
whether an entry is worth pulling forward.

---

## 1. The delay's ping-pong causes a 3.3 dB whole-mix stereo skew

**Owner: supertask 5, the delay. Diagnosed 2026-08-27. RESOLVED by the delay
supertask (merged 2026-09-10): the delay is a mono echo returned through the
mixer pan law, with no ping-pong (`src/effects/delay.cpp`). The entry is kept
as the record of the diagnosis.**

Severity: this is the largest whole-mix error we can currently measure, it
affects every song that uses the delay, and the fix is one line.

### What is wrong

`delay.cpp` has a ping-pong parameter that **defaults to fully on and is never
set by anything**. At full ping-pong the input is summed to mono and fed into
the LEFT line only, with the feedback crossed:

```
il = inL + (mono - inL) * pp     // pp = 1 -> mono into the left line
ir = inR * (1.0f - pp);          // pp = 1 -> nothing into the right line
```

so the first repeat is left-only and, with feedback below unity, every later
repeat leaves the left side ahead. The wet return measures **10.81 dB
left-biased** with an L/R correlation of -0.375.

ReBirth has no ping-pong. Appendix C page 198 gives the delay four controls:
Steps 100, Triplet 101, **Pan 102**, Feedback 103, and the chapter 15 reference
calls the third "the panning of the output from the delay". It is an output pan
on a symmetric delay, not a crossed one.

### The evidence

Trakka Rekka, full length, our render against ReBirth's own export:

| | ours, shipped | ours, ping-pong off | ReBirth |
|---|---|---|---|
| whole mix skew | +3.13 dB | **-0.18 dB** | -0.62 dB |
| 909 solo skew | +2.36 dB | -1.02 dB | -2.00 dB |
| the wet return alone | +10.81 dB | +0.99 dB | |

Turning ping-pong off moves the whole mix from 3.75 dB away from ReBirth to
0.44 dB away.

Isolation, in the order it was done, because two of these steps were dead ends
worth not repeating:

1. Our `--no-fx` solo renders already match ReBirth's solo exports: bass 1
   -0.01 against -0.04, bass 2 +0.94 against +0.79. So the voices and the pan
   law are not implicated.
2. Enabling the effect path swings the drums by 3.2 to 3.6 dB, so it is the
   rack, not the sources.
3. `--no-dist` changes nothing, so it is not the distortion.
4. Subtracting the sends-off render from the sends-on render isolates the wet
   return, which is valid here because OUR renders are deterministic even though
   ReBirth's are not. That is what showed the 10.81 dB.

### The fix, when supertask 5 comes round

1. Decode `DELY` +4 as the output pan and +2 as the triplet switch. Both are
   read today and thrown away.
2. Delete the ping-pong, or default it off and leave it as an advanced extra.
   It has no ReBirth control behind it, which by the standing rule makes it a
   phantom rather than a feature.
3. Apply the measured pan law to the delay's wet return using the decoded pan
   byte, rather than returning it centred as we do now.
4. Re-measure the whole-mix skew on all sixteen demo songs against
   `renders/nofxexports/`, not just Trakka Rekka. One song is a diagnosis, not
   a regression suite.

**More evidence, 2026-08-27.** Six full demo songs exported with the rack on
show the skew tracking the delay directly: the three that send to it from three
or four channels skew +3.79, +3.10 and +1.52 dB where ReBirth is at -1.18, -0.62
and +0.50, while the three sending from one or two channels skew +0.22, +0.15
and +0.00. More sources into a return that sums them to mono and puts them in
the left line means more left bias, which is what the ping-pong does.

**Pulling it forward is defensible.** It is one line to default it off, the
improvement is measured at 3.3 dB, and it is independent of everything else in
supertask 5. The argument against is that a change this large should land with
the rest of the delay's measurement so the whole return path is fitted once.

---

## 2. Our distortion attenuates 19 dB even when it is switched to no distortion

**Owner: supertask 7. Measured 2026-08-28. This one is blocking.**

> **2026-09-18 STATUS (most of this entry is now RESOLVED by the distortion
> supertask; the body below is the historical record).** The `distortion.cpp`
> `tanh` this entry dissects is gone, replaced by a fitted wavefolder whose
> CHARACTER Luke signed off 2026-09-18 (docs/research/PLAN_DIST.md), so the 25-38 dB
> harmonic shortfall and the closing-top-end mechanism above are no longer the
> state of the code. Amount 0 is now an EXACT bypass (byte-identical), so the
> "16.25/23.45 dB loss at amount 0" is fixed. The pan-law / `distBus_` sub-item
> below is fixed too: the per-section rewrite removed the buses, every section is
> panned once through `panGains`, guarded by `test_distortion_is_panned_per_section`.
> The `level` param is retired.
>
> **2026-09-18: this entry is now RESOLVED.** The last open piece, the absolute
> output/gain law, is fitted. Role A measured the output-level surface
> (`tools/measure/dist_gain.py`, `docs/dist_gain_target.csv`) and the walled
> implementer fit `outGain(amount,shape) = clamp(kOutBase*exp(kOutAmt*amount +
> kOutShp*shape))` from that data (`docs/specs/SPEC_DIST_GAIN.md`), replacing the
> `kOutTrim` constant. Verified: median |gap_dB| across the amount>0 grid
> 4.62 -> 1.99, the systematic -8 dB high-drive overshoot gone; the ~2 dB
> residual scatter is the irreducible cell-to-cell aliasing chaos of a hard-driven
> fold (a trend-level fit, documented as such). Character unchanged (pure output
> scalar), Amount 0 exact bypass. What remains is Luke's sign-off and a whole-mix
> regression, not a code defect.

> **2026-08-29: this is now the largest identified contributor to the whole-mix
> figure.** Honouring the per-section switch's automation put our distortion
> where ReBirth puts it, and because its CHARACTER is 25 to 38 dB short in the
> top three octaves it now costs more than it earns: G-Spot scores 1.30 frozen at
> its opening state, 2.84 performed correctly, and 2.26 with our distortion taken
> out of the song altogether. Its ladder will need to cross the amount knob with
> the input LEVEL, because a waveshaper's behaviour depends on how hard it is
> driven, unlike a filter's.

Severity: it makes any song with a distorted section unlistenable, it is why
Luke reported Calm's 303 as "practically inaudible", and it is one constant.

`distortion.cpp` computes

    outGain = (level / 0.7) * comp * trim * 0.115

At amount 0 both `comp` and `trim` are exactly 1 and `level` is its 0.7 default,
so the output gain is **0.115, which is -18.8 dB**, applied unconditionally
across the whole range. The comment says the 0.115 was "level-calibrated against
an original-program solo export of a distorted channel", so it was measured, but
at some non-zero amount and then frozen as a constant.

**ReBirth at amount 0 is transparent.** A two-section fixture with the
distortion routed to both and its amount at zero, exported from ReBirth,
loses 0.21 dB on the 303 and 0.00 dB on the 808. Ours loses 16.25 dB and
23.45 dB. The difference from 18.8 is the pan law below, which pulls the other
way by 3.01 dB.

Whatever the real loss is, it has to be **unity at amount 0** and grow with the
knob. Measuring the curve is a small amount-sweep and belongs to supertask 7.

**The character is the bigger half, and it is now measured.** Luke's ear on
2026-08-28: with the level fixed, our distorted 303 "doesn't have the distortion
present on the rebirth version". It does not, by a lot. Normalising level away
and comparing octave-band SHAPE across the seventeen-point sweep, our spectrum
against ReBirth's:

| band Hz | 56 | 113 | 226 | 452 | 894 | 1767 | 3535 | 7071 | 13416 |
|---|---|---|---|---|---|---|---|---|---|
| mean dB | +1.6 | -0.3 | +2.6 | -6.9 | -9.2 | -12.6 | **-24.8** | **-38.4** | **-35.2** |

The undistorted control of the same song is flat to within 1.8 dB in every band,
so this is the distortion and not the 303 underneath. **Our distortion generates
far too little harmonic content**: ReBirth lifts 2 to 12 kHz by 17 to 31 dB at
every setting, and we do not follow. The gap widens with drive, reaching -55 dB
in the top band at bytes 80 and above.

A LIKELY MECHANISM, not yet a measurement. `distortion.cpp` sets its post
low-pass to `9000 - amount * 4000` Hz, so our top end closes from 9 kHz down to
5 kHz as the drive rises. ReBirth's opens. That single line is the first thing
to check, and the comment above it says it was "reference-tuned" against a solo
export, which is the same kind of once-measured-then-frozen constant as the
output trim was.

**~~It also skips the pan law.~~ FIXED 2026-09-18.** This described the old
parallel-bus design: channels summed into `distBus_` without the pan gains the
dry path applied, so a distorted section was 3.01 dB hot and lost its pan
position, the same defect the PCF bus had. The per-section rewrite (PCF
supertask) removed `distBus_` entirely; each section carries its own chain and is
panned once through `panGains`. Verified in audio by
`test_distortion_is_panned_per_section`.

**Why this only surfaced now.** Until 2026-08-27 the loader read `MIXR` +1 as
the distortion's target when it is the compressor's, so the distortion was
pointed at the wrong section in fifteen of sixteen songs. Correcting the routing
put a second section through a unit that had been losing 21 dB on the first one
all along. Calm went from one quiet 303 to two inaudible ones.

**And the whole-mix figure is now much more sensitive to it, 2026-08-29.**
Honouring the per-section switch's automation (5b, fixed) moved the six-song
octave error from 1.76 to 2.13 dB while the level error improved from 1.94 to
1.69. That is the correct change scoring worse, and G-Spot shows why: frozen at
its opening state it scores 1.30, performed correctly 2.84, and **with our
distortion removed from the song entirely 2.26**. So the good number came from
freezing the unit into the one configuration that happened to flatter it, not
from freezing being right. Until this entry is fixed, our distortion costs more
than it earns wherever it runs, and the whole-mix number now says so.

---

## 2b. ~~Is ReBirth's distortion one shared unit or one per section?~~ ANSWERED

**Measured 2026-08-28: per section. Closed.**

Two 303s at 65.4 and 92.5 Hz, both routed to the distortion at drive 110. If
they shared a waveshaper their intermodulation products would appear at
frequencies neither carries alone. Across fourteen such frequencies the excess
over the power sum of each played alone is **-0.1 dB**. They do not meet.

Paired with the other topology fixture, where the PCF shut on one of two
distorted sections removes 15.2 dB from it and 0.01 dB from the other, the rack
is one set of knobs over four independent signal paths. The engine is built that
way now. The original text follows.

**Owner: supertask 7. Raised 2026-08-27, never measured.**

It matters now. The PCF is a series insert after the distortion, and five demo
songs route it to a section that is also distorted, so that section has to be
distorted on its own before the filter sees it. Our distortion carries
per-channel filter state, so that needs a SECOND instance, which is what the
engine does.

But a shared unit and two instances are not the same thing. With one unit, two
distorted sections intermodulate inside the waveshaper; with two they do not.

**Partly answered 2026-08-28, and the PCF half is settled.** Luke's point stands
that ReBirth presents ONE of each unit and routes sections through it, so the
unit count is not per-section. But the behaviour that matters here was measured
directly: a two-section fixture with BOTH sections distorted and the PCF slammed
shut on one of them. ReBirth removes 15.2 dB from the section the PCF is on and
**0.01 dB from the other**. So whatever the unit count, the PCF filters only its
own section and not the distortion bus as a whole, and our per-section handling
of it is behaviourally right.

What is still unmeasured is the intermodulation question: whether two sections
distorted together in ReBirth interact inside the waveshaper. The fixture for it
is the same shape, with a non-zero distortion amount, which the 2026-08-28 set
did not have.

---

## 3. The delay decodes two controls and discards them

**Owner: supertask 5. RESOLVED by the delay supertask (2026-09-10): the
straight/triplet switch and the return pan both reach the delay, from the chunk
and from the DELY track (`src/engine/engine.cpp`).**

`DELY` +2 is the straight/triplet switch and +4 is the output pan. Both are read
into `EffectInit` and neither reaches the delay. The pan affects ten of the
sixteen shipped songs, and it is the same field entry 1 needs.

Our delay also carries a `tone` control, a high-cut inside the feedback path,
which ReBirth does not expose. Same phantom question as the ping-pong.

---

## 4. ~~"Compressor target 1 = master" is inference~~ CONFIRMED 2026-08-28

Clicking the MASTER strip's own COMP button wrote **1** to `MIXR` +1 and
REPLACED the section value that was there, so the encoding is 0 = off,
1 = master, 2..5 = sections 1..4, all mutually exclusive. Decoded and applied.

The earlier attempt failed because the first click after the window takes focus
gets eaten; a warm-up click on dead panel space fixes it, and the (unpublished)
click tool did that from then on. The original text follows.

## 4b. The original inference

**Owner: supertask 6, the compressor.**

`MIXR` +1 is the compressor's target, and clicking COMP on section 1 wrote 2
while section 4 wrote 5, so the encoding is the 1-based section index plus one.
That leaves 1 for the master strip, which has its own COMP button on the panel.
The click that would have confirmed it did not register and has not been retried.

Retrying it costs one ReBirth session: click the master strip's COMP button,
save, and read the byte.

---

## 5. ~~The compressor's timed rides are carried, not played~~ FIXED (the delay's 2026-09-26, the compressor's 2026-10-01)

**Owner: closed. The delay half closed 2026-09-26, the COMP half 2026-10-01
(decision 3 of 2026-09-27). Kept for the evidence.**

The PCF's rides are decoded and dispatched as of 2026-08-27, and the PCF's and
compressor's ROUTING as of 2026-08-29. **The DISTORTION's own knob rides are now
decoded too (2026-09-18, item 4):** `rbs.cpp` reads the DIST track's Amount/Shape/
on-off timed events as `AutoTarget::Dist` and the engine dispatches them to the
unit the four per-section instances mirror. Measured heavy use: Mutilated
Species II 1241 Amount rides + 319 Shape, and several songs start at Amount 0 on
the track and ride up, so they used to render with the distortion off entirely.
STILL UNREAD: the DELAY's and the COMPRESSOR's own knob rides (tick-0 dumps only).
The pattern is in `rbs.cpp`, where the PCF and DIST tracks push their events and
the engine dispatches them by param number.

**2026-09-26:** both are now read and CARRIED, not played. The loader keeps
every DELY track event (tick 0 included) as `AutoTarget::Delay` and the COMP
track's timed rides as `AutoTarget::Comp`, so a save writes them back (review
W1), and `applyAutoEvent` ignores both. The delay still plays its chunk and the
compressor its tick-0 dump. Playing them is a parity change awaiting sign-off.

**2026-09-26, later: the DELAY half is FIXED** (review E3, approved by Luke,
`57c7cfb`). Measured on four isolated fixtures, ReBirth's Song mode plays the
DELY track, not the chunk, mod or no mod. The loader now reads the track's tick-0
dump into `effectInit` as the delay's start (a new `EffectInit::delayOn` from
param 0), the chunk stays the panel, and the engine plays the 4,435 timed rides
(`AutoTarget::Delay`: param 0 the unit bypass, 1 to 4 steps, grid, feedback and
pan). Record in `PARITY.md` and `research/FINDINGS_AUDIT.md`. **Still open: the COMP
track's 156 timed rides**, carried as `AutoTarget::Comp` and not applied, a
separate parity decision pending with Luke.

**2026-10-01: the COMP half is FIXED** (Luke's decision 3 of 2026-09-27: probe
it, then play it if ReBirth does). `fixtures-2026-10-01/comp-rides` (one 303
drone, the master compressor, one ride at tick 64, `gen_comp_rides.py`,
`tools/measure/comp_rides.py`): ReBirth plays every ride, and each lands within
0.03 dB of the static export at its end setting (cr_ratio_up -15.79 -> -32.23
dBFS, cr_ratio_dn -32.21 -> -15.79, cr_thr_up -32.21 -> -18.77, cr_thr_dn -18.75
-> -32.22). `applyAutoEvent` now plays params 0 (the unit's on/off), 1 (Ratio) and
2 (Threshold) and ignores 3 (the meter). The 156 rides in the demo songs are 65 on
Ratio and 91 on Threshold, none on params 0 or 3. The same probe found three
integration defects in the compressor, fixed with it: `PARITY.md`, "The
compressor's rides and topology". Ride timing is BACKLOG 18.

### 5b. ~~The per-section DISTORTION switch is the same defect the PCF just had~~ FIXED

**Fixed 2026-08-29 at Luke's call, alongside the PCF's.** Kept for the evidence.
`base+3` takes only the values 0 and 1 across all sixteen songs and 424 events,
so it is a switch and not a knob. Routing and the unit's own bypass were also
folded together at load time (`distEnable = distortionOn && distOn[ch]`), which
an automated switch would have walked straight past; they are separate now, the
same way the PCF's are.

Songs perform it, exactly as they perform the PCF's routing: **twelve of the
sixteen demo songs move the mixer track's `base+3` after tick 0, 360 events in
all**, from one apiece in A Taste of Haste and Calm up to 279 in Mutilated
Species II, 26 in KiloMix and 20 in Alloy Cowboy Resurrection. The loader
already decodes it and pushes it as `AutoTarget::Mixer`; `applyAutoEvent` has no
case for it, so every one of those songs distorts whatever it distorts at the
downbeat for its entire length.

This is the identical shape of bug that made our drums dull, in the same
dispatcher, found while fixing that one. It is left here rather than fixed on
the spot only because the deep-dive rule puts the distortion in its own
supertask. The fix mirrors the PCF's: a `base + 3` case calling
`setDistEnable(ch, value != 0)`.

---

## 6. Effect classes duplicate their parameter defaults

**Owner: nobody yet. A code hazard, not a parity defect.**

Every effect declares its defaults twice: once in the `kParams` description
table and once in the `p_` array initialiser. Nothing keeps them in step.

This cost real time on 2026-08-27. A test of whether ping-pong caused the stereo
skew edited the `kParams` entry, rebuilt, and produced output identical to the
last decimal, which reads exactly like a stale binary. The binary was fine; the
edited table simply is not what sets the running value. The same shape exists in
`pcf.cpp`, `delay.cpp` and the others.

Worth one pass that makes the array initialise from the table.

---

## 7. The 303 is reopened, and three of the four effects sit on it

**Owner: supertask 1. Closed: the 303 was signed off again on 2026-09-05
(`docs/COMPONENT_MATRIX.md`), and the effects were then measured on it.**

Not a backlog item in the usual sense, recorded here because it changes what
every OTHER entry means. On a clean single-303 comparison our harmonics run 10 to
20 dB weaker than ReBirth's from 86 Hz up, and batches B1 to B3 measured five
distinct defects. `docs/research/BASS303_FINDINGS.md` is the worklist.

Consequence for the effects: the distortion, the delay and the PCF all sit on a
303 in every shipped song, so **an effect measured or judged through our 303 is
measured through a source that is substantially wrong.** Build effect tests on
the 909 in sample mode instead, which nulls at 0.13 dB. That is sound for the
linear effects; the distortion and the compressor are non-linear and will
eventually need a 303 under them, which is another reason the 303 comes first.

---

## 8. The compressor's chunk is not parsed at all

**Owner: supertask 6.**

**Status 2026-09-26:** out of date. The COMP track's ratio and threshold were
decoded 2026-09-16 (supertask 6) and are what the compressor plays, and the
chunk (`[ver][ratio][threshold][meter]`) is now parsed too, into the save-time
panel only (`Song::effectPanel`), so a save writes it back. The original text
follows.

Recorded 2026-08-29 while planning the isolation batches, because it BLOCKS one
of them. `COMP` is not decoded, so we cannot even read a song's ratio and
threshold, let alone apply them. The compressor batch of
`docs/research/PLAN_ISOLATION_TESTS.md` cannot be built until it is.

Every ReBirth reference export to date was made with the compressor switched off,
deliberately, and every demo song switches it ON. So the whole-mix figure has
compared our uncompressed render against ReBirth's compressed one for the entire
life of this project, and will keep doing so until this is fixed.

## 9. Parser hostile-input hardening, and an ASan job to keep it

**Owner: cross-cutting. No supertask, which is why it is here.**

Raised by the external review 2026-08-31 (item 2), which proved three
out-of-bounds reads under AddressSanitizer from crafted `.rbm`, `.rbs` and `.wav`
files, all reachable from the plugin, which is exactly the surface handed hostile
input by another user.

**The three FIXES are done this session** (commit on `pcf-supertask`): the SSND
`dataOffset` underflow in `rbm.cpp`, the unbounded `parseTrak` reserve in
`rbs.cpp`, and the truncated-chunk over-read in `wav.cpp`. Three regression tests
in `test_main.cpp` pin the return-empty contract.

**What is NOT done, and is the owned work here:** those tests are hard guards
only under a sanitizer. On a bare build the pre-fix failure is UB the build may
absorb, or a 34 GB reserve a large-pagefile machine can satisfy, so a revert can
pass the tests. The fix is a dedicated CI job that builds `reborn_core` and the
suite with `-fsanitize=address` on Linux and runs `ctest`. The reviewer ran the
core suite clean under ASan post-fix, so address-only should be green; hold off
on `undefined` until the DSP is checked, since a signed shift or the like could
make it red for reasons unrelated to the parsers. A small hostile-file corpus
fuzzer could live in the same job later. This depends on the Linux build being
green, which the Open303 build fix (review item 11) just addressed.

## 10. The plugin allocates on the audio thread

**Owner: cross-cutting / plugin. Harmless in `reborn_render`, a real-time risk
in the VST3 and CLAP builds.**

Raised by the external review 2026-08-31 (items 6 and 7), confirmed in source.

`Processor::paramIndex` (`processor.h:44`) walks every parameter and constructs a
`ParamInfo` by value per step, each holding four `std::string`s. `setParamByName`
runs from `Engine::process` via `dispatchAutomation`, so every automation event
heap-allocates on the audio thread, and the PCF is the heaviest user of that path
(six name lookups' worth of surface, engine.cpp:527 to 534, dispatched as often
as a song rides the unit). Separately, `StereoBlock::resize` calls
`left.assign(n, 0)` which reallocates, and both `PluginProcessor::processBlock`
(`app/src/PluginProcessor.cpp:393`) and `Engine::process` (`engine.cpp:857`, 859)
can hit it when the host hands over a larger block than `prepareToPlay` expected.

Fixes: resolve parameter names to indices once at load and cache them, or add a
non-allocating `paramInfoRef(int)` over a static table for the audio path and
keep the string-returning version for the UI; and reserve generously in
`prepareToPlay` then clamp or loop so the audio thread never resizes. This sits
alongside entry 6, the other `kParams`-shaped hazard.

## 11. PCF review items, held for the supertask-4 closeout

**Owner: supertask 4.** Luke's call 2026-08-31: leave these in the backlog and
stay on the 303. The full review with reproduction harnesses is in
`docs/research/reviews/2026-08-31/`. The cross-cutting items from that review are already
filed above (parsers as entry 9, audio-thread allocation as entry 10, and the
Open303 build fix is done). What remains is PCF-specific and touches `pcf.cpp` or
`engine.cpp`, so under the one-component rule it waits for supertask 4 rather
than being done piecemeal while the 303 is live.

- **The one PARITY item, and it needs a ReBirth export.** The PCF envelope
  freezes while the unit is out of the path: `onStep` (`pcf.cpp:226`) refires
  `env_` on every sixteenth, but only `process` decays it (`pcf.cpp:289`), and
  the engine calls `onStep` unconditionally (`engine.cpp:763`) while calling
  `process` only when routed and on (`engine.cpp:816`). So while bypassed, `env_`
  pins at the last trigger's velocity and the filter comes back wide open on
  re-entry. Verified in-source. **But the reviewer had no ReBirth references, so
  the correct re-entry behavior (decay while bypassed, carry state with the
  routing, or reset) is a hypothesis, not a measurement.** Fix structurally with
  an unconditional `tick(frames)` on `Effect`, not the review's "decay in onStep"
  shortcut, which misses the in-between 32nd trigger. Choose the behavior against
  a bypass fixture measured against ReBirth. Lands under Luke's ear before
  supertask 4 is signed off.
- **`pcfBandpass` is inverted and audio is correct only via a double inversion.**
  `pattern.h:282` and `rbs.cpp:535` comments state the retired polarity; the
  loader stores the raw byte under the inverted name (`rbs.cpp:546`) and the
  engine re-inverts at `engine.cpp:222` and `:534`. The round-trip test and
  writer both encode the inverted name, so the suite is blind to it. **Fix as a
  neutral rename to `pcfModeByte` plus comment fixes plus a bandpass-shape test.
  Do NOT correct the sense** (`pcfLowpass` with the loader flipped): that forces
  `rbs_write.cpp:284`, both engine sites and `test_main.cpp:424` to flip in
  lockstep and carries the exact flip risk the reviewer warns of, for no gain.
- **PCF law tests (the review's highest-value item, item 8).** None of the
  measured laws are covered: decay quantisation, mode polarity, the free-running
  clock, no-trigger-on-first-tick, the preset table's invariants. `decayTau` and
  `kPcfPresets` are file-local statics in `pcf.cpp`, so this needs a small
  decision at closeout: expose them for direct unit tests, or test through
  `process()`. Harnesses 2 and 3 in the review folder are ready to adapt.
- **Resonance de-zippering (items 4, 5).** `kSm_` is smoothed per sample but the
  input gain and corner read the raw knob (`pcf.cpp:271`, `278`, `295`), so one Q
  event steps the corner up to 0.25 octaves within a sample, audible only at high
  Freq. `distortion.cpp` already smooths every derived quantity and is the
  pattern to copy. NOTE this is not a pure no-op: it changes transient behavior
  on any Q-automated song, so verify on a Q-automated fixture, not just
  `byte_neutral.py` on static ones.
- **Tidy, no behavior change (items 10, 12).** Dead `pcfPat_` /
  `selectPcfPattern` / `SongStep::pcfPattern` (written five places, read none);
  the retired `C_MOD` param still in the enum and `kParams` but read nowhere; the
  stale `pcf.cpp` header comment describing the pre-supertask design; a stray
  `(void)0` at `engine.cpp:208`; `knobSlew_` initialised to a coefficient in a
  milliseconds slot; and a missing `break` in `fireTick`'s ascending scan.

## 12. ~~PCF resonance-vs-cutoff interaction, and a ~0.1-octave base-cutoff offset at high Q~~ RETIRED 2026-09-05: a measurement artifact

**RETIRED the same day it was filed.** Pushing into the refinement, the first
step was to confirm the residual is real before touching the cutoff law, and it
is not. The round-11 broadband drum probe is unreliable for the static transfer
(its transient/noise spectrum biases the filtered-over-dry ratio +6 to +10 dB in
the rolloff at EVERY resonance including Q 0), and the 2-pole fit absorbs that
source/shape difference as a phantom corner offset. Measured cleanly on the 303
drone harmonic-by-harmonic (passband-normalised, current build), ours matches
ReBirth to 0.14 / 0.24 / 0.38 dB rms at Freq 64 Q 0 / 64 / 96, and the resonant
PEAK matches (Q 96, 382 Hz: ReBirth +12.4 dB, ours +12.8). No cutoff-law or Q-law
change is warranted. Full working in `research/PCF_FINDINGS.md` §11 resolution. The
original entry follows for the record.

Owner: supertask 4 (PCF), the last static-filter residual. Found 2026-09-05 in
round 11 (`fixtures-2026-09-05/pcf-round11`, `renders/pcf-round11-2026-09-05`,
`research/PCF_FINDINGS.md` §11), with a broadband probe and a 2-pole fit that resolves the
corner from the resonance where the round-2 303 drone could not.

### What is wrong

Two things at high resonance, both small and both concentrated where demo songs
sit rarely:

1. **A base-cutoff offset.** At Freq 64, our fitted corner sits about 5 to 8
   percent (roughly 0.1 octave) below ReBirth's across the whole Q sweep (347 vs
   379 Hz at Q 0, 415 vs 441 at Q 112). The corner DRIFT with Q is right (ours
   models it, 13 to 16 percent of fc); the base is a touch low. The drone missed
   it because its harmonics sit mostly in the flat passband.
2. **A resonance-vs-cutoff interaction the single Q(knob) law does not capture.**
   For the same Q byte, ReBirth's fitted Q is higher at Freq 96 than at Freq 64,
   so the resonance strength depends on the cutoff. Our `1/Q = 0.1055 + 1.3378 *
   (1-r)^1.77` is cutoff-independent. This is the bulk of the old "2.15 dB at Q
   96" residual.

### What it is not

The corner drift with resonance (once suspected) is real and already modelled;
the byte extremes are resolved (Q saturates at byte 112, bottom cutoff bytes are
below any drum probe's floor). This entry is only the high-Q fc/Q fine structure.

### The fix, if it is pulled forward

Export a Freq x Q PLANE (a Q sweep at three or four cutoffs), fit (fc, Q) per
cell, and refit two laws together: a small base-cutoff correction and a
cutoff-dependent Q. Re-measure the static filter after, and check the passband
(currently 0.49 dB mean) did not regress. One export round plus a `pcf.cpp` law
change. Whether it is worth doing before the whole-path assessment is Luke's
call; the residual lives at high resonance, which the demo songs use sparingly.

## 13. ~~PCF bandpass level is scaled correctly only at low resonance~~ FIXED 2026-09-06

**FIXED 2026-09-06 (`pcf.cpp`, kBpTrim).** ReBirth's static bandpass across
resonance (drone, `renders/pcf-bp-2026-09-06`) confirmed the BP peak GROWS with Q
(+5 dB at Q 0 to +15 dB at Q 112), about 3 dB above its own lowpass resonant
peak. The `k * kBpTrim` scaling cancelled that growth; a CONSTANT gain (1.53)
matches ReBirth's BP peak to ~0.3 dB across Q 0..96 (worst +0.66 at Q 112) and
the full BP curve to 0.28-0.37 dB rms. Original entry follows for the record. The
dynamic-makeup half of the note below (ours ~4 dB quiet when the envelope runs)
is confirmed and being worked next; it affects LP and BP alike.

Owner: supertask 4 (PCF). Found 2026-09-05 building the ear-listening set
(`listen-pcf-2026-09-05`): the bandpass fixture (pattern 16, Q 90, Amt 90) is
about 18 dB quieter than ReBirth (whole-file RMS -48.6 vs -30.9). This is a
code-level bug, not a metric artifact.

### What is wrong

`pcf.cpp:309` scales the bandpass tap `bpAmp = k * kBpTrim`, where `k = 1/Q`.
That was fit against the round-6 static bandpass fixtures, which were all at Q 0
(k ~1.44), and it is right there. But the raw TPT bandpass tap `v1` peaks at
about 1/k (= Q) at resonance, so multiplying by k normalises the resonant peak
to unity at EVERY Q. ReBirth keeps a tall resonant peak in bandpass (its lowpass
peak at Q 90 is +12 dB, matched by ours, and the bandpass taps the same pole), so
at high Q ours is roughly 20*log10(Q) below ReBirth: ~12 to 18 dB.

### The fix

Export a ReBirth STATIC bandpass sweep across resonance (Q 0, 32, 64, 96, 112 at
a fixed Freq, Amt 0) and refit `bpAmp` as a function of k so the bandpass peak
tracks ReBirth's across the whole resonance range, not just at Q 0. The lowpass
tap is unaffected (it already matches to 0.3 dB across Q). One export round plus a
one-line law change.

### Related, ear-pending: the swept LP patterns may run dark and quiet

The same listening set's LP patterns metered ~1 to 3 dB quieter and darker than
ReBirth (2.5 to 3.2 dB rms per octave, more low-mid and less top), while the
static filter, the decay law and the null all match. If Luke's ear confirms it,
the suspect is the makeup gain when the envelope sweeps: the +4.33 dB open-filter
gain (PCF_FINDINGS 3) was measured at Amt 0 and may not hold at Amt > 0. Held
here pending the ear, because metrics on transient drum material have proved
unreliable this session (the static transfer and the 2-pole fit both misled).

## 14. PCF swept-resonance brightness and trigger-density level deficit (accepted residuals)

Owner: supertask 4 (PCF). Accepted residuals from the 2026-09-07 swept-resonance
investigation (`exp/pcf-resonance`, `research/PCF_FINDINGS.md` §13, FINDINGS_AUDIT
2026-09-07). Recorded here so a future session does not re-run the exhausted fits.

- **Swept brightness**: ReBirth's resonant peak rides the fast cutoff sweep down
  as a bright curve; ours is duller (3-5 dB at 2-11 kHz on high-Amt low-base
  sweeps). It is a cutoff-POSITION difference (ours over-opens the cutoff at the
  attack), NOT resonance dynamics; a resonance-core rewrite was built and
  falsified. Makeup, cutoff-law top, envelope depth-scale and depth-offset all
  ruled out or non-converging.
- **Trigger-density deficit**: dense sixteenth/32nd PCF patterns run ~2.8 dB
  quieter than ReBirth (sparse patterns fine), a retrigger/attack effect;
  character (centroid) otherwise matches.

If reopened, the only untried lever is a full envelope+filter co-model that
re-derives ReBirth's exact swept cutoff trajectory shape (large, uncertain, the
303-round-chirp precedent). Do NOT re-run the makeup / cutoff-top / resonance-core
/ depth fits; they are recorded as ruled out.

---

## 15. The Open303 voice renders ~15.65 dB quieter than ReBirth

**Owner: the 303 (supertask 1). Diagnosed 2026-09-16 during the compressor
campaign. Half resolved 2026-09-18: the native TPT voice became the default
(commit a4fac4c). Open303's level gap itself is still open.**

When this was found the default 303 voice was Open303 (the calibrated clean-room
voice is TPT, then behind `REBORN_BASS_ENGINE=tpt`). Measured on the compressor drone fixtures
(saw, cutoff 127, slid, no accent): our default render sits at **-31.45 dBFS at
fader 127 where ReBirth is -15.80** (a constant ~15.65 dB offset across faders;
the fader LAW matches at 10 dB per 27 bytes, so it is a pure level offset, not a
fader-law bug). The TPT voice renders the same fixture at **-14.84 dBFS**, within
~1 dB of ReBirth. So Open303's output level is uncalibrated by ~15.65 dB.

Why it matters beyond the flag hazard in `NON_OBVIOUS_BEHAVIOUR.md`: a level offset this
large silently confounds every LEVEL-DEPENDENT measurement (the compressor, the
mixer, any whole-mix check), because it moves the signal relative to a
threshold/knee. It cost a confounded compressor-verification pass before the gap
was traced to the voice. The standing rule (`NON_OBVIOUS_BEHAVIOUR.md` and
`MEASUREMENT_PROTOCOL.md`) is now: never measure on Open303, always
`REBORN_BASS_ENGINE=tpt`.

TPT is now the default, so the remaining question is whether to calibrate
Open303's output level toward ReBirth's at all: Open303 is kept faithful to its
own source and is not a parity voice, so a fixed output trim is the most a fix
should do.

---

## 16. Compressor accepted residuals (signed off 2026-09-17)

**Owner: supertask 6 (compressor). Closed 2026-09-17; REOPENED 2026-10-01 on the
detector alone (third item).**

**Round 5 (2026-10-01, branch `comp-detector-cleanroom`, merged into `main`
on Luke's sign-off at the close-out, 2026-10-23) closes the first two items below by measurement through `comp_wav`**:
the detector is a peak follower now (every comp-material cell within 0.17 dB),
and the threshold-0 corner is -0.03 dB, not -4.17, because the static law turned
out to be a linear crossfade, `g = (1 - k) + k * T / max(D, F)`, which the
make-up-slope-softKnee form could not express. `specs/SPEC_COMP.md`, "Round 5
result".

The compressor was signed off within ~1 dB across the surface. Three items remain:

- **The level detector reads PEAKS; ours reads RMS. Measured 2026-10-01**
  (`fixtures-2026-10-01/comp-material`, `research/PLAN_COMP.md` "Material probe"). The
  sign-off fit used one source, a slid saw drone, on which the two detector types
  differ only by a constant the threshold law absorbed. On a square, a resonant
  drone and a dull drone our gain reduction misses ReBirth's by about -2.9, +2.8
  and -3.5 dB at ratio 127, which each source's crest factor predicts for a peak
  detector within about 0.6 dB; on staccato notes, a 909 kick and a 909 hat it
  misses by up to 3.3, 3.2 and 7.6 dB. It shows in the demo songs: after the
  topology fix, KiloMix bars 538 to 543 and Clodhopper bars 280 to 282 still
  compress 3 to 3.6 dB harder than ReBirth on a source 1.6 to 2 dB quieter.
  Re-measured 2026-10-09 with the mods' real voices (BACKLOG 17), where the
  uncompressed sources now agree within 0.54 dB: this branch's compressor cuts
  2.5 to 5.7 dB harder than ReBirth's in the compressed passages of Alloy Cowboy,
  Groove It Up, Clodhopper and KiloMix, and 5.1 to 8.1 dB harder in Clodhopper
  bars 276 to 283.
  `specs/SPEC_COMP.md` round 5 is the brief for a walled refit of the detector only.

- **The threshold-byte-0 / quietest-input corner, -4.17 dB.** PROVEN unclosable
  in the make-up-slope-softKnee model: the defect is under-boost, fixable only by
  a larger low-threshold make-up, which (being input-independent) is pinned by the
  ratio-127 loud cell: satisfying the quiet input contradicts the loud one
  (`research/PLAN_COMP.md`, batches 5-6, rounds 3-4, with an algebraic proof in
  `src/effects/compressor.cpp`'s header). Closing it needs an INPUT-DEPENDENT
  make-up / a structurally different (leveller/AGC) gain computer, a large
  speculative rewrite for one cell at the most extreme setting on an effect that
  defaults off. If reopened, do NOT re-run the feed-forward concave / hump fits or
  a feedback topology (feedback gives a constant effective slope); both are ruled
  out with proofs.
- ~~**The section (mono) compressor path is unverified.**~~ **MEASURED AND FIXED
  2026-10-01** (`fixtures-2026-10-01/comp-rides`, st_; `PARITY.md`, "The
  compressor's rides and topology"). The section compressor comes BEFORE the
  channel fader (fader 127 to 90 moves the compressed output by the full 13.69
  dB) and before the pan. Every section cell at fader 127 equals the master's
  hard-left cell (measured), which ours reproduces by feeding the section at the
  level a fader of 127 gives it, as one channel of the master's L+R detector (a
  fit to those cells, not a measured mechanism). Ours ran it after the fader at the bus
  level, 12.54 dB low at ratio 127 threshold 0, and the fader changed how hard it
  compressed. After the fix every compressed section cell is within 0.53 dB of
  ReBirth but one, r064_t000 at +1.33 dB raw. Of the TPT drone's own +0.94 dB
  level residual, `comp_wav` run on both drones (g = 6.02 dB, ratio 64,
  threshold 0: -18.98 on ReBirth's, -18.31 on ours) says 0.66 dB passes there,
  so the corrected miss is +0.66 dB. The uncompressed and ratio-0 section cells
  carry the residual whole (+0.93 to +0.95), and so do t096 and t127 at ratio
  127, where the section detector sits below the knee: corrected misses -0.44
  and -0.62 dB. What remains: the fader's place relative to the DISTORTION is
  unmeasured (entry 21); where the delay send taps relative to the section
  compressor is unmeasured (ours taps after the inserts, at the fader's level);
  where the channel MUTE sits relative to it is unmeasured (ours keeps the
  compressor running at mute and at fader 0 and silences its output, assumed,
  so a section comes back without the full-make-up burst it used to give); and
  no fixture moved the MASTER fader with a section compressed (ours assumes it
  has no effect on the section compressor, since it follows the master
  compressor).

## 17. Mod (`.rbm`) drums played NOISE in Reborn (2026-10-04); the readers unscramble since 2026-10-06, with exact generated keys since 2026-10-11; sound parity on mods was deferred to post-1.0 (Luke's call 2026-09-18)

**Owner: the noise is fixed in code, awaiting Luke's sign-off (below); the
measurements it invalidated were re-run 2026-10-09 (below). The ship gate as
written passes, but it does not test what plays.**

**2026-10-11, the key function found: the readers generate exact keys.** Derived
by black-box ModPacker packs only, at Luke's request ("we need to go for 3 here and
find the key"), and approved to ship by him on 2026-10-11 (`FORMAT_RBM.md`, "The
key function"; `src/io/rbm_keygen.cpp`). The two sums pick where each master starts
in a stream of 2048-byte chunks, each the bytes of a seeded 24-bit LCG with a fixed
set of words written over them. Through the C++ reader the four retail mods decode
with every key byte exact and no decoded byte different, Metallicon's 319 included;
the keys take under 0.2 ms to generate and a first read 43 to 74 ms on 24 cores
(53 to 125 ms on 4), since the self-check was made parallel on 2026-10-12. A fresh set of twelve partial packs, one file to all 106,
decodes byte for byte, every one read. The estimate stays as the fallback for an
archive the generated keys do not fit (one ModPacker did not make, one packed without a
ModName.txt, or one edited after packing) and keeps its evidence minimum. Generated
keys are judged without it, but each parity that holds audio must be confirmed on its
own, by enough wrap-test evidence or by the check refusing unrelated keys there: two
adversarial reviews found that the check passes any key on loud noise, and that an
archive edited in one half of an entry's readable kilobyte after packing is right on one
parity and wrong on the other (FORMAT_RBM.md, "Reading a scrambled mod"). This mostly settles both open questions below, as qualified there.
Merged into review/2026-09 with Luke's sign-off on 2026-10-12; not yet on main.

**2026-10-23, done: noise-only partial mods are read, and near misses are caught.** Luke
asked for partial mods made only of hats, cymbals and noise snares to be read. A parity
is now confirmed also by how far its decoded audio is from what an unrelated key gives
(`uniformOdds`, an exact weighted Chernoff bound, under one in a million), and the wrap
test counts its longest chain rather than a sum. Retail voices alone under their own
generated keys now read 180 of 184 (172 before), with 0 of 1472 unrelated keys taken.
The 2026-10-23 sweeps then found the old blind spot on near misses widened onto noise: an
archive edited after packing by a multiple of 2^22 in a sum has a generated key a few
bytes off, and on a loud-noise parity, which the old code could not confirm and so
refused, the new route took it. On Luke's choice the reader now sets each confirmed
parity's key against its near neighbours' keys and refuses it when one decodes the
parity better (`nearMissOutscored`; `FORMAT_RBM.md`, "Reading a scrambled mod"). On
edited [tone, capped noise] archives the wrong acceptances fell from 29 in 200 to 8, each
an offset inside the noise's range 32 to 57 dB under the voice; on the retail voices the
34 near misses of 215 that both versions take differ in low bytes or by one step, 33 to
94 dB under the voice.

**Still open, all small.** Four of the 184 retail voices fail the check alone in an
archive even under their exact keys (the two startup chimes, MSM2 `TR808CB`, PBE2
`tr808cy`), because the residual-ratio test reads a quiet readable start against a loud
body; it matters only for a partial mod made of such voices alone. Noise spread evenly up
to full scale is indistinguishable from a wrong key and is refused under any key. A
parity confirmed by decoys rather than by the wrap test or the uniformity bound still
passes a wrong key with its own small chance, 1.4 to 1.8 percent on a lone silent or
near-silent voice. Near misses whose only effect is a low-byte or one-step offset, or a
small offset inside loud noise, still pass (the figures above). Keys right for some
entries of a parity and wrong for others (entries reordered after packing) are refused in
every construction tried but not by a test aimed at them. And the archive ModPacker
writes without a ModName.txt (CAT length 0, other key constants) is not modelled and
falls to the estimate; whether ReBirth loads one at all is unmeasured.

**2026-10-06, fixed in code: the readers estimate the keys.** `readModSamples`
and `readModSprites` now unscramble every mod before reading it, with keys
estimated from the archive's own content (`FORMAT_RBM.md`, "Reading a scrambled
mod"; `src/io/rbm_unscramble.cpp`). Against the ModPacker oracle, MSM2, PBE2 and
Orbit2 decode exactly, every key byte, and Metallicon exactly except 319 bytes in
three voices, worst `TR909SDN2` with its error 39.1 dB below its signal. A result
that fails the self-check is refused with a reason; on the retail mods it refuses
34 of the 36 wrong keys tried (none, random, right except in part, or right except
for a run of bytes offset alike; the two it passes move PBE2's samples by 1 or by 256
under loud audio) and passes all four estimates. It cannot see small offsets on low
bytes, or on high bytes where no sample is loud enough to wrap. So
`reborn_render` with a mod named plays that mod's real voices: the MSM2 kick and
hat fixtures of 2026-10-04 now match ReBirth's exports to +0.03 and -0.49 dB, within
0.27 and 0.76 dB of octave shape, where the scrambled read sat at a 5 kHz centroid
and 24 dB of shape error. The interim workaround for modded songs, a folder of
oracle-decoded samples passed as `--samples`, is no longer needed; the
`--samples samples-original` advice for STOCK songs below still stands. The
measurements listed under "Measurements this invalidates" were re-run with the
real voices on 2026-10-09 (below).

**Mostly settled 2026-10-12, by the generated keys and a parallel self-check: the
estimate blocks the thread that loads.** Generating takes under 0.2 ms and the
self-check now judges its voices over the cores, with verdicts identical to the
single-core version on every case compared, so a first read of a whole retail mod
takes 43 to 74 ms on 24 cores and 53 to 125 ms on 4 (it took 0.44 to 1.09 s), and a
partial mod whose audio needs decoys up to 0.83 s. Only an archive whose keys must be
estimated still pays seconds, and only there do the remedies below still matter.
As it read before: one estimate
takes 2.7 to 4.5 s on the reference machine's 24 cores and 4.3 to 8.9 s pinned to
4 (the four retail mods, 2026-10-09), the first time a process reads a given mod. The app now reads
off the engine lock and installs under it, so the audio thread never waits, but
the read itself still runs on the message thread (opening a song, picking a mod)
or on the host's thread during `setStateInformation` (a DAW restoring a project),
which freezes for that long. Two remedies, not exclusive: load in the background
and switch the drums in when ready (the song plays a few seconds without its mod;
a DAW restore would no longer be synchronous), or keep the estimated keys in a
per-user cache keyed by the file's content, so only the first load on a machine
ever pays (keys derived from the user's own copy, stored locally, never shipped).

**Mostly settled 2026-10-11 by the generated keys: partial mods.** A partial mod
packed by ModPacker now reads exactly whenever each parity that holds audio can be
confirmed, which takes a voice there that the wrap test can judge (one whose readable
start steps gently) or enough other evidence to refuse unrelated keys; a parity whose
audio is all loud noise is refused, as the minimum refused it, and one of near silence
about two times in three (FORMAT_RBM.md, "Partial mods"). Its missing files play as Reborn's own (its
synthesis, or the user's sample folder) where ReBirth plays its standard
components; whether Reborn needs a set of sounds that stand in for ReBirth's
standard components is Luke's call for later. Everything else in this paragraph
now applies only to keys that must be estimated.
As it read before: the manual lets an author leave files out
of a mod, the missing ones playing as the standard components (p97); following
its recipe instead gives a whole archive. Today an archive with fewer than 12
voices reaching past its first kilobyte is refused outright, right keys or not,
and its drums stay as Reborn had them (its synthesis, or the user's sample
folder), with "mod unreadable" and the reason in the session note. Measured out
of sample on 2026-10-09 (`FORMAT_RBM.md`, "Partial mods"), that line is in the
wrong place. Whole mods: 8 of 8 pass, worst voice 39.5 dB clean. 12 to 46
voices: 15 of 24 pass, and 3 of those 15 carry a voice under 30 dB (27.4, 24.0,
19.6), so the check does not make them safe. With the minimum dropped, 3 voices:
the other tests would pass 5 of 12, four with a voice under 30 dB (down to 5.2); 6 to 11
voices: they would pass 20 of 40, all 26.4 dB clean or better. The errors that
get through sit at the top of the keys, which only the last few files reach,
inside noisy sounds. Choices: keep refusing below some count (a minimum of 6
would have refused every pass here under 25 dB, but that line is fitted to these runs), play whatever
passes with a note that a partial mod is decoded by estimate, or find the key
function (parked 2026-10-06), which would make every mod exact. Nothing measured
says which mods users have; the four retail mods are whole.

**2026-10-04, measured: our mod drums are noise.** Only about the first 1024
bytes of each file a mod embeds are stored in the clear; past that every payload
is scrambled at the same length (`FORMAT_RBM.md`, "Past the first kilobyte";
`tools/analyze/rbm_check.py`, all four installed mods). Our loader reads the
whole payload as plain PCM, so every mod drum voice plays a few hundred clean
samples and then full-scale noise. ReBirth unscrambles it: with
`fixtures-2026-10-04/mod-check` (the comp-material kick and hat fixtures with
`Massen's MSM 2.0` named, exports in `renders/mod-persistence-2026-10-04`),
ReBirth's 909 kick is a kick, centroid 111 Hz over the bar, where ours is
centred at 5 kHz with a band shape up to 73 dB off; ReBirth's hats are 2.4 dB
quieter than ours and 3.1 dB peakier. The 2026-09-18 ship gate checked load,
exit code, NaN and dropouts, and noise passes all four. Every song that names a
mod is affected, eight of the sixteen demo songs among them (Alloy Cowboy,
Mutilated Species II and RSI name PBE2; Clodhopper, Fonda Workout, Groove It Up
and SuperNova name MSM2; Metallicon City names Metallicon+).

**Luke chose (c) on 2026-10-04 ("i see no issue taking a mod apart"), and approved
doing it in-session on 2026-10-06.** Background agents could not: the auto-mode
permission check refused all three, so the work ran in the main session. Measured
by black-box ModPacker experiments only (`FORMAT_RBM.md`, "The scrambling,
measured"): an additive key from payload byte 1024, two masters per archive with a
period of 140 + 17 i, each master set by a 32-bit word sum over the readable first
kilobyte. With ModPacker as the oracle all four installed mods decode, and the
decoded MSM2 kick and hats play within 0.03 / 0.49 dB of ReBirth's level and 1.03 /
0.76 dB of its band shape. The function from a sum to its master was not known
then, and Reborn cannot run ModPacker when a user loads a mod, so the reader
estimated the keys instead (above). The function was found on 2026-10-11 (top),
and the decode is now exact everywhere, Metallicon's top included.

**Mods do not persist between songs in ReBirth (measured 2026-10-04).** A mod
belongs to the song document that names it (manual p89-92). Exported straight
after modded Clodhopper, the stock-sample kick and hat fixtures match fresh
exports and the 2026-10-01 ones exactly (level to 0.01 dB, octave-band shape to
0.00 dB) and our stock-sample renders within 0.9 dB of band shape. So fixture
exports are safe whatever ran before them.

**Measurements this invalidates.** Any whole-mix comparison of a song that names
a mod: our side played noise for its drums. That includes the 2026-09-18
whole-mix regression below, the per-song moves the 2026-10-01 compressor work
reported for Alloy Cowboy (PBE2), Clodhopper and Groove It Up (MSM2), and
entry 23. Also note that `reborn_render` without `--samples` plays the STOCK
songs' drums through our synthesis path, not the stock samples; pass
`--samples <workspace>/samples-original` to compare a stock song's drums on the
sample path ReBirth uses.

**Re-run with the real voices, 2026-10-09** (this branch's compressor, the
pre-round-5 detector; full rack, TPT, each song's own mod, the stock songs on
`--samples samples-original`). All three items below now read as ordinary parity
results, no worse than the stock songs beside them.

- **Whole mix** (`tools/measure/wholemix.py` against
  `rb338-exports/wholemix-2026-08-27`, renders in
  `renders/wholemix-mods-2026-10-09`). Metallicon City level +0.44 dB, octave
  error 1.23 dB, skew 0.51 against ReBirth's 0.50; SuperNova -1.05 dB, 0.87 dB,
  skew -1.04 against -1.18. The four stock songs beside them: 0.85 to 1.46 dB of
  octave error. The same two songs forced onto the stock kit score 2.59 and 2.44
  dB, and the 2026-09-18 renders (noise drums, an older engine) 8.15 and 5.71 dB
  by the same script, with +6.15 and +3.07 dB of level.
- **The compressor's four largest movers** (`tools/measure/perbar.py`, per-bar
  RMS, L and R; exports `renders/demo-comp-check-2026-10-01`, renders
  `renders/demo-comp-check-mods-2026-10-09`). With the compressor routed nowhere
  ours agrees with ReBirth within 0.54 dB in every range below; with it on, ours
  sits under, so the difference is our compressor cutting harder: Alloy Cowboy
  bars 73 to 79 by 2.9 L / 2.5 R dB, Groove It Up 144 to 145 by 4.5 / 4.3,
  Clodhopper 275 to 282 by 5.7 / 5.7, and the stock control KiloMix 537 to 544 by
  3.7 / 3.7. That is BACKLOG 16's detector, now seen on modded songs too.
- **Entry 23** (Clodhopper bars 268 to 274, on `comp-detector-cleanroom`) was the
  noise drums. Per-bar gain, compressed minus uncompressed: in bars 264 to 275
  ReBirth's is -5.1 to -7.5 L and -6.6 to -9.3 R, ours -5.4 to -8.1 and -7.0 to
  -10.1, so both compress throughout, ours 0.2 to 1.1 dB harder, with the
  uncompressed mixes within 0.5 dB. In bars 276 to 283 ReBirth takes 18.7 to 21.1
  dB and ours 24.4 to 27.8: 5.1 to 8.1 dB harder, the detector again.

A trap met on the way: `reborn_render --samples` loads after the song's mod and
replaces all 27 of its voices, so passing `--samples` to a modded song silently
plays the stock kit (it is how the stock-kit figures above were made). Pass it
only to stock songs.

The rest of this entry is the 2026-09-18 record. Its cause is RETIRED: the mod
songs ran "bass-light, treble-heavy and hot" because their drums were noise, not
because of the trim tables.

A `.rbm` mod supplies its OWN drum samples (and optionally a skin): it is an
intentional alteration of the core ReBirth sound, not the core sound itself.
Chasing exact playback parity on a third party's custom samples is post-1.0 work.

**The 1.0 ship gate is "load and play to completion", and it PASSES.** Verified
2026-09-18 across all eight modded demo songs and all three mods in use
(PBE2, MSM2, Metallicon): each resolves its mod and loads 27 voices, renders
exit-0, and plays continuously to completion with no crash, no NaN/Inf, no
dropout (Clodhopper's ~205s natural fade-out is the song ending, not a defect).

**The deferred half is SOUND parity.** Whole-mix regression 2026-09-18: the eight
NO-MOD songs track ReBirth within ~1-2 dB per band (Fast Forward 0.34 up to G-Spot
2.32, mean ~1.1 dB), but the mod songs run bass-light and treble-heavy and hot
(SuperNova mean |band delta| 6.3 dB, Metallicon 7.3; 30-160 Hz down 9-14 dB,
2.6-20k up ~7 dB). Cause: our per-voice trim tables (`kTrim808`/`kTrimSyn808`)
were fitted for ReBirth's BUILT-IN samples and are applied unchanged to a mod's
different samples, which arrive at their own levels. The fix, when pulled forward,
is the same method that calibrated the built-in drums: measure the mod voices
against ReBirth's own mod exports and fit per-mod (or per-sample-normalised) trims.
Nine-of-sixteen-song reach, but it is a self-contained mini-campaign.

---

## 18. Automation timing: rides land on the 16th grid and step, where ReBirth applies the tick and glides

**Owner: unowned, engine-wide. A parity change that needs Luke's sign-off.
Measured 2026-09-27.**

A song times its automation in 1/32-note ticks (32 to the bar), and the engine
dispatches it only from `fireStep`, once per 16th step (`songTicks_` advances two
ticks per step, `Engine::dispatchAutomation`). So an event on an odd tick is
applied at the next even tick, one tick late: 62.5 ms at 120 BPM, 44.1 ms at RSI's
170. An odd-tick event followed by one on the same param at the next even tick is
applied in the same instant, and the earlier value never sounds. And every ride is
a step: the new value is there at once.

**ReBirth does both differently** (`fixtures-2026-09-27/delay-rides`, `PARITY.md`,
delay section). A DELY pan ride to 127, with the one echo leaving the delay at
2.583 s, between tick 41 (2.5625 s) and tick 42 (2.625 s): ridden at tick 40 the
echo's far side reads -52.4 dB, at tick 41 -16.4 dB, at tick 42 -10.0 dB (centred).
So ReBirth applies the tick 41 ride before tick 42, which we do not, and it
GLIDES: 21 ms after the ride the far side is only 6.4 dB down, and 83 ms after it
is still 4.3 dB short of the -56.7 dB it settles at. We read -10.0 dB at tick 41
(the ride waits for tick 42) and the full cut at tick 40. The compressor work met
a glide on fader automation, roughly 100 ms (`PARITY.md`, compressor ballistics).
These two readings do not fix the delay's glide duration or shape: no single
simple glide fits both (a fast move, about 50 ms linear in amplitude or a one-pole
near 30 ms, fits tick 41; tick 40's residual needs a longer tail).

**Reach.** Timed events (tick 0 excluded) as the loader leaves them, 16 demo
songs, every one of which has some on odd ticks:

| Target | On odd ticks | Timed events |
|---|---:|---:|
| Bass 1 | 8,308 | 17,398 |
| Bass 2 | 9,214 | 18,928 |
| 808 | 230 | 2,654 |
| 909 | 2,036 | 4,622 |
| Mixer | 7,616 | 19,669 |
| PCF | 883 | 3,964 |
| DIST | 1,948 | 4,361 |
| DELY | 1,614 | 4,435 |
| COMP (played since 2026-10-01) | 62 | 156 |
| **All** | **31,911** | **76,187** |

**The compressor's rides, 2026-10-01** (`fixtures-2026-10-01/comp-rides`, the
four cr_ ride fixtures, one ride at tick 64, a step boundary, so only the glide
and not the grid is in them). `comp_rides.py` reads the ms from the ride until a
10 ms RMS envelope has covered 90 percent of the step in dB (10 ms resolution).
ReBirth: 15 ms for a ratio ride up or down and a threshold ride up, 25 ms for a
threshold ride down. Ours: 15 ms in the attack direction (cr_ratio_up and
cr_thr_dn, more cut) and 95 and 85 ms in the release direction (cr_ratio_dn and
cr_thr_up, less cut), because our ride is a step and the move comes from the
compressor's own gain ballistics (attack 6 ms, release 45 ms one-pole, fit from
drum transients). ReBirth's ride is as fast in both directions, so its control
glide and its gain ballistics are not the same thing. Not fixed: it belongs with
the glide. Round 5 (2026-10-01) has no gain smoother and decodes params once per
512-sample block, so its rides step within one block: 5 / 15 / 15 / 5 ms (ratio
down / ratio up / threshold down / threshold up) against ReBirth's 15 / 15 / 25
/ 15, faster on three of four by one 10 ms window. Measure ReBirth's ride glide
at a finer resolution before adding a param ramp. The step sizes: the ratio rides read 18.01 dB against ReBirth's 16.42
and 16.44 (1.57 to 1.59 dB raw), of which 0.94 dB is the TPT drone's level
residual at the ratio-0 end; corrected with `comp_wav`, 17.07 dB, a 0.63 to 0.65
dB miss. The threshold rides read 13.63 against 13.43 and 13.47 (0.16 to 0.20 raw,
0.06 to 0.10 corrected). Both are inside the compressor's signed-off ~1 dB.

**A demo-song instance of the 16th grid on the compressor** (2026-10-01). In
SuperNova the mixer track moves the compressor onto the 909 at tick 6367, an
odd tick, and the 909 track mutes the 909 at tick 6368. Ours applies both at
step 6368, so the compressor never acts on an audible 909 sample, and the full
render is byte-identical before and after the compressor work. ReBirth, playing
at tick resolution, compresses the 909 for the one tick (about 51 ms) in
between.

**Next measurement: the glide's shape.** One ride, one control, read continuously
rather than at one echo: for example a mixer fader ride on a held 303 note, or a
DELY pan ride under a dense echo train at feedback 127, read in short windows
from 20 ms before the tick to 200 ms after it. Does the glide take a fixed time
or a fixed rate, is it linear in the byte or in dB, and does every control glide
(pan, fader, knob, switch)? A switch cannot glide, so param 0 and the routing
bytes are the control case.

**The fix, once measured.** Dispatch automation at tick resolution, splitting
each step's render slice at the half step, and smooth the ridden controls with
the measured glide. That moves every demo song's render and the byte-neutral hash
(its `--no-fx` solos carry 303 knob rides on odd ticks), so it needs Luke's
sign-off, and it touches every component's automation at once.

---

## 19. At pan 127 the delay return's far side is -46.8 dB in ReBirth, not silent

**Owner: the pan law (mixer and delay). A parity change that needs Luke's
sign-off. Measured 2026-09-27.**

Over the echo of `dct_mirror` (delay return pan 127, one echo per bar) the left
channel is a coherent copy of the right at -46.8 dB: least-squares gain of L on
R, correlation 0.99, the same in bars 1, 2 and 3. `dr_even`'s bars 2 and 3, after
a ride to pan 127, read the same, -46.8 dB. `dct_ref` is -187.5 dB at that time,
so it is not the dry tail. Ours is digital zero there: `panGains` cuts the far
side to silence for |x| at or above `kPanCut` (0.92, which with
`panFromByte` = (byte - 64) / 63 is bytes 122 and up and 6 and below), on the
mixer's measurement. The delay's -46.8 dB sits almost on the curve's
own value at x = 1 (-45.9 dB).

**What is wrong in the docs.** `specs/SPEC_DELAY.md` and `research/PLAN_DELAY.md` said the far
side is silent at the extremes; that came from a 3-decimal reading that could not
resolve -47 dB. Both are corrected alongside this entry. The E3 entry in
`research/FINDINGS_AUDIT.md` did note ReBirth's far side 46.6 dB down while counting the
balance as matched.

**Before changing anything,** check whether the mixer's own end-cut measurement
(`pattern.h`, "bytes at or past 4 from either end cut the far side to digital
silence rather than to the curve's -42 dB") had the same resolution limit: re-read
its exports for a coherent far-side copy by least squares rather than by level.
If the mixer is not silent either, the cut goes for both; if it is, the delay's
return pan has its own end law. Inaudible under the near side in practice, and a
pan-law change either way.

---

## 20. ReBirth's drum hits sit on a 64-sample block grid, and its first hit is late

**Owner: the transport work (`research/PLAN_TRANSPORT_MODES.md`) and rhythm parity. A
parity change that needs Luke's sign-off. Found 2026-09-27, and restated the same
day after adversarial verification: it first read "off the grid, not linear, cause
unknown".**

ReBirth starts each hit on a 64-sample block boundary. One 909 rimshot per
downbeat, every onset in the exports at hand, less the rimshot's own 3-sample
attack, is a multiple of 64:

| Export | BPM | Bar 0, 1, 2, 3 onsets (samples) | Bars 1 to 3 against the exact grid |
|---|---:|---|---|
| `delay-taps` dl_ref | 120 | 64, 88192, 176384, 264576 | -8, -16, -24 |
| `delay-taps` dl_t090 | 90 | 128, 117568, 235136, 352768 | -32, -64, -32 |
| `delay-taps` dl_t144 | 144 | 64, 73472, 146944, 220480 | -28, -56, -20 |
| `delay-taps` dss_t123 | 123 | 64, 86016, 172096, 258112 | -33, -2, -34 |
| `delay-taps` dss_t100 | 100 | 128, 105792, 211648, 317504 | -48, -32, -16 |
| `delay-rides` dr_p0 | 120 | 64, 88192, 176384, 264576 | -8, -16, -24 |

dl_pan127 and the E3 dct_mirror read as dl_ref; dr_p0's bar 3 reads 264577, one
sample of cross-correlation jitter. Bars 1 and later sit at floor(k x bar / 64) x
64, the block the exact time falls in, 0 to 63 samples early: 17 of these 18 bars.
The exception is dl_t090's bar 2, whose exact time, 235,200, is itself a multiple
of 64, and which lands one block earlier, likely a floating-point floor. Bar 0
alone is LATE: one block at 120, 123 and 144 BPM, two at 90 and 100. This entry
first gave "tens to about 190 samples early, not linear in the bar number"; that
measured each bar against the late bar-0 hit, which mixed the start delay into the
drift (the 120 BPM row it gave, -72, -80, -88, is exactly linear).

Ours puts every hit on the exact grid, first onset at sample 3. The delay is exact
to each hit in both (the echo lag measured from its own hit).

**What it costs, measured.** Wherever an echo lands on a later hit the rendered
audio differs, because ReBirth's echo lands beside the hit (8 to 88 samples off it
in these fixtures) and sums with it out of phase, partly cancelling it in dct_ref's
bar 1, where ours lands on it and adds. On dct_ref (steps 4 triplet, feedback 96, one
rimshot a bar; 30 ms windows at the downbeat and the two echo times, each file
against its own bar-0 hit) the gap is 1.7, 3.2 and 3.3 dB in bar 1 and 1.0 to 1.8
dB in bars 2 and 3; on dr_p0's bar 3, 0.4 to 0.8 dB. A model built from ReBirth's
own bar-0 hit and echo gains reproduces its readings at these onsets and ours on
the exact grid, so all of it is hit timing (`research/FINDINGS_AUDIT.md`, the 2026-09-27
entry). It is the "0.9 dB echo-to-dry excess" once recorded for the delay, at its
real size. Any echo, or any sound still ringing, that meets a later hit is exposed
the same way; the shift itself is at most 1.45 ms within a song (one block) and
1.45 or 2.9 ms at its start.

**Still open, and the confirmation fixture.** What sets one block against two at
bar 0; whether dl_t090's bar 2 repeats; whether hits inside the bar follow the same
floor. A dedicated isolated fixture answers all three: one short 909 voice on every
16th for 32 bars or more, at 90, 100, 120, 144 and 170 BPM, exported twice to check
the onsets repeat, and the onsets fitted against floor(t / 64) x 64 plus a start
offset. No change to the sequencer clock before that confirms it: it moves every
render.

---

## 21. Where the channel fader sits relative to the DISTORTION is unmeasured

**Owner: the distortion (supertask 7). Found 2026-10-01, while measuring the
section compressor. Recorded, not acted on.**

`renderSlice` multiplies a section by its channel fader before the inserts, so
the distortion, the PCF and the compressor all see the faded signal. The
compressor was measured on 2026-10-01 (`fixtures-2026-10-01/comp-rides`, st_)
and comes BEFORE the fader in ReBirth: between fader 127 and 90 the compressed
output falls by the full 13.69 dB. It now runs on the section at fader-127
level and the fader's gain is put back after it. The PCF is linear, so its
place relative to a gain does not matter. The distortion is not linear, and its
place has never been measured. The distortion generators (`gen_dist_*.py`) never
set a fader, so the fit fixtures all ran at rbsgen's default. If ReBirth's fader
also follows the distortion, ours drives it softer or harder than ReBirth's at
every other fader, and the fitted gain law (entry 2) has absorbed whatever level
the fixtures used.

**How to measure it.** One 303 drone on a distorted section, the distortion the
only effect, a mid Amount and Shape, exported at fader 127, 100 and 70 with the
compressor and PCF off; and the same three with the distortion off as the
denominators. If the distorted output falls by exactly the fader's dB step and
its spectrum (the harmonic and inharmonic fractions, `dist_harmonics.py`,
`dist_inharm.py`) does not change with the fader, the fader comes after the
distortion; if the character changes, it comes before. Measure L and R
separately. If it comes after, the fix mirrors the compressor's: run the
distortion on the section at the level the fitted fader gives it and put the
fader's gain back after it, and refit the gain law only if that moves the fit
fixtures.

**Where the distortion and the section compressor share a section** (added
2026-10-01). The fader is applied before the distortion and the section
compressor's input is rescaled by fader 127 / v, so the compressor sees
dist(x * v) / v. That is linear in v only if the distortion is, so today's
mix of a pre-distortion fader and a 1 / v-rescaled compressor matches no single
chain order: at a low fader the compressor sees the distortion's near-linear
region amplified by up to 1 / v. The demo songs that sit in this state are the
cases to check once the order is measured: TGV's KiloMix '98 (6386 ticks with
the distortion and the section compressor on one section, including a fade
through faders 0 to 27), Alloy Cowboy Resurrection (957 ticks at faders 47 to
74) and Transfusion (1536 ticks at 112).

## 22. The TPT 303 saw's crest factor is 2.3 dB short of ReBirth's

**Owner: the 303 (supertask 1). Found 2026-10-01, while fitting the compressor's
detector (round 5). Recorded, not acted on. PRIORITY RAISED 2026-10-01: it now
blocks the compressor's raw parity on 303 material.** Until it is fixed, any TPT
303 through a compressor at ratio 127 and threshold 0 to 64 comes out about 1.3
dB hotter against ReBirth than in round 2 (raw +2.22 to +2.33 at threshold 0 to
48, +1.32 at 64). Independently reproduced on st_off_p064_v127 (1.0 to 3.9 s):
ReBirth RMS -15.79, peak -7.58, crest 8.21 dB; TPT RMS -14.85, peak -9.00, crest
5.85 dB, with the lowpassed peaks the same.

The slid saw drone the compressor campaigns use (cutoff 127, reso 0, fader 127)
reads, over 2.0 to 6.0 s: ReBirth RMS -15.80 dBFS, peak -7.64, crest 8.16 dB;
ours on `REBORN_BASS_ENGINE=tpt` RMS -14.84, peak -9.01, crest 5.83 dB
(`fixtures-2026-10-01/comp-rides` cr_ref_r000_t000, both exports). The RMS
residual (+0.94 dB) was known; the peak is 1.37 dB LOW. Since round 5 the
compressor reads peaks, as ReBirth's does, so a compressed TPT 303 is cut about
1.4 dB less than ReBirth's: the comp-rides cells read +0.93 to +2.33 dB raw
against ReBirth, where `comp_wav` on both drones says the DSP itself is within
0.07 dB. The fix belongs in the 303's waveform (the saw edge's height after the
filter at cutoff 127), not in the compressor.

## 23. ReBirth compresses Clodhopper bars 268 to 274 and ours does not

**Owner: the compressor (supertask 6), or automation timing (BACKLOG 18) if that
is where it lands. Found 2026-10-01 by the round-5 verification. Not caused by
round 5: the round-2 and round-5 builds are identical there.**

In the Clodhopper scratch copy with its Loop lengthened to 290 bars
(`fixtures-2026-10-01/demo-comp-check`, exports
`renders/demo-comp-check-2026-10-01`), ReBirth's per-bar gain (comp minus
nocomp) is -9.33 dB R at bar 268 and -6.3 L / -8.0 R at bars 272 to 274. Ours is
about -0.3 / -0.4 dB there, and our compression starts at about bar 275 (471 s),
3 to 7 bars later than ReBirth's. That L and R differ in ReBirth points to a
section compressor. A 6 to 9 dB miss. Next: find which section ReBirth
compresses in bars 268 to 274 and from which tick (the song's mixer and COMP
tracks there), and whether our engine reads that event at all.

**2026-10-04: Clodhopper names MSM2, and Reborn plays every MSM2 drum as noise
(entry 17).** The "808/909 balance" below is that: our side's drums in these
bars were noise. Settle entry 17 first; then re-measure here.

**Role A, 2026-10-01: most likely NOT the compressor.** The song routes the
compressor to the 909 section (mixer param 1 = 5) at tick 3646 (bar 114) and
off at tick 9064 (bar 283), and ours reads both events, so both programs
compress the 909 throughout bars 114 to 282. The 808 is muted by its own track
at tick 8800 (bar 275). Our "compression starts at bar 275" is that mute: in
bars 272 to 274 our mix is the 808 (our 808 solo -12.8 dBFS L against a whole
mix of -12.75), which the compressor never touches, and only once the 808 stops
does our compressed 909 carry the mix. ReBirth's mix there is quieter
uncompressed (-15.2 against our -12.5) and loses 6.3 L / 8.0 R dB when the 909
is compressed, so in ReBirth the 909 carries the mix in those bars and the 808
does not. That is a balance between the 808 and the 909 in this song, which
plays the MSM2 mod, and mod sample-path sound parity is deferred (entry 17). In
bars 275 to 282 ours is also 8.4 dB quieter uncompressed at bar 275, closing to
1.6 dB by bar 280, again before any compressor. To settle it: export copies of
the song from ReBirth with all faders but the 808's at 0, then all but the
909's, and compare each section uncompressed with ours in those bars.
