// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// JUCE AudioProcessor wrapping the dependency-free reborn_core engine.
// Builds as a Standalone app and a VST3 (AU added on macOS). Host transport
// sync replaces the original's ReWire integration.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include "reborn/engine.h"
#include "reborn/factories.h"   // Bass303Engine, makeBass303
#include "reborn/rbsx.h"        // Session (get/setStateInformation, sessions)
#include "PluginHost.h"         // third-party 303 host harness (fork P5)

class RebornAudioProcessor : public juce::AudioProcessor {
public:
    RebornAudioProcessor();
    ~RebornAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Reborn 338"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // The full session (song + environment + 303 voice slots) as a .rbsx image,
    // so a DAW project persists the whole rig, including a hosted plugin's own
    // state. See captureSession() / restoreSession().
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    // Engine access for the editor.
    reborn::Engine& engine() { return engine_; }

    // --- Pluggable 303 voice engine (the modular-voices seam) ---------------
    // Install a native 303 voice engine into a bass slot (kBass1/kBass2). Builds
    // the voice on the calling (message) thread, then swaps it in under
    // engineLock_ so the audio thread never sees a half-installed voice, and
    // destroys the displaced voice AFTER releasing the lock. A no-op for an
    // engine the core cannot build (HostedPlugin; that path arrives in fork P5).
    void setBassEngine(int channelId, reborn::Bass303Engine engine);
    // The engine currently selected for a bass slot (for the picker + sessions).
    reborn::Bass303Engine bassEngine(int channelId) const {
        return bassEngine_[channelId == reborn::kBass2 ? 1 : 0];
    }
    // Install a hosted third-party plugin voice into a bass slot (fork P5). The
    // editor builds the HostedInstrument off the audio thread; this swaps it in
    // under engineLock_ and frees the displaced voice after the lock, exactly
    // like setBassEngine but for a pre-built (non-registry) voice.
    void installBassPlugin(int channelId, std::unique_ptr<reborn::Instrument> voice,
                           const juce::String& name, const juce::String& identifier);
    // The plugin discovery / instantiation host, owned here so it outlives the
    // editor and its scanned list persists across editor open/close.
    PluginHost& pluginHost() { return pluginHost_; }
    // Display name of a slot's hosted plugin ("" when the slot is a native engine).
    juce::String bassPluginName(int channelId) const {
        return bassPluginName_[channelId == reborn::kBass2 ? 1 : 0];
    }
    void loadSongFile(const juce::File& file);
    void loadDemoSong();
    // Load a canonical drum sample-set directory (extracted from the user's
    // own original-program data); reapplies to the current song. Returns the
    // number of voices that received samples.
    int loadSampleDir(const juce::File& dir);
    // Load drum voices from a .rbm mod the user owns (see reborn/rbm.h). Only
    // the mod's AIFF audio is read; its skin artwork is not.
    int loadModFile(const juce::File& file);
    // Save the current song as .rbs. LOSSY by design (see
    // docs/FORMAT_RBSX.md); ask rbsLossFor() first and warn.
    bool saveSongFile(const juce::File& file, juce::String& err);

    // --- Native sessions (.rbsx) -------------------------------------------
    // A session is the song PLUS the environment it was played in, so
    // reopening one sounds identical instead of merely looking identical.
    bool saveSessionFile(const juce::File& file, juce::String& err);
    // Open either extension: .rbsx restores the environment too, .rbs goes
    // through the plain song path. Returns false if the file could not be read.
    bool openAnyFile(const juce::File& file, juce::String& err);
    juce::String songTitle, songAuthor;
    juce::String sampleDir;          // last loaded sample-set path ("" = synth)
    juce::String modName;            // last loaded .rbm mod name ("" = none)
    juce::String modPath;            // resolved .rbm path, so a session can reopen it
    juce::String modsDir;            // folder to resolve a song's referenced mod from
    juce::String pendingModNote;     // "" or a note about mod resolution on load
    int sampleVoices = 0;            // voices using samples
    bool followHostTransport = true;
    // Standalone transport intent. Without this, processBlock restarted the
    // engine every block when no host transport was present, so the Stop
    // button could never actually stop playback.
    bool transportRunning = true;

