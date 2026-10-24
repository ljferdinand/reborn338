# 808 parity findings

Measured against the isolated single-voice ReBirth exports (comp off).

> **Current as of 2026-08-24. The 808 SAMPLE path is complete and merged to main
> (rounds 7 to 9); read those as the live state.** The SYNTH path had its own pass
> on 2026-08-24 (branch `808-synth`, awaiting Luke's ear and sign-off; see the SYNTH
> PASS section just below). The three drum-channel master effects remain to do.
> Rounds 3 to 6 (2026-07/08) are the
> earlier measurement history and correct themselves in place: round 3 overturned
> two round-2 readings (the clap and rimshot were never mis-sampled, that apparent
> mismatch was the shared-column selector), and everything before round 3 was
> measured in SAMPLE mode because a loaded sample wins over the synthesis path. That
> is why there are two trim tables, `kTrim808` (sample) and `kTrimSyn808` (synth),
> both noted in `docs/NON_OBVIOUS_BEHAVIOUR.md`. **The measurement bug
> that dominates rounds 7 to 9: `score808`'s old `avg_hit` averaged hits coherently,
> which cancels ReBirth's non-aligned tonal/HF voices and reads them far too quiet.
> It is fixed (power averaging), but it had mis-set every trim AND the SD noise fit;
> suspect it first for the 909.**

---

# SYNTH PASS (2026-08-24, branch `808-synth`): the clean-room voice, re-fit

ReBirth is always its sample player, so the synth path is a best-effort
approximation Luke judges by ear; the parity target is still the sample path. The
synth voices had not been touched since round 3/4 (2026-07-25), which means every
synth constant was fitted through the coherent-averaging bug that rounds 7 to 9
later found and fixed on the sample side. Re-measured under POWER averaging (a new
`--synth` flag on `score808`, which renders with no sample set so the synthesis
model plays), the gap map was large, and three voices had never been fitted at all
because their reference fixtures only arrived in the 2026-08-18 batch.

Every voice below now lands within 0.04 dB of its export at the default knob after
a final trim re-fit (`tools/measure/fit_trims808.py --synth`), run LAST because the
shape work moved most voices' energy. `ctest` passes and the 303 TPT parity path is
byte-identical (pooled hash `50d00a6a`).

- **RS rimshot: rebuilt.** Dominant was an invented 1700 Hz (with a 4250 Hz
  partial), read 2097 centroid against ReBirth's 579. Real structure: dominant 484
  Hz, partials at 883 and 1766 Hz ~8 dB down, nothing above 3 kHz, ~3 ms decay. A
  decaying sine from phase 0 is an asymmetric pulse whose sub-bass ran away (our
  0-100 Hz band read 1859%), so a ~45 Hz high-pass DC-blocks it. Now dom 484 exact,
  centroid 625 vs 579. Claves (the column alternate) unchanged.
- **CP hand clap, MA maraca: first fit.** Clap band-pass was at 1050 Hz/Q1.3,
  capping centroid at 1902 vs 3160; widened to 1400/Q0.62 for centroid 3193.
  Maraca was a plain high-pass shelving to Nyquist (rolloff 19552 vs 16161); a
  band-pass at 12.6 kHz lands centroid 12068 and rolloff 16101.
- **SD snare noise: rebuilt, and the tone question goes the OTHER way here.** The
  2.2-3.4 kHz band capped brightness (default centroid 1982 vs 3619). ReBirth's
  noise is a fixed bright band whose LEVEL rises with snappy; a fixed 2.6-9.5 kHz
  two-stage band with a snappy level law tracks the sweep (188/751/3751/5773/6360
  vs 177/630/3619/5487/6010). Round 9 removed the noise TONE term on the sample
  path because tone selects a brighter body RECORDING there. The synth body is two
  low sines and cannot brighten that way, so a tone-independent noise reads far too
  bright at tone 0 (4325 vs 1836). The synth therefore KEEPS a tone term on the
  noise, refit under power averaging: a deliberate, documented divergence from the
  sample path, because it is the only way to carry an effect the synth body lacks.
- **OH open hat, CH closed hat: ported the round-8 findings.** OH decay is a GATE
  not an exponential (hold then fast release); confirmed on the synth path against
  the ohd sweep (RB -30 dB 71/239/409/558/565 ms). Same-step CH+OH now gives a
  short OPEN hat (choke ~21 ms, choh_same lands -30 dB at 71 ms vs RB 70), not the
  wrong closed hat. A `gateHold`/`gateRel` pair on VoiceState carries the gate.
