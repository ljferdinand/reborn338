// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// `.rbm` mod reader; see include/reborn/rbm.h and docs/FORMAT_RBM.md.
//
// Container:  CAT(PRBM) { HEAD, INFO, EMBF x106 }
// EMBF body:  [NUL-terminated original file name][file bytes verbatim]
// Audio:      46 AIFFs, of which 45 are voices named TR808*.aif / tr909*.aif;
//             the odd one out is Startup.aif and is deliberately not a voice
// Graphics:   59 JPEGs, plus one embedded Demo.rbs (59+46+1 = 106)
// Scrambled:  every EMBF payload past its first 1024 bytes, so every file is read
//             through readModImage, which unscrambles it (rbm_unscramble.h)
//
#include "reborn/rbm.h"
#include "reborn/rbm_unscramble.h"
#include "reborn/sampleset.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <mutex>
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace reborn {
namespace {

// IFF containers nested at most. A mod's chunks sit one level inside its CAT; a crafted
// file nesting thousands deep would otherwise overflow the stack, since every walk here
// recurses once per level.
constexpr int kMaxIffDepth = 16;

inline uint32_t rdU32BE(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
inline uint16_t rdU16BE(const unsigned char* p) {
    return uint16_t((uint16_t(p[0]) << 8) | uint16_t(p[1]));
}

// AIFF stores its sample rate as an 80-bit IEEE 754 extended float.
double readExtended80(const unsigned char* p) {
    const int expon = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mant = 0;
    for (int i = 2; i < 10; ++i) mant = (mant << 8) | p[i];
    if (expon == 0 && mant == 0) return 0.0;
    const double v = double(mant) * std::pow(2.0, double(expon - 16383 - 63));
    return (p[0] & 0x80) ? -v : v;
}

// Decode a big-endian PCM AIFF (8/16/24-bit, any channel count) to mono float.
bool decodeAiff(const unsigned char* p, std::size_t n,
                std::vector<float>& out, int& sampleRate) {
    if (n < 12 || std::memcmp(p, "FORM", 4) != 0 || std::memcmp(p + 8, "AIFF", 4) != 0)
        return false;
    std::size_t off = 12;
    int channels = 1, bits = 16;
    uint32_t frames = 0;
    const unsigned char* ssnd = nullptr;
    std::size_t ssndLen = 0;

    while (off + 8 <= n) {
        const unsigned char* id = p + off;
        const uint32_t len = rdU32BE(p + off + 4);
        const std::size_t body = off + 8;
        if (body + len > n) break;
        // COMM is 18 bytes per spec, but real mods ship SHORT ones: Metallicon
        // and Orbit2 write a 12-byte COMM with the 80-bit sample rate omitted.
        // Requiring 18 silently dropped 19 of their 46 voices, so accept any
        // COMM carrying at least channels+frames+bits and default the rate.
        if (std::memcmp(id, "COMM", 4) == 0 && len >= 8) {
            channels   = rdU16BE(p + body);
            frames     = rdU32BE(p + body + 2);
            bits       = rdU16BE(p + body + 6);
            sampleRate = (len >= 18)
                       ? (int) std::lround(readExtended80(p + body + 8))
                       : 44100;
        } else if (std::memcmp(id, "SSND", 4) == 0 && len >= 8) {
            const uint32_t dataOffset = rdU32BE(p + body);
            // dataOffset comes from the file. Unchecked, ssnd walks off the end
            // of the buffer and ssndLen underflows to a huge size_t. The sample
            // bytes must sit inside this chunk, which line 56 already bounds by
            // n, so dataOffset must leave room for the 8-byte SSND header.
            if (dataOffset <= len - 8) {
                ssnd = p + body + 8 + dataOffset;
                ssndLen = len - 8 - dataOffset;
            }
        }
        off = body + len + (len & 1);          // chunks are word-aligned
    }
    if (!ssnd || channels < 1 || frames == 0) return false;
    if (sampleRate < 1000 || sampleRate > 192000) sampleRate = 44100;

    const int bytesPerSample = bits / 8;
    if (bytesPerSample < 1 || bytesPerSample > 3) return false;
    const std::size_t avail = ssndLen / std::size_t(bytesPerSample * channels);
    const std::size_t count = std::min<std::size_t>(frames, avail);

    out.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        // Mono-sum the frame's channels.
        double acc = 0.0;
        for (int c = 0; c < channels; ++c) {
            const unsigned char* s = ssnd + (i * std::size_t(channels) + std::size_t(c))
                                            * std::size_t(bytesPerSample);
            int32_t v = 0;
            if (bytesPerSample == 1)      v = int8_t(s[0]) << 16;
            else if (bytesPerSample == 2) v = int16_t((s[0] << 8) | s[1]) << 8;
            else                          v = int32_t((int8_t(s[0]) << 16) | (s[1] << 8) | s[2]);
            acc += double(v) / 8388608.0;              // 24-bit full scale
        }
        out[i] = float(acc / double(channels));
    }
    return !out.empty();
}

// Mod file name -> our canonical voice name. The machine prefix disambiguates
// collisions: "HC" is the 808's HI CONGA but the 909's HAND CLAP.
std::string canonicalName(const std::string& fileName) {
    std::string s = fileName;
    const std::size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    if (s.rfind("tr808", 0) == 0) {
        const std::string v = s.substr(5);
        // BD SD1-5 N1-4 LT MT HT LC MC HC RS CP MA CB CY OH CH CL map straight
        // through; the 808's HC really is the hi conga.
        return "808_" + v;
    }
    if (s.rfind("tr909", 0) == 0) {
        std::string v = s.substr(5);
        if (v == "hc")   v = "cp";        // 909 "HC" = hand clap
        else if (v == "chh") v = "ch";
        else if (v == "ohh") v = "oh";
        else if (v == "ccy") v = "crash";
        else if (v == "rcy") v = "ride";
        return "909_" + v;
    }
    return {};                            // Startup.aif and friends: not a voice
}

} // namespace

namespace {

// Read the whole file; returns empty on failure.
std::vector<unsigned char> slurp(const std::string& path) {
    std::vector<unsigned char> buf;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return buf;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0) {
        buf.resize((std::size_t) size);
        if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
    }
    std::fclose(f);
    return buf;
}

// A mod's image as ReBirth reads it: the file with its embedded files unscrambled.
// ModPacker scrambles every EMBF payload past its first KB (docs/FORMAT_RBM.md). The
// keys are generated by its key function, exact for any mod it made from a folder with a
// ModName.txt, and the result is self-checked, a first read of 43 to 125 ms for a whole mod;
// for an archive the generated keys do not fit they are estimated instead, which takes seconds. So
// they are kept for the life of the process per distinct file content (a failure is
// kept too). Fails, with err set, when the file is unreadable, is not a
// mod, is too large to be one ModPacker made, or the unscrambled image fails
// checkUnscrambledRbm (which refuses keys wrong everywhere or in part); the caller
// then plays without the mod rather than noise.
uint64_t fnv1a64(const std::vector<unsigned char>& b) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : b) { h ^= c; h *= 1099511628211ull; }
    return h;
}

