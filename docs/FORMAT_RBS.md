# The `.rbs` song format (reverse-engineered)

This document records the on-disk layout of the original `.rbs` song files, as
reconstructed **purely by inspecting the shipped data files** (the demo and
default songs). No original program code was examined or used. It is provided as
preservation documentation and as the specification the loader in
[`src/io/rbs.cpp`](../src/io/rbs.cpp) implements.

Legend: ✅ confirmed & implemented · 🔎 confirmed by inspection · 🚧 partially
decoded / in progress.

---

## 1. Container: EA-IFF-85, big-endian ✅

The file is an [EA IFF 85](https://en.wikipedia.org/wiki/Interchange_File_Format)
chunk tree. Every chunk is:

```
+0   char[4]  chunk id
+4   u32      size (BIG-ENDIAN), not counting the 8-byte header
+8   u8[size] payload            (padded to an even length)
```

**Group** chunks (`CAT `, `FORM`, `LIST`, `PROP`) begin their payload with a
4-byte *group type*, followed by sub-chunks. All multi-byte integers are
big-endian (the format originates on 1990s Macintosh/68k + PowerPC).

### Top-level tree 🔎

```
CAT  RB40                         song container (form type "RB40" = v2.0 song)
├─ HEAD                           header / version
├─ GLOB                           global settings (tempo, shuffle, skin name)
├─ USRI                           user info: author + description (+ title)
├─ CAT  DEVL                      device rack
│  ├─ MIXR                        mixer settings
│  ├─ DELY  PCF   DIST  COMP      effect settings
│  └─ 303   303   808   909       instrument settings + pattern banks
└─ CAT  TRKL                      arrangement / automation tracks
   └─ TRAK × N                    per-track event lists
```

---

## 2. `GLOB`: global settings ✅ (tempo) / 🚧 (rest)

| Offset | Type | Meaning | Status |
|-------:|------|---------|--------|
| +0 | u8 | version / flags (observed `01`) | 🔎 |
| +1 | u8 | flag (loop/shuffle-enable?), varies `00`/`01` | 🚧 |
| **+2** | **u32 BE** | **tempo × 1000** (e.g. `0x0002_22E0` = 140000 → 140.000 BPM) | ✅ |
| +6..+10 | bytes | misc (varies per song) | 🚧 |
| **+11** | **u16 BE** | **loop length = bars × 3**: the transport's "Loop Length", and what an export runs for | ✅ |
| +13 | u8 | misc | 🚧 |
| +14 | u8 | shuffle amount (default `0x40`) | ✅ |
| **+15** | **cstr** | **skin / mod name this song was authored against**, a REFERENCE, never embedded content | ✅ |
| +80 | cstr | a download URL: `ftp://ftp.propellerheads.se/pub/rbmods/<mod>.rbm` in every demo song that names a mod, `ftp.propellerheads.se` in the stock ones | 🔎 |

The writer passes GLOB through. A loaded song keeps its own payload
(`Song::globRaw`) and only tempo (+2), loop length (+11), shuffle (+14) and
the skin name (+15) are patched, and a loop length or skin name the stored
bytes already decode to is left as it is. So +0, +1, +6..+10 and +13, whose
meanings are still under investigation, and the URL at +80 survive a save.

### Songs reference a mod; they never embed one

Checked across all 40 shipped songs (demo + default + the demos inside the four
retail mods): **no `.rbs` contains `EMBF` or a `PRBM` container.** The
relationship is one-way: a song names its mod, a mod embeds a demo song.

| skin name at GLOB+15 | songs | resolves to |
|---|--:|---|
| `Standard ReBirth` | 40 | the stock look/samples (no file) |
| `Massen's MSM 2.0` | 16 | `MSM2.rbm` |
| `Pitch Black Edition 2.0` | 12 | `PBE2.rbm` |
| `Metallicon+` | 4 | `Metallicon.rbm` |

This matters for **fidelity, not just looks**: a song written with a mod was
composed using that mod's drum samples, so rendering it with the stock set is
wrong. `findModByName()` (see [FORMAT_RBM.md](FORMAT_RBM.md) for the name
field) resolves the reference against the user's own mods folder, and
`reborn_render --mods <dir>` applies it automatically.

**Loop length decoding was validated against songs of independently known
length** (2026-07-24). The stored value is always exactly three times the bar
count:

| Song | GLOB+11 | ÷3 | true length |
|------|--------:|---:|------------:|
| purpose-made 20-bar test song | 60 | 20 | 20 bars |
| Calm | 516 | 172 | 172-bar export |
| Trakka Rekka | 372 | 124 | 124-bar export |
| Silent Default Song | 3 | 1 | 1 bar |
| ReBirth 1.0 Default Song | 12 | 4 | 4 bars |

This field is independent of the arrangement: a song may carry a long
arrangement but a short loop (Fonda Workout stores 3 = 1 bar). Song-mode export
follows the loop length, so authoring tools **must** write it. Omitting it (or
copying another song's value) makes every generated song export at that other
song's length.

**Tempo decoding was validated across songs:**

| Song | GLOB+2 (hex) | value | BPM |
|------|--------------|-------|-----|
| Calm | `0001FBD0` | 130000 | 130.000 |
| Fast Forward | `000222E0` | 140000 | 140.000 |
| G-Spot | `000249F0` | 150000 | 150.000 |

---

## 3. `USRI`: user info ✅

Two fixed fields, NUL-terminated, in the Mac Roman / Latin-1 range (©, curly
quotes and accents appear as high bytes):

| Offset | Field | Notes |
|-------:|-------|-------|
| +0  | **author / credit line** | fixed ≈41-byte field, e.g. `© 1998 by DJ Mouse` |
| +41 | **description** | free text; authors typically open it with the song title in `"quotes"` followed by notes/contact |

The loader reads the author field, extracts the first quoted substring of the
description as the title, and folds Latin-1 punctuation/accents to readable
ASCII.

---

## 4. Device chunks: pattern banks ✅ (decoded & verified)

Every instrument chunk is a fixed-size knob header followed by a bank of **32
pattern records** (4 banks × 8, as in the UI). Exact sizes validated against
all 18 shipped songs with zero mismatches:

| Chunk | Total size | Header | Records |
|-------|-----------:|-------:|---------|
| `303` | 1097 | 9 | 32 × 34 bytes |
| `808` | 6238 | 30 | 32 × 194 bytes |
| `909` | 6239 | 31 | 32 × 194 bytes |

Every record begins `[shuffle u8 (0/1)] [length u8 (1..16)]`.

### `303` record: 2 bytes + 16 × 2-byte steps ✅

Each step is `[note u8] [flags u8]`:

- **note**: semitone `0..12` (C up to the C an octave above)
- **flags**: `0x01` **SLIDE** (tie/glide into next step) · `0x02` **ACCENT** ·
  bits 2–3 octave flags (below) · **`0x10` REST**

> **Slide and accent, corrected 2026-08-02 in this document.** This line said
> `0x01` accent and `0x02` slide, which is backwards and contradicted both the
> loader and the generator. `src/io/rbs.cpp` has `kFlSlide = 0x01,
> kFlAccent = 0x02` with a measured justification dated 2026-07-24, and
> `tools/rbsgen/rbsgen.py` agrees. The measurement: a generated song with
> `0x01` on every fourth step, all steps the same note, came back from ReBirth
> with **no envelope retrigger** on those steps, which is tie behaviour and not
> accent, and it was invariant to the accent knob at 0 and at 110. The code was
> right and the prose was stale. Prefer the code here.
- **octave flags** (bits 2–3): independent signed flags that **sum**:
  `0x04` = **+1** octave · `0x08` = **−1** octave · `0x0C` = **net 0**
  (up and down cancel)

> **Octave flags (corrected 2026-07-23).** Proven by measurement, not
> structure-guessing: (1) a generated scratch song wrote `0x04` on a note-0
> step (base C2, 65.4 Hz) and ReBirth exported it at **130.7 Hz (C3), one
> octave up** (the earlier `0x04`=down reading was swapped); (2) FFT of the
> original *Trakka Rekka* 303-solo exports shows every `0x08` step sounding
> **one octave below** its notated semitone (bass2 pat 1/16, bass1 pat 17),
> and the `0x0C` steps of bass1 pat 17 sounding at the **unshifted** semitone
> (note 3 + tune → E♭3), i.e. the two flags cancel. The same combs put
> tune=127 ≈ 25 cents flat of +12 st, fixing the tune law at
> `(byte − 64) × 12/64` semitones.

**The gate is inverted**: a step *plays* when `flags & 0x10 == 0`. The shipped
`Default Songs/Read Me.txt` confirms this: the "ReBirth 1.0 Default Song"
(all-zero steps) contains *"repeating synth Patterns, as in version 1.0"*,
(i.e. all-zero = every step gates a C), while the "Silent Default Song"
(`0x10` on every step) contains *"completely silent patterns"*.

Flag-bit roles were pinned by musical-structure analysis across 609 contentful
patterns (e.g. *Calm*'s main line decodes to an ascending C-minor-pentatonic
walk with slide-tied notes and octave-up on the climax; competing bit
assignments would put slides on rests).

The 9-byte header is `[version, ?, k0..k5 (0..127), waveform]`: six knob
values believed to be *cutoff, resonance, env-mod, decay, accent, volume*
(order is a working assumption; wrong order affects timbre only, never notes).

### `808` / `909` record: 2 bytes + 16 steps × 12 voice bytes ✅

Step-major layout: each step is 12 bytes, one per row:

- **column 0 = global ACCENT row** (hardware-style; accents every voice hit on
  that step)
- **columns 1–11 = instruments in panel order**:
  - `808`: BD, SD, LT, MT, HT, RS/CL, CP/MA, CB, CY, OH, CH; values 0=off, 1=hit
  - `909`: BD, SD, LT, MT, HT, RS, CP, **CH, OH**, Crash, Ride; values 0=off,
    1=soft, 2=normal, 3=accented

Column identities were verified statistically across all songs: BD has the
highest on-beat concentration (0.63–0.65), the clap sits on backbeats, the open
hat is almost purely offbeat (0.07–0.11 on-beat), the 909 crash is 82 %
downbeat.

**Drum knob headers** (panel-order fields, aligned against the untouched
default templates where levels sit at `0x64` and centred knobs at `0x40`):

`909` (31 bytes):
`[ver][?][accent] BD[level,tune,attack,decay] SD[level,tune,tone,snappy]
LT[level,tune,decay] MT[...] HT[...] RS[level] CP[level]
HH[level, CH-decay, OH-decay] CR[level,tune] RD[level,?] [?,?]`

`808` (30 bytes):
`[ver][?][accent] BD[level,tone,decay] SD[level,tone,snappy]
LT/MT/HT[level,tune,tom-conga-switch] RS[level,switch] CP[level,switch]
CB[level] CY[level,tune,decay] OH[level,decay] CH[level] [?]`

The loader applies levels + the named voice controls to the engine's drum
instruments (see `decode808Knobs`/`decode909Knobs` in `src/io/rbs.cpp`). A few
trailing bytes remain unidentified.

**Shared-column switches (808 only).** Five columns each drive one of two
voices, selected by a byte in this header. Confirmed 2026-07-25 against fixture
batch E (see [research/DRUM808_FINDINGS.md](research/DRUM808_FINDINGS.md)):

| offset | column | value 0 | value != 0 |
|---|---|---|---|
| +11 | LT | Low Conga | Low Tom |
| +14 | MT | Mid Conga | Mid Tom |
| +17 | HT | Hi Conga | Hi Tom |
| +19 | RS | Claves | Rim Shot |
| +21 | CP | Maracas | Hand Clap |

**0 selects the alternate voice**, and is also the factory default. 17 of the 18
shipped songs set at least one column to an alternate, so ignoring these bytes
mis-voices most of the catalogue. Patterns still store the PRIMARY voice index
(that is what the file's column means); the routing happens at trigger time.

> **Loader status:** implemented in `src/io/rbs.cpp`. All 18 shipped songs
> load; the 16 demo songs render their decoded patterns (verified by onset-grid
> alignment and pitch-class analysis of the rendered audio); the 2 default
> templates correctly render silent.

---

## 5. `TRKL` / `TRAK`: arrangement & automation ✅ (decoded & verified)

The `TRKL` group holds **one `TRAK` chunk per device**, in the order:

> **MIXR, 303 (bass 1), 303 (bass 2), 808, 909, DELY, DIST, PCF, COMP**

(instruments before effects; note this differs from the `DEVL` chunk order,
which is MIXR, DELY, **PCF, DIST**, COMP: the two middle effects swap places
between the two lists.)

> **Corrected 2026-08-27.** This line read `DELY, PCF, DIST, COMP`. Track 6 is
> the distortion and track 7 is the PCF, by two independent proofs. By param
> count: track 6 carries only params 0..2, and the distortion has two knobs plus
> its on/off, while track 7 carries params 0..6 and the PCF has six knobs plus
> its on/off. By value: track 7's tick-0 dump reproduces the `PCF ` chunk byte
> for byte in *Mutilated Species II* and *KiloMix*, and across the corpus it
> matches that chunk on 27 of 31 never-ridden params where the next best
> candidate matches 6. Harmless so far only by luck, since the one effect track
> we read is the compressor's, at index 8 under either order.

### Event stream

```
TRAK payload = [u32 BE event count]
             + count × [VLQ delta-time][param u8][value u8]
```

where VLQ is a MIDI-style variable-length quantity (7 bits per byte, high bit
= continuation). **All 162 tracks across the 18 shipped songs parse exactly**
under this layout: byte counts consume each payload precisely.

### Time base

One tick = **a 32nd note**, so a 16-step bar = 32 ticks. Evidence: every
pattern-change gap in every song is a multiple of 32 (1-bar granularity, with
1713 one-bar gaps as the most common), and total song durations come out at
plausible lengths (3:31–8:30 for most demos; the 15-minute outlier is
literally titled "KiloMix"); the 1/16-tick alternative would double these.

### Params

- **Instrument tracks (positions 1–4):** `param 1` = **pattern select**
  (value 0..31; changes land on bar boundaries in every shipped song).
  `param 0` = mute toggle (0 = muted; 14 of 36 bass tracks start muted for
  intros). On the 303 tracks, `params 2..7` = the panel knobs in panel order
  (**tune, cutoff, resonance, env-mod, decay, accent**), and `param 8` =
  waveform (0/1). Confirmed by automation statistics: `param 3` (cutoff)
  carries 20,317 timed events across the corpus, 10× any other knob, and the
  time-0 dumps agree with the `303` chunk header bytes at matching offsets
  (differences = knobs moved after the automation was recorded, i.e. the
  TRAK dump is the authoritative song-start state).
- **Drum tracks (positions 3-4):** the same shape, and `param N` is simply
  **header byte N**: 2..28 for the 808, 2..29 for the 909, in the panel order
  `decode808Knobs` / `decode909Knobs` read. Confirmed 2026-08-26: on both
  machines every control whose knob was never touched after recording has a
  tick-0 event equal to its header byte exactly. The file's order is NOT
  Appendix C's: the MIDI map lists Hi-hat 83 before RS 84 and CP 85, while the
  chunk stores **RS, CP, then the one shared hi-hat level** at bytes 20, 21, 22.
  Measurement wins; a table built from the MIDI map silences those three voices.
  Because the header is the panel at SAVE time and the tick-0 dump is the song's
  START, the two often disagree and only the dump is playable: Trakka Rekka's
  909 snare reads 127 in the header and 0 at tick 0, with 54 rides after it.
- **Mixer track (position 0):** enumerated in full 2026-08-27. `param 1` is the
  **compressor target** and `param 2` the **PCF target**; params 3 to 5 never
  appear. Then per-channel groups of four at base `6 + 6*ch`:
  `[volume 0..127][pan, 64 = center][echo send][distortion on/off]`, with +4 and
  +5 unused. The time-0 dump provides the song's starting mixer state. There is
  no param for the channel's mute, which matches the manual: the mute is "not
  part of the Song automation".
- **Effect tracks (positions 5–8):** as with the drums, **param N is chunk byte
  N**, and `param 0` is the unit's on/off. So the delay track's `param 1` is its
  1..16 step-time knob because that is `DELY` byte 1.

### Loader status

Implemented: pattern-select timelines → per-bar arrangement rows; initial
mixer volume/pan; **timed automation dispatch** (303 knobs + waveform, **drum
knobs on both machines**, device mute toggles, mixer volume/pan rides), applied at step boundaries (half-tick
quantization, ≤ ~27 ms). Verified: *Fast Forward* plays its bass arrangement
note-for-note (D2 → A3 octave-up → C2), and *Calm*'s recorded filter ride now
renders (spectral-centroid variation rose from 0.15 to 0.76 relative sd, with
the authored dark-intro → build → release arc clearly visible). Not yet
applied: effect-track automation (including each effect track's `param 0`
on/off switch), per-pattern shuffle, the mixer's per-channel switch param.

---

## 6. `MIXR` and effect chunks ✅ (core fields decoded)

**`MIXR` (64 bytes)**, validated across all 18 songs:

| Offset | Field |
|-------:|-------|
| +0 | master volume (0..127; `0x64` nominal) |
| +1 | **compressor target**, exclusive: 0 = off, 1 = the master strip's own COMP button, 2..5 = sections 1..4. The 1 was confirmed 2026-08-28 by clicking the master strip's COMP button, which wrote 1 and replaced the section value (BACKLOG 4) |
| +2 | **PCF target**, exclusive: 0 = off, 1..4 = section |
| +3..+15 | zero in all 18 songs |
| +16 + 12·ch | per-channel group: `[on/off][volume][pan, 64 = center][echo send 0..127][distortion on/off][7 × 0]` |

Volume/pan here mirror the mixer TRAK's time-0 dump (which is authoritative);
the echo send and the channel on/off are unique to this chunk.

> **Corrected 2026-08-27.** +1 was documented as the distortion input selector
> and read that way by the loader, so every song routed the distortion to
> whatever section the *compressor* was on. The three routing switches were
> settled by clicking each one in ReBirth and diffing the save, since none of
> them is on the MIDI map. Clicking PCF on section 1 wrote 1 to +2 and then
> section 3 wrote 3, replacing it rather than adding a bit, which is the
> exclusivity the manual describes. Clicking DIST on section 2 toggled the
> channel-group byte at +32 and left +1 and +2 alone, so the distortion is a
> per-section flag with no exclusivity. Clicking COMP on section 1 wrote 2 and
> on section 4 wrote 5, an offset of one from the 1-based section index, which
> is what leaves room for the master. The old reading's supporting evidence
> (*Metallicon City* = 3) survives as a coincidence: under the corrected reading
> that song has the compressor on bass 2 and no distortion routed at all.

**+2 is zero in all 16 demo songs** while the mixer TRAK's `param 2` at tick 0
is non-zero in 11 of them. As everywhere else in this format the tick-0 dump is
the song's real starting state and the chunk is only the save-time panel, so
reading the chunk alone leaves the PCF routed nowhere in every shipped song.

Volume and pan are **not linear in the byte**. Both faders are exponential at
about 0.370 dB per step, and pan holds the near side at unity while attenuating
the far side by `45.9 * x^2` dB with `x = |byte - 64| / 63`. Measured 2026-08-26
by sweeping each one alone; see PARITY.md for the fits and what reading them as
linear cost.

**`DELY` (8 bytes)**: `[ver][time in sixteenths 1..16][grid][feedback][pan]`.
The grid byte is 0 for the triplet grid and nonzero for straight (decoded in
the delay supertask, 2026-09-10); the pan is the mixer pan byte.
**`DIST` (8 bytes)**: `[ver][shape][amount][...]`. Measured 2026-09-17
(supertask 7, dist-grid): byte 2 is Amount and gates the effect, since byte 2 =
0 gives no distortion whatever byte 1 holds, and byte 1 is Shape. That is
Appendix C's order (Shape CC 104, Amount CC 105). This line said
`[amount][tone]` until 2026-09-26, and the loader read it that way until the
measurement.
**`COMP` (8 bytes)**: `[ver][ratio][threshold][meter, 0x7F]`, decoded
2026-09-16. Byte 0 is 1 in all sixteen demo songs whatever their COMP tracks
say about on/off, so it is a version byte like the other effect chunks'.

**`PCF ` (12 bytes)**. Note the trailing space in the chunk id. Mapped
2026-08-27 by driving each control over its MIDI CC (Appendix C 94 to 99) and
diffing the save against a copy of the Silent Default Song, whose PCF chunk is
all zeros, so any non-zero byte came from us. Three passes at distinct values:

| Offset | Field | Range |
|-------:|-------|-------|
| +0 | version (`1`; the ReBirth 1.0 default song writes `0`) | |
| +1 | **Freq**, base cutoff | 0..127 |
| +2 | **Q**, resonance | 0..127 |
| +3 | **Amt**, envelope depth | 0..127 |
| +4 | **Pattern** | 0..54 |
| +5 | **Decay**, envelope decay time | 0..127 |
| +6 | **Mode**: 0 = BANDPASS, 1 = LOWPASS | 0/1 |
| +7..+11 | zero in all 18 songs and in every save we made | |

The panel puts Pattern and Mode on top and the four sliders Freq, Q, Amt, Dec
in a row beneath. The chunk keeps the sliders' order but breaks the row apart to
wedge Pattern between Amt and Dec, and puts Mode last.

> **Mode corrected 2026-08-27.** This table said `0 = LP, 1 = BP`, read off a
> panel screenshot taken with the byte at 0, where the switch handle sits low
> beside the LP legend. The handle is drawn low when the switch is UP. Measured
> from audio it is the other way round: byte 1 fits a 2-pole lowpass at 0.01 to
> 0.5 dB rms across the whole sweep and byte 0 fits a bandpass at 0.3 to 0.6,
> and neither fits the other shape at all. What the screenshot did settle is
> that the two modes are lowpass and bandpass, so Appendix C's "LP/HP" for
> CC 95 is wrong and the body text on pages 63, 65 and 160 is right.

The pattern CC clamps rather than scales: CC 0 gave pattern 0, CC 64 gave 54 and
CC 127 also gave 54, so the CC is the pattern number and saturates at the top.
The corpus never exceeds 52.

The claim that "none of the shipped songs route a channel to the PCF" was an
artefact of reading `MIXR` +2 alone, which is the save-time panel. Eleven of the
sixteen demo songs route the PCF at tick 0.

Applied by the loader: master volume, channel on/off, echo sends, distortion
routing + amount/tone, delay time-in-steps + feedback.

## 7. Writing ✅

`src/io/rbs_write.cpp` keeps the file's two kinds of state apart, as above:

- **Chunks are the panel at save time.** They are written from
  `Song::mixerPanel`, `Song::effectPanel` and the knob headers, which the
  loader fills from the chunks alone and the app fills from the live engine
  before every save. Faders and pans go through the exact inverses of the
  load laws (`faderByteFromGain`, `panByteFromPan`), the send stays
  `byte / 127`.
- **Tracks are the recorded song.** Each instrument track is its device's
  pattern selects (param 1 at bar 0 and wherever the arrangement changes that
  device's pattern), its automation as loaded, and a tick-0 dump for any
  param the automation does not state there (param 0 = on, param N = header
  byte N). The mixer and effect tracks are the start state's dump plus their
  rides.
- **Order within a tick.** ReBirth writes every tick-0 dump from the highest
  param down, the pattern select included (`..., 2, 1, 0` on all 64
  instrument tracks of the demo songs). The writer does the same for a loaded
  song, whose automation holds its tick-0 dump in the file's order; a song
  built in Reborn writes any tick-0 rides of its own first and the
  synthesised dump after them. The automation keeps its own order at equal
  ticks. A pattern select at a LATER tick is written after the other events
  at its tick: ReBirth writes those in the order they were recorded (of 715
  such ticks in the demo songs, the select is first in 302, last in 363 and
  between in 50), and the loader does not keep that position. So 21 of the 64
  demo instrument tracks re-save byte for byte, and the other 43 hold the
  same events at every tick in another order. The loader reads param 1 into
  the arrangement either way.
- **Length.** The loader ends the arrangement at the bar of the last event on
  the instrument and mixer tracks, so when nothing reaches the last bar the
  writer re-selects bass 1's pattern there, which is inaudible.
- **The drum accent row is a value.** A loaded step keeps the value it had
  (the 808 writes 1 and the 909 writes 2); a step accented in the grid takes
  its machine's.

Loaded, saved and reloaded, all sixteen demo songs decode to the same song
(`reborn_roundtrip`) and render to the same bytes. What the model does not
hold is not saved:

- Of the instrument headers' undecoded bytes, byte 0 is written as 1, and
  byte 1, the 808's byte 29 and the 909's byte 30 as zero. The 303 tune byte
  is written as its slot's canonical byte. Together these leave the 9-byte
  header different in 31 of the demo songs' 32 `303` chunks, so no `303`
  chunk pair re-saves byte for byte, although every pattern record does.
- A pan byte of 0 is written as 1 (`MIXR` and the mixer track). Both are hard
  left and render identically (`panByteFromPan`). One demo song has it in
  `MIXR` and two on the mixer track.
- The `USRI` author is read folded from Latin-1 to ASCII (accents dropped,
  `©` as `(c)`, line breaks and tabs as spaces, the ends trimmed) and written
  back folded. Eight demo songs' authors carry a `©`. The description is
  written back as the title in quotes, and HEAD is Reborn's own.
- `303` records: a note byte above 12 loads as 0, flag bits above `0x10` are
  dropped, a length outside 1..16 loads as 16, and a nonzero shuffle byte is
  written as 1. None of these occurs in the demo songs. Both octave bits
  together (`0x0C`, net 0) are kept (`BassStep::octBoth`).
- The position of a later-tick pattern select among its tick's other
  events, as above.

## 8. Related formats

- **`.rbm`**: "Mod" packages (skins + sound sets). Larger IFF/archive files
  (2–5 MB) containing graphics and sample/patch data. The loader works and the
  sample sets load; skin rendering is blocked on the sprites not being decodable
  JPEGs. See [FORMAT_RBM.md](FORMAT_RBM.md) for the format and
  [COMPONENT_MATRIX.md](COMPONENT_MATRIX.md) for status.
- **`.rbs`** default/silent songs ship with the original and are useful loader
  fixtures.
