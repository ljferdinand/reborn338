# Building Reborn 338

Reborn has two layers with very different build needs. The engine library
(`reborn_core`), the command-line tools and the tests are plain C++17 with no
dependencies and no platform code; they build anywhere CMake and a C++17
compiler do. The app and plugins add JUCE, which CMake fetches the first time
you configure them.

## Prerequisites

| | Engine, tools and tests | App and plugins, in addition |
|---|---|---|
| Windows | Visual Studio 2022 with the "Desktop development with C++" workload, CMake 3.20 or later, Git | nothing more |
| Linux | gcc or clang with C++17, CMake 3.20 or later, Git | the development packages JUCE needs (below) |
| macOS | Xcode command-line tools (`xcode-select --install`), CMake 3.20 or later, Git | nothing more |

The first configure of the app needs network access, to fetch JUCE 8.0.6 and a
pinned commit of clap-juce-extensions. On Linux, JUCE needs these packages
(the list CI installs on Ubuntu 24.04):

```sh
sudo apt-get install -y libasound2-dev libjack-jackd2-dev ladspa-sdk \
  libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev \
  libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  libglu1-mesa-dev mesa-common-dev
```

Python 3 is needed only for the measurement and fixture tools in `tools/`, not
for building. Their packages are listed in `tools/requirements.txt`.

## The engine, the tools and the tests

Windows, from a Developer PowerShell or any shell with CMake on the path:

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Linux and macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

With the Visual Studio and Xcode generators the binaries land in
`build/Release/`; with Make or Ninja they land in `build/`. The build produces:

| Program | What it does |
|---|---|
| `reborn_render` | renders a song, or the built-in demo, to a WAV with no audio device |
| `reborn_roundtrip` | loads a `.rbs`, saves it, reloads it and checks nothing was lost |
| `reborn_rbmkeys` | reports on how a mod's keys are generated or estimated, for diagnosis |
| `reborn_tests` | the unit tests (run by `ctest`) |
| `reborn_dock_tests` | the tests for the app's window-docking geometry (run by `ctest`) |
| `comp_wav`, `dist_wav` | run a WAV through the compressor or the distortion alone, for measurement |

A first render, to check the build works:

```sh
./build/Release/reborn_render --seconds 8 --out demo.wav
./build/Release/reborn_render --song examples/acid_line.rbs --seconds 16 --out acid_line.wav
```

`docs/USAGE.md` covers every option.