struct ModKeys { std::vector<uint8_t> even, odd; bool ok = false; std::string why; };

bool readModImage(const std::string& path, std::vector<unsigned char>& image, std::string* err) {
    image = slurp(path);
    if (image.size() <= 12) { if (err) *err = "cannot read " + path; return false; }
    if (std::memcmp(image.data(), "CAT ", 4) != 0 || std::memcmp(image.data() + 8, "PRBM", 4) != 0) {
        if (err) *err = "not an .rbm mod (expected CAT/PRBM)";
        return false;
    }
    if (rbmEntries(image).empty()) return true;            // nothing embedded, nothing to undo

    static std::mutex mutex;
    static std::map<std::pair<uint64_t, std::size_t>, ModKeys> cache;
    const auto id = std::make_pair(fnv1a64(image), image.size());
    ModKeys keys;
    bool known = false;
    {
        std::lock_guard<std::mutex> g(mutex);
        const auto it = cache.find(id);
        if (it != cache.end()) { keys = it->second; known = true; }
    }
    if (known) {
        if (keys.ok) applyRbmKeys(image, keys.even, keys.odd, -1);
    } else {
        RbmUnscrambleReport rep;
        keys.ok = unscrambleRbm(image, &rep, &keys.even, &keys.odd);
        if (rep.tooLarge)
            keys.why = "not a mod ModPacker made (over 32 MB, 1024 files or 8 M audio samples); refused unread";
        else if (!keys.ok && rep.generatedUnconfirmed && !rep.estimated)
            keys.why = "the mod's sounds give its self-check too little to confirm its keys ("
                     + std::to_string(rep.generatedCheck.voicesScrambled)
                     + " voices, too few, too noisy or too quiet); refused";
        else if (!keys.ok && (rep.nearMiss[0] || rep.nearMiss[1]) && !rep.estimated)
            keys.why = "the mod looks edited since it was packed (its keys are close to right but not "
                       "right, and it holds too few sounds to estimate them); refused";
        else if (!keys.ok && rep.check.tooFewVoices)
            keys.why = "the mod's keys had to be estimated, and it holds too few sounds to do that "
                       "with confidence ("
                     + std::to_string(rep.check.voicesScrambled) + " of the 12 needed); refused";
        else if (!keys.ok)
            keys.why = "the mod's embedded files could not be unscrambled (the result failed its self-check)";
        std::lock_guard<std::mutex> g(mutex);
        cache[id] = keys;
    }
    if (!keys.ok) {
        if (err) *err = keys.why;
        return false;
    }
    return true;
}

