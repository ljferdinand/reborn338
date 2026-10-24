# External code review, branch `pcf-supertask`, 2026-08-31

Reviewer: C8.31.2026a, working from a zip of the branch in a sandbox with no
repo access and no ReBirth references. Written to be handed to the cousin who
has the repo, so every item carries its file and line, how it was established,
and what to do about it.

Method. Read the docs in the order `CLAUDE.md` points to, then built
`reborn_core` with gcc 13 and ran the suite: 39 tests, all pass. Where a claim
was checkable I wrote a harness and checked it. Six items below are measured or
sanitizer-proven rather than inspected, and each says which.

What I could not do, so treat as unreviewed. No git history, so I cannot see
what the 58 commits changed or read commit messages. No ReBirth exports and no
fixture workspace, so nothing here is a parity measurement; every audio number
is our engine against itself. No cmake in the sandbox, so the build items are
gcc-only evidence.

The headline. The PCF DSP itself reads as careful measured work and the preset
table is internally clean. The defects cluster at the boundaries: state handling
around bypass, a name that means its own opposite, three parser bugs, and the
vendored Open303 build. One item is a parity defect I would want under Luke's
ear before supertask 4 is signed off.

---

## Severity 1

### 1. The PCF envelope freezes while the unit is out of the path

Measured, 93.5 dB.

`env_` is decayed only inside `process()` (`pcf.cpp:289`), but `onStep()`
retriggers it unconditionally (`pcf.cpp:226`). The engine calls `onStep` for the
PCF on every sixteenth regardless of routing (`engine.cpp:763`) and calls
`process` only when the section is routed and the unit is on
(`engine.cpp:816`). So while the PCF is out of the path, `fireTick` keeps
assigning `env_ = velocity` and nothing ever decays it. `env_` ends up pinned at
the velocity of the most recent sixteenth-aligned trigger and stays there.

Harness: Freq 0, Amt 127, Decay 64 (tau 88.6 ms), preset 0, 2 kHz probe at
amplitude 0.5, read over the 5 ms after switch-in, 0.69 s after the pattern's
last trigger, which is 7.8 time constants.

| | peak | dB |
|---|---|---|
| PCF in the path throughout | 0.000016 | -89.6 |
| bypassed for the same span, then switched in | 0.779174 | +3.9 |

The filter comes back wide open where it should be shut, and stays wrong until
the next pattern trigger. For a sparse preset that is up to a whole note, two
seconds at 120 BPM.

Why it matters here specifically. This lands on exactly the songs supertask 4
was reopened over. G-Spot moves the PCF's routing thirty-five times, twelve of
the sixteen demo songs ride it, and three ride the unit's own bypass. It is the
mirror image of what Luke heard on 2026-08-28: not our filter running where
ReBirth's does not, but our filter thrown wide open where ReBirth's would be
closing. It will read as a bright blurt on re-entry rather than as dull drums,
which is why the 8/29 fix would not have surfaced it.

Remediation. Advance the envelope on a clock that runs whether or not audio is
flowing. Cheapest correct version: in `onStep`, before `fireTick`, decay `env_`
by `exp(-stepSeconds / tau)` for the sixteenth just elapsed. `tempo_` and the
decay knob are both already in hand. The tidier version is a `virtual void
tick(int frames)` on `Effect` that the engine calls for the PCF unconditionally,
leaving `process()` for audio only, which also fixes the in-between
thirty-second tick never firing while bypassed.

Fixture that would catch a regression: a song that routes the PCF to a section,
takes it away for several whole notes at a long decay, and puts it back. That
fixture does not exist in the set.

### 2. Three parser bugs, all proven under AddressSanitizer, all one line

Built with `-fsanitize=address,undefined` and driven with crafted files. All
three are reachable from the plugin, and `.rbm` mods and `.rbs` songs are
precisely the files a user is handed by someone else.

**2a. `rbm.cpp:69`, SEGV.** `dataOffset` is read from the SSND chunk with no
check that `8 + dataOffset <= len`. Exceed it and `ssnd` points past the buffer
while `ssndLen = len - 8 - dataOffset` underflows to a huge `size_t`, so `avail`
goes enormous, `count` becomes `frames` (up to 2^32), and the decode loop reads
wherever it likes.

    AddressSanitizer: SEGV on unknown address 0x5190ffff04e8
      #0 decodeAiff src/io/rbm.cpp:92
      #1 take src/io/rbm.cpp:282
      #2 walk src/io/rbm.cpp:267
      #3 reborn::readModSamples src/io/rbm.cpp:291

