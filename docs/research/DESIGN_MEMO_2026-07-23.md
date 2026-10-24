> **SUPERSEDED IN PART 2026-08-04.** Kept for its reasoning trail. Its structural
> verdict, that ReBirth's 303 filter is a cheap non-oversampled four-pole ladder
> with one inexpensive nonlinearity rather than a physical circuit model, has
> held up: the four-pole reading was independently confirmed on 2026-07-31 from
> the skirt slope. What has moved since is everything about the ENVELOPE, which
> this memo predates entirely. The env mod knob turned out to be bipolar and
> symmetric about the cutoff corner, the accent turned out to need an attack, and
> the filter's live constants are all in `PLAN_303_ENVELOPE.md`, whose section 0
> also states which part of the remaining gap is a refit and which is a
> parameterisation limit. Its 808 section is superseded by `DRUM808_FINDINGS.md`
> round 3, which found the shared-column selector and the sample-versus-synthesis
> path split. Read this for the research pass and the reasoning, not for current
> state or for any number.

# Reborn Design Memo: ReBirth 303 Filter, Envelope/Accent, and 808 Section

Synthesized 2026-07-23 from a five-topic research pass (Open303 source,
diode-ladder literature, TB-303 circuit analyses, ReBirth-era lore, TR-808
circuits; 88 sources consulted). Clean-room inputs only: published papers,
schematics, manuals, MIT-licensed Open303. No ReBirth decompilation.

## 1. VERDICT

**ReBirth's 303 filter is a cheap, fixed-44.1 kHz, non-oversampled 4-pole
ladder with an explicit unit-delay-era discretization, one inexpensive
nonlinearity in the resonance loop, calibrated by ear/measurement to a real
TB-303's knob ranges. It is not a physical diode-ladder circuit model.**
Confidence: high on "cheap 90s ladder + cheap nonlinearity + aliasing"; medium
on the exact stage topology (diode-flavored coupled Euler vs. buffered
Moog-style with unit delay); both fit the data, and the knob-map is a free
parameter that masks the difference.

Evidence, weighed against our four measured signatures:

- **Feasibility/culture (decisive):** min spec Pentium-90 running two synths +
  two drum machines + FX at 44.1 kHz (ReBirth 2.0.1 manual pp.182,187; SOS
  reviews). No public coupled nonlinear diode-ladder formulation existed until
  ~2008+ (Zavalishin TPT is 2012; Huovilainen per-stage tanh is 2004). The
  era's toolkit was Stilson & Smith ICMC-96 (unit-delay ladder + tuning
  tables) and Rossum ICMC-92 (one clipper inside the loop). The 808/909
  sections are licensed sample playback with tiny per-knob DSP (manual
  pp.97–98; "Sound recordings (c) 1984 Roland Corporation"), the same
  pragmatic culture.
- **Designer lineage:** DSP author Peter Jubel designed the Nord Lead 1 (SOS
  Propellerhead profile), whose filter "will ring convincingly at maximum
  resonance, [but] you cannot persuade it to self-oscillate" (SOS Nord Lead
  review). Our "never a pure whistle" signature is Jubel's known design
  signature, and matches the hardware 303, whose stock feedback is ~half of
  self-oscillation (Whittle, Devil Fish manual; Stinchcombe).
- **Signature 1, HF comb climbing with the reso knob:** a linear filter of
  ANY topology cannot produce this: Open303's shipped diode-ladder mode is
  fully linear and produces no comb. Physical diode ladders at 303 drive
  levels are essentially linear (Zavalishin §5.10 fn.13). A clipper inside the
  loop at 44.1 kHz with no oversampling is exactly the Rossum-92 recipe: more
  feedback → more clipping → more aliased harmonics folding into a dense
  comb. **A digital artifact to reproduce, not analog physics to derive.**
