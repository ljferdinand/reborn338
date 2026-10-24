// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
#include <string>
#include <vector>
#include "reborn/audio.h"

namespace reborn {

// Write a stereo buffer to a 16-bit PCM WAV file. Samples outside [-1,1] are
// hard-clamped. Returns false on any file error.
bool writeWav16(const std::string& path, const StereoBlock& audio, int sampleRate);

// Convenience: write interleaved stereo float samples.
bool writeWav16Interleaved(const std::string& path,
                           const std::vector<float>& interleaved,
                           int sampleRate);

// Read a 16-bit PCM WAV file (mono or stereo; stereo is downmixed to mono).
// Returns normalized float samples and sets *sampleRate. Empty on failure.
std::vector<float> readWav16Mono(const std::string& path, int* sampleRate);

} // namespace reborn
