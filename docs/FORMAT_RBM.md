# `.rbm` mod container, decoded (2026-07-25)

A "mod" is the original program's skin-plus-soundset package. Decoded by
inspecting the four mods shipped with the retail installer (MSM2, Metallicon,
Orbit2, PBE2); no program code was disassembled.

## Container

Plain EA-IFF-85, big-endian, same family as `.rbs`:

```
CAT (PRBM)
├─ HEAD  256 bytes    (fixed header)
├─ INFO  1280 bytes   (mod name, author, credits, contact; NUL-padded text)
└─ EMBF  x106         one embedded file each
```

All four mods carry **exactly 106 `EMBF` chunks**, and the count is identical
across mods, so the set is a fixed slot list rather than an arbitrary archive.

## `EMBF` payload

Each chunk begins with the original file name, followed by the file at exactly
its own length. **Only about the first 1024 bytes of each payload are the file
as written; the rest is scrambled** (measured 2026-10-04, below). This section
used to say "verbatim", which holds for the first kilobyte only. Two payload
families:

| family | count | what it is |
|---|--:|---|
| JPEG | ~60 | the skin graphics, named by numeric **resource id** (`12015.jpg` … `12571.jpg`) plus a `splash.jpg` |
| AIFF | 46 | the drum sample set (`TR808BD.aif`, `TR808N1.aif`, …): the same 46 voices the program keeps in its own resources |

The numeric names are resource ids, matching the id-numbered resource scheme
the program uses internally (its built-in drum samples sit at ids 1000–1045).
A mod therefore **overrides built-in resources by id**, which is why every mod
has the same 106 slots.

### `INFO` layout: two different names

`INFO` is 1280 bytes and carries **two** name strings, which is a trap:

| offset | field |
|---|---|
| +0 | free-text credits blurb (author, contact, copyright), NUL-terminated |
| **+1003** | **the canonical mod name a song references** |

Only `INFO+1003` is safe to match on. The blurb's first line disagrees for half
the retail mods:

| mod | blurb first line (+0) | canonical name (+1003) | songs ask for |
|---|---|---|---|
| MSM2 | `Massens MSM 2.0` | `Massen's MSM 2.0` | `Massen's MSM 2.0` |
| Orbit2 | `ReBirth Orbit 2.0` | `Orbit 2.0` | `Orbit 2.0` |
| Metallicon | `Metallicon+` | `Metallicon+` | `Metallicon+` |
| PBE2 | `Pitch Black Edition 2.0` | `Pitch Black Edition 2.0` | `Pitch Black Edition 2.0` |

Matching on the blurb would fail for MSM2 (a curly apostrophe) and Orbit2 (a
`ReBirth ` prefix). `readModInfo()` reads +1003 for exactly this reason.

## Past the first kilobyte, every embedded file is scrambled (2026-10-04)

Measured on all four installed mods (MSM2, Metallicon, Orbit2, PBE2) with
`tools/analyze/rbm_check.py`, from the files alone:

- Every embedded AIFF is stored at exactly its own FORM size (46 of 46 in each
  mod), so this is a same-length transformation, not compression.
- Read as the 16-bit big-endian PCM its header declares, every drum sample is a
  clean waveform at first and then turns to full-scale noise at a median of
  1022 to 1048 bytes from the start of its payload (the earliest onsets are
  naturally noisy sounds, where the detector fires sooner). An 808 bass drum,
  which barely crosses zero, crosses it on nearly every sample from there on.
- Byte entropy is 4.3 to 7.1 bits in the readable start and 7.98 to 7.99 bits
  in the rest.
- The skin JPEGs start with a valid header and almost none ends with the FFD9
  end-of-image marker (1 of 59 in PBE2, 0 in MSM2 and Orbit2, 8 in Metallicon).

