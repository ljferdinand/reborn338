// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Writer for the original .rbs song format, the mirror of rbs.cpp.
//
// Layout is the one documented in docs/FORMAT_RBS.md and already proven by
// tools/rbsgen/rbsgen.py, whose generated songs the original program opens.
// This is a port of that authoring path into the engine so sessions edited in
// Reborn can be saved back out.
//
// A song holds two kinds of state, and the writer keeps them apart the way
// ReBirth does (review W1 to W3):
//   - the CHUNKS (MIXR, DELY, PCF , DIST, COMP and the instrument headers) are
//     the PANEL at save time: Song::mixerPanel / effectPanel and the knob
//     structs;
//   - the TRAKs are the recorded SONG. Each track's tick-0 dump is the start
//     state (Song::mixerInit / effectInit), the arrangement goes back as each
//     device's pattern-select timeline, and every Song::automation event goes
//     back onto the track it came from, in its original order.
// A loaded song saved without edits reloads to the same Song and renders to the
// same bytes, with one difference: every instrument track gets a tick-0 dump, so
// a track that had none (a generated fixture names only its pattern) comes back
// with its start stated as tick-0 events, which play the values it started from
// anyway. reborn_roundtrip (tools/render/roundtrip_main.cpp) checks the rest over
// every demo song, and CI renders original and copy to prove the sound is equal.
//
// Saving .rbs is still LOSSY in two ways (see docs/FORMAT_RBSX.md): the format
// has no field for hosted plugin chains, and its pattern records hold exactly
// 16 steps. rbsLossFor() reports what a given song would lose so the UI can
// warn before writing rather than discarding work silently.
//
#include "reborn/rbs.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <array>

