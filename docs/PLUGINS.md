# Plugins: Reborn as a plugin, and Reborn as a host

Three capabilities are easy to confuse, and they share almost no code:

| | What it means | State |
|---|---|---|
| Reborn as a plugin | load Reborn into a DAW as an instrument | VST3 and CLAP, AU on macOS; MIDI input advertised but ignored |
| Reborn hosting an instrument | a third-party synth in a 303 slot, played by Reborn's sequencer | VST3 and CLAP, AU on macOS; tested on Windows only |
| Reborn hosting effects | third-party effects on Reborn's channels or master | not implemented; design notes below |

There is also a separate plugin, Reborn Open303, described in `docs/VOICES.md`.

## Reborn as a plugin

The app builds as a Standalone program and as VST3 and CLAP instrument plugins,
and as an AU on macOS (`docs/BUILDING.md`). The CLAP build comes from
clap-juce-extensions, which wraps the same processor, editor and state. All
formats are the same instrument: stereo out, the full window, the whole engine.

What a host gets:

- **Tempo and transport.** Reborn takes the host's tempo every block, and starts
  and stops with the host's transport. Pressing Play or Stop in Reborn's own
  window hands transport back to that button for the rest of the session. It
  ignores the host's song position and always plays from the start.
- **State.** The plugin's state is a complete `.rbsx` session image: the song,
  the paths of the sample folder and the mod (the files themselves are not
  embedded), and each 303 slot's voice, including a hosted plugin's identifier
  and its own state. A DAW project therefore reopens the whole rig on a machine
  that has the same samples, mod and plugins at the same paths.
- **One window.** In a host, the panels stay inside the single plugin window; the
  floating, docking panels are a Standalone feature.

What it does not do:

- **MIDI input.** The plugin declares that it needs MIDI input
  (`NEEDS_MIDI_INPUT TRUE` in `CMakeLists.txt`), and `processBlock` discards the
  MIDI buffer it is given. Hosts route MIDI to it and nothing happens. This is
  deliberate for now and documented rather than hidden; see "MIDI input" below
  for what implementing it means.
- **Automation.** No control is exposed to the host as a parameter, so nothing
  can be automated from the DAW. Songs carry their own automation, which plays.
- **Validation beyond Windows.** The plugins were used on Windows only. The VST3
  build of the separate Open303 plugin passed pluginval at strictness 10 and its
  CLAP build passed clap-validator, in August 2026; the main plugin was not put
  through either.

## Reborn hosting an instrument in a 303 slot

Either 303 slot can hold a third-party instrument plugin instead of a built-in
voice. Click the slot's voice button and choose **Load plugin...**: the menu
scans the standard plugin folders, loads a `.vst3` or `.clap` file directly, or
picks from the plugins already found; **Open plugin editor** shows the plugin's
own window.

How it works: `app/src/PluginHost.h` keeps JUCE's plugin format manager and a
list of known plugins, and blacklists a plugin that crashes a scan so it is
skipped next time (the scan runs in Reborn's own process, behind a progress
window). `app/src/HostedInstrument.h` wraps the plugin instance as a Reborn
`Instrument`, shows up to 32 of its parameters in Reborn's knob panel, and turns
each 303 step into MIDI. JUCE 8 cannot host CLAP, so `app/src/ClapPluginFormat.h`
adds a CLAP format of Reborn's own on the CLAP headers: loading, scanning, audio
and note processing, parameters, state, and the plugin's own window (on Windows).

The choice and the plugin's state are saved in `.rbsx` sessions (the `RINS`
chunk, `docs/FORMAT_RBSX.md`) and in a DAW project. A session whose plugin is
missing still opens, with that slot on the default voice, and keeps the plugin's
record so that saving again does not lose it.

It was tested with jc303 (VST3 and CLAP) and with Reborn's own Open303 plugin,
on Windows. `reborn_host_smoke` checks the path headless: it loads a VST3 into a
slot, plays a note and checks it sounds and stops, or renders a song's first 303
line through it.

### Why a hosted 303 does not sound like ReBirth's

A hosted synth is a creative feature, not a parity one. Reborn's own two voices
receive each step as a `BassEvent` (`include/reborn/step_events.h`) and keep the
303's step semantics exactly. A hosted plugin receives MIDI, and three things a
303 step does have no MIDI equivalent:

- **Pitch on a rest.** ReBirth's oscillator follows every step's note, whether or
  not the step sounds. MIDI has no "retune without playing". On a shipped song
  this is the difference between playing D and playing G.
- **Slide.** ReBirth's slide is a fixed one-pole glide in log-frequency with an
  18 ms time constant, about three times faster than the Roland hardware most
  303 plugins imitate. MIDI legato plus the plugin's own portamento is the
  closest available, and it will differ.
- **Accent on a tied rest.** ReBirth reads the accent row on a rest and applies
  it to whatever is still sounding. MIDI velocity belongs to a note-on, and there
  is none there.

So a run of steps retriggers where ReBirth would glide, and some notes differ.
That is not a bug in the host.

## MIDI input

ReBirth's own MIDI is a remote control for its panel, not a way to play its
instruments (the manual's Appendix C is the full map). Every knob has a
controller number, and with "Program Synth from Keyboard" on, notes enter steps
in the 303's step-entry panel; no note-on ever sounds a 303 note. Two things
follow for anyone implementing MIDI input:

- **Implementing ReBirth's controller map is cheap and exact.** A controller,
  a panel knob and the byte the song file stores are all 0 to 127, so a knob
  moved by MIDI is identical to one moved by the mouse, and a controller only
  has to arrive before the next step, so the engine's step-sliced timing does not
  change. It would let a 1998 controller template drive Reborn unchanged.
- **Playing the voices from notes is new design,** with nothing in ReBirth to
  check it against. If it is added, quantise notes to the step grid rather than
  making the engine sample-accurate: every measured law in this project was
  fitted under the step-sliced timing.

Either way the `NEEDS_MIDI_INPUT` flag then becomes true in practice.

## Reborn hosting effects: design notes

Nothing here is implemented. The `.rbsx` format reserves the chunks a chain would
need (`RFXC`, `RFXS`, `docs/FORMAT_RBSX.md`); no code reads or writes them.

**Where chains would go.** Each section already has its own chain (distortion,
PCF, compressor) feeding the mixer. A hosted chain fits after the built-in
inserts and before the fader and pan, so the fader still means level and the
built-in effects keep the routing songs encode; a master chain fits after the
master bus.

**How many.** A dynamic chain per section and on the master, with a hard cap
(eight was the proposal), shown as the slots in use plus one empty slot. Four
sections of five fixed slots plus a master rack would be 25 instances, most of
them empty.

**Latency is the hard part.** Hosted effects report latency, and Reborn's
sections run in parallel into the master. If the 808's chain reports 512
samples and the 303's none, the drums arrive late. Each chain's latency has to be
measured, every other section delayed to match the largest, and the total
reported to the host, recomputed when a plugin changes its latency.

**Threads.** Scanning, loading and editor windows are slow, message-thread
work. Build a chain off the audio thread and swap it in at a block boundary; the
engine lock the app uses today is too coarse for that. Scanning effects would be
safer out of process than the instrument scan is.

**Nested hosting.** Hosting plugins inside Reborn-as-a-plugin is where hosts and
plugins misbehave. Support it fully in the Standalone app first, treat it as
best effort in a DAW, and never let a hosted plugin's failure take down the
instrument.