The manual (p96-97) says mods are built by passing a folder of plain AIFF and
JPEG files, laid out like the stock "Standard Components", through Propellerhead's
ModPacker utility; it mentions no way back. ReBirth plays these samples as
drums: with a fixture naming `Massen's MSM 2.0`, ReBirth's 909 bass drum is a
kick (spectral centroid 73 Hz on a hit) where ours, reading the payload as
plain PCM, is noise (5.7 kHz). So ReBirth reverses the transformation and our
loader does not: **every mod's drum samples, and every mod skin past its first
kilobyte, decode to noise in Reborn.** `readModInfo()`, the mod NAME and the embedded
demo song's first kilobyte are unaffected. (Fixed 2026-10-06: the readers now
unscramble every mod first; see "Reading a scrambled mod" below.)

## The scrambling, measured (2026-10-06)

**Interoperability.** Reborn undoes ModPacker's scrambling so that a user can play
the mods they already have. No mod, no key and no decoded content is in this
repository; the tests build synthetic archives instead. The scrambling and the key
function were measured by packing content of our choosing with ModPacker and
reading its output; no program was disassembled. Whether reading mods this way is
permitted where you live is for you to judge.

Derived by black-box experiment only: Propellerhead's own ModPacker (the drag-and-drop
packer that ships with ReBirth, `<ReBirth>\ModPacker`) was run on folders whose contents
we chose, and its output compared byte for byte. No program was disassembled or traced,
and nothing came from any analysis of the program. Several dozen packs, notes in the
2026-10-06 session record.

- **The operation.** Number the payload bytes from the start of the file name. Bytes 0 to
  1023 are stored as written. From byte 1024 on, `stored = plain + key (mod 256)`, byte by
  byte: one plain byte changed changes exactly one stored byte.
- **The key.** For the EMBF entry at index `i` (0-based, in archive order), the key at
  payload byte `o` is `M[i % 2][(o - 1024) % (140 + 17 * i)]`: two master sequences per
  archive, one for even entries and one for odd, each entry repeating its own prefix.
  The last entry (105) reads the odd master up to byte 1924.
- **What sets the masters.** Nothing stored (`HEAD` is the same in every archive, and the
  pack is deterministic). `M[1]` depends only on `S1` and `M[0]` only on `S0`, where `S1`
  and `S0` are the sums, modulo 2^32, of big-endian 32-bit words over payload bytes 0 to
  511 and 512 to 1023 of every entry, words counted from the payload start (name
  included), an entry shorter than a kilobyte filled out with 0x65 (found 2026-10-11, see
  "The key function"). Shown by: compensating edits that leave the sums unchanged leave the masters
  unchanged; equal edits in different entries, at any name length, give equal masters;
  and an oracle folder that matches only Metallicon's two sums, built from 48 swapped-in
  stock files plus one compensating word, reproduces Metallicon's masters exactly (its
  four untouched stock files decode back to the stock bytes).
- **The function from a sum to its master** was not known when this was written; it was
  derived on 2026-10-11 (next section). The two clues recorded here follow from it. A
  single bit of `S0` changed almost every master byte, except bits 22 to 24, which changed
  about ten bytes each: those move the chunk counter by a multiple of 2^13, which leaves
  the seed as it was (0x220800 times 2^13 is a multiple of 2^24) and changes only the
  special words. Bits 11 to 14 gave the same masters as bits 27 to 30: both move the
  counter by amounts that come to the same seed and the same segment type.

**Decoded and verified (local only, never committed).** With ModPacker as the oracle
(a folder whose two sums equal the mod's, packed, gives the mod's masters), all four
installed mods decode in full: every JPEG decodes and ends at its end-of-image marker,
every embedded `Demo.rbs` parses, every AIFF is continuous. Played through our engine from
a sample folder, the decoded MSM2 909 kick reads -16.00 dBFS against ReBirth's -16.03,
centroid 110 against 111 Hz, band shape within 1.03 dB; its hats within 0.49 dB and
0.76 dB of band shape. The stock-sample pair sits at 0.01 and 0.86 dB on the same test.

## The key function (2026-10-11)

Derived the same way as the layout above, by black-box experiment only. ModPacker packed
folders whose content we chose, most of them one stock folder with one word in each half
of one AIFF's readable kilobyte rewritten so that the two sums hit chosen values, and each
pack's masters were read off
it against its known plaintext. No program was disassembled, traced or searched. 559
packs, 1118 master windows, 2,142,647 key bytes; the working notes and every master were
kept out of the repository, since they are ModPacker's output. The generator is `src/io/rbm_keygen.cpp` (`generateRbmKeys`,
`rbmKeySequence`, `rbmKeySums`).

