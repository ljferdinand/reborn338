// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Shared drum sample-set loading: reads a folder of canonical-named WAVs
// that the user supplies (docs/SAMPLES.md lists the names; none ship) and
// installs them into an Engine's drum machines,
// picking multisample variants from the song's decoded knob values.
//
// Used by both the headless renderer and the GUI app so behaviour cannot
// drift between the two.
//
#include <string>
#include <vector>
#include <functional>
#include "reborn/engine.h"

namespace reborn {

// Supplies one voice recording by CANONICAL name ("808_bd", "909_sd3", ...).
// Returns an empty vector if that name is not available. `sampleRate` receives
// the recording's rate. This indirection lets the same variant-selection and
// snare-construction logic serve both a directory of WAVs and the AIFFs
// embedded in a user's .rbm mod (see rbm.h).
using SampleSource = std::function<std::vector<float>(const std::string& canonicalName,
                                                      int* sampleRate)>;

// Install samples from `source` into `engine`'s drum machines for the currently
// loaded song (variant selection uses engine.song()'s decoded knobs).
// Returns the number of voices that received samples. Call AFTER loadSong().
int loadSampleSetFrom(Engine& engine, const SampleSource& source);

// Convenience: a source reading canonical-named WAVs from `dir`.
int loadSampleSet(Engine& engine, const std::string& dir);

} // namespace reborn