- **Signature 2, emphasis 519→2584 Hz, compressing at top:** the slope
  matches the measured real-303 cutoff map almost exactly
  ((2394/314)^(100/127)=4.95 vs 2584/519=4.98; Open303 measured constants);
  the 2584 Hz top matches the stock 303's ~2.5 kHz max resonant frequency
  (Devil Fish manual). The top-end compression is the fingerprint of a
  **non-prewarped discretization at 1×** (Huovilainen DAFx-04 eq.23;
  Stilson-Smith).
- **Signature 3, bandwidth broadest at LOW cutoff:** matches both the
  Stilson-Smith bilinear-with-unit-delay case and the hardware's AC-coupled
  resonance loop (dominant loop HP pole ~92 Hz, Stinchcombe; Open303 models it
  as a 150 Hz feedback HP).
- **Signature 4, never a pure whistle:** hardware-accurate AND Jubel-typical
  AND automatic if k is capped below threshold with a quenching clipper.
- **Contemporary ears (SOS Aug 1997, Chris Carter A/B):** "slightly brighter,"
  "slight digital edge at the upper limit," extended filter range, weaker
  accent: precisely this artifact profile.

Uncertainty flag: the earlier "emphasis = baseCut/sqrt(2) diode fingerprint"
was computed against our own miscalibrated baseCut and is NOT independent
evidence for diode topology.

## 2. THE FILTER RECIPE (implemented as `REBORN_FILTER=rb`)

Primary bet: forward-Euler coupled diode ladder (Open303 TB-303 structure, ala
mystran/kunn; Zavalishin Eq.5.18 structure) run at native rate, no
oversampling, one cheap clipper in the feedback branch.

```
fc(knob) = A * (2394.411/313.815)^(knob/127)     # exponential; fit A
r(res01) = (1 - exp(-3*res01)) / (1 - exp(-3))   # Open303 resonance skew
wc = 2*pi*fc/fs;  fx = wc / (2*pi*sqrt(2))
b0 = (0.00045522346 + 6.1922189*fx) / (1 + 12.358354*fx + 4.4156345*fx^2)
                                                  # refit scale at 1x (RB_B0S)
k  = 17*r, capped at kmax (~16-17: rings near-critically, never sustains)

u   = clip(k * y4)          # cubic soft clip (clamp +/-sqrt2; x - x^3/6)
fb  = HP1(u)                # one-pole HP, corner 90-150 Hz
y0  = in - fb               # input normalized DOWN (~x1/16) so k*y4 crosses
                            # the clipper knee only at high resonance
y1 += 2*b0*(y0 - y1 + y2)   # first pole runs at double rate
y2 +=   b0*(y1 - 2*y2 + y3)
y3 +=   b0*(y2 - 2*y3 + y4)
y4 +=   b0*(y3 - 2*y4)      # last stage leaks to ground
out = 2*(1+r)*y4 * makeup
```

Nonlinearity placement: exactly ONE, on the y4 feedback tap (antto's
configuration that matched a real unit; the Rossum-92 idiom). No per-stage
tanh. Stability: clamp instantaneous fc <= fs/6, b0 <= 0.3, hard state clamp
+/-4. Oscillator: naive (aliased) saw is the plausible ReBirth source; the
`t303_rawosc_*` fixtures discriminate. Fallback if fit residuals stay bad:
Stilson-Smith ICMC-96 bilinear ladder with explicit unit delay, same clipper.
Do NOT use TPT/ZDF for parity: it removes the warping being matched.

First fit (sysid_c020..120, 2026-07-23): A=430, kmax=17.0, HP=90 Hz, in=1/16,
b0 scale 0.7 → emphasis EXACT at cutoff 20/45 (392/523 Hz), broadband comb
present (flatness right decade at every cutoff), but the emphasis curve
compresses too much at the top (1570 vs 2090 Hz at cutoff 120; both engines
lock emphasis onto note harmonics) and prominence runs 2-3 dB shy at high
cutoffs. Next fit axes: cutoff-map slope/anchor jointly, kmax/clipper shape.

