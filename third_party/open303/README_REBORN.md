# Vendored: Open303 DSP (`rosic`)

This directory is a **verbatim vendored copy** of the DSP code from Open303, an
MIT-licensed TB-303 emulation by Robin Schmidt.

- **Upstream:** https://github.com/RobinSchmidt/Open303
- **Commit:** `313bf0d9ade7c1dcb6b3a74f5ea1780a29d70074`
- **Pulled:** 2026-08-08
- **License:** MIT (see `LICENSE.txt`, Copyright (c) 2009 Robin Schmidt,
  www.rs-met.com). Retained in full.
- **Source path upstream:** `Source/DSPCode/` (copied whole; it is already the
  self-contained subset Open303 needs).

## Why it is here

Per the 2026-08-08 policy change (see `docs/PROVENANCE.md`), MIT/free-libre 303
emulators may be adapted and shipped with attribution. Open303 is the basis for
Reborn's selectable best-of-breed 303 voice (`src/devices/bass_open303.cpp`, a
`reborn::Instrument` wrapper); the default voice is Reborn's native TPT voice. This is a **synthesis/product** input: Open303
emulates the Roland *hardware* 303, not ReBirth, so it is never used as a
ReBirth-parity input. The native TPT engine remains the parity path.

## Modification policy

Keep this copy as close to upstream as possible so it can be re-synced. Any local
edit (e.g. a build-portability fix, a namespace guard, or a "best of breed"
improvement) must be:
- marked in-file with a `// REBORN:` comment stating what and why, and
- listed here, so the diff against upstream is always legible.

### Local modifications
- `rosic_Open303.h`: added `#include <climits>` for `INT_MAX`. Used upstream but
  never included; MSVC pulls it in transitively, libstdc++ (gcc/clang) does not.
- `rosic_FunctionTemplates.h`: added `#include <cstring>` for `memcpy`/`memmove`.
  Same reason as above.
- `GlobalFunctions.h` (around lines 81 and 305): the custom `log2` declaration
  and definition are compiled only when not on MSVC, which treats `log2` as an
  intrinsic that cannot be redefined (C2169); elsewhere the standard `log2` is
  used, so unqualified calls behave as before.
- `rosic_NumberManipulations.h` (around lines 22 and 58): the inline-assembly
  branches are taken only on 32-bit x86 MSVC (`_M_IX86`); x64 MSVC has no
  inline `__asm`, so it takes the portable branch that gcc and clang take.
- `rosic_RealFunctions.h` (around line 124): `sinCos` uses portable `sin`/`cos`
  everywhere except 32-bit x86 MSVC, for the same reason.
- Build system, not a file edit: `CMakeLists.txt` globs only `*.cpp` from this
  directory. `fft4g.c` is `#include`d by `rosic_FourierTransformerRadix2.cpp`, so
  compiling it standalone as well duplicated its symbols at link on gcc/clang.
- `fft4g.c`: a comment block at the top records its author and terms (below);
  the code itself is unchanged.

### `fft4g.c`

`fft4g.c` is Takuya Ooura's general purpose FFT package (1996-2001), which
Open303 includes. It is not under Open303's MIT licence but under its author's
own terms, quoted in the comment at the top of the file: use, copying,
modification and distribution for any purpose, including commercial use,
without fee, with a reference to the package when it is modified.

## What Reborn uses vs ignores

`rosic::Open303` (in `rosic_Open303.{h,cpp}`) is the synth. Its built-in
`AcidSequencer`/`AcidPattern` are NOT used for playback (Reborn drives the voice
from its own step sequencer via the `Instrument`/`BassEvent` boundary), but they
are part of the class and are compiled for completeness.
