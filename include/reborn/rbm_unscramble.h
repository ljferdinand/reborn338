// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Unscrambling a `.rbm` mod's embedded files.
//
// Measured by black-box experiment only, by packing chosen folders with the
// ModPacker utility that ships with ReBirth and comparing its output (see
// docs/FORMAT_RBM.md, "The scrambling, measured"; no program was disassembled):
// each EMBF payload (file name, NUL, file) is stored as written for its first
// 1024 bytes, and from byte 1024 on every byte has a key byte added (mod 256).
// The key for the entry at archive index i is K[i % 2][(o - 1024) % (140 + 17 i)]:
// two key sequences per archive, under 2 KB each.
//
// The keys are GENERATED (generateRbmKeys, src/io/rbm_keygen.cpp). The function
// ModPacker makes them with was measured the same way, black-box, from its output
// for packs of chosen content: two sums over the readable first kilobyte of every
// entry pick where each sequence starts in a stream of chunks, each chunk rand
// bytes from a seeded LCG with a fixed set of words written over them. It needs
// nothing but the archive, it is exact for every well-formed archive ModPacker
// makes (from a folder with a ModName.txt; without one ModPacker writes a CAT of
// length 0 and other keys), a partial mod included, and generating takes under
// 0.2 ms. Checked on the four retail mods (every key byte, and all 424 files decode
// byte for byte as ModPacker's own keys decode them), on 1118 sequences from packs
// of chosen content, and on twelve partial mods of random size and content.
//
// The keys can still be ESTIMATED from the content (estimateRbmKeys), which is the
// fallback for an archive the generated keys do not fit: one ModPacker did not make,
// one it packed without a ModName.txt, or one edited after packing. The estimate
// rests on the content alone: much of a mod is silence or
// zero runs (so the most common stored byte at a key position is usually the
// key itself), drum voices are predictable audio (so each key byte is refined to
// make the decoded audio most predictable, singly and in runs), and mods store
// some voices twice or once scaled (so each copy pins the other). On the four
// retail mods three estimates are exact and the fourth is exact but for 319 bytes
// in three voices, the worst of them 39 dB clean. See docs/FORMAT_RBM.md.
//
// Either way the result is checked (checkUnscrambledRbm) and refused if it fails.
//
#include <cstdint>
#include <vector>

