// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// The key function of a `.rbm` mod: the two key sequences ModPacker scrambles an
// archive with, computed from the archive itself. Measured by black-box experiment
// only, by packing chosen folders with ModPacker and reading the keys off its output
// (no program was disassembled). See include/reborn/rbm_unscramble.h and
// docs/FORMAT_RBM.md, "The key function", for what each step rests on.
#include "reborn/rbm_unscramble.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace reborn {
namespace {

// As in rbm_unscramble.cpp: the readable part of a payload and the key period of entry i.
constexpr std::size_t kReadable = 1024;
inline std::size_t period(std::size_t i) { return 140 + 17 * i; }

constexpr std::size_t kChunk = 2048;            // key bytes per chunk of the generator
constexpr unsigned char kFill = 0x65;           // what a payload shorter than 1 KB is summed as past its end
constexpr uint32_t kOffset[2] = { 0x3000, 0x2000 };  // added to the sum: even entries, odd entries
constexpr uint32_t kSeedBase = 0x1742C6;        // chunk seed = kSeedBase + kSeedStep * T, 24 bits
constexpr uint32_t kSeedStep = 0x220800;
constexpr uint32_t kLcgMul = 214013;            // the rand bytes: a 24-bit LCG on MSVC's
constexpr uint32_t kLcgAdd = 2531011;           // rand() constants, bits 16..23 of each state
constexpr uint32_t kState = 0xFFFFFF;

inline uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint32_t bit(uint32_t w, int i) { return (w >> i) & 1u; }

// A chunk's rand bytes as little-endian words: R(p) is bytes p..p+3.
struct Rand {
    const unsigned char* b;
    uint32_t operator()(std::size_t p) const {
        return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) | (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
    }
};

// The special words. A chunk of segment type t = (T >> 16) & 3 has a fixed set of
// little-endian 32-bit words written over its rand bytes, each from w = (T & 0xFFFF) >> 2:
// the word at `slot` becomes (R(slot) & own) | (value(w, R) & ~own), every value modulo
// 2^32, R the chunk's rand bytes as they were before any special was written. Fitted to
// 9723 observed words with none wrong, and between them they cover exactly the key bytes
// that are not rand bytes.
struct Special { uint16_t slot; uint32_t own; uint32_t (*value)(uint32_t w, const Rand& R); };

const Special kType0[] = {
    { 128,  0xF0000FFF, [](uint32_t w, const Rand&) -> uint32_t { return ((2 * w) & 0xFFFF) << 12; } },
    { 164,  0xFFFF0FFF, [](uint32_t, const Rand&) -> uint32_t { return 0; } },
    { 168,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 3; } },
    { 176,  0xFFF00FFF, [](uint32_t w, const Rand&) -> uint32_t { return ((((w & 0xF3) + 10 + bit(w, 7)) >> 1) & 0xFF) << 12; } },
    { 268,  0xC00000FF, [](uint32_t w, const Rand&) -> uint32_t { return ((0x38 * w + 0xA8) & 0x3FFFFF) << 8; } },
    { 352,  0xFF000000, [](uint32_t w, const Rand&) -> uint32_t { return (7 * w + 0xE) & 0xFFFFFF; } },
    { 444,  0xFFF00FFF, [](uint32_t w, const Rand&) -> uint32_t { return (((w & 0xF3) + 10 + ((w & 1) << 7)) & 0xFF) << 12; } },
    { 464,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 3 * w + 0x15; } },
    { 532,  0x00000000, [](uint32_t w, const Rand& R) -> uint32_t { return 3 * R(400) + 19 * w + 0x4E; } },
    { 556,  0xF000000F, [](uint32_t w, const Rand&) -> uint32_t { return ((w & 0x321) << 6) | 0x1000; } },
    { 580,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 5 * w + 0x35D56; } },
    { 596,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0xD; } },
    { 624,  0x00000000, [](uint32_t w, const Rand& R) -> uint32_t { return 3 * R(400) + 5 * w + 0x14; } },
    { 784,  0x0000FFFF, [](uint32_t w, const Rand&) -> uint32_t { return ((2 * w + 4) & 0xFFFF) << 16; } },
    { 1444, 0xFF00000F, [](uint32_t w, const Rand&) -> uint32_t { return (((w >> 4) & 0xFF) + 0xFA) << 8; } },
    { 1564, 0xFFFFFF00, [](uint32_t w, const Rand&) -> uint32_t { return (w + 6) & 0xFF; } },
    { 1628, 0xFFFF00FF, [](uint32_t w, const Rand&) -> uint32_t { return ((w + 6) >> 8) << 8; } },
    { 1768, 0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 7 * w + 0x14867; } },
    { 2016, 0xFF00000F, [](uint32_t w, const Rand&) -> uint32_t { return ((w & 0x222) + 11) << 4; } },
    { 2040, 0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 0x78 - w * w; } },
};
const Special kType1[] = {
    { 16,   0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x1A0F; } },
    { 36,   0xFF00000F, [](uint32_t w, const Rand&) -> uint32_t { return ((w & 0x123) << 5) | 0x4000; } },
    { 44,   0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 5 * w + 0x3344114; } },
    { 56,   0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 4 * w + 0x3C; } },
    { 164,  0xFFF000FF, [](uint32_t w, const Rand&) -> uint32_t { return 0x1800u << (4 * bit(w, 2)); } },
    { 172,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 0x3333223B + (w & 0xFF); } },
    { 224,  0x00000000, [](uint32_t w, const Rand& R) -> uint32_t { return (R(396) & 0xFFF) | (((4 * w + 0x48) & 0xFFFFF) << 12); } },
    { 396,  0x00000FFF, [](uint32_t w, const Rand&) -> uint32_t { return ((w + 4) & 0xFFFFF) << 12; } },
    { 408,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 0x06A12082 | ((((w >> 4) & 3) + 3) << 4) | ((((w >> 8) & 15) + 5) << 8); } },
    { 940,  0x00000000, [](uint32_t w, const Rand& R) -> uint32_t { return (R(396) & 0xFFF) | (((w + 8) & 0xFFFFF) << 12); } },
    { 1592, 0xFFF00FFF, [](uint32_t w, const Rand&) -> uint32_t { return ((w & 0xAA) + 12) << 12; } },
};
const Special kType2[] = {
    { 0,    0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 3 * w + 0x55FB; } },
    { 4,    0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 9 * w + 0x109CD; } },
    { 24,   0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x1ED1; } },
    { 28,   0x00000000, [](uint32_t, const Rand& R) -> uint32_t { return R(84) + 1; } },
    { 148,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x186A4; } },
    { 192,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 0x3FFF * w + 0x363; } },
    { 396,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x445; } },
    { 500,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 3 * w + 0x7FFE; } },
    { 536,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x1B208; } },
    { 564,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x1D4E2; } },
    { 588,  0x00000000, [](uint32_t, const Rand& R) -> uint32_t { return R(992) + 8; } },
    { 780,  0x0000FFFF, [](uint32_t w, const Rand&) -> uint32_t { return (((w & 0x7F) + 0x7F) & 0xFF) << 16; } },
    { 1556, 0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x99; } },
    { 1616, 0xFF00000F, [](uint32_t w, const Rand&) -> uint32_t { return (w & 0x123) << 5; } },
    { 2032, 0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0xADA7; } },
    { 2044, 0xFFF000FF, [](uint32_t w, const Rand&) -> uint32_t { return (0x28u << (4 * bit(w, 2))) << 8; } },
};
const Special kType3[] = {
    { 76,   0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 0x33F3 + 13 * (w & 15); } },
    { 116,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return 5 * w + 0x27; } },
    { 332,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0x50; } },
    { 396,  0x00000000, [](uint32_t w, const Rand&) -> uint32_t { return w + 0xA; } },
    { 1468, 0xFF00000F, [](uint32_t w, const Rand&) -> uint32_t { return ((w & 0x123) << 5) + 0x7D00; } },
};