From a 1132-byte file. This is a hard crash, not just an over-read.

**2b. `rbs.cpp:260`, unbounded allocation.** `parseTrak` does
`out.reserve(count)` with `count` straight from the TRAK header, before any
check that the payload could hold that many events.

    AddressSanitizer: out of memory: allocator is trying to allocate
      0x7ffffff80 bytes
      #5 parseTrak src/io/rbs.cpp:260
      #6 reborn::loadRbs src/io/rbs.cpp:565

34 GB requested from a 106-byte song file.

**2c. `wav.cpp:95`, heap over-read.** The RIFF walk guards only
`pos + 8 <= b.size()`, then the `fmt ` branch reads out to `pos + 23`.

    AddressSanitizer: heap-buffer-overflow, READ of size 1
      #0 operator() src/io/wav.cpp:89
      #1 reborn::readWav16Mono src/io/wav.cpp:98
    3 bytes after 44-byte region

Note that the `sz < 44` floor at `wav.cpp:81` hides this from a naive test file;
the trigger needs a file at or above 44 bytes with a `fmt ` header in its last
eight. My first attempt was 36 bytes and was rejected before parsing, which is
worth knowing if you write the regression test.

Remediation. Patch attached separately as `security-fixes.patch`, three hunks.
`rbm.cpp` and `rbsx.cpp` already do the bounds check that `wav.cpp` is missing,
so 2c is bringing one file up to the house standard rather than inventing a
rule. I applied all three, re-ran the sanitizer on the same three inputs (clean,
no findings, all three files parse to a sane empty result), and re-ran the test
suite (39 tests, all pass).

Suggested regression tests: three in-memory buffers in `test_main.cpp` asserting
that each parser returns empty or false rather than crashing.
`loadRbs(const unsigned char*, size_t, ...)` already takes a buffer, so 2b needs
no file on disk. Worth considering whether a small corpus fuzzer belongs in CI;
the parsers are the only part of this codebase that eats hostile input, and they
are also the only part with no owner in `BACKLOG.md`.

---

## Severity 2

### 3. `pcfBandpass` means the opposite of its name, and two comments state the retired polarity

`pattern.h:282` says `// mode byte: 0 = LP, 1 = BP`. `rbs.cpp:535` says
`[mode 0=LP/1=BP]`. `CLAUDE.md`, `PCF_FINDINGS.md` section 3 and the comment at
`engine.cpp:221` all say the measured answer, which is that byte 0 is BANDPASS
and byte 1 is LOWPASS.

The audio is correct today. Verified end to end rather than trusting either
side, by loading a Song with each byte value and both reading back the internal
param and measuring the resulting shape on white noise:

| chunk byte 6 | `pcfBandpass` | internal `mode` param | high/low | shape |
|---|---|---|---|---|
| 0 | false | 1.0 | -3.1 dB | bandpass |
| 1 | true | 0.0 | -11.4 dB | lowpass |

It is right because two inversions cancel. The loader stores the raw byte under
a name asserting the opposite (`rbs.cpp:546`), and `engine.cpp:222` inverts
again on the way in. The round-trip test passes because `rbs_write.cpp:284`
mirrors the loader, so nothing in the suite can see it, and
`test_main.cpp:424` asserts `pcfBandpass` is true for a value that means
lowpass.

This is the exact hazard `CLAUDE.md` opens by warning about: something that
looks like a bug, is not, and is cheap to break by accident. Anyone who
"corrects" `pattern.h:282` to agree with its own name flips the polarity of the
mode switch in the two shipped songs that use bandpass, and the only thing that
would catch it is an ear.

Remediation. Rename the field to `pcfModeByte`, or to `pcfLowpass` with the
sense corrected at `rbs.cpp:546`. Fix both comments. Put the one-line reason for
the inversion at `engine.cpp:222` and at `engine.cpp:534`, where
`e.value ? 0.0f : 1.0f` currently looks like a bug on sight and is correct. Add
a test asserting byte 0 gives a bandpass shape; my harness is about thirty lines
and is reproduced at the end of this document.

