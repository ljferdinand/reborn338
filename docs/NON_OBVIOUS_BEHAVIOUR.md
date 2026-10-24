# Non-obvious behaviour in this code

Several of the things below look like bugs and are not. Each was established by
measuring ReBirth's output, and each is cheap to break by accident. Read this before
"fixing" anything in the engine, the file readers or the effect rack. Where a finding
has a fuller record, the file named holds it.

## The 303

- **The default 303 voice is the native TPT voice** (`include/reborn/factories.h`).
  The Open303 voice is selectable but is not a parity voice, and it renders about
  15.65 dB quieter than ReBirth (`BACKLOG.md`, entry 15). Never measure anything
  level-dependent on it. `reborn_render` reads `REBORN_BASS_ENGINE=tpt|open303` to
  swap both bass slots; set it explicitly on both sides of any before-and-after
  comparison, because an earlier default made every 303 constant invisible to a
  regression run that did not.
- **The 303 VCA re-plucks with a 1.2 ms attack, not an instant jump.** The instant
  jump was a step discontinuity and caused a flat broadband high-frequency shelf
  that survived four wrong hypotheses.

## The drum machines

- **Drum track automation param 0 is a mute that mutes when the value is ZERO.**
  Inverting it silences whole demo songs. In one demo song (Calm) the 808 really is
  silent for the whole piece.
- **The 808 has five shared columns** (LT/LC, MT/MC, HT/HC, RS/CL, CP/MA), selected
  by switch bytes at `808` chunk offsets +11, +14, +17, +19 and +21. The alternate
  voice inherits the column's level. Getting this wrong mis-voices almost every demo
  song.
- **Two trim tables per drum machine**, `kTrim808` and `kTrimSyn808` (and the 909's
  `kTrim909` and `kTrimSyn909`), one for the sample path and one for the synthesis
  path, chosen per voice by whether a sample is loaded.
- **808 accent depth is global**, and the accent knob alone does nothing: it scales
  only the steps whose accent row is set. The manual agrees that the accent has equal
  effect on all sounds.
- **The accent column is a VALUE, not a flag.** The 808 writes 1 there and the 909
  writes 2, in every shipped song and never mixed, and ReBirth scales the accent
  depth by it. Reading it as a boolean ran every 909 accent at half strength.
  `tools/rbsgen` writes each machine's own value.
- **The 909 cell value is its own axis**: 1 soft, 2 normal, 3 and above flam.
  Accent is column 0 on both machines.
- **A drum TRAK's param N is header byte N**, and the file's byte order is not the
  manual's MIDI order: bytes 20, 21 and 22 are rim shot, clap and the shared hi-hat
  level, where the manual's MIDI table lists the hi-hat first. Building the table
  from the MIDI map silences the hats, clap and rim.
- **The drum device buses are LINEAR and must stay linear.** Both used to carry a
  `tanh`, which capped a fully stacked 909 at -23.5 dBFS where ReBirth reaches
  -3.3. ReBirth's drum bus has no saturator at all. The master output is a hard
  clamp for the same reason: ReBirth runs straight to full scale and then clips.
  Any saturator in the chain is a parity surface; if nobody can point at the
  measurement that put it there, it is a bug.

## The mixer

- **The faders are exponential and the pan law is not equal-power.** Both faders run
  at about 0.370 dB per byte. Pan holds the NEAR side at unity and only attenuates
  the far side, so the centre sits 3.01 dB above a hard pan. Each law is anchored
  where a generated fixture lands (fader 100, pan 64), which is also why no fixture
  could ever see them: a control whose fixture value never moves has not been
  measured, however many fixtures use it.
- **Every summing path applies the pan law exactly once.** The effect rack is
  per-section chains, not shared buses (see below), and each section is panned once
  through `panGains`. An earlier PCF bus that summed without the pan law ran every
  PCF render 3.01 dB hot.

## The effect rack

- **The effect rack is PER-SECTION CHAINS, not shared buses.** ReBirth's panel shows
  one unit per effect, and every routed section passes through a unit with those
  settings, but the sections do not meet inside it: with two sections distorted and
  the PCF shut on one, ReBirth takes 15.2 dB off that section and 0.01 dB off the
  other, and two 303s distorted together are the power sum of each alone. So there
  is one set of knobs and four independent paths, chained distortion, then PCF,
  then compressor.
- **The effect TRAK order is DELY, DIST, PCF, COMP**, while the DEVL chunk order is
  the other way round. Both lists are right; they disagree.
- **`MIXR` +1 is the COMPRESSOR's target (0 off, 1 master, 2 to 5 sections) and +2
  is the PCF's.** The distortion is a per-section flag at +4 in each channel group.
  None of the three switches is on the manual's MIDI map; they were settled by
  clicking each one in ReBirth and comparing the saves.
- **The mixer track's params 1 and 2 are the compressor's and the PCF's targets**,
  in the same encoding. A click on the switch writes the chunk; only recording writes
  the track.
