// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "HostedInstrument.h"
#include <algorithm>

HostedInstrument::HostedInstrument(std::unique_ptr<juce::AudioPluginInstance> plugin,
                                   juce::String displayName)
    : plugin_(std::move(plugin)), name_(std::move(displayName)) {
    buf_.setSize(2, kMaxBlock);
    buildParamCache();   // param names are fixed after construction; snapshot now
}

// Snapshot the plugin's parameter names/labels so paramInfo() never builds a
// juce::String or calls into the plugin on the audio thread (see the header).
void HostedInstrument::buildParamCache() {
    paramCache_.clear();
    if (!plugin_) return;
    const int n = std::min((int) plugin_->getParameters().size(), kMaxParams);
    paramCache_.reserve((size_t) n);
    for (int i = 0; i < n; ++i) {
        auto* p = plugin_->getParameters()[i];
        reborn::ParamInfo info;
        juce::String nm = p->getName(48);
        if (nm.isEmpty()) nm = "param " + juce::String(i);
        info.name         = nm.toStdString();
        info.label        = p->getName(16).toStdString();
        info.minValue     = 0.0f;                 // JUCE parameters are normalised
        info.maxValue     = 1.0f;
        info.defaultValue = p->getDefaultValue();
        info.unit         = p->getLabel().toStdString();
        paramCache_.push_back(std::move(info));
    }
}

HostedInstrument::~HostedInstrument() {
    // The plugin instance is torn down here, on whatever thread destroys this
    // voice. Engine::setInstrument hands the displaced voice back to the app,
    // which frees it on the message thread (PluginProcessor::installBassPlugin),
    // so a plugin's message-thread-only teardown is honoured.
    if (plugin_) plugin_->releaseResources();
}

void HostedInstrument::prepare(double sampleRate) {
    sr_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    if (!plugin_) return;
    // A synth: 0 audio inputs, 2 outputs. Some plugins publish a different
    // layout; setPlayConfigDetails is advisory and JUCE falls back gracefully.
    plugin_->setPlayConfigDetails(0, 2, sr_, kMaxBlock);
    plugin_->prepareToPlay(sr_, kMaxBlock);
    // Size the work buffer to what the plugin ACTUALLY reports, not a hard 2. A
    // plugin that keeps a >2-channel layout despite the 0/2 request would have the
    // JUCE wrapper write past a 2-channel buffer (out-of-bounds); a mono plugin
    // fills only channel 0, so remember its real output count to duplicate it to
    // stereo in process().
    outChannels_ = juce::jmax(1, plugin_->getTotalNumOutputChannels());
    bufChannels_ = juce::jmax(2, juce::jmax(plugin_->getTotalNumInputChannels(),
                                            plugin_->getTotalNumOutputChannels()));
    buf_.setSize(bufChannels_, kMaxBlock, false, false, true);
    midi_.clear();
    midi_.ensureSize(256);          // no first-note heap alloc on the audio thread
    heldNote_ = -1;
    buildParamCache();
}

void HostedInstrument::reset() {
    midi_.clear();
    if (plugin_) {
        // Belt and braces: some plugins do not silence a sounding voice on
        // reset(), so send an explicit note-off for whatever is held rather than
        // orphaning it. Delivered on the next process().
        if (heldNote_ >= 0) queueNoteOff(heldNote_);
        plugin_->reset();
    }
    heldNote_ = -1;
}

int HostedInstrument::numParams() const {
    return (int) paramCache_.size();
}

reborn::ParamInfo HostedInstrument::paramInfo(int index) const {
    if (index < 0 || index >= (int) paramCache_.size()) return reborn::ParamInfo{};
    return paramCache_[(size_t) index];   // cached: no allocation, no plugin call
}

float HostedInstrument::getParam(int index) const {
    if (!plugin_ || index < 0 || index >= numParams()) return 0.0f;
    return plugin_->getParameters()[index]->getValue();
}

void HostedInstrument::setParam(int index, float value) {
    if (!plugin_ || index < 0 || index >= numParams()) return;
    auto* p = plugin_->getParameters()[index];
    const float v = juce::jlimit(0.0f, 1.0f, value);
    // Song-mode automation reaches this on the AUDIO thread (Engine ->
    // setParamByName): setValueNotifyingHost would fire the plugin's parameter
    // listeners (an open editor) synchronously from the audio thread, which is not
    // RT-safe. On the message thread (the generic knob panel) notify normally so
    // the plugin's own UI tracks the knob; off it, just set the value.
    if (juce::MessageManager::existsAndIsCurrentThread())
        p->setValueNotifyingHost(v);
    else
        p->setValue(v);
}

