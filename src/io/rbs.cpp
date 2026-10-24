// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Clean-room loader for the original .rbs song format.
//
// The format is EA-IFF-85: a tree of chunks, each `<4-byte id><big-endian u32
// size><payload>` with payloads padded to an even length. Group chunks
// ("CAT ", "FORM", "LIST", "PROP") begin their payload with a 4-byte group
// type followed by sub-chunks.
//
// Reconstructed entirely by inspecting the shipped data files (no original code
// was used). Confirmed structure (see docs/FORMAT_RBS.md):
//
//   CAT  RB40                       song container
//     HEAD                          header/version
//     GLOB    tempo (u32 BE at +2 == BPM*1000), global settings, skin name
//     USRI    NUL-terminated strings: title, description, author
//     CAT  DEVL                     device rack
//       MIXR DELY PCF  DIST COMP    mixer + effect settings
//       303  303  808  909          instrument settings + pattern banks
//     CAT  TRKL                     arrangement / automation tracks
//       TRAK ...
//
// Pattern banks (fully decoded, validated against all 18 shipped songs):
//
//   303 chunk = 9-byte header + 32 records x 34 bytes
//     header: [ver, ?, six knob bytes 0..127, waveform]
//     record: [shuffle u8][length u8 1..16] + 16 x [note u8][flags u8]
//       note  = semitone 0..12 (C..C an octave up)
//       flags = 0x01 SLIDE | 0x02 ACCENT | 0x04 octave-UP | 0x08 octave-DOWN
//             | 0x10 REST (gate is INVERTED: flags&0x10==0 means the step
//               plays; an all-zero pattern is the ReBirth-1.0-style "repeating
//               synth pattern", a fresh silent pattern has 0x10 on every step)
//
//   808 chunk = 30-byte header + 32 records x 194 bytes
//   909 chunk = 31-byte header + 32 records x 194 bytes
//     record: [shuffle u8][length u8] + 16 steps x 12 voice bytes
//       column 0     = global ACCENT row (hardware-style)
//       columns 1-11 = instruments in panel order:
//         808: BD SD LT MT HT RS/CL CP/MA CB CY OH CH  (0 off, 1 hit)
//         909: BD SD LT MT HT RS   CP    CH OH CR RD   (0 off, 1 soft,
//              2 normal, 3 accented)
//
#include "reborn/rbs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace reborn {
namespace {

inline uint32_t rdU32BE(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool isGroup(const unsigned char* id) {
    return !std::memcmp(id, "CAT ", 4) || !std::memcmp(id, "FORM", 4)
        || !std::memcmp(id, "LIST", 4) || !std::memcmp(id, "PROP", 4);
}

// A located chunk: id + payload span.
struct Chunk { const unsigned char* id; const unsigned char* data; uint32_t size; };

// IFF containers nested at most. A song's chunks sit two or three levels deep; a
// crafted file nesting thousands deep would otherwise overflow the stack, since the
// walk below recurses once per level (review S1). The same cap as the mod reader.
constexpr int kMaxIffDepth = 16;

// Recursively collect every leaf chunk with the given 4-char id.
void findLeaves(const unsigned char* buf, size_t off, size_t end,
                const char id4[4], std::vector<Chunk>& out, int depth = 0) {
    while (off + 8 <= end) {
        const unsigned char* id = buf + off;
        uint32_t size = rdU32BE(buf + off + 4);
        size_t body = off + 8;
        if (body + size > end) break;                 // corrupt / truncated
        if (isGroup(id) && size >= 4) {
            if (depth < kMaxIffDepth) findLeaves(buf, body + 4, body + size, id4, out, depth + 1);
        } else if (!std::memcmp(id, id4, 4)) {
            out.push_back({ id, buf + body, size });
        }
        size_t pad = size + (size & 1u);
        off = body + pad;
    }
}

// Fold a Latin-1 byte to a readable ASCII approximation (the files use the Mac
// Roman / Latin-1 range for accents, ©, curly quotes, etc.).
void appendFolded(std::string& s, unsigned char c) {
    if (c == '\r' || c == '\n' || c == '\t') { s.push_back(' '); return; }
    if (c >= 32 && c < 127) { s.push_back(char(c)); return; }
    switch (c) {
        case 0xA9: s += "(c)"; return;                 // ©
        case 0xAE: s += "(r)"; return;                 // ®
        case 0xB4: case 0x92: case 0x27: s.push_back('\''); return; // ´ ’ '
        case 0xE4: case 0xE0: case 0xE1: case 0xE2: case 0xE3: s.push_back('a'); return;
        case 0xF6: case 0xF2: case 0xF3: case 0xF4: case 0xF5: s.push_back('o'); return; // ö
        case 0xFC: case 0xF9: case 0xFA: case 0xFB: s.push_back('u'); return;            // ü
        case 0xEB: case 0xE8: case 0xE9: case 0xEA: s.push_back('e'); return;            // é è
        case 0xEF: case 0xEC: case 0xED: case 0xEE: s.push_back('i'); return;
        case 0xF1: s.push_back('n'); return;           // ñ
        case 0xDF: s += "ss"; return;                  // ß
        default: return;                               // drop other control/hi bytes
    }
}

// Read one NUL-terminated field starting at offset, up to maxLen bytes, folded
// to readable ASCII and trimmed of surrounding whitespace.
std::string readField(const unsigned char* p, uint32_t total, uint32_t offset, uint32_t maxLen) {
    std::string s;
    for (uint32_t i = offset; i < total && i < offset + maxLen; ++i) {
        if (p[i] == 0) break;
        appendFolded(s, p[i]);
    }
    size_t a = s.find_first_not_of(' ');
    size_t b = s.find_last_not_of(' ');
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}

// Extract the first double-quoted substring (used to recover a song title that
// authors typically wrote as "Title" at the start of the description).
std::string extractQuoted(const std::string& s) {
    size_t a = s.find('"');
    if (a == std::string::npos) return {};
    size_t b = s.find('"', a + 1);
    if (b == std::string::npos) return {};
    return s.substr(a + 1, b - a - 1);
}

// ---------------------------------------------------------------------------
// Pattern-bank decoding
// ---------------------------------------------------------------------------

// 303 step flags (see header comment; gate is inverted).
// Octave bits 2-3 are INDEPENDENT SIGNED FLAGS THAT SUM (proven by controlled
// A/B + FFT of the original's solo exports, 2026-07-23):
//   0x04 = +1 octave (scratch song: note-0 step exported at 130.7 Hz = C3,
//          one octave ABOVE the 65.4 Hz base; old 0x04=down was swapped)
//   0x08 = -1 octave (Trakka bass2 pat-1/16 and bass1 pat-17 0x08 steps all
//          sound one octave BELOW the notated semitone)
//   0x0C = net 0     (Trakka bass1 pat-17 steps 6-7: note 3 + tune sounds at
//          Eb3 exactly: up and down cancel)
// SLIDE and ACCENT are 0x01 and 0x02 respectively, CORRECTED 2026-07-24,
// they were previously swapped. Proven by a controlled export: a pattern with
// 0x01 set on every 4th step (all steps the same note) came back from ReBirth
// with those steps DARKER (centroid 484 vs 667 Hz) and slightly quieter, and,
// decisively, producing NO ONSET AT ALL: 47-49 onsets were detected where
// 64 steps were programmed, i.e. exactly the 16 flagged steps did not
// retrigger. That is tie/slide behaviour (no envelope retrigger), not accent,
// which would be brighter and louder. It was also identical with the ACCENT
// knob at 0 and at 110, so the flag cannot be the accent bit.
constexpr unsigned kFlSlide = 0x01, kFlAccent = 0x02, kFlOctUp = 0x04,
                   kFlOctDown = 0x08, kFlRest = 0x10;

constexpr int k303Header = 9,  k303Record = 34;
constexpr int k808Header = 30, k909Header = 31, kDrumRecord = 194;
constexpr int kBankPatterns = 32;

// Decode one 303 bank chunk into a Bank<BassPattern>. Also applies the knob
// header to the instrument's stored knob values in the pattern container.
// Header knob order (panel order, confirmed by automation statistics: the
// cutoff at byte 3 is by far the most-automated param in the TRAK data):
//   [ver, ?, tune, cutoff, resonance, envmod, decay, accent, waveform]
void decode303(const unsigned char* p, uint32_t n, Bank<BassPattern>& bank,
               float knobs[7] /*tune,cutoff,reso,envmod,decay,accent,waveform*/) {
    if ((int)n < k303Header + kBankPatterns * k303Record) return;
    for (int k = 0; k < 6; ++k) knobs[k] = float(p[2 + k]) / 127.0f;
    knobs[6] = p[8] ? 1.0f : 0.0f;

    bank.patterns.assign(kBankPatterns, BassPattern{});
    for (int r = 0; r < kBankPatterns; ++r) {
        const unsigned char* rec = p + k303Header + r * k303Record;
        BassPattern& pat = bank.patterns[r];
        int len = rec[1];
        pat.length = (len >= 1 && len <= 16) ? len : 16;
        pat.shuffle = rec[0] != 0;
        pat.octaveBase = 3;                       // note 0 ~= C2 (classic bass register)
        for (int s = 0; s < 16; ++s) {
            unsigned note  = rec[2 + 2 * s];
            unsigned flags = rec[3 + 2 * s];
            BassStep& st = pat.steps[s];
            st.gate   = (flags & kFlRest) == 0;
            st.note   = (note <= 12) ? int(note) : 0;
            st.octave = ((flags & kFlOctUp) ? 1 : 0)
                      - ((flags & kFlOctDown) ? 1 : 0);   // signed bits sum; 0x0C = 0
            // Kept so a save writes 0x0C back rather than a plain 0.
            st.octBoth = (flags & (kFlOctUp | kFlOctDown)) == (kFlOctUp | kFlOctDown);
            st.accent = (flags & kFlAccent) != 0;
            st.slide  = (flags & kFlSlide) != 0;
        }
    }
}

// Column -> engine-voice maps (file columns 1..11, panel order; see header).
// Engine Drum808 voices: BD SD LT MT HT LC MC HC RS CP MA CB CY OH CH CL
const int k808VoiceMap[11] = { 0, 1, 2, 3, 4, 8, 9, 11, 12, 13, 14 };
// Engine Drum909 voices: BD SD LT MT HT RS CP OH CH CR RD  (file has CH before OH)
const int k909VoiceMap[11] = { 0, 1, 2, 3, 4, 5, 6, 8, 7, 9, 10 };

void decodeDrums(const unsigned char* p, uint32_t n, int headerLen,
                 const int voiceMap[11], bool valueHasAccent,
                 Bank<DrumPattern>& bank) {
    if ((int)n < headerLen + kBankPatterns * kDrumRecord) return;
    bank.patterns.assign(kBankPatterns, DrumPattern{});
    for (int r = 0; r < kBankPatterns; ++r) {
        const unsigned char* rec = p + headerLen + r * kDrumRecord;
        DrumPattern& pat = bank.patterns[r];
        int len = rec[1];
        pat.length = (len >= 1 && len <= 16) ? len : 16;
        pat.shuffle = rec[0] != 0;
        for (int s = 0; s < 16; ++s) {
            const unsigned char* step = rec + 2 + s * 12;
            DrumStep& st = pat.steps[s];
            bool globalAccent = step[0] != 0;     // column 0 = accent row
            st.accentValue = step[0];             // and its VALUE sets the depth
            for (int c = 0; c < 11; ++c) {
                unsigned v = step[1 + c];
                if (!v) continue;
                int voice = voiceMap[c];
                st.setOn(voice, true);
                // ACCENT is column 0 on BOTH machines. The 909's CELL VALUE is a
                // separate axis: 1 soft, 2 normal, 3 and up FLAM. Measured
                // 2026-08-25 by isolating each factor against ReBirth: setting
                // column 0 alone lifts the hit 10.07 dB at accent knob 70 and adds
                // no second transient, while cell 3 alone adds a transient at the
                // flam gap and does NOT lift the level. An earlier reading had 3 as
                // accent and 4 as flam and was wrong in both halves, which mattered
                // because 14 of the 16 shipped songs use value 1 (2423 cells) and
                // value 3 (281 cells) on their 909 parts.
                st.setAccent(voice, globalAccent);
                if (valueHasAccent) {
                    if (v == 1) st.setSoft(voice, true);
                    if (v >= 3)  st.setFlam(voice, true);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// TRAK arrangement decoding
//
// TRKL holds one TRAK per device, in the order: MIXR, 303 (bass 1), 303
// (bass 2), 808, 909, DELY, DIST, PCF, COMP. Each TRAK payload is
//   [u32 BE event count] + count x [VLQ delta-time][param u8][value u8]
// where VLQ is a MIDI-style variable-length quantity. All 162 tracks across
// the shipped songs parse exactly under this layout.
//
// Time unit: one tick = a 32nd note, so a 16-step bar = 32 ticks. On the four
// instrument tracks, param 1 = pattern select (0..31), and pattern changes
// land on bar boundaries in every shipped song. Params 2+ are knob automation,
// which the engine applies as the song plays; param 0 is a 0/1 switch that
// mutes the section when it is 0 (polarity confirmed, see engine.cpp and
// docs/NON_OBVIOUS_BEHAVIOUR.md).
//
// The mixer track's initial dump carries per-channel groups of four params at
// base 6 + 6*ch: [volume 0..127][pan 0..127, 64 = center][?][0/1 switch].
// ---------------------------------------------------------------------------

struct TrakEvent { uint32_t time; uint8_t param, value; };

// Parse one TRAK payload. Returns false if the payload does not conform.
bool parseTrak(const unsigned char* p, uint32_t n, std::vector<TrakEvent>& out) {
    if (n < 4) return false;
    uint32_t count = rdU32BE(p);
    uint32_t i = 4, t = 0;
    out.clear();
    // count is attacker-controlled and reserving it directly is a 34 GB
    // allocation from a 106-byte file. Every event is at least 3 bytes (one VLQ
    // byte, param, value), so cap the reservation at what the payload can hold.
    // The loop below still enforces the real bound and returns false on truncation.
    out.reserve(std::min<uint32_t>(count, (n - 4) / 3));
    for (uint32_t e = 0; e < count; ++e) {
        // VLQ delta time (max 4 bytes).
        uint32_t dt = 0; int k = 0;
        for (; k < 4; ++k) {
            if (i >= n) return false;
            unsigned char c = p[i++];
            dt = (dt << 7) | (c & 0x7Fu);
            if (!(c & 0x80u)) break;
        }
        if (k == 4) return false;
        if (i + 2 > n) return false;
        t += dt;
        out.push_back({ t, p[i], p[i + 1] });
        i += 2;
    }
    return i == n;
}

constexpr uint32_t kTicksPerBar = 32;   // 1 tick = 1/32 note; 16 steps = 32 ticks

// Extract the pattern-select timeline (param 1) from an instrument track as
// bar-index -> pattern, and report the last event time seen on the track.
void patternTimeline(const std::vector<TrakEvent>& ev,
                     std::vector<std::pair<uint32_t, int>>& changes,
                     uint32_t& lastTime) {
    changes.clear();
    for (const TrakEvent& e : ev) {
        if (e.time > lastTime) lastTime = e.time;
        if (e.param == 1 && e.value < kBankPatterns)
            changes.emplace_back(e.time / kTicksPerBar, int(e.value));
    }
}

// Decode the 909 chunk knob header (31 bytes; panel order, validated against
// the untouched default templates; see docs/FORMAT_RBS.md section 4):
//   [ver][?][accent] BD[L,T,A,D] SD[L,T,Tone,Snap] LTx3[L,T,D] RS[L] CP[L]
//   HH[L, CHdecay, OHdecay] CR[L,T] RD[L,T] [?,?]
void decode909Knobs(const unsigned char* p, DrumKnobs& k) {
    auto f = [&](int i) { return float(p[i]) / 127.0f; };
    k.accent = f(2);
    k.level[0] = f(3);  k.bdTune = f(4); k.bdAttack = f(5); k.bdDecay = f(6);
    k.level[1] = f(7);  k.sdTune = f(8); k.sdTone = f(9);   k.sdSnappy = f(10);
    k.level[2] = f(11); k.level[3] = f(14); k.level[4] = f(17);
    // Each tom column carries its OWN Tune and Decay: LT p[12,13], MT p[15,16],
    // HT p[18,19]. Only the LT pair used to be read (into a single shared knob),
    // so the mid and hi tom Tune and Decay controls did nothing.
    k.ltTune = f(12); k.ltDecay = f(13);
    k.mtTune = f(15); k.mtDecay = f(16);
    k.htTune = f(18); k.htDecay = f(19);
    k.level[5] = f(20); k.level[6] = f(21);
    k.level[7] = f(22);                    // OH shares the HH level
    k.level[8] = f(22);                    // CH shares the HH level
    k.chDecay = f(23); k.ohDecay = f(24);
    k.level[9] = f(25); k.level[10] = f(27);
    // Crash and ride mirror as [Level, Tune] pairs: crash tune p[26], ride tune
    // p[28] (confirmed 2026-08-25 by the shipped-song header scan and the rdt
    // sweep). Byte 28 was previously unread, so the ride tune did nothing.
    k.cymTune = f(26); k.rdTune = f(28);
    // Byte 29 is the FLAM amount (MIDI CC 92), confirmed 2026-08-25 by export.
    // Byte 30 is not a control: it mirrors bytes 25 and 27 (the crash and ride
    // levels) in every one of the 16 shipped songs.
    k.flam = f(29);
    k.present = true;
}

// Decode the 808 chunk knob header (30 bytes):
//   [ver][?][accent] BD[L,Tone,D] SD[L,Tone,Snap] LT/MT/HT[L,T,sw]
//   RS[L,sw] CP[L,sw] CB[L] CY[L,T,D] OH[L,D] CH[L] [?]
void decode808Knobs(const unsigned char* p, DrumKnobs& k) {
    auto f = [&](int i) { return float(p[i]) / 127.0f; };
    k.accent = f(2);
    k.level[0] = f(3);  k.bdTune = f(4); k.bdDecay = f(5);
    k.level[1] = f(6);  k.sdTone = f(7); k.sdSnappy = f(8);
    k.level[2] = f(9);  k.level[3] = f(12); k.level[4] = f(15);
    // Each tom column carries its own tune byte: LT p[10], MT p[13], HT p[16].
    // Only p[10] used to be read (into a single shared knob), so the mid and hi
    // tom tune controls did nothing.
    k.ltTune = f(10); k.mtTune = f(13); k.htTune = f(16);
    k.level[5] = f(18); k.level[6] = f(20);
    k.level[7] = f(22);                    // CB
    k.level[8] = f(23);                    // CY
    k.cymTune = f(24); k.crDecay = f(25);  // CY tune/decay
    k.level[9] = f(26); k.ohDecay = f(27); // OH level/decay
    k.level[10] = f(28);                   // CH
    // Shared-column switches. Each of these five columns drives one of two
    // voices; 0 selects the ALTERNATE (conga / clave / maracas). Ignoring them
    // made 17 of the 18 shipped songs play at least one column with the wrong
    // voice, which is why several demos' "toms" sounded an octave low.
    k.sharedPrimary[SC_LT] = p[11] != 0;
    k.sharedPrimary[SC_MT] = p[14] != 0;
    k.sharedPrimary[SC_HT] = p[17] != 0;
    k.sharedPrimary[SC_RS] = p[19] != 0;
    k.sharedPrimary[SC_CP] = p[21] != 0;
    k.present = true;
}

// Does a pattern contain anything audible?
bool hasContent(const BassPattern& p) {
    for (int s = 0; s < p.length; ++s)
        if (p.steps[s].gate && (p.steps[s].note != 0 || p.steps[s].octave != 0
                                || p.steps[s].accent || p.steps[s].slide))
            return true;
    // A pattern of gated repeated Cs only counts if every step gates (the
    // ReBirth-1.0 "repeating" template); treat that as empty for selection.
    return false;
}
bool hasContent(const DrumPattern& p) {
    for (int s = 0; s < p.length; ++s)
        if (p.steps[s].on) return true;
    return false;
}

template <typename P>
int firstContentful(const Bank<P>& bank) {
    for (int i = 0; i < (int)bank.patterns.size(); ++i)
        if (hasContent(bank.patterns[i])) return i;
    return 0;
}

} // namespace

std::string skinNameFromGlob(const std::vector<uint8_t>& glob) {
    std::string name;
    if (glob.size() <= 15) return name;
    const unsigned char* s = glob.data() + 15;
    const std::size_t avail = glob.size() - 15;
    std::size_t n = 0;
    while (n < avail && n < 64 && s[n] != 0) ++n;
    for (std::size_t i = 0; i < n; ++i) appendFolded(name, s[i]);
    return name;
}

bool loadRbs(const unsigned char* data, size_t size, Song& out, std::string* err) {
    if (!data || size < 12 || std::memcmp(data, "CAT ", 4) != 0) {
        if (err) *err = "not an .rbs file (missing CAT/RB40 header)";
        return false;
    }
    // Verify the form type is RB40 (ReBirth 2.0 song).
    if (std::memcmp(data + 8, "RB40", 4) != 0) {
        // Not fatal for older/newer variants, but note it.
        if (err) *err = "unexpected form type (expected RB40)";
    }

    out = Song();  // reset to defaults

    // --- Tempo (GLOB + 2, u32 BE, == BPM * 1000) ---
    std::vector<Chunk> glob;
    findLeaves(data, 0, size, "GLOB", glob);
    // Kept whole for the writer, which patches the fields read below and leaves
    // every other byte as it found it (Song::globRaw).
    if (!glob.empty()) out.globRaw.assign(glob[0].data, glob[0].data + glob[0].size);
    if (!glob.empty() && glob[0].size >= 6) {
        uint32_t t = rdU32BE(glob[0].data + 2);
        double bpm = double(t) / 1000.0;
        if (bpm >= 20.0 && bpm <= 320.0) out.tempo = bpm;
        // GLOB+14 = global shuffle amount (0..127, factory default 0x40):
        // both untouched default-song templates carry 0x40, and songs that use
        // no shuffled patterns leave it at 0x40 while shuffle-heavy songs move
        // it. Applies only to patterns whose shuffle flag is set.
        if (glob[0].size >= 15) out.shuffle = float(glob[0].data[14]) / 127.0f;
        // GLOB+11 = loop length in bars * 3 (the transport's Loop Length, and
        // what a song-mode export runs for). See FORMAT_RBS.md.
        if (glob[0].size >= 13) {
            const int raw = (int(glob[0].data[11]) << 8) | int(glob[0].data[12]);
            const int bars = raw / 3;
            if (bars >= 1 && bars <= 4096) out.loopBars = bars;
        }
        // GLOB+15 = NUL-terminated SKIN / MOD NAME this song was authored
        // against ("Standard ReBirth" for the stock look). It is a REFERENCE,
        // not embedded data: no shipped song carries mod content. Songs made
        // with a mod were composed using THAT MOD'S drum samples, so resolving
        // it matters for fidelity; see rbm.h findModByName().
        if (glob[0].size > 15) out.skinName = skinNameFromGlob(out.globRaw);
    }

    // --- Title / author / description (USRI) ---
    // Layout (reverse-engineered from the demo songs):
    //   offset  0 : author / credit line   (fixed ~41-byte field)
    //   offset 41 : description            (authors typically start it with the
    //               song title in "quotes", followed by notes / contact info)
    std::vector<Chunk> usri;
    findLeaves(data, 0, size, "USRI", usri);
    if (!usri.empty()) {
        const unsigned char* p = usri[0].data;
        uint32_t n = usri[0].size;
        out.author = readField(p, n, 0, 41);
        std::string desc = readField(p, n, 41, n);
        std::string quoted = extractQuoted(desc);
        if (!quoted.empty()) out.title = quoted;      // "Title" recovered from desc
        else if (!desc.empty()) out.title = desc.substr(0, 48);
    }

    // --- Instrument pattern banks ---
    std::vector<Chunk> c303, c808, c909;
    findLeaves(data, 0, size, "303 ", c303);
    findLeaves(data, 0, size, "808 ", c808);
    findLeaves(data, 0, size, "909 ", c909);

    auto applyKnobs = [](BassKnobs& k, const float v[7]) {
        // TUNE is a 32-slot lookup table with a wide centre detent, NOT the
        // continuous law this used to assume. See tuneSemitonesFromByte() in
        // reborn/rbs.h for the measured table and why the old fit was wrong.
        const int tuneByte = (int) std::lround(v[0] * 127.0f);
        k.tuning = (float) tuneSemitonesFromByte(tuneByte);
        k.cutoff = v[1]; k.resonance = v[2]; k.envmod = v[3]; k.decay = v[4];
        k.accent = v[5]; k.waveform = v[6]; k.present = true;
    };
    float knobs[7];
    if (c303.size() >= 1) { decode303(c303[0].data, c303[0].size, out.bass1, knobs); applyKnobs(out.bass1Knobs, knobs); }
    if (c303.size() >= 2) { decode303(c303[1].data, c303[1].size, out.bass2, knobs); applyKnobs(out.bass2Knobs, knobs); }
    if (!c808.empty()) {
        decodeDrums(c808[0].data, c808[0].size, k808Header, k808VoiceMap, false, out.drum808);
        if (c808[0].size >= 30) decode808Knobs(c808[0].data, out.drum808Knobs);
    }
    if (!c909.empty()) {
        decodeDrums(c909[0].data, c909[0].size, k909Header, k909VoiceMap, true, out.drum909);
        if (c909[0].size >= 31) decode909Knobs(c909[0].data, out.drum909Knobs);
    }

    // --- Mixer + effect settings (MIXR / DELY / DIST chunks) ---
    // MIXR (64 bytes): [master vol][compressor target: 0 off, 1 master, 2..5
    // section][PCF target: 0 off, 1..4 section][13 x 0] then four 12-byte
    // channel groups at 16+12c:
    // [on/off][volume][pan][echo send][distortion on/off][7 x 0].
    // DELY (8): [ver][steps 1..16][grid][feedback][pan]. DIST (8): [ver]
    // [shape][amount]. See docs/FORMAT_RBS.md section 6.
    {
        std::vector<Chunk> mixr, dely, dist, pcf;
        findLeaves(data, 0, size, "MIXR", mixr);
        findLeaves(data, 0, size, "DELY", dely);
        findLeaves(data, 0, size, "DIST", dist);
        findLeaves(data, 0, size, "PCF ", pcf);   // note the trailing space
        EffectInit& fx = out.effectInit;
        if (!mixr.empty() && mixr[0].size >= 64) {
            const unsigned char* m = mixr[0].data;
            // Master fader: the shared exponential law (see faderGain in
            // pattern.h for the measurement). Six shipped songs sit away from
            // the nominal 100 (Clodhopper 70, Fast Forward 74, Calm 91,
            // Fonda Workout 95, KiloMix 97, Transfusion 106, Untitled 104),
            // and reading the byte as value/100 made Clodhopper 8.0 dB and
            // Fast Forward 7.0 dB too loud.
            fx.masterVolume = faderGain(m[0]);
            // +1 is the COMPRESSOR's target, offset by one from the 1-based
            // section index so that 1 can mean the master strip. +2 is the
            // PCF's, which is a plain 1-based section index. Both exclusive.
            // Both are the save-time panel, so the mixer TRAK's tick-0 dump
            // below overrides them when it exists.
            fx.compTarget = (m[1] == 1) ? kCompMaster
                          : (m[1] >= 2 && m[1] <= 5) ? m[1] - 2 : -1;
            fx.pcfTarget  = (m[2] >= 1 && m[2] <= 4) ? m[2] - 1 : -1;
            for (int c = 0; c < 4; ++c) {
                const unsigned char* g = m + 16 + 12 * c;
                fx.channelOn[c] = g[0] != 0;
                // g[3] is the echo send level. g[4] is this section's
                // DISTORTION switch: clicking DIST on section 2 toggles it and
                // touches nothing else. It is not a send-enable, which is why
                // SuperNova runs max sends with g[4]=0 and still echoes.
                fx.delaySend[c] = float(g[3]) / 127.0f;
                fx.distOn[c] = g[4] != 0;
                // g[1] volume / g[2] pan also live in the TRAK init dump, which
                // is authoritative; use these as fallback when TRAK was absent.
                if (!out.mixerInit.present) {
                    out.mixerInit.volume[c] = faderGain(g[1], kChannelUnityGain);
                    out.mixerInit.pan[c] = panFromByte(g[2]);
                }
            }
            if (!out.mixerInit.present) out.mixerInit.present = true;
            fx.present = true;
        }
        if (!dely.empty() && dely[0].size >= 5) {
            const unsigned char* p = dely[0].data;
            if (p[1] >= 1 && p[1] <= 16) fx.delaySteps = p[1];
            // +2 is the triplet/straight grid switch (sense inverted from the
            // name: 0 = triplet /3, nonzero = straight /4) and +4 the wet-return
            // pan. Both were decoded and thrown away until the delay supertask.
            // All four are the save-time panel: the DELY track's tick-0 dump
            // below overrides them, as the mixer's does the MIXR routing.
            fx.delayStraight = (p[2] != 0);
            fx.delayFeedback = float(p[3]) / 127.0f;
            fx.delayPan      = p[4];
            fx.present = true;
        }
        if (!dist.empty() && dist[0].size >= 3) {
            const unsigned char* p = dist[0].data;
            // Measured 2026-09-17 (supertask 7, dist-grid): byte 2 is AMOUNT (it
            // gates the effect: byte2=0 gives no distortion for any byte1) and
            // byte 1 is SHAPE (the character; byte1=0 with byte2 up still
            // distorts). This is Appendix C's order (Shape 104, Amount 105); the
            // file byte order IS the MIDI order here. Previously read the other
            // way round, which drove the shaper with Shape and the tone with
            // Amount. `distTone` still names the SHAPE control until the DSP
            // rework redefines it (it is a character control, not a tone tilt).
            fx.distAmount = float(p[2]) / 127.0f;   // byte 2 = Amount
            fx.distTone   = float(p[1]) / 127.0f;   // byte 1 = Shape
            fx.present = true;
        }
        // PCF (12): [ver][freq][q][amt][pattern 0..54][decay][mode 0=LP/1=BP].
        // Mapped by driving each control over its MIDI CC and diffing the save;
        // the panel's slider row is Freq Q Amt Dec but the chunk wedges Pattern
        // between Amt and Dec. See docs/FORMAT_RBS.md section 6.
        if (!pcf.empty() && pcf[0].size >= 7) {
            const unsigned char* p = pcf[0].data;
            fx.pcfFreq     = float(p[1]) / 127.0f;
            fx.pcfQ        = float(p[2]) / 127.0f;
            fx.pcfAmt      = float(p[3]) / 127.0f;
            fx.pcfPattern  = (p[4] <= 54) ? int(p[4]) : 54;
            fx.pcfDecay    = float(p[5]) / 127.0f;
            fx.pcfBandpass = p[6] != 0;
            fx.present = true;
        }
    }

    // The chunks are the PANEL as it stood at save time. Up to here effectInit
    // and mixerInit hold nothing but chunk values and defaults, so snapshot them
    // before the tracks' tick-0 dumps override the start state. The writer saves
    // the panel back as chunks and the start state as tracks, which is how a
    // save keeps both (review W1 to W3).
    out.mixerPanel  = out.mixerInit;
    out.effectPanel = out.effectInit;
    // COMP (8): [ver][ratio][threshold][meter, 127]. Byte 0 reads 1 in all
    // sixteen demo songs, Fast Forward included, whose COMP track keeps the unit
    // off for the whole song, and it is the version byte on every other effect
    // chunk, so it is read as a version and not as the on/off. The panel only:
    // what the compressor plays comes from the COMP track below.
    {
        std::vector<Chunk> comp;
        findLeaves(data, 0, size, "COMP", comp);
        if (!comp.empty() && comp[0].size >= 3) {
            out.effectPanel.compRatio     = float(comp[0].data[1]) / 127.0f;
            out.effectPanel.compThreshold = float(comp[0].data[2]) / 127.0f;
            out.effectPanel.present = true;
        }
    }

    // --- Arrangement (TRAK tracks) ---
    // Track order: MIXR, 303a, 303b, 808, 909, DELY, DIST, PCF, COMP.
    std::vector<Chunk> traks;
    findLeaves(data, 0, size, "TRAK", traks);

    bool haveArrangement = false;
    if (traks.size() >= 5) {
        std::vector<TrakEvent> ev;
        std::vector<std::pair<uint32_t, int>> chg[4];   // 303a, 303b, 808, 909
        uint32_t lastTime = 0;
        bool parsedAll = true;
        const AutoTarget targets[4] = { AutoTarget::Bass1, AutoTarget::Bass2,
                                        AutoTarget::Drum808, AutoTarget::Drum909 };
        for (int d = 0; d < 4; ++d) {
            if (!parseTrak(traks[1 + d].data, traks[1 + d].size, ev)) { parsedAll = false; break; }
            patternTimeline(ev, chg[d], lastTime);
            // Collect knob/mute automation. p0 = mute toggle (0 = muted),
            // p1 = pattern select (already consumed by patternTimeline), and
            // from p2 up the panel knobs in panel order. The 303s stop at p8
            // (tune, cutoff, reso, envmod, decay, accent, waveform); the DRUM
            // machines run to p28 (808) and p29 (909), one param per header
            // byte. This filter used to stop at 8 for every device, so every
            // drum knob ride in every shipped song was silently discarded,
            // including the tick-0 dump that is the song's true starting state.
            const uint8_t maxParam = (d < 2) ? 8 : 29;
            for (const TrakEvent& e : ev)
                if (e.param == 0 || (e.param >= 2 && e.param <= maxParam))
                    out.automation.push_back({ e.time, targets[d], e.param, e.value });
        }
        // Mixer initial state (volume/pan per channel from the time-0 dump),
        // plus timed volume/pan automation.
        if (parsedAll && parseTrak(traks[0].data, traks[0].size, ev)) {
            for (const TrakEvent& e : ev) {
                if (e.time > lastTime) lastTime = e.time;
                // params 1 and 2 are the compressor's and the PCF's targets.
                // The chunk holds the panel at SAVE time and this dump is the
                // song's START, and for the PCF the two disagree in every
                // shipped song: MIXR +2 is zero in all sixteen while eleven of
                // them route the PCF here. Reading the chunk alone leaves the
                // PCF silent in every demo.
                //
                // AND THE ROUTING IS PERFORMED, not just set. G-Spot moves
                // param 2 thirty-five times: the PCF is on the 909 for the
                // first 38 seconds, off for the next 166, back on for 25, and
                // then flickering on and off every few beats to the end. Held
                // at its opening value the filter runs over the whole song,
                // which is what made our drums dull where ReBirth's are not.
                // Eleven of the sixteen demo songs ride this.
                if (e.param == 1 || e.param == 2) {
                    if (e.time != 0) {
                        out.automation.push_back({ e.time, AutoTarget::Mixer,
                                                   e.param, e.value });
                        continue;
                    }
                    if (e.param == 1)
                        out.effectInit.compTarget = (e.value == 1) ? kCompMaster
                                                  : (e.value >= 2 && e.value <= 5) ? e.value - 2 : -1;
                    else
                        out.effectInit.pcfTarget  = (e.value >= 1 && e.value <= 4) ? e.value - 1 : -1;
                    out.effectInit.present = true;
                    continue;
                }
                for (int ch = 0; ch < 4; ++ch) {
                    int base = 6 + 6 * ch;
                    if (e.param < base || e.param > base + 3) continue;
                    if (e.time == 0) {
                        if (e.param == base)          out.mixerInit.volume[ch] = faderGain(e.value, kChannelUnityGain);
                        else if (e.param == base + 1) out.mixerInit.pan[ch] = panFromByte(e.value);
                        else if (e.param == base + 3) {
                            // base+3 is this section's DISTORTION switch, and
                            // it agrees byte for byte with the MIXR chunk's
                            // channel-group +4 across the corpus.
                            out.effectInit.distOn[ch] = e.value != 0;
                            out.effectInit.present = true;
                        }
                        else {
                            // base+2 = echo send. The TRAK time-0 dump is the
                            // authoritative song-start state (the MIXR chunk
                            // holds save-time knob positions, which can be
                            // zero while the song opens with live sends,
                            // e.g. G-Spot's dub delay).
                            out.effectInit.delaySend[ch] = float(e.value) / 127.0f;
                            out.effectInit.present = true;
                        }
                        out.mixerInit.present = true;
                    } else {
                        out.automation.push_back({ e.time, AutoTarget::Mixer, e.param, e.value });
                    }
                }
            }
        }
        // Effect tracks are positions 5..8 and the order is DELY, DIST, PCF,
        // COMP, NOT the DELY, PCF, DIST, COMP this file used to claim. Track
        // 6 carries only params 0..2 and the distortion has two knobs plus its
        // on/off; track 7 carries params 0..6 and the PCF has six plus its
        // on/off, and track 7's tick-0 dump reproduces the PCF chunk byte for
        // byte. As with the drums, param N is chunk byte N and param 0 is the
        // unit's on/off.
        //
        // Track 5 is the DELAY: param 0 the unit's on/off, 1 Steps, 2 the grid
        // (nonzero = straight), 3 Feedback, 4 the wet-return pan, chunk byte
        // order. Tick 0 is the START and overrides the chunk, which stays in
        // effectPanel. Measured 2026-09-26 (review E3): with the DELY chunk
        // patched to disagree with this dump on steps, feedback and pan at once,
        // ReBirth's Song mode played the track's values to 0.1 dB, and a copy
        // naming a mod played them too. The two disagree in 8 of the 16 demo
        // songs (Trakka Rekka's chunk has feedback 0 and pan 127, its track
        // opens at 36 and 63), and until then we played the chunk. The timed
        // rides are performed: 4435 across the corpus, G-Spot alone 2311.
        if (parsedAll && traks.size() >= 9 && parseTrak(traks[5].data, traks[5].size, ev)) {
            EffectInit& fx = out.effectInit;
            for (const TrakEvent& e : ev) {
                if (e.time != 0) {
                    if (e.param <= 4)
                        out.automation.push_back({ e.time, AutoTarget::Delay, e.param, e.value });
                    continue;
                }
                switch (e.param) {
                case 0: fx.delayOn       = (e.value != 0); break;
                // Clamped as the engine clamps a ride, so a tick-0 value and a
                // timed one read the same. The chunk's range is 1..16 too.
                case 1: fx.delaySteps    = e.value < 1 ? 1 : (e.value > 16 ? 16 : int(e.value)); break;
                case 2: fx.delayStraight = (e.value != 0); break;
                case 3: fx.delayFeedback = float(e.value) / 127.0f; break;
                case 4: fx.delayPan      = e.value; break;
                default: continue;
                }
                fx.present = true;
            }
        }
        // Track 6 is the DISTORTION. Param 0 = the unit's on/off, 1 = Shape
        // (distTone), 2 = Amount (distAmount), in chunk byte order. Tick 0 is the
        // authoritative START (the chunk is only the save-time panel), and the
        // TIMED rides are performed heavily: Mutilated Species II alone carries
        // 1241 Amount rides and 319 Shape, TGV's KiloMix 929/632, Groove It Up
        // 354/151. Several songs (Mutilated, KiloMix, SuperNova, Transfusion)
        // START at Amount 0 on the track and ride it up, so freezing at tick 0
        // rendered them with the distortion off for the whole piece (item 4).
        if (parsedAll && traks.size() >= 9 && parseTrak(traks[6].data, traks[6].size, ev)) {
            EffectInit& fx = out.effectInit;
            for (const TrakEvent& e : ev) {
                if (e.time != 0) {
                    if (e.param <= 2)
                        out.automation.push_back({ e.time, AutoTarget::Dist, e.param, e.value });
                    continue;
                }
                switch (e.param) {
                case 0: fx.distortionOn = (e.value != 0); break;
                case 1: fx.distTone   = float(e.value) / 127.0f; break;   // Shape
                case 2: fx.distAmount = float(e.value) / 127.0f; break;   // Amount
                default: continue;
                }
                fx.present = true;
            }
        }
        if (parsedAll && traks.size() >= 9 && parseTrak(traks[7].data, traks[7].size, ev)) {
            EffectInit& fx = out.effectInit;
            for (const TrakEvent& e : ev) {
                if (e.time != 0) {
                    // Timed rides. Songs use these heavily: Mutilated Species II
                    // alone carries 1242 events on the PCF's Q and 13 on its
                    // on/off, and KiloMix 930. Held flat, any song that performs
                    // its filter renders with the opening setting.
                    if (e.param <= 6)
                        out.automation.push_back({ e.time, AutoTarget::Pcf, e.param, e.value });
                    continue;
                }
                switch (e.param) {
                case 0: fx.pcfOn = (e.value != 0); break;
                case 1: fx.pcfFreq = float(e.value) / 127.0f; break;
                case 2: fx.pcfQ = float(e.value) / 127.0f; break;
                case 3: fx.pcfAmt = float(e.value) / 127.0f; break;
                case 4: fx.pcfPattern = (e.value <= 54) ? int(e.value) : 54; break;
                case 5: fx.pcfDecay = float(e.value) / 127.0f; break;
                case 6: fx.pcfBandpass = (e.value != 0); break;
                default: continue;
                }
                fx.present = true;
            }
        }
        if (parsedAll && traks.size() >= 9 && parseTrak(traks[8].data, traks[8].size, ev)) {
            EffectInit& fx = out.effectInit;
            for (const TrakEvent& e : ev) {
                if (e.time != 0) {
                    // Timed rides, played as AutoTarget::Comp: ReBirth plays
                    // them (measured 2026-10-01, comp-rides cr_*), and the demo
                    // songs carry 156, 65 on Ratio and 91 on Threshold.
                    if (e.param <= 3)
                        out.automation.push_back({ e.time, AutoTarget::Comp, e.param, e.value });
                    continue;
                }
                switch (e.param) {
                // COMP track: param 0 on/off, 1 Ratio, 2 Threshold (byte/127),
                // 3 the level-reduction meter (display, not a control). Decoded
                // 2026-09-16 for supertask 6; the byte->behaviour law is in the
                // compressor DSP.
                case 0: fx.compressorOn = (e.value != 0); break;
                case 1: fx.compRatio = float(e.value) / 127.0f; break;
                case 2: fx.compThreshold = float(e.value) / 127.0f; break;
                default: continue;
                }
                fx.present = true;
            }
        }

        std::stable_sort(out.automation.begin(), out.automation.end(),
                         [](const AutoEvent& a, const AutoEvent& b) { return a.tick < b.tick; });

        if (parsedAll) {
            uint32_t bars = lastTime / kTicksPerBar + 1;
            if (bars > 2048) bars = 2048;               // sanity cap (~17 min at 125 BPM)
            out.arrangement.clear();
            out.arrangement.reserve(bars);
            int cur[4] = { 0, 0, 0, 0 };
            size_t idx[4] = { 0, 0, 0, 0 };
            for (uint32_t bar = 0; bar < bars; ++bar) {
                for (int d = 0; d < 4; ++d)
                    while (idx[d] < chg[d].size() && chg[d][idx[d]].first <= bar)
                        cur[d] = chg[d][idx[d]++].second;
                SongStep row;
                row.bass1Pattern = cur[0]; row.bass2Pattern = cur[1];
                row.drum808Pattern = cur[2]; row.drum909Pattern = cur[3];
                row.repeats = 1;
                // Merge identical consecutive bars into one row with repeats.
                if (!out.arrangement.empty()) {
                    SongStep& prev = out.arrangement.back();
                    if (prev.bass1Pattern == row.bass1Pattern && prev.bass2Pattern == row.bass2Pattern
                        && prev.drum808Pattern == row.drum808Pattern && prev.drum909Pattern == row.drum909Pattern) {
                        prev.repeats++; continue;
                    }
                }
                out.arrangement.push_back(row);
            }
            haveArrangement = !out.arrangement.empty();
        }
    }

    if (!haveArrangement) {
        // Fallback: loop each device's first contentful pattern.
        SongStep row;
        row.bass1Pattern   = firstContentful(out.bass1);
        row.bass2Pattern   = firstContentful(out.bass2);
        row.drum808Pattern = firstContentful(out.drum808);
        row.drum909Pattern = firstContentful(out.drum909);
        row.repeats = 1;
        out.arrangement.assign(1, row);
    }

    if (err) err->clear();
    return true;
}

bool loadRbsFile(const std::string& path, Song& out, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "cannot open file"; return false; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> buf(sz > 0 ? size_t(sz) : 0);
    if (sz > 0 && std::fread(buf.data(), 1, size_t(sz), f) != size_t(sz)) {
        std::fclose(f); if (err) *err = "read error"; return false;
    }
    std::fclose(f);
    return loadRbs(buf.data(), buf.size(), out, err);
}

} // namespace reborn