- **CY cymbal: dominant, tune, decay.** Dominant was 5599 vs 7268 (its high-pass
  sat at 5000 while the shared metal dominant is ~7268); fixed at 6800. The tune
  knob was a pitch multiplier that dragged the dominant 5599->10400; ReBirth pins
  it and only tilts brightness (centroid 8053->10207), so tune now moves the band
  top and the high-pass mix instead, dominant pinned at 7197. Decay re-fit to match
  the endpoints of a 12.5x span (85 and 1056 ms vs 90 and 1130), ~35% long in the
  middle, the accepted single-exponential residual.
- **BD kick tone: a level control, not a body/click trade.** Under power averaging
  ReBirth's kick is 99.7% sub-100 Hz at EVERY tone (the 500-2k band is 0.0%
  throughout), so round 3's "trades body for click" was an averaging artifact. The
  knob raises LEVEL 4.6 dB from tone 0-8 to tone 32 then holds flat (the sample
  path's tone-minimum quirk). The synth did the reverse; replacing the trade with
  the measured level curve makes the tone offset flat at -7.08 dB across the knob.

Accepted synth residuals: SD tone-0 cannot go as dark as ReBirth's darkest body
recording (2239 vs 1836 centroid); CY decay mid ~35% long; the four synth-only
knobs ReBirth's 808 lacks (tom_decay, ch_decay, cp_spread, cb_tune) are left in
place pending Luke's product call on whether to hard-code or keep them as extras.

## Ear-correction round (2026-08-24, same branch): five voices on Luke's listening

Luke listened to the first synth pass and flagged five things. Each was measured
by WIDENING what the metric looked at (envelope shape, per-band ratios, sub-octave
content), since the summary centroid/rolloff had already "passed" on all five.

- **OH open hat, the glaring one, fully reconsidered.** Two faults. (1) My round-8
  gate held the synth voice FLAT at -1 dB then cliffed; ReBirth's open hat actually
  decays smoothly, a gentle plateau then a faster fall (envshape -0.9/-2.9/-5.9/
  -9.8 dB at 50/100/200/300 ms for the default). The gate fit the -30 dB LENGTH by
  luck while getting the shape wrong. Replaced with a two-phase decay (a 0.4 s
  plateau tau then a knob-scaled faster tau), landing -30 dB at 65/229/382/521 ms
  vs 71/239/409/558. (2) ReBirth's OH has 0.00% below 3.5 kHz; ours leaked the
  sub-800 Hz square fundamentals +23 to +35 dB through a single 12 dB/oct
  high-pass. A cascaded high-pass at 6 kHz clears them. Two-pole limit leaves the
  top a little open (hissier above 13 kHz), an accepted residual.
- **CB cowbell, sounded an octave low.** The narrow cascaded band-pass at 810 Hz
  stripped the square harmonics ReBirth carries (ours 99.7% in 300-1000 vs 90.6%)
  and left the 538 Hz sub-partial nearly as loud as the 807 dominant (-8.2 vs
  ReBirth's -14.9). A high-pass at 700 + low-pass at 2400 with the 810 oscillator
  weighted up lands the band split 89/10/0.8 vs 90.6/8.8/0.6; the 538 pulls to
  -10.5 (still ~4 dB hot, it sits a fifth under the dominant, a residual).
- **SD snare, sounded hollow/low.** The body pitch matched (177 Hz) but the full
  snare had a -5 dB hole in 500-2000 Hz: two pure sines with no harmonics, and the
  noise band starting at 2.6 kHz. Lowering the noise low corner to 1.3 kHz fills it
  (region now within ~2 dB) and the sweeps still track.
- **CY cymbal, another pass.** Trimmed the raw high-pass mix to pull down the
  9-13 kHz. The 6-9 kHz peak stays under ReBirth's (32% vs 48%), a two-pole limit.
- **RS rimshot, more click.** Raised the brief noise click 0.10 to 0.28, kept short
  so the centroid holds (632 vs 579).

After a second trim re-fit every voice is again within ~0.1 dB of its export.

---

# Round 3 (2026-07-25, batch E): the two data-gated items, solved

Batch E was designed to separate two effects that earlier fixtures confounded.
Both are now settled, and both overturned a round-2 conclusion.

## 1. Shared-column selector: SOLVED

Five 808 panel columns each drive one of two voices. The song file carries a
switch per column, in the `808 ` chunk header:

| offset | column | switch = 0 | switch != 0 |
|---|---|---|---|
| +11 | LT | Low Conga | Low Tom |
| +14 | MT | Mid Conga | Mid Tom |
| +17 | HT | Hi Conga | Hi Tom |
| +19 | RS | Claves | Rim Shot |
| +21 | CP | Maracas | Hand Clap |

**0 selects the ALTERNATE voice**, and 0 is also ReBirth's factory default.

Evidence, in three independent layers:

- **Byte diff.** Each `d808_sel_*0` / `*1` pair differs in exactly three bytes:
  the title, one byte in `808 `, and its mirror in the TRAK initial state.
- **Spectral decomposition.** Fitting each export onto its two candidate raw
  samples by non-negative least squares gives an unambiguous winner every time:

  | fixture | fit |
  |---|---|
  | `sel_lt0` / `sel_lt1` | low conga 100% / low tom 99.5% |
  | `sel_rs0` / `sel_rs1` | clave 99.4% / rimshot 100% |
  | `sel_cp0` / `sel_cp1` | maracas 100% / clap 83.9% |

- **Layout scan.** Dumping the header across all 18 shipped songs, the only
  bytes that take exactly two distinct values are 0, 11, 14, 17, 19, 21, 30, 32
  and 33. Batch E pins 11/19/21; 14 and 17 are the remaining switches inside the
  same contiguous run, exactly where the documented `LT[L,T,sw] MT[L,T,sw]
  HT[L,T,sw] RS[L,sw] CP[L,sw]` layout puts them.

**This mattered more than expected.** Reading those five bytes out of the
shipped songs, **17 of the 18 play at least one shared column that we got wrong
before this fix**: four songs (Calm, G-Spot, Transfusion, TGV's KiloMix) had
all five wrong. It explains the long-standing "toms sound an octave low"
impression: those songs were asking for congas.

Implemented at TRIGGER time (`fireDrum`), not at load time, so pattern data
stays byte-faithful to the file and the writer round-trips unchanged.

## 2. Accent: SOLVED, and round 2 had it backwards

Round 2 concluded the accent KNOB globally boosted every hit. That came from
comparing `bd_ref` against `d808_accent`, which varied knob and row together.
Batch E crosses them, and the answer is the opposite:

| file | vs `k0_norow`, accented steps | unaccented steps |
|---|---|---|
| `acc_k127_norow` | **+0.00 dB** | +0.00 dB |
| `acc_k0_row` | **+0.00 dB** | +0.00 dB |
| `acc_k64_row` | **+9.51 dB** | −0.2 dB |
| `acc_k127_row` | **+13.89 dB** | −0.1 dB |

The knob does nothing on its own. The row does nothing at knob 0. Unaccented
hits never move. Fitting `gain = 1 + a*k^p` returns **p = 1.0006**, exactly
linear:

```
accent gain = 1 + 3.95 * (knob/127),  on accented steps ONLY
```

Our implementation now reproduces this to within 0.12 dB at every knob setting.

Two notes on method. Onset detection is useless here (a louder accent raises the
file's peak, which moves the 15%-of-peak threshold and silently changes the hit
count: 34 / 31 / 24 across the three settings); these numbers are step-locked
to the known grid instead. And the sequencer used to scale unaccented hits by
0.78 before calling `trigger()`, double-counting the accent flag; that is gone,
folded into the trim tables so no measured level moved.

---

# Round 3 also: the SYNTHESIS path, calibrated

Both drum machines decide per voice, at render time:

```cpp
if (!smp_[v].empty()) return renderSample(v);   // a loaded sample wins
```

Every voice has a full clean-room synthesis model; `setSample()` overrides one
voice with PCM, so a partial sample set yields a hybrid kit. Everything measured
before this round was measured in SAMPLE mode, leaving synthesis uncalibrated.
It has now been held against the same exports, rendering with no sample
directory. What that found:

| voice | was | now |
|---|---|---|
| **BD** | the knob swept PITCH 28.6 → 68.6 Hz | it is TONE: pitch holds at ~50 Hz, the knob trades body for click (peak −4.4 dB, 500–2000 Hz band +12.4 dB) |
| **BD decay** | tau 0.025 → 0.343 s | 0.038 → 0.273 s, matching |
| **SD** | both partials transposed together, 151 → 214 Hz | partials FIXED at 180 / 330 Hz; TONE balances them, as ReBirth does (dominant 178 Hz at low tone, 330 Hz at full) |
| **SD snappy** | `0.7 + snap*0.8`, 70% noise at zero | near-linear with a true zero, matching the export's 25.6 dB HF range |
| **SD decay** | tau 0.152 s | 0.027 s |
| **toms/congas** | LT 80 / MT 120 / HT 165 Hz, ±6 semitone knob, tau ~0.30 | measured pitches (83.6 / 129.2 / 174.6, congas 183.5 / 269.8 / 382.2), ±2 semitones, per-voice tau 0.033–0.094 |
| **CB** | band-pass at **2640 Hz** (above both oscillators) and `f2` never set; centroid 2251 vs ReBirth's 902 | both stages on the 810 Hz partial; centroid 782, dominant 808.6 exact |
| **CH / OH / CY** | identical filters, differing only in decay | split per voice: CH top-weighted (centroid 13444 vs 12560), OH peaked 4–8 kHz (9268 vs 8890) |
| **CY tune** | ±2.5 semitone varispeed | a spectral TILT: ReBirth's dominant holds at 7742.7 Hz across the whole knob. This was wrong in the SAMPLE path too, not only synthesis. |
| **hat choke** | sample path only | both paths |

Trims now live in **two tables**, `kTrim808` (sample) and `kTrimSyn808`
(synthesis), selected per voice. They are applied at the output stage, so one
table cannot serve both paths: a synthesized voice and a sampled one have no
reason to arrive at the same level.

After this, every isolated voice lands within ~1 dB of its export, and the
pitch/decay laws track across their knobs.

---

---

# Round 4 (2026-07-25, batch F): the leftovers, closed

## Tom switches confirmed, and all three toms finally measured

Batch F is the first fixture set to put a tom-column switch at **1**. Before it,
every fixture defaulted to 0, so ReBirth played congas and the tom rows had
never sounded in any measurement.

| fixture | ReBirth dom | raw tom | raw conga | verdict |
|---|---|---|---|---|
| `d808_tomsw_lt1` | 84.0 | 83.6 | 183.5 | **tom** |
| `d808_tomsw_mt1` | 130.0 | 129.4 | 269.8 | **tom** |
| `d808_tomsw_ht1` | 174.0 | 174.8 | 382.2 | **tom** |

So offsets **+14 (MT) and +17 (HT) are now measured, not inferred**, and MT/HT
no longer borrow LT's trim. All 16 synthesis trims are direct measurements.

## Decay knobs do NOT trade level for length

Round 3 flagged a possible level/length trade after seeing the cymbal drop 15 dB
at full decay. Sweeping each knob properly says otherwise:

| knob | level change across the full sweep |
|---|---|
| cymbal decay (0 → 127) | **+0.12 dB** |
| bass drum decay (0 → 127) | **+0.94 dB** |
| open hat decay (0 → 127) | **+0.71 dB** |

The apparent 15 dB was entirely the onset-detector artifact suspected at the
time: at long decay the envelope never falls back under the threshold, so the
detector reports mid-decay points instead of hits. Nothing to model.

## Two bugs this fixture exposed

- **The tom pitch envelope was far too deep.** At +90% initial pitch the sweep
  smeared so much energy above the base that our strongest bin sat at 188 Hz on
  a 129 Hz mid tom. ReBirth's toms cluster within ~2 Hz of the base. Now +16%
  over 16 ms, and the dominants line up (84.0|84.0, 130.0|132.0, 174.0|178.0).
- **A column routed to its alternate voice ignored the song's level.** One
  fader drives both voices of a shared column, but only the primary was being
  set, so congas, claves and maracas played at the engine's default instead of
  whatever the song asked for. They inherit the column's level now.

---

---

# Round 5 (2026-07-26): the residuals

## Hat/cymbal fizz: FIXED

The three metal voices high-passed the oscillator bank and nothing bounded the
top, so a high-pass alone passed everything to Nyquist. Feeding the second
filter from the FIRST one's high-pass output makes the pair a band rather than a
shelf, and its corner then maps almost 1:1 onto the measured 85% rolloff:

| | ReBirth rolloff | before | after |
|---|---|---|---|
| closed hat | 15401 | 19181 | **15481** |
| open hat | 11698 | 16627 | **11998** |
| cymbal | 11898 | 12001 (cent had regressed) | **12001** |

Centroids came along with it: CH 12560 vs 11670, OH 8890 vs 8360, CY 8186 vs
7722. First attempt over-corrected (rolloff 13072, and the cymbal's centroid
collapsed to 5733 from a previously-good 8334) because the corners were set too
low; raising them to 16.5k / 13k / 15k landed it.

## Snare brightness: improved, with an honest trade

The snappy level was linear in the knob, which matched snappy-127 exactly but
left mid-knob too bright. The fix is the exponent, not the level. Chosen on
total |centroid error| across the three snare fixtures:

| exponent | reference | tone 0 | tone 127 | total |
|---|---|---|---|---|
| 1.0 (was) | 688 | 1099 | 255 | 2042 |
| 1.25 | 277 | 702 | 586 | 1565 |
| **1.5 (ships)** | **100** | **342** | 868 | **1310** |

^1.5 is much the best on the reference snare and at tone 0 and the worst at
tone 127. Worth noting the two metrics disagree about ReBirth's own tone-127
dominant (the wide-window measurement reads 330 Hz, the fixture harness reads
180), so that fixture is the least trustworthy of the three.

---

# Round 6 (2026-07-26): the snare tone knob, settled

The two metrics disagreed about ReBirth's dominant at tone 127 (330 Hz
wide-window vs 180 in the fixture harness), which made it unsafe to fit. Settled
by reading each candidate partial DIRECTLY, per hit, step-locked, no argmax
over a band, so no dependence on where a search window stops:

| 330 minus 180 | tone 0 | tone 64 | tone 127 |
|---|---|---|---|
| **ReBirth** | −23.9 dB | −11.5 dB | **−2.3 dB** |
| ours, before | −12.0 | −3.6 | **+18.8** |
| ours, after | **−23.9** | **−11.5** | **−2.3** |

**ReBirth is 180-dominant at every setting.** The knob brings 330 toward parity
and never past it. The fixture harness was right; the wide-window read was the
misleading one. Our old crossfade inverted the pair, so 330 took over entirely.

Holding 180 at unity and scaling only the upper partial reproduces the curve
exactly: `r = 0.0638 * exp(3.1849*t - 0.6981*t²)` fits all three points.

## And tone also drives the NOISE

With the body correct, a systematic residual stood out: every snappy exponent
was too bright at tone 0 and too dark at tone 127. No tone-independent noise
level can produce that, so the 808's tone knob sets overall brightness rather
than only the body balance. Adding a `(0.55 + 0.9*tone)` factor:

| | target | flat noise | tone-dependent |
|---|---|---|---|
| sd_ref centroid | 1888 | 2001 | 2008 |
| sd_tone000 | 1229 | 2066 | **968** |
| sd_tone127 | 2134 | 1615 | **2436** |
| **total error** | n/a | 1469 | **683** |

tone-127's 85% rolloff also went 4026 → 5598 against ReBirth's 5652.

A process note worth keeping: the snappy exponent had to be **re-fitted** once
the body was right. ^1.5 won while the body was still wrong; ^1.2 wins now.
Fitting two coupled things at once would have locked in the wrong pair.

## Still open

1. **Snare centroid at tone 0** is now 968 against 1229; we overshot slightly
   in the other direction. Within ~260 Hz on a metric that spans 1229–2134, and
   the two ends now bracket the target rather than both falling short, so this is
   refinement rather than a wrong model.

## Verified NOT bugs

- **Clap and rimshot play their samples correctly.** The apparent mismatch in
  round 2 was the shared-column selector, now solved above.

---

# Round 7 (2026-08-17): supertask 2 deep dive, sample path

Branch `808-parity`. Measured against ReBirth's built-in 808 (the default
`Standard Components` samples, converted to canonical WAVs), with the step-locked
scorer `tools/measure/score808.py` and the 41 isolated fixtures from
`gen_batch808complete.py`. One method both sides, level offset reported
separately. Full per-voice control map is in `COMPONENT_MATRIX.md` section 2.

**Key lesson reconfirmed: fit decay before trim.** The BD "7.6 dB level deficit"
was a decay error in disguise: our BD decayed ~2x too fast, so the integrated
RMS ran low even though the peak was only ~5.6 dB down. Fixing the decay first
collapsed the trim to a clean 0.4 dB scalar.

## Tom tune: fitted (commit 32ef222)
Only `lt_tune` (byte p[10]) was decoded into a single shared knob, so the mid and
hi tom tune knobs did nothing, and a spurious ~2.1x varispeed factor put every
tom ~an octave high. Now three tunes decoded (p[10]/p[13]/p[16]) and each tom
plays its sample at natural pitch at centre with +/-2 semitones. Measured centres
~86/129/172 Hz (LT/MT/HT); ours tracks ReBirth within one FFT bin.

## BD voice: fitted (commits ac5c4d3, 595f1de)
- **Tone holds pitch.** The knob is Tone, not tune; ReBirth pins 48.4 Hz across
  it. Our sample path repitched +/-1 semitone (dominant drifted to 53.8); dropped.
  A click-band probe shows no tone-driven brightness on the sample, so no filter.
- **Decay** re-fit to `tau = 0.040 * 7.063^(decay^0.776)` s (RB: 40/82/120/287 ms
  at decay 0/.25/.5/1). Because the envelope multiplies the ~0.287 s sample ring,
  the envelope tau is the reciprocal remainder, flat near the top. Tracks within
  a few ms across the sweep.
- **Trim** `kTrim808[BD]` 0.1248 -> 0.2349 (+5.5 dB) once the decay matched;
  level within +/-0.4 dB.
- Residual (measured, not modelled): +4.5 dB boom at the exact tone-0 minimum
  only (tone 32+ flat).

## SD voice: fitted (commit 101d4b4)
ReBirth's snare is a tone body variant plus one of four REAL snappy-noise
recordings (`808_n1..4`, all ~6.4 kHz). The loader never loaded them; the device
synthesised noise at ~2.2-3.4 kHz, too dark, so the snare undershot ReBirth's
brightness. Now the loader pre-mixes body + real noise and mutes the synth noise
(mirroring the 909 path), with `g = 0.32 * snappy^2 * (0.70 + 0.45*tone)`. Snappy
centroid tracks within ~10% (RB 169/844/4378), tone within ~20% (exact at
centre). Trim `kTrim808[SD]` 1.5210 -> 0.826 (-5.3 dB); level within +/-0.9 dB.
Residuals: ReBirth's tone saturates at the top where our linear tone factor keeps
rising slightly; tone/snappy are latched at load by the pre-mix (no automation),
the same limitation as the 909 sample path.

## Still open at the end of round 7
CY tone tilt on the sample path, CY decay, OH decay, the CH+OH one-step hi-hat,
the spurious tom/CH decay envelopes, and every metallic voice's trim. All of these
are addressed in round 8 below.

---

# Round 8 (2026-08-18): CY, OH, the hi-hat, and a measurement correction

Branch `808-parity`. Finished the sample-path voices. Fixtures are the wide-spaced
`cyd`/`ohd` decay sweeps plus the `ch`/`cb`/`rs`/`cl`/`cp`/`ma`/`mtc`/`htc`
references in `fixtures-2026-08-18/808cyoh`, exported unattended from ReBirth.

## The measurement correction that reset every trim (the biggest finding)
`score808`'s `avg_hit` summed the step-locked hit windows **coherently** (time
domain) before taking RMS and the PSD. ReBirth's hits are not sample-aligned:
oscillator and noise phase are not reset per hit, so a **tonal or high-frequency
voice partially cancels** under coherent averaging and reads far too quiet. This
is exactly the "never null-test against ReBirth" trap the measurement protocol
(`docs/MEASUREMENT_PROTOCOL.md`) warns about, reached through the back door of a coherent average. The error grows with
pitch and noise content: BD ~0 dB, LT tom 0.6, HC hi-conga 4.9, CY 6.5, OH 7.7,
CH 13.3, CB (a near-tone) 15.7.

