# rbsgen: controlled `.rbs` test-song generator

`rbsgen` produces minimal `.rbs` songs for A/B testing Reborn against the
original ReBirth. Each song exercises one 303 voice with known, static knob
settings and no automation, so a measured difference between the two apps is the
voice under test, not arrangement, recorded knob rides, or effects we do not
control.

## Two modes

**scratch (standalone, recommended).** Author a fresh RB40 file from the
documented layout, with no baseline and no bytes copied from the original
program. Confirmed to open in ReBirth using a neutral HEAD string, so it is
clean-room pure, and confirmed to play its knob values back in ReBirth's
song-mode export (the export path) once the 303 track carries Calm's tick-0
snapshot including param 0. This is the recommended path for A/B.

**patch (alternative).** Take a real `.rbs` you can already open in ReBirth and
overwrite parameter bytes in place, changing nothing about the file's size or
structure. Useful when you want to test a voice inside a real song's surrounding
context, or as a fallback if a future ReBirth build is pickier about a
synthesised file.

### The scratch-mode fixes

The first from-scratch file failed in ReBirth two ways, both now understood.
First, the skin name: the built-in skin is stored as `Standard ReBirth` at GLOB
offset 15 with no leading `@` (the `@` seen in a hex dump is the shuffle byte
0x40 at offset 14 sitting just before it). The generator had written
`@Standard ReBirth`, so ReBirth searched for a nonexistent mod. Second, USRI is a
fixed 712-byte field and the generator wrote 256, which ReBirth rejected as an
unsupported format. Scratch mode now writes the correct skin, a 712-byte USRI,
populated tracks, and a synthesised GLOB and HEAD. The one remaining unknown is
whether ReBirth validates the free string inside HEAD. It does not: a
fully-synthesised file with a neutral HEAD string opens in ReBirth, so scratch
mode needs no bytes from the original program.

## Requirements

Python 3.10+ and nothing else (standard library only).

## Usage

Scratch (recommended), no baseline needed:

```
python3 tools/rbsgen/rbsgen.py --out res_hi.rbs \
    --cutoff 90 --reso 115 --envmod 70 --note 9 --bars 4 --title "res hi"
```

`--bars` sets the song length; a few bars is plenty and keeps ReBirth exports
short. The knobs are written as automation across those bars (see below).

Patch an existing song in place (alternative):

```
python3 tools/rbsgen/rbsgen.py --patch /path/to/RealSong.rbs \
    --out res_hi.rbs --voice bass2 --cutoff 90 --reso 115 --envmod 70 --note 9
```

This overwrites, inside a copy of `RealSong.rbs`, without changing any sizes:
the chosen 303's knob header (your cutoff/reso/etc.), that 303's pattern record 0
(a repeated note by default), that 303's automation track in place
(pattern-select events forced to 0, knob events set to your static values, mute
events set to unmuted, so knobs hold and the arrangement stays on pattern 0), and
the MIXR channel on/off so only the chosen section is on.

Library, for scripting a sweep:

```python
import rbsgen
base = open("RealSong.rbs", "rb").read()
for reso in (20, 60, 100, 127):
    data = rbsgen.patch_song(base, knobs=dict(cutoff=90, reso=reso, envmod=70),
                             pattern=rbsgen.repeated_note(9), voice="bass2")
    open(f"res_{reso:03d}.rbs", "wb").write(data)
```

Knobs are 0..127: `tune` (64 = centre), `cutoff`, `reso`, `envmod`, `decay`,
`accent`, `waveform` (0 = saw, 1 = square). `note` is a semitone 0..12 (9 = A)
played on every step; `--accent-steps 0,4,8,12` accents specific steps.

## Rendering both sides

Reborn (this repo), voice isolated, effects off:

```
reborn_render --song res_hi.rbs --solo bass2 --no-dist --no-delay \
    --seconds 60 --out res_hi_reborn.wav
```

ReBirth (the reference): open `res_hi.rbs` and Export it. ReBirth's Export runs
in SONG mode, so it plays the arrangement, not the current pattern-mode knob
position. Two consequences the generator already handles: the song is kept short
(a few bars, set with `--bars`), and the knob values are written as automation at
the start of every bar so they play back on export. Mute the other
sections, turn the master compressor off, export, then measure both sides with
one identical method.

## Choosing a baseline for patch mode

Any `.rbs` you can open in ReBirth works. Prefer one whose 303 you can drive
cleanly; the patcher forces the arrangement to pattern 0 and neutralizes the
chosen voice's automation, so the baseline's own bassline and knob rides do not
leak in. The other sections are muted at export time, so they do not matter.

## Scope

This was written for the 303 first (the same in-place approach extends to the
808 and 909 voices) and for the distortion. The PCF and compressor were wired in
the loader later and are A/B tested the same way; `docs/MEASUREMENT_PROTOCOL.md`
has the rules every fixture follows.
