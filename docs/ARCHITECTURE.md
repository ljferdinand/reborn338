# Architecture

Reborn 338 is a dependency-free C++17 engine with a JUCE front-end on top. The
engine knows nothing about windows, plugin formats or audio devices, so it builds
anywhere, runs headless in `reborn_render`, and is tested without a GUI.

Before changing anything in the audio path, read
[NON_OBVIOUS_BEHAVIOUR.md](NON_OBVIOUS_BEHAVIOUR.md): many details below look like
mistakes and are measured behaviour of ReBirth.

```
  .rbs / .rbsx ──► io/  ──► Song ──► Engine ──► StereoBlock ──► reborn_render (WAV)
  .rbm, WAV  ───► sample sets ─┘       │                    └──► app/ (Standalone,
                                       │                         VST3, CLAP, AU)
                    Instruments: 303, 303, 808, 909
                    Effects:     distortion, PCF, compressor, delay
```

## The layers

| Directory | What it holds |
|---|---|
| `include/reborn/`, `src/` | `reborn_core`, the engine library, with no dependencies |
| `src/devices/` | the instruments: `bass_synth.cpp` (the TPT 303), `bass_open303.cpp` (the Open303 wrapper), `drum808.cpp`, `drum909.cpp` |
| `src/effects/` | `distortion.cpp`, `delay.cpp`, `compressor.cpp`, `pcf.cpp` with its 55 measured trigger patterns in `pcf_presets.inc` |
| `src/engine/` | `engine.cpp`: transport, sequencer, song mode, automation, mixer and effect routing |
| `src/io/` | file formats: `.rbs` (`rbs.cpp`, `rbs_write.cpp`), `.rbsx` (`rbsx.cpp`), `.rbm` (`rbm.cpp`, which also decodes the mods' AIFF recordings, `rbm_keygen.cpp`, `rbm_unscramble.cpp`), WAV (`wav.cpp`), sample sets (`sampleset.cpp`) |
| `third_party/open303/` | Robin Schmidt's Open303, built as its own static library |
| `app/` | the JUCE app and plugin: processor, editor, plugin hosting, docking windows, and two headless smoke harnesses |
| `plugins/open303/` | the separate Reborn Open303 instrument plugin |
| `tests/` | `test_main.cpp` (the engine, formats, mods, effects) and `test_dock.cpp` (the docking geometry) |
| `tools/` | the renderer and other command-line tools (`render/`), fixture generation (`rbsgen/`), measurement (`measure/`, `analyze/`), parameter search (`optimize/`) |

## Interfaces

All in `include/reborn/`.

- **`Processor`** (`processor.h`): anything with parameters and a sample-rate
  lifecycle. Parameters are addressed by name, so the song loader and the UI do
  not depend on a module's internal parameter order. The app builds its knob
  panels from this interface.
- **`Instrument`**: a `Processor` that is triggered (note or voice, velocity,
  accent, slide), released, and renders a stereo block. It can also take a
  recording per voice (`setSample`), which is how sample sets and mods reach the
  drum machines.
- **`Effect`**: a `Processor` that processes a block in place, with optional tempo
  and step hooks for the tempo-synced units.
- **`BassEvent`** (`step_events.h`): the boundary between the sequencer and a 303
  voice, as a value. `decideBassStep()` turns one step and the previous step's
  slide into strike, hold or release, with note, accent, slide and tie, and
  `applyBassEvent()` makes the calls. It is a pure function because three shipped
  defects lived in that decision, and it can be tested without audio.
- **Factories** (`factories.h`): the only coupling between the engine and the
  concrete DSP. Each device and effect lives in one `.cpp` behind a `makeX()`
  function, and `makeBass303(engine)` builds a 303 voice by id. The app can install
  a hosted plugin in a 303 slot through the same `Instrument` interface.

## The engine

**Timing.** The sequencer runs sixteenth-note steps and every pattern wraps at
step 16. `Engine::process()` renders in slices bounded by the next event: a step
boundary, a shuffle-delayed trigger or a 909 flam. At a step boundary it fires
the step's triggers and dispatches the automation due, so no DSP module handles
events inside a block. Shuffle delays the off-beat triggers of each device whose
current pattern is flagged as shuffled; the step clock itself stays straight. Song automation is timed in 1/32-note ticks
but applied on the sixteenth-note grid (a known difference from ReBirth,
`docs/BACKLOG.md` entry 18).

**Song mode.** A loaded song plays its arrangement: pattern changes, knob and
mixer rides, effect rides, and the routing of the PCF, distortion and compressor,
which songs perform as they play. The song starts from its tracks' tick-0 state,
not from the panel its file saved (`docs/FORMAT_RBS.md`). Pattern mode loops the
selected patterns and plays no automation.

**The signal path,** per slice, as measured in ReBirth (`docs/PARITY.md`):

1. Each section (bass 1, bass 2, 808, 909) renders into a scratch block, and the
   channel fader's exponential gain is applied.
2. The section's own effect chain runs on that block: the distortion, if routed
   there and switched on (one instance per section, all sharing one set of
   knobs), then the PCF, if routed there, then the compressor, if routed to that
   section. ReBirth runs these as separate chains per section, not shared buses.
   The section compressor sees the section with the fader divided out and
   restored after it, because in ReBirth it comes before the fader.
3. The delay send taps the section after its chain, through the same
   exponential fader law.
4. The section is panned once, through the measured pan law, onto the master
   bus.
5. The delay processes the summed sends and returns onto the master bus.
6. The master compressor, if a song routes it there, runs on the master bus.
7. The master volume is applied and the result is hard-clipped at full scale, as
   ReBirth does. The drum buses and the master carry no saturation.

**Instruments.** The 303 voices are monophonic and take their steps from
`BassEvent`. The drum machines take each step's grid cells (accent, soft, normal,
flam on the 909) and resolve the 808's shared columns from the song's switches.
Each drum voice synthesises unless it has been given a recording, and picks its
level trim by which of the two it is playing.

