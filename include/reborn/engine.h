// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Top-level engine: owns the instruments, the effects, the mixer and the
// step sequencer, and renders audio. UI-agnostic and host-agnostic.
//
#include <memory>
#include <array>
#include <atomic>
#include "reborn/audio.h"
#include "reborn/processor.h"
#include "reborn/pattern.h"

namespace reborn {

// One mixer channel drives one instrument.
struct MixerChannel {
    float volume     = 0.80f;   // 0..1
    float pan        = 0.0f;    // -1..1
    bool  mute       = false;
    bool  distEnable = false;   // this section's distortion insert is on
    bool  pcfEnable  = false;   // this section's PCF insert is on (exclusive)
    float delaySend  = 0.0f;    // 0..1 send to delay
};

enum ChannelId { kBass1 = 0, kBass2 = 1, kDrum808 = 2, kDrum909 = 3, kNumChannels = 4 };

class Engine {
public:
    Engine();
    ~Engine();

    void prepare(double sampleRate);
    void reset();

    // Transport ------------------------------------------------------------
    void   setTempo(double bpm);
    double tempo() const { return tempo_; }
    void   setShuffle(float amt);           // 0..1
    float  shuffle() const { return shuffle_; }
    void   play();
    void   stop();
    bool   isPlaying() const { return playing_; }
    void   setSongMode(bool on) { songMode_ = on; }
    bool   songMode() const { return songMode_; }
    int    currentStep() const { return step_; }

    // Content --------------------------------------------------------------
    void  loadSong(const Song& s);          // copies patterns + applies tempo
    Song& song() { return song_; }
    const Song& song() const { return song_; }

    // Write the LIVE panel into a song about to be saved (review W3). The GUI
    // edits engine state, not the song: the mixer channels, the routing and
    // unit switches, the effect objects' own parameters, the voices' knobs.
    // This copies all of it into out's PANEL (mixerPanel, effectPanel, tempo,
    // shuffle and the knob headers), which the writer saves as chunks, and
    // leaves the start state (mixerInit/effectInit), the automation and the
    // patterns alone, since those are the recorded song. An empty arrangement
    // gets one row of the patterns now playing. `out` is a copy of song().
    //
    // Field by field, a live value that equals, as the byte the file stores,
    // either the panel's value or the song's start value (the start state, or
    // the tick-0 dump that play() applies) leaves the panel's own in place.
    // Only a value the user or the automation has moved replaces it, so an
    // unedited open, play and save writes the file's chunks back unchanged.
    //
    // On a machine whose sd_snap the sample loader has repurposed (the flags
    // below), the device value is not the knob, so the song's own is kept.
    void captureLivePanel(Song& out) const;

    // The song a save writes: a copy of song() with the live panel captured
    // into it and the given title and author. Every save in the app goes
    // through this (.rbs, .rbsx sessions and DAW state), under the engine lock,
    // and writes the result to disk after releasing it.
    Song songForSave(const std::string& title, const std::string& author) const;

    // Whether the drum sample loader has repurposed each machine's sd_snap. It
    // sets sd_snap to 0 to mute the synthesised snare layer (review P3), but
    // only when both the snare body and its noise take loaded, so these say
    // exactly when the device's sd_snap stopped being the knob. loadSong clears
    // both and loadSampleSetFrom sets each one as it zeroes that machine's.
    bool sdSnapRepurposed808 = false;
    bool sdSnapRepurposed909 = false;

    // Live pattern selection (pattern mode).
    void selectBassPattern(int deviceIndex /*0..1*/, int pat);
    void selectDrumPattern(int deviceIndex /*0..1*/, int pat);
    void selectPcfPattern(int pat);

    // The PCF's currently active pattern index (0..54), and, in song mode, the
    // arrangement row now playing. Read-only, for the UI's arrangement editor.
    int activePcfPattern() const { return pcfPat_; }
    int currentSongRow()  const { return songMode_ ? songRow_ : -1; }

    // Currently active pattern index for a channel (kBass1..kDrum909).
    int activePattern(int channelId) const {
        switch (channelId) {
            case kBass1:   return bass1Pat_;
            case kBass2:   return bass2Pat_;
            case kDrum808: return drum808Pat_;
            case kDrum909: return drum909Pat_;
            default:       return 0;
        }
    }

    // Devices & effects (for UI + parameter access) ------------------------
    Instrument* instrument(int channelId);