- **The sums.** As above, with the rule for short entries: every entry contributes a
  1024-byte window, its payload and then `0x65` bytes past its end. `S1` is the sum of
  the window's big-endian words over bytes 0 to 511 and `S0` over 512 to 1023, modulo
  2^32, so an entry of 512 bytes or less adds an all-`0x65` half to `S0`. Measured with
  crafted short AIFFs (payloads of 266, 512, 514 and 990 to 1022 bytes) and on
  Metallicon's eight short JPEGs. Both sums lie in the readable part, so the stored
  archive gives them.
- **The counter.** For the even master `q = S0 + 0x3000` and for the odd `q = S1 + 0x2000`,
  modulo 2^32. The low 11 bits, `i0 = q & 2047`, are where the master starts within its
  first chunk. With `V = 4 (q >> 11)` that chunk's counter is `T0 = V + floor(V / 65535)`,
  and the next chunk's is `T0 + 4`, or `T0 + 5` when `(V mod 65535) + 4` reaches 65536.
  The 32-bit wrap is real: on the way it looked like a quadratic correction above two
  thresholds, which were where `S + Y` passes 2^32, and the quadratic was what a seed
  moved by a wrap does to the rand bytes. Away from the wrap the masters repeat in `S`
  with period 2048 times 65535.
- **A chunk.** 2048 bytes. Its rand bytes come from a 24-bit LCG on MSVC's `rand()`
  constants: seeded `x = 0x1742C6 + 0x220800 T` (mod 2^24), each byte steps
  `x = 214013 x + 2531011` (mod 2^24) and takes bits 16 to 23. A fixed set of 32-bit
  little-endian words is then written over them, chosen by the chunk's segment type
  `t = (T >> 16) & 3`: 20, 11, 16 and 5 words for types 0 to 3 (60, 38, 59 and 19 bytes).
  Some keep bits of their rand word; the rest of each word comes from a closed form in
  `w = (T & 0xFFFF) >> 2`: counters such as `3w + 0x15`, counters on masked or shifted `w`
  such as `0x33F3 + 13 (w & 15)`, scattered bits such as `(w & 0x123) << 5`, one quadratic
  (`0x78 - w^2`), and six that take another rand word of the same chunk, plus a constant,
  tripled or as low bits. The table in `src/io/rbm_keygen.cpp` is its only copy. Fitted to
  9723 observed words with none wrong; between them they cover exactly the key bytes that
  are not rand bytes.
- **The key.** A master is the chunk bytes from `i0` of chunk `T0` on, into the next
  chunk, each negated: `key = -raw` (mod 256), added to the plain byte.
- **ModName.txt.** These constants hold for a folder holding a `ModName.txt`, whatever it
  says; no other text file affects the keys. Without one ModPacker writes a malformed
  archive, its top-level `CAT ` length 0 and about 1.3 KB of header missing, under other
  constants. Every retail mod has a name. The reader does not model the malformed kind,
  so one falls through to the estimate below.

Checked, byte for byte: all 1118 master windows of the lab packs; the four retail mods
from the archive alone, every key byte of both masters and all 424 embedded files
identical to the oracle decodes; and twelve partial packs of random size and content,
crafted short AIFFs among them. Then through the C++ reader (`reborn_rbmkeys --truth`,
2026-10-11): the four retail mods, every key byte, no decoded byte different, the keys
generated in under 0.2 ms each; and a fresh set of twelve partial packs, one file to all
106, every key byte exact and every one read with generated keys, the four with one to
five voices included, also under the safeguards the adversarial reviews added (next
section).

**Open.** The step between chunks is measured for the first two only, which hold 2049 to
4096 key bytes; ModPacker writes at most 106 files and so no key past byte 1924, so a
third chunk cannot occur in an archive it made, and past two the generator assumes the
same step. The malformed no-name archive is not modelled. And generated keys can be
close to right without being right: sums that differ by a small multiple of 2^22 usually
give keys that differ in 11 to 20 bytes, so an archive edited in its readable part after
packing can decode nearly right, with errors the self-check cannot see (found by the
adversarial reviews of 2026-10-11).

