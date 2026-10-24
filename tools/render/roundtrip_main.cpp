// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// reborn_roundtrip: load .rbs, save it, reload, and diff the decoded model.
//
// The acceptance harness for the .rbs writer (docs/FORMAT_RBSX.md): run it
// over the shipped songs to prove a save/reload preserves what we decode.
//
// It compares everything the engine plays and everything the panel holds: the
// patterns, the arrangement bar by bar, the automation, the fader and pan
// bytes, every EffectInit field of both the start state and the panel, the
// knob headers and the GLOB bytes. Until 2026-09-26 it compared patterns,
// tempo, skin and loop length only, which is how a writer that dropped the
// whole arrangement and all automation passed it on every song (review T1).
// Floats the file stores as a byte are compared as that byte, since the byte is
// all a save can carry. It also saves the reloaded song again and requires the
// same bytes, so repeated saves cannot drift. A track saved without a tick-0
// dump comes back with one (the writer restates every start), and those added
// tick-0 events are counted as a note rather than failed; see compareAutomation.
//
//   reborn_roundtrip [--save-dir DIR] <song.rbs> [...]
//
// --save-dir writes each re-saved song as DIR/<basename>.rbs, so the original
// and the copy can be rendered and compared.
//
#include "reborn/rbs.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

using namespace reborn;

static int g_bad = 0;

static void want(bool ok, const std::string& what) {
    if (!ok) { std::printf("    MISMATCH: %s\n", what.c_str()); ++g_bad; }
}

static int kb(float v) {                            // the byte the writer stores
    return (int) std::lround(std::max(0.0f, std::min(127.0f, v * 127.0f)));
}

static bool sameBass(const BassPattern& a, const BassPattern& b) {
    if (a.length != b.length || a.shuffle != b.shuffle) return false;
    // Every step, rests included: a rest still names the note the oscillator
    // tunes to, so its note and octave are played data too.
    for (int i = 0; i < 16; ++i) {
        const BassStep& x = a.steps[i];
        const BassStep& y = b.steps[i];
        if (x.gate != y.gate || x.note != y.note || x.octave != y.octave
            || x.accent != y.accent || x.slide != y.slide || x.octBoth != y.octBoth) return false;
    }
    return true;
}

static bool sameDrum(const DrumPattern& a, const DrumPattern& b) {
    if (a.length != b.length || a.shuffle != b.shuffle) return false;
    for (int i = 0; i < 16; ++i) {
        const DrumStep& x = a.steps[i];
        const DrumStep& y = b.steps[i];
        // accentValue sets the 909's accent depth, so it is compared as well.
        if (x.on != y.on || x.accent != y.accent || x.flam != y.flam
            || x.soft != y.soft || x.accentValue != y.accentValue) return false;
    }
    return true;
}

// The arrangement expanded to one entry per bar. The loader merges identical
// consecutive bars into one row, so rows may legitimately differ; bars may not.
static std::vector<std::array<int, 4>> perBar(const Song& s) {
    std::vector<std::array<int, 4>> v;
    for (const SongStep& r : s.arrangement)
        for (int i = 0; i < std::max(1, r.repeats) && v.size() < (1u << 20); ++i)
            v.push_back({ r.bass1Pattern, r.bass2Pattern, r.drum808Pattern, r.drum909Pattern });
    return v;
}

using AutoKey = std::tuple<uint32_t, int, int, int>;   // tick, target, param, value
static AutoKey key(const AutoEvent& e) {
    return AutoKey(e.tick, int(e.target), int(e.param), int(e.value));
}

// The writer states every instrument track's start in a tick-0 dump, which the
// loader reads back, so a track whose original carried no dump (a generated
// fixture names only its pattern) comes back with one. Those restated tick-0
// events play the knob header's values, which the song already started from, so
// they are reported as a note and not as a mismatch. A lost event, any change past
// tick 0, and any tick-0 change to a parameter the original stated still fail.
// That the restated values are right is shown by rendering the original and the
// copy (--save-dir) and comparing them sample for sample, as CI does.
static int g_restated = 0;

