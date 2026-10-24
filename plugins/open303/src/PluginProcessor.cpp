// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "PluginProcessor.h"

using APVTS = juce::AudioProcessorValueTreeState;

APVTS::ParameterLayout Open303PluginProcessor::makeLayout() {
    APVTS::ParameterLayout layout;
    auto addF = [&layout](const char* id, const char* name, float lo, float hi,
                          float def, const char* unit = "") {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ id, 1 }, name,
            juce::NormalisableRange<float>(lo, hi), def,
            juce::AudioParameterFloatAttributes().withLabel(unit)));
    };
    // Voice: the same 303 controls Reborn exposes, same ranges. (drive is inert
    // on Open303; the plugin's FX section provides the grit instead.)
    addF("tuning",    "Tuning",   -12.0f, 12.0f, 0.00f, "st");
    addF("waveform",  "Waveform",   0.0f,  1.0f, 0.00f);   // 0 = saw, 1 = square
    addF("cutoff",    "Cutoff",     0.0f,  1.0f, 0.35f);
    addF("resonance", "Resonance",  0.0f,  1.0f, 0.55f);
    addF("envmod",    "Env Mod",    0.0f,  1.0f, 0.55f);
    addF("decay",     "Decay",      0.0f,  1.0f, 0.55f);
    addF("accent",    "Accent",     0.0f,  1.0f, 0.50f);
    addF("volume",    "Volume",     0.0f,  1.0f, 0.85f);
    // FX: ReBirth's 303 distortion (the classic knob-FX), plus a master trim.
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "dist_on", 1 }, "Dist On", false));
    addF("dist_amount", "Dist Amount", 0.0f, 1.0f, 0.50f);
    addF("dist_tone",   "Dist Tone",   0.0f, 1.0f, 0.50f);
    addF("out_level",   "Out Level",   0.0f, 1.0f, 0.80f);
    return layout;
}

Open303PluginProcessor::Open303PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", makeLayout()) {
    voice_ = reborn::makeOpen303();
    dist_  = reborn::makeDistortion();

    // Cache the voice/effect parameter indices once, so the per-block apply is a
    // direct setParam(index) and never a by-name search on the audio thread.
    iTuning_ = voice_->paramIndex("tuning");
    iWave_   = voice_->paramIndex("waveform");
    iCutoff_ = voice_->paramIndex("cutoff");
    iReso_   = voice_->paramIndex("resonance");
    iEnvmod_ = voice_->paramIndex("envmod");
    iDecay_  = voice_->paramIndex("decay");
    iAccent_ = voice_->paramIndex("accent");
    iVolume_ = voice_->paramIndex("volume");
    idAmount_ = dist_->paramIndex("amount");
    idTone_   = dist_->paramIndex("tone");
    idLevel_  = dist_->paramIndex("level");

    pTuning_   = apvts_.getRawParameterValue("tuning");
    pWave_     = apvts_.getRawParameterValue("waveform");
    pCutoff_   = apvts_.getRawParameterValue("cutoff");
    pReso_     = apvts_.getRawParameterValue("resonance");
    pEnvmod_   = apvts_.getRawParameterValue("envmod");
    pDecay_    = apvts_.getRawParameterValue("decay");
    pAccent_   = apvts_.getRawParameterValue("accent");
    pVolume_   = apvts_.getRawParameterValue("volume");
    pDistOn_   = apvts_.getRawParameterValue("dist_on");
    pDistAmt_  = apvts_.getRawParameterValue("dist_amount");
    pDistTone_ = apvts_.getRawParameterValue("dist_tone");
    pOutLevel_ = apvts_.getRawParameterValue("out_level");
}

void Open303PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    if (voice_) voice_->prepare(sampleRate);
    if (dist_)  dist_->prepare(sampleRate);
    const std::size_t n = (std::size_t) juce::jmax(1, samplesPerBlock);
    voiceBlock_.resize(n);
    slice_.resize(n);
    held_.clear();
}

void Open303PluginProcessor::reset() {
    held_.clear();
    if (voice_) voice_->reset();
    if (dist_)  dist_->reset();
}

bool Open303PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannels() == 0;   // instrument: no audio input
}