### 4. Resonance is de-zippered in the damping but not in the gain or the corner

`kSm_` is smoothed per sample (`pcf.cpp:271`). `gainFor(p_[C_RES])` at
`pcf.cpp:278` and the `fcScale` at `pcf.cpp:295` both read the raw knob. One
automation event therefore steps the input gain by up to 8.48 dB and the corner
by up to 0.25 octaves within a single sample.

How much survives depends on Freq, because at a low corner the filter smooths
its own discontinuity. Measured, one Q event from 0 to 127 on a 300 Hz tone at
amplitude 0.3, as departure from smooth continuation:

| Freq knob | fc | step, relative to signal |
|---|---|---|
| 0.25 | 57 Hz | -78 dB |
| 0.50 | 362 Hz | -47 dB |
| 0.75 | 2.3 kHz | -30 dB |
| 0.90 | 6.9 kHz | -14 dB |
| 1.00 | 14.5 kHz | -4.1 dB |

Negligible at low Freq, a plain click at high. Mutilated Species II carries 1242
events on its Q alone. I could not check what Freq that song sits at, so I am
not claiming this is audible in a shipped song, only that the mechanism is real
and the fix is cheap.

The argument for fixing it rather than filing it: `distortion.cpp` in the same
directory already does this correctly. It smooths every derived quantity per
sample (`distortion.cpp:193` to `198`) including its own output gain, which is
the direct analogue of `gIn`, and it carries a `snap_` flag so `reset()` lands
the smoothers on their targets instead of ramping from stale ones
(`distortion.cpp:100`, `183`). The PCF's partial smoothing looks like an
oversight against its sibling rather than a design choice, and there is a
pattern in the repo to copy.

Remediation. Smooth one normalised `rSm_` and derive `k`, `fcScale` and `gIn`
from it. That subsumes item 5. Precompute `kResGainDb` as linear gains at
compile time so the `std::pow` disappears entirely.

### 5. `gainFor()` runs a `std::pow` and a nine-entry table scan per sample

`pcf.cpp:278` sits inside the sample loop and its argument is invariant across
the block. Hoisting is correct today. Folding it into `rSm_` per item 4 is
better and costs nothing extra.

### 6. `setParamByName` allocates on the audio thread

`Processor::paramIndex` (`processor.h:44`) walks every parameter and constructs a
`ParamInfo` per step, and `ParamInfo` holds four `std::string`s
(`processor.h:22`). One `"mode"` lookup on the PCF constructs up to 28 strings.
`dispatchAutomation` runs from `Engine::process`, so every automation event heap
allocates on the audio thread.

Harmless in `reborn_render`. A dropout risk in the VST3 and CLAP builds, and the
PCF is the heaviest user of this path in the repo: `engine.cpp:527` to `534` is
six name lookups' worth of surface, dispatched as often as the song rides the
unit.

Remediation. Resolve names to indices once at load and cache them, or add a
`const ParamInfo& paramInfoRef(int)` over a static table and leave the
string-returning version for the UI. Architectural rather than PCF-specific, so
it may belong in `BACKLOG.md` alongside entry 6, which is the other
`kParams`-shaped hazard.

### 7. `scratch_.resize()` on the audio thread in the plugin

`app/src/PluginProcessor.cpp:393` grows the scratch block inside `processBlock`
if the host hands over more samples than `prepareToPlay` was told to expect.
`StereoBlock::resize` calls `left.assign(n, 0.0f)` (`audio.h:26`), which
reallocates. JUCE's `samplesPerBlock` is a hint rather than a hard ceiling, so
this path is reachable, and being the rare belt-and-braces branch is exactly why
it would bite in the field rather than in testing.

`Engine::process` has the same shape at `engine.cpp:857` and `859`, so the
allocation can happen twice.

Remediation. Reserve generously in `prepareToPlay` (twice the requested block is
the usual choice) and then either clamp `numSamples` or loop over the prepared
size, so the audio thread never allocates. The try-lock and
`ScopedNoDenormals` around it are both right, so this is the one gap in an
otherwise careful `processBlock`.

### 8. No C++ test covers any measured PCF law

