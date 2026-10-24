# Mods

A mod is ReBirth's package for replacing its look and its drum sounds: a single
`.rbm` file, made with Propellerhead's ModPacker utility from a folder of JPEG
pictures and AIFF recordings. Four mods came with ReBirth 2.0.1 and many more
were made by its users. A song can name the mod it was written with, and
ReBirth then plays it with that mod's drums.

Reborn reads mods so that you can play the mods you already have. No mod, no key
and no decoded mod content is in this repository; the tests build synthetic
archives instead. `docs/FORMAT_RBM.md` describes the format, the scrambling and
the key function in full, with every measurement behind them.

## Using a mod

From the command line, give the renderer the mod, or the folder that holds your
mods so it can find the one a song names:

```sh
reborn_render --song song.rbs --mod "path/to/Some Mod.rbm" --out out.wav
reborn_render --song song.rbs --mods "path/to/your/ReBirth/Mods" --out out.wav
```

With `--mods`, a song that names a mod in that folder gets it; a song that names
the stock "Standard ReBirth" set, or none, plays without one; and a song naming a
mod that is not there plays with Reborn's own drums and prints a warning. A song
names its mod by the name stored inside the mod, not by its file name, so
renaming the file does not matter.

In the app, the **Mod...** button loads a mod's drums. The folder it came from
is remembered, and from then on a song opened with **Load .rbs...** that names a
mod in the same folder gets that mod automatically. When a song names a mod the
app cannot find, or finds one it cannot read, the header says so ("needs mod",
"mod unreadable"). A saved `.rbsx` session records the mod's name and path and
reopens it.

`--samples` and the app's **Samples...** button load drums from a folder of WAV
files instead (`docs/SAMPLES.md`). When the renderer is given both a mod and a
sample folder, the mod is loaded first and the folder's files then replace the
mod's recordings voice by voice, whatever order the options were given in.

## What works

- **The container.** Every part of the archive is parsed: the header, the mod's
  name and credits, and its 106 embedded files (in the four retail mods, 59
  pictures, 46 AIFF recordings and one demo song).
- **Unscrambling.** ModPacker scrambles every embedded file past its first
  kilobyte. Reborn generates the keys exactly from two sums over the readable
  part, in under 0.2 ms, and confirms each half of the key separately before
  trusting it. All four retail mods decode exactly, and so do partial mods that
  ModPacker packed.
- **A self-check.** Every decode is checked before it is used: picture data must
  be clean, sample levels and residuals must be plausible at every key position,
  and no sample may wrap. A first read of a whole mod takes 43 to 125 ms, nearly
  all of it this check, and the result is cached for the rest of the session.
- **The drums.** All 45 drum recordings are mapped onto Reborn's 808 and 909
  voices, with the same variant selection and snare construction as a sample
  folder (`docs/SAMPLES.md`). The 46th recording, a startup chime, is skipped.
- **Picture bytes.** The pictures can be extracted as the encoded JPEG bytes they
  are, from your own copy of a mod; nothing draws them yet.
- **A fallback.** When the generated keys do not fit an archive (one ModPacker
  did not make, one packed without a `ModName.txt`, one edited after packing),
  the keys are estimated from the content instead, which takes 3 to 5 s on a
  24-core machine and up to 9 s on 4 cores. The app reads mods off the audio
  thread's lock, so playback does not stall.

## What does not, or not fully

- **Mods edited after packing.** Editing a file inside a packed mod changes the
  sums the keys come from, and the keys it was packed with no longer follow from
  them. Reborn checks the neighbouring keys such an edit would produce and
  refuses the mod when one of them decodes it better. Some edits slip through:
  in a sweep of 200 edited synthetic mods, 8 were taken with keys slightly wrong,
  leaving an error 32 to 57 dB below the sound. A mod edited after packing whose
  keys must be estimated and that holds fewer than 12 sounds is refused.
- **Archives without `ModName.txt`.** ModPacker packs them under other keys, so
  they always go through the slower estimate, and the estimate needs at least 12
  sounds to be trusted.
- **Skins.** No mod artwork is ever drawn. `docs/SKINNING.md` describes how a
  skin could be rendered from the user's own mod.
- **Writing mods.** There is no `.rbm` writer.
- **Mod-voice parity.** How ReBirth plays a mod's recordings (levels, envelopes,
  tuning) was never measured voice by voice; the trims fitted for the stock
  sounds are applied to mod sounds unchanged. The whole-song check that was made
  is encouraging: on the demo songs written with mods, Reborn's mix agrees with
  ReBirth's within 0.54 dB in the passages measured with the compressor off.
  With it on, Reborn's compressor cuts harder than ReBirth's, which is a
  compressor defect rather than a mod one (`docs/BACKLOG.md`, entry 16).

When a mod is refused, Reborn plays without it (its own drums, or your sample
folder) and says why. The reasons, as printed:

| Message | Meaning |
|---|---|
| not an .rbm mod (expected CAT/PRBM) | the file is not a mod |
| not a mod ModPacker made (over 32 MB, 1024 files or 8 M audio samples); refused unread | the file is larger than any mod, so it is not read at all |
| the mod's sounds give its self-check too little to confirm its keys (...) | too few, too noisy or too quiet sounds to tell right keys from wrong ones |
| the mod looks edited since it was packed (...) | a neighbouring key decodes it better, and there are too few sounds to estimate |
| the mod's keys had to be estimated, and it holds too few sounds to do that with confidence (...) | an estimate was needed and fewer than 12 sounds reach past the readable part |
| the mod's embedded files could not be unscrambled (the result failed its self-check) | no keys passed |

## Diagnosing a mod

`reborn_rbmkeys` reads a mod the way the readers do and reports what happened:

```sh
reborn_rbmkeys "Some Mod.rbm"              # generated keys, or the estimate
reborn_rbmkeys "Some Mod.rbm" --estimate   # skip the generated keys
reborn_rbmkeys "Some Mod.rbm" --raw        # run the self-check on the file as stored
```

It prints the key sums, how each half of the key was confirmed, the self-check's
verdict and the result. `--out keys.bin` writes the keys, and `--truth keys.bin`
scores them against known keys; keep any such file out of the repository, since
keys are derived from someone else's mod.

## Interoperability

The reader exists so that a user can play the mods they already have. The
scrambling and the key function were found by packing content of our own choice
with ModPacker and reading its output; no program was disassembled or traced.
Whether reading mods this way is permitted where you live is for you to judge.

## Where to take it next

Drawing skins from the user's own mod (`docs/SKINNING.md`); measuring how ReBirth
plays mod recordings so their levels match; reading archives without
`ModName.txt` exactly rather than by estimate; and a writer, if anyone wants to
make mods with Reborn. `docs/HANDOVER.md` puts these among the other open work.
