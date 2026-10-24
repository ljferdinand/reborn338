// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// ClapPluginFormat: a juce::AudioPluginFormat that hosts CLAP plugins (fork
// P7). JUCE 8 ships no CLAP host format (clap-juce-extensions is export-only),
// so this bridges the CLAP C API to JUCE's plugin abstraction from scratch.
//
// The point of doing it as a real AudioPluginFormat: once it is registered with
// the AudioPluginFormatManager (see PluginHost), the ENTIRE host harness (the
// scan, the KnownPluginList, the picker, HostedInstrument, and the .rbsx session
// state) works for CLAP with no other changes, because all of that is written
// against AudioPluginFormat / AudioPluginInstance, not against VST3 directly.
//
// CLAP is MIT-licensed, so hosting it carries none of the VST3 SDK's terms.
//
#include <juce_audio_processors/juce_audio_processors.h>

class ClapPluginFormat : public juce::AudioPluginFormat {
public:
    ClapPluginFormat() = default;

    juce::String getName() const override { return "CLAP"; }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning(const juce::PluginDescription&) override;
    bool doesPluginStillExist(const juce::PluginDescription&) override;
    bool canScanForPlugins() const override { return true; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins(const juce::FileSearchPath&, bool recursive,
                                            bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;
    bool requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription&) const override {
        return false;
    }

protected:
    void createPluginInstance(const juce::PluginDescription&, double initialSampleRate,
                              int initialBufferSize, PluginCreationCallback) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClapPluginFormat)
};
