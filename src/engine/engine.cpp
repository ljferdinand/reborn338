// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "reborn/engine.h"
#include <cstdio>
#include <cstdlib>
#include "reborn/factories.h"
#include "reborn/rbs.h"   // tuneSemitonesFromByte: the tune slot table
#include "reborn/step_events.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace reborn {


constexpr int kStepsPerBar = 16;
// The engine's master gain at the nominal master byte 100: loadSong multiplies
// the song's linear master by it, and captureLivePanel divides it back out.
constexpr float kMasterHeadroom = 0.72f;

// The drum knob headers by name, one table per machine, shared by loadSong
// (which applies a song's header to the device) and captureLivePanel (which
// reads the live device back into one) so the two directions cannot drift.
// loadSong sets them in table order, which is the order it always has.
static const char* const kLevelNames909[11] = {
    "level_Bass Drum","level_Snare","level_Low Tom","level_Mid Tom",
    "level_Hi Tom","level_Rim","level_Clap","level_Open Hat",
    "level_Closed Hat","level_Crash","level_Ride" };
static const char* const kLevelNames808[11] = {
    "level_Bass Drum","level_Snare","level_Low Tom","level_Mid Tom",
    "level_Hi Tom","level_Rim Shot","level_Hand Clap","level_Cowbell",
    "level_Cymbal","level_Open Hat","level_Closed Hat" };
// The alternate voice of each shared 808 column, and the column it shares.
static const char* const kAltLevelNames808[SC_COUNT] = {
    "level_Low Conga", "level_Mid Conga", "level_Hi Conga",
    "level_Claves",    "level_Maracas" };
static const int kAltLevelSrc808[SC_COUNT] = { 2, 3, 4, 5, 6 };   // column index
// The header byte, which is also the drum track's param, of each level above.
// The 909's two hats share byte 22.
static const int kLevelParam909[11] = { 3, 7, 11, 14, 17, 20, 21, 22, 22, 25, 27 };
static const int kLevelParam808[11] = { 3, 6, 9, 12, 15, 18, 20, 22, 23, 26, 28 };
// The 808's five shared-column switch bytes, in SharedColumn808 order.
static const int kSwitchParam808[SC_COUNT] = { 11, 14, 17, 19, 21 };

// A drum knob: device parameter, header field, and header byte (= track param).
struct DrumKnobName { const char* name; float DrumKnobs::* field; int param; };
static const DrumKnobName kKnobs909[] = {
    { "bd_tune",   &DrumKnobs::bdTune,    4 }, { "bd_decay",  &DrumKnobs::bdDecay,   6 },
    { "bd_attack", &DrumKnobs::bdAttack,  5 }, { "sd_tune",   &DrumKnobs::sdTune,    8 },
    { "sd_tone",   &DrumKnobs::sdTone,    9 }, { "sd_snap",   &DrumKnobs::sdSnappy, 10 },
    { "lt_tune",   &DrumKnobs::ltTune,   12 }, { "mt_tune",   &DrumKnobs::mtTune,   15 },
    { "ht_tune",   &DrumKnobs::htTune,   18 }, { "lt_decay",  &DrumKnobs::ltDecay,  13 },
    { "mt_decay",  &DrumKnobs::mtDecay,  16 }, { "ht_decay",  &DrumKnobs::htDecay,  19 },
    { "ch_decay",  &DrumKnobs::chDecay,  23 }, { "oh_decay",  &DrumKnobs::ohDecay,  24 },
    { "cym_tune",  &DrumKnobs::cymTune,  26 }, { "rd_tune",   &DrumKnobs::rdTune,   28 },
    { "accent",    &DrumKnobs::accent,    2 } };
static const DrumKnobName kKnobs808[] = {
    { "bd_tune",  &DrumKnobs::bdTune,   4 }, { "bd_decay", &DrumKnobs::bdDecay,   5 },
    { "sd_tone",  &DrumKnobs::sdTone,   7 }, { "sd_snap",  &DrumKnobs::sdSnappy,  8 },
    { "lt_tune",  &DrumKnobs::ltTune,  10 }, { "mt_tune",  &DrumKnobs::mtTune,   13 },
    { "ht_tune",  &DrumKnobs::htTune,  16 }, { "oh_decay", &DrumKnobs::ohDecay,  27 },
    { "cy_decay", &DrumKnobs::crDecay, 25 }, { "cy_tune",  &DrumKnobs::cymTune,  24 },
    { "accent",   &DrumKnobs::accent,   2 } };

// Apply a song's decoded 303 knob settings to a voice (by name, so it works for
// any Instrument: native TPT, Open303, or a hosted plugin adapter). Used by
// loadSong and by setInstrument, so a swapped-in voice matches the loaded patch.
static void applyBassKnobs(Instrument* inst, const BassKnobs& k) {
    if (!inst || !k.present) return;
    inst->setParamByName("tuning",    k.tuning);
    inst->setParamByName("cutoff",    k.cutoff);
    inst->setParamByName("resonance", k.resonance);
    inst->setParamByName("envmod",    k.envmod);
    inst->setParamByName("decay",     k.decay);
    inst->setParamByName("accent",    k.accent);
    inst->setParamByName("waveform",  k.waveform);
}

Engine::Engine() {
    // Both 303 slots start on the default voice engine (the native TPT parity
    // voice, kDefaultBass303Engine; the UI picker, or reborn_render's
    // REBORN_BASS_ENGINE=open303, swaps in Open303 via setInstrument). Built
    // through the registry so the slot is a pluggable seam from the first note.
    bass1_   = makeBass303(kDefaultBass303Engine);
    bass2_   = makeBass303(kDefaultBass303Engine);
    drum808_ = makeDrum808();
    drum909_ = makeDrum909();
    dist_    = makeDistortion();
    delay_   = makeDelay();
    comp_    = makeCompressor();
    pcf_     = makePCF();
    for (auto& d : distExtra_) d = makeDistortion();

    // Sensible default routing: drums centered, basses slightly spread.
    mix_[kBass1].pan = -0.15f;
    mix_[kBass2].pan =  0.15f;

    // The delay sits on a send bus: channels feed it via delaySend and its
    // output is summed into the master. It returns wet-only by construction --
    // process() overwrites its bus with the panned echo return, no dry path.

    // The compressor is an assignable insert in the original, not a master
    // strip: keep it transparent unless a song configures it (threshold at
    // 0 dB never engages; no make-up gain).
    if (comp_) {
        comp_->setParamByName("threshold", 1.0f);
        comp_->setParamByName("makeup", 0.0f);
    }
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (Instrument* i : { bass1_.get(), bass2_.get(), drum808_.get(), drum909_.get() })
        if (i) i->prepare(sampleRate);
    for (Effect* e : { dist_.get(), delay_.get(), comp_.get(), pcf_.get() })
        if (e) { e->prepare(sampleRate); e->setTempo(tempo_); }
    for (auto& d : distExtra_) if (d) { d->prepare(sampleRate); d->setTempo(tempo_); }
    reset();
}

void Engine::reset() {
    step_ = 0; sampleInStep_ = 0.0; songRow_ = 0; songRepeat_ = 0;
    songTicks_ = 0; autoIdx_ = 0;
    for (double& p : pendingAt_) p = -1.0;
    flamAt_ = -1.0; flamVoices_ = 0; flamAccent_ = 0;
    bass1PrevSlide_ = bass2PrevSlide_ = false;
    bass1PrevSounding_ = bass2PrevSounding_ = false;
    for (Instrument* i : { bass1_.get(), bass2_.get(), drum808_.get(), drum909_.get() })
        if (i) i->reset();
    for (Effect* e : { dist_.get(), delay_.get(), comp_.get(), pcf_.get() })
        if (e) e->reset();
    for (auto& d : distExtra_) if (d) d->reset();
}

void Engine::setTempo(double bpm) {
    tempo_ = std::max(20.0, std::min(300.0, bpm));
    for (Effect* e : { delay_.get(), pcf_.get() }) if (e) e->setTempo(tempo_);
}

void Engine::setShuffle(float amt) { shuffle_ = clampf(amt, 0.0f, 1.0f); }

void Engine::play() {
    const bool song = songMode_ && !song_.arrangement.empty();
    // Song mode plays from bar 1 and from the song's recorded start, as
    // ReBirth's tick-0 dump does. fireStep() below re-dispatches the instrument
    // tracks' dumps; the mixer and effect dumps live in mixerInit and
    // effectInit, and their rides may have moved everything since loadSong, so
    // a Stop then Play or a host restart used to start where the last ride left
    // the faders, routing and effects. Before reset(), because a unit's reset
    // takes its knob smoothers from its current knobs (the PCF's glide from the
    // last ride otherwise). Pattern mode plays no automation and keeps the
    // panel as the user left it.
    if (song) applySongStart();
    reset();
    playing_ = true;
    if (song) {
        songRow_ = 0;
        const SongStep& r = song_.arrangement[0];
        bass1Pat_ = r.bass1Pattern; bass2Pat_ = r.bass2Pattern;
        drum808Pat_ = r.drum808Pattern; drum909Pat_ = r.drum909Pattern; pcfPat_ = r.pcfPattern;
    }
    fireStep();   // fire step 0 immediately
}