## Reading a scrambled mod: generated keys, then the estimate (2026-10-06, 2026-10-11)

`readModSamples` and `readModSprites` undo the scrambling before they read anything
(`unscrambleRbm` in `src/io/rbm_unscramble.cpp`, called through `readModImage` in
`src/io/rbm.cpp`), and leave the file as stored when they refuse it. Since 2026-10-11
they try the generated keys first. Generating takes under 0.2 ms; a whole first read of a
retail mod takes 43 to 74 ms on the reference machine's 24 cores and 53 to 125 ms pinned to
4. The self-check judges its voices in parallel (since 2026-10-12) and gives the same
verdict, to the last bit, as judging them one at a time, which took 0.44 to 1.09 s. The
generated keys are tried on an archive whose top-level `CAT ` spans the file, as every
archive ModPacker writes from a folder with a `ModName.txt` does (without one it writes 0
there, under other keys), and within the limits given below. They are taken when the decode passes the self-check without its
evidence minimum, leaves every entry where it was, and is confirmed on each parity that
holds audio.

The minimum is replaced, not dropped, and the replacement works parity by parity. Two
adversarial reviews on 2026-10-11 showed why both matter. The check is powerless on loud
noise: decoded under any wrong key, a hi-hat, cymbal or noise voice is noise as plausible
as the right decode, and the check passed both (a single white-noise voice under every
wrong key tried). And the two sums set the two keys independently, so an archive edited in
one half of an entry's readable kilobyte after packing has generated keys right on one
parity and unrelated on the other; evidence on the right parity then passed the whole
archive whenever the wrong parity held nothing the check could judge (every such archive
tried, each refused by the reader before). So each parity that holds scrambled audio (an
AIFF or AIFC) is confirmed on its own, in one of three ways, tried in order.

- **The wrap test's chances.** Under an unrelated key every sample past the readable part
  decodes to a uniform value, two in a row lie more than three quarters of scale apart one
  time in 16, and in a voice whose readable steps all stay under half scale that is a wrap
  the check refuses. A parity with 280 such chances on one chain (per channel, at most one
  key period of frames; the longest chain only, since a parity's voices share its key bytes
  and their chances are not independent) lets a wrong key through less than once in a
  million, allowing 1 in 20 per step for the correlation between steps. Every parity of the
  four retail mods has 937 to 962.
- **How far the audio is from uniform** (since 2026-10-23). Under an unrelated key three
  quarters of the samples past the readable part lie at or above a quarter of full scale,
  half at or above half, and a quarter at or above three quarters; a drum sound, a hi-hat,
  a cymbal and a noise snare included, decays and keeps most of its samples well under. The
  samples under one key position share a draw (their high bytes are offset by the same key
  byte), so they are pooled by position and each position counts as one Bernoulli draw
  weighted by its samples, a model that can only overstate the odds; the exact weighted
  Chernoff bound on the lower tail at each level, the smallest times three, must come under
  one in a million. This is what reads a partial mod made only of hats, cymbals and noise
  snares, where the check passes a wrong key as readily as the right one. Noise spread
  evenly up to full scale looks exactly like a wrong key and is still refused.
- **Decoys.** A parity short of both is decoded with 64 key pairs that replace its key with
  unrelated ones, the first no key at all, and the check must refuse every one. The decoys
  are fixed, so they show that the check's chance of passing a wrong key on that parity is
  small, not how small: on a lone silent or near-silent voice that clears them it passes 1.4
  to 1.8 percent of wrong keys.

