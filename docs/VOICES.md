# Voices

Reborn has two 303 slots and two drum machines. Each 303 slot can hold one of
two built-in voices, or a third-party instrument plugin. This page describes the
voices, how to choose between them, and the separate Reborn Open303 plugin.

## The native TPT voice (the default)

Reborn's own 303, in `src/devices/bass_synth.cpp`. It is built from standard
virtual-analog parts: a band-limited saw or square oscillator (polyBLEP), a
four-pole resonant lowpass in topology-preserving-transform form with a
saturating feedback path that lets it self-oscillate without blowing up, a
main envelope driving the cutoff through env mod, a separate faster accent
envelope that lifts both level and cutoff, an amplifier with a 1.2 ms re-pluck,
and an exponential slide with an 18 ms time constant. Every constant that sets
how it sounds was fitted to ReBirth's exports, one control at a time; none comes
from ReBirth's code.

It is the voice the parity work targeted, and it is the one every measurement
uses. Its work was signed off on 2026-09-05. Three things are open: the "chirp"
of the resonant peak on retriggered mid-resonance notes is a little muted against
ReBirth's and was accepted as a documented residual; the saw's crest factor is
2.3 dB short of ReBirth's, which makes a compressed TPT 303 come out up to about
2.3 dB hot (`docs/BACKLOG.md`, entry 22); and ReBirth's "Vintage ReBirth Synth
Sound" switch is not implemented. `docs/COMPONENT_MATRIX.md` has the
control-by-control state and `docs/research/BASS303_FINDINGS.md` the measurements.

## Open303 (selectable)

Robin Schmidt's Open303, an MIT-licensed emulation of the Roland TB-303
hardware, vendored in `third_party/open303/` from upstream commit `313bf0d9`
with only build-portability edits, each listed in
`third_party/open303/README_REBORN.md`. `src/devices/bass_open303.cpp` wraps it
as a Reborn instrument and drives it from Reborn's sequencer rather than
Open303's own.

It exposes the same nine controls as the TPT voice, with the same names, ranges
and defaults (tuning, waveform, cutoff, resonance, env mod, decay, accent,
volume, drive), so a song or session can switch between the two without
rescaling anything. The wrapper maps cutoff exponentially onto 314 to 2394 Hz and
decay onto 200 to 2000 ms, as Open303's own plugin does; an accented step is a
note at velocity 127 (Open303 accents at 100 and above), a slide is a legato
glide, and a step's gate closes after half the step unless it is tied. Two
controls do nothing on this voice: drive (Open303 has no drive stage) and the
accent ReBirth fires on a rest over a ringing note.

Open303 emulates the hardware, not ReBirth, and it was never tuned toward
ReBirth. It renders about 15.65 dB quieter than ReBirth, so it is never used for
measurement (`docs/BACKLOG.md`, entry 15).

## Choosing a voice

- **In the app,** each 303 strip has a voice button (`TPT`, `O303`, or `HOST`
  for a hosted plugin). Clicking it offers the two built-in voices, loading a
  plugin, and opening a hosted plugin's own editor. The choice is saved in a
  `.rbsx` session (its `RINS` chunk, `docs/FORMAT_RBSX.md`) and in a DAW
  project's plugin state. `examples/acid_line_open303.rbsx` is a session with
  Open303 in the first slot.
- **In `reborn_render`,** set `REBORN_BASS_ENGINE=open303` (or `tpt`, the default)
  to swap both slots. It is the only `REBORN_*` variable meant for users
  (`docs/ENVIRONMENT_PROBES.md`).
- **A third-party 303** can fill a slot in the app: see `docs/PLUGINS.md`.

## The Reborn Open303 plugin

`plugins/open303/` builds a small instrument plugin for other hosts (Standalone,
VST3 and CLAP, and AU on macOS): the same Open303 voice, played from MIDI, with
ReBirth-style distortion after it. Inside Reborn the same code is linked
directly, so the plugin is not needed there.

Its parameters are Tuning (-12 to +12 semitones), Waveform (saw to square),
Cutoff, Resonance, Env Mod, Decay, Accent and Volume, then Dist On, Dist Amount,
Dist Tone and Out Level. It is monophonic with last-note priority: a note-on
sounds the note (velocity 100 or more is an accent), a note that starts while
another is held slides to it, releasing a note slides back to the most recent
one still held, and all-notes-off silences it. Notes sustain until their note-off.

## The 808 and the 909

Both drum machines follow ReBirth's layout: the 808's eleven columns, five of
them shared between two instruments (low tom or low conga, rimshot or claves,
clap or maracas, and so on) by a switch in the song; the 909's eleven voices
with soft, normal and flam steps and a flam amount. Each voice can play from
Reborn's synthesis, the default, or from a recording (`docs/SAMPLES.md`,
`docs/MODS.md`). Both paths were fitted to ReBirth's exports and signed off: the
808's in August 2026 (`docs/research/DRUM808_FINDINGS.md`) and the 909's on both
paths in August 2026 (`docs/research/DRUM909_FINDINGS.md`), the 909's synthesis
path within about 1 dB per isolated voice.

A few behaviours look like bugs and are measured facts: the accent knob alone
does nothing (it scales only steps whose accent row is set, by the same amount
on every voice), the accent row stores a value rather than a flag, and the drum
buses are linear with no saturation. `docs/NON_OBVIOUS_BEHAVIOUR.md` lists these
and the rest.
