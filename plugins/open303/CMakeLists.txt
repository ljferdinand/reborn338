# Reborn Open303: the standalone "fat" 303 instrument plugin.
#
# The best-of-breed Open303 voice (the same makeOpen303() Reborn offers as a
# selectable 303 voice) wrapped as a MIDI instrument, plus ReBirth's 303 knob-FX
# (distortion), self-contained for any DAW. CLAP is the license-clean primary
# format; VST3 (+ AU on macOS) are added under Steinberg's terms.
#
# This target is for OTHER hosts only. Inside Reborn the same DSP is linked
# directly (see src/devices/bass_open303.cpp), not hosted.
#
# Requires the JUCE + clap-juce-extensions that the parent CMakeLists fetches in
# its REBORN_BUILD_APP block, so it is added as a subdirectory from there.

set(OPEN303_FORMATS Standalone VST3)
if(APPLE)
  list(APPEND OPEN303_FORMATS AU)
endif()

juce_add_plugin(Open303Plugin
  PRODUCT_NAME "Reborn Open303"
  COMPANY_NAME "RebornProject"
  PLUGIN_MANUFACTURER_CODE Rbrn
  PLUGIN_CODE Op03
  FORMATS ${OPEN303_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Open303Plugin PRIVATE
  src/PluginProcessor.cpp)

target_compile_definitions(Open303Plugin PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Open303Plugin PRIVATE
  reborn_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags)

# Wrap the same target as a CLAP, the license-clean primary format.
if(REBORN_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET Open303Plugin
    CLAP_ID "org.rebornproject.open303"
    CLAP_FEATURES instrument synthesizer
    CLAP_MANUAL_URL "https://github.com/ljferdinand/reborn338"
    CLAP_SUPPORT_URL "https://github.com/ljferdinand/reborn338/issues")
endif()
