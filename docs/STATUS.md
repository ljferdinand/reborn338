# Status at close-out (October 2026)

Active development stopped in October 2026. This page says, component by
component, what works, how closely it matches ReBirth, what is known to be off
and by how much, and what was never built. Every figure comes from comparing
Reborn's render with ReBirth's own export of the same song; the documents named
in each section hold the measurements. `docs/HANDOVER.md` orders the open items.

"Signed off" means the maintainer listened to the result against ReBirth and
accepted it, after the measurements below; it is not a claim of exactness.
ReBirth's own exports are not sample-deterministic, so parity here is measured in
spectra, envelopes and levels, never by subtracting waveforms.

## Summary

| Component | State |
|---|---|
| 303, native TPT voice | signed off 2026-09-05; two documented residuals |
| 303, Open303 voice | works; not a parity voice; about 15.65 dB quiet |
| 808 | signed off on both the sample and synthesis paths |
| 909 | signed off on both paths |
| PCF | signed off 2026-09-16; accepted residuals |
| Delay | done 2026-09-10; automation timing and one pan corner open |
| Compressor | round 5 merged 2026-10-23; detector still cuts harder than ReBirth on some passages |
| Distortion | signed off 2026-09-18; output level within about 2 dB median |
| Mixer | fader and pan laws measured; automation timing coarser than ReBirth's |
| Sequencer and song mode | plays songs; several transport behaviours differ |
| `.rbs`, `.rbsx` | read and write; all 16 demo songs round-trip |
| Mods (`.rbm`) | read, with exact keys for packed mods; skins never drawn; no writer |
| App | works on Windows; Linux and macOS untested |
| Plugins | VST3 and CLAP (AU on macOS); MIDI ignored; no automation; no effect hosting |

## The 303

**Native TPT voice** (`src/devices/bass_synth.cpp`, the default). Every control
was measured against ReBirth and fitted: oscillator, filter corner and
resonance, envelope and env mod, accent, slide, octave, tuning and the amplifier.
Signed off 2026-09-05. Accepted residuals: on retriggered notes at mid
resonance the resonant peak's "chirp" is a little muted against ReBirth's
(`docs/research/BASS303_FINDINGS.md` section 31); and the saw's crest factor is
2.3 dB short of ReBirth's, which makes compressed 303 material come out up to
about 2.3 dB hot (`docs/BACKLOG.md`, entry 22). Not implemented: the "Vintage
ReBirth Synth Sound" switch. Detail: `docs/COMPONENT_MATRIX.md`, section 1.

**Open303** (selectable). Works in both slots and as a separate plugin. It
emulates the hardware 303, not ReBirth, was never tuned toward ReBirth, and
renders about 15.65 dB quieter than ReBirth (`docs/BACKLOG.md`, entry 15).
`docs/VOICES.md` describes both voices.

## The drum machines

**808.** Every voice is fitted on both paths: the sample path (rounds 7 to 9) and
the synthesis path, which ships by default and was signed off 2026-08-24 with
every voice within about 0.1 dB of its export at the default knob setting. The
five shared columns, the global accent and the closed-hat choke are all modelled.
`docs/research/DRUM808_FINDINGS.md`.

**909.** Both paths signed off, every control on the manual's MIDI map measured
and fitted, flam included. The synthesis path sits within about 1 dB per
isolated voice and within 0.04 dB over a whole song. `docs/research/DRUM909_FINDINGS.md`.

Open for both: ReBirth starts each drum hit on a 64-sample block boundary and
its first hit is late, which Reborn does not reproduce (`docs/BACKLOG.md`, entry
20).

## The effects

All four run as per-section chains (distortion, then PCF, then the section
compressor), with the delay as a send and the master compressor after the mix,
as measured in ReBirth.

**PCF.** Signed off 2026-09-16: routing, cutoff and resonance laws, corner drift,
level law, the decay table and all 55 trigger patterns, with cutoff tracking
ReBirth within 0.23 octave, and a re-derived resonator, envelope reach, velocity
curve and attack. Accepted residuals: swept-resonance brightness and a level
deficit at high trigger density (`docs/BACKLOG.md`, entry 14).
`docs/research/PCF_FINDINGS.md`, `docs/specs/SPEC_PCF_ENVELOPE.md`,
`docs/specs/SPEC_PCF_RESONATOR.md`.

**Delay.** Done 2026-09-10: echo length on the straight and triplet grids,
feedback, the return pan, the send law, and its automation track. Open: a ride
lands on the next 16th and steps where ReBirth applies it at its 1/32 tick and
glides (entry 18, engine-wide), and at pan 127 ReBirth's far channel carries the
echo at -46.8 dB where Reborn's is silent (entry 19). `docs/research/PLAN_DELAY.md`,
`docs/specs/SPEC_DELAY.md`.