The suite covers PCF routing exclusivity (`test_main.cpp:295` to `301`),
routing automation (`337` to `353`) and chunk round-trip (`410` to `442`). It
does not cover the decay quantisation, the mode polarity, the free-running
clock, the no-trigger-on-first-tick rule, or the preset table's shape. Each of
those cost fixture rounds to establish and each is a one-line edit away from
being lost. `regress_ab.py` cannot catch any of them: it needs fixtures and
references, and it scores spectra.

All five are testable with no ReBirth reference and no audio fixture:

- `decayTau`: bytes 48 to 55 all return 88.6 ms, steps land on every multiple
  of eight, and the sixteen rates are the 1-2-3-4-5-7 series. This is the
  finding that took rounds 7 and 8 and it is a pure function of one float.
- Mode byte 0 produces a bandpass shape. Item 3's harness, as is.
- `onStep(16)` does not restart a length-18 preset. Pattern 51 is the case, and
  this is the bug that made twenty of the fifty-five presets play wrong.
- The first `onStep` of a render fires nothing.
- The table invariants in item 9.

This is the highest-value item in the report per hour spent, because it converts
findings that currently live only in prose into things the build enforces.

---

## Severity 3

### 9. The preset table is clean, and nothing stops it drifting

I validated all fifty-five programmatically. `count` matches every array length,
no `step32` reaches its own `length32`, no duplicate positions within a pattern,
all ascending, and fourteen distinct non-divisor lengths as documented (6, 10,
11, 14, 18, 20, 24, 28, 38, 54, 58, 60, 64, 128). No defect.

But `count` is a hand-written integer sitting beside a separate array in a
generated file, and `fireTick` trusts it to index (`pcf.cpp:362`). A
regeneration bug becomes an out-of-bounds read rather than a compile error.

Remediation. Emit the rows with `std::size(kPcfPatNN)` in the `count` slot, or
add a `static_assert` per row. The validation script is fifteen lines of Python
and could also live in CI next to the round-trip check.

### 10. `pcfPat_` is written in five places and read in none

`engine.cpp` lines 101, 115, 226, 270 and 347 all assign it. Nothing reads it.
`Engine::selectPcfPattern()` is public API (`engine.h:55`) and does nothing at
all. `SongStep::pcfPattern` (`pattern.h:110`) is never populated by the loader
either. The pattern reaches the unit only through `engine.cpp:223` at load and
automation param 4 at `engine.cpp:531`.

Dead scaffolding from the pre-supertask design, when PCF patterns were assumed
to be per-song-row like device patterns. Delete all three, or wire
`selectPcfPattern` to `pcf_->setParamByName("pattern", ...)` if the UI is meant
to call it.

### 11. Build: vendored Open303 does not compile on gcc, and CI may be red

`third_party/open303` is missing `#include <climits>` for `INT_MAX`
(`rosic_Open303.h:347`) and `<cstring>` for `memcpy` and `memmove`
(`rosic_FunctionTemplates.h:185` and `186`). Both are hard errors on gcc 13. I
had to build with `-include climits -include cstring` to get anything to link.
MSVC pulls both in transitively, which is why this is invisible on a Windows
machine.

Separately, `CMakeLists.txt:35` globs `*.c` into the `open303` target, but
`fft4g.c` is `#include`d by `rosic_FourierTransformerRadix2.cpp:2`, so it is
compiled twice. On my toolchain that gives duplicate definitions of `cdft`,
`rdft`, `ddct`, `ddst`, `dfct` and `dfst` at link. `project(... LANGUAGES CXX)`
also does not enable C, so what cmake does with a `.c` in that list is something
I cannot determine without cmake.

This is a question, not a finding, and it is the one thing in this report I most
want checked first. `STATUS_2026-08-30.md` says the tree is clean and tests
pass, which reads like a local MSVC result. Has the Linux job in
`.github/workflows/build.yml` been green since Open303 was vendored? One
`actions_list` call settles it, and if it has been red then the matrix has been
decorative for however long, which matters more than any single defect here
given that the whole point of the three-platform matrix is to stop "portable"
becoming "compiles where it was written".

Remediation. Drop `*.c` from the glob at `CMakeLists.txt:37`. Add the two
includes as a documented local patch, which `PROVENANCE.md` already contemplates
for adapted third-party DSP, and record them in
`third_party/open303/README_REBORN.md` so the next vendoring pass reapplies
them.

### 12. Smaller items