Confirmation shows that a parity's key is not unrelated to the right one. It cannot show
that a key is not CLOSE to the right one, and an archive edited in its readable part after
packing can give exactly that. An edit moves a sum, and the keys of the new sum are
unrelated to the old ones unless the move is a multiple of 2^22 that leaves the generator's
chunk counter the same modulo 2^13: the rand bytes then stay as they were, only the special
words move, and the two keys differ in 1 to 20 bytes. A one-byte edit at any word-aligned
offset moves a sum by a multiple of 2^24, so this is not rare among edited archives. A key
byte that differs adds one constant to every byte decoded under it. So each confirmed
parity's key is also set against the keys of its near neighbours, sum + m 2^22 for m from
-31 to 31, kept when they differ in 1 to 64 bytes (typically 7 per parity): on the
positions where the two differ, one decode displaces every sample whose high byte falls
there and the other leaves the voice as it was. A displaced sample stands out from its
neighbours: its prediction residual (against the mean of the two either side), scaled by the
local residual level, is lifted by the offset. Summed over the samples on the differing
positions, the squared scaled residual under the generated key less the same under the near
one is negative under a right generated key, near zero where the offset is small against
the local level, and grows with every sample the offset lifts above it when the generated
key is the displaced one, as it does in the quiet tail of a decaying sound; over 50 refuses
the generated key (a right key gets there by chance at about six standard deviations). So
do three or more samples beyond the loudest of the voice's other samples that the near key
brings back inside that range, which is how a mid-sized offset shows in loud, steady noise
(`nearMiss` in the report). What passes is an offset small against the local level or on
low bytes only: on edited [tone, capped noise] archives 8 wrong decodes in 200 passed
(from 29), each 32 to 57 dB under the voice and inside its range. The test adds 7 to 11 ms
to a first read.

A parity of pictures and text alone is judged by the check alone, as an estimate always
was. If a parity cannot be confirmed, or a near neighbour decodes it better, the generated
keys are not taken: with 1 to 11 voices the archive is refused as it stands, as an
estimate would be under the minimum, and otherwise its keys are estimated. Keys that fail
mean an archive they do not fit (one ModPacker did not make, or one edited after packing),
or one whose content the check cannot judge even under exact keys; the keys are then
estimated, under the full check. `reborn_rbmkeys --estimate` skips the generated keys.

The estimate, which is now that fallback, was the reader's only way from 2026-10-06 until
the key function was found: Reborn cannot run ModPacker when a user loads a mod, so the
two masters were estimated from the archive itself. The estimate uses the stored content
and nothing else, four kinds of evidence:

- **Zeros.** Silence and zero runs make plain 0 the most common byte at most key
  positions, so each key byte starts as the most common stored byte at its position.
- **Not the JPEG end markers.** Every JPEG in the retail mods ends `FF D9`, which would
  pin two key bytes each, but the audio alone gives the same keys on all four mods, a
  file may carry bytes after its end marker (a version that pinned them decoded PBE2
  with two zeros appended to each JPEG 67,954 bytes wrong), and an end marker the
  estimate does not force stays free to confirm it.
- **Predictable audio.** Every channel of every 16-bit AIFF is scored by an order-4
  linear predictor fitted to it, each residual taken against the running median of its
  65 neighbours, and each key byte is moved to make the decoded audio most predictable,
  singly over all 256 values. Every other key position is the same byte of consecutive
  samples in every voice, so a run of samples offset alike costs only at its two ends
  and no single-byte move can undo it; a beam search along that chain of positions finds
  the joint move, over shifts of up to two steps per link, in steps of 16, then 4, then 1.
- **Twins.** Mods store some voices twice, or once scaled: all four retail mods do,
  mostly their snare-noise and 808 noise voices, which sit at the top of the archive
  where little else reaches the key. A pair whose readable samples agree to rounding
  after one gain, and whose whole decodes still agree on 80 percent of samples once the
  rest of the key has settled, is tied together, and each copy then pins the other. A
  proposal that snaps a sample to its twins' consensus repairs the one error nothing
  else reaches: a high byte one out with its low byte compensating.

Checked against the oracle masters above with `reborn_rbmkeys <mod> --truth <masters>`.
The work spreads over up to 16 cores and gives the same keys on any number; times are
one estimate, on all 24 cores of the reference machine and pinned to 4 of them
(measured 2026-10-09):

| mod | key bytes exact | decoded bytes wrong | 24 cores | 4 cores |
|---|---|---|---|---|
| MSM2 | 3833 of 3833 | 0 of 2,183,516 | 2.7 s | 4.3 s |
| PBE2 | 3833 of 3833 | 0 of 3,623,024 | 3.0 s | 6.2 s |
| Orbit2 | 3833 of 3833 | 0 of 5,063,584 | 4.5 s | 8.9 s |
| Metallicon | 3808 of 3833 | 319 of 3,532,098 | 3.8 s | 8.0 s |

