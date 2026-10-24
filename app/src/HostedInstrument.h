// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// HostedInstrument: a reborn::Instrument backed by a hosted third-party plugin
// (a juce::AudioPluginInstance). This is the fork's host-harness voice: it lets a
// 303 slot hold jc303 or any other synth, sequenced by Reborn's step engine.
//
// It is the DOCUMENTED-LOSSY path. Reborn's BassEvent boundary (step_events.h)
// carries acid semantics that MIDI cannot express (pitch-on-rest, the 18 ms
// slide time, accent-on-a-tied-rest), so a hosted synth sequenced this way will
// NOT reproduce 303 slide behaviour. That is a creative feature, never a parity
// bug. Reborn's own two engines (Open303, TPT) stay linked in C++ and keep the
// semantics losslessly; only this third-party path goes through MIDI.
//
// Lives in app/ because it needs JUCE; the engine only ever sees the JUCE-free
// reborn::Instrument interface, installed via Engine::setInstrument.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "reborn/processor.h"

class HostedInstrument : public reborn::Instrument {
public:
    explicit HostedInstrument(std::unique_ptr<juce::AudioPluginInstance> plugin,
                              juce::String displayName);
    ~HostedInstrument() override;

    const juce::String& pluginName() const { return name_; }
    juce::AudioPluginInstance* plugin() const { return plugin_.get(); }

    // --- Processor ---------------------------------------------------------
    void prepare(double sampleRate) override;
    void reset() override;
    int numParams() const override;
    reborn::ParamInfo paramInfo(int index) const override;
    float getParam(int index) const override;
    void  setParam(int index, float value) override;

    // --- Instrument: the BassEvent boundary, translated to MIDI ------------
    void trigger(int note, float velocity, bool accent, bool slide) override;
    void release() override;
    void setStepTiming(double stepSeconds, bool tie) override;
    void setStepNote(int note) override;
    void holdStep(double stepSeconds, bool tie, bool accent) override;
    void accentOnly() override;
    void process(reborn::StereoBlock& out, int numFrames) override;

private:
    void queueNoteOn(int note, int vel);
    void queueNoteOff(int note);
    // Snapshot the plugin's parameter names/labels ONCE (message thread). The
    // engine's song-mode knob automation calls setParamByName on the AUDIO thread,
    // and the base helper scans paramInfo(i) for every param; building those from
    // juce::String (and calling the plugin's getName) per tick would allocate and
    // round-trip into the plugin on the audio thread. Cached, paramInfo is a plain
    // struct read.
    void buildParamCache();

    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    juce::String name_;

    juce::AudioBuffer<float> buf_;   // work buffer, sized to the plugin's channels
    juce::MidiBuffer midi_;          // events queued for the next process()
    double sr_ = 44100.0;
    int  heldNote_ = -1;             // mono: the MIDI note currently sounding
    int  bufChannels_ = 2;           // buf_ channel count (>= max(plugin in, out))
    int  outChannels_ = 2;           // the plugin's real output channels (mono check)
    std::vector<reborn::ParamInfo> paramCache_;   // names/labels, built once

    static constexpr int kMaxBlock  = 8192;   // plugin prepare/process ceiling
    static constexpr int kMaxParams = 32;     // cap the generic panel for sanity
};