Refit **every** sample-path trim by **power averaging** (mean of the per-hit RMS,
no coherent sum). Two independent checks say this is right: the six tom/conga
trims, which coherent averaging had spread across 0.30 to 0.36 (higher pitches
cancelling more), collapse onto one shared **~0.385**; and the percussion voices
(RS/CP/MA/CB/CY) land together near ~0.24-0.25. `score808` now power-averages both
the level and the spectrum (per-hit PSDs averaged in the power domain), so the
tool cannot walk into this again. It also gained a `--wide` flag for the
one-hit-per-bar fixtures. **BD/SD were fit this way in round 7 and inherited the
bug**: BD was fine (low frequency) but SD was 4.6 dB quiet and is now corrected.

## CY Tone: a moving-pivot tilt (was inert on the sample path)
The knob was synth-only, so it did nothing to the loaded sample. Measured in
absolute band energy from the `cyt` sweep, ReBirth's CY Tone is a first-order tilt
whose **pivot moves with the knob**: tone up cuts the body broadly (0-1 kHz -9.1
dB, cut reaching ~7 kHz) and lifts the sizzle (16-20 kHz +4.0 dB); tone down adds a
NARROW low thump below ~2 kHz and slightly dulls the top. The 11.6 kHz dominant
never moves and the hit runs ~2.7 dB louder at full tone. A fixed pivot cannot do
both the broad cut and the narrow boost, so `fc` rises 1.5 -> 8.5 kHz across the
knob. Implemented as a one-pole tilt on the CY sample; fit residual **1.2 dB RMS**
across the sweep.