// --- BassEvent -> MIDI. All events land at offset 0 of the coming slice, since
// the engine fires a step's events immediately before rendering that step. ----

void HostedInstrument::queueNoteOn(int note, int vel) {
    midi_.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) vel), 0);
}
void HostedInstrument::queueNoteOff(int note) {
    midi_.addEvent(juce::MidiMessage::noteOff(1, note), 0);
}

void HostedInstrument::trigger(int note, float /*velocity*/, bool accent, bool slide) {
    note = juce::jlimit(0, 127, note);
    // A plain note must stay BELOW the accent threshold. jc303 and Open303 treat
    // velocity >= 100 as an accent, so sending 100 for a non-accent step would
    // fire the accent envelope on every step and make the accent flag inert. 64
    // matches what the native Open303 voice sends (bass_open303.cpp).
    const int vel = accent ? 127 : 64;
    if (slide && heldNote_ >= 0) {
        // Legato: sound the new note WHILE the old one is still on (most 303
        // synths read the overlap as a glide), then release the old one. A slide
        // INTO the same pitch is a no-op: queueNoteOn(X) then queueNoteOff(X) at
        // the same offset reads as an immediate note-off on a rosic-derived synth
        // (its note-off matches by key and drops the still-ringing note), so the
        // already-sounding note is left alone. Same hazard the native Open303
        // voice guards at bass_open303.cpp.
        if (note != heldNote_) {
            queueNoteOn(note, vel);
            queueNoteOff(heldNote_);
        }
    } else {
        if (heldNote_ >= 0) queueNoteOff(heldNote_);   // retrigger
        queueNoteOn(note, vel);
    }
    heldNote_ = note;
}

void HostedInstrument::release() {
    if (heldNote_ >= 0) queueNoteOff(heldNote_);
    heldNote_ = -1;
}

void HostedInstrument::setStepTiming(double, bool) {
    // The half-step gate is a native-voice VCA behaviour; a hosted synth uses
    // its own envelope and is gated by trigger()/release() only.
}

void HostedInstrument::setStepNote(int) {
    // Pitch-follows-a-rest is a ReBirth quirk MIDI cannot express without gating.
    // Documented loss (docs/PLUGINS.md); the note arrives on trigger() anyway.
}

void HostedInstrument::holdStep(double, bool, bool) {
    // A tied-into rest carries the sounding note. In MIDI the note-on was never
    // released, so it already sustains; nothing to send.
}

void HostedInstrument::accentOnly() {
    // Accent on a ringing note with no gate: MIDI has no accent-without-note.
    // Documented loss.
}

void HostedInstrument::process(reborn::StereoBlock& out, int numFrames) {
    if (!plugin_) {
        for (int i = 0; i < numFrames; ++i) {
            out.left[(std::size_t) i] = 0.0f;
            out.right[(std::size_t) i] = 0.0f;
        }
        midi_.clear();
        return;
    }
    int done = 0;
    bool first = true;
    while (done < numFrames) {
        const int chunk = std::min(numFrames - done, kMaxBlock);
        buf_.clear();
        // View has as many channels as the plugin reports, so a >2-out plugin
        // cannot write past the buffer.
        juce::AudioBuffer<float> view(buf_.getArrayOfWritePointers(), bufChannels_, chunk);
        if (first) {
            plugin_->processBlock(view, midi_);   // this slice's note events
            first = false;
        } else {
            juce::MidiBuffer none;
            plugin_->processBlock(view, none);
        }
        const float* l = view.getReadPointer(0);
        // A genuinely mono plugin fills only channel 0; duplicate it to both.
        const float* r = outChannels_ >= 2 ? view.getReadPointer(1) : l;
        for (int i = 0; i < chunk; ++i) {
            out.left[(std::size_t) (done + i)]  = l[i];
            out.right[(std::size_t) (done + i)] = r[i];
        }
        done += chunk;
    }
    midi_.clear();
}
