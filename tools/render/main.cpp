// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// reborn_render - headless verification harness.
//
// Renders the engine to a WAV file with no audio device or GUI, so the build
// can be verified deterministically. With no --song it renders a built-in acid
// demo (303 line + 808 beat + distortion); --song loads a .rbs file. Run it with
// --help for every option.
//
#include "reborn/engine.h"
#include "reborn/factories.h"   // makeBass303 + Bass303Engine (REBORN_BASS_ENGINE A/B)
#include "reborn/wav.h"
#include "reborn/rbs.h"
#include "reborn/sampleset.h"
#include "reborn/rbm.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <initializer_list>

using namespace reborn;

static void on(DrumPattern& p, int voice, std::initializer_list<int> steps) {
    for (int s : steps) p.steps[s].setOn(voice, true);
}

static Song makeDemoSong() {
    Song song;
    song.title = "Reborn Demo";
    song.tempo = 130.0;

    // --- Bass 1: a sliding, accented acid line in A ---
    BassPattern& b = song.bass1.patterns[0];
    b.length = 16; b.octaveBase = 3;               // low register
    auto setStep = [&](int i,int note,int oct,bool acc,bool slide){
        b.steps[i].gate=true; b.steps[i].note=note; b.steps[i].octave=oct;
        b.steps[i].accent=acc; b.steps[i].slide=slide;
    };
    // A(9) C(0,+1) etc: a classic 303 wiggle
    setStep(0, 9,0,true,false);
    setStep(1, 9,0,false,true);
    setStep(2, 9,1,false,false);
    setStep(3, 0,1,false,true);
    setStep(4, 9,0,true,false);
    setStep(6, 7,0,false,true);
    setStep(7, 9,0,false,false);
    setStep(8, 9,0,true,false);
    setStep(9, 9,1,false,true);
    setStep(10,12,0,false,false);
    setStep(11, 9,0,false,false);
    setStep(12, 5,0,true,false);
    setStep(14, 9,0,false,true);
    setStep(15, 0,1,false,false);

    // --- 808 beat ---
    DrumPattern& d = song.drum808.patterns[0];
    d.length = 16;
    on(d, 0,  {0,4,8,12});           // Bass Drum four-on-floor
    on(d, 1,  {4,12});               // Snare
    on(d, 14, {0,2,4,6,8,10,12,14}); // Closed Hat 8ths
    on(d, 13, {14});                 // Open Hat offbeat
    d.steps[4].setAccent(1,true);
    d.steps[12].setAccent(1,true);

    return song;
}

static void usage() {
    std::printf(
        "usage: reborn_render [options]\n"
        "\n"
        "Renders Reborn to a 16-bit stereo WAV with no audio device. With no --song it\n"
        "plays a built-in demo (an acid line on bass 1 and an 808 beat).\n"
        "\n"
        "  --song FILE.rbs    render a ReBirth song file instead of the demo\n"
        "  --seconds N        length to render (default 8)\n"
        "  --sr N             sample rate (default 44100)\n"
        "  --out FILE.wav     output file (default reborn_out.wav)\n"
        "  --solo DEVICE      render one device alone: bass1, bass2, 808 or 909\n"
        "  --no-fx            effect rack off: distortion unrouted, delay sends held at\n"
        "                     zero (the PCF and compressor still play as the song sets)\n"
        "  --no-dist          distortion unrouted only\n"
        "  --mute-drums       mute both drum machines\n"
        "  --samples DIR      drum samples from a folder of 16-bit WAVs with the\n"
        "                     canonical names (docs/SAMPLES.md)\n"
        "  --mod FILE.rbm     drum samples from a ReBirth mod (docs/MODS.md)\n"
        "  --mods DIR         find the mod a song names in this folder\n"
        "  --debug-bars       print the song's channels and arrangement\n"
        "  --help             this text\n"
        "\n"
        "Environment: REBORN_BASS_ENGINE=tpt|open303 swaps both 303 voices (the\n"
        "default is the native TPT voice). Other REBORN_* variables are measurement\n"
        "probes (docs/ENVIRONMENT_PROBES.md).\n");
}