Metallicon's 25 misses all sit at the top of its keys: even positions 1872 to 1905 and
odd 1904 and 1906. Even positions from 1874 up are reached by `TR909SDN2` alone, twelve
samples per key byte and mostly low bytes of loud noise, and unlike the other three mods
its three snare-noise voices are independent noise, not twins, so nothing else in the
archive constrains those bytes. The cost is confined to three voices: `TR909SDN2` decodes
with its error 39.1 dB below its signal, `TR909SDN3` 48.5 dB, and `TR909SD5` has 19
samples one LSB out (99.9 dB). Every other voice and every JPEG in all four mods decodes
exactly. Played through the reader, the MSM2 kick and hat fixtures of 2026-10-04
(`fixtures-2026-10-04/mod-check`) match ReBirth's exports to +0.03 and -0.49 dB, within
0.27 and 0.76 dB of octave shape, and are sample-identical to renders made from the
oracle-decoded samples.

**The self-check** (`checkUnscrambledRbm`) has six parts, each aimed at a way a key can
be wrong:

- **JPEG scans.** An unscrambled entropy-coded scan holds `FF` only as stuffing `FF 00`,
  a restart marker or a marker, so past the readable part, up to its end marker, it must
  hold no `FF` followed by 01 to BF; a wrong key byte that decodes to `FF` breaks that
  three times in four. Bytes a writer leaves after the end marker are not judged. At
  least 90 percent of the judged JPEGs must be clean. The retail mods' JPEGs are all
  early in the archive, so this sees only the lower part of the keys.
- **Key positions by level, for keys wrong in part.** A wrong high byte puts a
  full-scale value at the same key position in every period of every voice it reaches.
  Each sample past the readable part is set against the running median of its 65
  neighbours' sizes, plus a floor of 16 LSB or the smallest step the readable samples
  use if that is larger (256 for audio that came from 8 bits), and filed under the key
  position of its most significant byte. A position whose lower median is above 4 is
  bad. This finds ruined noise voices, where predictability cannot: randomising only the
  top 2 percent of MSM2's key ruins four snare-noise voices (as bad as noise) and leaves
  every predictability measure where it was.
- **Key positions by residual.** A run of key bytes offset alike moves its samples
  together, so none stands out by level, but every period leaves a step at both ends of
  the run. Each sample's residual under an order-4 predictor fitted to its voice's
  readable samples alone, against the running median of its neighbours' residuals plus 4
  LSB, is filed the same way, and a position whose lower median is above 3.5 is bad. Two
  bad positions of either kind and the key is refused. Its limit is the window: a run
  longer than about 32 samples moves the running median with it, so unless the predictor
  holds DC well the run is lost. The synthetic test's run of 20 samples offset by 66
  leaves 43 bad positions; the same offset over 100 samples leaves none, and is refused
  only by the JPEG scans it breaks and the samples it wraps.
- **Wraps.** A high byte one out over most of a class of key positions (every other
  position, the same byte of every sample) shifts the voices it reaches by 256. No
  position test sees that, and the reader removes each voice's mean anyway, but a sample
  near one end of the range wraps to the other, which is a loud click. A voice whose
  readable steps from sample to sample all stay under half scale, and that then steps
  more than three quarters of scale past the readable part, has wrapped, and the key is
  refused. Under exact keys the largest such step anywhere in the four retail mods is
  37,215; the two voices that already step further inside their readable part (one in
  MSM2 at 49,227, one in Metallicon at 54,589) are exempt.
- **Voices, for keys wrong everywhere.** With every byte wrong there is no right
  neighbour to compare with, but each voice, predicted by an order-4 predictor fitted to
  its readable samples alone, becomes far harder to predict past the readable part. The
  median over voices of the mean absolute residual past it, against inside it, must stay
  under 4. Voices whose readable start averages under 64 LSB are left out: a quiet start
  cannot anchor a ratio, and counting them refused right keys.