On macOS you can build a universal binary by adding
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`
to the configure line. The song files are big-endian and read byte by byte, so
the engine has no endianness or architecture dependence.

## The app and the plugins

Configure in a separate build directory with `REBORN_BUILD_APP` on:

```sh
cmake -S . -B build-app -G "Visual Studio 17 2022" -A x64 -DREBORN_BUILD_APP=ON
cmake --build build-app --config Release --parallel
```

On Linux and macOS drop the generator and add `-DCMAKE_BUILD_TYPE=Release`, as
above. Building everything also builds `reborn_dock_spike`, an early experiment
with the docking windows that nothing depends on; to build only what matters,
name the targets:

```sh
cmake --build build-app --config Release --parallel --target RebornApp_Standalone RebornApp_VST3 RebornApp_CLAP Open303Plugin_Standalone Open303Plugin_VST3 Open303Plugin_CLAP reborn_session_smoke reborn_host_smoke
```

On macOS add `RebornApp_AU` and `Open303Plugin_AU`. Two options are on by
default: `REBORN_BUILD_CLAP` (the CLAP builds, and CLAP hosting inside the app)
and `REBORN_BUILD_OPEN303_PLUGIN` (the separate Reborn Open303 plugin).

What you get, under `build-app/`:

| Artefact | Where |
|---|---|
| Reborn 338, standalone | `RebornApp_artefacts/Release/Standalone/` (`Reborn 338.exe` on Windows, `Reborn 338.app` on macOS) |
| Reborn 338, VST3 | `RebornApp_artefacts/Release/VST3/Reborn 338.vst3` |
| Reborn 338, CLAP | `RebornApp_artefacts/Release/CLAP/Reborn 338.clap` |
| Reborn 338, AU (macOS) | `RebornApp_artefacts/Release/AU/Reborn 338.component` |
| Reborn Open303 | `build-app/plugins/open303/Open303Plugin_artefacts/Release/`, in the same four forms |
| the smoke harnesses | `reborn_session_smoke_artefacts/Release/`, `reborn_host_smoke_artefacts/Release/` |

With Make or Ninja the `Release/` level is absent.

The plugins are not copied anywhere by the build. To use them in a host, copy
them to its plugin folder: on Windows `C:\Program Files\Common Files\VST3` and
`C:\Program Files\Common Files\CLAP`; on macOS `~/Library/Audio/Plug-Ins/VST3`,
`~/Library/Audio/Plug-Ins/CLAP` and `~/Library/Audio/Plug-Ins/Components` (for
the AU, which `auval -v aumu Rb38 Rbrn` validates); on Linux `~/.vst3` and
`~/.clap`.

**Building offline.** If JUCE and clap-juce-extensions are already on disk, point
CMake at them instead of fetching:
`-DFETCHCONTENT_SOURCE_DIR_JUCE=<path to JUCE 8.0.6>` and
`-DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=<path to the pinned commit>`
(the commit is in `CMakeLists.txt`; it needs its `clap-libs` submodules).

**What has been validated.** The app and both plugins were developed and used on
Windows. The Linux and macOS app builds compile in CI when it is run by hand, and
nobody has run them; treat them as untested. Signing and notarisation for macOS
distribution were never set up.

### The smoke harnesses

Two headless programs exercise the app's code without a window:

```sh
reborn_session_smoke examples/acid_line_open303.rbsx
reborn_host_smoke <plugin.vst3>
reborn_host_smoke --render <plugin.vst3> <song.rbs> <out.wav> [seconds]
```

`reborn_session_smoke` opens a `.rbsx` session in the real plugin processor,
saves its state and restores it. `reborn_host_smoke` loads a third-party VST3
into a 303 slot and checks that a note sounds and stops, or renders a song's
first 303 line through it.

## Checking a machine renders correctly

A build that compiles but renders slightly differently is the worst state to
measure anything in, so the project keeps a recorded baseline. The fixtures it
uses are generated by committed code, so this needs nothing outside the
repository:

```sh
export REBORN_WORKSPACE="$PWD/../reborn-workspace"   # any empty directory
mkdir -p "$REBORN_WORKSPACE"
python tools/rbsgen/gen_batchG.py
python tools/rbsgen/gen_batchH.py
python tools/measure/platform_baseline.py check ./build/Release/reborn_render "$REBORN_WORKSPACE" docs/baselines/windows-x86_64.json
```

It renders ten deliberately varied fixtures on the native TPT 303 voice and
compares peak, rms, spectral centroid and 85 percent rolloff with the reference
recorded on Windows, within 0.05 dB and 0.5 percent. Bit-identical output across
compilers is not expected (MSVC and clang make different floating-point
contractions); agreeing measurements are. If a metric drifts, find out why before
trusting any measurement made with that build.

The baseline names the commit it was recorded at, in the project's private
development history, which is not published; read it as a date stamp. Any
change to the 303's DSP
moves these renders, so such a change has to re-record the baseline, on Windows
only (`platform_baseline.py record`), in the same commit.

## What CI runs

`.github/workflows/build.yml` runs on every push and pull request:

- on Linux (gcc), macOS (clang, arm64) and Windows (MSVC): configure, build, run
  the tests, render the built-in demo, check the documentation
  (`tools/check_docs.py`, Linux only), generate the baseline fixtures, check the
  platform baseline, round-trip the 36 generated fixtures, and confirm that six
  re-saved fixtures render byte-identically to the originals;
- on Windows: build the app, both plugins in all formats, and the smoke
  harnesses.

Linux and macOS app builds run only when the workflow is started by hand
(Actions, "Run workflow"). Runners are pinned to `ubuntu-24.04`, `macos-14` and
`windows-2022` so an image change cannot turn the badge red on its own.

The engine, the tools and the tests build without compiler warnings on MSVC;
the app build adds only JUCE's notice that the splash-screen flag it is given
is ignored. The build does not use
`-Werror` or `/WX`, on purpose: compilers and runner images change, and a
dormant project should not fail on a new warning.

## The measurement tools

The Python tools in `tools/` generate fixtures, render batches and measure
renders against ReBirth's exports. They need the packages in
`tools/requirements.txt`:

```sh
python -m pip install -r tools/requirements.txt
```

Two environment variables tell them where material outside the repository
lives. `REBORN_WORKSPACE` is the directory that holds generated fixtures
(`fixtures-<date>/`) and ReBirth's exports (`renders/`); it defaults to the
directory that contains your checkout. `REBORN_SONGS_DIR` is your own ReBirth
install's `Demo Songs` folder, which the whole-song tools and
`tools/render/byte_neutral.py` read. A tool that needs `REBORN_SONGS_DIR` and
finds it unset stops with a message; one that finds no fixtures or exports under
the workspace says where it looked. ReBirth's exports have to come from your own copy of
ReBirth; `docs/MEASUREMENT_PROTOCOL.md` describes how a valid comparison is made.

Reference exports come from ReBirth 2.0.1, a 32-bit Windows program, so new
exports can only be made on Windows (or wherever you can run it faithfully). Every
other part of the work, from generating fixtures to rendering and measuring, runs
anywhere. If you do run ReBirth some other way, under an emulator or on another
operating system, check its exports against Windows exports of the same song
before mixing them into a measurement: a different build or audio path is a
different target.