template <std::size_t N> void writeSpecials(const Special (&table)[N], uint32_t w, const Rand& R, unsigned char* raw) {
    for (const Special& s : table) {
        const uint32_t v = (R(s.slot) & s.own) | (s.value(w, R) & ~s.own);
        for (int k = 0; k < 4; ++k) raw[s.slot + std::size_t(k)] = (unsigned char) (v >> (8 * k));
    }
}

// The 2048 raw bytes of the chunk with counter T: rand bytes from the seed T gives, then
// the special words of its segment type written over them.
void chunk(uint32_t T, unsigned char* raw) {
    unsigned char rnd[kChunk];
    uint32_t st = (kSeedBase + kSeedStep * T) & kState;
    for (std::size_t k = 0; k < kChunk; ++k) {
        st = (kLcgMul * st + kLcgAdd) & kState;
        rnd[k] = (unsigned char) (st >> 16);
    }
    std::memcpy(raw, rnd, kChunk);
    const Rand R{ rnd };
    const uint32_t w = (T & 0xFFFF) >> 2;
    switch ((T >> 16) & 3) {
    case 0: writeSpecials(kType0, w, R, raw); break;
    case 1: writeSpecials(kType1, w, R, raw); break;
    case 2: writeSpecials(kType2, w, R, raw); break;
    default: writeSpecials(kType3, w, R, raw); break;
    }
}