static void compareAutomation(const Song& a, const Song& b) {
    std::vector<std::pair<int, int>> stated;           // (target, param) at tick 0
    for (const AutoEvent& e : a.automation)
        if (e.tick == 0) stated.emplace_back(int(e.target), int(e.param));
    std::sort(stated.begin(), stated.end());
    std::vector<AutoKey> x, y;
    for (const AutoEvent& e : a.automation) x.push_back(key(e));
    int restated = 0;
    for (const AutoEvent& e : b.automation) {
        if (e.tick == 0 && !std::binary_search(stated.begin(), stated.end(),
                                                std::make_pair(int(e.target), int(e.param)))) {
            ++restated;
            continue;
        }
        y.push_back(key(e));
    }
    if (restated) {
        std::printf("    note: %d tick-0 event(s) restated by the writer's start dump\n", restated);
        g_restated += restated;
    }
    // In vector order first: the engine applies events in this order, so an
    // order change at one tick is a playback change even when nothing is lost.
    bool sameOrder = (x == y);
    std::sort(x.begin(), x.end());
    std::sort(y.begin(), y.end());
    if (x != y) {
        std::vector<AutoKey> lost, gained;
        std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(lost));
        std::set_difference(y.begin(), y.end(), x.begin(), x.end(), std::back_inserter(gained));
        char msg[256];
        std::snprintf(msg, sizeof msg,
                      "automation multiset: %zu events to %zu, %zu lost, %zu gained",
                      x.size(), y.size(), lost.size(), gained.size());
        want(false, msg);
        const AutoKey& k = !lost.empty() ? lost[0] : gained[0];
        std::printf("      first %s: tick %u target %d param %d value %d\n",
                    !lost.empty() ? "lost" : "gained", std::get<0>(k), std::get<1>(k),
                    std::get<2>(k), std::get<3>(k));
        return;
    }
    want(sameOrder, "automation order (same events, different sequence)");
}

static void compareFx(const EffectInit& x, const EffectInit& y, const std::string& w) {
    want(x.present == y.present, w + " present");
    want(faderByteFromGain(x.masterVolume) == faderByteFromGain(y.masterVolume),
         w + " master fader byte");
    for (int c = 0; c < 4; ++c) {
        const std::string ch = w + " ch" + std::to_string(c) + " ";
        want(x.channelOn[c] == y.channelOn[c], ch + "channelOn");
        want(kb(x.delaySend[c]) == kb(y.delaySend[c]), ch + "delaySend");
        want(x.distOn[c] == y.distOn[c], ch + "distOn");
    }
    want(x.distortionOn == y.distortionOn, w + " distortionOn");
    want(kb(x.distAmount) == kb(y.distAmount), w + " distAmount");
    want(kb(x.distTone) == kb(y.distTone), w + " distTone");
    want(x.pcfTarget == y.pcfTarget, w + " pcfTarget");
    want(x.pcfOn == y.pcfOn, w + " pcfOn");
    want(kb(x.pcfFreq) == kb(y.pcfFreq), w + " pcfFreq");
    want(kb(x.pcfQ) == kb(y.pcfQ), w + " pcfQ");
    want(kb(x.pcfAmt) == kb(y.pcfAmt), w + " pcfAmt");
    want(kb(x.pcfDecay) == kb(y.pcfDecay), w + " pcfDecay");
    want(x.pcfPattern == y.pcfPattern, w + " pcfPattern");
    want(x.pcfBandpass == y.pcfBandpass, w + " pcfBandpass");
    want(x.compTarget == y.compTarget, w + " compTarget");
    want(x.delaySteps == y.delaySteps, w + " delaySteps");
    want(kb(x.delayFeedback) == kb(y.delayFeedback), w + " delayFeedback");
    want(x.delayStraight == y.delayStraight, w + " delayStraight (grid)");
    want(x.delayPan == y.delayPan, w + " delayPan");
    want(x.delayOn == y.delayOn, w + " delayOn");
    want(x.compressorOn == y.compressorOn, w + " compressorOn");
    want(kb(x.compRatio) == kb(y.compRatio), w + " compRatio");
    want(kb(x.compThreshold) == kb(y.compThreshold), w + " compThreshold");
}

