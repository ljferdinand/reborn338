// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Core audio primitives for the Reborn 338 engine.
// Everything here is plain C++17 with no external dependencies so the engine
// builds identically on Windows (MSVC) and macOS (clang).
//
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cmath>

namespace reborn {

constexpr double kDefaultSampleRate = 44100.0;
constexpr double kPi   = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// A block of non-interleaved stereo audio. The engine renders into these.
struct StereoBlock {
    std::vector<float> left;
    std::vector<float> right;

    explicit StereoBlock(std::size_t n = 0) : left(n, 0.0f), right(n, 0.0f) {}

    std::size_t size() const { return left.size(); }

    void resize(std::size_t n) { left.assign(n, 0.0f); right.assign(n, 0.0f); }

    void clear() {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
    }

    // Mix another block into this one.
    void add(const StereoBlock& o) {
        const std::size_t n = size() < o.size() ? size() : o.size();
        for (std::size_t i = 0; i < n; ++i) { left[i] += o.left[i]; right[i] += o.right[i]; }
    }
};

// Prevent denormal slowdowns without needing SSE intrinsics on every platform.
inline float flushDenormal(float x) {
    return (std::fabs(x) < 1.0e-30f) ? 0.0f : x;
}

// Double overload. The float version silently ROUNDS a double argument to float
// precision, which matters inside a recursive filter: the 303's ladder states are
// declared double but were being quantised to 24-bit mantissa every sample by
// flushDenormal((float)x). Denormal flushing does not require narrowing.
inline double flushDenormal(double x) {
    return (std::fabs(x) < 1.0e-300) ? 0.0 : x;
}

inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

// Convert a MIDI-ish note number to frequency (A4 = note 69 = 440 Hz).
inline double noteToHz(double note) {
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// Linear interpolation.
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// Cubic soft-clip: near-linear for |x|<~0.6, smooth saturation approaching the
// rails, hard-limited beyond. Prevents harsh digital clipping on the master.
inline float softClip(float x) {
    if (x <= -1.0f) return -2.0f / 3.0f;
    if (x >=  1.0f) return  2.0f / 3.0f;
    return x - (x * x * x) / 3.0f;
}
// softClip maps into [-2/3, 2/3]; scale by 1.5 to restore unity for small signals.
inline float softClipUnity(float x) { return 1.5f * softClip(x * (2.0f / 3.0f)); }

// The MASTER output stage. ReBirth is linear right up to the converter and then
// clips hard: measured 2026-08-26 on an overdriven 909 export whose samples run
// to exactly -32768 with nothing rounded off approaching it. softClipUnity, by
// contrast, is already 1.07 dB down at -1 dBFS, so using it on the master cost
// real level on anything loud while ReBirth stayed straight. Voices, the delay
// feedback path and the PCF keep the soft version, where the bound is character
// or a safety rail rather than a parity surface.
inline float hardClipUnity(float x) {
    return x <= -1.0f ? -1.0f : (x >= 1.0f ? 1.0f : x);
}

} // namespace reborn