- **Evidence.** A scrambled archive with nothing judged is refused, so is one holding a
  voice the check cannot read (more than two channels), and so is one whose keys were
  estimated with fewer than 12 voice channels reaching past the readable part, whatever
  its tests say. Generated keys are judged without that minimum, and in its place each
  parity that holds audio is confirmed on its own (above).

Calibrated with `reborn_rbmkeys <mod> --keys <keys>` and `--raw` on the four mods
(2026-10-09). Under exact keys and under the estimates: every JPEG scan clean, no
position above 2.72 by level or 2.89 by residual, no voice wrapped, ratio 0.27 to 0.36.
Under keys right except in their top 2, 5 or 20 percent or at 100 scattered bytes: 70 to
796 bad positions and 1 to 28 voices wrapped each. Under keys right except for a run of
200 positions with every other byte offset by 1, 66 or 255: 6 to 21 bad positions, but
PBE2 passes at 1 and at 255, which move its samples by 1 or by 256 under loud audio. Under
no key or random keys: at most 10 of 51 scans clean, the ratio 29 to 92, and 39 to 45
voices wrapped. All four estimates pass and 34 of the 36 wrong keys fail. What the check
cannot see is a low byte, or a high byte where no sample is loud enough to wrap, that is
wrong by a little: those change a sample by a few LSB, or by 256 under thousands, which
is also what costs least to the ear (Metallicon's 319 wrong bytes are of this kind). The
first version of the check could not fail: it tested JPEG end markers the estimate itself
forced and a scale-normalised roughness that scored noise like audio; an adversarial
review fed it random bytes and it passed. Three reviews in all found ways past it (keys
wrong in part, runs offset alike, too little evidence, wraps), each closed before commit.
When the check fails, the readers return nothing and say why, so the caller plays without
the mod rather than playing noise.

**Partial mods.** A mod need not carry every file. The manual's recipe copies
ModPacker's `Standard Components` folder (110 files: 46 sounds, 59 pictures, a song
and four text files) and replaces files in the copy, which gives a whole archive;
but an author may delete files instead, and anything missing plays as the standard
component (manual p97). How far the estimate can be trusted depends on the audio
the archive holds, so it was measured on archives built from the oracle-decoded
files of the four retail mods (local only, never committed), a random subset of the
voices in each with no pictures, or the whole mod, scrambled under random keys
(2026-10-09). The wrap test and the 12-voice minimum were chosen on seeds 31 to 33,
so only seeds 34 and 35 are out of sample, and their figures lead.

Out of sample. Whole mods (all 106 files): 8 of 8 pass, 6 exact, the worst voice
39.5 dB clean (Metallicon); the 8 in-sample ones did as well. 12, 24 and 46 voices,
24 archives: 15 pass and 9 are refused. Every refused one has a voice 29.1 dB clean
or worse, but so do 3 of the 15 that pass: Metallicon with 24 voices at 27.4 dB,
MSM2 with all 46 at 24.0, and MSM2 with 12 at 19.6. In the 12-voice one every wrong
key byte is in the top of its keys, which only the last one to three files in the
archive reach, and here those were two snares: offsets of a few steps on high
bytes, 624 and 444 samples off by more than 1000 inside loud noise, which no test
sees. The in-sample seeds had read "every passing archive of 12 voices or more
decodes 30 dB clean"; seeds 34 and 35 disprove it.

