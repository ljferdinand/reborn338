# Using Reborn 338

Reborn plays ReBirth song files (`.rbs`) and its own session files (`.rbsx`).
There are two ways to use it: the command-line renderer, which turns a song into
a WAV file with no audio device, and the app, which plays, edits and saves songs
and also runs as a plugin inside a DAW. `docs/BUILDING.md` says how to build both.

## The command-line tools

On Windows the tools are in `build/Release/`; with Make or Ninja they are in
`build/`. The examples below omit the path.

### `reborn_render`

```sh
reborn_render [options]
```

With no options it plays a built-in demo (an acid line on bass 1 and an 808
beat) for 8 seconds into `reborn_out.wav`.

| Option | Meaning |
|---|---|
| `--song FILE.rbs` | render a song file instead of the demo |
| `--seconds N` | length to render (default 8) |
| `--sr N` | sample rate (default 44100) |
| `--out FILE.wav` | output file (default `reborn_out.wav`) |
| `--solo DEVICE` | render one device alone: `bass1`, `bass2`, `808` or `909` |
| `--no-fx` | the effect rack off: the distortion unrouted and the delay sends held at zero, even against the song's automation (the PCF and the compressor still play as the song sets them) |
| `--no-dist` | the distortion unrouted only |
| `--mute-drums` | mute both drum machines |
| `--samples DIR` | drum samples from a folder of WAVs (`docs/SAMPLES.md`) |
| `--mod FILE.rbm` | drum samples from a ReBirth mod (`docs/MODS.md`) |
| `--mods DIR` | find the mod a song names in this folder |
| `--debug-bars` | print the song's channels and arrangement |
| `--help` | the option list |

An unknown option prints a warning and is ignored. The output is always a 16-bit
stereo WAV. A song renders in song mode: the arrangement and all its automation
play, exactly as ReBirth's own export does, and the song loops from the start if
`--seconds` runs past its end.

```sh
reborn_render --song examples/acid_line.rbs --seconds 16 --out acid_line.wav
reborn_render --song examples/beat_909.rbs --solo 909 --seconds 16 --out beat.wav
REBORN_BASS_ENGINE=open303 reborn_render --song examples/acid_line.rbs --out open303.wav
```

The last line swaps both 303 slots to the Open303 voice (`docs/VOICES.md`). On
Windows PowerShell set the variable first with `$env:REBORN_BASS_ENGINE = "open303"`.

### `reborn_roundtrip`

```sh
reborn_roundtrip [--save-dir DIR] song.rbs [more.rbs ...]
```

Loads each song, saves it, reloads the copy and compares everything the engine
plays and the panel holds: patterns, arrangement, automation, fader and pan
bytes, effect settings, knob headers. It also saves the copy again and requires
the same bytes. Events the writer adds when it states each track's starting
settings are reported as a note, not a failure. With `--save-dir` it keeps the
re-saved copies (the directory must exist), which is how CI checks that a
re-saved song renders byte-identically to the original.

### `reborn_rbmkeys`

Reports how a mod's keys are found and judged; see `docs/MODS.md`.

### `comp_wav` and `dist_wav`

```sh
comp_wav in.wav out.wav ratio01 threshold01
dist_wav in.wav out.wav amount01 shape01
```

Measurement aids: each runs one WAV (mixed to mono) through the compressor or
the distortion alone, with its two controls given from 0 to 1. They exist to
feed ReBirth's own clean render through Reborn's effect, so the input cannot
differ.

## The app

Run `Reborn 338` (the Standalone build) or load it as an instrument plugin in a
DAW. It opens on a built-in demo with the same acid line and 808 beat as the
renderer's, so there is something to hear.

### The window

- **Header.** Play/Stop (also the space bar), the Song/Pattern switch, the tempo
  readout with a fine slider, the global shuffle amount, and the file buttons:
  **Load .rbs...** (opens `.rbs` songs and `.rbsx` sessions), **Samples...**,
  **Mod...** and **Save...**. A status line shows notes such as a missing or
  unreadable mod.
