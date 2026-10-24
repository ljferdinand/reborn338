// SPDX-License-Identifier: AGPL-3.0-only AND MIT
// Copyright (C) 2026 Luke Ferdinand
// Parts adapted from Open303, Copyright (c) 2009 Robin Schmidt, under the MIT
// licence in third_party/open303/LICENSE.txt (see the attribution below).
//
// Open303-derived 303 voice: a reborn::Instrument wrapper around rosic::Open303
// (MIT, Robin Schmidt; vendored in third_party/open303, see docs/PROVENANCE.md).
//
// This is the selectable "best of breed" 303 (the default voice is the native
// TPT voice, include/reborn/factories.h). It emulates the Roland hardware 303,
// NOT ReBirth, so it is never a ReBirth-parity engine; the native TPT voice
// (bass_synth.cpp) keeps that job. Reborn drives this voice from its own step
// sequencer through the Instrument / BassEvent boundary (step_events.h), so we
// bypass Open303's built-in AcidSequencer entirely.
//
// Control mapping and note model are taken from Open303's own VST parameter
// handling and noteOn logic:
//   accent  = velocity >= 100
//   slide   = legato: a note-on while a note is still held glides instead of
//             retriggering.
#include "reborn/processor.h"
#include "reborn/factories.h"

// The vendored 2009 headers are noisy under our warning level; silence them here
// (gcc's strict-aliasing note on their type punning included).
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wstrict-aliasing"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "rosic_Open303.h"   // pulls the rosic chain + the global lin/exp helpers
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reborn {

namespace {
// Reborn's frozen 9-param 303 contract. Same names, order, RANGES and defaults
// as the native BassSynth303 (bass_synth.cpp kParams), so the song loader, the
// generic knob panel and sessions treat the two voices identically and switching
// engines never rescales a stored value. Only the DSP mapping in apply() differs.
struct POpen { const char* name; const char* label; float mn, mx, def; const char* unit; };
const POpen kOpenParams[9] = {
    { "tuning",    "Tuning",    -12.f, 12.f, 0.00f, "st" },
    { "waveform",  "Waveform",    0.f,  1.f, 0.00f, ""   },  // 0 = saw, 1 = square
    { "cutoff",    "Cutoff",      0.f,  1.f, 0.35f, ""   },
    { "resonance", "Resonance",   0.f,  1.f, 0.55f, ""   },
    { "envmod",    "Env Mod",     0.f,  1.f, 0.55f, ""   },
    { "decay",     "Decay",       0.f,  1.f, 0.55f, ""   },
    { "accent",    "Accent",      0.f,  1.f, 0.50f, ""   },
    { "volume",    "Volume",      0.f,  1.f, 0.85f, ""   },
    { "drive",     "Drive",       0.f,  1.f, 0.20f, ""   },
};
enum { P_TUNING, P_WAVE, P_CUTOFF, P_RESO, P_ENVMOD, P_DECAY, P_ACCENT, P_VOLUME, P_DRIVE };
} // namespace

class BassOpen303 : public Instrument {
public:
    BassOpen303() {
        for (int i = 0; i < 9; ++i) p_[i] = kOpenParams[i].def;
        core_.setSampleRate(sr_);
        applyAll();
    }

    // --- Processor ---------------------------------------------------------
    void prepare(double sampleRate) override {
        sr_ = sampleRate;
        core_.setSampleRate(sampleRate);
        applyAll();
    }
    void reset() override {
        core_.allNotesOff();
        heldNote_ = -1;
        gateLeft_ = -1;
    }
    int numParams() const override { return 9; }
    ParamInfo paramInfo(int i) const override {
        if (i < 0 || i >= 9) return ParamInfo{ "unused", "Unused", 0.0f, 1.0f, 0.0f, "" };
        const POpen& q = kOpenParams[i];
        return ParamInfo{ q.name, q.label, q.mn, q.mx, q.def, q.unit };
    }
    float getParam(int i) const override { return (i >= 0 && i < 9) ? p_[i] : 0.0f; }
    void  setParam(int i, float v) override {
        if (i < 0 || i >= 9) return;
        p_[i] = std::min(kOpenParams[i].mx, std::max(kOpenParams[i].mn, v));
        apply(i);
    }

    // --- Instrument: step events (see step_events.h applyBassEvent) --------
    void setStepNote(int note) override {
        // Open303 has no "retune without gating"; that is a ReBirth quirk and not
        // this voice's job. Remember the note in case the step ends up gating.
        pendingNote_ = note;
    }
    void setStepTiming(double stepSeconds, bool tie) override {
        stepSec_ = stepSeconds;
        tie_     = tie;
    }
    void trigger(int note, float /*velocity*/, bool accent, bool slide) override {
        const int vel = accent ? 127 : 64;   // Open303: accent == velocity >= 100
        if (slide && heldNote_ >= 0) {
            // Legato: glide to `note`, then release the old note so only the new
            // one is held. If the slide is INTO the SAME pitch, do nothing: it is
            // already sounding, and Open303's note-off matches by key ONLY, so
            // re-adding then removing key X would wipe BOTH list entries and
            // silence the still-ringing note.
            if (note != heldNote_) {
                core_.noteOn(note, vel, 0.0);
                core_.noteOn(heldNote_, 0, 0.0);
            }
        } else {
            if (heldNote_ >= 0) core_.noteOn(heldNote_, 0, 0.0);  // clear -> retrigger
            core_.noteOn(note, vel, 0.0);
        }
        heldNote_ = note;
        armGate();
    }
    void holdStep(double stepSeconds, bool tie, bool /*accent*/) override {
        // A tied-into rest carries the sounding note; just re-arm the gate window.
        stepSec_ = stepSeconds;
        tie_     = tie;
        if (heldNote_ >= 0) armGate();
    }
    void release() override {
        if (heldNote_ >= 0) core_.noteOn(heldNote_, 0, 0.0);
        heldNote_ = -1;
        gateLeft_ = -1;
    }
    void accentOnly() override {
        // ReBirth fires an accent envelope on a plain rest over a ringing note;
        // Open303 has no accent-without-gate, so this is a no-op here.
    }

    // --- Audio -------------------------------------------------------------
    void process(StereoBlock& out, int numFrames) override {
        for (int i = 0; i < numFrames; ++i) {
            if (gateLeft_ > 0) {
                if (--gateLeft_ == 0 && heldNote_ >= 0)
                    core_.noteOn(heldNote_, 0, 0.0);   // gate off -> articulate the step
            }
            const float y = (float) core_.getSample();
            out.left[(std::size_t) i]  = y;
            out.right[(std::size_t) i] = y;
        }
    }

private:
    // Non-tied notes gate for a fraction of the step then release, which is what
    // makes a run of sixteenths articulate; a tie sustains (gateLeft_ = -1).
    // Match the native voice's measured half-step gate (bass_synth.cpp
    // setStepTiming: flat to ~62 ms of a 125 ms step, so 0.500), then Open303's
    // own amp release shapes the tail. The same REBORN_GATE_FRAC override drives
    // both engines, so a gate-feel experiment stays aligned across the two.
    static double gateFraction() {
        const char* e = std::getenv("REBORN_GATE_FRAC");
        return e ? std::atof(e) : 0.500;
    }
    void armGate() {
        gateLeft_ = tie_ ? -1 : std::max(1L, (long)(stepSec_ * gateFraction() * sr_ + 0.5));
    }

    void apply(int i) {
        const double v = (double) p_[i];
        switch (i) {
            case P_WAVE:   core_.setWaveform(v);                             break;
            case P_CUTOFF: core_.setCutoff(linToExp(v, 0.0, 1.0, 314.0, 2394.0)); break;
            case P_RESO:   core_.setResonance(v * 100.0);                    break;
            case P_ENVMOD: core_.setEnvMod(v * 100.0);                       break;
            case P_DECAY:  core_.setDecay(linToExp(v, 0.0, 1.0, 200.0, 2000.0)); break;
            case P_ACCENT: core_.setAccent(v * 100.0);                       break;
            case P_VOLUME: core_.setVolume(linToLin(v, 0.0, 1.0, -60.0, 0.0)); break;
            // Reborn's coarse +/- semitone transpose. Open303 has no per-note
            // transpose, but pitchToFreq(note, tuning) scales every note by the
            // master A4 reference, so a reference of 440 * 2^(semi/12) shifts
            // every note by `semi` semitones, identical to the native voice's
            // noteToHz(note + tuning), continuous and fractional, picked up on
            // the next triggerNote / slideToNote.
            case P_TUNING: core_.setTuning(440.0 * std::pow(2.0, v / 12.0)); break;
            // Open303 has no general drive control (best-of-breed addition later).
            case P_DRIVE:  break;
        }
    }
    void applyAll() { for (int i = 0; i < 9; ++i) apply(i); }

    rosic::Open303 core_;
    double sr_ = kDefaultSampleRate;
    float  p_[9] = {};
    int    heldNote_ = -1, pendingNote_ = -1;
    double stepSec_ = 0.125;
    bool   tie_ = false;
    long   gateLeft_ = -1;         // samples until gate-off; -1 = sustaining
};

std::unique_ptr<Instrument> makeOpen303() { return std::make_unique<BassOpen303>(); }

} // namespace reborn