int main(int argc, char** argv) {
    double seconds = 8.0;
    int sampleRate = 44100;
    std::string out = "reborn_out.wav";
    std::string songPath;
    bool debugBars = false, muteDrums = false, noDist = false, noFx = false;
    std::string solo;             // "", "bass1", "bass2", "808", "909"
    std::string samplesDir;       // optional drum sample set (canonical names)
    std::string modPath;          // optional .rbm mod (drum samples come from it)
    std::string modsDir;          // optional mods folder: resolve the song's own mod
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seconds") && i+1<argc) seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--sr") && i+1<argc) sampleRate = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i+1<argc) out = argv[++i];
        else if (!std::strcmp(argv[i], "--song") && i+1<argc) songPath = argv[++i];
        else if (!std::strcmp(argv[i], "--debug-bars")) debugBars = true;
        else if (!std::strcmp(argv[i], "--mute-drums")) muteDrums = true;
        else if (!std::strcmp(argv[i], "--solo") && i + 1 < argc) solo = argv[++i];
        else if (!std::strcmp(argv[i], "--samples") && i + 1 < argc) samplesDir = argv[++i];
        else if (!std::strcmp(argv[i], "--mod") && i + 1 < argc) modPath = argv[++i];
        else if (!std::strcmp(argv[i], "--mods") && i + 1 < argc) modsDir = argv[++i];
        else if (!std::strcmp(argv[i], "--no-dist")) noDist = true;
        // --no-fx: match a ReBirth export made with the effect rack switched
        // OFF, so a whole-song A/B compares instruments rather than effects.
        // It unroutes the distortion and holds every echo send at zero; the
        // PCF and the compressor still run as the song's tracks set and ride
        // them.
        else if (!std::strcmp(argv[i], "--no-fx")) { noDist = true; noFx = true; }
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) { usage(); return 0; }
        // An option this program does not know (or one missing its value) is reported and
        // skipped rather than refused, so scripts written against older versions still run.
        else std::fprintf(stderr, "reborn_render: ignoring unknown or incomplete option %s (see --help)\n", argv[i]);
    }

    Engine engine;
    engine.prepare(sampleRate);

    // Diagnostic 303-voice A/B override: REBORN_BASS_ENGINE = tpt | open303 swaps
    // both bass slots off the default. This override lives HERE, not in the shared
    // makeBass303, so a stray environment variable can never silently override the
    // app's engine picker. The byte-neutral parity guard drives the TPT path
    // through this. Done before loadSong, so loadSong applies the song's knobs to
    // the swapped-in voice.
    if (const char* e = std::getenv("REBORN_BASS_ENGINE")) {
        Bass303Engine which = kDefaultBass303Engine;
        if      (e[0] == 't') which = Bass303Engine::Native303Tpt;
        else if (e[0] == 'o') which = Bass303Engine::Open303;
        engine.setInstrument(kBass1, makeBass303(which));
        engine.setInstrument(kBass2, makeBass303(which));
    }

    Song song;
    if (!songPath.empty()) {
        std::string err;
        if (!loadRbsFile(songPath, song, &err)) {
            std::fprintf(stderr, "reborn_render: failed to load %s: %s\n", songPath.c_str(), err.c_str());
            return 2;
        }
        std::printf("reborn_render: loaded song \"%s\" by %s (tempo %.1f)\n",
                    song.title.c_str(), song.author.c_str(), song.tempo);
    } else {
        song = makeDemoSong();
        // Dirty up the bass with a touch of distortion.
        engine.channel(kBass1).distEnable = true;
        engine.distortion()->setParamByName("amount", 0.35f);
        engine.distortion()->setParamByName("tone", 0.6f);
    }

    engine.loadSong(song);

    // Optional drum sample set (shared loader; see src/io/sampleset.cpp).
    // A song names the skin/mod it was authored against; songs written with a
    // mod were composed using THAT mod's drum samples, so prefer it.
    if (modPath.empty() && !modsDir.empty() && !song.skinName.empty()) {
        std::string found = reborn::findModByName({ modsDir }, song.skinName);
        if (!found.empty()) {
            modPath = found;
            std::printf("reborn_render: song asks for mod \"%s\" -> %s\n",
                        song.skinName.c_str(), found.c_str());
        } else {
            // findModByName returns "" for the stock look as well as for a
            // genuinely missing mod; only the latter is worth warning about.
            std::string lower = song.skinName;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return (char) std::tolower(c); });
            if (lower.find("standard") != std::string::npos)
                std::printf("reborn_render: song uses the stock sample set (\"%s\")\n",
                            song.skinName.c_str());
            else
                std::printf("reborn_render: WARNING song asks for mod \"%s\" "
                            "but it is not in %s; using stock samples\n",
                            song.skinName.c_str(), modsDir.c_str());
        }
    }
    if (!modPath.empty()) {
        std::string err;
        int n = reborn::loadModSampleSet(engine, modPath, &err);
        if (n > 0)
            std::printf("reborn_render: mod sample set loaded from %s (%d voices)\n",
                        modPath.c_str(), n);
        else
            std::printf("reborn_render: WARNING mod load failed (%s): %s\n",
                        modPath.c_str(), err.c_str());
    }
    if (!samplesDir.empty()) {
        int n = reborn::loadSampleSet(engine, samplesDir);
        if (n > 0)
            std::printf("reborn_render: drum sample set loaded from %s (%d voices)\n",
                        samplesDir.c_str(), n);
        else
            std::printf("reborn_render: WARNING no samples found in %s\n", samplesDir.c_str());
    }

    if (muteDrums) {
        engine.channel(kDrum808).mute = true;
        engine.channel(kDrum909).mute = true;
    }
    if (!solo.empty()) {
        engine.soloMask = (solo == "bass1") ? (1 << kBass1)
                        : (solo == "bass2") ? (1 << kBass2)
                        : (solo == "808")   ? (1 << kDrum808)
                        : (solo == "909")   ? (1 << kDrum909) : 0;
    }
    engine.play();
    // After play(), which in Song mode puts the song's start state back,
    // routing and sends included, so a switch made before it would be undone.
    // Nothing between the two reads them: play() fires step 0, and the tick-0
    // automation it dispatches is the instrument tracks' only.
    if (noDist)
        for (int c = 0; c < kNumChannels; ++c) engine.channel(c).distEnable = false;
    if (noFx) {
        for (int c = 0; c < kNumChannels; ++c) engine.channel(c).delaySend = 0.0f;
        engine.disableSends = true;      // and keep them there against automation
    }

    if (debugBars) {
        std::printf("channel state: ");
        for (int c = 0; c < kNumChannels; ++c)
            std::printf("[ch%d vol=%.2f send=%.2f mute=%d] ", c,
                        engine.channel(c).volume, engine.channel(c).delaySend,
                        (int) engine.channel(c).mute);
        std::printf("\n");
        std::printf("arrangement rows: %zu (song mode %s)\n",
                    song.arrangement.size(), engine.songMode() ? "on" : "off");
        for (std::size_t r = 0; r < song.arrangement.size() && r < 24; ++r) {
            const SongStep& s = song.arrangement[r];
            std::printf("  row %2zu x%d: b1=%d b2=%d d808=%d d909=%d\n", r, s.repeats,
                        s.bass1Pattern, s.bass2Pattern, s.drum808Pattern, s.drum909Pattern);
        }
    }

    const std::size_t frames = std::size_t(seconds * sampleRate);
    StereoBlock block(frames);
    // Render in blocks to exercise the block-based path.
    const int bs = 512;
    for (std::size_t pos = 0; pos < frames; pos += bs) {
        int n = (int)std::min<std::size_t>(bs, frames - pos);
        StereoBlock chunk(n);
        engine.process(chunk, n);
        for (int i = 0; i < n; ++i) { block.left[pos+i]=chunk.left[i]; block.right[pos+i]=chunk.right[i]; }
    }

    if (!writeWav16(out, block, sampleRate)) {
        std::fprintf(stderr, "reborn_render: failed to write %s\n", out.c_str());
        return 1;
    }
    std::printf("reborn_render: wrote %s (%.2fs @ %d Hz, %zu frames)\n",
                out.c_str(), seconds, sampleRate, frames);
    return 0;
}
