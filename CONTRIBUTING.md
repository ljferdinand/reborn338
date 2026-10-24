# Contributing

Active development of Reborn 338 has stopped, but the repository stays open.
Issues and pull requests are welcome; they may not be answered quickly. If you
want to carry the project on, `docs/HANDOVER.md` is the place to start, and a
fork is as welcome as a pull request.

## Building and testing

`docs/BUILDING.md` covers every platform. Before sending a change:

```sh
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
python tools/check_docs.py
```

CI builds and runs the tests on Linux, macOS and Windows and the documentation
check on Linux, plus the platform baseline and the round-trip checks, and builds
the app and plugins on Windows. A pull request should leave it green.

## The rules that keep the project what it is

**Nothing derived from ReBirth, ever.** No samples, songs, mods, mod keys,
decoded mod content, artwork, screenshots, exports or bytes of the program may be
committed, not even in a test. Tests build synthetic data instead.
`.gitignore` blocks the common file types, but the rule is yours to keep.

**No decompiled or disassembled material as a source.** Reborn is built from
measurements of ReBirth's audio output and file formats. A constant, table,
formula or structure taken from a decompilation or disassembly of ReBirth or
ModPacker cannot be accepted, and neither can a change justified by one.
`docs/PROVENANCE.md` explains the position.

**Measure before changing sound.** A change that alters how Reborn sounds should
come with the measurement that motivates it, made the way
`docs/MEASUREMENT_PROTOCOL.md` describes: one device, one effect or none, the
same method on both sides, never a null test against ReBirth. Change one
component per pull request, and record what you found in the relevant findings
document or in `docs/BACKLOG.md`.

**Keep the baselines honest.** A change to the 303's DSP moves the platform
baseline and must re-record `docs/baselines/windows-x86_64.json` on Windows in the
same change. A change meant to leave sound alone should show the byte-neutral
check unchanged (`tools/render/byte_neutral.py`, which needs the demo songs from
your own ReBirth install). Read `docs/NON_OBVIOUS_BEHAVIOUR.md` first: several
things in the code look like bugs and are measured behaviour.

## Style

Match the file you are editing. The existing code is C++17 with no dependencies
in the engine, and the existing documents are plain prose that avoids em-dashes
(`tools/check_docs.py` enforces that in markdown), states figures with the
conditions they were measured under, and says when something is uncertain.

## Licence

Reborn 338 is licensed under the GNU Affero General Public License, version 3
only (`LICENSE`). Contributions are accepted under the same licence; there is no
contributor licence agreement. Add the SPDX tag
(`// SPDX-License-Identifier: AGPL-3.0-only`) to new C and C++ files.