    // Replace the voice in a channel slot at runtime: the pluggable-voice seam.
    // Prepares `inst` at the current sample rate, resets that channel's slide/
    // legato memory, swaps `inst` into the slot, and RETURNS the displaced voice
    // so the caller can destroy it OFF the audio thread. channelId is
    // kBass1..kDrum909; an invalid id is a no-op that returns `inst` unchanged.
    //
    // Not audio-thread-safe on its own: the swap itself is a cheap pointer move,
    // but the caller MUST exclude the audio thread for its duration (hold the same
    // lock process() runs under) and should build the incoming voice and destroy
    // the returned one off the audio thread, since those can be slow.
    std::unique_ptr<Instrument> setInstrument(int channelId,
                                              std::unique_ptr<Instrument> inst);
    Effect*     distortion() { return dist_.get(); }
    Effect*     delay()      { return delay_.get(); }
    Effect*     compressor() { return comp_.get(); }
    Effect*     pcf()        { return pcf_.get(); }
    // Routing, with ReBirth's own exclusivity rules. The distortion is a
    // per-section flag any number of sections may set; the PCF and the
    // compressor are exclusive, and setting one clears the others.
    void setDistEnable(int ch, bool on);  // per-section routing only
    void setDistUnit(bool on);            // the distortion unit's own bypass
    bool distUnit() const;
    void setPcfTarget(int ch);            // -1 = nowhere; routing only
    int  pcfTarget() const;
    void setPcfUnit(bool on);             // the unit's own bypass
    bool pcfUnit() const;
    void setCompTarget(int t);            // -1 nowhere, 0..3 section, kCompMaster
    int  compTarget() const { return compTarget_; }
    // The delay unit's own bypass. Switching it OFF clears the delay, as
    // ReBirth does (measured 2026-09-27): a one-off fill of the ring, so the
    // caller holds the lock process() runs under, as for any other setter.
    void setDelayUnit(bool on);
    bool delayUnit() const { return delayUnitOn_; }
    // The 909's flam amount as it plays now, 0..1. It is not a device
    // parameter, so the edit bar sets it here. loadSong takes it from the song's
    // 909 header and automation rides it; the header itself stays as loaded,
    // because it is the panel a save writes back (captureLivePanel).
    float flam909() const { return flam909_; }
    void  setFlam909(float v) { flam909_ = clampf(v, 0.0f, 1.0f); }

    MixerChannel& channel(int id) { return mix_[id]; }
    const MixerChannel& channel(int id) const { return mix_[id]; }
    float masterVolume = 0.72f;
    // Hold every channel's echo send at zero for the whole render, automation
    // and the song's start state included. Setting the sends once is not
    // enough: a song's mixer track rides them, so a --no-fx render quietly
    // grew its delay back partway through and the wet return (which is not
    // centred) skewed the stereo image by 3.9 dB on Trakka Rekka.
    bool disableSends = false;
    // Master compressor on/off, from the song's COMP track (see EffectInit).
    bool compressorOn = false;

    // Debug/analysis solo: when non-zero, only channels whose bit is set are
    // rendered. Unlike MixerChannel::mute this cannot be overridden by song
    // automation (which drives the mute flags).
    int soloMask = 0;

    // Metering -------------------------------------------------------------
    // Post-fader peak for one channel, 0..1, with a ~150 ms release so a UI
    // can sample it at any rate. Written by the audio thread, read by the UI;
    // relaxed atomics because a meter needs freshness, not ordering.
    float channelPeak(int channelId) const {
        return (channelId >= 0 && channelId < kNumChannels)
             ? chPeak_[(std::size_t) channelId].load(std::memory_order_relaxed) : 0.0f;
    }

    // Post-master peak, 0..1, same release as the channel meters. Measured
    // AFTER the master fader and clipper, so it is what actually leaves.
    float masterPeak() const { return mPeak_.load(std::memory_order_relaxed); }

    // Audio ----------------------------------------------------------------
    double sampleRate() const { return sampleRate_; }
    // Render numFrames of stereo audio, advancing the sequencer. Overwrites out.
    void process(StereoBlock& out, int numFrames);

private:
    void renderSlice(StereoBlock& out, int startFrame, int n);
    void fireStep();                        // trigger instruments for step_
    void advanceStep();
    double samplesPerStep(int stepIndex) const;

    double sampleRate_ = kDefaultSampleRate;
    double tempo_      = 120.0;
    float  shuffle_    = 0.0f;
    bool   playing_    = false;
    bool   songMode_   = false;

