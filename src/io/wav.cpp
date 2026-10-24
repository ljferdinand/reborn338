// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "reborn/wav.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace reborn {

namespace {
void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
    b.push_back(uint8_t((v >> 16) & 0xFF));
    b.push_back(uint8_t((v >> 24) & 0xFF));
}
void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v & 0xFF));
    b.push_back(uint8_t((v >> 8) & 0xFF));
}
void putTag(std::vector<uint8_t>& b, const char* t) {
    b.push_back(uint8_t(t[0])); b.push_back(uint8_t(t[1]));
    b.push_back(uint8_t(t[2])); b.push_back(uint8_t(t[3]));
}
int16_t toPcm16(float x) {
    if (x >  1.0f) x =  1.0f;
    if (x < -1.0f) x = -1.0f;
    // Symmetric-ish scaling; 32767 avoids overflow at +1.0.
    return static_cast<int16_t>(x * 32767.0f);
}
} // namespace

bool writeWav16Interleaved(const std::string& path,
                           const std::vector<float>& interleaved,
                           int sampleRate) {
    const uint16_t channels = 2;
    const uint16_t bits = 16;
    const uint32_t frameCount = static_cast<uint32_t>(interleaved.size() / channels);
    const uint32_t byteRate = uint32_t(sampleRate) * channels * (bits / 8);
    const uint16_t blockAlign = channels * (bits / 8);
    const uint32_t dataBytes = frameCount * blockAlign;

    std::vector<uint8_t> hdr;
    hdr.reserve(44);
    putTag(hdr, "RIFF");
    putU32(hdr, 36 + dataBytes);
    putTag(hdr, "WAVE");
    putTag(hdr, "fmt ");
    putU32(hdr, 16);            // PCM fmt chunk size
    putU16(hdr, 1);             // PCM
    putU16(hdr, channels);
    putU32(hdr, uint32_t(sampleRate));
    putU32(hdr, byteRate);
    putU16(hdr, blockAlign);
    putU16(hdr, bits);
    putTag(hdr, "data");
    putU32(hdr, dataBytes);

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(hdr.data(), 1, hdr.size(), f);

    // Stream PCM in chunks to avoid a large temp allocation.
    std::vector<uint8_t> buf;
    buf.reserve(4096);
    for (float s : interleaved) {
        int16_t v = toPcm16(s);
        buf.push_back(uint8_t(uint16_t(v) & 0xFF));
        buf.push_back(uint8_t((uint16_t(v) >> 8) & 0xFF));
        if (buf.size() >= 4096) { std::fwrite(buf.data(), 1, buf.size(), f); buf.clear(); }
    }
    if (!buf.empty()) std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    return true;
}

std::vector<float> readWav16Mono(const std::string& path, int* sampleRate) {
    std::vector<float> out;
    if (sampleRate) *sampleRate = 0;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (sz < 44) { std::fclose(f); return out; }
    std::vector<uint8_t> b((size_t) sz);
    if (std::fread(b.data(), 1, (size_t) sz, f) != (size_t) sz) { std::fclose(f); return out; }
    std::fclose(f);
    if (std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return out;

    auto rdU32 = [&](size_t o) { return uint32_t(b[o]) | uint32_t(b[o+1]) << 8
                                      | uint32_t(b[o+2]) << 16 | uint32_t(b[o+3]) << 24; };
    auto rdU16 = [&](size_t o) { return uint16_t(b[o]) | uint16_t(b[o+1]) << 8; };

    // Walk RIFF chunks for fmt + data (handles extra chunks like LIST/fact).
    size_t pos = 12;
    int channels = 0, bits = 0, rate = 0;
    size_t dataOff = 0, dataLen = 0;
    while (pos + 8 <= b.size()) {
        uint32_t csz = rdU32(pos + 4);
        // The loop guards only the 8-byte header. A truncated chunk whose
        // declared size runs past the buffer would let the fmt branch below read
        // out to pos+23. rbm.cpp and rbsx.cpp already bound the body this way.
        if (pos + 8 + (size_t) csz > b.size()) break;
        if (!std::memcmp(&b[pos], "fmt ", 4) && csz >= 16) {
            channels = rdU16(pos + 10);
            rate     = (int) rdU32(pos + 12);
            bits     = rdU16(pos + 22);
        } else if (!std::memcmp(&b[pos], "data", 4)) {
            dataOff = pos + 8; dataLen = csz;
        }
        pos += 8 + csz + (csz & 1);
    }
    if (!dataOff || bits != 16 || channels < 1 || channels > 2 || rate <= 0) return out;
    if (dataOff + dataLen > b.size()) dataLen = b.size() - dataOff;

    size_t frames = dataLen / (2u * (unsigned) channels);
    out.reserve(frames);
    for (size_t i = 0; i < frames; ++i) {
        size_t o = dataOff + i * 2 * channels;
        int16_t l = int16_t(uint16_t(b[o]) | uint16_t(b[o+1]) << 8);
        float v = float(l) / 32768.0f;
        if (channels == 2) {
            int16_t r = int16_t(uint16_t(b[o+2]) | uint16_t(b[o+3]) << 8);
            v = 0.5f * (v + float(r) / 32768.0f);
        }
        out.push_back(v);
    }
    if (sampleRate) *sampleRate = rate;
    return out;
}

bool writeWav16(const std::string& path, const StereoBlock& audio, int sampleRate) {
    std::vector<float> inter;
    inter.resize(audio.size() * 2);
    for (std::size_t i = 0; i < audio.size(); ++i) {
        inter[i * 2 + 0] = audio.left[i];
        inter[i * 2 + 1] = audio.right[i];
    }
    return writeWav16Interleaved(path, inter, sampleRate);
}

} // namespace reborn
