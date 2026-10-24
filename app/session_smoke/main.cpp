// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// reborn_session_smoke: headless check of the FULL processor session path
// (fork P6). Instantiates the real RebornAudioProcessor, opens a .rbsx file
// through openAnyFile (the exact path the app's Open button uses, minus the
// editor), runs audio through the restored voices, then round-trips the whole
// rig through getStateInformation / setStateInformation on a second processor.
//
// This reproduces "opening a session with a hosted 303 crashes" without a GUI.
//   reborn_session_smoke <file.rbsx> [plugin-to-preregister.vst3]
//
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginProcessor.h"
#include "../src/HostedInstrument.h"
#include <cstdio>
#include <cstring>

static void runAudio(RebornAudioProcessor& p, int blocks) {
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < blocks; ++i) { buf.clear(); p.processBlock(buf, midi); }
}

// --load-plugin <plugin>: reproduce the app's "Load plugin" path minus the
// editor: addFile, create, wrap, installBassPlugin, then run engine audio
// through the hosted voice. Isolates a processor-side crash from an editor one.
static int runLoadPlugin(const juce::String& pluginPath) {
    RebornAudioProcessor proc;
    proc.prepareToPlay(44100.0, 512);
    const auto added = proc.pluginHost().addFile(pluginPath);
    if (added.isEmpty()) { std::printf("FAIL: no plugin at '%s'\n", pluginPath.toRawUTF8()); return 1; }
    const juce::PluginDescription desc = added.getReference(0);
    juce::String err;
    auto inst = proc.pluginHost().create(desc, proc.engine().sampleRate(), 512, err);
    if (inst == nullptr) { std::printf("FAIL: create: %s\n", err.toRawUTF8()); return 1; }
    proc.installBassPlugin(reborn::kBass1,
                           std::make_unique<HostedInstrument>(std::move(inst), desc.name),
                           desc.name, desc.createIdentifierString());
    std::printf("installed '%s' into bass1: engine=%d name='%s' params=%d\n",
                desc.name.toRawUTF8(), (int) proc.bassEngine(reborn::kBass1),
                proc.bassPluginName(reborn::kBass1).toRawUTF8(),
                proc.engine().instrument(reborn::kBass1)->numParams());

    // Exercise the plugin's OWN editor create/destroy (the win32 CLAP GUI embed
    // path). This is what "Open plugin editor" hits, and the only part that could
    // not be checked earlier. No window is shown; just construct and tear down.
    if (auto* h = dynamic_cast<HostedInstrument*>(proc.engine().instrument(reborn::kBass1))) {
        if (auto* pi = h->plugin()) {
            std::printf("plugin hasEditor=%d\n", (int) pi->hasEditor());
            if (pi->hasEditor()) {
                auto* ed = pi->createEditorIfNeeded();
                std::printf("createEditor -> %s\n", ed != nullptr ? "ok" : "null");
                delete ed;
                std::printf("editor destroyed cleanly\n");
            }
        }
    }

    // Construct Reborn's OWN editor over the loaded rig. Its constructor runs
    // rebuildPanels() over the hosted CLAP's parameters, the path the app hits
    // on load (the picker calls it right after installBassPlugin).
    { std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
      std::printf("reborn editor over hosted CLAP: %s\n", ed != nullptr ? "ok" : "null"); }
    std::printf("reborn editor destroyed cleanly\n");

    runAudio(proc, 500);   // ~5.8 s of engine audio through the hosted voice
    juce::MemoryBlock mb; proc.getStateInformation(mb);
    std::printf("ran audio + captured state (%d bytes). PASS (no crash)\n", (int) mb.getSize());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage:\n"
                    "  reborn_session_smoke <file.rbsx> [plugin.vst3]\n"
                    "  reborn_session_smoke --load-plugin <plugin.vst3|.clap>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered, so a crash keeps the trail
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (std::strcmp(argv[1], "--load-plugin") == 0 && argc >= 3)
        return runLoadPlugin(juce::CharPointer_UTF8(argv[2]));
    const juce::String path = juce::CharPointer_UTF8(argv[1]);
    const juce::File file(path);

    RebornAudioProcessor proc;
    proc.prepareToPlay(44100.0, 512);
    if (argc >= 3) proc.pluginHost().addFile(juce::CharPointer_UTF8(argv[2]));

    juce::String err;
    const bool ok = proc.openAnyFile(file, err);
    std::printf("openAnyFile: %s%s\n", ok ? "OK" : "FAIL",
                err.isEmpty() ? "" : (" (" + err + ")").toRawUTF8());
    if (!ok) return 1;
    std::printf("  bass1 engine=%d name='%s'\n",
                (int) proc.bassEngine(reborn::kBass1),
                proc.bassPluginName(reborn::kBass1).toRawUTF8());
    std::printf("  bass2 engine=%d name='%s'\n",
                (int) proc.bassEngine(reborn::kBass2),
                proc.bassPluginName(reborn::kBass2).toRawUTF8());
    // Which drums the restore installed: "no crash" alone cannot tell a mod that
    // loaded from one that was refused and fell back.
    std::printf("  drum samples: %d voices, mod '%s', note '%s'\n", proc.sampleVoices,
                proc.modName.toRawUTF8(), proc.pendingModNote.toRawUTF8());
    runAudio(proc, 200);   // ~2.3 s through the restored voices

    // Full DAW-state round-trip onto a fresh processor.
    juce::MemoryBlock mb;
    proc.getStateInformation(mb);
    std::printf("getStateInformation: %d bytes\n", (int) mb.getSize());

    RebornAudioProcessor proc2;
    proc2.prepareToPlay(44100.0, 512);
    if (argc >= 3) proc2.pluginHost().addFile(juce::CharPointer_UTF8(argv[2]));
    proc2.setStateInformation(mb.getData(), (int) mb.getSize());
    std::printf("  after setState: bass1 engine=%d name='%s'\n",
                (int) proc2.bassEngine(reborn::kBass1),
                proc2.bassPluginName(reborn::kBass1).toRawUTF8());
    std::printf("  drum samples: %d voices, mod '%s', note '%s'\n", proc2.sampleVoices,
                proc2.modName.toRawUTF8(), proc2.pendingModNote.toRawUTF8());
    runAudio(proc2, 200);

    std::printf("PASS (no crash)\n");
    return 0;
}