## CY Decay: exponential, sample-limited at the top
CY is a genuine exponential ring. Re-fit `tau = 0.0432 * 37.037^decay` s by
matching the dB envelope curve on the wide-spaced sweep. Tracks RB's -30 dB length
(85/170/230/395 ms at decay 0/.25/.5/.75) within a few ms; the top is
**sample-limited**: RB reaches -30 dB at 820 ms, our recording only rings to
~525 ms, so max decay cannot match (documented residual, same class as the toms).

## OH Decay: a GATE, not an exponential
The 20 ms envelope dump was decisive: ReBirth's open hat follows the sample's
natural decay to a hold time set by the knob, then drops to **digital silence**
(-182 dB). It is a gate with a fast release, not an exponential. Modelled as: hold
the natural sample to `T_gate = min(0.018 + 0.721*decay, 0.585)` s, then a 16 ms
release. Matches RB's -30 dB length (75/245/415/565 ms) to within a few ms at every
knob.

## Tom and CH decay envelopes: removed (they were spurious)
Only BD, OH and CY have a Decay knob (MIDI map p196). The toms and CH carried a
~116 ms envelope with no knob behind it, which truncated the natural tom ring 19-39
dB low by 250-400 ms. Removed; tom tails now match RB within **0.2 dB** through the
audible decay. Removing it made the toms/congas ring their full tail and thus 2-6
dB hot, which the power trim re-fit above absorbs (decay-changed-so-trim-moves).