// Locate the first INFO chunk in an IFF tree.
bool findInfo(const unsigned char* p, std::size_t off, std::size_t end,
              std::size_t& infoOff, uint32_t& infoLen, int depth = 1) {
    while (off + 8 <= end) {
        const unsigned char* id = p + off;
        const uint32_t len = rdU32BE(p + off + 4);
        const std::size_t body = off + 8;
        if (body + len > end) return false;
        if (std::memcmp(id, "CAT ", 4) == 0 || std::memcmp(id, "FORM", 4) == 0
            || std::memcmp(id, "LIST", 4) == 0 || std::memcmp(id, "PROP", 4) == 0) {
            if (depth < kMaxIffDepth && findInfo(p, body + 4, body + len, infoOff, infoLen, depth + 1)) return true;
        } else if (std::memcmp(id, "INFO", 4) == 0) {
            infoOff = body; infoLen = len; return true;
        }
        off = body + len + (len & 1);
    }
    return false;
}

std::string cstrAt(const unsigned char* p, std::size_t avail, std::size_t max) {
    std::size_t n = 0;
    while (n < avail && n < max && p[n] != 0) ++n;
    return std::string((const char*) p, n);
}

std::string trimLower(std::string s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (unsigned char) s[a] <= ' ') ++a;
    while (b > a && (unsigned char) s[b - 1] <= ' ') --b;
    s = s.substr(a, b - a);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

// The canonical name songs reference sits at this offset inside INFO. Verified
// identical across all four retail mods.
constexpr std::size_t kModNameOffset = 1003;

// Files with the given extension directly inside `dir` (not recursive).
std::vector<std::string> listFiles(const std::string& dir, const std::string& ext) {
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    if (ec) return out;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string e = it->path().extension().string();
        std::transform(e.begin(), e.end(), e.begin(),
                       [](unsigned char c) { return (char) std::tolower(c); });
        if (e == ext) out.push_back(it->path().string());
    }
    return out;
}

} // namespace

ModInfo readModInfo(const std::string& rbmPath) {
    ModInfo info;
    std::vector<unsigned char> buf = slurp(rbmPath);
    if (buf.size() < 16) return info;
    if (std::memcmp(buf.data(), "CAT ", 4) != 0 || std::memcmp(buf.data() + 8, "PRBM", 4) != 0)
        return info;
    std::size_t off = 0; uint32_t len = 0;
    if (!findInfo(buf.data(), 12, buf.size(), off, len)) return info;
    const unsigned char* inf = buf.data() + off;
    info.description = cstrAt(inf, len, 512);
    if (len > kModNameOffset)
        info.name = cstrAt(inf + kModNameOffset, len - kModNameOffset, 80);
    info.valid = !info.name.empty();
    return info;
}

std::string findModByName(const std::vector<std::string>& searchDirs,
                          const std::string& skinName) {
    const std::string want = trimLower(skinName);
    if (want.empty() || want == "standard rebirth") return {};   // the stock look
    for (const std::string& dir : searchDirs) {
        if (dir.empty()) continue;
        for (const std::string& path : listFiles(dir, ".rbm")) {
            const ModInfo info = readModInfo(path);
            if (info.valid && trimLower(info.name) == want) return path;
        }
    }
    return {};
}

