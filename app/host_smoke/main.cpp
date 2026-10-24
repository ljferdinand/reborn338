// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// reborn_host_smoke: headless checks for the 303 host harness (fork P5).
//
// Two modes:
//   reborn_host_smoke <plugin.vst3>
//       Load a VST3 through PluginHost, wrap it in HostedInstrument, drive one
//       note, and assert it sounds then silences. A pass/fail gate, no GUI.
//
//   reborn_host_smoke --render <plugin.vst3> <song.rbs> <out.wav> [seconds]
//       Install the hosted plugin into a real reborn::Engine's bass-1 slot and
//       render a song's 303-1 line through the STEP SEQUENCER to a WAV, exactly
//       as the app would. This exercises the whole trigger/release/slide/accent
//       MIDI translation and produces audio to listen to. Bass 1 is soloed and
//       the effect rack is off, matching reborn_render --solo bass1 --no-fx.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include "../src/PluginHost.h"
#include "../src/HostedInstrument.h"
#include "reborn/audio.h"
#include "reborn/engine.h"
#include "reborn/factories.h"
#include "reborn/rbs.h"
#include "reborn/rbsx.h"
#include "reborn/wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace reborn;

static double blockRms(const StereoBlock& b, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += (double) b.left[(std::size_t) i] * b.left[(std::size_t) i];
    return std::sqrt(s / (n > 0 ? n : 1));
}