## Threads and locks

The engine is driven from one audio thread. In the app, `RebornAudioProcessor`
owns the engine and an engine lock: the editor takes the lock to change the song
or the voices, and the audio callback only tries the lock and renders silence for
one block if it is busy. Anything slow happens off the lock: reading a mod (which
unscrambles and self-checks it, a tenth of a second or, when its keys must be
estimated, several seconds) is done first and only installing its voices takes the
lock. A session restore marks itself in progress so the audio thread does not play
half-loaded state.

## Files

- **`.rbs`**: an EA-IFF-85 container of chunks (the save-time panel) and tracks
  (what plays). The reader decodes patterns, arrangement, all automation, the mixer,
  every effect and the drum knob headers; the writer restates every track's start
  so a saved song reopens in the same state. `docs/FORMAT_RBS.md`.
- **`.rbsx`**: Reborn's session file: a byte-for-byte `.rbs` image plus the sample
  folder, the mod, and each 303 slot's voice or hosted plugin with its state. The
  plugin's DAW state is the same image. `docs/FORMAT_RBSX.md`.
- **`.rbm`**: the mod container, its unscrambling and key generation, and the
  mapping of its recordings onto the drum voices. `docs/FORMAT_RBM.md`,
  `docs/MODS.md`.
- **Sample sets**: a folder of WAV files under canonical names, or a mod's
  recordings, installed into the drum machines with the same variant selection.
  `docs/SAMPLES.md`.

## The app

`RebornAudioProcessor` (`app/src/PluginProcessor.cpp`) is the plugin: it owns the engine, loads and saves
songs and sessions, follows the host's tempo and play/stop, and saves the session
as its state. `PluginEditor` is the single window: header, mixer strips, pattern
editor, device and effect panels, arrangement. In the Standalone build the panels
can float into separate snapping windows (`app/src/dock/`, `docs/PLAN_GUI.md`).
`PluginHost` and `HostedInstrument` load a third-party instrument plugin into a
303 slot; `ClapPluginFormat` adds CLAP hosting, which JUCE lacks.
`docs/PLUGINS.md` describes the plugin side and `docs/SKINNING.md` the planned
skins.

## Verification

`reborn_render` renders any song headless, which is what every measurement and
CI check is built on. `reborn_tests` covers the engine, the formats, the mod
reader and the effects' measured laws; `reborn_dock_tests` the docking geometry.
CI adds a cross-platform baseline, round-trips and a documentation check
(`docs/BUILDING.md`). The measurement method is in `docs/MEASUREMENT_PROTOCOL.md`.