std::vector<ModSample> readModSamples(const std::string& rbmPath, std::string* err) {
    std::vector<ModSample> out;
    std::vector<unsigned char> buf;
    if (!readModImage(rbmPath, buf, err)) return out;

    // Walk the IFF tree, collecting EMBF payloads.
    struct Walker {
        const unsigned char* p;
        std::vector<ModSample>* out;
        void walk(std::size_t off, std::size_t end, int depth = 1) {
            while (off + 8 <= end) {
                const unsigned char* id = p + off;
                const uint32_t len = rdU32BE(p + off + 4);
                const std::size_t body = off + 8;
                if (body + len > end) return;
                if (std::memcmp(id, "CAT ", 4) == 0 || std::memcmp(id, "FORM", 4) == 0
                    || std::memcmp(id, "LIST", 4) == 0 || std::memcmp(id, "PROP", 4) == 0) {
                    if (depth < kMaxIffDepth) walk(body + 4, body + len, depth + 1);
                } else if (std::memcmp(id, "EMBF", 4) == 0) {
                    take(p + body, len);
                }
                off = body + len + (len & 1);
            }
        }
        void take(const unsigned char* body, uint32_t len) {
            // [NUL-terminated name][file bytes]
            uint32_t i = 0;
            while (i < len && body[i] != 0) ++i;
            if (i == 0 || i + 1 >= len) return;
            const std::string fileName((const char*) body, i);
            const std::string canon = canonicalName(fileName);
            if (canon.empty()) return;
            ModSample ms;
            ms.name = canon;
            if (decodeAiff(body + i + 1, len - i - 1, ms.data, ms.sampleRate)) {
                double mean = 0.0;                       // DC-block, as the WAV path does
                for (float v : ms.data) mean += v;
                mean /= (double) ms.data.size();
                for (float& v : ms.data) v = (float) (v - mean);
                out->push_back(std::move(ms));
            }
        }
    } w{ buf.data(), &out };
    w.walk(12, buf.size());

    if (out.empty() && err) *err = "no drum voices found in mod";
    return out;
}

std::vector<ModSprite> readModSprites(const std::string& rbmPath, std::string* err) {
    std::vector<ModSprite> out;
    std::vector<unsigned char> buf;
    if (!readModImage(rbmPath, buf, err)) return out;
    // Same walk as readModSamples, keeping the payloads that are NOT audio.
    // "Not audio" is decided by trying the AIFF header rather than by file
    // extension, because a mod is free to name its files anything.
    struct Walker {
        const unsigned char* p;
        std::vector<ModSprite>* out;
        void walk(std::size_t off, std::size_t end, int depth = 1) {
            while (off + 8 <= end) {
                const unsigned char* id = p + off;
                const uint32_t len = rdU32BE(p + off + 4);
                const std::size_t body = off + 8;
                if (body + len > end) return;
                if (std::memcmp(id, "CAT ", 4) == 0 || std::memcmp(id, "FORM", 4) == 0
                    || std::memcmp(id, "LIST", 4) == 0 || std::memcmp(id, "PROP", 4) == 0) {
                    if (depth < kMaxIffDepth) walk(body + 4, body + len, depth + 1);
                } else if (std::memcmp(id, "EMBF", 4) == 0) {
                    take(p + body, len);
                }
                off = body + len + (len & 1);
            }
        }
        void take(const unsigned char* body, uint32_t len) {
            uint32_t i = 0;
            while (i < len && body[i] != 0) ++i;
            if (i == 0 || i + 1 >= len) return;
            const unsigned char* data = body + i + 1;
            const uint32_t n = len - i - 1;
            // FORM/AIFF? then it is a voice, handled by readModSamples.
            if (n >= 12 && std::memcmp(data, "FORM", 4) == 0
                        && std::memcmp(data + 8, "AIF", 3) == 0) return;
            ModSprite sp;
            sp.fileName.assign((const char*) body, i);
            sp.bytes.assign(data, data + n);
            out->push_back(std::move(sp));
        }
    } w{ buf.data(), &out };
    w.walk(12, buf.size());

    if (out.empty() && err) *err = "no skin graphics found in mod";
    return out;
}

int loadModSampleSet(Engine& engine, const std::string& rbmPath, std::string* err) {
    return installModSamples(engine, readModSamples(rbmPath, err));
}

int installModSamples(Engine& engine, const std::vector<ModSample>& samples) {
    if (samples.empty()) return 0;

    std::map<std::string, const ModSample*> byName;
    for (const ModSample& s : samples) byName[s.name] = &s;

    return loadSampleSetFrom(engine, [&byName](const std::string& name, int* sr) {
        std::vector<float> d;
        auto it = byName.find(name);
        if (it != byName.end()) { d = it->second->data; if (sr) *sr = it->second->sampleRate; }
        return d;
    });
}

} // namespace reborn