- **Channel strips.** One per section (bass 1, bass 2, 808, 909): volume, Mute
  and Solo, a level meter, and the routing buttons `D` (distortion), `P` (PCF)
  and `C` (compressor). Any number of sections can be distorted; the PCF and the
  compressor go to one section at a time, so pressing `P` or `C` on one strip
  takes it from another. `D` and `P` also switch their effect on; `C` only
  routes, and the compressor stays off unless the song switched it on (its
  default ratio, 0, is a bypass too). The two 303 strips also carry the voice
  button (`TPT`, `O303` or `HOST`) described in `docs/VOICES.md`.
- **Pattern editor.** Choose a device, a pattern (32 per device) and its length, then edit the step grid. For a drum machine the rows are
  its voices; for a 303 they are twelve semitones plus accent, slide and octave
  rows. Click sets a step, shift-click or right-click sets an accent, alt-click a
  soft step and ctrl-click (cmd-click on macOS) a flam (909), and dragging
  paints. Steps past the pattern's length are dimmed. Patterns are at most 16
  steps, as in ReBirth.
- **Device and effect panels.** Every control of the 303s, the drum machines and
  the four effects, grouped the way the hardware groups them: knobs for
  continuous controls, steppers for counts (delay steps 1 to 16, PCF pattern 0 to
  54), and toggles for switches (PCF bandpass or lowpass, delay straight or
  triplet).
- **Arrangement.** In Song mode the song plays its arrangement, shown as a strip
  of steps. **Capture** appends a step from the current pattern selection, **Del**
  removes the selected step, **Clear** empties the arrangement and returns to Pattern mode, and **Rep** sets
  how many times the selected step repeats. In Pattern mode the selected patterns
  loop and the song's automation does not play; choosing a pattern by hand
  switches to Pattern mode.

In the Standalone app, **Dock** floats the device and effect panels into
separate windows that snap together, and **Grid** (or the G key) opens a shared
step grid for whichever device is selected. A plugin's editor is always a single
window.

### Opening and saving

**Load .rbs...** opens either kind of file. A `.rbs` song brings its patterns,
arrangement, automation, mixer and effect settings; if it names a mod and you
have loaded a mod from the same folder before, that mod is used
(`docs/MODS.md`). A `.rbsx` session also restores the sample folder, the mod,
and each 303 slot's voice, including a hosted plugin and its settings.

**Save...** saves a `.rbsx` session by default. Type a name ending in `.rbs` to
export a ReBirth song instead, which ReBirth can open. That export is lossy: it
cannot hold a voice choice, a hosted plugin or the sample folder, and the app
lists what was left out after saving.
`docs/FORMAT_RBSX.md` describes the two formats.

### As a plugin in a DAW

Reborn is an instrument with a stereo output. It follows the host's tempo, and
it starts and stops with the host's transport until you press Play or Stop in
its own window, after which it follows only its own button for the rest of the
session. It does not follow the host's song position: it always plays from the
start. The whole session (the song, the paths of the sample folder and the
mod, the voices and any hosted plugin's state) is saved with the DAW project;
the samples and the mod themselves stay where they are on disk.

Three things a DAW user might expect are missing. The plugin accepts MIDI but
ignores it, so notes from the host do not play the 303s or the drums; its
controls are not exposed as automatable parameters; and it cannot host effect
plugins. `docs/PLUGINS.md` has the details, and `docs/HANDOVER.md` lists all
three as open work. To play a 303 from MIDI in a DAW, use the separate Reborn
Open303 plugin (`docs/VOICES.md`).

### Gaps worth knowing

Patterns are at most 16 steps, as in ReBirth; the pattern model has room for
32, which nothing uses yet. The effect units' own on/off switches are not exposed, so the compressor cannot be switched
on from the app, and there is no button to route it to the master strip,
although songs that do either play correctly. Solo stops the other sections'
voices rather than silencing them, so a cymbal can resume mid-tail when solo is
released. ReBirth's MIDI remote control is not implemented.