// Mode 1: load, one note, assert it sounds then silences.
static int runNoteTest(PluginHost& host, const juce::String& path) {
    const auto added = host.addFile(path);
    if (added.isEmpty()) { std::printf("FAIL: no plugin found at '%s'\n", path.toRawUTF8()); return 1; }

    juce::String err;
    auto inst = host.create(added.getReference(0), 44100.0, 512, err);
    if (inst == nullptr) { std::printf("FAIL: create: %s\n", err.toRawUTF8()); return 1; }

    const juce::String name = inst->getName();
    HostedInstrument voice(std::move(inst), name);
    voice.prepare(44100.0);

    StereoBlock held; held.resize(44100);
    voice.setStepTiming(0.25, false);
    voice.trigger(57, 1.0f, /*accent=*/false, /*slide=*/false);   // A3
    voice.process(held, 44100);
    const double heldRms = blockRms(held, 44100);

    voice.release();
    StereoBlock settle; settle.resize(8820);  voice.process(settle, 8820);
    StereoBlock tail;   tail.resize(22050);    voice.process(tail, 22050);
    const double tailRms = blockRms(tail, 22050);

    std::printf("host smoke: plugin='%s' params=%d heldRms=%.6f tailRms=%.6f\n",
                name.toRawUTF8(), voice.numParams(), heldRms, tailRms);
    const bool ok = heldRms > 1.0e-3 && tailRms < heldRms * 0.5;
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Mode 2: sequence a song's 303-1 line through the engine hosting the plugin.
static int runRender(PluginHost& host, const juce::String& pluginPath,
                     const std::string& songPath, const std::string& outPath,
                     double seconds) {
    Song song; std::string serr;
    if (!loadRbsFile(songPath, song, &serr)) {
        std::printf("FAIL: load song '%s': %s\n", songPath.c_str(), serr.c_str());
        return 1;
    }
    const auto added = host.addFile(pluginPath);
    if (added.isEmpty()) { std::printf("FAIL: no plugin at '%s'\n", pluginPath.toRawUTF8()); return 1; }
    juce::String perr;
    auto inst = host.create(added.getReference(0), 44100.0, 512, perr);
    if (inst == nullptr) { std::printf("FAIL: create: %s\n", perr.toRawUTF8()); return 1; }
    const juce::String pname = inst->getName();

    const int sr = 44100;
    Engine engine;
    engine.prepare(sr);
    engine.loadSong(song);
    // Install the hosted plugin into the 303-1 slot, exactly as the app does.
    // (setInstrument reapplies the song's bass-1 knobs by name; a third-party
    // synth's names will not match, so it plays its default patch. Fine, this
    // tests the SEQUENCING, not knob parity.)
    engine.setInstrument(kBass1, std::make_unique<HostedInstrument>(std::move(inst), pname));

    for (int c = 0; c < kNumChannels; ++c) {
        engine.channel(c).distEnable = false;   // --no-fx
        engine.channel(c).delaySend  = 0.0f;
    }
    engine.soloMask = (1 << kBass1);             // --solo bass1
    engine.play();

    const std::size_t frames = (std::size_t) (seconds * sr);
    StereoBlock block(frames);
    const int bs = 512;
    for (std::size_t pos = 0; pos < frames; pos += bs) {
        const int n = (int) std::min<std::size_t>(bs, frames - pos);
        StereoBlock chunk(n);
        engine.process(chunk, n);
        for (int i = 0; i < n; ++i) {
            block.left[pos + (std::size_t) i]  = chunk.left[(std::size_t) i];
            block.right[pos + (std::size_t) i] = chunk.right[(std::size_t) i];
        }
    }
    if (!writeWav16(outPath, block, sr)) { std::printf("FAIL: write '%s'\n", outPath.c_str()); return 1; }
    std::printf("rendered '%s' 303-1 hosting '%s' -> %s (%.1fs, rms %.4f)\n",
                song.title.empty() ? songPath.c_str() : song.title.c_str(),
                pname.toRawUTF8(), outPath.c_str(), seconds, blockRms(block, (int) frames));
    return 0;
}

// Mode 3: prove a hosted plugin's STATE survives a session round-trip (fork P6).
// Change a parameter, capture the plugin's state into a session, serialise and
// re-parse the session bytes, re-find the plugin by identifier, re-instantiate
// it and restore the state, then assert the parameter came back.
static int runStateRoundtrip(PluginHost& host, const juce::String& path) {
    const auto added = host.addFile(path);
    if (added.isEmpty()) { std::printf("FAIL: no plugin found at '%s'\n", path.toRawUTF8()); return 1; }
    const juce::PluginDescription desc = added.getReference(0);
    const juce::String id = desc.createIdentifierString();

    juce::String err;
    auto a = host.create(desc, 44100.0, 512, err);
    if (a == nullptr) { std::printf("FAIL: create A: %s\n", err.toRawUTF8()); return 1; }
    if (a->getParameters().isEmpty()) { std::printf("FAIL: plugin exposes no parameters\n"); return 1; }

    auto* pa = a->getParameters()[0];
    const float want = pa->getValue() < 0.5f ? 0.9f : 0.1f;   // a distinctive value
    pa->setValue(want);
    // Deliver the change the way a running host does. A CLAP plugin receives
    // parameter changes through processing (or params.flush), not just a setter,
    // so run one block before capturing state, exactly what the live app does.
    a->prepareToPlay(44100.0, 512);
    { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer mb; b.clear(); a->processBlock(b, mb); }
    juce::MemoryBlock stateA; a->getStateInformation(stateA);

    reborn::Session s;
    s.bass[0].engine   = (int) reborn::Bass303Engine::HostedPlugin;
    s.bass[0].pluginId = id.toStdString();
    s.bass[0].pluginState.assign((const unsigned char*) stateA.getData(),
                                 (const unsigned char*) stateA.getData() + stateA.getSize());
    std::vector<unsigned char> buf;
    if (!reborn::saveRbsxToMemory(buf, s)) { std::printf("FAIL: saveRbsxToMemory\n"); return 1; }
    reborn::Session s2;
    if (!reborn::loadRbsxFromMemory(buf.data(), buf.size(), s2, nullptr)) {
        std::printf("FAIL: loadRbsxFromMemory\n"); return 1;
    }

    auto desc2 = host.knownList().getTypeForIdentifierString(juce::String(s2.bass[0].pluginId));
    if (desc2 == nullptr) { std::printf("FAIL: plugin id not found on reload\n"); return 1; }
    auto b = host.create(*desc2, 44100.0, 512, err);
    if (b == nullptr) { std::printf("FAIL: create B: %s\n", err.toRawUTF8()); return 1; }
    b->setStateInformation(s2.bass[0].pluginState.data(), (int) s2.bass[0].pluginState.size());
    const float got = b->getParameters()[0]->getValue();

    std::printf("state roundtrip: plugin='%s' param0 set=%.3f restored=%.3f (%d-byte blob)\n",
                desc.name.toRawUTF8(), want, got, (int) stateA.getSize());
    const bool ok = std::abs(got - want) < 1.0e-3f
                 && s2.bass[0].engine == (int) reborn::Bass303Engine::HostedPlugin
                 && s2.bass[0].pluginId == id.toStdString();
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;   // plugin instantiation needs this
    PluginHost host;

    if (argc >= 3 && std::strcmp(argv[1], "--state-roundtrip") == 0)
        return runStateRoundtrip(host, juce::CharPointer_UTF8(argv[2]));

    if (argc >= 5 && std::strcmp(argv[1], "--render") == 0) {
        const double secs = argc >= 6 ? std::atof(argv[5]) : 8.0;
        return runRender(host, juce::CharPointer_UTF8(argv[2]), argv[3], argv[4], secs);
    }
    if (argc < 2) {
        std::printf("usage:\n"
                    "  reborn_host_smoke <plugin.vst3>\n"
                    "  reborn_host_smoke --render <plugin.vst3> <song.rbs> <out.wav> [seconds]\n");
        return 2;
    }
    return runNoteTest(host, juce::CharPointer_UTF8(argv[1]));
}
