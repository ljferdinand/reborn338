cmake_minimum_required(VERSION 3.20)
project(Reborn338 VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Reborn 338 - a clean-room reimplementation of a classic pattern-based groovebox
# (2x bass synth, 2x drum machine, effects rack), licensed AGPL-3.0-only.
#
# No code, samples or artwork from the original software are in this project; its
# behaviour was measured from the original's audio output (see docs/PROVENANCE.md).
# One bass voice is the MIT-licensed Open303, vendored in third_party/. The engine
# is dependency-free standard C++; the GUI and plugins use JUCE (-DREBORN_BUILD_APP).
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
  # /wd4244 /wd4305: intentional double->float narrowing is pervasive in DSP.
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc /wd4244 /wd4305)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  add_compile_options(-Wall -Wextra)
endif()

# --- Vendored Open303 DSP (MIT, third_party/open303) ----------------------
# Robin Schmidt's Open303, MIT-licensed; the selectable Open303 bass voice (the
# default is the native TPT voice; see docs/PROVENANCE.md and
# third_party/open303/README_REBORN.md).
# Built as its own static lib with RELAXED flags: this 2009 code is not held to
# the strict /W4 /permissive- we hold our own code to. Linked PRIVATE into
# reborn_core so its headers stay internal to the bass_open303 wrapper.
# Only *.cpp. fft4g.c is the sole .c file and it is #included by
# rosic_FourierTransformerRadix2.cpp, so globbing it here compiled it a second
# time as its own translation unit and duplicated cdft/rdft/ddct/... at link on
# gcc and clang. MSVC happened to tolerate it; the Linux and macOS jobs did not.
file(GLOB OPEN303_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303/*.cpp")
add_library(open303 STATIC ${OPEN303_SOURCES})
target_include_directories(open303 PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303")
if(MSVC)
  target_compile_options(open303 PRIVATE /W0 /permissive)
else()
  target_compile_options(open303 PRIVATE -w)
endif()

# --- Core engine: pure C++, zero external dependencies --------------------
# (reborn_core's own code stays dependency-free; the Open303 lib above is the one
# vendored exception, kept PRIVATE so it never leaks into the engine's API.)
file(GLOB_RECURSE REBORN_CORE_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(reborn_core STATIC ${REBORN_CORE_SOURCES})
target_include_directories(reborn_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
find_package(Threads REQUIRED)   # the mod unscrambler spreads its work over the cores
target_link_libraries(reborn_core PRIVATE open303 PUBLIC Threads::Threads)

# --- Headless renderer: verification harness (WAV out, no audio device) ---
add_executable(reborn_render tools/render/main.cpp)
target_link_libraries(reborn_render PRIVATE reborn_core)

# --- Unit / smoke tests ---------------------------------------------------
enable_testing()
add_executable(reborn_roundtrip tools/render/roundtrip_main.cpp)
target_link_libraries(reborn_roundtrip PRIVATE reborn_core)

# Mod unscrambling diagnostics: estimate a .rbm's keys, self-check, score vs exact keys.
add_executable(reborn_rbmkeys tools/render/rbmkeys_main.cpp)
target_link_libraries(reborn_rbmkeys PRIVATE reborn_core)

add_executable(reborn_tests tests/test_main.cpp)
target_link_libraries(reborn_tests PRIVATE reborn_core)
add_test(NAME reborn_tests COMMAND reborn_tests)
set_tests_properties(reborn_tests PROPERTIES TIMEOUT 600)   # a parser hang fails, not waits

# Docking geometry tests: pure rectangle math (app/src/dock/DockGeometry), no
# JUCE, so the Stage-1 snapping/grouping/multi-screen-recovery invariants are
# covered headlessly and always built, even without -DREBORN_BUILD_APP.
add_executable(reborn_dock_tests tests/test_dock.cpp app/src/dock/DockGeometry.cpp)
add_test(NAME reborn_dock_tests COMMAND reborn_dock_tests)

# --- JUCE front-end (optional; fetches JUCE) -------------------------------
# Configure with -DREBORN_BUILD_APP=ON to build the Standalone app + VST3.
# Kept off by default so the core builds without network access.
option(REBORN_BUILD_APP "Build the JUCE front-end (fetches JUCE)" OFF)
if(REBORN_BUILD_APP)
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(JUCE)

  # CLAP export. clap-juce-extensions reuses the existing processor/editor/state
  # (no processor changes) and pulls its own clap + clap-helpers deps. Declared
  # BEFORE juce_add_plugin so clap_juce_extensions_plugin() is available below.
  #
  # CLAP is an open, MIT-licensed plugin standard; the VST3 SDK that JUCE bundles is
  # used under its GPLv3 option, which the project's AGPL-3.0 licence is compatible
  # with. Opt-out because it needs network access at configure time. Pinned to a
  # commit (not a branch) so a build is reproducible; GIT_SHALLOW cannot fetch a
  # bare commit, so it is left off here.
  option(REBORN_BUILD_CLAP "Also build a CLAP (fetches clap-juce-extensions)" ON)
  if(REBORN_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
      GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
      GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d)
    FetchContent_MakeAvailable(clap-juce-extensions)
    # CLAP HOSTING (fork P7): the clap-core INTERFACE target that comes with
    # clap-juce-extensions gives us the CLAP C API headers, so we can also HOST
    # CLAP plugins (a hand-rolled juce::AudioPluginFormat). Attached to any target
    # that compiles PluginHost.cpp.
    set(REBORN_CLAP_HOST_SOURCES app/src/ClapPluginFormat.cpp)
    set(REBORN_CLAP_HOST_LIBS    clap-core)
    set(REBORN_CLAP_HOST_DEF     REBORN_HOST_CLAP=1)
  endif()

  # AU is macOS-only; asking for it elsewhere makes JUCE warn and skip.
  set(REBORN_FORMATS Standalone VST3)
  if(APPLE)
    list(APPEND REBORN_FORMATS AU)
  endif()

  juce_add_plugin(RebornApp
    PRODUCT_NAME "Reborn 338"
    COMPANY_NAME "RebornProject"
    PLUGIN_MANUFACTURER_CODE Rbrn
    PLUGIN_CODE Rb38
    FORMATS ${REBORN_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(RebornApp PRIVATE
    app/src/PluginProcessor.cpp
    app/src/PluginEditor.cpp
    app/src/dock/DockGeometry.cpp
    app/src/PluginHost.cpp
    app/src/HostedInstrument.cpp
    ${REBORN_CLAP_HOST_SOURCES})

  # Host-format flags (fork P5): Reborn can HOST third-party 303 synths in a voice
  # slot. VST3 hosting always (covers jc303 + our own Open303 plugin); AU on macOS.
  # CLAP hosting is not built in to JUCE 8, so it is Reborn's own
  # app/src/ClapPluginFormat.cpp, compiled in with REBORN_HOST_CLAP above.
  # JUCE_MODAL_LOOPS_PERMITTED re-enables ThreadWithProgressWindow::runThread(),
  # which the plugin scan uses to block on a modal progress window (a deliberate
  # user action from the "Load plugin" picker).
  set(REBORN_HOST_DEFS JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 ${REBORN_CLAP_HOST_DEF})
  if(APPLE)
    list(APPEND REBORN_HOST_DEFS JUCE_PLUGINHOST_AU=1)
  endif()

  target_compile_definitions(RebornApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    ${REBORN_HOST_DEFS})

  target_link_libraries(RebornApp PRIVATE
    reborn_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    ${REBORN_CLAP_HOST_LIBS})

  # Wrap the same target as a CLAP. Features follow the CLAP taxonomy so hosts
  # file Reborn under instruments; the id is a stable reverse-DNS string.
  if(REBORN_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET RebornApp
      CLAP_ID "org.rebornproject.reborn338"
      CLAP_FEATURES instrument synthesizer drum-machine sequencer
      CLAP_MANUAL_URL "https://github.com/ljferdinand/reborn338"
      CLAP_SUPPORT_URL "https://github.com/ljferdinand/reborn338/issues")
  endif()

  # The standalone "fat" Open303 instrument plugin (fork P4): the best-of-breed
  # 303 voice + ReBirth's 303 knob-FX, self-contained for other DAWs. Added here
  # so it sees the JUCE + clap-juce-extensions fetched above.
  option(REBORN_BUILD_OPEN303_PLUGIN "Build the standalone Open303 instrument plugin" ON)
  if(REBORN_BUILD_OPEN303_PLUGIN)
    add_subdirectory(plugins/open303)
  endif()

  # Headless host-harness smoke test (fork P5): loads a VST3 through PluginHost,
  # wraps it in HostedInstrument, drives a note, and asserts it sounds then
  # silences. Verifies the whole hosting path with no GUI (point it at any VST3).
  juce_add_console_app(reborn_host_smoke PRODUCT_NAME "reborn_host_smoke")
  target_sources(reborn_host_smoke PRIVATE
    app/host_smoke/main.cpp
    app/src/PluginHost.cpp
    app/src/HostedInstrument.cpp
    ${REBORN_CLAP_HOST_SOURCES})
  target_compile_definitions(reborn_host_smoke PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    ${REBORN_HOST_DEFS})
  target_link_libraries(reborn_host_smoke PRIVATE
    reborn_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${REBORN_CLAP_HOST_LIBS})

  # Headless session-state smoke (fork P6): instantiate the REAL processor and
  # open a .rbsx / round-trip its DAW state, so the full restore path (including
  # a hosted 303) is verifiable without the GUI. Links the app processor + editor
  # sources (the editor only for createEditor's symbol; it is never constructed).
  juce_add_console_app(reborn_session_smoke PRODUCT_NAME "reborn_session_smoke")
  target_sources(reborn_session_smoke PRIVATE
    app/session_smoke/main.cpp
    app/src/PluginProcessor.cpp
    app/src/PluginEditor.cpp
    app/src/dock/DockGeometry.cpp
    app/src/PluginHost.cpp
    app/src/HostedInstrument.cpp
    ${REBORN_CLAP_HOST_SOURCES})
  target_compile_definitions(reborn_session_smoke PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    ${REBORN_HOST_DEFS})
  target_link_libraries(reborn_session_smoke PRIVATE
    reborn_core
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    ${REBORN_CLAP_HOST_LIBS})

  # Dock spike (fork GUI Stage 1): a standalone GUI app that stands up the
  # frameless-block docking model (app/src/dock/*) with placeholder panels, so
  # the cross-window magnetic-snapping model can be judged on real Windows before
  # the visual redesign. See docs/PLAN_GUI.md section 5.
  juce_add_gui_app(reborn_dock_spike PRODUCT_NAME "Reborn Dock Spike")
  target_sources(reborn_dock_spike PRIVATE
    app/dock_spike/main.cpp
    app/src/dock/DockGeometry.cpp)
  target_compile_definitions(reborn_dock_spike PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="Reborn Dock Spike"
    JUCE_APPLICATION_VERSION_STRING="0.1")
  target_link_libraries(reborn_dock_spike PRIVATE
    reborn_core
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
endif()

add_executable(comp_wav tools/render/comp_wav_main.cpp)
target_link_libraries(comp_wav PRIVATE reborn_core)

add_executable(dist_wav tools/render/dist_wav_main.cpp)
target_link_libraries(dist_wav PRIVATE reborn_core)