`pcf.cpp` `reset()` clears every piece of state except `sinceTick_`
(declared `pcf.cpp:376`, zeroed only at `pcf.cpp:228`). Harmless today because
`onStep` rezeroes it on the next sixteenth, which is accidental protection
rather than intent.

The file's opening comment block (`pcf.cpp:1` to `39`) still describes the
retired design: a glide toward `base_cutoff + envmod * mod`, "a resonant
lowpass" when it is now lowpass and bandpass, and a `mod` parameter. `C_MOD` is
still in the enum at `pcf.cpp:50`, still counted by `numParams()`, still
declared in `kParams`, and read nowhere. Delete the parameter and rewrite the
header to the AD-plus-presets design the rest of the file implements. Note that
deleting `C_MOD` shifts the enum, so check nothing indexes these by number.

`engine.cpp:208` has a stray `(void) 0;`.

`decodeAiff` can return true with an empty `data` vector when `avail` computes to
zero, and `rbm.cpp:285` then divides by `ms.data.size()`. That is 0.0/0.0. It
produces a NaN that nothing subsequently uses, so it is cosmetic, but it is one
line to guard.

The PCF runs two independent stereo cores with identical coefficients
(`pcf.cpp:312` and `322`), but pan is applied after the insert
(`engine.cpp:826`) and all four devices write mono into both channels, so the
right core is duplicated work in every current routing. Worth keeping if the
unit should be usable on a stereo return later, in which case say so in a
comment.

`fireTick` scans past `pos` even though the steps are sorted ascending
(`pcf.cpp:362`). `break` when `step32 > pos`.

`renderSlice` zeroes `chScratch_` before a loop whose first act overwrites it
(`engine.cpp:766` and `785`). Safe, because `processor.h:120` documents the
overwrite contract and I checked all four devices assign rather than accumulate
(`bass_synth.cpp:2113`, `drum909.cpp:789`, `drum808.cpp:598`,
`bass_open303.cpp:137`). Redundant, not a bug.

`knobSlew_`'s in-class initialiser is `0.005f` (`pcf.cpp:386`), a filter
coefficient sitting in the slot where the 5 ms it corresponds to would go.
Rename or initialise to the real coefficient.

`tools/rbsgen/wholemix_song.py:35` hardcodes a path on the author's machine with
no environment override, where `byte_neutral.py:56` and the export automation
both use `os.environ.get` with that path as the default (since fixed: the
published tools read REBORN_SONGS_DIR and stop when it is unset). CI runs on Linux and macOS.

Compiler warnings across the whole core at `-Wall -Wextra`: four, all
`-Wmisleading-indentation` at `bass_synth.cpp:1802` to `1805`. That is unusually
clean for 8,400 lines of DSP and worth saying out loud.

---

## Open questions, where I could not settle it

**Does the PCF's thirty-second clock need to follow shuffle?**
`samplesPer32` at `pcf.cpp:264` is a straight thirty-second from `tempo_` alone,
and `onStep` fires on the straight grid because shuffle delays only device
triggers through `pendingAt_` (`engine.cpp:746`). So the PCF is entirely
shuffle-blind. Whether ReBirth's is too is unmeasured. `onStep` resyncing every
sixteenth bounds any error to within one step, so this is a shape question rather
than a drift one. A fixture at maximum shuffle with preset 2 or 3 and a short
decay would settle it in one export.

**When routing moves, should the unit's state move with it?**
The PCF and the compressor are single instances that migrate between sections
under automation, carrying their state: the PCF its integrators, the compressor
its gain reduction and release memory. Six songs ride the compressor's target and
twelve ride the PCF's. In ReBirth there is one unit, so carrying state may well
be right, but nothing measures it. Item 1 is wrong regardless of how this
resolves, because frozen is not the same as either running or reset.

**Does anything catch `std::bad_alloc` from the loaders?**
I did not trace the exception paths out of `loadRbsFile` into the plugin. If one
escapes on the message thread inside a host, the host takes it. Worth ten minutes
once item 2 is in.

---

## Two corrections to my own working

I would rather record these than let the report look cleaner than the process
was.

