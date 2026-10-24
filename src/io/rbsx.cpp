// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// `.rbsx` session reader/writer; see include/reborn/rbsx.h.
//
//   CAT (RBSX)
//   ├─ RVER  u32 BE format version
//   ├─ RSNG  a complete .rbs image, byte-for-byte
//   ├─ RENV  session environment, UTF-8 "key=value" lines
//   └─ RINS  per-303-slot voice record (optional): engine id + hosted-plugin
//            identifier and opaque state blob
//
// RENV is deliberately TEXT: it is small, it is the part a human is most
// likely to want to read or repair, and a preservation project benefits from
// a format someone can inspect without our tooling. The heavy/binary parts
// (the song, and a hosted plugin's state) stay in their own chunks. RINS is
// OPTIONAL: a reader that predates it, or a session with nothing to say about
// its voices, simply omits it and the default engine applies.
//
#include "reborn/rbsx.h"
#include "reborn/rbs.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace reborn {
namespace {

uint32_t rdU32BE(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
         | (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
void putU32BE(std::vector<unsigned char>& b, uint32_t v) {
    b.push_back((unsigned char)(v >> 24)); b.push_back((unsigned char)(v >> 16));
    b.push_back((unsigned char)(v >> 8));  b.push_back((unsigned char)v);
}
void putChunk(std::vector<unsigned char>& out, const char id[4],
              const unsigned char* body, std::size_t n) {
    out.insert(out.end(), id, id + 4);
    putU32BE(out, (uint32_t) n);
    out.insert(out.end(), body, body + n);
    if (n & 1) out.push_back(0);
}

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

// Find a top-level chunk inside CAT(RBSX). Returns false if absent.
bool findChunk(const std::vector<unsigned char>& b, const char id[4],
               std::size_t& off, uint32_t& len) {
    if (b.size() < 12) return false;
    std::size_t o = 12;                       // past CAT + size + "RBSX"
    while (o + 8 <= b.size()) {
        const uint32_t n = rdU32BE(b.data() + o + 4);
        if (o + 8 + n > b.size()) return false;
        if (std::memcmp(b.data() + o, id, 4) == 0) { off = o + 8; len = n; return true; }
        o += 8 + n + (n & 1);
    }
    return false;
}

std::string envValue(const std::string& text, const std::string& key) {
    const std::string needle = key + "=";
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        if (text.compare(pos, needle.size(), needle) == 0)
            return text.substr(pos + needle.size(), eol - pos - needle.size());
        pos = eol + 1;
    }
    return {};
}

} // namespace

// Serialise a session to the CAT(RBSX) byte image. Shared by the file and
// memory-buffer entry points below.
static bool buildRbsx(const Session& s, std::vector<unsigned char>& out, std::string* err) {
    std::vector<unsigned char> rbs;
    if (!saveRbs(s.song, rbs, err)) return false;

    // RENV is line-based ("key=value\n"), so a value must not contain a newline:
    // one would truncate the value and let its tail be read as another key. Paths
    // cannot contain newlines on Windows (the reference platform); strip any that
    // appear rather than corrupt the record. Values without newlines are unchanged.
    auto oneLine = [](std::string v) {
        for (char& c : v) if (c == '\n' || c == '\r') c = ' ';
        return v;
    };
    std::string env;
    env += "sampleDir=" + oneLine(s.env.sampleDir) + "\n";
    env += "modName="   + oneLine(s.env.modName)   + "\n";
    env += "modPath="   + oneLine(s.env.modPath)   + "\n";

    std::vector<unsigned char> children;
    {
        std::vector<unsigned char> ver;
        putU32BE(ver, (uint32_t) kRbsxVersion);
        putChunk(children, "RVER", ver.data(), ver.size());
    }
    putChunk(children, "RSNG", rbs.data(), rbs.size());
    putChunk(children, "RENV", (const unsigned char*) env.data(), env.size());

    // RINS: per-303-slot voice record. Written only when a slot is recorded, so
    // a session with nothing to say about its voices stays byte-compatible with
    // readers that predate the chunk. Layout: u32 slotCount, then per slot
    // { u8 engine (0xFF = unrecorded), u32 idLen, id bytes, u32 stateLen, state }.
    if (s.bass[0].engine >= 0 || s.bass[1].engine >= 0) {
        std::vector<unsigned char> ins;
        putU32BE(ins, 2);
        for (int i = 0; i < 2; ++i) {
            const InstrumentSlot& sl = s.bass[i];
            ins.push_back((unsigned char) (sl.engine < 0 ? 0xFF : (sl.engine & 0xFF)));
            putU32BE(ins, (uint32_t) sl.pluginId.size());
            ins.insert(ins.end(), sl.pluginId.begin(), sl.pluginId.end());
            putU32BE(ins, (uint32_t) sl.pluginState.size());
            ins.insert(ins.end(), sl.pluginState.begin(), sl.pluginState.end());
        }
        putChunk(children, "RINS", ins.data(), ins.size());
    }

    std::vector<unsigned char> body(4);
    std::memcpy(body.data(), "RBSX", 4);
    body.insert(body.end(), children.begin(), children.end());

    out.clear();
    putChunk(out, "CAT ", body.data(), body.size());
    return true;
}

// Parse a CAT(RBSX) byte image. Shared by the file and memory-buffer readers.
static bool parseRbsx(const std::vector<unsigned char>& b, Session& out, std::string* err) {
    if (b.size() < 12) { if (err) *err = "session too small"; return false; }
    if (std::memcmp(b.data(), "CAT ", 4) != 0 || std::memcmp(b.data() + 8, "RBSX", 4) != 0) {
        if (err) *err = "not a .rbsx session (expected CAT/RBSX)";
        return false;
    }

    std::size_t off = 0; uint32_t len = 0;
    // Keep the version UNSIGNED for the guard: a raw u32 with the top bit set
    // (a corrupt or hostile RVER) would become a negative int and slip past the
    // `> kRbsxVersion` future-version check, then be mis-parsed as an old version.
    const uint32_t ver = (findChunk(b, "RVER", off, len) && len >= 4)
                       ? rdU32BE(b.data() + off) : 1u;
    // Refuse a file from a FUTURE version rather than silently mis-reading it.
    if (ver > (uint32_t) kRbsxVersion) {
        if (err) *err = "session was written by a newer version of Reborn";
        return false;
    }
    out.version = (int) ver;

    if (!findChunk(b, "RSNG", off, len)) {
        if (err) *err = "session contains no song (RSNG missing)";
        return false;
    }
    if (!loadRbs(b.data() + off, len, out.song, err)) return false;

    out.env = SessionEnv{};
    if (findChunk(b, "RENV", off, len)) {
        const std::string text((const char*) b.data() + off, len);
        out.env.sampleDir = envValue(text, "sampleDir");
        out.env.modName   = envValue(text, "modName");
        out.env.modPath   = envValue(text, "modPath");
    }

    out.bass[0] = InstrumentSlot{};
    out.bass[1] = InstrumentSlot{};
    if (findChunk(b, "RINS", off, len) && len >= 4) {
        const unsigned char* p   = b.data() + off;
        const unsigned char* end = p + len;
        const uint32_t slots = rdU32BE(p); p += 4;
        for (uint32_t i = 0; i < slots && i < 2; ++i) {
            // Compare end - p, never form p + n: with fewer than n bytes left,
            // p + n points past one-past-the-end, which is UB even unread. p is
            // only ever advanced after a passing check, so p <= end always holds.
            if (end - p < 1) break;
            InstrumentSlot sl;
            const int eng = *p++;
            sl.engine = (eng == 0xFF) ? -1 : eng;
            if (end - p < 4) break;
            const uint32_t idLen = rdU32BE(p); p += 4;
            if ((std::size_t) (end - p) < idLen) break;
            sl.pluginId.assign((const char*) p, idLen); p += idLen;
            if (end - p < 4) break;
            const uint32_t stLen = rdU32BE(p); p += 4;
            if ((std::size_t) (end - p) < stLen) break;
            sl.pluginState.assign(p, p + stLen); p += stLen;
            out.bass[i] = sl;
        }
    }
    return true;
}

bool saveRbsx(const std::string& path, const Session& s, std::string* err) {
    std::vector<unsigned char> out;
    if (!buildRbsx(s, out, err)) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { if (err) *err = "cannot open " + path + " for writing"; return false; }
    const std::size_t n = std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (n != out.size()) { if (err) *err = "short write"; return false; }
    return true;
}

bool saveRbsxToMemory(std::vector<unsigned char>& out, const Session& s, std::string* err) {
    return buildRbsx(s, out, err);
}

bool loadRbsx(const std::string& path, Session& out, std::string* err) {
    const std::vector<unsigned char> b = slurp(path);
    if (b.empty()) { if (err) *err = "cannot read " + path; return false; }
    return parseRbsx(b, out, err);
}

bool loadRbsxFromMemory(const unsigned char* data, std::size_t n, Session& out, std::string* err) {
    if (!data || n == 0) { if (err) *err = "empty session buffer"; return false; }
    const std::vector<unsigned char> b(data, data + n);
    return parseRbsx(b, out, err);
}

bool extractRbsImage(const std::string& rbsxPath, std::vector<unsigned char>& out,
                     std::string* err) {
    const std::vector<unsigned char> b = slurp(rbsxPath);
    std::size_t off = 0; uint32_t len = 0;
    if (b.size() < 12 || std::memcmp(b.data(), "CAT ", 4) != 0
        || std::memcmp(b.data() + 8, "RBSX", 4) != 0
        || !findChunk(b, "RSNG", off, len)) {
        if (err) *err = "no embedded song image in " + rbsxPath;
        return false;
    }
    out.assign(b.begin() + (long) off, b.begin() + (long) (off + len));
    return true;
}

} // namespace reborn