namespace reborn {
namespace {

void putU32BE(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back((unsigned char)(v >> 24)); b.push_back((unsigned char)(v >> 16));
    b.push_back((unsigned char)(v >> 8));  b.push_back((unsigned char)v);
}

// One IFF chunk: 4-byte id, u32 BE length, payload, pad to even.
void putChunk(std::vector<unsigned char>& out, const char id[4],
              const std::vector<unsigned char>& body) {
    out.insert(out.end(), id, id + 4);
    putU32BE(out, (uint32_t) body.size());
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1) out.push_back(0);
}

// A CAT group wrapping already-serialised children.
void putGroup(std::vector<unsigned char>& out, const char type[4],
              const std::vector<unsigned char>& children) {
    std::vector<unsigned char> body(type, type + 4);
    body.insert(body.end(), children.begin(), children.end());
    putChunk(out, "CAT ", body);
}

unsigned char kb(float norm) {                     // 0..1 -> 0..127
    const float v = norm * 127.0f;
    return (unsigned char) std::lround(std::max(0.0f, std::min(127.0f, v)));
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int k303Header = 9,  k303Record = 34;
constexpr int k808Header = 30, k909Header = 31, kDrumRecord = 194;
constexpr int kBankPatterns = 32;
// Engine voice index for each of the 11 file columns (mirror of rbs.cpp).
const int k808VoiceMap[11] = { 0, 1, 2, 3, 4, 8, 9, 11, 12, 13, 14 };
const int k909VoiceMap[11] = { 0, 1, 2, 3, 4, 5, 6, 8, 7, 9, 10 };
constexpr unsigned kFlSlide = 0x01, kFlAccent = 0x02, kFlOctUp = 0x04,
                   kFlOctDown = 0x08, kFlRest = 0x10;

// One TRAK event: tick, param, value.
using Event = std::array<int, 3>;

constexpr int kTicksPerBar = 32;       // 1 tick = 1/32 note; a 16-step bar = 32
// The last tick a TRAK can carry. Deltas are VLQs and the loader accepts at
// most four VLQ bytes (28 bits), so a later tick would need a fifth byte and
// make the whole song unreadable. Real songs stay far below it: 2^28 ticks is
// over eight million bars.
constexpr int kMaxTick = 0x0FFFFFFF;

// MIXR +1 and mixer track param 1: 0 off, 1 the master strip, 2..5 sections 1..4.
int compTargetByte(int t) { return t == kCompMaster ? 1 : (t >= 0 && t <= 3) ? t + 2 : 0; }
// MIXR +2 and mixer track param 2: 0 off, 1..4 sections 1..4.
int pcfTargetByte(int t)  { return (t >= 0 && t <= 3) ? t + 1 : 0; }

std::vector<unsigned char> buildHead() {
    std::vector<unsigned char> h(256, 0);
    static const unsigned char kPrefix[9] = { 0x5B,0x54,0x5B,0x54,0xBC,0x04,0x01,0x00,0x00 };
    std::memcpy(h.data(), kPrefix, sizeof kPrefix);
    const char* note = "Reborn 338";
    std::memcpy(h.data() + 9, note, std::strlen(note));
    return h;
}

// GLOB. A loaded song brings its own payload (Song::globRaw) and only the
// fields we model are patched into it, so the bytes still under investigation
// (+0, +1, +6..+10) and the mod's download URL at +80 survive a save. A song
// built in Reborn gets a synthesised one.
std::vector<unsigned char> buildGlob(const Song& s, int arrangementBars) {
    const bool raw = !s.globRaw.empty();
    std::vector<unsigned char> g = raw
        ? std::vector<unsigned char>(s.globRaw.begin(), s.globRaw.end())
        : std::vector<unsigned char>(512, 0);
    if (!raw) { g[0] = 1; g[1] = 1; }
    if (g.size() < 80) g.resize(80, 0);        // room for every patched field

    // +2: tempo * 1000, u32 BE. Exact for any tempo the loader accepts.
    const uint32_t t = (uint32_t) std::lround(s.tempo * 1000.0);
    g[2] = (unsigned char)(t >> 24); g[3] = (unsigned char)(t >> 16);
    g[4] = (unsigned char)(t >> 8);  g[5] = (unsigned char)t;

    // +11: loop length in bars * 3 (see FORMAT_RBS.md). The exact meaning of
    // GLOB+11 is still under investigation. A stored value the loader already
    // reads as this song's loopBars is left as it is, remainder and all.
    const int rawLoop = (int(g[11]) << 8) | int(g[12]);
    const int rawBars = (rawLoop / 3 >= 1 && rawLoop / 3 <= 4096) ? rawLoop / 3 : 0;
    if (!raw || rawBars != s.loopBars) {
        const int bars = s.loopBars > 0 ? s.loopBars : std::max(1, arrangementBars);
        const uint16_t loop = (uint16_t) std::min(0xFFFF, bars * 3);
        g[11] = (unsigned char)(loop >> 8); g[12] = (unsigned char)loop;
    }

    g[14] = kb(s.shuffle);                     // +14: shuffle amount, exact

    // +15: the skin / mod name, compared as the loader folds it so that an
    // unedited Latin-1 name keeps its own bytes. The field runs to the URL at
    // +80, and a new name clears it first so no tail of the old one is left.
    if (!raw || skinNameFromGlob(s.globRaw) != s.skinName) {
        const std::string skin = s.skinName.empty() ? std::string("Standard ReBirth") : s.skinName;
        std::fill(g.begin() + 15, g.begin() + 80, (unsigned char) 0);
        const std::size_t n = std::min<std::size_t>(skin.size(), 64);
        std::memcpy(g.data() + 15, skin.data(), n);
    }
    return g;
}

std::vector<unsigned char> buildUsri(const Song& s) {
    std::vector<unsigned char> u(712, 0);
    const std::size_t a = std::min<std::size_t>(s.author.size(), 40);
    std::memcpy(u.data(), s.author.data(), a);
    std::string desc = s.title.empty() ? std::string() : ("\"" + s.title + "\"");
    const std::size_t d = std::min<std::size_t>(desc.size(), 712 - 41 - 1);
    std::memcpy(u.data() + 41, desc.data(), d);
    return u;
}

// The 303 knob header: [ver][?][tune][cutoff][reso][envmod][decay][accent]
// [waveform]. Header byte N is also the 303 TRAK's param N, so the track's
// synthesised tick-0 dump reads its values from here rather than a second table.
std::vector<unsigned char> header303(const BassKnobs& k) {
    std::vector<unsigned char> h(k303Header, 0);
    h[0] = 1;
    // tuning is a quantised +/-12 st slot table, not a continuous law; see
    // tuneByteFromSemitones() in reborn/rbs.h.
    h[2] = (unsigned char) tuneByteFromSemitones((int) std::lround(k.tuning));
    h[3] = kb(k.cutoff); h[4] = kb(k.resonance);
    h[5] = kb(k.envmod); h[6] = kb(k.decay); h[7] = kb(k.accent);
    h[8] = k.waveform >= 0.5f ? 1 : 0;
    return h;
}

std::vector<unsigned char> build303(Bank<BassPattern>& bank,
                                    const std::vector<unsigned char>& header) {
    std::vector<unsigned char> b(header);
    b.reserve(k303Header + kBankPatterns * k303Record);
    for (int r = 0; r < kBankPatterns; ++r) {
        BassPattern& p = bank.at(r);
        b.push_back(p.shuffle ? 1 : 0);
        b.push_back((unsigned char) std::max(1, std::min(16, p.length)));
        for (int i = 0; i < 16; ++i) {
            const BassStep& st2 = p.steps[i];
            unsigned flags = 0;
            if (!st2.gate) flags |= kFlRest;          // gate is inverted in the file
            if (st2.accent) flags |= kFlAccent;
            if (st2.slide)  flags |= kFlSlide;
            if (st2.octave > 0) flags |= kFlOctUp;
            else if (st2.octave < 0) flags |= kFlOctDown;
            else if (st2.octBoth) flags |= kFlOctUp | kFlOctDown;   // net 0, as the file had it
            b.push_back((unsigned char)(st2.note & 0xFF));
            b.push_back((unsigned char) flags);
        }
    }
    return b;
}

// The drum knob header. As on the 303, header byte N is the drum TRAK's param
// N (docs/NON_OBVIOUS_BEHAVIOUR.md), so the track's synthesised dump reads it from here.
std::vector<unsigned char> headerDrum(const DrumKnobs& k, bool is909) {
    const int headerLen = is909 ? k909Header : k808Header;
    std::vector<unsigned char> b(headerLen, 0);
    b[0] = 1; b[2] = kb(k.accent);
    // Header bytes are written at EXPLICIT offsets that mirror rbs.cpp's
    // decode808Knobs / decode909Knobs. A running counter was used here before
    // and silently drifted: the 808's five shared-column switch bytes were
    // never emitted, so everything from byte 19 on landed one or two bytes
    // early. Nothing caught it because the round-trip harness compared
    // patterns and tempo but not the knob header.
    auto at = [&](int i, float v, float dflt) {
        if (i >= 0 && i < headerLen) b[i] = kb(v < 0.0f ? dflt : v);
    };
    if (is909) {
        // [ver][?][accent] BD[L,T,A,D] SD[L,T,Tone,Snap] LT/MT/HT[L,T,D]
        // RS[L] CP[L] HH[L,CHdec,OHdec] CR[L,T] RD[L,T] [?,?]
        at(3,  k.level[0], 0.79f); at(4, k.bdTune, 0.5f); at(5, k.bdAttack, 0.5f); at(6, k.bdDecay, 0.5f);
        at(7,  k.level[1], 0.79f); at(8, k.sdTune, 0.5f); at(9, k.sdTone, 0.5f);   at(10, k.sdSnappy, 0.5f);
        // Each tom column carries its OWN Tune and Decay: LT 12/13, MT 15/16,
        // HT 18/19. Only the LT pair used to be written, so a save silently
        // flattened the mid and hi tom controls onto the low tom's.
        at(11, k.level[2], 0.79f); at(12, k.ltTune, 0.5f); at(13, k.ltDecay, 0.5f);
        at(14, k.level[3], 0.79f); at(15, k.mtTune, 0.5f); at(16, k.mtDecay, 0.5f);
        at(17, k.level[4], 0.79f); at(18, k.htTune, 0.5f); at(19, k.htDecay, 0.5f);
        at(20, k.level[5], 0.79f); at(21, k.level[6], 0.79f);
        at(22, k.level[7], 0.79f);                      // OH/CH share the HH level
        at(23, k.chDecay, 0.25f);  at(24, k.ohDecay, 0.5f);
        at(25, k.level[9], 0.79f); at(26, k.cymTune, 0.5f);
        // Crash and ride mirror as [Level, Tune]; byte 28 is the RIDE tune and
        // byte 29 the FLAM amount, neither of which used to be written.
        at(27, k.level[10], 0.79f); at(28, k.rdTune, 0.5f);
        at(29, k.flam, 0.5f);
    } else {
        // [ver][?][accent] BD[L,Tone,D] SD[L,Tone,Snap] LT/MT/HT[L,T,sw]
        // RS[L,sw] CP[L,sw] CB[L] CY[L,T,D] OH[L,D] CH[L] [?]
        at(3,  k.level[0], 0.79f); at(4, k.bdTune, 0.5f);  at(5, k.bdDecay, 0.5f);
        at(6,  k.level[1], 0.79f); at(7, k.sdTone, 0.5f);  at(8, k.sdSnappy, 0.5f);
        at(9,  k.level[2], 0.79f); at(10, k.ltTune, 0.5f);
        at(12, k.level[3], 0.79f); at(13, k.mtTune, 0.5f);
        at(15, k.level[4], 0.79f); at(16, k.htTune, 0.5f);
        at(18, k.level[5], 0.79f);
        at(20, k.level[6], 0.79f);
        at(22, k.level[7], 0.79f);                       // CB
        at(23, k.level[8], 0.79f); at(24, k.cymTune, 0.5f); at(25, k.crDecay, 0.5f);
        at(26, k.level[9], 0.79f); at(27, k.ohDecay, 0.5f);
        at(28, k.level[10], 0.79f);                      // CH
        // Shared-column switches (0 = alternate voice, 1 = primary).
        b[11] = k.sharedPrimary[SC_LT] ? 1 : 0;
        b[14] = k.sharedPrimary[SC_MT] ? 1 : 0;
        b[17] = k.sharedPrimary[SC_HT] ? 1 : 0;
        b[19] = k.sharedPrimary[SC_RS] ? 1 : 0;
        b[21] = k.sharedPrimary[SC_CP] ? 1 : 0;
    }
    return b;
}

std::vector<unsigned char> buildDrum(Bank<DrumPattern>& bank,
                                     const std::vector<unsigned char>& header, bool is909) {
    std::vector<unsigned char> b(header);
    const int* map = is909 ? k909VoiceMap : k808VoiceMap;
    for (int r = 0; r < kBankPatterns; ++r) {
        DrumPattern& p = bank.at(r);
        std::vector<unsigned char> rec(kDrumRecord, 0);
        rec[0] = p.shuffle ? 1 : 0;
        rec[1] = (unsigned char) std::max(1, std::min(16, p.length));
        for (int s = 0; s < 16; ++s) {
            unsigned char* step = rec.data() + 2 + s * 12;
            const DrumStep& ds = p.steps[s];
            bool anyOn = false, anyAccent = false;
            for (int c = 0; c < 11; ++c) {
                const int voice = map[c];
                if (!ds.voiceOn(voice)) continue;
                anyOn = true;
                // 909 cell values: 1 soft, 2 normal, 3+ flam (see the decoder).
                // Accent is column 0 on both machines.
                step[1 + c] = is909 ? (ds.voiceFlam(voice) ? 3
                                     : ds.voiceSoft(voice) ? 1 : 2) : 1;
                if (ds.voiceAccent(voice)) anyAccent = true;
            }
            // Column 0 is the accent row on BOTH machines, and it is a VALUE:
            // the 808 writes 1 and the 909 writes 2, and the 909 scales its
            // accent depth by it (DrumStep::accentValue). Writing 1 on both put
            // every re-saved 909 accent at half depth. A loaded step keeps its
            // own value, a step accented in the grid takes its machine's, and
            // a step with no hits keeps whatever it carried, which nothing plays.
            if (anyAccent)   step[0] = ds.accentValue ? ds.accentValue : (is909 ? 2 : 1);
            else if (!anyOn) step[0] = ds.accentValue;
        }
        b.insert(b.end(), rec.begin(), rec.end());
    }
    return b;
}

// A TRAK: [u32 BE count] + count x [VLQ delta][param][value].
void putVlq(std::vector<unsigned char>& b, uint32_t n) {
    assert(n <= uint32_t(kMaxTick));           // a fifth byte is unreadable
    unsigned char tmp[5]; int i = 0;
    tmp[i++] = (unsigned char)(n & 0x7F); n >>= 7;
    while (n) { tmp[i++] = (unsigned char)((n & 0x7F) | 0x80); n >>= 7; }
    while (i > 0) b.push_back(tmp[--i]);
}

std::vector<unsigned char> buildTrak(const std::vector<Event>& events) {
    std::vector<unsigned char> b;
    putU32BE(b, (uint32_t) events.size());
    uint32_t last = 0;
    for (const auto& e : events) {
        // Clamped so that no delta needs a fifth VLQ byte (see kMaxTick). The
        // events arrive sorted by tick, so the clamp cannot reorder them.
        const uint32_t t = (uint32_t) clampi(e[0], 0, kMaxTick);
        putVlq(b, t - last);
        last = t;
        b.push_back((unsigned char) e[1]);
        b.push_back((unsigned char) e[2]);
    }
    return b;
}

// The song's automation for one track, in its original order. Param 1 on an
// instrument track is the pattern select, which the arrangement owns: the
// loader never puts one in the automation and the engine would ignore one, so
// one that got there some other way is not written over the arrangement's.
std::vector<Event> ridesFor(const Song& s, AutoTarget target, bool instrument) {
    std::vector<Event> v;
    for (const AutoEvent& e : s.automation) {
        if (e.target != target || (instrument && e.param == 1)) continue;
        v.push_back({ (int) std::min<uint32_t>(e.tick, uint32_t(kMaxTick)),
                      int(e.param), int(e.value) });
    }
    return v;
}

// One track's events: the rides, then the synthesised tick-0 dump, then the
// pattern selects, stably sorted by tick so that equal ticks keep that order.
// A dump param the rides already state at tick 0 is left out, so a loaded song
// writes back exactly the dump it read. The order only matters for two events
// on one param at one tick, since the loader treats each param on its own, and
// for those it keeps the song's own sequence. The dump itself goes out in
// descending param order, which is how ReBirth writes every tick-0 dump (all
// nine tracks of all sixteen demo songs).
//
// The pattern select at tick 0 is part of that dump, so it goes in its
// descending slot, just before the first tick-0 event with a lower param
// (param 0): all 64 instrument tracks of the demo songs end their tick-0 dump
// 2, 1, 0. At a later tick a select follows the rides. ReBirth has no fixed
// place for it there: of 715 later ticks where a select shares a tick with
// other events on its track, it comes first in 302, last in 363 and between
// in 50, which is the order they were recorded in, and the loader does not
// keep that.
std::vector<Event> assemble(std::vector<Event> rides, std::vector<Event> dump,
                            const std::vector<Event>& selects) {
    bool stated[256] = {};
    for (const Event& e : rides) if (e[0] == 0) stated[e[1] & 0xFF] = true;
    std::stable_sort(dump.begin(), dump.end(),
                     [](const Event& a, const Event& b) { return a[1] > b[1]; });
    for (const Event& e : dump)  if (!stated[e[1] & 0xFF]) rides.push_back(e);
    std::vector<Event> atZero;
    for (const Event& e : selects) (e[0] == 0 ? atZero : rides).push_back(e);
    std::stable_sort(rides.begin(), rides.end(),
                     [](const Event& a, const Event& b) { return a[0] < b[0]; });
    for (const Event& s : atZero) {
        auto at = rides.begin();
        while (at != rides.end() && (*at)[0] == 0 && (*at)[1] >= s[1]) ++at;
        rides.insert(at, s);
    }
    return rides;
}

// Each device's pattern-select timeline from the arrangement (review W1):
// param 1 at bar 0, then at every bar where that device's pattern changes,
// with rows expanded through their repeats. A device that holds its pattern
// across a row boundary writes nothing there. Returns the arrangement's length
// in bars, 0 for an empty arrangement, which keeps the old single select of
// pattern 0.
int patternSelects(const Song& s, std::vector<Event> sel[4]) {
    if (s.arrangement.empty()) {
        for (int d = 0; d < 4; ++d) sel[d].push_back({ 0, 1, 0 });
        return 0;
    }
    constexpr int64_t kMaxBars = kMaxTick / kTicksPerBar;
    int64_t bar = 0;
    int cur[4] = { -1, -1, -1, -1 };
    for (const SongStep& r : s.arrangement) {
        const int pat[4] = { r.bass1Pattern, r.bass2Pattern, r.drum808Pattern, r.drum909Pattern };
        const int tick = (int) std::min<int64_t>(bar * kTicksPerBar, kMaxTick);
        for (int d = 0; d < 4; ++d) {
            const int p = clampi(pat[d], 0, kBankPatterns - 1);
            if (p != cur[d]) { sel[d].push_back({ tick, 1, p }); cur[d] = p; }
        }
        bar = std::min<int64_t>(bar + std::max(1, r.repeats), kMaxBars);
    }
    return (int) bar;
}

} // namespace

RbsLoss rbsLossFor(const Song& s) {
    RbsLoss loss;
    auto check = [&loss](int len) { if (len > 16) loss.overLongPatterns++; };
    for (auto& p : s.bass1.patterns)   check(p.length);
    for (auto& p : s.bass2.patterns)   check(p.length);
    for (auto& p : s.drum808.patterns) check(p.length);
    for (auto& p : s.drum909.patterns) check(p.length);
    loss.lossless = (loss.overLongPatterns == 0 && loss.pluginChains == 0);
    return loss;
}

bool saveRbs(const Song& songIn, std::vector<unsigned char>& out, std::string* err) {
    Song& s = const_cast<Song&>(songIn);      // Bank::at() is non-const
    out.clear();

    // The chunks are written from the panel and the tracks' tick-0 dumps from
    // the start state. A song built in Reborn may carry only one of the two,
    // and then it stands in for the other: without a panel the start state is
    // the best record of the knobs, and without a start state the panel (the
    // live capture the app takes before saving) is what the user heard.
    const MixerInit&  panelMix = s.mixerPanel.present  ? s.mixerPanel  : s.mixerInit;
    const EffectInit& panelFx  = s.effectPanel.present ? s.effectPanel : s.effectInit;
    const MixerInit&  startMix = s.mixerInit.present   ? s.mixerInit   : panelMix;
    const EffectInit& startFx  = s.effectInit.present  ? s.effectInit  : panelFx;

    // Instrument headers, computed once: they are the chunk headers and, byte
    // for byte, the instrument tracks' synthesised tick-0 dumps.
    const std::vector<unsigned char> hdr[4] = {
        header303(s.bass1Knobs), header303(s.bass2Knobs),
        headerDrum(s.drum808Knobs, false), headerDrum(s.drum909Knobs, true) };

    std::vector<unsigned char> devl;
    {
        // Faders go through the inverse of the loader's exponential law.
        // round(gain * 127) put nominal songs back +9.99 dB hot and Clodhopper's
        // master from -11.10 dB to -24.05 (review W2).
        std::vector<unsigned char> mixr(64, 0);
        mixr[0] = (unsigned char) faderByteFromGain(panelFx.masterVolume, 1.0f);
        // +1 is the compressor's target (offset by one so 1 can mean the
        // master strip), +2 the PCF's. The distortion is a per-section flag in
        // each channel group at +4, not a selector here.
        mixr[1] = (unsigned char) compTargetByte(panelFx.compTarget);
        mixr[2] = (unsigned char) pcfTargetByte(panelFx.pcfTarget);
        for (int c = 0; c < 4; ++c) {
            unsigned char* g = mixr.data() + 16 + 12 * c;
            g[0] = panelFx.channelOn[c] ? 1 : 0;
            g[1] = (unsigned char) faderByteFromGain(panelMix.volume[c], kChannelUnityGain);
            g[2] = (unsigned char) panByteFromPan(panelMix.pan[c]);
            g[3] = kb(panelFx.delaySend[c]);   // the send is stored as byte / 127
            g[4] = panelFx.distOn[c] ? 1 : 0;
        }
        putChunk(devl, "MIXR", mixr);

        // DELY (8): [ver][steps 1..16][grid][feedback][pan][0 0 0]. Byte 2 is
        // the grid switch, nonzero = straight; it used to go unwritten, so every
        // straight delay reloaded as triplet, and the pan was fixed at 64.
        std::vector<unsigned char> dely(8, 0);
        dely[0] = 1;
        dely[1] = (unsigned char) clampi(panelFx.delaySteps, 1, 16);
        dely[2] = panelFx.delayStraight ? 1 : 0;
        dely[3] = kb(panelFx.delayFeedback);
        dely[4] = (unsigned char) clampi(panelFx.delayPan, 0, 127);
        putChunk(devl, "DELY", dely);
        // PCF (12): [ver][freq][q][amt][pattern][decay][mode]. The panel's
        // slider row is Freq Q Amt Dec, but the chunk wedges Pattern between
        // Amt and Dec and puts Mode last. See docs/FORMAT_RBS.md section 6.
        std::vector<unsigned char> pcfc(12, 0);
        pcfc[0] = 1;
        pcfc[1] = kb(panelFx.pcfFreq);
        pcfc[2] = kb(panelFx.pcfQ);
        pcfc[3] = kb(panelFx.pcfAmt);
        pcfc[4] = (unsigned char) clampi(panelFx.pcfPattern, 0, 54);
        pcfc[5] = kb(panelFx.pcfDecay);
        pcfc[6] = panelFx.pcfBandpass ? 1 : 0;
        putChunk(devl, "PCF ", pcfc);
        std::vector<unsigned char> dist(8, 0);
        // byte 1 = Shape (distTone), byte 2 = Amount (distAmount); measured
        // 2026-09-17, mirrors the reader in rbs.cpp.
        dist[0] = 1; dist[1] = kb(panelFx.distTone); dist[2] = kb(panelFx.distAmount);
        putChunk(devl, "DIST", dist);
        // COMP (8): [ver][ratio][threshold][meter, 127][0 0 0 0]. Byte 0 is a
        // version byte, not the on/off: all sixteen demo songs carry 1 there
        // while their COMP tracks disagree on on/off. This chunk used to be
        // written as eight zeros.
        std::vector<unsigned char> comp(8, 0);
        comp[0] = 1; comp[1] = kb(panelFx.compRatio); comp[2] = kb(panelFx.compThreshold);
        comp[3] = 127;
        putChunk(devl, "COMP", comp);

        putChunk(devl, "303 ", build303(s.bass1, hdr[0]));
        putChunk(devl, "303 ", build303(s.bass2, hdr[1]));
        putChunk(devl, "808 ", buildDrum(s.drum808, hdr[2], false));
        putChunk(devl, "909 ", buildDrum(s.drum909, hdr[3], true));
    }

    std::vector<Event> sel[4];
    const int bars = patternSelects(s, sel);

    std::vector<unsigned char> trkl;
    {
        // Mixer (track 0): the start state's routing and channel groups, then
        // the song's rides. Param N is not a chunk offset here: the channels
        // sit in groups of four at 6 + 6*ch (FORMAT_RBS.md section 5).
        std::vector<Event> mixDump;
        mixDump.push_back({ 0, 1, compTargetByte(startFx.compTarget) });
        mixDump.push_back({ 0, 2, pcfTargetByte(startFx.pcfTarget) });
        for (int c = 0; c < 4; ++c) {
            const int base = 6 + 6 * c;
            mixDump.push_back({ 0, base + 0, faderByteFromGain(startMix.volume[c], kChannelUnityGain) });
            mixDump.push_back({ 0, base + 1, panByteFromPan(startMix.pan[c]) });
            mixDump.push_back({ 0, base + 2, kb(startFx.delaySend[c]) });
            mixDump.push_back({ 0, base + 3, startFx.distOn[c] ? 1 : 0 });
        }
        const std::vector<Event> mix = assemble(ridesFor(s, AutoTarget::Mixer, false), mixDump, {});

        // Instruments (tracks 1..4): the pattern selects, the song's rides, and
        // a tick-0 dump for any param the rides do not state there. Param 0 is
        // the unmute (1 = playing) and param N from 2 up is header byte N.
        const AutoTarget targets[4] = { AutoTarget::Bass1, AutoTarget::Bass2,
                                        AutoTarget::Drum808, AutoTarget::Drum909 };
        const int lastParam[4] = { 8, 8, 28, 29 };
        std::vector<Event> dev[4];
        for (int d = 0; d < 4; ++d) {
            std::vector<Event> dump;
            dump.push_back({ 0, 0, startFx.channelOn[d] ? 1 : 0 });
            for (int p = 2; p <= lastParam[d]; ++p) dump.push_back({ 0, p, int(hdr[d][p]) });
            dev[d] = assemble(ridesFor(s, targets[d], true), dump, sel[d]);
        }

        // Song length. The loader ends the arrangement at the bar holding the
        // last event on the instrument and mixer tracks (whether the effect
        // tracks should count is review E2). When nothing reaches the last bar,
        // re-select bass 1's pattern there: the same pattern, so it is
        // inaudible, and it pins the length. This replaces a restatement of the
        // channel volumes at the last bar, which snapped a ridden fader back to
        // its opening value there.
        if (bars > 0) {
            const int lastBarTick = (int) std::min<int64_t>(int64_t(bars - 1) * kTicksPerBar, kMaxTick);
            int reach = -1;
            const std::vector<Event>* counted[5] = { &mix, &dev[0], &dev[1], &dev[2], &dev[3] };
            for (const std::vector<Event>* t : counted)
                if (!t->empty()) reach = std::max(reach, t->back()[0]);   // sorted by tick
            if (reach < lastBarTick)
                dev[0].push_back({ lastBarTick, 1,
                                   clampi(s.arrangement.back().bass1Pattern, 0, kBankPatterns - 1) });
        }

        putChunk(trkl, "TRAK", buildTrak(mix));
        for (int d = 0; d < 4; ++d) putChunk(trkl, "TRAK", buildTrak(dev[d]));

        // The four effect tracks, in the file's order: DELY, DIST, PCF, COMP.
        // Param N is chunk byte N and param 0 is the unit's on/off, so each
        // dump restates its unit's start state; a bare `param 1 = 0` once read
        // back on the PCF track as Freq = 0 and overrode the chunk.
        //
        // DELY: 0 = the unit's on/off (fixed at 1 until 2026-09-26), 1 Steps,
        // 2 the grid, 3 Feedback, 4 the pan. The loader reads this dump into
        // effectInit and the engine plays it, so it is the song's delay while
        // the DELY chunk above is the panel (review E3).
        const EffectInit& fx = startFx;
        putChunk(trkl, "TRAK", buildTrak(assemble(ridesFor(s, AutoTarget::Delay, false), {
            { 0, 0, fx.delayOn ? 1 : 0 }, { 0, 1, clampi(fx.delaySteps, 1, 16) },
            { 0, 2, fx.delayStraight ? 1 : 0 }, { 0, 3, kb(fx.delayFeedback) },
            { 0, 4, clampi(fx.delayPan, 0, 127) } }, {})));
        // DIST track params: 0 = the unit's on/off (fixed at 1 until 2026-09-26,
        // so a bypassed distortion came back on), 1 = Shape (distTone), 2 =
        // Amount (distAmount), mirroring the chunk decode measured 2026-09-17.
        putChunk(trkl, "TRAK", buildTrak(assemble(ridesFor(s, AutoTarget::Dist, false), {
            { 0, 0, fx.distortionOn ? 1 : 0 }, { 0, 1, kb(fx.distTone) },
            { 0, 2, kb(fx.distAmount) } }, {})));
        putChunk(trkl, "TRAK", buildTrak(assemble(ridesFor(s, AutoTarget::Pcf, false), {
            { 0, 0, fx.pcfOn ? 1 : 0 }, { 0, 1, kb(fx.pcfFreq) }, { 0, 2, kb(fx.pcfQ) },
            { 0, 3, kb(fx.pcfAmt) }, { 0, 4, clampi(fx.pcfPattern, 0, 54) },
            { 0, 5, kb(fx.pcfDecay) }, { 0, 6, fx.pcfBandpass ? 1 : 0 } }, {})));
        // COMP: on/off, Ratio (1), Threshold (2). Param 3 is the chunk's meter
        // byte, which no ReBirth song carries on the track.
        putChunk(trkl, "TRAK", buildTrak(assemble(ridesFor(s, AutoTarget::Comp, false), {
            { 0, 0, fx.compressorOn ? 1 : 0 }, { 0, 1, kb(fx.compRatio) },
            { 0, 2, kb(fx.compThreshold) } }, {})));
    }

    std::vector<unsigned char> children;
    putChunk(children, "HEAD", buildHead());
    putChunk(children, "GLOB", buildGlob(s, bars));
    putChunk(children, "USRI", buildUsri(s));
    putGroup(children, "DEVL", devl);
    putGroup(children, "TRKL", trkl);
    putGroup(out, "RB40", children);

    if (out.empty()) { if (err) *err = "nothing written"; return false; }
    return true;
}

bool saveRbsFile(const std::string& path, const Song& s, std::string* err) {
    std::vector<unsigned char> buf;
    if (!saveRbs(s, buf, err)) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { if (err) *err = "cannot open " + path + " for writing"; return false; }
    const std::size_t n = std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (n != buf.size()) { if (err) *err = "short write"; return false; }
    return true;
}

} // namespace reborn