**I probed a lowpass with DC.** My first attempt at item 1 fed a DC signal and
came back with a ratio of 0.0x, which looked like the opposite of my hypothesis.
A lowpass passes DC at any cutoff, so the probe could not see the corner at all
and I was reading integrator warm-up. The 2 kHz tone settled it in one run. This
is the project's own rule (change the source before the statistics) and a fifth
instance of the thing `STATUS_2026-08-30.md` records four of.

**I overstated the Python loader duplication.** In my first pass I counted
thirteen mutually different `load` functions and named it "the shape of risk",
pointing at the July mono-as-stereo mis-sum. On checking, that is wrong. Of the
46 scripts that read WAV, every single mono reduction uses `mean(axis=1)`, 39 of
them, with no variant. The three that take `[:, 0]` do so deliberately:
`pcf_response.py:156` reads the left channel of a dual-mono render,
`wholemix.py:68` computes the L/R skew, and `lowend_grid.py:109` measures the
side signal. The thirteen "different bodies" differ in whether they use a context
manager and in the order of the returned tuple. There is no numerical
disagreement between them. This is duplication to tidy at leisure, not a hazard,
and the residual risk is only that a future fix to one would not propagate to
forty-six. I withdraw the framing.

---

## Already in the backlog, not re-raised

`BACKLOG` 6 covers the duplicated parameter defaults between `kParams` and the
`p_` initialiser, `pcf.cpp` included. `BACKLOG` 1, 2, 3 and 8 cover the delay,
distortion and compressor. One staleness note: `PCF_FINDINGS.md` section 4 says
"the distortion bus still has this" about the missing pan law, and on this branch
that is no longer true, because the rack is per-section chains and the pan is
applied after the inserts at `engine.cpp:826`.

---

## Suggested order

1. Check whether CI's Linux job is green (item 11). It conditions how much the
   rest of the matrix is telling you.
2. Land `security-fixes.patch` with three buffer regression tests (item 2). It
   is verified, it is three lines of logic, and it is orthogonal to every parity
   surface, so `byte_neutral.py` should be unchanged by it.
3. Fix item 1 and cut the bypass fixture. This is the one parity defect and it
   belongs in front of Luke's ear before supertask 4 is signed off.
4. One tidying pass for items 3, 10 and 12, no behavioural change, checked with
   `byte_neutral.py`.
5. Items 4 and 5 together, copying `distortion.cpp`'s smoothing pattern.
6. Item 8, starting with the decay table and the mode polarity, which are the
   two cheapest and cover the two most expensive findings.
7. Items 6 and 7 as their own pass, or into `BACKLOG` as a real-time-safety
   entry, since they are the plugin's problem rather than the engine's.

Items 2, 6, 7 and 11 all sit outside the one-component-at-a-time rule, since
they belong to no supertask. They may want a `BACKLOG` entry of their own so
they have an owner.

---

## Appendix: the harnesses

Three standalone `main()` files, built against the core with

    g++ -std=c++17 -O2 -include climits -include cstring -include cstdlib \
        -I include -I third_party/open303 <harness>.cpp \
        $(find src -name '*.cpp') third_party/open303/*.cpp -o probe

Note that `third_party/open303/fft4g.c` must be excluded from that list, per
item 11.

Item 1, the envelope freeze. Build a PCF, set Freq 0, Amt 127, Decay 64, preset
0, tempo 120. Run 24 sixteenths, calling `onStep(step)` each time and calling
`process()` on a full step of samples only in the control arm. Then render the
same short block of a 2 kHz tone in both arms and compare the peak. The bypassed
arm reads 93.5 dB hotter.

Item 3, the mode polarity. Construct a `Song`, set `effectInit.present`,
`pcfFreq = 0.5`, `pcfAmt = 0`, `pcfBandpass` to each of true and false,
`pcfOn = true`, `pcfTarget = 0`. `loadSong`, then read
`engine.pcf()->getParamByName("mode")` and run the unit on white noise through a
one-pole split at 1 kHz. Byte 0 must come out bandpass.

Item 4, the resonance step. Settle the unit on a 300 Hz tone at Q 0, record the
last two output samples to get the local slope, call
`setParamByName("resonance", 1.0f)`, process eight more samples, and measure the
first sample's departure from `last + slope`. Sweep the Freq knob to get the
table in item 4.

I have all three as files and can hand them over in whatever form is easiest to
drop into `tests/` or `tools/`.

The lantern passes on.
C8.31.2026a