static void compareMixer(const MixerInit& x, const MixerInit& y, const std::string& w) {
    want(x.present == y.present, w + " present");
    for (int c = 0; c < 4; ++c) {
        const std::string ch = w + " ch" + std::to_string(c) + " ";
        want(faderByteFromGain(x.volume[c], kChannelUnityGain)
             == faderByteFromGain(y.volume[c], kChannelUnityGain), ch + "fader byte");
        want(panByteFromPan(x.pan[c]) == panByteFromPan(y.pan[c]), ch + "pan byte");
    }
}

static void compareBassKnobs(const BassKnobs& x, const BassKnobs& y, const std::string& w) {
    want(x.present == y.present, w + " present");
    want(x.tuning == y.tuning, w + " tuning");
    want(kb(x.cutoff) == kb(y.cutoff), w + " cutoff");
    want(kb(x.resonance) == kb(y.resonance), w + " resonance");
    want(kb(x.envmod) == kb(y.envmod), w + " envmod");
    want(kb(x.decay) == kb(y.decay), w + " decay");
    want(kb(x.accent) == kb(y.accent), w + " accent");
    want((x.waveform >= 0.5f) == (y.waveform >= 0.5f), w + " waveform");
}

static void compareDrumKnobs(const DrumKnobs& x, const DrumKnobs& y, const std::string& w) {
    want(x.present == y.present, w + " present");
    want(kb(x.accent) == kb(y.accent), w + " accent");
    for (int c = 0; c < SC_COUNT; ++c)
        want(x.sharedPrimary[c] == y.sharedPrimary[c], w + " sharedPrimary " + std::to_string(c));
    for (int v = 0; v < 11; ++v) {
        // -1 means "not in the header"; anything else is a byte.
        const bool same = (x.level[v] < 0.0f || y.level[v] < 0.0f)
                        ? x.level[v] == y.level[v] : kb(x.level[v]) == kb(y.level[v]);
        want(same, w + " level " + std::to_string(v));
    }
    const std::pair<const char*, float DrumKnobs::*> knobs[] = {
        { "bdTune", &DrumKnobs::bdTune },   { "bdAttack", &DrumKnobs::bdAttack },
        { "bdDecay", &DrumKnobs::bdDecay }, { "sdTune", &DrumKnobs::sdTune },
        { "sdTone", &DrumKnobs::sdTone },   { "sdSnappy", &DrumKnobs::sdSnappy },
        { "tomTune", &DrumKnobs::tomTune }, { "tomDecay", &DrumKnobs::tomDecay },
        { "ltTune", &DrumKnobs::ltTune },   { "mtTune", &DrumKnobs::mtTune },
        { "htTune", &DrumKnobs::htTune },   { "ltDecay", &DrumKnobs::ltDecay },
        { "mtDecay", &DrumKnobs::mtDecay }, { "htDecay", &DrumKnobs::htDecay },
        { "chDecay", &DrumKnobs::chDecay }, { "ohDecay", &DrumKnobs::ohDecay },
        { "cymTune", &DrumKnobs::cymTune }, { "rdTune", &DrumKnobs::rdTune },
        { "crDecay", &DrumKnobs::crDecay }, { "rdDecay", &DrumKnobs::rdDecay },
        { "flam", &DrumKnobs::flam },
    };
    for (const auto& k : knobs)
        want(kb(x.*(k.second)) == kb(y.*(k.second)), w + " " + k.first);
}