Below the minimum (the check's verdict with only the minimum dropped, seeds 33 to
35, 52 archives): with 3 voices 5 of 12 pass and 4 of those 5 have a voice under
30 dB (27.5, 24.2, 13.7 and 5.2); with 6 to 11 voices 20 of 40 pass, all 26.4 dB clean or better, and 6
of the 20 refused were 30 dB clean or better. So the minimum refuses 6 to 11 voice
mods that the other tests judge as well as larger ones, and does not make 12 to 46
voice mods safe. What matters is how well the last few files pin the top of each
key, which the count of voices follows only roughly. A mod whose keys must be
estimated and that has fewer than 12 voices is refused, and its drums stay as Reborn
had them (its synthesis, or the user's sample folder), with the reason in the note.

All of this now applies to archives whose keys must be estimated. A partial mod that
ModPacker packed is read exactly with generated keys whenever each parity that holds
audio can be confirmed: the fresh set of twelve partial packs above decoded byte for byte,
the four with one to five voices included, which the minimum would have refused (BACKLOG
17). Of the 184 voices of the four retail mods, each alone in an archive under its own
generated keys, 180 are read exactly (172 before the uniformity route of 2026-10-23: the
eight it added are hats, cymbals and noise snares). Four fail the check even under their
exact keys (their readable start is far more predictable than the rest: two startup
chimes and two quiet-start voices) and are refused. Under 1472 unrelated keys across
those single voices, and 60 across the four whole mods rebuilt, none was taken. Synthetic sweeps
through the whole reader (2026-10-11, 3600 archives of one to five voices under random
keys, no key, or another sum's generated keys) took a wrong key once, a lone hi-hat under
another sum's keys. Under its own generated keys a voice of 8-bit noise was refused 20
times in 20 and one of dithered near silence 14 times in 20; one archive of exact silence
was read. A partial mod built from retail voices reads in under 0.01 s to 0.83 s, the
long end when a parity needs its decoys. An archive with no audio past its readable part, and
a parity of pictures and text alone, are judged by the check alone, as an estimate always
was: a lone JPEG whose scan reaches only a few hundred bytes past the readable part can
pass under wrong keys (half the time at 200 bytes, nearly always at 20), and a song on a
parity of its own decodes wrong if that parity's half of the readable part was edited
after packing. That costs pictures and songs only, which the app does not load.

When the keys must be estimated, the first read of the mod in a process pays for it; the
keys are cached by the file's content for the life of the process, refusals included. An archive over 32 MB,
with over 1024 entries, or holding over 8 million audio samples is refused unread (the
estimate needs about 90 bytes per audio sample; ModPacker's hold 106 files and at most
2.3 million samples), and the walk over its chunks stops 16 containers deep. Tests build
a synthetic archive in the test itself (`test_rbm_unscramble_synthetic`, and
`test_rbm_keygen` scrambled with its own generated keys), so no mod content and no mod's
key enters the repository; `test_rbm_keygen` pins the generator with hashes of the Python
reference's output for sums chosen to reach every segment type and both steps, not for
any mod.

## Mods also carry a song

Every retail mod embeds a `Demo.rbs` alongside its graphics and audio, and that
demo's own `GLOB+15` names the mod it ships with, confirming the reference
convention from both directions.

## Skin graphics are FIXED-PIXEL sprites

This is the fact that drives [SKINNING.md](SKINNING.md). Measured sizes from
MSM2:

| id | size | role (inferred from dimensions) |
|---|---|---|
| `12500`, `12520`, `12564` | 366 × 115 | device panel backgrounds (three of them) |
| `12507` | 1674 × 30 | wide film-strip (many frames side by side) |
| `12557` | 15 × 792 | tall film-strip (frames stacked) |
| `12554` | 68 × 504 | tall film-strip |
| `12552` | 608 × 19 | full-width strip |
| `12521`, `12522` | 504 × 13 | full-width strips |
| `12559`–`12563` | 67 × 25 | button states (5 variants) |
| `12062` | 16 × 510 | tall strip |
| `splash` | 256 × 256 | splash image |

Nothing is vector and nothing is 9-slice: every widget is a pixel-exact bitmap,
and animated controls (knobs, LEDs, meters) are **film-strips**: N frames in
one image, indexed by value. The widest full-width strips (608 px) put the
classic window interior at roughly **640 px wide**, i.e. a late-1990s
fixed 640 × 480-class layout.

## Consequences

1. A classic skin cannot reflow. Its layout is baked into the bitmaps, so any
   modern-resolution support is a **scaling** problem, not a layout problem.
2. Film-strip frame counts must be derived per sprite (strip length ÷ frame
   size); the frame size is not stored in the container.
3. Mods are **third-party artwork**. They are not ours to redistribute; the
   engine must load them from the user's own copy, exactly as it already does
   for the drum samples. See [PROVENANCE.md](PROVENANCE.md).
