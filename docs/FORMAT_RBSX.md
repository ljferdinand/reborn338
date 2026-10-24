# `.rbsx` - Reborn's native session format

## Why a second format

`.rbs` is a 1998 song file. It has no field for a plugin graph, no field for
which sample set you are using, and its pattern records are exactly 16 steps.
Once Reborn hosts effects, a song is no longer expressible in it.

So there are two file types with two different jobs:

| | `.rbsx` (native) | `.rbs` (interop) |
|---|---|---|
| role | everything Reborn knows | exchange with the original program |
| plugin chains | ✅ saved with state | ❌ **dropped, with a warning** |
| sample set / mod reference | ✅ | mod name only (it has a field) |
| patterns > 16 steps | ✅ | ❌ truncated |
| read by the original | ❌ | ✅ |

**Both are loadable and savable.** Saving `.rbs` is a deliberate lossy export,
not a failure mode - it is how you hand a track to someone running the real
thing, and it must stay supported.

## The lossy-save contract

Saving to `.rbs` must warn **before** writing, listing what will not survive,
and must never silently discard work:

- hosted plugin chains and all their state (the big one),
- any pattern longer than 16 steps,
- anything stored only in `.rbsx` extras.

If the session has no plugins and no over-length patterns, the save is
lossless and the warning should say so rather than crying wolf. The dialog
offers "Save as .rbsx instead" as the default action.

## Container

Chunked binary, same EA-IFF-85 shape as `.rbs`, because the project already
has readers and writers for that shape and the core stays dependency-free.
Binary plugin blobs live in their own chunks rather than being base64'd into
text.

```
CAT (RBSX)
├─ RVER   u32 format version (1)
├─ RSNG   a complete .rbs image, byte-for-byte
│         (so the interop path is "extract this chunk and write it out")
├─ RENV   session environment: sample-set path, mod name + resolved path,
│         loop bars, transport state
├─ RINS   303 voice slots (optional): u32 slotCount, then per slot
│         { u8 engine (0=TPT, 1=Open303, 2=hosted, 0xFF=unrecorded),
│           u32 idLen + hosted-plugin identifier, u32 stateLen + state blob }
├─ RFXC   one per chain: u8 target (0..3 = channel, 255 = master),
│         then per slot: format id, plugin identifier, bypass flag
└─ RFXS   one per plugin instance: chain index, slot index, u32 length,
          opaque state blob
```

Two properties worth the design effort:

**`RSNG` holds a real `.rbs` image.** Saving `.rbs` from a session is then a
chunk extraction, not a re-serialisation - so the interop path cannot drift
away from the loader, and a `.rbsx` always contains a file the original
program can open. It also means `.rbsx` load is `.rbs` load plus extras, which
keeps one parser rather than two.

**Plugin state is separated from plugin identity** (`RFXC` vs `RFXS`, and the
same split inside `RINS`). A session whose plugins are missing on this machine
still opens: the chain structure loads, the missing slot is marked unresolved
and bypassed, and its state blob is preserved untouched so moving the session
back to a machine that has the plugin loses nothing. Silently dropping an
unresolved plugin's state would be data loss disguised as robustness.

**`RINS` is the 303 voice slots (fork P6), the first of the plugin chunks to be
implemented.** It records which voice fills each 303 slot: the native TPT parity
engine (the default), Open303, or a hosted third-party synth. For a hosted slot it
carries the plugin identifier and its opaque `getStateInformation` blob, so a
reopened session brings the plugin back with its own settings. It follows the
same "identity separate from state, missing plugin preserved" rule as `RFXC` /
`RFXS`: a hosted plugin that cannot be reloaded falls back to the default voice,
but its record is kept and re-emitted on the next save. The chunk is OPTIONAL, so
a session that only uses default voices omits it and older readers ignore it. The
same serialisation backs the plugin's DAW state (`get`/`setStateInformation`), so
a host project persists the full rig too, not only a native `.rbsx` file.

## Writing `.rbs` at all

`tools/rbsgen/rbsgen.py` authors valid files that the real program opens
(that is how the whole fixture suite is made), so the byte layout is proven.
The C++ writer (`src/io/rbs_write.cpp`) is a port of what rbsgen does, and
rbsgen doubles as the reference implementation to diff against.

The writer keeps ReBirth's two kinds of state apart. The chunks are the panel
at save time (`Song::mixerPanel`, `Song::effectPanel`, the knob headers),
which the app fills from the live engine before every save
(`Engine::songForSave`, which runs `captureLivePanel`), so the `RSNG` image
and the DAW state carry what the GUI shows. The tracks are the recorded song: each track's tick-0 dump is
the start state, the arrangement goes back as each device's pattern selects,
and every automation event goes back on its own track. GLOB is passed through
from the loaded file with only the fields Reborn models patched. Until
2026-09-26 the writer dropped the arrangement and every automation event,
wrote the faders linearly, lost the delay's grid and pan, and saved the song
as loaded rather than as edited (review W1 to W3), so `.rbs` saves, sessions
and DAW projects written before then lost all of that.

A round-trip test is the acceptance criterion: load each shipped song, save
it, reload the result, and assert the decoded model matches. `reborn_roundtrip`
compares the patterns, the knob headers, the arrangement per bar, the
automation, the start state and the panel field by field, and the GLOB bytes,
and requires a second save to be the same file. It must report all 16 demo
songs with 0 problems and 0 skipped.

## What neither format saves

Some controls have no field in `.rbs`, and `.rbsx` adds none for them, so a
save drops them and a reload puts back what the song implies. Found 2026-09-26
by listing each device's parameters against what `captureLivePanel` can write.

The GUI edits four of them, in the undocked knob panels
(`PluginEditor::buildPanel`) and the docked device panels and mixer strips
(`app/src/dock/`) alike:

- **The 808's alternate-voice levels** (Low, Mid and Hi Conga, Claves,
  Maracas), each a knob of its own. Each shared column has one level byte;
  loading gives the alternate its column's level, so an edit to the alternate
  alone is lost.
- **The 909's closed-hat level**, a knob of its own. One hi-hat level byte
  serves both hats, and it is saved from the open hat, so a closed-hat edit
  is lost.
- **Solo**, the strips' solo buttons, which is transport state
  (`Engine::soloMask`), not song data.
- **The Open303 waveform blend.** The waveform knob is continuous and so is
  the Open303 voice's waveform; the file holds one bit (saw or square), so a
  blend is saved as the nearer end.

The rest are not on the GUI at all: both panel builders hide them (Luke,
2026-09-18), so only a direct parameter write reaches them, code calling
`setParam` or `setParamByName` on the device, and for Mod In the engine
itself:

- **The 303 Volume and Drive** on both voices. The header has no byte for
  either.
- **The drum extras:** the 808's `tom_decay`, `cb_tune`, `cp_spread` and
  `ch_decay`, and the 909's `hh_tune`, `cr_decay` and `rd_decay`. None of
  them has a byte in the drum headers.
- **The PCF's Mod In**, which the engine rewrites on every step anyway.

A control that sits exactly on the song's start value is saved as the panel
the song was loaded with, not as that value (review follow-up F1), which is
what lets an unedited save leave the file's panel alone.

## Sequencing

1. **`.rbs` writer** + round-trip tests. Independent of plugins, and useful
   immediately: it is what makes the editor's pattern editing savable at all.
   Done; its arrangement, automation and live-panel gaps were closed
   2026-09-26 (review W1 to W3).
2. **`.rbsx` reader/writer** wrapping it, with `RSNG` + `RENV` only. Done,
   with `RINS` for the 303 voice slots.
3. **`RFXC`/`RFXS`** once chains exist.
