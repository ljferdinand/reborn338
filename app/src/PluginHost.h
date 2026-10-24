// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// PluginHost: the third-party plugin discovery + instantiation layer for the
// 303 voice-slot host harness (fork P5). Wraps a juce::AudioPluginFormatManager
// and a persisted juce::KnownPluginList, with a dead-man's-pedal blacklist so a
// plugin that crashes a scan is skipped next time instead of crashing again.
//
// Format support follows the JUCE_PLUGINHOST_* flags set in CMake: VST3 always,
// AU on macOS. JUCE 8 has no CLAP hosting of its own, so CLAP is added as a
// custom format (ClapPluginFormat.h/.cpp, on clap-juce-extensions' CLAP headers).
//
// Owned by the processor (survives editor open/close). GUI-free except scan(),
// which puts up JUCE's own modal progress window.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class PluginHost {
public:
    PluginHost();

    juce::KnownPluginList&          knownList() { return list_; }
    juce::AudioPluginFormatManager& formats()   { return fmt_; }

    // Full rescan of every format's default locations, on a background thread
    // with a modal progress window. Persists the refreshed list to the cache.
    void scan();

    // Add a single plugin file (e.g. a user-picked .vst3) to the known list and
    // return the plugin type(s) it contains (empty on failure). Persists the cache.
    juce::Array<juce::PluginDescription> addFile(const juce::String& path);

    // Instantiate a plugin by description. Returns nullptr and sets `error` on
    // failure. Slow: call from the message thread, never the audio thread.
    std::unique_ptr<juce::AudioPluginInstance>
    create(const juce::PluginDescription& desc, double sampleRate, int blockSize,
           juce::String& error);

private:
    juce::File cacheFile() const;
    juce::File pedalFile() const;
    void save();

    juce::AudioPluginFormatManager fmt_;
    juce::KnownPluginList list_;
};
