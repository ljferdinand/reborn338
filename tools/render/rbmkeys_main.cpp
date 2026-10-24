// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// reborn_rbmkeys: unscramble a mod as the readers do and report on the keys.
//
//   reborn_rbmkeys <mod.rbm> [--estimate] [--truth keys.bin] [--out keys.bin]
//   reborn_rbmkeys <mod.rbm> --keys keys.bin [--no-evidence]   self-check the mod under the given keys
//   reborn_rbmkeys <mod.rbm> --raw [--no-evidence]             self-check the mod as stored
//
// The keys are the generated ones when unscrambleRbm takes them, and estimated when it does
// not (unless it refuses outright); --estimate skips the generated keys and estimates
// outright. keys.bin is two big-endian u32-length-prefixed byte strings (even entries, then
// odd), the layout the black-box oracle tooling writes. With --truth, the keys last tried,
// taken or not, are scored position by position and byte by byte against exact keys.
// --keys and --raw run only the self-check, which is how its thresholds are calibrated
// against right and wrong keys; add --no-evidence to judge without the 12-voice minimum,
// as generated keys are. Exits 1 when the mod is refused.
#include "reborn/rbm_unscramble.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> slurp(const char* path) {
    std::vector<unsigned char> b;
    if (std::FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        if (n > 0) { b.resize(std::size_t(n)); if (std::fread(b.data(), 1, b.size(), f) != b.size()) b.clear(); }
        std::fclose(f);
    }
    return b;
}

static bool readKeys(const char* path, std::vector<uint8_t>& k0, std::vector<uint8_t>& k1) {
    const std::vector<unsigned char> b = slurp(path);
    std::size_t off = 0;
    for (std::vector<uint8_t>* k : { &k0, &k1 }) {
        if (off + 4 > b.size()) return false;
        const std::size_t n = (std::size_t(b[off]) << 24) | (std::size_t(b[off + 1]) << 16) | (std::size_t(b[off + 2]) << 8) | b[off + 3];
        off += 4; if (off + n > b.size()) return false;
        k->assign(b.begin() + long(off), b.begin() + long(off + n)); off += n;
    }
    return true;
}