## CH + OH on one step: the short open hat
ReBirth plays a very short OPEN hat (manual p149); we were playing the closed hat
(our `choh` output was spectrally byte-identical to the raw CH). Fixed: OH fires
just before CH in the same block, so when CH sees OH already playing at position
zero it keeps OH, applies a ~28 ms choke, and suppresses the CH hit. A CH that
chokes an already-ringing OH from an earlier step still mutes it and plays the
closed hat. Our `choh` now matches RB's rolloff (11822 vs 11789) and short decay.

## The "metallic resonance" was a coherent-averaging artifact too
The first pass of this round reported a strong narrow ~11.6 kHz resonance on all
the metallic voices (CY/CH/OH/RS) that the broadband samples lacked. That was
measured with the old coherent `avg_hit`, and it was fiction: the 11.6 kHz
component is simply the least-cancelled under coherent averaging, so it stood up
as a fake peak. Re-measured with power-domain per-hit PSD averaging, ReBirth's
metallic voices are broad, not peaked, and:

- **CH matches exactly** (centroid 12702 vs 12758, 85% rolloff 16306 vs 16306).
- **OH matches exactly** (centroid 8903 vs 8904, rolloff 11784 vs 11784).
- A byte-for-byte check confirms `samples-standard-808` == `samples-original` (the
  extracted ReBirth samples) for every metallic voice, so the samples are right and
  there is nothing to add for CH/OH.

