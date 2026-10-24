// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Reborn Open303: the standalone "fat" 303 instrument plugin.
//
// This is the best-of-breed Open303 voice (the same makeOpen303() Reborn offers
// as a selectable 303 voice) wrapped as a MIDI-driven instrument, plus ReBirth's 303
// knob-FX (distortion), so the plugin is self-contained in any DAW. CLAP is the
// license-clean primary format; VST3/AU are added under Steinberg's terms.
//
// It emulates the Roland *hardware* 303, not ReBirth, so it is a synthesis
// product, never a ReBirth-parity engine (that stays the native TPT voice inside
// Reborn). Inside Reborn this DSP is LINKED directly, not hosted; this target
// exists only for use in other hosts.
//
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include "reborn/factories.h"
#include "reborn/processor.h"

class Open303PluginProcessor : public juce::AudioProcessor {
public:
    Open303PluginProcessor();
    ~Open303PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Reborn Open303"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Synth: no input bus; mono or stereo output.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorValueTreeState& state() { return apvts_; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void applyParams();
    void renderVoice(int dstOffset, int count);
    void handleMidi(const juce::MidiMessage& m);
    void noteOn(int note, int vel);
    void noteOff(int note);
    void allNotesOff();

    juce::AudioProcessorValueTreeState apvts_;

    // The shared best-of-breed voice + ReBirth's 303 distortion, both from
    // reborn_core (JUCE-free). The plugin only ever sees the Instrument / Effect
    // interfaces, so the vendored Open303 headers stay internal to reborn_core.
    std::unique_ptr<reborn::Instrument> voice_;
    std::unique_ptr<reborn::Effect>     dist_;

    // Cached parameter handles (RT-safe reads) + cached voice/effect indices
    // (so the per-block apply never does a by-name lookup on the audio thread).
    std::atomic<float>* pTuning_ = nullptr; std::atomic<float>* pWave_    = nullptr;
    std::atomic<float>* pCutoff_ = nullptr; std::atomic<float>* pReso_    = nullptr;
    std::atomic<float>* pEnvmod_ = nullptr; std::atomic<float>* pDecay_   = nullptr;
    std::atomic<float>* pAccent_ = nullptr; std::atomic<float>* pVolume_  = nullptr;
    std::atomic<float>* pDistOn_ = nullptr; std::atomic<float>* pDistAmt_ = nullptr;
    std::atomic<float>* pDistTone_ = nullptr; std::atomic<float>* pOutLevel_ = nullptr;
    int iTuning_ = -1, iWave_ = -1, iCutoff_ = -1, iReso_ = -1;
    int iEnvmod_ = -1, iDecay_ = -1, iAccent_ = -1, iVolume_ = -1;
    int idAmount_ = -1, idTone_ = -1, idLevel_ = -1;

    // Per-block work buffers. voiceBlock_ is the whole block, assembled from
    // sub-slices (slice_) rendered between MIDI events for sample-accurate note
    // timing. Sized in prepareToPlay, grown only if a host sends a larger block.
    reborn::StereoBlock voiceBlock_;
    reborn::StereoBlock slice_;

    // Mono note stack: a 303 is monophonic with last-note priority, and a new
    // note struck over a held one glides (slide). Kept here so held-note
    // bookkeeping is robust to whatever note order a host throws at us.
    std::vector<int> held_;
    bool  distOn_   = false;
    float outLevel_ = 0.8f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Open303PluginProcessor)
};
