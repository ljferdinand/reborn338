# Provenance & clean-room statement

Reborn 338 is a **clean-room reimplementation** created for non-commercial
software preservation, study, and interoperability.

## What was and was not used

**Used (permitted):**
- **Observation of shipped data files** (`.rbs` songs) to reverse-engineer the
  open, self-describing IFF container format, a standard interoperability
  activity. Findings are documented in [FORMAT_RBS.md](FORMAT_RBS.md).
- **Publicly documented behaviour** of the analog hardware the original emulated
  (Roland TB-303 / TR-808 / TR-909): oscillator shapes, filter topologies,
  envelope and voicing descriptions found in service manuals, teardowns, and the
  general synth-DSP literature.
- **Third-party open-source emulators under permissive licenses.** DSP source and
  constants from MIT-licensed (and other free/libre) TB-303 emulators (Open303
  (RobinSchmidt/rosic) and the like) may be read, adapted, and shipped, subject
  to each project's own license and with attribution. See "Third-party
  open-source code" below.

**Not used (deliberately excluded):**
- No decompilation output, disassembly, or transcribed machine code **from the
  original program (ReBirth RB-338)** is present in this codebase. A separate
  third-party decompilation of ReBirth exists outside this repository (see
  "Third-party binary analysis" below); the commitment here is that none of its
  output, and no coefficient, table, equation, or constant recovered from it,
  ever enters the codebase.
- No DSP source, coefficient tables, wavetables, or ROM sample data **from the
  original program or from Roland hardware** is copied or redistributed. (This
  exclusion is about ReBirth and Roland specifically; it does not restrict the
  permissively-licensed third-party emulators covered above. Reborn's own
  synthesis remains original code.)
- No GUI artwork, bitmaps, skins ("Mods"), fonts, or help content from the
  original are included.

## Third-party open-source code

Reborn may incorporate DSP from permissively-licensed open-source instrument
emulators. The clean-room rule is a rule about **ReBirth**, not a blanket ban on
reading other code: it forbids decompiling or transcribing ReBirth's own binary,
and it keeps the ReBirth *parity* engine free of any code that is not our own
measurement of ReBirth. It does **not** forbid using freely-licensed third-party
303 emulators as the basis for a separate, best-of-breed voice.

- **What is allowed.** Reading, adapting, and shipping code from MIT-licensed and
  comparably free/libre 303 emulators (e.g. Open303, MIT), each under its own
  license terms, with its LICENSE text retained and attribution given.
- **What stays off-limits.** GPL-licensed forks (e.g. jc303, GPLv3) are **not**
  bundled into a shipped Reborn binary, because linking GPL code would force the
  whole product under the GPL. A GPL plugin may still be *hosted* as a separate
  binary (the user's choice, no linking). And ReBirth's own code is never
  decompiled or transcribed, ever.
- **Where it lives, and why it does not muddy parity.** Adapted third-party DSP
  lives in its own module (e.g. an Open303-derived voice) with its provenance and
  license recorded. The native ReBirth-parity engine's constants are fitted from
  ReBirth exports, so it is unaffected. Open303 emulates the Roland *hardware*,
  not ReBirth, so it is a legitimate **synthesis / product** input but never a
  ReBirth **parity** input; parity is still judged only against ReBirth's output.

## Third-party binary analysis (added 2026-09), and the measurement-only wall

The clean-room commitment above originally described an absolute posture: the
binary was never inspected in any form. That is no longer literally true of this
project's orbit, and this section records the change plainly rather than leaving
the older sentences to imply otherwise.

In September 2026, at the maintainer's direction, a separate third-party tool
produced a decompilation of ReBirth (recovered DSP structures, coefficients and
tables for the synths and effects). It lives **outside this repository**, in a
non-committed location, and it is treated as forbidden source material by the
same rule as before: **no output of it, and no coefficient, generator equation,
table, magic constant, arithmetic ordering, or code recovered from it, is ever
copied into this codebase.**

What the analysis is used for is narrower than "a source of findings": it is used
only to **prioritise which black-box experiments to run**. Repeatedly, our fitted
models had the wrong structure (a filter topology, a quantised control, a
nonlinear transfer), and further curve-fitting on the wrong structure could not
converge. The analysis points at the discriminating experiment that reveals the
right structure. The finding itself is then established by **measuring ReBirth's
audio output**, exactly as before, and only the measurement is cited.

To keep that distinction real rather than nominal, a re-derived component passes
through a two-role wall:

- **Role A, measurement and specification.** May be informed by the analysis.
  Runs the black-box probes, produces the measured data, and writes a
  **coefficient-free behavioural specification** that names experiments and
  observed behaviour, never a recovered constant.
- **Role B, implementation.** Works from the behavioural specification, the
  measured data, and the existing codebase **only**. It is walled off from the
  analysis and fits **its own** coefficients to the measured curves. In practice
  this is a fresh context with no access to the analysis or to the conversation
  that read it.

In a few places a value fitted this way later turned out to lie close to one in
the analysis. Those agreements were never used to set or to check a value: every
value in the code comes from the measurements recorded in `docs/research/` and
`docs/specs/`, and the working notes that remarked on an agreement were edited
before publication so that no value from the analysis appears in this repository,
even as a comparison. The target remains behavioural parity against ReBirth's
output, not reproduction of the original's internal numbers, which the
non-deterministic exports make meaningless to chase in any case.

## Why clean-room

The original software is discontinued but remains under copyright; it was
distributed free for a period but never placed in the public domain or
open-sourced. A verbatim decompilation would therefore be a derivative of
copyrighted code. Reimplementing behaviour from scratch (the approach used by
established preservation projects such as ScummVM and OpenMW) keeps the result
free of the original's protected expression while still honoring and preserving
the *design*.

## Trademarks

Product names and hardware model numbers referenced in documentation are
trademarks of their respective owners and are used solely for identification and
historical accuracy. This project is not affiliated with or endorsed by any of
them. The project's own working name ("Reborn 338") is intentionally distinct.

## Intent

This work is shared for preservation, education, and anyone who wants to
continue it, under the AGPL-3.0 (`LICENSE`). If a rights holder has concerns,
please contact the maintainer through the repository on GitHub.