void Engine::stop() {
    playing_ = false;
    for (Instrument* i : { bass1_.get(), bass2_.get() }) if (i) i->release();
}

void Engine::loadSong(const Song& s) {
    song_ = s;
    setTempo(s.tempo);
    setShuffle(s.shuffle);
    bass1Pat_ = bass2Pat_ = drum808Pat_ = drum909Pat_ = pcfPat_ = 0;
    songMode_ = !song_.arrangement.empty();
    // The header sets sd_snap below, so it is the knob again until a sample
    // set is loaded for this song.
    sdSnapRepurposed808 = sdSnapRepurposed909 = false;

    // Apply decoded bass-synth knob settings, if the song carried them.
    applyBassKnobs(bass1_.get(), song_.bass1Knobs);
    applyBassKnobs(bass2_.get(), song_.bass2Knobs);

    // Apply decoded drum-machine knob headers (panel-order levels + voice
    // controls; see docs/FORMAT_RBS.md section 4).
    if (drum909_ && song_.drum909Knobs.present) {
        const DrumKnobs& k = song_.drum909Knobs;
        for (int v = 0; v < 11; ++v)
            if (k.level[v] >= 0.0f) drum909_->setParamByName(kLevelNames909[v], k.level[v]);
        for (const DrumKnobName& n : kKnobs909) drum909_->setParamByName(n.name, k.*n.field);
    }
    if (drum808_ && song_.drum808Knobs.present) {
        const DrumKnobs& k = song_.drum808Knobs;
        for (int v = 0; v < 11; ++v)
            if (k.level[v] >= 0.0f) drum808_->setParamByName(kLevelNames808[v], k.level[v]);
        // A shared column has ONE level fader driving either of its two voices,
        // so the alternate has to inherit it. Without this the congas, claves
        // and maracas ignored the song and sat at the engine's default level.
        for (int c = 0; c < SC_COUNT; ++c)
            if (k.level[kAltLevelSrc808[c]] >= 0.0f)
                drum808_->setParamByName(kAltLevelNames808[c], k.level[kAltLevelSrc808[c]]);
        for (const DrumKnobName& n : kKnobs808) drum808_->setParamByName(n.name, k.*n.field);
    }
    // Header values that are not device parameters, taken whether or not the
    // song carried a header, as they always were.
    flam909_ = song_.drum909Knobs.flam;
    std::copy(std::begin(song_.drum808Knobs.sharedPrimary),
              std::end(song_.drum808Knobs.sharedPrimary), shared808_);

    applySongStart();
}

// The mixer and effect start state: the chunks, with each track's tick-0 dump
// over them (the loader's Song::mixerInit and effectInit). loadSong applies it,
// and play() again in Song mode. Everything here is ridden by some song, so a
// restart that skipped it would start from the last ride (review, 2026-09-27).
// A render calls play() once straight after loadSong, so there the second pass
// sets what the first already set and changes nothing.
void Engine::applySongStart() {
    if (song_.mixerInit.present) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            // The ceiling is the fader law at byte 127 (+9.99 dB), not 1.0:
            // clamping a channel to unity would flatten the top quarter of
            // every fader's travel.
            mix_[ch].volume = clampf(song_.mixerInit.volume[ch], 0.0f, 3.16f * kChannelUnityGain);
            mix_[ch].pan    = clampf(song_.mixerInit.pan[ch], -1.0f, 1.0f);
        }
    }

    // The delay unit's own on/off. Taken whether or not the song carried effect
    // data, so a song that says nothing plays with the delay on, the EffectInit
    // default, rather than inheriting a previous song's bypass.
    setDelayUnit(song_.effectInit.delayOn);

    if (!song_.effectInit.present) return;
    const EffectInit& fx = song_.effectInit;
    // fx.masterVolume is already a linear gain, 1.0 at the nominal byte 100, so
    // 0.72 stays the engine's headroom at nominal. The ceiling is the law's
    // value at byte 127 (+9.98 dB).
    masterVolume = kMasterHeadroom * clampf(fx.masterVolume, 0.0f, 3.16f);
    compressorOn = fx.compressorOn;
    for (int ch = 0; ch < kNumChannels; ++ch) {
        // The distortion is a per-section flag; the PCF is an exclusive insert
        // on one section (manual p61, p64).
        // ROUTING only. The unit's own bypass is `distUnitOn_`, and folding the
        // two together here was invisible until songs turned out to perform
        // the routing, at which point an automated switch would have distorted
        // a section whose unit is off.
        mix_[ch].distEnable = fx.distOn[ch];
        mix_[ch].pcfEnable  = (fx.pcfTarget == ch);
        // --no-fx holds the sends at zero through the start state too, as it
        // does through the rides.
        mix_[ch].delaySend  = disableSends ? 0.0f : clampf(fx.delaySend[ch], 0.0f, 1.0f);
        if (!fx.channelOn[ch]) mix_[ch].mute = true;
    }
    if (dist_) {
        dist_->setParamByName("amount", fx.distAmount);
        dist_->setParamByName("tone",   fx.distTone);
    }
    if (pcf_) {
        pcf_->setParamByName("cutoff",    fx.pcfFreq);
        pcf_->setParamByName("resonance", fx.pcfQ);
        pcf_->setParamByName("envmod",    fx.pcfAmt);
        pcf_->setParamByName("decay",     fx.pcfDecay);
        // Mode byte 0 is BANDPASS and 1 is LOWPASS, measured 2026-08-27.
        pcf_->setParamByName("mode",      fx.pcfBandpass ? 0.0f : 1.0f);
        pcf_->setParamByName("pattern",    float(fx.pcfPattern) / 54.0f);
    }
    distUnitOn_ = fx.distortionOn;
    pcfPat_ = fx.pcfPattern;
    pcfOn_ = fx.pcfOn;
    compTarget_ = fx.compTarget;
    if (comp_) {
        comp_->setParamByName("ratio",     fx.compRatio);
        comp_->setParamByName("threshold", fx.compThreshold);
    }
    // The DELY track's tick-0 dump, not the DELY chunk, when the song has one:
    // in Song mode ReBirth plays the track (review E3, measured).
    if (delay_) {
        if (fx.delaySteps >= 1) delay_->setParamByName("steps", float(fx.delaySteps) / 16.0f);
        delay_->setParamByName("grid",     fx.delayStraight ? 1.0f : 0.0f);
        delay_->setParamByName("feedback", fx.delayFeedback);
        delay_->setParamByName("pan",      float(fx.delayPan) / 127.0f);
    }
}