    // Sequencer position.
    int    step_        = 0;      // 0..length-1 within current patterns
    double sampleInStep_ = 0.0;   // fractional sample counter within step
    int    songRow_     = 0;      // index into arrangement
    int    songRepeat_  = 0;
    uint32_t songTicks_ = 0;      // 1/32-note ticks since song start (2 per step)
    size_t autoIdx_     = 0;      // cursor into song_.automation

    void dispatchAutomation();
    void applyAutoEvent(const AutoEvent& e);
    // The song's start state for everything its tracks' tick-0 dumps load into
    // mixerInit and effectInit rather than the automation: the channels'
    // faders, pans, echo sends and routing, the unit switches, and the PCF,
    // distortion, compressor and delay knobs. loadSong applies it, and so does
    // play() in Song mode, since the rides move all of it.
    void applySongStart();
    void fireDevice(int device);            // fire one device's current step now
    double swingDelaySamples() const;       // shuffle-amount -> off-step delay

    // Shuffle: devices whose active pattern is flagged fire odd steps late.
    // pendingAt_[d] = sample offset within the current step at which device d
    // should fire (< 0 = nothing pending).
    double pendingAt_[4] = { -1, -1, -1, -1 };
    // 909 FLAM: a flam step fires its voices a SECOND time, flamAt_ samples into
    // the step. Separate from pendingAt_ because a step can be both shuffled and
    // flammed, and because only the flam voices repeat, not the whole device.
    double   flamAt_ = -1.0;
    uint32_t flamVoices_ = 0;
    uint32_t flamAccent_ = 0;

    // Active pattern indices (pattern mode / current song row).
    int bass1Pat_ = 0, bass2Pat_ = 0, drum808Pat_ = 0, drum909Pat_ = 0, pcfPat_ = 0;

    // Sequencer controls that live in the drum headers rather than on a
    // device: the 909's flam amount and the 808's shared-column switches. These
    // are the values playing now. Automation used to write them into song_'s
    // headers, which made an unedited save write the song's start state over
    // its panel.
    float flam909_ = 0.5f;
    bool  shared808_[SC_COUNT] = { false, false, false, false, false };

    Song song_;

    std::unique_ptr<Instrument> bass1_, bass2_, drum808_, drum909_;
    std::unique_ptr<Effect>     dist_, delay_, comp_, pcf_;
    // THE DISTORTION IS PER SECTION. ReBirth's panel shows one unit with one
    // pair of knobs, and every routed section passes through a unit with those
    // settings, but the sections do not meet inside it. Measured 2026-08-28:
    // two 303s at 65.4 and 92.5 Hz, both routed, at drive 110. If they shared a
    // waveshaper their intermodulation products would appear at frequencies
    // neither carries alone. Across fourteen such frequencies the excess over
    // the power sum of each played alone is -0.1 dB. So `dist_` is section 0's
    // instance, the UI edits it, and these three mirror its settings.
    std::array<std::unique_ptr<Effect>, kNumChannels - 1> distExtra_;
    Effect* distortionFor(int ch) {
        return ch == 0 ? dist_.get() : distExtra_[(std::size_t)(ch - 1)].get();
    }
    std::array<MixerChannel, kNumChannels> mix_;

    // Scratch buffers reused across slices.
    // No dist/pcf buses any more: each section carries its own chain in
    // chScratch_, which is what the topology measurement showed ReBirth does.
    StereoBlock chScratch_, delayBus_, masterBus_;
    bool  pcfOn_ = false;                 // the unit's bypass, ridden by its track
    bool  distUnitOn_ = false;            // the distortion unit's own bypass
    // The delay unit's own bypass, DELY track param 0. On by default, as every
    // demo song states it, so a song built in Reborn keeps its echo.
    bool  delayUnitOn_ = true;
    // The compressor's target: -1 nowhere, 0..3 a section, kCompMaster the
    // master bus. Exclusive, and confirmed 2026-08-28 by clicking the master
    // strip's own COMP button, which wrote 1 and replaced the section value.
    int   compTarget_ = kCompMaster;

    // Post-fader peak per channel (UI metering only; never read by DSP).
    std::array<std::atomic<float>, kNumChannels> chPeak_ {};
    std::atomic<float> mPeak_ { 0.0f };

    // Remember previous bass step slide state for legato, and whether that
    // step left a note RUNNING, so a slide can tell if it has anything to
    // glide from. Both start in the state an empty pattern implies.
    bool bass1PrevSlide_ = false, bass2PrevSlide_ = false;
    bool bass1PrevSounding_ = false, bass2PrevSounding_ = false;
};

} // namespace reborn