There is **no shared metallic resonance and no filter to build**. Do not chase it.

## Cymbal brightness: fitted (the one real metallic residual)
ReBirth darkens the cymbal relative to its own raw sample (85% rolloff 13867 in
the recording, 12155 in playback) while leaving CH and OH untouched, so our cymbal
ran ~1.7 kHz bright in centroid. It is a gentle fixed **~3 kHz one-pole lowpass**:
that single corner matches both the windowed centroid/rolloff (8475 vs 8449, 12242
vs 12155) and the centroid fall across the whole decay (9437 vs 9415 at the attack
down to 6328 vs 6261 at 300 ms). Applied after the tone tilt, tone-independent, so
the tilt fit is undisturbed (still 1.2 dB). It cut ~9.7 dB of HF, so the CY trim
went 0.2256 -> 0.70. It also stripped the fast-decaying HF that had made max decay
look sample-limited, so the CY decay law was re-fit with the lowpass in place to
`0.0400 * 33.00^decay`; a single exponential still cannot hit both the mid and the
top, so it balances them (mid ~20% long, top ~8% short); the residual is the
cymbal's non-exponential envelope shape.

---

# Round 9 (2026-08-23): residual polish, and a confounded SD fit found

Branch `808-residuals` (off `main` after the round-8 merge). Polishing the
documented residuals turned up that one of them was not a small residual at all.

