// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Factory functions: the ONLY coupling point between the engine and the
// concrete DSP modules. Each factory is defined in its own translation unit
// (src/devices/*.cpp, src/effects/*.cpp). The engine never sees a concrete
// class, so modules can be developed and reviewed fully independently.
//
#include <memory>
#include "reborn/processor.h"

namespace reborn {

// --- Instruments ---
// TB-303-style monophonic acid bass synth (the native ReBirth-PARITY engine).
std::unique_ptr<Instrument> makeBassSynth();

// --- 303 voice-engine selection (the pluggable-voice registry) ---
// Which engine fills a 303 slot. The native engines are built by makeBass303
// below; HostedPlugin is fulfilled by the app layer, which installs a
// JUCE-plugin-backed Instrument via Engine::setInstrument (the core cannot build
// one; reborn_core is JUCE-free).
enum class Bass303Engine { Native303Tpt = 0, Open303 = 1, HostedPlugin = 2 };

// The default voice a fresh engine puts in each 303 slot: the native TPT PARITY
// engine (Luke's call 2026-09-18: ours is the shipped default now that the
// whole-mix regression validates it; a ReBirth-parity product should default to
// the voice tuned to ReBirth, not to a best-of-breed abstract 303). The
// Open303-derived voice (fork P3) stays one pick away in the registry, selectable
// via the app's picker or reborn_render's REBORN_BASS_ENGINE=open303. The
// byte-neutral regression guard still forces REBORN_BASS_ENGINE=tpt explicitly, so
// it is unaffected by this default.
constexpr Bass303Engine kDefaultBass303Engine = Bass303Engine::Native303Tpt;

// Build a native 303 voice by id. HostedPlugin returns nullptr (the app installs
// it).
std::unique_ptr<Instrument> makeBass303(Bass303Engine engine);

// The MIT Open303-derived best-of-breed 303 voice (third_party/open303 wrapped as
// a reborn::Instrument). Emulates the hardware 303, not ReBirth; a synthesis
// input, never a parity input (native TPT stays the parity path).
std::unique_ptr<Instrument> makeOpen303();
// TR-808-style analog-modeled drum machine (clean-room synthesis, no samples).
std::unique_ptr<Instrument> makeDrum808();
// TR-909-style hybrid drum machine (clean-room synthesis, no samples).
std::unique_ptr<Instrument> makeDrum909();

// --- Effects ---
std::unique_ptr<Effect> makeDistortion();   // waveshaping "PDF"-style distortion
std::unique_ptr<Effect> makeDelay();        // tempo-syncable stereo delay
std::unique_ptr<Effect> makeCompressor();   // master-bus compressor
std::unique_ptr<Effect> makePCF();          // pattern-controlled filter

} // namespace reborn
