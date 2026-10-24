# Measurement protocol

Reborn's constants were fitted by measuring ReBirth's audio output: render a test
song in ReBirth, render the same song in Reborn, compare. Exports of your own
licensed copy of ReBirth are the ground truth, and nothing else is. This file is the
protocol that kept those comparisons honest. A measurement that breaks it is not
weak evidence; it is a finding that will have to be retired later, and retiring
findings cost this project more time than making them. `docs/research/FINDINGS_AUDIT.md`
(under `docs/research/`) records what was proven, what was retired, and why.

Nothing from ReBirth (no export, sample, song file or mod) belongs in this
repository. Keep exports and fixtures in a workspace outside it; the tools read the
`REBORN_WORKSPACE` environment variable where they need one.

## Isolation

**One device. One INSTANCE of it. One effect or none. Proved, not assumed.**

- One 303, never two. One drum machine, never both. Two simultaneous lines cannot be
  separated by ear, and a band metric cannot separate them either.
- Exactly one effect in the path, on exactly one device, everything else off and
  unrouted, nothing automated after tick 0.
- Run `python tools/rbsgen/isolation_check.py <fixture> --device 909 --effect pcf`
  before every export. It reads the arrangement, not the save-time panel, and tests
  silence by rendering each device soloed.
- Build effect tests on the source the engine is closest on: the 909 in sample mode
  with the rack off was the cleanest single-device match in this project.
- **A denominator differs from its numerator in exactly one thing.** Several
  measurement rounds were lost to references that did not.
- Every listening comparison opens with a **null pair**: the same fixture with no
  effects, Reborn against ReBirth, so the listener hears what "the same" sounds like
  before being asked whether something differs.

## Conditions for a valid comparison

- **No effects** unless the effect is what is being measured: distortion off, delay
  send zero, PCF off, and **ReBirth's master compressor verified off in its own
  panel** before exporting. It is on by default and silently squashes loud material.
- **Song mode.** ReBirth's Export plays the arrangement, not the panel, so settings
  have to be baked into the arrangement as tick-0 automation. `tools/rbsgen` does
  this.
- **Stereo, both sides.** `reborn_render` writes 16-bit stereo. Measure left and right
  separately as well as summed. Mono-summed metrics once hid a pan law seven times
  too wide; a script that sums to mono must say so and justify it.
- **One method, both sides.** The same script, window and normalisation on the
  ReBirth WAV and on Reborn's.
- **Never null-test against ReBirth.** Its exports are not sample-deterministic:
  two exports of one song had sample correlation 0.66 while every octave band
  matched within 0.07 dB. Compare spectra, band energies, envelopes and level.
- **Measure the 303 on the TPT voice** (`REBORN_BASS_ENGINE=tpt`). The Open303 voice
  is about 15.65 dB quiet and confounds anything level-dependent.
- **Demo songs are not fixtures.** They mix every unknown at once. They are an
  end-stage regression suite for the whole mix, once every component is calibrated.

## Fixtures

Generate them with `tools/rbsgen/rbsgen.py`. Scratch mode writes songs from first
principles (no bytes from any ReBirth file) and bakes a compressor-off snapshot into
each one. ReBirth opens and plays them.

- **A fixture set for one control is a sweep, not a pair**: three points minimum,
  five preferred, across the range including both extremes. Two points cannot tell a
  linear law from an exponential one.
- **Cross the sweep with what it might interact with** rather than moving two things
  at once.
- **A control whose fixture value never moves has not been measured**, however many
  fixtures use it. Compare a generated fixture's bytes with the same bytes in a real
  song before trusting a law fitted on it.
- Name each export after its `.rbs` so pairing is mechanical, and keep a manifest
  describing what each file pins down.

## Fitting

- **One thing at a time.** Fitting coupled parameters together gives a fit that is
  right in aggregate and wrong in every part.
- **Fit decay before trim.** A level deficit is often a decay error in disguise.
- **Check the baseline is current** before trusting a suggested correction.
- **Onset detection shifts when level changes.** Use windows locked to the sequencer
  step.
- **Average power, never waveforms.** Coherent averaging cancels noise and fakes
  resonances; it confounded both trims and a spectral fit before it was found.
- **Prefer robust metrics to peak picking**: Welch PSD (`nperseg` 8192, `noverlap`
  4096), 85 percent rolloff, magnitude-weighted centroid, peak prominence in a stated
  band. `tools/measure/score303.py` is a model scoring harness.
- **Normalise before comparing spectra**: band energy relative to each file's own
  total, with the level offset reported separately.
- **On a near-inaudible residual, a spectral metric that "matches" is not a fix
  until the ear confirms it.**

## When measurement and the manual disagree

The manual is a strong prior about intent and a weak one about behaviour. Measurement
wins, and the disagreement is written down rather than silently resolved. Examples
are in `docs/NON_OBVIOUS_BEHAVIOUR.md` (the PCF mode byte, the PCF pattern diagrams).

## When something sounds very different

Take the magnitude at face value and widen what is being measured, instead of
refining the current metric. The two largest defects this project found, a
whole-song summing tilt and the pan law, were invisible to every existing metric and
were found by changing what was looked at.

## Recording a result

A finding is not done until it is written down with the condition under which it was
measured: `docs/PARITY.md` for settled laws, the component findings files under
`docs/research/` for the detail, `docs/research/FINDINGS_AUDIT.md` for what is proven
and what was retired. A retired finding stays retired.