## SD snappy/tone: the round-7 fit was confounded, re-fit under power averaging
Measured with the corrected power-domain `score808`, our snare was **~5x too dark**
across the whole snappy range (centroid 788 vs ReBirth 3619 at snappy 64, verified
on single isolated hits so it is not an averaging artifact). The round-7 noise-mix
gain was fit under coherent averaging, which cancelled ReBirth's non-aligned snare
noise and made it read dark, so our gain came out ~4x too quiet at mid snappy. The
"SD tone saturation" residual was the visible tip of this.

Re-fit the noise-mix gain `g(snappy)` in `sampleset.cpp` under power averaging,
matching the per-hit centroid across the sweep. Two corrections: the gain is
**tone-independent** (the tone brightness comes from the body variant `sdv`, not
the noise, so the old `(0.70 + 0.45*tone)` term was wrong), and it is an **S-curve
in snappy** (needed g 0 / 0.055 / 0.26 / 0.62 / 0.81 at snappy 0 / 32 / 64 / 96 /
127) fitted to a clamped cubic. The snare now tracks ReBirth: default snappy within
~8% centroid, extremes within a few percent. Trim re-fit 1.40 -> 1.17 for the
louder noise. Residuals: snappy 32 reads a little dark (the cubic's soft floor),
and max snappy runs ~2 dB quiet (the take-4 noise level); tone/snappy are still
latched at load (no per-hit automation), the same accepted limit as the 909.

## BD tone-minimum quirk: three things, and two ear corrections
The BD Tone knob is inert on the sample across tone 16..127 (ours matches RB there),
but at the very bottom (tone 0..8) ReBirth does THREE things to the kick, and it
took two rounds of Luke's ear to find them all. Measured tone 0 vs tone 64 in a
clean early window: **fundamental -4.0 dB, harmonics 70-200 Hz -6.7 dB, harmonics
200-600 Hz -13.7 dB**. So:
1. **Level**: the body runs ~4 dB quieter (`bdToneGain`). This was the first pass;
   it nulled the RMS but the kick still sounded too open.
2. **Gate**: ReBirth cuts the kick to silence at ~200 ms, where ours rang past 380
   ms (`bdGate_`, ~183 ms then a 2 ms release). Luke's ear caught this; a steady
   spectrum cannot see it because tone 0 and tone 16 share the first 180 ms.
3. **Hi-cut**: the harmonics are rolled off progressively more than the
   fundamental, a ~100 Hz one-pole lowpass (`bdLp_`). Luke's ear (and a spectrogram)
   caught this after the gate: ours still had the low-mid harmonic "character" that
   made it read louder and brighter, while ReBirth's tone-0 is nearly a pure thud.
All three engage below tone ~0.10 and clear by tone 16. After the hi-cut the tone-0
harmonics sit within ~0.5-1.7 dB of ReBirth (was +7 to +14) and the level nulls
across the sweep. Two clean cases of the ear catching what a steady-spectrum metric
missed, exactly the measurement-protocol rule about widening the measurement. The faint HF
buzz ReBirth adds at the minimum sits at -84 dB and is left unmodelled.

## CY decay shape: two-stage tried, single exponential kept
The round-8 CY decay leaves the mid ~20% long and the top ~8% short. A two-stage
(fast head + slow tail) envelope was fitted against RB's envelope curves across the
sweep and did NOT materially improve it: the mid stayed long (270-485 ms vs RB
230-395) and only the near-inaudible top tightened slightly. The mid-long residual
is inherent to sample x envelope, not the envelope order, so the single exponential
`0.0400 * 33.00^decay` stands. Left as an accepted residual.

## Still open for supertask 2 (accepted residuals)
- CY decay: mid ~20% long, top ~8% short (single-exp limit; two-stage did not help).
- SD: snappy-32 a little dark (cubic floor), max snappy ~2 dB quiet (take-4 level),
  tone/snappy latched at load (no per-hit automation, same as the 909).
- BD: the -84 dB tone-minimum buzz is unmodelled (inaudible).