namespace {

// The byte a captured field takes in the file, each mirroring the writer
// (src/io/rbs_write.cpp), so the capture compares only what a save can hold.
int knobByte(float v)      { return (int) std::lround(std::max(0.0f, std::min(127.0f, v * 127.0f))); }
int levelByte(float v)     { return knobByte(v < 0.0f ? 0.79f : v); }   // -1 is written as the default
int boolByte(bool b)       { return b ? 1 : 0; }
int clampByte(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
int compTargetByte(int t)  { return t == kCompMaster ? 1 : (t >= 0 && t <= 3) ? t + 2 : 0; }
int pcfTargetByte(int t)   { return (t >= 0 && t <= 3) ? t + 1 : 0; }
int tuneByte(float st)     { return tuneByteFromSemitones((int) std::lround(st)); }
int masterByte(float g)    { return faderByteFromGain(g, 1.0f); }
int channelByte(float g)   { return faderByteFromGain(g, kChannelUnityGain); }
int tempoKey(double bpm)   { return (int) std::lround(bpm * 1000.0); }   // GLOB +2

// The rule for one panel field (review follow-up F1). The live value replaces
// the panel's only when, as a file byte, it differs from both the panel and
// the song's start (startByte, -1 when the song has none). A control that sits
// where the loaded chunk put it, or where the start state put it when play()
// dispatched the tick-0 dump, has not been moved, so the chunk keeps its own
// byte and an unedited open-then-save writes the file's panel back unchanged.
// A value moved by the user or by automation reaches the panel, as in ReBirth,
// whose chunk is where the knobs are at save time.
template <typename T, typename Enc>
void settle(T& panel, const T& live, Enc enc, int startByte) {
    const int b = enc(live);
    if (b != enc(panel) && b != startByte) panel = live;
}

} // namespace

// The inverse of loadSong for everything the GUI edits (review W3). It fills
// the song's PANEL only, field by field under settle()'s rule. The start state
// and the automation are the recorded song and stay as loaded, which is how
// ReBirth keeps them too: its chunks are the panel at save time and its tracks
// are what plays. `out` is a copy of song(), whose panel and start it reads.
void Engine::captureLivePanel(Song& out) const {
    // One parameter by name, only where the processor exposes it: a hosted
    // plugin in a 303 slot has its own names, and then the song's value stands.
    auto read = [](const Processor* p, const char* name, float& dst) {
        const int i = p ? p->paramIndex(name) : -1;
        if (i < 0) return false;
        dst = p->getParam(i);
        return true;
    };

    // Each track's tick-0 value, where the loader keeps it in the automation:
    // the instrument tracks (param 0 and the knobs). The mixer, DELY, PCF,
    // DIST and COMP dumps load into mixerInit / effectInit instead. The last
    // event wins, as it does when play() dispatches them.
    int start0[9][32];
    for (auto& row : start0) for (int& x : row) x = -1;
    for (const AutoEvent& e : out.automation)
        if (e.tick == 0 && e.param < 32 && int(e.target) < 9) start0[int(e.target)][e.param] = e.value;
    auto knob0 = [&](AutoTarget t, int param) {
        const int r = start0[int(t)][param];
        return r < 0 ? -1 : knobByte(float(r) / 127.0f);
    };
    auto switch0 = [&](AutoTarget t, int param) {
        const int r = start0[int(t)][param];
        return r < 0 ? -1 : boolByte(r != 0);
    };

    settle(out.tempo, (double) tempo_, tempoKey, -1);
    settle(out.shuffle, shuffle_, knobByte, -1);

    // A song built in Reborn has no panel yet; its start state is what the
    // writer would have saved in the panel's place, so the rule starts there.
    if (!out.mixerPanel.present && out.mixerInit.present)   out.mixerPanel  = out.mixerInit;
    if (!out.effectPanel.present && out.effectInit.present) out.effectPanel = out.effectInit;
    MixerInit&  mx = out.mixerPanel;
    EffectInit& fx = out.effectPanel;
    const MixerInit*  ms = out.mixerInit.present  ? &out.mixerInit  : nullptr;
    const EffectInit* fs = out.effectInit.present ? &out.effectInit : nullptr;

    settle(fx.masterVolume, masterVolume / kMasterHeadroom, masterByte,
           fs ? masterByte(fs->masterVolume) : -1);
    const AutoTarget chTarget[kNumChannels] = { AutoTarget::Bass1, AutoTarget::Bass2,
                                                AutoTarget::Drum808, AutoTarget::Drum909 };
    for (int ch = 0; ch < kNumChannels; ++ch) {
        const MixerChannel& m = mix_[(std::size_t) ch];
        settle(mx.volume[ch], m.volume, channelByte, ms ? channelByte(ms->volume[ch]) : -1);
        settle(mx.pan[ch], m.pan, panByteFromPan, ms ? panByteFromPan(ms->pan[ch]) : -1);
        settle(fx.delaySend[ch], m.delaySend, knobByte, fs ? knobByte(fs->delaySend[ch]) : -1);
        settle(fx.distOn[ch], m.distEnable, boolByte, fs ? boolByte(fs->distOn[ch]) : -1);
        // The channel starts on or off where its instrument track's param 0
        // puts it at tick 0 (0 = muted), which is what play() applied.
        const int on0 = switch0(chTarget[ch], 0);
        settle(fx.channelOn[ch], !m.mute, boolByte,
               on0 >= 0 ? on0 : (fs ? boolByte(fs->channelOn[ch]) : -1));
    }
    settle(fx.compTarget, compTarget_, compTargetByte, fs ? compTargetByte(fs->compTarget) : -1);
    settle(fx.pcfTarget, pcfTarget(), pcfTargetByte, fs ? pcfTargetByte(fs->pcfTarget) : -1);

    // Effect knobs, inverting the mappings loadSong applies.
    float v = 0.0f;
    auto fxKnob = [&](const Processor* p, const char* name, float& panel, float EffectInit::* start) {
        if (read(p, name, v)) settle(panel, v, knobByte, fs ? knobByte(fs->*start) : -1);
    };
    const Processor* pcf = pcf_.get();
    fxKnob(pcf, "cutoff",    fx.pcfFreq,  &EffectInit::pcfFreq);
    fxKnob(pcf, "resonance", fx.pcfQ,     &EffectInit::pcfQ);
    fxKnob(pcf, "envmod",    fx.pcfAmt,   &EffectInit::pcfAmt);
    fxKnob(pcf, "decay",     fx.pcfDecay, &EffectInit::pcfDecay);
    if (read(pcf, "mode", v))                                       // 0 = bandpass
        settle(fx.pcfBandpass, v < 0.5f, boolByte, fs ? boolByte(fs->pcfBandpass) : -1);
    auto patternByte = [](int p) { return clampByte(p, 0, 54); };
    if (read(pcf, "pattern", v))
        settle(fx.pcfPattern, patternByte((int) std::lround(v * 54.0f)), patternByte,
               fs ? patternByte(fs->pcfPattern) : -1);
    // The delay's start is its track's tick-0 dump, which the loader reads
    // into the start state like the PCF's (review E3).
    auto stepsByte = [](int s) { return clampByte(s, 1, 16); };
    auto panByte   = [](int p) { return clampByte(p, 0, 127); };
    const Processor* dly = delay_.get();
    if (read(dly, "steps", v))
        settle(fx.delaySteps, stepsByte((int) std::lround(v * 16.0f)), stepsByte,
               fs ? stepsByte(fs->delaySteps) : -1);
    if (read(dly, "grid", v))
        settle(fx.delayStraight, v >= 0.5f, boolByte, fs ? boolByte(fs->delayStraight) : -1);
    fxKnob(dly, "feedback", fx.delayFeedback, &EffectInit::delayFeedback);
    if (read(dly, "pan", v))
        settle(fx.delayPan, panByte((int) std::lround(v * 127.0f)), panByte,
               fs ? panByte(fs->delayPan) : -1);
    fxKnob(dist_.get(), "amount",    fx.distAmount,    &EffectInit::distAmount);
    fxKnob(dist_.get(), "tone",      fx.distTone,      &EffectInit::distTone);
    fxKnob(comp_.get(), "ratio",     fx.compRatio,     &EffectInit::compRatio);
    fxKnob(comp_.get(), "threshold", fx.compThreshold, &EffectInit::compThreshold);
    // The unit switches have no chunk byte, so for a loaded song these do not
    // reach the file; a song built in Reborn takes its track dumps from them.
    settle(fx.distortionOn, distUnitOn_,  boolByte, fs ? boolByte(fs->distortionOn) : -1);
    settle(fx.pcfOn,        pcfOn_,       boolByte, fs ? boolByte(fs->pcfOn) : -1);
    settle(fx.compressorOn, compressorOn, boolByte, fs ? boolByte(fs->compressorOn) : -1);
    settle(fx.delayOn,      delayUnitOn_, boolByte, fs ? boolByte(fs->delayOn) : -1);
    fx.present = true;
    mx.present = true;

    // The 303 headers, by the names applyBassKnobs sets, header byte N being
    // the track's param N. "tuning" is already in semitones.
    auto captureBass = [&](const Instrument* inst, BassKnobs& k, AutoTarget t) {
        const int (&s0)[32] = start0[int(t)];
        auto bassKnob = [&](const char* name, float& panel, int param) {
            if (!read(inst, name, v)) return false;
            settle(panel, v, knobByte, knob0(t, param));
            return true;
        };
        bool any = false;
        if (read(inst, "tuning", v)) {
            any = true;
            settle(k.tuning, v, tuneByte,
                   s0[2] < 0 ? -1 : tuneByteFromSemitones(tuneSemitonesFromByte(s0[2])));
        }
        any |= bassKnob("cutoff",    k.cutoff,    3);
        any |= bassKnob("resonance", k.resonance, 4);
        any |= bassKnob("envmod",    k.envmod,    5);
        any |= bassKnob("decay",     k.decay,     6);
        any |= bassKnob("accent",    k.accent,    7);
        if (read(inst, "waveform", v)) {
            any = true;
            settle(k.waveform, v, [](float w) { return boolByte(w >= 0.5f); }, switch0(t, 8));
        }
        if (any) k.present = true;
    };
    captureBass(bass1_.get(), out.bass1Knobs, AutoTarget::Bass1);
    captureBass(bass2_.get(), out.bass2Knobs, AutoTarget::Bass2);

    // The drum headers, through the tables loadSong applies them with. The
    // 808's alternate-voice levels have no byte of their own: loadSong and the
    // automation give each the level of the column it shares, and an edit made
    // to one alone is not saved (docs/FORMAT_RBSX.md).
    auto captureDrum = [&](const Instrument* inst, DrumKnobs& k, AutoTarget t,
                           const char* const* levels, const int* levelParams,
                           const DrumKnobName* knobs, std::size_t n, bool snapRepurposed) {
        if (!inst) return;
        for (int lv = 0; lv < 11; ++lv)
            if (read(inst, levels[lv], v)) settle(k.level[lv], v, levelByte, knob0(t, levelParams[lv]));
        for (std::size_t i = 0; i < n; ++i) {
            if (snapRepurposed && !std::strcmp(knobs[i].name, "sd_snap")) continue;
            if (read(inst, knobs[i].name, v))
                settle(k.*knobs[i].field, v, knobByte, knob0(t, knobs[i].param));
        }
        k.present = true;
    };
    captureDrum(drum909_.get(), out.drum909Knobs, AutoTarget::Drum909, kLevelNames909,
                kLevelParam909, kKnobs909, sizeof kKnobs909 / sizeof kKnobs909[0],
                sdSnapRepurposed909);
    // One hi-hat fader drives both 909 hats (header byte 22), and the loader
    // reads both columns from it, so the closed hat's follows the open hat's.
    if (drum909_) out.drum909Knobs.level[8] = out.drum909Knobs.level[7];
    captureDrum(drum808_.get(), out.drum808Knobs, AutoTarget::Drum808, kLevelNames808,
                kLevelParam808, kKnobs808, sizeof kKnobs808 / sizeof kKnobs808[0],
                sdSnapRepurposed808);
    // The 909 flam and the 808 switches are engine state, not device params.
    settle(out.drum909Knobs.flam, flam909_, knobByte, knob0(AutoTarget::Drum909, 29));
    for (int c = 0; c < SC_COUNT; ++c)
        settle(out.drum808Knobs.sharedPrimary[c], shared808_[c], boolByte,
               switch0(AutoTarget::Drum808, kSwitchParam808[c]));

    // A user who never built an arrangement saves the patterns being heard.
    if (out.arrangement.empty()) {
        SongStep row;
        row.bass1Pattern   = activePattern(kBass1);
        row.bass2Pattern   = activePattern(kBass2);
        row.drum808Pattern = activePattern(kDrum808);
        row.drum909Pattern = activePattern(kDrum909);
        row.repeats = 1;
        out.arrangement.push_back(row);
    }
}

Song Engine::songForSave(const std::string& title, const std::string& author) const {
    Song s = song_;
    captureLivePanel(s);
    s.title  = title;
    s.author = author;
    return s;
}

// ReBirth's exclusivity rules, kept in one place so a loaded song and a click
// in the UI cannot disagree. The distortion is a per-section flag any number of
// sections may set (each channel group's MIXR +4); the PCF and the compressor
// are single selectors (MIXR +2 and +1), and setting one clears the others.
void Engine::setDistEnable(int ch, bool on) {
    if (ch >= 0 && ch < kNumChannels) mix_[ch].distEnable = on;
}

void Engine::setDistUnit(bool on) { distUnitOn_ = on; }
bool Engine::distUnit() const { return distUnitOn_; }

// ROUTING ONLY. The unit's own bypass is a separate control (`setPcfUnit`,
// PCF track param 0) and this used to set it too, which was harmless while
// routing was something only a click did. It is not: songs perform the routing,
// and a song that takes the PCF off a section and puts it back must not be able
// to switch the unit on behind the unit's own automation.
void Engine::setPcfTarget(int ch) {
    for (int c = 0; c < kNumChannels; ++c) mix_[c].pcfEnable = (c == ch);
}

void Engine::setPcfUnit(bool on) { pcfOn_ = on; }
bool Engine::pcfUnit() const { return pcfOn_; }

// The delay unit's bypass, DELY track param 0. Switching it OFF CLEARS the
// delay. Measured 2026-09-27 (fixtures-2026-09-27/delay-rides, dr_p0: steps 4,
// feedback 96, the unit off at bar 1 and on at bar 2): ReBirth plays no echo
// while it is off, and the first hit after it comes back echoes at -10.0 and
// -12.4 dB, exactly a fresh hit's, with nothing left from before the switch-off.
// So the ring is emptied here, once, as the switch-off is applied (from a ride
// that is on the audio thread). While the unit is off renderSlice skips the
// delay and its return, so input during the off period never reaches the ring,
// which matches ReBirth too, and switching on resumes from an empty ring.
void Engine::setDelayUnit(bool on) {
    if (delayUnitOn_ && !on && delay_) delay_->reset();
    delayUnitOn_ = on;
}

int Engine::pcfTarget() const {
    for (int c = 0; c < kNumChannels; ++c) if (mix_[c].pcfEnable) return c;
    return -1;
}

void Engine::setCompTarget(int t) {
    compTarget_ = (t == kCompMaster || (t >= 0 && t < kNumChannels)) ? t : -1;
}

void Engine::selectBassPattern(int dev, int pat) { (dev == 0 ? bass1Pat_ : bass2Pat_) = pat; }
void Engine::selectDrumPattern(int dev, int pat) { (dev == 0 ? drum808Pat_ : drum909Pat_) = pat; }
void Engine::selectPcfPattern(int pat) { pcfPat_ = pat; }

Instrument* Engine::instrument(int id) {
    switch (id) {
        case kBass1:   return bass1_.get();
        case kBass2:   return bass2_.get();
        case kDrum808: return drum808_.get();
        case kDrum909: return drum909_.get();
        default:       return nullptr;
    }
}

std::unique_ptr<Instrument> Engine::setInstrument(int id, std::unique_ptr<Instrument> inst) {
    std::unique_ptr<Instrument>* slot = nullptr;
    switch (id) {
        case kBass1:   slot = &bass1_;   break;
        case kBass2:   slot = &bass2_;   break;
        case kDrum808: slot = &drum808_; break;
        case kDrum909: slot = &drum909_; break;
        default:       return inst;   // invalid id: hand it straight back
    }
    // Never install a NULL voice. Swapping one in would leave the slot empty, and
    // the sequencer's fire path (fireBass -> applyBassEvent) has no null guard the
    // way renderSlice does, so the next played step would dereference it. Hand the
    // null straight back and keep the current voice. makeBass303(HostedPlugin)
    // returns null by contract, so this guards the documented hosted-plugin flow.
    if (!inst) return inst;
    // Ready the incoming voice for the current rate before it goes live. (For a
    // heavy voice the caller should have done the slow build off-thread; prepare
    // on a native engine is cheap.)
    {
        inst->prepare(sampleRate_);
        inst->reset();
        // Match the loaded patch: a bass voice takes this slot's decoded knobs.
        if (id == kBass1)      applyBassKnobs(inst.get(), song_.bass1Knobs);
        else if (id == kBass2) applyBassKnobs(inst.get(), song_.bass2Knobs);
    }
    // A fresh voice has nothing gliding into it: clear this bass slot's legato
    // memory so the first step strikes rather than trying to slide from silence.
    if (id == kBass1) { bass1PrevSlide_ = false; bass1PrevSounding_ = false; }
    else if (id == kBass2) { bass2PrevSlide_ = false; bass2PrevSounding_ = false; }
    std::swap(*slot, inst);   // `inst` now holds the displaced voice
    return inst;              // caller destroys it off the audio thread
}

double Engine::samplesPerStep(int /*stepIndex*/) const {
    // A step is a 16th note; the master clock runs STRAIGHT. Shuffle is applied
    // per device by delaying odd-step triggers (see fireStep), matching the
    // original's per-pattern shuffle flags.
    return sampleRate_ * 15.0 / tempo_;
}

double Engine::swingDelaySamples() const {
    // shuffle_ is the global shuffle AMOUNT (0..1, 0.5 = factory default).
    // Delay the off-16ths of shuffled patterns by up to half a step
    // (amount 1.0 -> 75% swing; 0.5 -> a classic ~62.5% swing).
    return shuffle_ * 0.5 * samplesPerStep(step_);
}

void Engine::advanceStep() {
    step_++;
    songTicks_ += 2;                        // a step is two 1/32-note ticks
    if (step_ >= kStepsPerBar) {
        step_ = 0;
        if (songMode_ && !song_.arrangement.empty()) {
            // Editing the arrangement live (the UI can delete rows during song
            // playback) may leave songRow_ past the end; clamp before indexing.
            if (songRow_ >= (int) song_.arrangement.size()) { songRow_ = 0; songRepeat_ = 0; }
            songRepeat_++;
            const SongStep& cur = song_.arrangement[songRow_];
            if (songRepeat_ >= std::max(1, cur.repeats)) {
                songRepeat_ = 0;
                songRow_ = (songRow_ + 1) % (int)song_.arrangement.size();
                if (songRow_ == 0) {        // looped back to the top
                    // Only the instrument tracks' tick-0 dumps come back here
                    // (autoIdx_ = 0); the mixer and effect start state does
                    // not, deliberately, so lap 2 keeps the last rides. ReBirth
                    // does not wrap at the song's end at all (review E1,
                    // measured), so no lap 2 has anything to match, and what a
                    // Loop should restore is PLAN_TRANSPORT_MODES Stage B's
                    // question (L2). So no applySongStart here.
                    songTicks_ = 0;
                    autoIdx_ = 0;
                }
            }
            const SongStep& r = song_.arrangement[songRow_];
            bass1Pat_ = r.bass1Pattern; bass2Pat_ = r.bass2Pattern;
            drum808Pat_ = r.drum808Pattern; drum909Pat_ = r.drum909Pattern; pcfPat_ = r.pcfPattern;
        }
    }
    fireStep();
}

// ---------------------------------------------------------------------------
// Drum knob automation
//
// A drum TRAK's param index IS its header byte index. Proof, on every one of
// the 16 shipped songs and both machines: each control whose knob was never
// touched after the automation was recorded has a tick-0 event whose value
// equals its header byte exactly. So these tables and decode808Knobs() /
// decode909Knobs() in rbs.cpp describe the same layout and must stay in step.
//
// The tick-0 dump is the song's STARTING state; the header is the panel at
// SAVE time. Where the two disagree the header is simply the wrong number to
// play, and until 2026-08-26 the header was all we loaded. Trakka Rekka's 909
// snare is the extreme case: header 127, song start 0, and 54 rides after it,
// which is why our render opened with a loud snare that ReBirth never plays.
// ---------------------------------------------------------------------------

// Params handled outside these tables: 0 (mute) and 1 (pattern select) on both
// machines, 20 (the 909's ONE hi-hat level, which drives both hats), 29 (the
// 909 flam amount, which is not a device parameter), and the 808's five
// shared-column switches.
static const char* drum909ParamName(unsigned param) {
    switch (param) {
        case  2: return "accent";
        case  3: return "level_Bass Drum";
        case  4: return "bd_tune";
        case  5: return "bd_attack";
        case  6: return "bd_decay";
        case  7: return "level_Snare";
        case  8: return "sd_tune";
        case  9: return "sd_tone";
        case 10: return "sd_snap";
        case 11: return "level_Low Tom";
        case 12: return "lt_tune";
        case 13: return "lt_decay";
        case 14: return "level_Mid Tom";
        case 15: return "mt_tune";
        case 16: return "mt_decay";
        case 17: return "level_Hi Tom";
        case 18: return "ht_tune";
        case 19: return "ht_decay";
        // Appendix C lists these as Hi-hat 83, RS 84, CP 85, which would make
        // them params 20, 21, 22. The FILE disagrees: decode909Knobs() reads
        // byte 20 as RS, 21 as CP and 22 as the one shared hi-hat level, and
        // rbsgen writes them in that order too. Measurement wins: building
        // this table from the MIDI map instead silenced the hats, clap and rim
        // outright, which score909.py caught as a 63 to 74 dB level offset.
        case 20: return "level_Rim";
        case 21: return "level_Clap";
        case 23: return "ch_decay";
        case 24: return "oh_decay";
        case 25: return "level_Crash";
        case 26: return "cym_tune";
        case 27: return "level_Ride";
        case 28: return "rd_tune";
        default: return nullptr;
    }
}

static const char* drum808ParamName(unsigned param) {
    switch (param) {
        case  2: return "accent";
        case  3: return "level_Bass Drum";
        case  4: return "bd_tune";        // the panel calls it TONE (CC 39)
        case  5: return "bd_decay";
        case  6: return "level_Snare";
        case  7: return "sd_tone";
        case  8: return "sd_snap";
        case  9: return "level_Low Tom";
        case 10: return "lt_tune";
        case 12: return "level_Mid Tom";
        case 13: return "mt_tune";
        case 15: return "level_Hi Tom";
        case 16: return "ht_tune";
        case 18: return "level_Rim Shot";
        case 20: return "level_Hand Clap";
        case 22: return "level_Cowbell";
        case 23: return "level_Cymbal";
        case 24: return "cy_tune";
        case 25: return "cy_decay";
        case 26: return "level_Open Hat";
        case 27: return "oh_decay";
        case 28: return "level_Closed Hat";
        default: return nullptr;
    }
}

// A shared 808 column has ONE level fader driving either of its two voices, so
// a level ride has to reach the alternate as well. Same rule the header path
// applies at load; without it a conga column ignores its own fader ride.
static const char* drum808AltLevelName(unsigned param) {
    switch (param) {
        case  9: return "level_Low Conga";
        case 12: return "level_Mid Conga";
        case 15: return "level_Hi Conga";
        case 18: return "level_Claves";
        case 20: return "level_Maracas";
        default: return nullptr;
    }
}

void Engine::applyAutoEvent(const AutoEvent& e) {
    auto bassKnob = [this](Instrument* inst, int channelId, uint8_t param, uint8_t value) {
        if (!inst) return;
        float v = float(value) / 127.0f;
        switch (param) {
            case 0: mix_[channelId].mute = (value == 0); break;   // mute toggle
            // TUNE is a quantised 32-slot table with a wide centre detent, not
            // a continuous law. This is the SECOND site that decodes it: the
            // header knobs go through rbs.cpp, but a song's tick-0 automation
            // lands here and overrides them, so both must agree.
            case 2: inst->setParamByName(
                        "tuning", (float) tuneSemitonesFromByte((int) value)); break;
            case 3: inst->setParamByName("cutoff", v); break;
            case 4: inst->setParamByName("resonance", v); break;
            case 5: inst->setParamByName("envmod", v); break;
            case 6: inst->setParamByName("decay", v); break;
            case 7: inst->setParamByName("accent", v); break;
            case 8: inst->setParamByName("waveform", value ? 1.0f : 0.0f); break;
            default: break;
        }
    };
    switch (e.target) {
        case AutoTarget::Bass1: bassKnob(bass1_.get(), kBass1, e.param, e.value); break;
        case AutoTarget::Bass2: bassKnob(bass2_.get(), kBass2, e.param, e.value); break;
        // Param 0 is a MUTE and it mutes when ZERO. Polarity CONFIRMED
        // 2026-07-26 against the effects-off exports: inverting it silences
        // G-Spot entirely (-104 dBFS) and Fonda Workout (-57.8), and makes
        // Trakka Rekka's spectral match worse. Do not "fix" this.
        //
        // A consequence that looks like a bug but is not: Calm's 808 track sets
        // param 0 to 0 at tick 0 and to 1 only at tick 4992 (bar 156, past the
        // exported length), so its 808 is legitimately silent for the whole
        // piece. Our remaining 18 dB deficit in that song's kick band therefore
        // comes from somewhere else; see docs/PARITY.md.
        case AutoTarget::Drum808: {
            if (e.param == 0) { mix_[kDrum808].mute = (e.value == 0); break; }
            if (!drum808_) break;
            const float v = float(e.value) / 127.0f;
            // The five shared-column switches pick which voice the column
            // plays. Non-zero selects the primary (tom / rimshot / clap), the
            // same polarity decode808Knobs() reads from the header.
            switch (e.param) {
                case 11: shared808_[SC_LT] = (e.value != 0); return;
                case 14: shared808_[SC_MT] = (e.value != 0); return;
                case 17: shared808_[SC_HT] = (e.value != 0); return;
                case 19: shared808_[SC_RS] = (e.value != 0); return;
                case 21: shared808_[SC_CP] = (e.value != 0); return;
                default: break;
            }
            if (const char* n = drum808ParamName(e.param)) drum808_->setParamByName(n, v);
            if (const char* a = drum808AltLevelName(e.param)) drum808_->setParamByName(a, v);
            break;
        }
        case AutoTarget::Drum909: {
            if (e.param == 0) { mix_[kDrum909].mute = (e.value == 0); break; }
            if (!drum909_) break;
            const float v = float(e.value) / 127.0f;
            // One hi-hat fader drives both hats, and the flam amount is a
            // sequencer control rather than a voice parameter, so it rides on
            // the engine's own copy, which the step scheduler reads.
            if (e.param == 22) {
                drum909_->setParamByName("level_Open Hat", v);
                drum909_->setParamByName("level_Closed Hat", v);
                break;
            }
            if (e.param == 29) { flam909_ = v; break; }
            if (const char* n = drum909ParamName(e.param)) drum909_->setParamByName(n, v);
            break;
        }
        case AutoTarget::Pcf:
            // Param N is PCF chunk byte N; param 0 is the unit's own on/off,
            // which gates the routing rather than any knob.
            if (pcf_) {
                switch (e.param) {
                case 0: pcfOn_ = (e.value != 0); break;
                case 1: pcf_->setParamByName("cutoff",    float(e.value) / 127.0f); break;
                case 2: pcf_->setParamByName("resonance", float(e.value) / 127.0f); break;
                case 3: pcf_->setParamByName("envmod",    float(e.value) / 127.0f); break;
                case 4: pcf_->setParamByName("pattern",
                            float(e.value > 54 ? 54 : e.value) / 54.0f); break;
                case 5: pcf_->setParamByName("decay",     float(e.value) / 127.0f); break;
                case 6: pcf_->setParamByName("mode",      e.value ? 0.0f : 1.0f); break;
                default: break;
                }
            }
            break;
        case AutoTarget::Dist:
            // DIST track: param 0 = the unit's own on/off, 1 = Shape, 2 =
            // Amount. The four per-section instances mirror `dist_`'s params at
            // the top of every renderSlice, so setting `dist_` here reaches all
            // of them. Songs ride these hard (Mutilated Species II: 1241 Amount
            // rides), and several start at Amount 0 and ride it up.
            switch (e.param) {
            case 0: distUnitOn_ = (e.value != 0); break;
            case 1: if (dist_) dist_->setParamByName("tone",   float(e.value) / 127.0f); break;
            case 2: if (dist_) dist_->setParamByName("amount", float(e.value) / 127.0f); break;
            default: break;
            }
            break;
        case AutoTarget::Mixer:
            // Params 1 and 2 are the compressor's and the PCF's routing, and
            // songs PERFORM them: the same encoding as the MIXR chunk's +1 and
            // +2, arriving as timed events. Routed through the same setters as
            // a click so the exclusivity rules cannot be bypassed by a song.
            if (e.param == 1) {
                setCompTarget(e.value == 1 ? kCompMaster
                              : (e.value >= 2 && e.value <= 5) ? int(e.value) - 2 : -1);
                break;
            }
            if (e.param == 2) {
                setPcfTarget((e.value >= 1 && e.value <= 4) ? int(e.value) - 1 : -1);
                break;
            }
            for (int ch = 0; ch < kNumChannels; ++ch) {
                int base = 6 + 6 * ch;
                if (e.param == base)     mix_[ch].volume = faderGain(e.value, kChannelUnityGain);
                if (e.param == base + 1) mix_[ch].pan = panFromByte(e.value);
                if (e.param == base + 2 && !disableSends) mix_[ch].delaySend = float(e.value) / 127.0f;  // dub rides
                // base+3 is this section's DISTORTION switch, and songs perform
                // it exactly as they perform the PCF's routing: twelve of the
                // sixteen demo songs move it after tick 0, 360 events in all,
                // 279 of them in Mutilated Species II alone. Held flat, every
                // one of those songs distorts whatever it distorts at the
                // downbeat for its entire length.
                if (e.param == base + 3) setDistEnable(ch, e.value != 0);
            }
            break;
        case AutoTarget::Delay:
            // DELY track: param N is DELY chunk byte N and param 0 the unit's
            // own on/off, which gates the send bus rather than any knob. In
            // Song mode ReBirth plays this track, not the chunk (review E3,
            // measured 2026-09-26), and songs perform it: 4435 rides across the
            // demo songs, 2311 in G-Spot alone, all on params 1 to 4.
            switch (e.param) {
            case 0: setDelayUnit(e.value != 0); break;   // off clears the delay
            case 1:
                if (delay_) delay_->setParamByName("steps",
                                float(e.value < 1 ? 1 : (e.value > 16 ? 16 : e.value)) / 16.0f);
                break;
            case 2: if (delay_) delay_->setParamByName("grid", e.value ? 1.0f : 0.0f); break;
            case 3: if (delay_) delay_->setParamByName("feedback", float(e.value) / 127.0f); break;
            case 4: if (delay_) delay_->setParamByName("pan",      float(e.value) / 127.0f); break;
            default: break;
            }
            break;
        case AutoTarget::Comp:
            // COMP track: param 0 = the unit's own on/off, 1 = Ratio, 2 =
            // Threshold (byte/127), 3 = the level meter, which is display only
            // and is ignored. ReBirth PLAYS the rides (measured 2026-10-01,
            // fixtures-2026-10-01/comp-rides cr_*): a ratio or threshold ride
            // at tick 64 lands within 0.03 dB of the static export at its end
            // setting. The demo songs carry 156 of them, 65 on Ratio and 91 on
            // Threshold, none on params 0 or 3.
            switch (e.param) {
            case 0: compressorOn = (e.value != 0); break;
            case 1: if (comp_) comp_->setParamByName("ratio",     float(e.value) / 127.0f); break;
            case 2: if (comp_) comp_->setParamByName("threshold", float(e.value) / 127.0f); break;
            default: break;
            }
            break;
    }
}

// Runs from fireStep, once per 16th step, while the file times events in 1/32
// ticks, so an event on an odd tick is applied at the next even one, up to
// 62.5 ms late at 120 BPM, and every change is a step. ReBirth applies a ride
// at its tick and glides the control over roughly 100 ms (measured 2026-09-27
// on a DELY pan ride). An engine-wide parity change for sign-off: BACKLOG 18.
void Engine::dispatchAutomation() {
    if (!songMode_) return;
    const std::vector<AutoEvent>& ev = song_.automation;
    while (autoIdx_ < ev.size() && ev[autoIdx_].tick <= songTicks_)
        applyAutoEvent(ev[autoIdx_++]);
}

// The decision this used to inline now lives in reborn/step_events.h, with the
// measurements that produced it, so that it can be unit-tested without
// rendering audio. Three shipped defects lived in those four lines and all
// three were found by ear; they are regression tests now.
// Read once. Both default OFF, so an unconfigured build is byte-identical to
// what shipped. See BassRules in step_events.h for what each one measured.
static const BassRules& bassRules() {
    static const BassRules r = []{
        BassRules b;
        const char* a = std::getenv("REBORN_SLIDE_NEEDS_VOICE");
        const char* c = std::getenv("REBORN_REST_ACCENT");
        b.slideNeedsVoice   = a && std::atof(a) != 0.0;
        b.accentOnPlainRest = c && std::atof(c) != 0.0;
        return b;
    }();
    return r;
}

// task 75: dump decideBassStep's actual per-step verdict, at the real call
// site, so the trace can never diverge from what the render actually did.
// REBORN_TRACE_BASS=1 traces bass1, =2 traces bass2, =3 both. Off by default
// and printed to stderr, so it never touches the rendered audio.
static bool traceEnabled(int channelTag) {
    static const int mask = []{
        const char* e = std::getenv("REBORN_TRACE_BASS");
        return e ? std::atoi(e) : 0;
    }();
    return (mask & channelTag) != 0;
}

static void fireBass(Instrument* inst, BassPattern& p, int step, bool& prevSlide,
                     bool& prevSounding, double stepSeconds,
                     int channelTag = 0, int globalStep = -1) {
    const int idx = step % std::max(1, p.length);
    const BassStep& st = p.steps[(std::size_t) idx];
    const BassEvent ev = decideBassStep(st, p.absoluteNote(idx), prevSlide,
                                        prevSounding, bassRules());
    if (channelTag && traceEnabled(channelTag)) {
        const int g = globalStep >= 0 ? globalStep : step;
        const char* kindName = ev.kind == BassEvent::Kind::Strike ? "Strike"
                              : ev.kind == BassEvent::Kind::Hold   ? "Hold"
                                                                    : "Release";
        // bar/step are 1-based to match onset_ab.py's printed columns
        // (i//16+1, i%16+1) so the two can be read side by side.
        std::fprintf(stderr,
            "TRACE bass%d step=%3d bar=%2d step_in_bar=%2d pat_idx=%2d | "
            "raw gate=%d slide=%d accent=%d note=%2d oct=%+d | "
            "decided kind=%-7s tie=%d sounding=%d accent=%d slideIn=%d\n",
            channelTag, g, g / 16 + 1, g % 16 + 1, idx,
            st.gate ? 1 : 0, st.slide ? 1 : 0, st.accent ? 1 : 0,
            st.note, st.octave,
            kindName, ev.tie ? 1 : 0, ev.sounding ? 1 : 0,
            ev.accent ? 1 : 0, ev.slideIn ? 1 : 0);
    }
    applyBassEvent(inst, ev, stepSeconds);
    prevSlide    = ev.tie;
    prevSounding = ev.sounding;
}

// The 808's five shared columns, as {primary voice, alternate voice} pairs in
// SharedColumn808 order. Patterns always store the PRIMARY voice index (that is
// what the file's column means), and the song's switches decide which of the
// pair actually sounds, so pattern data stays byte-faithful to the file and
// only playback is rerouted.
struct SharedPair { int primary, alternate; };
static const SharedPair kShared808[SC_COUNT] = {
    { 2, 5 },   // SC_LT: Low Tom  <-> Low Conga
    { 3, 6 },   // SC_MT: Mid Tom  <-> Mid Conga
    { 4, 7 },   // SC_HT: Hi Tom   <-> Hi Conga
    { 8, 15 },  // SC_RS: Rim Shot <-> Claves
    { 9, 10 },  // SC_CP: Hand Clap<-> Maracas
};

static void fireDrum(Instrument* inst, DrumPattern& p, int step,
                     const bool* sharedPrimary /* null = no remapping */,
                     uint32_t* flamOut = nullptr, uint32_t* flamAccOut = nullptr) {
    int idx = step % std::max(1, p.length);
    const DrumStep& st = p.steps[idx];
    for (int v = 0; v < kNumDrumVoices; ++v) {
        if (st.voiceOn(v)) {
            int voice = v;
            if (sharedPrimary) {
                for (int c = 0; c < SC_COUNT; ++c)
                    if (kShared808[c].primary == v) {
                        if (!sharedPrimary[c]) voice = kShared808[c].alternate;
                        break;
                    }
            }
            // The 909's cell value carries a SOFT level as well as the flam
            // flag: measured 2026-08-25, a soft cell sits 7.96 dB under a normal
            // one (a linear 0.40). The 808 has no soft cells. Accent is separate,
            // from column 0, and each device applies its own measured depth.
            // A FLAM cell also plays quieter. ReBirth's flam cell sits 1.66 dB
            // UNDER a normal hit even though it fires twice; with the second
            // trigger restarting the voice the two hits sum to about 1.68x a
            // single one at full velocity, so matching -1.66 dB needs each hit at
            // roughly 0.64. Measured 2026-08-25 against the isolated cell fixtures.
            const float vel = st.voiceSoft(v) ? 0.40f
                            : st.voiceFlam(v) ? 0.637f : 1.0f;
            inst->trigger(voice, vel, st.voiceAccent(v), false);
            // A flam step fires this voice again later; the engine schedules it.
            if (flamOut && st.voiceFlam(v)) {
                *flamOut |= (1u << voice);
                if (st.voiceAccent(v) && flamAccOut) *flamAccOut |= (1u << voice);
            }
        }
    }
}

void Engine::fireDevice(int device) {
    // One 16th-note step, in seconds. Shuffle moves WHEN a step fires, not how
    // long the step nominally is, so the gate keeps its straight-eighth length.
    const double stepSeconds = 15.0 / tempo_;
    // songTicks_ is two 1/32-note ticks per step and is monotonic since the
    // start of the current pass through the arrangement (see advanceStep),
    // which is exactly the linear step index onset_ab.py reports against.
    const int globalStep = (int)(songTicks_ / 2);
    switch (device) {
        case kBass1:   fireBass(bass1_.get(), song_.bass1.at(bass1Pat_), step_,
                                bass1PrevSlide_, bass1PrevSounding_, stepSeconds,
                                1, globalStep); break;
        case kBass2:   fireBass(bass2_.get(), song_.bass2.at(bass2Pat_), step_,
                                bass2PrevSlide_, bass2PrevSounding_, stepSeconds,
                                2, globalStep); break;
        case kDrum808: fireDrum(drum808_.get(), song_.drum808.at(drum808Pat_), step_,
                                shared808_); break;
        case kDrum909: {
            // The 909 has no shared columns, but it does have FLAM. Measured
            // 2026-08-25: a flam step fires the voice twice and the knob sets the
            // GAP, linearly from coincident to one 32nd note (64.5 ms at knob 127
            // against a 32nd's 62.5 ms). Because this is a sample player, a second
            // trigger restarts the voice, which is why a zero gap sounds like one
            // hit rather than a doubled one.
            // Column 0's VALUE scales the accent depth, and a 909 always
            // writes 2 there, so 2 is the scale of 1.0 the depth constant was
            // measured at. Anything else (an externally authored file) scales
            // proportionally; an unaccented step never reads this.
            DrumPattern& p909 = song_.drum909.at(drum909Pat_);
            const DrumStep& s909 = p909.steps[step_ % std::max(1, p909.length)];
            drum909_->setParamByName("accent_cell",
                                     s909.accentValue ? float(s909.accentValue) * 0.5f : 1.0f);
            uint32_t fv = 0, fa = 0;
            fireDrum(drum909_.get(), p909, step_, nullptr, &fv, &fa);
            if (fv) {
                const double thirtySecond = stepSeconds * 0.5 * sampleRate_;
                double gap = (double) flam909_ * thirtySecond;
                if (gap >= 1.0) { flamAt_ = gap; flamVoices_ = fv; flamAccent_ = fa; }
            }
            break;
        }
        default: break;
    }
}

void Engine::fireStep() {
    dispatchAutomation();

    // Per-pattern shuffle: a device whose ACTIVE pattern is flagged fires its
    // odd (off-beat) steps late by the global shuffle amount; everything else
    // fires on the straight grid now. Delayed fires are handled in process().
    const bool odd = (step_ & 1) != 0;
    const double delay = swingDelaySamples();
    const bool shuffled[4] = {
        song_.bass1.at(bass1Pat_).shuffle,
        song_.bass2.at(bass2Pat_).shuffle,
        song_.drum808.at(drum808Pat_).shuffle,
        song_.drum909.at(drum909Pat_).shuffle,
    };
    for (int d = 0; d < 4; ++d) {
        if (odd && shuffled[d] && delay >= 1.0) {
            pendingAt_[d] = delay;
        } else {
            pendingAt_[d] = -1.0;
            fireDevice(d);
        }
    }

    // Drive the pattern-controlled filter. Its 55 patterns are ReBirth presets
    // and cannot be edited, so the unit owns them and only needs the step; it
    // keeps its own thirty-second clock because several patterns trigger on
    // both halves of a sixteenth.
    // FREE-RUNNING sixteenth index, not the bar-local one. `step_` wraps at 16,
    // so passing it restarted the PCF's pattern at every bar line and every
    // preset whose length is not a divisor of 32 played wrong: pattern 51 is 18
    // thirty-seconds and repeated its first nine, where ReBirth carries on
    // across the bar. Twenty of the 55 presets have such a length.
    if (pcf_) pcf_->onStep(static_cast<int>(songTicks_ / 2));
}

void Engine::renderSlice(StereoBlock& out, int startFrame, int n) {
    for (int i = 0; i < n; ++i) {
        delayBus_.left[i]= delayBus_.right[i]= 0.0f;
        chScratch_.left[i] = chScratch_.right[i] = 0.0f;
        masterBus_.left[i] = masterBus_.right[i] = 0.0f;
    }

    // The three inserts share one set of knobs but NOT one signal path. Keep
    // the per-section distortions in step with the one the UI edits.
    if (dist_) {
        for (auto& d : distExtra_) {
            if (!d) continue;
            for (int p = 0, np = dist_->numParams(); p < np; ++p)
                d->setParam(p, dist_->getParam(p));
        }
    }

    for (int c = 0; c < kNumChannels; ++c) {
        Instrument* inst = instrument(c);
        if (!inst) continue;
        if (soloMask && !(soloMask & (1 << c))) continue;
        inst->process(chScratch_, n);
        const MixerChannel& mc = mix_[c];
        const float v = mc.mute ? 0.0f : mc.volume;

        // Post-fader peak for the UI meter, with a time-based release so the
        // reading is independent of the host's block size.
        {
            float pk = 0.0f;
            for (int i = 0; i < n; ++i) {
                const float a = std::fabs(chScratch_.left[i]);
                if (a > pk) pk = a;
            }
            pk *= v;
            const float rel = std::exp(-float(n) / float(0.15 * sampleRate_));
            float prev = chPeak_[(std::size_t) c].load(std::memory_order_relaxed) * rel;
            chPeak_[(std::size_t) c].store(pk > prev ? pk : prev, std::memory_order_relaxed);
        }

        // With the compressor on this section, the fader comes after it (see
        // below), so a section at fader 0 or muted must still feed the
        // compressor. It used to run on zeros there, its gain released to full
        // make-up (+15 dB at ratio 127, threshold 0), and the section came back
        // from mute or fader 0 as a full-scale burst that fader byte 1 never
        // gave. So the chain runs at vRun and the output is silenced after the
        // compressor. Muted, vRun is the fader the section would play at, so
        // the chain is the one it would be unmuted; at fader 0 it is byte 1,
        // the nearest fader that sounds. Where ReBirth's MUTE sits relative to
        // the section compressor is UNMEASURED; after it, like the fader, is
        // assumed. For v > 0, vRun == v and nothing changes.
        const bool secComp = (compTarget_ == c && comp_ && compressorOn);
        float vRun = v;
        if (secComp && vRun <= 0.0f)
            vRun = mc.volume > 0.0f ? mc.volume : faderGain(1, kChannelUnityGain);

        for (int i = 0; i < n; ++i) { chScratch_.left[i] *= vRun; chScratch_.right[i] *= vRun; }

        // One section, one chain, in the manual's order: distortion, then the
        // PCF (p64 says explicitly that the PCF comes after), then the
        // compressor, which is last on the mixer strip. Measured 2026-08-28:
        // with both sections distorted and the PCF shut on one of them, ReBirth
        // takes 15.2 dB off that section and 0.01 dB off the other, so an
        // insert acts on its own section and not on a shared bus.
        if (mc.distEnable && distUnitOn_) {
            if (Effect* d = distortionFor(c)) d->process(chScratch_, n);
        }
        if (mc.pcfEnable && pcf_ && pcfOn_) pcf_->process(chScratch_, n);
        // The section compressor comes BEFORE the channel fader, and sees the
        // section at the level a fader of 127 gives it, as ONE channel of the
        // L+R detector. Measured 2026-10-01 (fixtures-2026-10-01/comp-rides,
        // st_): between fader 127 and 90 ReBirth's compressed output falls by
        // the full 13.69 dB the uncompressed one does (r127_t000 -26.47 ->
        // -40.16, r127_t127 -18.29 -> -31.95), so the fader follows it; and
        // every section cell at fader 127 equals the master compressor's
        // HARD-LEFT cell (-26.47 / -20.61 / -17.14 at threshold 0 / 32 / 64),
        // i.e. the detector sees the mono section at the output level in one
        // channel only, 6.02 dB below a centred master. Ours ran it after the
        // fader, at the bus level: 12.54 dB low at r127_t000 v127. The fader
        // vRun is already in chScratch_, so k takes it out and puts in fader
        // 127, the centre pan, the master headroom and the detector's 0.5, and
        // 1 / k undoes it after (so ratio 0, an exact bypass in the DSP, can
        // move a sample by one LSB here). That scale is a FIT to the st_
        // cells, not a measured mechanism. At v == 0 (fader 0 or mute) the
        // output is then silenced. Where the fader sits relative to the
        // DISTORTION is unmeasured (BACKLOG 21): with the distortion on this
        // section the compressor sees dist(x * v) / v, which matches no single
        // chain order. The PCF is linear, so it commutes with the fader.
        if (secComp) {
            const float k = faderGain(127, kChannelUnityGain) * kPanUnityGain
                          * kMasterHeadroom * 0.5f / vRun;
            for (int i = 0; i < n; ++i) { chScratch_.left[i] *= k; chScratch_.right[i] *= k; }
            comp_->process(chScratch_, n);
            const float ik = 1.0f / k;
            for (int i = 0; i < n; ++i) { chScratch_.left[i] *= ik; chScratch_.right[i] *= ik; }
            if (v <= 0.0f)
                for (int i = 0; i < n; ++i) chScratch_.left[i] = chScratch_.right[i] = 0.0f;
        }

        // The echo send taps POST-insert: channel, inserts, send, then fader.
        // The send level is the exponential mixer fader on the send byte, not a
        // linear byte/127 (measured 2026-09-10; delaySendGain in pattern.h).
        if (mc.delaySend > 0.0001f) {
            const float sgain = delaySendGain(mc.delaySend);
            for (int i = 0; i < n; ++i) {
                delayBus_.left[i]  += chScratch_.left[i]  * sgain;
                delayBus_.right[i] += chScratch_.right[i] * sgain;
            }
        }

        float gl, gr; panGains(mc.pan, gl, gr);
        for (int i = 0; i < n; ++i) {
            masterBus_.left[i]  += chScratch_.left[i]  * gl;
            masterBus_.right[i] += chScratch_.right[i] * gr;
        }
    }

    // The delay runs, and its return reaches the master, only while the unit
    // is on (DELY track param 0). Off, both are skipped. setDelayUnit emptied
    // the ring as the unit went off, so nothing sent while it is off echoes
    // later, and it comes back on empty: what ReBirth does, measured 2026-09-27
    // (dr_p0; see setDelayUnit). No demo song toggles param 0.
    if (delay_ && delayUnitOn_) {
        delay_->process(delayBus_, n);
        for (int i = 0; i < n; ++i) {
            masterBus_.left[i]  += delayBus_.left[i];
            masterBus_.right[i] += delayBus_.right[i];
        }
    }
    // The compressor sits on ONE place: a section, handled in the loop above,
    // or the master bus. Confirmed exclusive by clicking the master strip's
    // own COMP button, which replaced the section value.
    //
    // The master compressor sees the OUTPUT level, and the master fader comes
    // after it. Measured 2026-10-01 (fixtures-2026-10-01/comp-rides, mt_): with
    // ratio 127 compressing, moving the master byte from 70 to 115 moves
    // ReBirth's output by the fader's full 16.65 dB at threshold 0 and at 127,
    // exactly as it does with the compressor off. So the bus is scaled by the
    // fixed headroom before the compressor and only the fader's own gain is
    // applied after it. It used to see the bus 2.85 dB hotter than the output
    // (the headroom was applied after it), which put the ratio-127 cr_ref
    // cells 2.99 dB below what comp_wav, feeding the DSP alone our drone at
    // the output level, gives for them. With the compressor off the expression
    // is the one it always was, so every comp-off render is byte-identical.
    float outGain = masterVolume;
    if (compTarget_ == kCompMaster && comp_ && compressorOn) {
        for (int i = 0; i < n; ++i) {
            masterBus_.left[i]  *= kMasterHeadroom;
            masterBus_.right[i] *= kMasterHeadroom;
        }
        comp_->process(masterBus_, n);
        outGain = masterVolume / kMasterHeadroom;
    }

    float mpk = 0.0f;                       // post-master peak for the UI

    for (int i = 0; i < n; ++i) {
        out.left[startFrame + i]  = flushDenormal(hardClipUnity(masterBus_.left[i]  * outGain));
        out.right[startFrame + i] = flushDenormal(hardClipUnity(masterBus_.right[i] * outGain));
        const float a = std::fabs(out.left[startFrame + i]);
        if (a > mpk) mpk = a;
    }
    {
        const float rel = std::exp(-float(n) / float(0.15 * sampleRate_));
        float prev = mPeak_.load(std::memory_order_relaxed) * rel;
        mPeak_.store(mpk > prev ? mpk : prev, std::memory_order_relaxed);
    }
}

void Engine::process(StereoBlock& out, int numFrames) {
    if ((int)out.size() < numFrames) out.resize(numFrames);
    // Ensure scratch capacity.
    if ((int)chScratch_.size() < numFrames) {
        chScratch_.resize(numFrames); delayBus_.resize(numFrames);
        masterBus_.resize(numFrames);
    }

    int done = 0;
    while (done < numFrames) {
        int n = numFrames - done;
        if (playing_) {
            // Slice at the next event: the step boundary, or the earliest
            // pending (shuffle-delayed) trigger, whichever comes first.
            double remain = (double)samplesPerStep(step_) - sampleInStep_;
            int until = (int)std::ceil(remain);
            for (int d = 0; d < 4; ++d) {
                if (pendingAt_[d] >= 0.0) {
                    int u = (int)std::ceil(pendingAt_[d] - sampleInStep_);
                    if (u < until) until = u;
                }
            }
            if (flamAt_ >= 0.0) {
                int u = (int)std::ceil(flamAt_ - sampleInStep_);
                if (u < until) until = u;
            }
            if (until < 1) until = 1;
            if (until < n) n = until;
        }
        renderSlice(out, done, n);
        if (playing_) {
            sampleInStep_ += n;
            for (int d = 0; d < 4; ++d) {
                if (pendingAt_[d] >= 0.0 && sampleInStep_ >= pendingAt_[d] - 1e-6) {
                    pendingAt_[d] = -1.0;
                    fireDevice(d);
                }
            }
            if (flamAt_ >= 0.0 && sampleInStep_ >= flamAt_ - 1e-6) {
                const uint32_t fv = flamVoices_, fa = flamAccent_;
                flamAt_ = -1.0; flamVoices_ = 0; flamAccent_ = 0;
                if (drum909_)
                    for (int v = 0; v < kNumDrumVoices; ++v)
                        if (fv & (1u << v))
                            drum909_->trigger(v, 0.637f, (fa & (1u << v)) != 0, false);
            }
            if (sampleInStep_ >= (double)samplesPerStep(step_) - 1e-6) {
                sampleInStep_ -= (double)samplesPerStep(step_);
                // A pending flam belongs to the step that scheduled it, so drop it
                // at the boundary rather than letting it leak into the next step.
                // This MUST happen before advanceStep(), which calls fireStep() and
                // so schedules the NEXT step's flam; clearing afterwards wiped it.
                flamAt_ = -1.0; flamVoices_ = 0; flamAccent_ = 0;
                advanceStep();
            }
        }
        done += n;
    }
}

} // namespace reborn
