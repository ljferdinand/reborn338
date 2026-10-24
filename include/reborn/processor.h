// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Frozen processing interfaces for the Reborn 338 engine.
//
// Every instrument (bass synth, drum machines) and effect implements one of
// these interfaces and is created through a factory function (see factories.h).
// Concrete DSP lives entirely inside a single translation unit, so modules are
// independent: adding or changing one cannot break another's compilation.
//
// Parameters are addressed BY NAME (ParamInfo::name) so the song loader and UI
// never depend on a module's internal parameter ordering.
//
#include <string>
#include <memory>
#include <vector>
#include "reborn/audio.h"

namespace reborn {

// Description of one automatable parameter.
struct ParamInfo {
    std::string name;      // stable identifier, e.g. "cutoff"
    std::string label;     // human-facing, e.g. "Cutoff"
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    std::string unit;      // e.g. "Hz", "dB", "%"
};

// Base for anything with parameters and a sample-rate lifecycle.
class Processor {
public:
    virtual ~Processor() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset() = 0;

    virtual int numParams() const = 0;
    virtual ParamInfo paramInfo(int index) const = 0;
    virtual float getParam(int index) const = 0;
    virtual void setParam(int index, float value) = 0;

    // Convenience helpers that address parameters by name. Return -1 if absent.
    int paramIndex(const std::string& name) const {
        for (int i = 0; i < numParams(); ++i)
            if (paramInfo(i).name == name) return i;
        return -1;
    }
    bool setParamByName(const std::string& name, float value) {
        int i = paramIndex(name);
        if (i < 0) return false;
        setParam(i, value);
        return true;
    }
    float getParamByName(const std::string& name, float fallback = 0.0f) const {
        int i = paramIndex(name);
        return i < 0 ? fallback : getParam(i);
    }
};

// A pitched or drum instrument driven step-by-step by the sequencer.
//
// The engine renders in slices bounded by step boundaries, so a trigger always
// lands at the start of the next process() call, so individual DSP modules never
// need to handle intra-block timing offsets.
class Instrument : public Processor {
public:
    // Fire a note/hit.
    //   noteOrVoice : for the bass synth, an absolute semitone (A4=69);
    //                 for drum machines, the voice index (0..numVoices-1).
    //   velocity    : 0..1.
    //   accent      : classic accent (louder + more filter env / harder hit).
    //   slide       : bass-synth slide/tie into this note (glide, no re-trigger).
    virtual void trigger(int noteOrVoice, float velocity, bool accent, bool slide) = 0;

    // Gate off (bass synth). Drum one-shots may ignore this.
    virtual void release() {}

    // Tell the voice how long the step it is about to be triggered on lasts,
    // and whether that step ties into the next one. Call it BEFORE trigger().
    //
    // The 303 needs this because its VCA does not stay open for the whole step:
    // ReBirth gates for half of one and then releases, which is what makes a run
    // of sixteenths articulate note by note instead of running together. A voice
    // whose envelope does not depend on the step ignores this entirely, which is
    // every drum voice.
    virtual void setStepTiming(double /*stepSeconds*/, bool /*tie*/) {}

    // Set the pitch this STEP names, without gating anything.
    //
    // ReBirth's oscillator pitch follows every step's note byte, whether or not
    // that step gates. The rest flag controls the GATE and nothing else.
    // Measured 2026-08-01 on fixtures-2026-08-01/restpitch: a C that slides
    // into a REST whose note is G comes out of ReBirth at 98.2 Hz, which is the
    // G, while we held the C at 65.3. Clodhopper's step 3 is exactly that shape
    // and we were playing the wrong note on a shipped song.
    //
    // Called for EVERY step before the gate decision, so the glide target is
    // right whether the step strikes, ties, or rests. A voice with no pitch
    // ignores it, which is every drum voice.
    virtual void setStepNote(int /*noteOrVoice*/) {}

    // Carry a sounding note through a step that has no note of its own, because
    // the previous step tied into it. Re-arms the gate for this step without
    // retriggering pitch or envelopes. A voice with no gate ignores it, and a
    // voice that is not currently sounding must treat it as a no-op.
    // `accent` is read even though the step has no note: ReBirth fires the
    // accent on a rest, and it lands on whatever is sounding through it.
    virtual void holdStep(double /*stepSeconds*/, bool /*tie*/, bool /*accent*/) {}

    // Fire the accent envelope without gating, retriggering or retuning
    // anything. For an accent that lands on a step which does not sound a note
    // of its own but has one still ringing through it.
    //
    // Measured 2026-08-02 on fixtures-2026-08-02/accentrate: an accent on a
    // PLAIN rest over a ringing note lifts ReBirth's brightness by 10.61 dB,
    // where our release path drops the flag and lifts 0.00. See
    // BassRules::accentOnPlainRest, which gates this and is default off.
    virtual void accentOnly() {}

    // Render exactly numFrames of audio, OVERWRITING out[0..numFrames).
    // Instruments render mono content into both channels equally; the mixer
    // applies pan. (Stereo instruments may write true stereo.)
    virtual void process(StereoBlock& out, int numFrames) = 0;

    // For drum machines: how many selectable voices this instrument exposes.
    virtual int numVoices() const { return 1; }
    virtual std::string voiceName(int) const { return ""; }

    // Optional: give a voice a PCM sample to play INSTEAD of its synthesized
    // model (mono, normalized floats at rootRate Hz). The instrument still
    // applies its level/decay/accent handling. Pass an empty vector to revert
    // the voice to synthesis. Default: ignored (synthesis-only instruments).
    // NOT real-time-safe; call from the loader/UI thread before playback.
    virtual void setSample(int /*voice*/, std::vector<float> /*data*/,
                           double /*rootRate*/) {}
};

// A stereo insert/send effect.
class Effect : public Processor {
public:
    // Process a stereo block in place.
    virtual void process(StereoBlock& io, int numFrames) = 0;

    // Optional tempo-sync hooks (used by the delay and the pattern-controlled
    // filter). Default no-ops keep simple effects trivial.
    virtual void setTempo(double /*bpm*/) {}
    // Called once per 16th-note step. `stepIndex` is FREE-RUNNING from the start
    // of the song, not the bar-local 0..15: the PCF's preset patterns are up to
    // 64 thirty-seconds long and must carry across bar lines.
    virtual void onStep(int /*stepIndex*/) {}
};

} // namespace reborn