    // The editor mutates pattern data from the message thread; take this lock
    // around any edit so the audio thread never reads a half-written step.
    // (processBlock uses a TRY-lock and simply skips a block if it is held.)
    juce::SpinLock& engineLock() { return engineLock_; }

    // The editor registers a callback here so the processor can close any open
    // hosted-plugin editor windows, on the message thread, BEFORE a session
    // restore frees the 303 voices those windows are bound to. Without it, a host
    // re-applying state (or a session load) while a hosted plugin's own UI is open
    // would leave that window pointing at a freed instance: a use-after-free,
    // and for a CLAP plugin a destroy-order violation. Cleared by ~editor.
    void setVoicesWillChangeCallback(std::function<void()> cb) {
        onVoicesWillChange_ = std::move(cb);
    }
    // Called on the message thread at the end of a session restore, after the
    // voice swaps and after play() has dispatched the song's tick-0 dump, so the
    // editor rebuilds its knob panels synchronously in the same call (rather
    // than waiting up to a timer tick, during which a knob drag could still
    // reach a freed voice) and they show the song's start. Cleared by ~editor.
    void setVoicesChangedCallback(std::function<void()> cb) {
        onVoicesChanged_ = std::move(cb);
    }
    // Bumped on every 303 voice swap. The editor's timer watches it so a swap it
    // did NOT initiate (a host getState/setState restore) still triggers a knob-
    // panel rebuild; otherwise its knobs keep pointing at the freed voices.
    std::uint32_t voiceRevision() const {
        return voiceRevision_.load(std::memory_order_relaxed);
    }

private:
    reborn::Engine engine_;
    reborn::StereoBlock scratch_;
    juce::SpinLock engineLock_;
    // Which engine is installed in each 303 slot (index 0 = kBass1, 1 = kBass2).
    reborn::Bass303Engine bassEngine_[2] {
        reborn::kDefaultBass303Engine, reborn::kDefaultBass303Engine };
    juce::String bassPluginName_[2];        // hosted plugin name per slot, if any
    juce::String bassPluginId_[2];          // hosted plugin identifier (to reload it)
    PluginHost   pluginHost_;               // third-party plugin discovery/loading

    // A hosted plugin a restore could NOT reload (not installed on this machine).
    // The slot falls back to the default voice, but the record is kept so a
    // re-save preserves it and it reappears if the plugin returns.
    struct PendingSlot { bool has = false; juce::String pluginId; juce::MemoryBlock state; };
    PendingSlot bassPending_[2];

    // Voice-swap notification for the editor (see the accessors above).
    std::atomic<std::uint32_t> voiceRevision_ { 0 };
    // Set while restoreSession is between loading the song and play(): the
    // voice swaps in between release the lock, and processBlock must not run
    // the new song from the old one's position meanwhile. processBlock reads it
    // with engineLock_ held; restoreSession clears it on every exit.
    std::atomic<bool>          restoring_ { false };
    std::function<void()>      onVoicesWillChange_;   // close hosted UIs pre-swap
    std::function<void()>      onVoicesChanged_;      // rebuild knob panels post-swap

    // Build a Session from the live rig (song + environment + 303 slots + any
    // hosted plugin state). Caller must hold engineLock_.
    reborn::Session captureSession();
    // Restore a whole session: song + environment (locked), then the 303 slots
    // (unlocked; the swap helpers take the lock themselves), then play()
    // (locked), so the tick-0 dump and step 0 reach the restored voices, then
    // the editor's knob rebuild.
    void restoreSession(const reborn::Session& sess, const juce::String& fallbackTitle);
    // Apply the per-slot voice records from a session (install native or hosted
    // voices). Caller must NOT hold engineLock_.
    void applyInstrumentSlots(const reborn::Session& sess);
    void applyInstrumentSlot(int i, int ch, const reborn::InstrumentSlot& sl);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RebornAudioProcessor)
};