void sumsOf(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries, uint32_t sum[2]) {
    sum[0] = sum[1] = 0;
    for (const RbmEntry& e : entries) {
        unsigned char window[kReadable];
        const std::size_t n = std::min(e.length, kReadable);
        std::memcpy(window, image.data() + e.payload, n);
        std::memset(window + n, kFill, kReadable - n);
        for (std::size_t k = 0; k < kReadable; k += 4) sum[k < kReadable / 2 ? 1 : 0] += be32(window + k);
    }
}

} // namespace

void rbmKeySums(const std::vector<unsigned char>& image, uint32_t sum[2]) {
    sumsOf(image, rbmEntries(image), sum);
}

std::vector<uint8_t> rbmKeySequence(int parity, uint32_t sum, std::size_t length) {
    std::vector<uint8_t> key;
    key.reserve(length);
    // q's low 11 bits are where the sequence starts in its first chunk. The rest, times
    // four, are that chunk's counter, which skips one at every multiple of 65535, and
    // the next chunk's counter is four on, or five when (V mod 65535) + 4 reaches 65536
    // (measured: a step that lands exactly on a multiple does not count).
    // Measured for the first two chunks, which hold every key byte an archive of
    // ModPacker's 106 files reaches; past them the step is assumed to stay the same.
    const uint32_t q = sum + kOffset[parity & 1];                   // modulo 2^32
    std::size_t i = q & (kChunk - 1);
    const uint32_t V = 4 * (q >> 11);
    uint32_t T = V + V / 65535;
    const uint32_t step = V % 65535 + 4 >= 65536 ? 5 : 4;
    unsigned char raw[kChunk];
    while (key.size() < length) {
        chunk(T, raw);
        for (; i < kChunk && key.size() < length; ++i) key.push_back(uint8_t(0x100 - raw[i]));   // key = -raw
        i = 0;
        T += step;
    }
    return key;
}

bool generateRbmKeys(const std::vector<unsigned char>& image,
                     std::vector<uint8_t>& keyEven, std::vector<uint8_t>& keyOdd,
                     RbmUnscrambleReport* report) {
    const auto t0 = std::chrono::steady_clock::now();
    RbmUnscrambleReport rep;
    const std::vector<RbmEntry> entries = rbmEntries(image);
    rep.entries = int(entries.size());
    if (entries.empty()) { if (report) *report = rep; return false; }
    std::size_t length[2] = { 0, 0 };
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].length > kReadable) {
            ++rep.scrambled;
            length[i % 2] = std::max(length[i % 2], std::min(period(i), entries[i].length - kReadable));
        }
    sumsOf(image, entries, rep.sum);
    keyEven = rbmKeySequence(0, rep.sum[0], length[0]);
    keyOdd = rbmKeySequence(1, rep.sum[1], length[1]);
    rep.keyLength[0] = int(keyEven.size()); rep.keyLength[1] = int(keyOdd.size());
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (report) *report = rep;
    return true;
}

} // namespace reborn