static std::string baseName(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

int main(int argc, char** argv) {
    std::string saveDir;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--save-dir" && i + 1 < argc) saveDir = argv[++i];
        else paths.push_back(a);
    }
    if (paths.empty()) {
        std::printf("usage: reborn_roundtrip [--save-dir DIR] <song.rbs> [...]\n");
        return 2;
    }
    int files = 0, skipped = 0;
    for (const std::string& path : paths) {
        Song a; std::string err;
        if (!loadRbsFile(path, a, &err)) {
            std::printf("  SKIP %s (%s)\n", path.c_str(), err.c_str());
            ++skipped;
            continue;
        }
        ++files;
        std::vector<unsigned char> buf;
        if (!saveRbs(a, buf, &err)) {
            std::printf("  FAIL save %s: %s\n", path.c_str(), err.c_str());
            ++g_bad; continue;
        }
        Song b;
        if (!loadRbs(buf.data(), buf.size(), b, &err)) {
            std::printf("  FAIL reload %s: %s\n", path.c_str(), err.c_str());
            ++g_bad; continue;
        }
        std::printf("  %s\n", path.c_str());
        if (!saveDir.empty()) {
            const std::string out = saveDir + "/" + baseName(path);
            std::FILE* f = std::fopen(out.c_str(), "wb");
            const bool ok = f && std::fwrite(buf.data(), 1, buf.size(), f) == buf.size();
            if (f) std::fclose(f);
            want(ok, "could not write " + out);
        }

        want(std::fabs(a.tempo - b.tempo) < 0.01, "tempo");
        want(kb(a.shuffle) == kb(b.shuffle), "shuffle");
        want(a.skinName == b.skinName, "skin name");
        want(a.title == b.title, "title");
        want(a.author == b.author, "author");
        want(a.loopBars == b.loopBars || a.loopBars == 0, "loop bars");
        // GLOB is passed through (Song::globRaw), so an unedited song must
        // save back its own bytes, the undecoded ones included.
        want(!a.globRaw.empty() && a.globRaw == b.globRaw, "GLOB bytes");

        int bad303 = 0, bad808 = 0, bad909 = 0;
        for (int p = 0; p < 32; ++p) {
            if (!sameBass(a.bass1.at(p), b.bass1.at(p))) ++bad303;
            if (!sameBass(a.bass2.at(p), b.bass2.at(p))) ++bad303;
            if (!sameDrum(a.drum808.at(p), b.drum808.at(p))) ++bad808;
            if (!sameDrum(a.drum909.at(p), b.drum909.at(p))) ++bad909;
        }
        if (bad303) { std::printf("    MISMATCH: %d/64 303 patterns\n", bad303); ++g_bad; }
        if (bad808) { std::printf("    MISMATCH: %d/32 808 patterns\n", bad808); ++g_bad; }
        if (bad909) { std::printf("    MISMATCH: %d/32 909 patterns\n", bad909); ++g_bad; }

        const auto barsA = perBar(a), barsB = perBar(b);
        if (barsA != barsB) {
            std::size_t i = 0;
            while (i < barsA.size() && i < barsB.size() && barsA[i] == barsB[i]) ++i;
            want(false, "arrangement: " + std::to_string(a.arrangement.size()) + " rows ("
                 + std::to_string(barsA.size()) + " bars) to " + std::to_string(b.arrangement.size())
                 + " rows (" + std::to_string(barsB.size()) + " bars), first bad bar "
                 + std::to_string(i));
        }
        compareAutomation(a, b);

        compareFx(a.effectInit, b.effectInit, "effectInit");
        compareFx(a.effectPanel, b.effectPanel, "effectPanel");
        compareMixer(a.mixerInit, b.mixerInit, "mixerInit");
        compareMixer(a.mixerPanel, b.mixerPanel, "mixerPanel");
        compareBassKnobs(a.bass1Knobs, b.bass1Knobs, "bass1Knobs");
        compareBassKnobs(a.bass2Knobs, b.bass2Knobs, "bass2Knobs");
        compareDrumKnobs(a.drum808Knobs, b.drum808Knobs, "drum808Knobs");
        compareDrumKnobs(a.drum909Knobs, b.drum909Knobs, "drum909Knobs");

        // A reloaded song must save back to the same bytes: the second
        // generation is a fixed point, so saving twice cannot drift.
        std::vector<unsigned char> again;
        want(saveRbs(b, again, &err) && again == buf, "re-save is not a fixed point");
    }
    // A skip has to be as loud as a failure. "17 file(s) round-tripped, 0
    // problem(s)" was reported for months over a set of EIGHTEEN, because a
    // filename with an apostrophe never reached the loader and the one SKIP line
    // scrolled past above the summary. Nothing was wrong with that song; the
    // verification was just quietly covering one less file than anyone thought.
    std::printf("\n%d file(s) round-tripped, %d problem(s)", files, g_bad);
    if (g_restated) std::printf(", %d tick-0 event(s) restated", g_restated);
    if (skipped) std::printf(", %d SKIPPED (not verified)", skipped);
    std::printf("\n");
    return (g_bad == 0 && skipped == 0) ? 0 : 1;
}