## 3. ENVELOPE / ACCENT / VCA

Open303's measured architecture (MIT, from hardware): MEG = one-shot exp decay,
no attack; normal decay 200-2000 ms, **accent forces 200 ms**; applied in
log2-frequency with measured bipolar coupling (envScaler/envOffset constants in
rosic_Open303.cpp). Accent: MEG->200ms, filter gets accentGain*rc2(env) with
rc2 ~15 ms smoother (the C13 "wow" cap); C13 charge ACCUMULATES across rapid
repeated accents (successive wows ride higher, bleed ~0.5 s), lag scales with
the resonance pot's second gang. Amp env: instant attack, exp decay ~1230 ms
toward sustain 0, release ~1 ms (50 ms accent), plus **MEG bleed into the VCA**
(ampOut = ampEnv + 0.45*env + 4*accentGain*env on accent).

Reconciliation with our plucky-VCA finding (AMP_DECAY 0.06 / SUS 0.08,
re-pluck on struck notes): the fast pluck is most plausibly the MEG bleeding
into the VCA; its time constant would then TRACK THE DECAY KNOB. The
`t303_decay032/127` fixtures discriminate. Slides: retrigger nothing, slew
pitch only (Open303: one-pole slew in Hz, tau=60 ms; gate ~58% of a 16th).
Version trap: ReBirth 2.0 lengthened synth env decay; the "Vintage ReBirth
Synth Sound" switch restores 1.5 behavior and is auto-enabled by old songs
(manual p.49). Determine which mode reference WAVs used before fitting decay.

## 4. 808 PLAN

**Sample playback, definitively. Do not synthesize for parity.** ReBirth's own
manual (Mods appendix pp.97-98) documents the engine; the resource-DLL samples
match its snare architecture exactly.

| Voice | Engine (manual) | Knobs |
|---|---|---|
| BD | sample x exp decay env -> tone LPF | Tone, Decay, Level |
| SD | crossfade of 5 body samples (Tune) + 4 HP-noise samples in RANDOM order, gain = Snappy | Tone, Snappy, Level |
| Toms/congas | single samples, Tune = varispeed | Tune, Level |
| RS/CL, CP/MA, CB, CH | raw one-shots x Level | Level |
| CY | sample x decay env -> tone LPF | Tone, Decay, Level |
| OH | sample x decay env; CHOKED instantly by CH | Decay, Level |
| Accent | per-step gain scaled by Accent Level knob, all voices equally | Accent Level |

Parity rules: all voices monophonic hard-cut on retrigger except BD; CH chokes
OH; SD noise-sample random order is a nondeterminism source (match per-hit
against all four candidates). Black-box fit three laws from the batch A/B
fixtures: tone-LPF curve, decay-envelope law, varispeed + interpolation type.
Match ReBirth, not the hardware 808 (ReBirth deviates: toms have Tune, SD
"Tone" repitches the body; shipped samples run ~a tone high per Carter).

## 5. DISCRIMINATING MEASUREMENTS (ordered by information yield)

1. Bass-vs-reso at low cutoff: fundamental constant => HP in feedback loop;
   sagging ~1/(1+k) => DC-coupled, drop the HP.
2. Raw-oscillator aliasing (`t303_rawosc_*`): comb present without resonance
   => oscillator is naive; isolates osc vs filter contributions.
3. Comb fold-structure at several notes: peaks tracking |n*f0 - m*44100|
   confirms aliasing origin.
4. Pluck-tracks-DECAY (`t303_decay*`): tracks => MEG-bleed VCA model.
5. Two-model fit-off: Euler diode ladder vs bilinear unit-delay ladder on the
   emphasis + flatness curves.
6. Repeated-accent build-up at high reso: successive sweeps climbing => C13
   accumulation modeled.
7. Vintage-mode audit: confirm which synth-decay mode every reference WAV used.