- **Routing and a unit's bypass are separate controls.** Setting the PCF target must
  not switch the unit on.
- **The master compressor only runs when the song switches it on** (COMP track param
  0), and defaults off for a song that states nothing. Its input is scaled: on the
  master it sees the bus times the 0.72 headroom, with only the master fader's gain
  after it, because ReBirth's master fader follows the compressor. On a section it
  comes before the channel fader and sees the section rescaled to fader 127, centre
  pan, the headroom and a factor 0.5; that scale is a fit, not a measured mechanism.
  The master detector sums L and R.
- **The PCF's mode byte is 0 = BANDPASS, 1 = LOWPASS.** The panel graphic reads the
  other way because the switch handle is drawn low when the switch is up, and the
  manual's MIDI table calls it "LP/HP".
- **The PCF's step clock is FREE-RUNNING**, taken from the song tick, not the 16-step
  bar. Twenty of the 55 patterns have lengths that do not divide 32.
- **The first tick of a render does not trigger the PCF.** The envelope fires on the
  transition into a step.
- **The PCF's decay knob is sixteen quantised steps**, one per `byte >> 3`, in a
  1-2-3-4-5-7 series, and it is in seconds (tempo-independent).
- **A retrigger RESETS the PCF envelope**, so a trigger landing below a running
  envelope pulls it down. A detector that only looks for rises misses most of the
  32nd-note patterns.
- **One decay setting cannot measure a PCF pattern.** The preset table takes step
  positions from decay 64 and velocities from decay 96.
- **The manual's PCF pattern diagrams are right about positions and wrong about
  velocity scale** (about 1.4 times too tall on the 1/16 axis), and wrong about
  pattern 53's length.
- **The distortion is a fitted wavefolder**, and its output gain is a fitted
  function of amount and shape (`src/effects/distortion.cpp`), not a constant.
- **A ratio of 0.00 dB in every octave band is not a filter sitting open**, it is a
  filter that is not in the path. Check whether an effect is switched in before
  interpreting what it does.

## Songs: the chunk is the panel, the track is what plays

- **A CHUNK IS THE SAVE-TIME PANEL. THE TRACK IS WHAT PLAYS.** A song's `PCF `,
  `303`, `DELY` (and every other) chunk records what the panel looked like when the
  file was saved, which is the Pattern-mode panel. The tick-0 dump on the song's
  track is what Song mode starts from. They often disagree, and ReBirth's Song mode
  plays the track. Never quote a chunk value as the setting a measurement ran at.
- **Songs PERFORM the routing, they do not just set it.** Most demo songs ride the
  PCF's target, several ride the compressor's target and the PCF unit's bypass, and
  the per-section distortion switch and the distortion unit's own Amount, Shape and
  on/off are ridden too. Some songs start the distortion at Amount 0 and ride it up.
- **The `.rbs` writer uses explicit byte offsets** for the drum knob headers. A
  running counter once drifted and silently dropped the five switch bytes.
- **The writer states every instrument track's start as a tick-0 dump**, so a track
  that had none (a generated fixture names only its pattern) reloads with one.
  `reborn_roundtrip` reports those as restated, not as a mismatch, and CI renders
  original and copy to show they sound the same.
- **`--no-fx` has to survive automation.** It is a latch (`Engine::disableSends`),
  because a song's mixer track would otherwise ride the delay sends back up
  mid-render.

## Mods

- **Every `.rbm` is scrambled past each embedded file's first kilobyte**, and the
  reader generates the keys exactly from two sums over the readable part
  (`src/io/rbm_keygen.cpp`, `docs/FORMAT_RBM.md`). Read as stored, every mod drum
  is noise. The generated keys are tried only on an archive whose top-level `CAT `
  spans the file; when they fail, the keys are estimated from the content, which
  takes seconds, so never read a mod under a lock the audio thread takes. The app
  reads with `readModSamples` off its engine lock and installs with
  `installModSamples` under it.
- **The self-check that guards the decode has known blind spots**, listed in
  `FORMAT_RBM.md`. Any change to it must still fail the corrupted-key cases in
  `test_rbm_unscramble_synthetic` and the noise and parity-split cases in
  `test_rbm_keygen`.
- **`reborn_render --samples` replaces a song's mod voices**; never pass it to a
  modded song.

## Measurement traps

- **Never null-test against ReBirth.** Its exports are not sample-deterministic
  (oscillator and noise phase are not reset per render): two exports of one song are
  spectrally identical and sample-wise uncorrelated. Compare spectra, band energies,
  envelopes and level.
- **Coherent averaging hides noise.** Averaging hits in the time domain cancels
  noise components and fakes resonances; average power, not waveforms.
- **Fit decay before trim.** A level that looks wrong is often a decay that is wrong.
- **Onset detection shifts when level changes.** Use windows locked to the sequencer
  step, which is always known for a generated fixture.
- See `docs/MEASUREMENT_PROTOCOL.md` for the full fixture and isolation rules.