namespace reborn {

// What checkUnscrambledRbm found in an unscrambled image. Its tests, each aimed at a way a
// key can be wrong, are calibrated on the four retail mods (docs/FORMAT_RBM.md):
//  - JPEG scans: past the readable part an unscrambled scan holds FF only as stuffing, a
//    restart or a marker up to its EOI; a wrong key byte that decodes to FF breaks that
//    three times in four.
//  - Key positions, two ways. Each sample past the readable part is set against its
//    neighbours, by level and by prediction residual, under the key position of its most
//    significant byte, and a position is judged by the lower median over its samples. A
//    wrong high byte stands out by level in every period of every voice it reaches; a run
//    of key bytes offset alike leaves residual steps at both its ends.
//  - Voices: a key wrong everywhere makes the audio past the readable part far less
//    predictable than inside it (voices whose readable start is near silence excepted).
//  - Evidence: under ESTIMATED keys, an archive with fewer than 12 voices reaching past
//    the readable part is refused (a policy, open in BACKLOG 17). At 3 voices the other
//    tests passed estimates as bad as 5 dB. The count is a rough proxy, though: what
//    decides is how well the last files in the archive pin the top of each key, and
//    estimates of 12 to 46 voice archives have passed with a noisy voice 20 to 24 dB
//    clean. Generated keys are judged without it (requireEvidence false), and in its
//    place unscrambleRbm confirms each key parity that holds audio on its own: by enough
//    chances for the wrap test to catch a wrong key there, by audio too far from uniform
//    for an unrelated key to have decoded it (which is what reads hats, cymbals and noise
//    snares, where the check itself passes a wrong key as readily as the right one), or
//    else by the check refusing the archive with that parity's key replaced by unrelated
//    ones.
//  - Wraps: a voice whose readable steps all stay under half scale and that then steps
//    more than three quarters of scale has had a sample near full scale wrapped to the
//    other end, which is what a high byte off by one does to a loud voice.
// What it cannot see: a constant offset on a whole class of key positions, which shifts a
// voice's DC (removed when a mod is loaded) or its lowest bit, where no sample is loud
// enough to wrap; offsets of one step under loud noise; generated keys that are close but not
// right, which they are for an archive edited in its readable part after packing by a multiple
// of 2^22 in a sum (they then differ in 11 to 20 bytes; unscrambleRbm sets the generated keys
// against those neighbours itself); and keys right for some entries of a parity and wrong for
// others (one whose entries were reordered after packing).
struct RbmCheck {
    int jpegsChecked = 0;        // JPEGs with scan data past the readable part
    int jpegsClean = 0;          // of those, how many hold no illegal FF pair in it
    int positionsChecked = 0;    // key positions with at least three samples to judge
    int positionsBad = 0;        // positions above 4 by level or 3.5 by residual (each counted)
    double worstPosition = 0.0;  // the largest position level (about 1 to 3 for a right key)
    double worstResidual = 0.0;  // the largest position residual (about 1 to 3 for a right key)
    int voicesScrambled = 0;     // voice channels with audio past the readable part
    int voicesUnjudged = 0;      // of those, any the check could not read (over 2 channels)
    bool tooFewVoices = false;   // fewer than 12 of them: too little evidence to trust an estimate
    int voicesChecked = 0;       // channels whose readable start can anchor the residual ratio
    double residualRatio = 0.0;  // median over those of the predictor residual past the readable
                                 // part against inside it (predictor fitted inside it)
    int voicesWrapped = 0;       // channels whose readable steps stay under half scale and that
                                 // then step over three quarters of it: a sample wrapped
    bool ok = false;             // all of: judged at all if scrambled, 12 voices or none (when
                                 // evidence is required), every voice readable, 90 percent of
                                 // judged JPEGs clean, ratio under 4, fewer than 2 bad
                                 // positions, no voice wrapped
};

struct RbmUnscrambleReport {
    int entries = 0;             // EMBF entries found
    int scrambled = 0;           // entries longer than the readable 1024 bytes
    int keyLength[2] = { 0, 0 }; // key bytes generated or estimated, even and odd entries
    uint32_t sum[2] = { 0, 0 };  // the archive's key sums, even and odd, the key function's input
    bool generated = false;      // the keys are the generated ones (exact), not an estimate
    bool generatedRefused = false; // the generated keys were tried and not taken
    bool generatedUnconfirmed = false; // of those, keys that passed the check but on a parity
                                 // where it also passed unrelated keys (too little evidence)
    RbmCheck generatedCheck;     // the self-check of the generated keys, when they were tried
    int wrapSteps[2] = { 0, 0 }; // generated keys: the wrap test's chances to catch a wrong key,
                                 // per parity, on its longest chain (280 confirm it outright)
    double unrelatedOdds[2] = { 1.0, 1.0 }; // generated keys: the odds an unrelated key would
                                 // decode the parity's audio as far from uniform (under 1e-6
                                 // confirms it), when the wrap test did not
    bool nearMiss[2] = { false, false }; // generated keys refused on a parity because a near
                                 // neighbour's key decodes it better (an archive edited after
                                 // packing: the generated key is close but not right)
    int nearKeys = 0;            // generated keys: near neighbours' keys set against them
    int decoys = 0;              // generated keys: unrelated keys the check was run under
    bool estimated = false;      // the keys were estimated (the fallback ran and produced keys)
    int audioStreams = 0;        // estimate: 16-bit AIFF channels used as evidence
    int twins = 0;               // estimate: voice pairs stored twice or scaled, used as evidence
    int sweeps = 0;              // estimate: refinement sweeps run
    int rounds = 0;              // estimate: chain-and-sweep rounds run
    bool tooLarge = false;       // refused unread: over 32 MB, 1024 entries or 8 M audio samples
    double seconds = 0.0;        // time the keys took
    RbmCheck check;              // the self-check of the result (unscrambleRbm only)
    bool ok = false;             // the unscrambled image passed the self-check
};

// The archive index of each EMBF entry, its payload offset and length, in file order.
struct RbmEntry {
    std::size_t payload = 0;     // offset of the payload (name, NUL, file) in the image
    std::size_t length = 0;      // payload length in bytes
    std::size_t nameLength = 0;  // name bytes before the NUL
};
std::vector<RbmEntry> rbmEntries(const std::vector<unsigned char>& image);

// The key function (docs/FORMAT_RBM.md, "The key function").
//
// The two sums an archive's keys come from. Each entry's payload is read as its first
// 1024 bytes, filled out with 0x65 past its end if it is shorter, and as 256 big-endian
// words: the first 128 add into sum[1] and the other 128 into sum[0], modulo 2^32. Both
// lie in the readable part, so a scrambled archive gives the same sums as its original.
void rbmKeySums(const std::vector<unsigned char>& image, uint32_t sum[2]);

// The first `length` bytes of the key sequence for one parity (0 even entries, 1 odd)
// of an archive whose sum for that parity is `sum`. A longer length extends the same
// sequence. Exact over the first two chunks of the generator, 2049 to 4096 bytes
// depending on where the sequence starts, which covers every key ModPacker writes (its
// archives hold at most 106 files, so no key reaches past 1925 bytes).
std::vector<uint8_t> rbmKeySequence(int parity, uint32_t sum, std::size_t length);

// Generate both key sequences of a mod image, each as long as its entries reach. False
// only if the image holds no entry. Cheap at any size. Exact for an archive ModPacker
// made from a folder with a ModName.txt and not edited since; for any other they are
// wrong, usually everywhere on at least one parity, which unscrambleRbm judges.
bool generateRbmKeys(const std::vector<unsigned char>& image,
                     std::vector<uint8_t>& keyEven, std::vector<uint8_t>& keyOdd,
                     RbmUnscrambleReport* report = nullptr);

// Estimate both key sequences of a mod image. False if the image is not a mod, or is no
// archive ModPacker could have made (report.tooLarge: over 32 MB, over 1024 entries, or
// over 8 million audio samples, past which the estimate's memory would run to gigabytes).
bool estimateRbmKeys(const std::vector<unsigned char>& image,
                     std::vector<uint8_t>& keyEven, std::vector<uint8_t>& keyOdd,
                     RbmUnscrambleReport* report = nullptr);

// Add (sign +1) or remove (sign -1) the given keys over every payload from byte 1024.
// Key bytes past a sequence's end are left unapplied.
void applyRbmKeys(std::vector<unsigned char>& image, const std::vector<uint8_t>& keyEven,
                  const std::vector<uint8_t>& keyOdd, int sign);

// Check an image that has been unscrambled (or never was scrambled). requireEvidence
// applies the 12-voice minimum, which guards estimated keys.
RbmCheck checkUnscrambledRbm(const std::vector<unsigned char>& image, bool requireEvidence = true);

// Unscramble the image in place and check the result. Returns report.ok, and leaves the
// image as stored unless it is true. Past 32 MB, 1024 entries or 8 million audio samples
// (any bit depth) it is refused unread (report.tooLarge). The generated keys come first,
// on an archive whose top-level CAT spans the file, judged without the evidence minimum,
// and taken only when each parity that holds scrambled audio is confirmed on its own (by
// 280 wrap-test steps on one chain, by odds under 1e-6 that an unrelated key would decode
// its audio as far from uniform, or by the check refusing 64 unrelated keys on it), and
// refused if the key of a near neighbouring sum (sum + m 2^22) decodes a parity better, which
// is what an archive edited after packing gives. Keys that cannot be confirmed or are refused so on
// an archive of 1 to 11 voices refuse it outright; otherwise the keys are estimated and
// judged with the minimum. The keys last tried are handed back when asked for, taken or
// not (for diagnostics). `estimate` skips the generated keys.
bool unscrambleRbm(std::vector<unsigned char>& image, RbmUnscrambleReport* report = nullptr,
                   std::vector<uint8_t>* keyEven = nullptr, std::vector<uint8_t>* keyOdd = nullptr,
                   bool estimate = false);

} // namespace reborn