**Compressor.** Round 5, merged at the close-out: a peak detector with hold and a
dB-linear release, and a linear gain law, fitted from measurement alone. On the
measured grid all 152 static cells are within 0.03 dB of the fit, 15 hold-out
cells never used in fitting are within 0.22 dB, and the threshold-0 corner that
round 2 missed by 4.17 dB is at -0.03 dB. Open: on loud compressed passages of
four demo songs Reborn's compressor cuts 2.5 to 5.7 dB harder than ReBirth's, and
5.1 to 8.1 dB harder in one Clodhopper passage, with the uncompressed mixes
agreeing within 0.54 dB (entry 16). It is off unless a song switches it on, and
the app cannot switch it on (`docs/USAGE.md`). `docs/specs/SPEC_COMP.md`,
`docs/research/PLAN_COMP.md`.

**Distortion.** Signed off 2026-09-18: a fitted wavefolder whose character was
matched by ear and measurement, with its output-level law fitted across the
amount and shape grid to a median gap of about 2 dB (the remaining scatter is
cell-to-cell aliasing of a hard-driven fold). Unmeasured: whether ReBirth's
channel fader sits before or after the distortion (entry 21).
`docs/research/PLAN_DIST.md`, `docs/specs/SPEC_DIST.md`, `docs/specs/SPEC_DIST_GAIN.md`.

## The mixer and the transport

**Mixer.** The faders follow a measured exponential law (0.370 dB per byte), the
pan law holds the near side at unity and attenuates only the far side, the drum
buses are linear, and the master is a hard clamp at full scale, all as measured.
Mute, solo and the three routing switches work. `docs/PARITY.md`.

**Sequencer and song mode.** Songs play their arrangement and their automation:
pattern changes, knob rides, mixer moves, effect rides and the performed routing
of the PCF, distortion and compressor. Known differences, from
`docs/research/PLAN_TRANSPORT_MODES.md`:

- automation is applied on the 16th-note grid and steps, where ReBirth applies
  it at its 1/32 tick (entry 18);
- a pattern shorter than 16 steps restarts every bar instead of looping on its own
  length, and in Pattern mode a pattern chosen by hand takes over at the next step
  rather than at the end of the playing pattern;
- at the end of a song Reborn wraps to the start, where ReBirth plays on to bar
  999; ReBirth's loop range is not implemented;
- inside a host, playback always starts from the beginning.

## Files

**`.rbs`.** Read and written. The loader decodes patterns, arrangement, all
automation, the mixer, every effect, the drum knob headers and the song's named
mod. All 16 demo songs and 36 generated fixtures survive a save and reload with
nothing lost, and re-saved fixtures render byte-identically. `docs/FORMAT_RBS.md`.

**`.rbsx`.** Reborn's session format: the song plus the sample folder, the mod,
and each 303 slot's voice or hosted plugin with its state. The effect-chain
chunks it reserves are not implemented. `docs/FORMAT_RBSX.md`.

**Mods.** Read and unscrambled with exact generated keys for mods ModPacker
packed, all four retail mods exact, and estimated keys otherwise; all 45 drum
voices mapped. Mods edited after packing are often refused, skins are never
drawn, and there is no writer. `docs/MODS.md`, `docs/FORMAT_RBM.md`.

## The app and the plugins

**App.** Plays, edits and saves songs and sessions; mixer strips, a 16-step
pattern editor, every device and effect control, an arrangement editor, sample
and mod loading, and in the Standalone build floating, docking panels. Validated
on Windows only. Known gaps: the effect units' on/off switches are not exposed, solo stops other voices instead of
silencing them, and ReBirth's MIDI remote control is not implemented.
`docs/USAGE.md`, `docs/PLAN_GUI.md`.

**Plugins.** Reborn 338 builds as VST3 and CLAP (and AU on macOS) and can host a
third-party instrument plugin in either 303 slot (VST3, CLAP, and AU on macOS),
tested with jc303 and Open303 on Windows. It follows host tempo and play/stop
and saves its whole session in the host project (the sample folder and mod by
path). It ignores incoming MIDI, has
no automatable parameters and cannot host effect plugins. The separate Reborn
Open303 plugin takes MIDI. `docs/PLUGINS.md`.

**Real-time safety.** The audio path calls `getenv` about 75 times per block in
the TPT voice (measurement probes left in the code, `docs/ENVIRONMENT_PROBES.md`)
and allocates on the audio thread on every 909 step and every automation event
(`docs/research/REVIEW_2026-09.md`, RT1 and RT2; `docs/BACKLOG.md`, entry 10).
Harmless in `reborn_render`, a risk in a DAW.

## Platforms and verification

The engine, the command-line tools and the tests build and pass on Linux,
macOS and Windows in CI, which also checks that each platform's renders measure
the same as the Windows reference within 0.05 dB and 0.5 percent. The app and both
plugins build in CI on Windows; their Linux and macOS builds compile only when
CI is run by hand and have not been used. The parsers were hardened against
crafted files after an external review, with regression tests, but no
AddressSanitizer job or fuzzer runs them (`docs/BACKLOG.md`, entry 9).
