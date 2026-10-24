# Third-party components

Reborn 338 itself is licensed under the GNU Affero General Public License, version 3
only (see `LICENSE`). It builds on the components below. Only Open303 (with the Ooura
FFT inside it) is copied into this repository; the rest are fetched by CMake when you
configure with `-DREBORN_BUILD_APP=ON`, and the command-line engine needs none of them.

| Component | Version | Where | Licence | Used for |
|---|---|---|---|---|
| Open303 (rosic library) | upstream commit `313bf0d9ade7c1dcb6b3a74f5ea1780a29d70074` | vendored in `third_party/open303/` | MIT, Copyright (c) 2009 Robin Schmidt (`third_party/open303/LICENSE.txt`) | the selectable Open303 bass voice (`src/devices/bass_open303.cpp`) and the standalone Reborn Open303 plugin (`plugins/open303/`) |
| Ooura FFT (`fft4g.c`) | as shipped inside Open303 | `third_party/open303/fft4g.c` | Copyright Takuya Ooura, 1996-2001; free to use, copy, modify and distribute for any purpose, including commercial use, without fee, provided the package is credited when modified | compiled as part of Open303 |
| JUCE | 8.0.6 (tag, commit `51a8a6d7`) | fetched by CMake (`FetchContent`) | AGPLv3, or the commercial JUCE 8 licence; Reborn uses it under the AGPLv3 | the app, the plugins and the plugin host |
| VST3 SDK | as bundled in JUCE 8.0.6 | inside JUCE | Steinberg VST3 License or GPLv3; Reborn uses it under the GPLv3 | building and hosting VST3 plugins |
| AudioUnitSDK | as bundled in JUCE 8.0.6 | inside JUCE, macOS only | Apache 2.0 | building AU plugins on macOS |
| clap-juce-extensions | commit `7adee3a1bd4684d4caa5601100e364abccff4b4d` | fetched by CMake | MIT, Copyright 2019-2020 Paul Walker | building the CLAP plugins |
| CLAP | 1.2.7 (`29ffcc27`), via clap-juce-extensions | fetched | MIT, Copyright (c) 2021 Alexandre Bique | the CLAP plugin format and Reborn's CLAP host (`app/src/ClapPluginFormat.cpp`) |
| clap-helpers | `a61bcdf0`, via clap-juce-extensions | fetched | MIT, Copyright (c) 2021 Alexandre Bique | CLAP plugin support |

JUCE also bundles further libraries (FLAC, Ogg Vorbis, jpeglib, CHOC, Oboe and
others) under their own permissive licences; see JUCE's `LICENSE.md` in the fetched
tree. Reborn builds no AAX or LV2 plugins.

## Code adapted from Open303 outside its folder

Two first-party files carry material derived from Open303 and say so where it
appears:

- `src/devices/bass_open303.cpp` wraps the Open303 engine as a Reborn instrument.
- `src/devices/bass_synth.cpp` uses filter coefficients and a resonance skew adapted
  from Open303 in its default-off `REBORN_FILTER=rb` probe (see the ATTRIBUTION
  comment there).

Local edits to the vendored copy are listed in `third_party/open303/README_REBORN.md`
and marked `// REBORN:` in the source.

## Python tools

The measurement and fixture scripts under `tools/` use numpy, scipy and matplotlib,
and optionally cma and Pillow (`tools/requirements.txt`). They are installed by the
user and not redistributed.

## Trademarks

ReBirth and ReBirth RB-338 are trademarks of Reason Studios AB (formerly
Propellerhead Software). TB-303, TR-808 and TR-909 are trademarks of Roland
Corporation. VST is a trademark of Steinberg Media Technologies GmbH. Audio Units is
a trademark of Apple Inc. JUCE is a trademark of Raw Material Software Limited. These
names are used only to describe what Reborn is compatible with or modelled on.
Reborn is not affiliated with or endorsed by any of these companies.
