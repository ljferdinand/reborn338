// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "PluginHost.h"
#if REBORN_HOST_CLAP
 #include "ClapPluginFormat.h"
#endif

PluginHost::PluginHost() {
    fmt_.addDefaultFormats();   // VST3 (+ AU on macOS) per the JUCE_PLUGINHOST_* flags
   #if REBORN_HOST_CLAP
    // CLAP hosting (fork P7): JUCE has no built-in CLAP format, so add our own.
    // Registering it here means the scan, picker, HostedInstrument and session
    // state all handle CLAP with no further changes.
    fmt_.addFormat(new ClapPluginFormat());
   #endif
    if (auto xml = juce::parseXML(cacheFile().loadFileAsString()))
        list_.recreateFromXml(*xml);
    cacheFile().getParentDirectory().createDirectory();
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(list_, pedalFile());
}

juce::File PluginHost::cacheFile() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Reborn338").getChildFile("plugin_cache.xml");
}

juce::File PluginHost::pedalFile() const {
    return cacheFile().getSiblingFile("scan_pedal.txt");
}

void PluginHost::save() {
    cacheFile().getParentDirectory().createDirectory();
    if (auto xml = list_.createXml())
        cacheFile().replaceWithText(xml->toString({}));
}

void PluginHost::scan() {
    struct ScanJob : juce::ThreadWithProgressWindow {
        ScanJob(juce::AudioPluginFormatManager& fm, juce::KnownPluginList& kl,
                const juce::File& pedal)
            : ThreadWithProgressWindow("Scanning for plugins...", true, true),
              mgr(fm), list(kl), pedalFile(pedal) {}
        void run() override {
            juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(list, pedalFile);
            for (auto* format : mgr.getFormats()) {
                juce::PluginDirectoryScanner scanner(list, *format,
                    format->getDefaultLocationsToSearch(), true, pedalFile);
                juce::String name;
                while (!threadShouldExit() && scanner.scanNextFile(true, name)) {
                    setStatusMessage(format->getName() + ": " + name);
                    setProgress(scanner.getProgress());
                }
            }
        }
        juce::AudioPluginFormatManager& mgr;
        juce::KnownPluginList& list;
        juce::File pedalFile;
    };
    ScanJob job(fmt_, list_, pedalFile());
    job.runThread();
    save();
}

juce::Array<juce::PluginDescription> PluginHost::addFile(const juce::String& path) {
    juce::Array<juce::PluginDescription> out;
    // Dead-man's-pedal for the single-file path. scan() gets this from
    // PluginDirectoryScanner, but scanAndAddFile does not: record the file BEFORE
    // probing it (loading a plugin binary runs its init/factory, which can crash),
    // then clear it on success. If the probe crashes, the entry survives and the
    // ctor's applyBlacklistingsFromDeadMansPedal blacklists it on the next launch
    // instead of crashing the add again.
    juce::StringArray pedal;
    pedal.addLines(pedalFile().loadFileAsString());
    pedal.addIfNotAlreadyThere(path);
    pedalFile().replaceWithText(pedal.joinIntoString("\n"));

    for (auto* format : fmt_.getFormats()) {
        juce::OwnedArray<juce::PluginDescription> found;
        list_.scanAndAddFile(path, /*dontRescanIfAlreadyInList=*/true, found, *format);
        for (auto* d : found) out.add(*d);
    }

    pedal.removeString(path);               // survived the probe: clear the entry
    pedalFile().replaceWithText(pedal.joinIntoString("\n"));

    if (!out.isEmpty()) save();
    return out;
}

std::unique_ptr<juce::AudioPluginInstance>
PluginHost::create(const juce::PluginDescription& desc, double sampleRate,
                   int blockSize, juce::String& error) {
    return fmt_.createPluginInstance(desc, sampleRate, blockSize, error);
}
