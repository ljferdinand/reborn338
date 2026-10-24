# Drum samples

ReBirth's 808 and 909 are sample players: they play recordings of the Roland
machines, shaped by their knobs. Reborn ships no recordings. By default both
drum machines use Reborn's own synthesis, written from measurements of how
ReBirth's drums sound, so every song plays out of the box. If you have
recordings, Reborn can play them instead, from a folder of WAV files or from a
ReBirth mod (`docs/MODS.md`).

## A sample folder

A sample folder is a directory of WAV files named after the voices they
replace. Point the renderer at it with `--samples`, or choose it in the app with
**Samples...**:

```sh
reborn_render --song song.rbs --samples path/to/kit --out out.wav
```

Each file must be uncompressed 16-bit PCM, mono or stereo (stereo is mixed to
mono), at any sample rate; the voice plays it back at its recorded rate. Every
file is DC-blocked on load. A voice whose file is missing, unreadable or in
another format keeps whatever it had (Reborn's synthesis, or a mod's
recording), so a folder can replace as few voices as you like.

The names, 45 in all:

| Machine | Files |
|---|---|
| 808 | `808_bd`, `808_sd1` to `808_sd5`, `808_n1` to `808_n4`, `808_lt`, `808_mt`, `808_ht`, `808_lc`, `808_mc`, `808_hc`, `808_rs`, `808_cl`, `808_cp`, `808_ma`, `808_cb`, `808_cy`, `808_oh`, `808_ch` |
| 909 | `909_bd1` to `909_bd4`, `909_sd1` to `909_sd5`, `909_sdn1` to `909_sdn3`, `909_lt`, `909_mt`, `909_ht`, `909_rs`, `909_cp`, `909_oh`, `909_ch`, `909_crash`, `909_ride` |

each with the extension `.wav`. The numbered files are variants, and the
song's knobs choose among them when the set is loaded:

- **808 snare.** The Tone knob picks the body, `808_sd1` (tone 0) to `808_sd5`
  (tone 127), and Snappy picks one of four noise takes, `808_n1` to `808_n4`.
  The body and the noise are mixed by a fitted Snappy law into one snare.
- **909 bass drum.** Tune picks `909_bd1` to `909_bd4`, in equal quarters of the
  knob.
- **909 snare.** Tune picks the body, `909_sd1` to `909_sd5`, and Snappy picks
  the noise, `909_sdn1` to `909_sdn3`. Tone sets how long the noise layer is held
  before it is gated off, and Snappy how loud it is.
- **808 shared columns.** The congas (`808_lc`, `808_mc`, `808_hc`), claves
  (`808_cl`) and maracas (`808_ma`) play when a song switches the shared column
  from tom, rimshot or clap to its alternate.

A constructed snare needs its body and its noise file at the same sample rate;
with only the body, the body plays alone.

**Variants latch at load.** The choice of variant uses the song's knob settings
at the moment the samples are loaded, and moving those knobs afterwards, by hand
or by automation, does not swap recordings. The app re-applies the folder each
time a song is opened, so each song gets the variants its own settings ask for.

## Levels, and why they differ by path

Each drum voice has two level trims: one for the sample path and one for the
synthesis path, chosen per voice by whether a sample is loaded. The sample-path
trims were fitted with ReBirth's own drum recordings loaded, so a kit recorded at
very different levels will play louder or quieter than ReBirth would play it.
The two tables are `kTrim808`/`kTrimSyn808` in `src/devices/drum808.cpp` and their
909 counterparts in `src/devices/drum909.cpp`; `docs/NON_OBVIOUS_BEHAVIOUR.md`
explains why there are two.

## Where recordings come from

The names follow ReBirth's own drum set, which is what the project measured
against, but Reborn includes no way to extract those recordings and none of
them are in this repository. Any 16-bit WAV files with these names will play:
your own recordings, or a kit you have the rights to use.

## Samples and mods together

A mod (`--mod`, `--mods`, or **Mod...** in the app) supplies the same 45 voices
from its embedded recordings, under the same names and the same variant rules.
When the renderer is given both, it loads the mod first and then the folder, so
the folder's files replace the mod's voice by voice, whatever order the options
appear in. This is easy to do by accident: adding `--samples` to a render of a
song that names a mod silently replaces that mod's drums.

In the app, each load replaces the voices its source provides and leaves the
others as they were, so loading a folder after a mod keeps the mod's voices
wherever the folder has no file. Loading a mod makes the app forget the folder;
loading a folder does not make it forget the mod. A `.rbsx` session records the
folder and the mod's name and path, and restores them when reopened.