void Open303PluginProcessor::applyParams() {
    voice_->setParam(iTuning_, pTuning_->load());
    voice_->setParam(iWave_,   pWave_->load());
    voice_->setParam(iCutoff_, pCutoff_->load());
    voice_->setParam(iReso_,   pReso_->load());
    voice_->setParam(iEnvmod_, pEnvmod_->load());
    voice_->setParam(iDecay_,  pDecay_->load());
    voice_->setParam(iAccent_, pAccent_->load());
    voice_->setParam(iVolume_, pVolume_->load());
    dist_->setParam(idAmount_, pDistAmt_->load());
    dist_->setParam(idTone_,   pDistTone_->load());
    dist_->setParam(idLevel_,  1.0f);            // master trim is applied on output
    distOn_   = pDistOn_->load() > 0.5f;
    outLevel_ = pOutLevel_->load();
}

void Open303PluginProcessor::renderVoice(int dstOffset, int count) {
    if (count <= 0) return;
    voice_->process(slice_, count);              // slice_ is filled [0, count)
    for (int j = 0; j < count; ++j) {
        voiceBlock_.left[(std::size_t) (dstOffset + j)]  = slice_.left[(std::size_t) j];
        voiceBlock_.right[(std::size_t) (dstOffset + j)] = slice_.right[(std::size_t) j];
    }
}

void Open303PluginProcessor::noteOn(int note, int vel) {
    if (vel <= 0) { noteOff(note); return; }     // note-on with velocity 0 == note-off
    const bool accent = vel >= 100;              // Open303's accent convention
    const bool slide  = !held_.empty();          // gliding out of a still-held note
    voice_->setStepTiming(0.0, /*tie=*/true);    // sustain until the DAW note-off
    voice_->setStepNote(note);
    voice_->trigger(note, (float) vel / 127.0f, accent, slide);
    held_.push_back(note);
}

void Open303PluginProcessor::noteOff(int note) {
    for (int i = (int) held_.size() - 1; i >= 0; --i)
        if (held_[i] == note) { held_.erase(held_.begin() + i); break; }
    if (held_.empty()) {
        voice_->release();
    } else {
        // Mono last-note priority: glide back to the most recent note still held.
        const int top = held_.back();
        voice_->setStepTiming(0.0, true);
        voice_->setStepNote(top);
        voice_->trigger(top, 0.5f, /*accent=*/false, /*slide=*/true);
    }
}

void Open303PluginProcessor::allNotesOff() {
    held_.clear();
    if (voice_) voice_->release();
}

void Open303PluginProcessor::handleMidi(const juce::MidiMessage& m) {
    if (m.isNoteOn())                              noteOn(m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())                        noteOff(m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) allNotesOff();
}

void Open303PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n    = buffer.getNumSamples();
    const int nOut = buffer.getNumChannels();

    if ((int) voiceBlock_.size() < n) voiceBlock_.resize((std::size_t) n);
    if ((int) slice_.size()      < n) slice_.resize((std::size_t) n);

    applyParams();

    // Render the voice in sub-slices bounded by MIDI events, so note timing is
    // sample-accurate rather than quantised to the block.
    int pos = 0;
    for (const auto meta : midi) {
        const int off = juce::jlimit(0, n, meta.samplePosition);
        if (off > pos) { renderVoice(pos, off - pos); pos = off; }
        handleMidi(meta.getMessage());
    }
    if (pos < n) renderVoice(pos, n - pos);

    // ReBirth's 303 distortion, then the master output trim.
    if (distOn_ && dist_) dist_->process(voiceBlock_, n);

    const float g = outLevel_;
    float* L = buffer.getWritePointer(0);
    float* R = nOut > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < n; ++i) {
        L[i] = voiceBlock_.left[(std::size_t) i] * g;
        if (R) R[i] = voiceBlock_.right[(std::size_t) i] * g;
    }
    for (int ch = 2; ch < nOut; ++ch) buffer.clear(ch, 0, n);
}

void Open303PluginProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void Open303PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* Open303PluginProcessor::createEditor() {
    // A generic knob panel over the parameter tree. A skinned rack surface is
    // future work; the generic editor is fully functional and host-safe.
    return new juce::GenericAudioProcessorEditor(*this);
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new Open303PluginProcessor();
}