static void printCheck(const reborn::RbmCheck& c) {
    std::printf("self-check: JPEG scans clean %d/%d, bad key positions %d/%d (worst level %.2f, residual %.2f), "
                "residual ratio %.3f over %d voices, %d voices past the readable part%s%s, %d wrapped -> %s\n",
                c.jpegsClean, c.jpegsChecked, c.positionsBad, c.positionsChecked, c.worstPosition, c.worstResidual,
                c.residualRatio, c.voicesChecked, c.voicesScrambled, c.tooFewVoices ? " (too few)" : "",
                c.voicesUnjudged ? ", some unreadable" : "", c.voicesWrapped, c.ok ? "ok" : "FAILED");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: reborn_rbmkeys <mod.rbm> [--estimate] [--truth keys.bin] [--out keys.bin]"
                             " | --keys keys.bin | --raw [--no-evidence]\n");
        return 2;
    }
    const char* truth = nullptr; const char* out = nullptr; const char* given = nullptr;
    bool raw = false, estimate = false, evidence = true;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--raw")) raw = true;
        else if (!std::strcmp(argv[i], "--estimate")) estimate = true;
        else if (!std::strcmp(argv[i], "--no-evidence")) evidence = false;
        else if (i + 1 < argc && !std::strcmp(argv[i], "--truth")) truth = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--out")) out = argv[++i];
        else if (i + 1 < argc && !std::strcmp(argv[i], "--keys")) given = argv[++i];
    }
    std::vector<unsigned char> image = slurp(argv[1]);
    if (image.empty()) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    if (reborn::rbmEntries(image).empty()) { std::fprintf(stderr, "not a mod\n"); return 1; }
    if (raw || given) {
        if (given) {
            std::vector<uint8_t> k0, k1;
            if (!readKeys(given, k0, k1)) { std::fprintf(stderr, "cannot read %s\n", given); return 1; }
            reborn::applyRbmKeys(image, k0, k1, -1);
        }
        printCheck(reborn::checkUnscrambledRbm(image, evidence));
        return 0;
    }
    const std::vector<unsigned char> original = image;
    reborn::RbmUnscrambleReport rep;
    std::vector<uint8_t> k0, k1;
    reborn::unscrambleRbm(image, &rep, &k0, &k1, estimate);
    if (rep.tooLarge) { std::printf("refused: over 32 MB, 1024 entries or 8 M audio samples\n"); return 1; }
    std::printf("entries %d, scrambled %d, key lengths %d/%d, sums %08X/%08X\n", rep.entries, rep.scrambled,
                rep.keyLength[0], rep.keyLength[1], unsigned(rep.sum[0]), unsigned(rep.sum[1]));
    if (rep.generated) {
        std::printf("keys generated, %.3f ms; wrap-test steps %d/%d, unrelated-key odds %.1e/%.1e, "
                    "unrelated keys checked %d, near keys checked %d\n", rep.seconds * 1000.0,
                    rep.wrapSteps[0], rep.wrapSteps[1], rep.unrelatedOdds[0], rep.unrelatedOdds[1], rep.decoys,
                    rep.nearKeys);
    } else {
        if (rep.generatedRefused) {
            const char* why = rep.generatedUnconfirmed
                ? "a parity's check also passes unrelated keys (too little evidence to confirm them)"
                : (rep.nearMiss[0] || rep.nearMiss[1])
                    ? "a near neighbouring sum's key decodes a parity better (edited after packing?)"
                    : "they fail the self-check, or move an entry";
            std::printf("generated keys not taken: %s; wrap-test steps %d/%d, unrelated-key odds %.1e/%.1e, "
                        "unrelated keys checked %d, near keys checked %d, near-miss %d/%d\n", why,
                        rep.wrapSteps[0], rep.wrapSteps[1], rep.unrelatedOdds[0], rep.unrelatedOdds[1], rep.decoys,
                        rep.nearKeys, int(rep.nearMiss[0]), int(rep.nearMiss[1]));
            printCheck(rep.generatedCheck);
        }
        if (rep.estimated)
            std::printf("keys estimated: audio streams %d, twins %d, sweeps %d, rounds %d, %.1f s\n",
                        rep.audioStreams, rep.twins, rep.sweeps, rep.rounds, rep.seconds);
        else
            std::printf("no keys estimated\n");
    }
    if (rep.generated || rep.estimated) printCheck(rep.check);
    std::printf("result: %s\n", rep.ok ? "unscrambled" : "REFUSED (the mod is left as stored)");
    if (out) {
        if (std::FILE* f = std::fopen(out, "wb")) {
            for (const std::vector<uint8_t>* k : { &k0, &k1 }) {
                const uint32_t n = uint32_t(k->size());
                const unsigned char hdr[4] = { (unsigned char) (n >> 24), (unsigned char) (n >> 16), (unsigned char) (n >> 8), (unsigned char) n };
                std::fwrite(hdr, 1, 4, f); std::fwrite(k->data(), 1, k->size(), f);
            }
            std::fclose(f);
        }
    }
    if (truth) {
        std::vector<uint8_t> t0, t1;
        if (!readKeys(truth, t0, t1)) { std::fprintf(stderr, "cannot read %s\n", truth); return 1; }
        for (int par = 0; par < 2; ++par) {
            const std::vector<uint8_t>& k = par ? k1 : k0; const std::vector<uint8_t>& t = par ? t1 : t0;
            const std::size_t n = std::min(k.size(), t.size()); std::size_t ok = 0;
            for (std::size_t p = 0; p < n; ++p) ok += k[p] == t[p];
            std::printf("parity %d: %zu/%zu key bytes exact\n", par, ok, n);
        }
        // Scored on the keys last tried, taken or not, so a refusal is scored too.
        std::vector<unsigned char> exact = original, tried = original;
        reborn::applyRbmKeys(exact, t0, t1, -1);
        reborn::applyRbmKeys(tried, k0, k1, -1);
        std::size_t wrong = 0;
        for (std::size_t b = 0; b < exact.size(); ++b) wrong += exact[b] != tried[b];
        std::printf("decoded bytes differing from the exact decode, under the keys tried: %zu of %zu\n",
                    wrong, exact.size());
    }
    return rep.ok ? 0 : 1;
}
