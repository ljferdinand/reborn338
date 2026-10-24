// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Minimal dependency-free test harness for reborn_core.
// Returns non-zero on the first failure so CTest reports it.
//
#include "reborn/audio.h"
#include "reborn/wav.h"
#include "reborn/engine.h"
#include "reborn/factories.h"   // makeOpen303 (the plugin's shared voice)
#include "reborn/rbs.h"
#include "reborn/rbsx.h"
#include "reborn/rbm.h"
#include "reborn/rbm_unscramble.h"
#include "reborn/sampleset.h"
#include "reborn/step_events.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace reborn;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); ++g_failures; } \
} while (0)

static void test_note_to_hz() {
    CHECK(std::fabs(noteToHz(69) - 440.0) < 1e-6);
    CHECK(std::fabs(noteToHz(57) - 220.0) < 1e-6);   // A3
    CHECK(std::fabs(noteToHz(81) - 880.0) < 1e-6);   // A5
}

static void test_clamp_and_denormal() {
    CHECK(clampf(2.0f, -1.0f, 1.0f) == 1.0f);
    CHECK(clampf(-2.0f, -1.0f, 1.0f) == -1.0f);
    CHECK(flushDenormal(1.0e-40f) == 0.0f);
    CHECK(flushDenormal(0.5f) == 0.5f);
}

static void test_stereo_block() {
    StereoBlock a(4), b(4);
    for (int i = 0; i < 4; ++i) { a.left[i] = 0.1f; b.left[i] = 0.2f; }
    a.add(b);
    CHECK(std::fabs(a.left[0] - 0.3f) < 1e-6);
    a.clear();
    CHECK(a.left[0] == 0.0f);
}

// Render 2 bars of a hat-every-step pattern and return onset sample indices.
static std::vector<int> hatOnsets(bool shuffled, float amount) {
    Engine e;
    e.prepare(44100.0);
    Song s;
    s.tempo = 120.0;
    s.shuffle = amount;
    DrumPattern& p = s.drum909.patterns[0];
    p.length = 16;
    p.shuffle = shuffled;
    for (int i = 0; i < 16; ++i) p.steps[i].setOn(5, true);   // rimshot: ~10 ms
                                                              // decay, so every
                                                              // onset separates
    e.loadSong(s);
    e.play();
    const int frames = 44100 * 2;
    StereoBlock out((std::size_t) frames);
    e.process(out, frames);
    // Onset = amplitude rising above threshold after >10 ms below it.
    std::vector<int> on;
    int below = 100000;
    for (int i = 0; i < frames; ++i) {
        float a = std::fabs(out.left[i]);
        if (a > 0.02f && below > 441) on.push_back(i);
        if (a > 0.02f) below = 0; else below++;
    }
    return on;
}

static void test_shuffle_timing() {
    const double step = 44100.0 * 15.0 / 120.0;               // 5512.5 samples
    std::vector<int> straight = hatOnsets(false, 0.5f);
    std::vector<int> swung    = hatOnsets(true, 0.5f);
    CHECK(straight.size() >= 8 && swung.size() >= 8);
    if (straight.size() < 8 || swung.size() < 8) return;
    // Straight: uniform intervals ~= step.
    for (size_t i = 2; i < 8; ++i) {
        double d = double(straight[i] - straight[i - 1]);
        CHECK(std::fabs(d - step) < step * 0.05);
    }
    // Shuffled at amount 0.5: intervals alternate step*(1+0.25), step*(1-0.25).
    for (size_t i = 2; i < 8; ++i) {
        double d = double(swung[i] - swung[i - 1]);
        double expectLong  = step * 1.25, expectShort = step * 0.75;
        bool nearLong  = std::fabs(d - expectLong)  < step * 0.06;
        bool nearShort = std::fabs(d - expectShort) < step * 0.06;
        CHECK(nearLong || nearShort);
    }
    // And the alternation must actually alternate.
    double d1 = double(swung[3] - swung[2]);
    double d2 = double(swung[4] - swung[3]);
    CHECK(std::fabs(d1 - d2) > step * 0.3);
}

// --------------------------------------------------------------------------
// Pattern EDITING -> audio. The GUI's step grid mutates exactly these fields,
// so these lock in the edit path it depends on: a toggled drum step must make
// sound, a cleared one must not, pattern length must bound playback, and a
// 303 note row must produce the pitch the grid's row mapping implies.
// --------------------------------------------------------------------------
static double rmsOf(const StereoBlock& b, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += double(b.left[i]) * double(b.left[i]);
    return std::sqrt(s / std::max(1, n));
}

// One full bar at 120 BPM = 16 x 0.125 s = 2 s. Render that so every step of
// the pattern is actually reached.
constexpr int kBarFrames = 88200;

// An idle engine still emits an inaudible floor (~-97 dBFS: filter settling and
// denormal-flushed state), so "silent" is asserted RELATIVE to a real hit.
constexpr double kSilenceRatio = 0.01;      // 40 dB below the hit

static double renderDrumRms(bool placeHit, int length = 16, int hitStep = 0) {
    Song song;
    song.tempo = 120.0;
    DrumPattern& d = song.drum808.patterns[0];
    d.length = length;
    if (placeHit) d.steps[hitStep].setOn(0, true);     // what the grid's click does
    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.play();
    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);
    return rmsOf(out, kBarFrames);
}

static void test_edit_drum_step_makes_sound() {
    const double withHit = renderDrumRms(true);
    const double empty   = renderDrumRms(false);
    CHECK(withHit > 1e-4);                          // a toggled-on step is audible
    CHECK(empty   < withHit * kSilenceRatio);       // an empty pattern is silent
}

// Pattern length must bound playback: a hit past the length is never reached
// (the sequencer indexes steps modulo the pattern length).
static void test_pattern_length_bounds_playback() {
    const double inside  = renderDrumRms(true, 16, 6);   // step 6 within length 16
    const double outside = renderDrumRms(true, 4,  6);   // shortened past step 6
    CHECK(inside > 1e-4);
    CHECK(outside < inside * kSilenceRatio);
}

// The grid maps row -> semitone as note = 11 - row (B at top, C at bottom).
// Verify the engine turns that note into the expected frequency.
static void test_bass_note_row_mapping() {
    // octaveBase 4, note 9 (row 2 = "A") -> absolute 12*4 + 9 = 57 = A3 = 220 Hz.
    Song song;
    BassPattern& b = song.bass1.patterns[0];
    b.octaveBase = 4;
    b.steps[0] = BassStep{ true, 9, 0, false, false };
    CHECK(b.absoluteNote(0) == 57);
    CHECK(std::fabs(noteToHz(b.absoluteNote(0)) - 220.0) < 1e-6);
    // Octave row: +1 and -1 shift by exactly an octave.
    b.steps[0].octave = 1;
    CHECK(std::fabs(noteToHz(b.absoluteNote(0)) - 440.0) < 1e-6);
    b.steps[0].octave = -1;
    CHECK(std::fabs(noteToHz(b.absoluteNote(0)) - 110.0) < 1e-6);
}

// Drum accent/hit bits are independent per voice (the grid sets one voice's
// bit without disturbing its neighbours).
static void test_drum_voice_bits_independent() {
    DrumStep s;
    s.setOn(0, true);
    s.setOn(5, true);
    s.setAccent(5, true);
    CHECK(s.voiceOn(0) && s.voiceOn(5));
    CHECK(!s.voiceOn(1) && !s.voiceOn(4));
    CHECK(s.voiceAccent(5) && !s.voiceAccent(0));
    s.setOn(5, false);
    CHECK(!s.voiceOn(5) && s.voiceOn(0));      // clearing one leaves the other
}

// Transport: stop() must actually stop the sequencer (the standalone app used
// to restart it every block, making the Stop button a no-op).
static void test_transport_stop_silences() {
    Song song;
    song.tempo = 120.0;
    DrumPattern& d = song.drum808.patterns[0];
    d.length = 16;
    for (int s = 0; s < 16; ++s) d.steps[s].setOn(0, true);
    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.play();
    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);
    const double playing = rmsOf(out, kBarFrames);
    CHECK(playing > 1e-4);
    e.stop();
    CHECK(!e.isPlaying());
    // A stopped engine fires no further steps. Measure the SECOND half of the
    // next bar, by which point the last hit's tail has fully decayed, so this
    // must sit at the idle floor. (The first half still carries that tail:
    // one decaying hit spread over a bar is ~0.18 of a playing bar's RMS, so
    // asserting on the whole buffer would be a near-miss by construction.)
    StereoBlock q; q.resize(kBarFrames);
    e.process(q, kBarFrames);
    const int half = kBarFrames / 2;
    double s = 0.0;
    for (int i = half; i < kBarFrames; ++i) s += double(q.left[i]) * double(q.left[i]);
    const double tail = std::sqrt(s / double(kBarFrames - half));
    CHECK(tail < playing * kSilenceRatio);
}

// A song REFERENCES its skin/mod by name (GLOB+15) and never embeds it, so the
// loader must surface that name for resolution against the user's own mods.
static void test_song_carries_skin_reference() {
    Song s;
    CHECK(s.skinName.empty());        // default: nothing referenced
    s.skinName = "Massen's MSM 2.0";
    CHECK(s.skinName == "Massen's MSM 2.0");
    CHECK(s.loopBars == 0);           // 0 = the file did not store one
}

// Per-channel metering feeds the mixer strip's meters: a channel that plays
// must report a peak, a silent one must not, and mute must zero it.
static void test_channel_peak_metering() {
    Song song;
    song.tempo = 120.0;
    DrumPattern& d = song.drum808.patterns[0];
    d.length = 16;
    for (int s = 0; s < 16; ++s) d.steps[s].setOn(0, true);   // 808 only
    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.play();
    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);

    CHECK(e.channelPeak(kDrum808) > 0.01f);    // the playing channel reads
    CHECK(e.channelPeak(kBass1)  < 1.0e-3f);   // a silent one does not
    CHECK(e.channelPeak(kDrum909) < 1.0e-3f);

    // Muting drops the post-fader peak to nothing.
    e.channel(kDrum808).mute = true;
    StereoBlock q; q.resize(kBarFrames);
    e.process(q, kBarFrames);
    CHECK(e.channelPeak(kDrum808) < 1.0e-3f);
}

// The master meter reads what actually leaves, so the master fader moves it.
static void test_master_peak_follows_fader() {
    Song song;
    song.tempo = 120.0;
    DrumPattern& d = song.drum808.patterns[0];
    d.length = 16;
    for (int s = 0; s < 16; ++s) d.steps[s].setOn(0, true);
    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.play();
    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);
    const float loud = e.masterPeak();
    CHECK(loud > 0.01f);

    e.masterVolume = 0.0f;                 // pulled down: nothing leaves
    StereoBlock q; q.resize(kBarFrames);
    e.process(q, kBarFrames);
    e.process(q, kBarFrames);              // let the 150 ms release settle
    CHECK(e.masterPeak() < loud * 0.1f);
}

// Round-trip: a Song written to .rbs and read back must decode identically.
// This is the acceptance criterion for the writer: without it, pattern
// editing in the GUI has nowhere to go.
static void test_rack_routing_exclusivity() {
    // ReBirth's rules, measured 2026-08-27/28 by clicking each switch and
    // diffing the save: the distortion is a per-section flag any number of
    // sections may set, while the PCF and the compressor are single selectors
    // and setting one clears whoever had it. The engine owns the rule so a
    // loaded song and a click in the UI cannot disagree.
    Engine e;
    e.prepare(44100.0);

    e.setDistEnable(0, true);
    e.setDistEnable(2, true);
    CHECK(e.channel(0).distEnable);
    CHECK(!e.channel(1).distEnable);
    CHECK(e.channel(2).distEnable);

    e.setPcfTarget(1);
    CHECK(e.pcfTarget() == 1);
    e.setPcfTarget(3);
    CHECK(e.pcfTarget() == 3);
    CHECK(!e.channel(1).pcfEnable);      // taken away from whoever had it
    CHECK(e.channel(3).pcfEnable);
    e.setPcfTarget(-1);
    CHECK(e.pcfTarget() == -1);
    // The distortion is untouched by any of that.
    CHECK(e.channel(0).distEnable);
    CHECK(e.channel(2).distEnable);

    e.setCompTarget(2);
    CHECK(e.compTarget() == 2);
    e.setCompTarget(kCompMaster);
    CHECK(e.compTarget() == kCompMaster);
    e.setCompTarget(-1);
    CHECK(e.compTarget() == -1);
    e.setCompTarget(99);                  // out of range falls back to nowhere
    CHECK(e.compTarget() == -1);
}

// Songs PERFORM the rack's routing, and holding it at its opening value is what
// made our drums dull. Twelve of the sixteen demo songs ride the mixer track's
// param 2: G-Spot moves it thirty-five times and its PCF is on the 909 for only
// 27 percent of the song, RSI for 8. Held flat, the filter runs throughout and
// takes 14 to 39 dB off the top of the drums where ReBirth takes none.
static void test_pcf_routing_is_automatable() {
    Song song;
    song.tempo = 120.0;
    song.drum909.patterns[0].steps[0].setOn(0, true);
    song.effectInit.present   = true;
    song.effectInit.pcfOn     = true;
    song.effectInit.pcfTarget = 3;                  // on the 909 at the start
    // 32 ticks to the bar, so this takes it off after one bar and back after two.
    song.automation.push_back({ 32, AutoTarget::Mixer, 2, 0 });
    song.automation.push_back({ 64, AutoTarget::Mixer, 2, 4 });

    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.setSongMode(true);
    e.play();
    CHECK(e.pcfTarget() == 3);
    CHECK(e.pcfUnit());

    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);                     // through bar 1
    CHECK(e.pcfTarget() == -1);
    // Taking the routing away must NOT switch the unit off: the two are
    // separate controls and the PCF track rides its own bypass.
    CHECK(e.pcfUnit());
    e.process(out, kBarFrames);                     // through bar 2
    CHECK(e.pcfTarget() == 3);

    // And a click routes without disturbing the unit either way.
    e.setPcfUnit(false);
    e.setPcfTarget(1);
    CHECK(e.pcfTarget() == 1);
    CHECK(!e.pcfUnit());
}

// The per-section distortion switch is performed too: twelve of the sixteen
// demo songs move the mixer track's base+3 after tick 0, 360 events in all.
// It is a per-section FLAG, so switching one section must leave the others
// alone, unlike the PCF's exclusive target.
static void test_distortion_switch_is_automatable() {
    Song song;
    song.tempo = 120.0;
    song.drum909.patterns[0].steps[0].setOn(0, true);
    song.effectInit.present      = true;
    song.effectInit.distortionOn = true;            // the unit's own bypass
    song.effectInit.distOn[1]    = true;
    song.effectInit.distOn[3]    = true;
    const int base3 = 6 + 6 * 3;                    // the 909's group
    song.automation.push_back({ 32, AutoTarget::Mixer, base3 + 3, 0 });
    song.automation.push_back({ 64, AutoTarget::Mixer, base3 + 3, 1 });

    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.setSongMode(true);
    e.play();
    CHECK(e.channel(3).distEnable);

    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);
    CHECK(!e.channel(3).distEnable);
    CHECK(e.channel(1).distEnable);                 // its neighbour is untouched
    e.process(out, kBarFrames);
    CHECK(e.channel(3).distEnable);
    CHECK(e.channel(1).distEnable);

    // Routing and the unit's bypass are separate, so a section routed into a
    // switched-off unit passes through clean. This used to be folded together
    // at load time, which an automated switch would have walked straight past.
    CHECK(e.distUnit());
    e.setDistUnit(false);
    CHECK(e.channel(3).distEnable);
    CHECK(!e.distUnit());
}

// Distortion structural items 1 and 2 (2026-09-18). ReBirth's rack is per-
// section chains, not shared buses: each routed section carries its own
// distortion instance and is panned exactly once, the same as a clean channel.
// The old parallel-bus design summed a routed section's audio WITHOUT the pan
// law and so came out ~3.01 dB hot (the PCF-bus bug, docs/NON_OBVIOUS_BEHAVIOUR.md); the
// per-section rewrite during the PCF supertask removed the buses outright, and
// this pins that a distorted section still obeys the pan law in audio: hard to
// one side is silent on the far side, centred is symmetric. A regression that
// reintroduced an un-panned distortion bus would fail the far-side checks.
static void test_distortion_is_panned_per_section() {
    auto renderDistorted = [](float pan, double& l, double& r) {
        Song song;
        song.tempo = 120.0;
        // A 909 kick on every step: steady, broadband, easy to drive hard.
        DrumPattern& p = song.drum909.patterns[0];
        p.length = 16;
        for (int i = 0; i < 16; ++i) p.steps[i].setOn(0, true);
        song.effectInit.present          = true;
        song.effectInit.distortionOn     = true;          // the unit's bypass: on
        song.effectInit.distOn[kDrum909] = true;          // routed to the 909 only
        song.effectInit.distAmount       = 1.0f;          // full drive, plenty of signal
        song.effectInit.distTone         = 0.5f;

        Engine e;
        e.prepare(44100.0);
        e.loadSong(song);
        e.channel(kDrum909).pan = pan;                    // song carries no mixerInit, so this holds
        e.setSongMode(true);
        e.play();
        StereoBlock out; out.resize(kBarFrames);
        e.process(out, kBarFrames);
        double sl = 0.0, sr = 0.0;
        for (int i = 0; i < kBarFrames; ++i) {
            sl += double(out.left[i]) * double(out.left[i]);
            sr += double(out.right[i]) * double(out.right[i]);
        }
        l = std::sqrt(sl / kBarFrames);
        r = std::sqrt(sr / kBarFrames);
    };

    double l, r;
    renderDistorted(0.0f, l, r);                          // centre
    CHECK(l > 1e-4 && r > 1e-4);                          // the distorted section sounds
    CHECK(std::fabs(l - r) < 0.05 * (l + r));             // one pan, symmetric, not a hot bus

    renderDistorted(-1.0f, l, r);                         // hard left
    CHECK(l > 1e-4);
    CHECK(r < l * 0.02);                                  // far (right) side cut by the pan law

    renderDistorted(1.0f, l, r);                          // hard right
    CHECK(r > 1e-4);
    CHECK(l < r * 0.02);                                  // far (left) side cut
}

// Distortion structural item 4 (2026-09-18): the DIST track rides Shape
// (param 1), Amount (param 2) and the unit's on/off (param 0), and songs perform
// them heavily: Mutilated Species II carries 1241 Amount rides, and several
// songs (it, KiloMix, SuperNova, Transfusion) START at Amount 0 on the track and
// ride it up, so freezing at tick 0 rendered them with the distortion off. This
// pins that those rides are decoded (AutoTarget::Dist) and applied to the unit
// the four per-section instances mirror.
static void test_distortion_knob_automation() {
    Song song;
    song.tempo = 120.0;
    song.drum909.patterns[0].steps[0].setOn(0, true);
    song.effectInit.present          = true;
    song.effectInit.distortionOn     = true;
    song.effectInit.distOn[kDrum909] = true;
    song.effectInit.distAmount       = 0.0f;   // starts clean, like the shipped songs
    song.effectInit.distTone         = 0.5f;
    // Ride Amount to full at bar 1, Shape to 0 at bar 2, the unit off at bar 3.
    song.automation.push_back({ 32, AutoTarget::Dist, 2, 127 });
    song.automation.push_back({ 64, AutoTarget::Dist, 1, 0 });
    song.automation.push_back({ 96, AutoTarget::Dist, 0, 0 });

    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);
    e.setSongMode(true);
    e.play();
    Effect* d = e.distortion();
    CHECK(d != nullptr);
    CHECK(d->getParam(0) < 1e-6);        // amount 0 = starts clean (exact bypass)
    CHECK(e.distUnit());

    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames);
    CHECK(d->getParam(0) > 0.9f);        // Amount rode up to full
    e.process(out, kBarFrames);
    CHECK(d->getParam(1) < 0.05f);       // Shape rode down to 0
    e.process(out, kBarFrames);
    CHECK(!e.distUnit());                // the unit's own on/off rode off
}

static void test_pcf_chunk_round_trip() {
    // The PCF chunk and the mixer's three routing fields, mapped 2026-08-27 by
    // driving each control over its MIDI CC and by clicking each switch. The
    // distortion is a per-section flag while the PCF is an exclusive insert,
    // and the two used to share one mis-read byte.
    Song a;
    a.title = "PCF";
    a.effectInit.present     = true;
    a.effectInit.pcfOn       = true;
    a.effectInit.pcfTarget   = 3;      // the 909
    a.effectInit.pcfFreq     = 70.0f / 127.0f;
    a.effectInit.pcfQ        = 64.0f / 127.0f;
    a.effectInit.pcfAmt      = 40.0f / 127.0f;
    a.effectInit.pcfDecay    = 90.0f / 127.0f;
    a.effectInit.pcfPattern  = 24;
    a.effectInit.pcfBandpass = true;
    a.effectInit.compTarget  = 1;      // bass 2
    a.effectInit.distOn[0]   = true;
    a.effectInit.distOn[2]   = true;

    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));
    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));

    CHECK(b.effectInit.pcfTarget == 3);
    CHECK(b.effectInit.pcfPattern == 24);
    CHECK(b.effectInit.pcfBandpass);
    CHECK(std::fabs(b.effectInit.pcfFreq  - a.effectInit.pcfFreq)  < 0.01f);
    CHECK(std::fabs(b.effectInit.pcfQ     - a.effectInit.pcfQ)     < 0.01f);
    CHECK(std::fabs(b.effectInit.pcfAmt   - a.effectInit.pcfAmt)   < 0.01f);
    CHECK(std::fabs(b.effectInit.pcfDecay - a.effectInit.pcfDecay) < 0.01f);
    CHECK(b.effectInit.compTarget == 1);
    // The distortion flags must survive independently of the PCF and the
    // compressor: one selector byte used to stand in for all three.
    CHECK(b.effectInit.distOn[0]);
    CHECK(!b.effectInit.distOn[1]);
    CHECK(b.effectInit.distOn[2]);
    CHECK(!b.effectInit.distOn[3]);

    // A pattern number above the 55 the unit has must clamp, not wrap.
    a.effectInit.pcfPattern = 200;
    CHECK(saveRbs(a, buf, &err));
    Song c;
    CHECK(loadRbs(buf.data(), buf.size(), c, &err));
    CHECK(c.effectInit.pcfPattern == 54);
}

static void test_rbs_round_trip() {
    Song a;
    a.title = "Round Trip";
    a.author = "Reborn";
    a.tempo = 137.5;
    a.shuffle = 0.25f;
    a.loopBars = 7;
    a.skinName = "Massen's MSM 2.0";
    a.bass1Knobs.present = true;
    a.bass1Knobs.cutoff = 0.75f; a.bass1Knobs.resonance = 0.5f;
    a.bass1Knobs.envmod = 0.25f; a.bass1Knobs.decay = 0.6f;
    a.bass1Knobs.accent = 0.9f;  a.bass1Knobs.waveform = 1.0f;
    a.bass1Knobs.tuning = 0.0f;

    BassPattern& bp = a.bass1.patterns[0];
    bp.length = 12;
    bp.steps[0] = BassStep{ true, 9, 0, true,  false };
    bp.steps[1] = BassStep{ true, 4, 1, false, true  };
    bp.steps[2] = BassStep{ true, 7,-1, false, false };
    bp.steps[3] = BassStep{ false,0, 0, false, false };   // rest

    DrumPattern& dp = a.drum808.patterns[0];
    dp.length = 16;
    dp.steps[0].setOn(0, true);                 // BD
    dp.steps[4].setOn(1, true);                 // SD
    dp.steps[4].setAccent(1, true);
    dp.steps[2].setOn(14, true);                // closed hat

    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));
    CHECK(buf.size() > 1000);

    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));

    // identity + transport
    CHECK(b.title == a.title);
    CHECK(std::fabs(b.tempo - a.tempo) < 0.01);
    CHECK(b.loopBars == a.loopBars);
    CHECK(b.skinName == a.skinName);
    CHECK(std::fabs(b.shuffle - a.shuffle) < 0.02f);

    // 303 knobs survive the 0..127 quantisation
    CHECK(std::fabs(b.bass1Knobs.cutoff - a.bass1Knobs.cutoff) < 0.02f);
    CHECK(std::fabs(b.bass1Knobs.resonance - a.bass1Knobs.resonance) < 0.02f);
    CHECK(b.bass1Knobs.waveform >= 0.5f);
    CHECK(std::fabs(b.bass1Knobs.tuning) < 0.2f);

    // 303 pattern: notes, octaves, accent, slide, rests, length
    const BassPattern& rb = b.bass1.patterns[0];
    CHECK(rb.length == 12);
    CHECK(rb.steps[0].gate && rb.steps[0].note == 9 && rb.steps[0].accent);
    CHECK(rb.steps[1].gate && rb.steps[1].note == 4 && rb.steps[1].octave == 1
          && rb.steps[1].slide);
    CHECK(rb.steps[2].gate && rb.steps[2].note == 7 && rb.steps[2].octave == -1);
    CHECK(!rb.steps[3].gate);

    // 808 pattern: the right voices on the right steps, accent preserved
    const DrumPattern& rd = b.drum808.patterns[0];
    CHECK(rd.steps[0].voiceOn(0));
    CHECK(rd.steps[4].voiceOn(1) && rd.steps[4].voiceAccent(1));
    CHECK(rd.steps[2].voiceOn(14));
    CHECK(!rd.steps[1].voiceOn(0));

    // the loss report should call this session lossless
    const RbsLoss loss = rbsLossFor(a);
    CHECK(loss.lossless && loss.overLongPatterns == 0);
}

// --------------------------------------------------------------------------
// The save path (review W1 to W3, T1). The writer dropped the arrangement and
// every automation event, wrote the faders linearly, lost the delay's grid and
// pan, and saved the song as loaded rather than the panel the GUI had changed.
// --------------------------------------------------------------------------
static int byteOf(float v) { return (int) std::lround(v * 127.0f); }

// Every TRAK of a saved file, in file order, as {tick, param, value} events: a
// minimal walk of the IFF tree, independent of the loader under test.
using TrakEvents = std::vector<std::array<int, 3>>;
static void walkTraks(const std::vector<unsigned char>& f, std::size_t off, std::size_t end,
                      std::vector<TrakEvents>& out) {
    auto u32 = [&f](std::size_t i) {
        return (uint32_t(f[i]) << 24) | (uint32_t(f[i + 1]) << 16) | (uint32_t(f[i + 2]) << 8) | f[i + 3];
    };
    while (off + 8 <= end) {
        const std::size_t body = off + 8, size = u32(off + 4);
        if (body + size > end) return;
        if (!std::memcmp(&f[off], "CAT ", 4)) walkTraks(f, body + 4, body + size, out);
        if (!std::memcmp(&f[off], "TRAK", 4)) {
            TrakEvents ev;
            std::size_t i = body + 4;
            int t = 0;
            for (uint32_t n = u32(body); n > 0; --n) {
                int dt = 0;
                while (f[i] & 0x80) dt = (dt << 7) | (f[i++] & 0x7F);
                t += (dt << 7) | f[i++];
                ev.push_back({ t, f[i], f[i + 1] });
                i += 2;
            }
            out.push_back(ev);
        }
        off = body + size + (size & 1);
    }
}
static std::vector<TrakEvents> trakEventsOf(const std::vector<unsigned char>& f) {
    std::vector<TrakEvents> out;
    walkTraks(f, 0, f.size(), out);
    return out;
}

// The writer's fader and pan laws are the exact inverses of the loader's: every
// byte the file can hold comes back as itself, at both unities the loader uses.
static void test_fader_and_pan_inverses() {
    int badFader = 0, badPan = 0;
    for (int b = 0; b <= 127; ++b) {
        if (faderByteFromGain(faderGain(b, 1.0f), 1.0f) != b) ++badFader;
        if (faderByteFromGain(faderGain(b, kChannelUnityGain), kChannelUnityGain) != b) ++badFader;
    }
    for (int b = 1; b <= 127; ++b)
        if (panByteFromPan(panFromByte(b)) != b) ++badPan;
    CHECK(badFader == 0);
    CHECK(badPan == 0);
    // Pan bytes 0 and 1 are both hard left and render identically; 0 comes back as 1.
    CHECK(panFromByte(0) == panFromByte(1));
    CHECK(panByteFromPan(panFromByte(0)) == 1);
    // Only a zero gain is silence; anything above it is at least byte 1, and
    // the top clamps at 127.
    CHECK(faderByteFromGain(0.0f) == 0);
    CHECK(faderByteFromGain(1.0e-9f) == 1);
    CHECK(faderByteFromGain(100.0f) == 127);
    // Byte 100 is the anchor at both unities.
    CHECK(faderByteFromGain(1.0f) == 100);
    CHECK(faderByteFromGain(kChannelUnityGain, kChannelUnityGain) == 100);
}

// Measured 2026-10-01 (fixtures-2026-10-01/comp-rides, cr_*): ReBirth's Song
// mode plays the COMP track's timed rides. A ratio or threshold ride at tick 64
// lands within 0.03 dB of the static export at its end setting. The demo songs
// carry 156 (65 Ratio, 91 Threshold). This pins that the rides reach the unit
// at their bar, that the meter (param 3) moves nothing, and that a save still
// writes every one of them back.
static void test_compressor_rides_play() {
    Song song;
    song.tempo = 120.0;
    song.effectInit.present       = true;
    song.effectInit.compTarget    = kCompMaster;
    song.effectInit.compressorOn  = true;
    song.effectInit.compRatio     = 0.0f;
    song.effectInit.compThreshold = 0.0f;
    using T = AutoTarget;
    song.automation = { { 32, T::Comp, 1, 127 }, { 32, T::Comp, 3, 50 },
                        { 64, T::Comp, 2, 100 }, { 96, T::Comp, 0, 0 } };
    SongStep row;
    row.repeats = 4;
    song.arrangement = { row };

    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(song, buf, &err));
    Song loaded;
    CHECK(loadRbs(buf.data(), buf.size(), loaded, &err));
    std::vector<std::array<int, 3>> rides;
    for (const AutoEvent& ev : loaded.automation)
        if (ev.target == T::Comp && ev.tick > 0) rides.push_back({ int(ev.tick), ev.param, ev.value });
    CHECK((rides == std::vector<std::array<int, 3>>{
        { 32, 1, 127 }, { 32, 3, 50 }, { 64, 2, 100 }, { 96, 0, 0 } }));

    Engine e;
    e.prepare(44100.0);
    e.loadSong(loaded);
    e.setSongMode(true);
    e.play();
    Effect* c = e.compressor();
    CHECK(c != nullptr);
    CHECK(byteOf(c->getParamByName("ratio")) == 0 && byteOf(c->getParamByName("threshold")) == 0);
    CHECK(e.compressorOn);

    StereoBlock out; out.resize(kBarFrames);
    e.process(out, kBarFrames / 2);
    CHECK(byteOf(c->getParamByName("ratio")) == 0);        // nothing before its bar
    e.process(out, kBarFrames / 2);
    CHECK(byteOf(c->getParamByName("ratio")) == 127);      // Ratio rode up at bar 1
    CHECK(byteOf(c->getParamByName("threshold")) == 0);    // and the meter moved nothing
    e.process(out, kBarFrames);
    CHECK(byteOf(c->getParamByName("threshold")) == 100);  // Threshold at bar 2
    CHECK(e.compressorOn);
    e.process(out, kBarFrames);
    CHECK(!e.compressorOn);                                // the unit's on/off at bar 3
}

// Measured 2026-10-01 (fixtures-2026-10-01/comp-rides, mt_): with the master
// compressor compressing, moving the master byte from 70 to 115 moves ReBirth's
// output by the fader's full 16.65 dB, so the master fader follows the
// compressor; and comp_wav, which feeds the DSP the output level, matches
// ReBirth where the engine read 2.99 dB low, because the engine's compressor
// saw the bus before the fixed 0.72 headroom. This pins both: the output is
// exactly linear in masterVolume with the compressor on, and at masterVolume
// 0.72 (master byte 100) the engine's output is a standalone compressor run
// on the comp-off output, i.e. the compressor's input is the bus times 0.72.
// Only the second check discriminates: the linearity check also passed on the
// old code, which already applied masterVolume after the compressor and only
// had the fixed headroom on the wrong side of it.
static void test_master_compressor_sees_the_output_level() {
    auto render = [](bool compOn, float masterVolume) {
        Song song;
        song.tempo = 120.0;
        DrumPattern& d = song.drum808.patterns[0];
        d.length = 16;
        for (int st = 0; st < 16; ++st) d.steps[st].setOn(0, true);
        song.effectInit.present       = true;
        song.effectInit.compTarget    = kCompMaster;
        song.effectInit.compressorOn  = compOn;
        song.effectInit.compRatio     = 1.0f;     // ratio 127
        song.effectInit.compThreshold = 0.0f;     // threshold 0: compressing hard
        Engine e;
        e.prepare(44100.0);
        e.loadSong(song);
        e.play();
        e.masterVolume = masterVolume;
        StereoBlock out; out.resize(kBarFrames);
        e.process(out, kBarFrames);
        return out;
    };
    const float unity = 0.72f;                    // kMasterHeadroom, master byte 100
    const StereoBlock off  = render(false, unity);
    const StereoBlock on   = render(true,  unity);
    const StereoBlock half = render(true,  unity * 0.5f);

    float peakOff = 0.0f, peakOn = 0.0f;
    for (int i = 0; i < kBarFrames; ++i) {
        peakOff = std::max(peakOff, std::fabs(off.left[i]));
        peakOn  = std::max(peakOn,  std::fabs(on.left[i]));
    }
    CHECK(peakOff > 0.01f && peakOff < 0.99f);    // sounding, and never clipped
    CHECK(peakOn  > 0.001f && peakOn < 0.99f);

    // The fader follows the compressor: half the master gain, half the output.
    float worst = 0.0f;
    for (int i = 0; i < kBarFrames; ++i) {
        worst = std::max(worst, std::fabs(half.left[i]  - 0.5f * on.left[i]));
        worst = std::max(worst, std::fabs(half.right[i] - 0.5f * on.right[i]));
    }
    CHECK(worst < 1e-7f);

    // The compressor sees bus * 0.72: the same DSP run standalone on the
    // comp-off output (which IS bus * 0.72) gives the comp-on output.
    std::unique_ptr<Effect> c = makeCompressor();
    c->prepare(44100.0);
    c->setParamByName("ratio", 1.0f);
    c->setParamByName("threshold", 0.0f);
    StereoBlock ref = off;
    c->process(ref, kBarFrames);
    worst = 0.0f;
    float diffOn = 0.0f;
    for (int i = 0; i < kBarFrames; ++i) {
        worst  = std::max(worst, std::fabs(ref.left[i]  - on.left[i]));
        worst  = std::max(worst, std::fabs(ref.right[i] - on.right[i]));
        diffOn = std::max(diffOn, std::fabs(off.left[i] - on.left[i]));
    }
    CHECK(diffOn > 1e-3f);                        // the compressor is doing something
    CHECK(worst < 1e-6f);
}

// Measured 2026-10-01 (fixtures-2026-10-01/comp-rides, mp_): ReBirth's master
// compressor reads a hard-left drone 5.73 to 5.92 dB lower than the same drone
// centred, so it detects the L+R sum. The detector's input is (l + r) / 2: a
// hard-left signal of amplitude a reads exactly as a centred one of a / 2, 6.02
// dB lower, and so receives less gain reduction than the same signal centred.
// (Round 2 squared that input for an RMS detector; round 5 takes its peak. For
// l == r, (l + l) * 0.5f is l exactly in float, so centred material reads as
// one channel alone. Signed versus |l| + |r| is unmeasured.)
static void test_compressor_detects_the_lr_sum() {
    const int n = 44100;
    auto run = [n](float al, float ar) {
        std::unique_ptr<Effect> c = makeCompressor();
        c->prepare(44100.0);
        c->setParamByName("ratio", 1.0f);         // ratio 127
        c->setParamByName("threshold", 0.0f);     // threshold 0: compressing hard
        StereoBlock b; b.resize(n);
        for (int i = 0; i < n; ++i) {
            const float x = std::sin(2.0f * 3.14159265f * 220.0f * float(i) / 44100.0f);
            b.left[i] = al * x; b.right[i] = ar * x;
        }
        c->process(b, n);
        return b;
    };
    const StereoBlock centred = run(0.5f, 0.5f);
    const StereoBlock halfCentred = run(0.25f, 0.25f);
    const StereoBlock left = run(0.5f, 0.0f);
    bool exact = true, silentRight = true;
    for (int i = 0; i < n; ++i) {
        exact = exact && left.left[i] == 2.0f * halfCentred.left[i];
        silentRight = silentRight && left.right[i] == 0.0f;
    }
    CHECK(exact);           // hard left at a is detected as centred at a / 2
    CHECK(silentRight);
    // Less gain reduction than the same signal centred: louder out of the left.
    double eL = 0.0, eC = 0.0;
    for (int i = n / 2; i < n; ++i) {
        eL += double(left.left[i]) * left.left[i];
        eC += double(centred.left[i]) * centred.left[i];
    }
    CHECK(eL > eC * 1.5);   // ratio 127 (k = 0.99) pins the output near T whatever
                            // the input, so left, twice the half-level centred
                            // output, comes out nearly 6 dB louder
}

// Round 5 (2026-10-01, comp-material): ReBirth's detector reads steady material
// by its PEAK, not its RMS, and after a hit it holds, then falls at a fixed
// rate in dB. Two pins, both of which the round-2 detector (25 ms RMS into a
// 6 ms / 45 ms gain smoother) fails:
//  - a square and a sine at the SAME RMS: the square's peak is 3.01 dB lower,
//    so at ratio 127 it is cut about 3 dB less (an RMS detector cuts both the
//    same);
//  - after a loud burst, over a quiet tone below the threshold floor, the gain
//    stays flat through the hold and then rises at a constant rate in dB, the
//    same over 60-80 ms as over 90-110 ms (a one-pole smoother on the gain
//    slows as it nears its target).
static void test_compressor_detector_reads_peaks_and_releases_linearly_in_db() {
    const double sr = 44100.0;
    auto grDb = [sr](bool square) {
        const int n = 44100;
        std::unique_ptr<Effect> c = makeCompressor();
        c->prepare(sr);
        c->setParamByName("ratio", 1.0f);         // ratio 127
        c->setParamByName("threshold", 0.0f);     // threshold 0
        StereoBlock b; b.resize(n);
        const float amp = square ? 0.25f / std::sqrt(2.0f) : 0.25f;   // equal RMS
        for (int i = 0; i < n; ++i) {
            const float ph = float(std::fmod(110.0 * i / sr, 1.0));
            const float x = square ? (ph < 0.5f ? amp : -amp)
                                   : amp * std::sin(2.0f * 3.14159265f * ph);
            b.left[i] = x; b.right[i] = x;
        }
        double ein = 0.0;
        for (int i = n / 2; i < n; ++i) ein += double(b.left[i]) * b.left[i];
        c->process(b, n);
        double eout = 0.0;
        for (int i = n / 2; i < n; ++i) eout += double(b.left[i]) * b.left[i];
        return 10.0 * std::log10(eout / ein);
    };
    const double grSine = grDb(false), grSquare = grDb(true);
    CHECK(grSine < -10.0);                          // compressing hard
    CHECK(grSquare - grSine > 2.6);                 // peak reading: ~3 dB less cut
    CHECK(grSquare - grSine < 3.4);

    // Hold, then a dB-linear release.
    const int burst = 22050, tail = 22050, n = burst + tail;
    std::unique_ptr<Effect> c = makeCompressor();
    c->prepare(sr);
    c->setParamByName("ratio", 1.0f);
    c->setParamByName("threshold", 0.0f);
    StereoBlock b; b.resize(n);
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i) {
        const float a = i < burst ? 0.5f : 1.0e-4f;   // the tail is below the floor
        in[i] = a * std::sin(2.0f * 3.14159265f * 1000.0f * float(i) / float(sr));
        b.left[i] = in[i]; b.right[i] = in[i];
    }
    c->process(b, n);
    auto gainAtMs = [&](double ms) {               // gain in dB over a 5 ms window
        const int s = burst + int(ms * sr / 1000.0), w = int(0.005 * sr);
        double eo = 0.0, ei = 0.0;
        for (int i = s; i < s + w; ++i) { eo += double(b.left[i]) * b.left[i]; ei += double(in[i]) * in[i]; }
        return 10.0 * std::log10(eo / ei);
    };
    const double g2 = gainAtMs(2.0), g15 = gainAtMs(15.0);
    const double g60 = gainAtMs(60.0), g80 = gainAtMs(80.0), g90 = gainAtMs(90.0), g110 = gainAtMs(110.0);
    CHECK(std::fabs(g15 - g2) < 0.05);              // held
    const double r1 = (g80 - g60) / 0.020, r2 = (g110 - g90) / 0.020;   // dB/s
    CHECK(r1 > 120.0 && r1 < 180.0);                // the fitted ~150 dB/s
    CHECK(std::fabs(r2 - r1) < 0.05 * r1);          // constant in dB
}

// Measured 2026-10-01 (fixtures-2026-10-01/comp-rides, st_): with the
// compressor on the 303's section, moving that section's fader from 127 to 90
// drops ReBirth's compressed output by the full 13.69 dB the uncompressed one
// drops, so the channel fader comes AFTER the section compressor, which sees
// the section at the level a fader of 127 gives it. Ours ran it after the fader,
// so the fader changed how hard it compressed. This pins that the section's
// output scales linearly with its fader with the compressor on, and that the
// gain reduction (compressed over uncompressed) does not move with the fader.
static void test_section_compressor_precedes_the_fader() {
    auto render = [](bool compOn, int faderByte) {
        Song song;
        song.tempo = 120.0;
        DrumPattern& d = song.drum808.patterns[0];
        d.length = 16;
        for (int st = 0; st < 16; ++st) d.steps[st].setOn(0, true);
        song.effectInit.present       = true;
        song.effectInit.compTarget    = kDrum808;
        song.effectInit.compressorOn  = compOn;
        song.effectInit.compRatio     = 1.0f;     // ratio 127
        song.effectInit.compThreshold = 0.0f;     // threshold 0: compressing hard
        Engine e;
        e.prepare(44100.0);
        e.loadSong(song);
        e.play();
        e.channel(kDrum808).volume = faderGain(faderByte, kChannelUnityGain);
        StereoBlock out; out.resize(kBarFrames);
        e.process(out, kBarFrames);
        return out;
    };
    auto energy = [](const StereoBlock& o) {
        double s = 0.0;
        for (int i = kBarFrames / 2; i < kBarFrames; ++i)
            s += double(o.left[i]) * o.left[i] + double(o.right[i]) * o.right[i];
        return s;
    };
    const StereoBlock on127 = render(true, 127), on90 = render(true, 90);
    const StereoBlock off127 = render(false, 127), off90 = render(false, 90);
    CHECK(energy(on127) > 1e-6);

    // Linear in the fader: on90 is on127 times the fader's 37 bytes. Samples
    // the master output's hard clamp has cut at fader 127 are skipped: since
    // round 5 the compressor releases to its measured +29.7 dB ceiling between
    // hits at ratio 127 threshold 0 and lets each onset through boosted until
    // the 9 ms attack catches it, as ReBirth does (comp-material kick), so a
    // few onset samples clip at fader 127 and not at 90.
    const float g = faderGain(90, kChannelUnityGain) / faderGain(127, kChannelUnityGain);
    float worst = 0.0f, peak = 0.0f;
    int compared = 0;
    for (int i = 0; i < kBarFrames; ++i) {
        if (std::fabs(on127.left[i]) >= 0.999f || std::fabs(on127.right[i]) >= 0.999f) continue;
        ++compared;
        peak  = std::max(peak, std::fabs(on127.left[i]));
        worst = std::max(worst, std::fabs(on90.left[i]  - g * on127.left[i]));
        worst = std::max(worst, std::fabs(on90.right[i] - g * on127.right[i]));
    }
    CHECK(compared > kBarFrames * 99 / 100);
    CHECK(peak > 1e-3f);
    CHECK(worst < peak * 1e-5f);

    // The same gain reduction at both faders, and a real one.
    const double gr127 = 10.0 * std::log10(energy(on127) / energy(off127));
    const double gr90  = 10.0 * std::log10(energy(on90)  / energy(off90));
    CHECK(std::fabs(gr127) > 1.0);
    CHECK(std::fabs(gr127 - gr90) < 0.001);
}

// With the channel fader after the section compressor, a section at fader 0 or
// muted must keep feeding the compressor. Ours ran it on silence there, so its
// gain released to full make-up and the section came back from mute or fader 0
// as a clipped, full-scale burst (first 10 ms peak 1.0 on the comp-rides
// st_r127_t000 drone, where fader byte 1 gave 0.09). This pins that a muted
// section comes back exactly as if it had never been muted, that one held at
// fader 0 comes back as one held at byte 1 does, and that both are silent while
// down.
static void test_section_compressor_runs_while_the_section_is_down() {
    enum Down { None, Mute, Fader0, Fader1 };
    auto render = [](Down down) {
        Song song;
        song.tempo = 120.0;
        DrumPattern& d = song.drum808.patterns[0];
        d.length = 16;
        for (int st = 0; st < 16; ++st) d.steps[st].setOn(0, true);
        song.effectInit.present       = true;
        song.effectInit.compTarget    = kDrum808;
        song.effectInit.compressorOn  = true;
        song.effectInit.compRatio     = 1.0f;     // ratio 127
        song.effectInit.compThreshold = 0.0f;     // threshold 0: up to +29.7 dB below T (the measured ceiling at env = F)
        Engine e;
        e.prepare(44100.0);
        e.loadSong(song);
        e.play();
        MixerChannel& ch = e.channel(kDrum808);
        const float v127 = faderGain(127, kChannelUnityGain);
        ch.volume = v127;
        StereoBlock out; out.resize(2 * kBarFrames);
        StereoBlock blk; blk.resize(kBarFrames);
        if (down == Mute)   ch.mute = true;
        if (down == Fader0) ch.volume = 0.0f;
        if (down == Fader1) ch.volume = faderGain(1, kChannelUnityGain);
        e.process(blk, kBarFrames);
        for (int i = 0; i < kBarFrames; ++i) { out.left[i] = blk.left[i]; out.right[i] = blk.right[i]; }
        ch.mute = false;
        ch.volume = v127;
        e.process(blk, kBarFrames);
        for (int i = 0; i < kBarFrames; ++i) {
            out.left[kBarFrames + i] = blk.left[i]; out.right[kBarFrames + i] = blk.right[i];
        }
        return out;
    };
    const StereoBlock ref = render(None), mute = render(Mute);
    const StereoBlock f0 = render(Fader0), f1 = render(Fader1);

    float downPeak = 0.0f, refPeak = 0.0f, muteDiff = 0.0f, f0f1 = 0.0f, f0Peak = 0.0f;
    for (int i = 0; i < kBarFrames; ++i) {
        downPeak = std::max({ downPeak, std::fabs(mute.left[i]), std::fabs(mute.right[i]),
                              std::fabs(f0.left[i]), std::fabs(f0.right[i]) });
        const int j = kBarFrames + i;
        refPeak  = std::max(refPeak, std::fabs(ref.left[j]));
        f0Peak   = std::max(f0Peak, std::fabs(f0.left[j]));
        muteDiff = std::max({ muteDiff, std::fabs(mute.left[j] - ref.left[j]),
                              std::fabs(mute.right[j] - ref.right[j]) });
        f0f1     = std::max({ f0f1, std::fabs(f0.left[j] - f1.left[j]),
                              std::fabs(f0.right[j] - f1.right[j]) });
    }
    CHECK(downPeak == 0.0f);                // silent while down
    CHECK(refPeak > 1e-3f);
    CHECK(muteDiff == 0.0f);                // unmuted: as if never muted, bit for bit
    CHECK(f0f1 < refPeak * 1e-3f);          // fader 0 comes back as byte 1 does
    CHECK(f0Peak < refPeak * 1.05f);        // and with no burst
}

// W1. A song with a multi-row arrangement (repeats, a device holding its
// pattern across a row boundary, two identical rows the loader will merge) and
// a ride on every automation target comes back bar for bar and event for event.
// Two events on one param at one tick must keep their order: the engine plays
// both and the second one wins.
static void test_rbs_arrangement_and_automation_round_trip() {
    Song a;
    a.tempo = 128.0;
    auto row = [](int b1, int b2, int d8, int d9, int rep) {
        SongStep r;
        r.bass1Pattern = b1; r.bass2Pattern = b2;
        r.drum808Pattern = d8; r.drum909Pattern = d9; r.repeats = rep;
        return r;
    };
    a.arrangement = { row(1, 2, 3, 4, 2), row(1, 5, 3, 4, 3),
                      row(7, 5, 0, 31, 1), row(7, 5, 0, 31, 2) };
    // The start state, which the tracks' tick-0 dumps carry.
    a.mixerInit.present = true;
    for (int c = 0; c < 4; ++c) {
        a.mixerInit.volume[c] = faderGain(70 + 10 * c, kChannelUnityGain);
        a.mixerInit.pan[c]    = panFromByte(20 + 30 * c);
    }
    a.effectInit.present      = true;
    a.effectInit.masterVolume = faderGain(70);        // Clodhopper's master byte
    a.effectInit.delaySend[1] = 50.0f / 127.0f;
    a.effectInit.distOn[2]    = true;
    a.effectInit.distortionOn = false;                // a bypassed unit stays bypassed
    a.effectInit.compTarget   = 1;
    a.effectInit.pcfTarget    = 3;
    a.effectInit.pcfOn        = true;
    a.effectInit.compressorOn = true;
    a.effectInit.compRatio     = 30.0f / 127.0f;
    a.effectInit.compThreshold = 90.0f / 127.0f;
    a.effectInit.delayFeedback = 20.0f / 127.0f;
    // A ride on every target, sorted by tick as the loader leaves them. The
    // mixer, DELY, PCF, DIST and COMP rides start after tick 0, because on
    // those tracks a tick-0 event IS the start state and loads into mixerInit /
    // effectInit rather than into the automation.
    using T = AutoTarget;
    a.automation = {
        {   0, T::Bass1,   3, 40 },
        {  16, T::Bass2,   5, 99 }, {  24, T::Drum808, 9, 60 }, { 24, T::Drum909, 22, 70 },
        {  32, T::Mixer,   1,  1 }, {  40, T::Pcf,     1, 70 }, { 48, T::Dist,     2, 100 },
        {  64, T::Bass1,   3, 10 }, {  64, T::Bass1,   3, 90 },  // one param, one tick
        {  64, T::Mixer,   6, 80 },
        {  72, T::Delay,   3, 50 }, {  80, T::Comp,    1, 30 },
        {  96, T::Mixer,   2,  3 }, { 100, T::Drum909, 0,  0 }, { 120, T::Drum808, 0, 1 },
    };

    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));
    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));

    // The arrangement, bar for bar. Nothing reaches the last bar, so the writer
    // pins the length with a pattern select there; the song must still be 8.
    auto perBar = [](const Song& s) {
        std::vector<std::array<int, 4>> v;
        for (const SongStep& r : s.arrangement)
            for (int i = 0; i < std::max(1, r.repeats); ++i)
                v.push_back({ r.bass1Pattern, r.bass2Pattern, r.drum808Pattern, r.drum909Pattern });
        return v;
    };
    CHECK(perBar(b).size() == 8);
    CHECK(perBar(a) == perBar(b));
    CHECK(b.arrangement.size() == 3);                 // the identical rows merged

    // Every timed ride, target by target and in order.
    const T all[] = { T::Bass1, T::Bass2, T::Drum808, T::Drum909, T::Mixer,
                      T::Pcf, T::Dist, T::Delay, T::Comp };
    for (T t : all) {
        std::vector<std::array<int, 3>> x, y;
        for (const AutoEvent& e : a.automation)
            if (e.target == t && e.tick > 0) x.push_back({ int(e.tick), e.param, e.value });
        for (const AutoEvent& e : b.automation)
            if (e.target == t && e.tick > 0) y.push_back({ int(e.tick), e.param, e.value });
        CHECK(x == y);
    }
    // The tick-0 rides survive with their values, and the writer's synthesised
    // dump fills in only the params they did not state.
    auto at0 = [](const Song& s, T t, int param) {
        int n = 0, v = -1;
        for (const AutoEvent& e : s.automation)
            if (e.tick == 0 && e.target == t && e.param == param) { ++n; v = e.value; }
        return n == 1 ? v : -1000 - n;
    };
    CHECK(at0(b, T::Bass1, 3) == 40);
    CHECK(at0(b, T::Bass1, 0) == 1);                  // unmuted at the start
    CHECK(at0(b, T::Drum909, 29) >= 0);               // the 909 dump runs to its flam byte

    // The start state, as bytes.
    for (int c = 0; c < 4; ++c) {
        CHECK(faderByteFromGain(b.mixerInit.volume[c], kChannelUnityGain) == 70 + 10 * c);
        CHECK(panByteFromPan(b.mixerInit.pan[c]) == 20 + 30 * c);
    }
    CHECK(faderByteFromGain(b.effectInit.masterVolume) == 70);
    CHECK(byteOf(b.effectInit.delaySend[1]) == 50);
    CHECK(b.effectInit.distOn[2] && !b.effectInit.distOn[0]);
    CHECK(!b.effectInit.distortionOn);
    CHECK(b.effectInit.compTarget == 1);
    CHECK(b.effectInit.pcfTarget == 3);
    CHECK(b.effectInit.pcfOn);
    CHECK(b.effectInit.compressorOn);
    CHECK(byteOf(b.effectInit.compRatio) == 30);
    CHECK(byteOf(b.effectInit.compThreshold) == 90);
    CHECK(byteOf(b.effectInit.delayFeedback) == 20);

    // And a second save of the reloaded song is the same file.
    std::vector<unsigned char> again;
    CHECK(saveRbs(b, again, &err));
    CHECK(again == buf);
}

// W2. DELY +2 is the grid switch (nonzero = straight) and +4 the wet-return
// pan. The writer left +2 at zero, so every straight delay reloaded as triplet,
// and fixed the pan at 64.
static void test_dely_grid_and_pan_round_trip() {
    for (int straight = 0; straight < 2; ++straight) {
        Song a;
        a.effectInit.present       = true;
        a.effectInit.delaySteps    = 3;
        a.effectInit.delayStraight = straight != 0;
        a.effectInit.delayPan      = straight ? 100 : 20;
        a.effectInit.delayFeedback = 45.0f / 127.0f;

        std::vector<unsigned char> buf;
        std::string err;
        CHECK(saveRbs(a, buf, &err));
        Song b;
        CHECK(loadRbs(buf.data(), buf.size(), b, &err));
        for (const EffectInit* fx : { &b.effectInit, &b.effectPanel }) {
            CHECK(fx->delayStraight == a.effectInit.delayStraight);
            CHECK(fx->delayPan == a.effectInit.delayPan);
            CHECK(fx->delaySteps == 3);
            CHECK(byteOf(fx->delayFeedback) == 45);
        }
        // The DELY track's tick-0 dump says the same, read from the file
        // itself: the loader consumes it into effectInit.
        int grid = -1, pan = -1;
        const std::vector<TrakEvents> tracks = trakEventsOf(buf);
        CHECK(tracks.size() == 9);
        if (tracks.size() == 9)
            for (const auto& e : tracks[5])
                if (e[0] == 0) {
                    if (e[1] == 2) grid = e[2];
                    if (e[1] == 4) pan  = e[2];
                }
        CHECK(grid == straight);
        CHECK(pan == a.effectInit.delayPan);
    }
}

// Review E3. In Song mode ReBirth plays the DELY TRACK: its tick-0 dump is the
// delay's start, not the DELY chunk, and its timed rides move the delay.
// Measured 2026-09-26 on four isolated fixtures whose chunk and track disagree
// on steps, feedback and pan (fixtures-2026-09-26/delay-chunk-vs-track). The
// delay used to play its chunk and ignore the track, tick 0 included. The chunk
// stays the panel, and the unit's own on/off, track param 0, round-trips.
static void test_delay_plays_its_track() {
    // A file with the E3 fixtures' two sides: the chunk says 2 steps, feedback
    // 0, pan 127 and straight; the track opens at 4 steps, 96, 64, triplet.
    Song a;
    a.tempo = 120.0;
    a.effectPanel.present = a.effectInit.present = true;
    a.effectPanel.delaySteps    = 2;     a.effectInit.delaySteps    = 4;
    a.effectPanel.delayStraight = true;  a.effectInit.delayStraight = false;
    a.effectPanel.delayFeedback = 0.0f;  a.effectInit.delayFeedback = 96.0f / 127.0f;
    a.effectPanel.delayPan      = 127;   a.effectInit.delayPan      = 64;
    // Rides at bar 2 on every knob, and the unit off at bar 3.
    using T = AutoTarget;
    a.automation = { { 64, T::Delay, 1, 6 }, { 64, T::Delay, 2, 1 }, { 64, T::Delay, 3, 30 },
                     { 64, T::Delay, 4, 20 }, { 96, T::Delay, 0, 0 } };
    SongStep row;
    row.repeats = 4;
    a.arrangement = { row };
    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));

    // The file itself, read without the loader: the DELY chunk is the panel and
    // track 5's tick-0 dump is the start, unit on.
    static const unsigned char kId[8] = { 'D', 'E', 'L', 'Y', 0, 0, 0, 8 };
    std::size_t at = 0;
    while (at + 16 <= buf.size() && std::memcmp(&buf[at], kId, 8) != 0) ++at;
    CHECK(at + 16 <= buf.size());
    if (at + 16 <= buf.size()) {
        const unsigned char* c = &buf[at + 8];
        CHECK(c[1] == 2 && c[2] == 1 && c[3] == 0 && c[4] == 127);
    }
    const std::vector<TrakEvents> tracks = trakEventsOf(buf);
    CHECK(tracks.size() == 9);
    int dump[5] = { -1, -1, -1, -1, -1 };
    if (tracks.size() == 9)
        for (const auto& e : tracks[5])
            if (e[0] == 0 && e[1] >= 0 && e[1] <= 4) dump[e[1]] = e[2];
    CHECK(dump[0] == 1 && dump[1] == 4 && dump[2] == 0 && dump[3] == 96 && dump[4] == 64);

    // The loader: the start state from the track, the panel from the chunk,
    // the tick-0 dump consumed and the timed rides kept.
    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));
    CHECK(b.effectInit.delayOn);
    CHECK(b.effectInit.delaySteps == 4 && !b.effectInit.delayStraight);
    CHECK(byteOf(b.effectInit.delayFeedback) == 96 && b.effectInit.delayPan == 64);
    CHECK(b.effectPanel.delaySteps == 2 && b.effectPanel.delayStraight);
    CHECK(byteOf(b.effectPanel.delayFeedback) == 0 && b.effectPanel.delayPan == 127);
    int at0 = 0, timed = 0;
    for (const AutoEvent& ev : b.automation)
        if (ev.target == T::Delay) (ev.tick == 0 ? at0 : timed)++;
    CHECK(at0 == 0 && timed == 5);

    // The engine starts from the track, and each ride lands at its tick.
    Engine e;
    e.prepare(44100.0);
    e.loadSong(b);
    Effect* d = e.delay();
    CHECK(d != nullptr);
    if (!d) return;
    auto byteAt = [d](const char* name, float scale) {
        return (int) std::lround(d->getParam(d->paramIndex(name)) * scale);
    };
    auto startsFromTrack = [&] {
        return byteAt("steps", 16.0f) == 4 && byteAt("grid", 1.0f) == 0
            && byteAt("feedback", 127.0f) == 96 && byteAt("pan", 127.0f) == 64;
    };
    CHECK(startsFromTrack());
    CHECK(e.delayUnit());
    e.play();
    StereoBlock out; out.resize(kBarFrames);
    const int step = kBarFrames / 16;
    e.process(out, kBarFrames);
    e.process(out, kBarFrames - step / 2);        // half a step short of bar 2
    CHECK(startsFromTrack());
    e.process(out, step);                         // past it
    CHECK(byteAt("steps", 16.0f) == 6 && byteAt("grid", 1.0f) == 1);
    CHECK(byteAt("feedback", 127.0f) == 30 && byteAt("pan", 127.0f) == 20);
    CHECK(e.delayUnit());
    e.process(out, kBarFrames);                   // past bar 3
    CHECK(!e.delayUnit());

    // delayOn round-trips through track param 0, and a second save is the
    // same file.
    Song c = b;
    c.effectInit.delayOn = false;
    std::vector<unsigned char> off, again;
    CHECK(saveRbs(c, off, &err));
    int on0 = -1;
    const std::vector<TrakEvents> offTracks = trakEventsOf(off);
    CHECK(offTracks.size() == 9);
    if (offTracks.size() == 9)
        for (const auto& ev : offTracks[5]) if (ev[0] == 0 && ev[1] == 0) on0 = ev[2];
    CHECK(on0 == 0);
    Song r;
    CHECK(loadRbs(off.data(), off.size(), r, &err));
    CHECK(!r.effectInit.delayOn);
    CHECK(saveRbs(r, again, &err) && again == off);
    Engine eo;
    eo.prepare(44100.0);
    eo.loadSong(r);
    CHECK(!eo.delayUnit());

    // A unit that is off adds nothing: a 909 hit sent to a switched-off delay
    // renders bit for bit as the same hit with no send at all, and the same
    // hit sent to a delay that is on does not.
    auto render = [](bool unitOn, float send) {
        Song s;
        s.tempo = 120.0;
        s.drum909.patterns[0].steps[0].setOn(0, true);
        s.effectInit.present = true;
        s.effectInit.delayOn = unitOn;
        s.effectInit.delaySteps = 2;
        s.effectInit.delayFeedback = 64.0f / 127.0f;
        s.effectInit.delaySend[kDrum909] = send;
        SongStep one;
        s.arrangement = { one };
        Engine en;
        en.prepare(44100.0);
        en.loadSong(s);
        en.play();
        StereoBlock o; o.resize(kBarFrames);
        en.process(o, kBarFrames);
        return o;
    };
    const StereoBlock offSent = render(false, 100.0f / 127.0f);
    const StereoBlock dry     = render(true, 0.0f);
    const StereoBlock onSent  = render(true, 100.0f / 127.0f);
    CHECK(offSent.left == dry.left && offSent.right == dry.right);
    CHECK(onSent.left != dry.left);
}

// Measured 2026-09-27 (fixtures-2026-09-27/delay-rides, dr_p0): switching the
// delay unit OFF clears it. ReBirth plays no echo while it is off, and after it
// is back on nothing from before the switch-off comes out. We used to freeze
// the ring and resume it, which replayed bar 0's tail two seconds late.
static void test_delay_switch_off_clears_it() {
    // One rimshot in bar 0 and one in bar 3, nothing between; steps 4 triplet,
    // feedback 96, so bar 0's echoes would run on through bars 1 and 2.
    auto render = [](bool rideOff) {
        Song s;
        s.tempo = 120.0;
        s.drum909.patterns.resize(2);                 // pattern 1 stays empty
        s.drum909.patterns[0].steps[0].setOn(5, true);
        s.effectInit.present = true;
        s.effectInit.delaySteps = 4;
        s.effectInit.delayFeedback = 96.0f / 127.0f;
        s.effectInit.delaySend[kDrum909] = 100.0f / 127.0f;
        SongStep hit, rest;
        rest.drum909Pattern = 1;
        s.arrangement = { hit, rest, rest, hit };
        if (rideOff)   // off at bar 1, back on at bar 2
            s.automation = { { 32, AutoTarget::Delay, 0, 0 }, { 64, AutoTarget::Delay, 0, 1 } };
        Engine en;
        en.prepare(44100.0);
        en.loadSong(s);
        en.play();
        StereoBlock o; o.resize(4 * kBarFrames);
        en.process(o, 4 * kBarFrames);
        return o;
    };
    auto peak = [](const StereoBlock& o, int from, int to) {
        float p = 0.0f;
        for (int i = from; i < to; ++i) p = std::max(p, std::max(std::fabs(o.left[i]), std::fabs(o.right[i])));
        return p;
    };
    const StereoBlock held = render(false), rode = render(true);
    CHECK(peak(held, kBarFrames, 3 * kBarFrames) > 1e-3f);   // the echoes run on
    CHECK(peak(rode, kBarFrames, 2 * kBarFrames) < 1e-5f);   // off: none
    CHECK(peak(rode, 2 * kBarFrames, 3 * kBarFrames) < 1e-5f);   // on again, and empty
    // Back on, it echoes what arrives: bar 3's hit, 0.667 s later.
    const int echo = 3 * kBarFrames + 29400;
    CHECK(peak(rode, echo, echo + 2000) > 1e-3f);
}

// Review 2026-09-27, the restart. Song mode plays from the song's recorded
// start, so play() puts back everything the mixer and effect tracks' tick-0
// dumps load into mixerInit and effectInit, which songs ride: the channels'
// faders, pans, sends and routing, the unit switches, and the PCF, distortion,
// compressor and delay knobs. A Stop then Play, or a host restart, used to
// start where the last ride left them (8 of the 16 demo songs for the delay
// alone). An unedited save after it is the plain save. Pattern mode is left
// alone, and so is the arrangement's wrap, which ReBirth does not do (E1).
static void test_song_mode_play_restores_the_start() {
    Song a;
    a.tempo = 120.0;
    a.mixerInit.present = true;
    a.effectInit.present = true;
    for (int c = 0; c < 4; ++c) {
        a.mixerInit.volume[c]     = faderGain(90 + 5 * c, kChannelUnityGain);
        a.mixerInit.pan[c]        = panFromByte(40 + 10 * c);
        a.effectInit.delaySend[c] = float(30 + c) / 127.0f;
    }
    EffectInit& fx = a.effectInit;
    fx.masterVolume  = faderGain(90);
    fx.distOn[kBass2] = true;
    fx.distortionOn  = true;
    fx.distAmount    = 20.0f / 127.0f;  fx.distTone  = 90.0f / 127.0f;
    fx.pcfTarget     = kDrum909;        fx.pcfOn     = true;
    fx.pcfFreq       = 53.0f / 127.0f;  fx.pcfQ      = 82.0f / 127.0f;
    fx.pcfAmt        = 68.0f / 127.0f;  fx.pcfDecay  = 67.0f / 127.0f;
    fx.pcfPattern    = 32;
    fx.compTarget    = kCompMaster;     fx.compressorOn = true;
    fx.compRatio     = 30.0f / 127.0f;  fx.compThreshold = 90.0f / 127.0f;
    fx.delaySteps    = 4;               fx.delayFeedback = 96.0f / 127.0f;
    // The panel disagrees with the start, as in G-Spot and Trakka Rekka, so
    // the save below has a real choice to get wrong.
    a.mixerPanel  = a.mixerInit;
    a.effectPanel = a.effectInit;
    a.mixerPanel.volume[kBass1]  = faderGain(70, kChannelUnityGain);
    a.effectPanel.pcfFreq        = 70.0f / 127.0f;
    a.effectPanel.delayPan       = 127;
    a.effectPanel.delayFeedback  = 0.0f;
    // A ride on every field the start state holds that a song can ride, in
    // bars 1 and 2.
    using T = AutoTarget;
    a.automation = {
        { 32, T::Mixer, 6, 50 },  { 32, T::Mixer, 7, 100 }, { 32, T::Mixer, 8, 0 },
        { 32, T::Mixer, 9, 1 },   { 32, T::Mixer, 15, 0 },  { 32, T::Mixer, 24, 120 },
        { 40, T::Mixer, 1, 3 },   { 40, T::Mixer, 2, 1 },
        { 48, T::Pcf, 0, 0 },  { 48, T::Pcf, 1, 10 }, { 48, T::Pcf, 2, 20 }, { 48, T::Pcf, 3, 30 },
        { 48, T::Pcf, 4, 5 },  { 48, T::Pcf, 5, 40 }, { 48, T::Pcf, 6, 1 },
        { 56, T::Dist, 0, 0 }, { 56, T::Dist, 1, 5 }, { 56, T::Dist, 2, 120 },
        { 64, T::Delay, 1, 6 }, { 64, T::Delay, 2, 1 }, { 64, T::Delay, 3, 30 },
        { 64, T::Delay, 4, 20 }, { 72, T::Delay, 0, 0 },
        { 60, T::Comp, 1, 100 }, { 60, T::Comp, 2, 10 },
    };
    SongStep row;
    row.repeats = 4;
    a.arrangement = { row };
    std::vector<unsigned char> file, plain;
    std::string err;
    CHECK(saveRbs(a, file, &err));
    Song loaded;
    CHECK(loadRbs(file.data(), file.size(), loaded, &err));
    CHECK(saveRbs(loaded, plain, &err));

    auto open = [&loaded](Engine& e) {
        e.prepare(44100.0);
        e.loadSong(loaded);
        e.play();
    };
    // Every field applySongStart sets, compared with a fresh open's.
    auto sameStart = [](Engine& x, Engine& y) {
        bool ok = x.masterVolume == y.masterVolume && x.compressorOn == y.compressorOn
               && x.compTarget() == y.compTarget() && x.pcfTarget() == y.pcfTarget()
               && x.pcfUnit() == y.pcfUnit() && x.distUnit() == y.distUnit()
               && x.delayUnit() == y.delayUnit()
               && x.activePcfPattern() == y.activePcfPattern();
        for (int c = 0; c < kNumChannels; ++c) {
            const MixerChannel& p = x.channel(c);
            const MixerChannel& q = y.channel(c);
            ok = ok && p.volume == q.volume && p.pan == q.pan && p.delaySend == q.delaySend
                    && p.distEnable == q.distEnable && p.pcfEnable == q.pcfEnable
                    && p.mute == q.mute;
        }
        const std::pair<Effect*, Effect*> units[] = { { x.pcf(), y.pcf() },
            { x.distortion(), y.distortion() }, { x.compressor(), y.compressor() },
            { x.delay(), y.delay() } };
        for (const auto& u : units)
            for (int i = 0; i < u.first->numParams(); ++i)
                ok = ok && u.first->getParam(i) == u.second->getParam(i);
        return ok;
    };
    auto save = [&loaded](const Engine& e) {
        std::vector<unsigned char> b;
        saveRbs(e.songForSave(loaded.title, loaded.author), b, nullptr);
        return b;
    };

    Engine fresh;
    open(fresh);
    // The fresh open is the start state, as bytes.
    CHECK(faderByteFromGain(fresh.channel(kBass1).volume, kChannelUnityGain) == 90);
    CHECK(panByteFromPan(fresh.channel(kDrum909).pan) == 70);
    CHECK(byteOf(fresh.channel(kBass1).delaySend) == 30);
    CHECK(fresh.channel(kBass2).distEnable && fresh.pcfTarget() == kDrum909);
    CHECK(fresh.compTarget() == kCompMaster && fresh.compressorOn);
    CHECK(fresh.pcfUnit() && fresh.distUnit() && fresh.delayUnit());
    CHECK(byteOf(fresh.pcf()->getParamByName("cutoff")) == 53);
    CHECK((int) std::lround(fresh.delay()->getParamByName("feedback") * 127.0f) == 96);
    CHECK(save(fresh) == plain);

    Engine e;
    open(e);
    StereoBlock out; out.resize(kBarFrames);
    for (int bar = 0; bar < 3; ++bar) e.process(out, kBarFrames);   // past every ride
    // The rides played...
    CHECK(faderByteFromGain(e.channel(kBass1).volume, kChannelUnityGain) == 50);
    CHECK(e.channel(kBass1).distEnable && !e.channel(kBass2).distEnable);
    CHECK(e.compTarget() == kBass2 && e.pcfTarget() == kBass1);
    CHECK(!e.pcfUnit() && !e.distUnit() && !e.delayUnit());
    CHECK(byteOf(e.pcf()->getParamByName("cutoff")) == 10);
    CHECK((int) std::lround(e.delay()->getParamByName("steps") * 16.0f) == 6);
    CHECK(byteOf(e.compressor()->getParamByName("ratio")) == 100);
    CHECK(byteOf(e.compressor()->getParamByName("threshold")) == 10);
    // ...and the GUI moved the master and the compressor, which no song
    // here rides off.
    e.masterVolume = 0.1f;
    e.compressorOn = false;
    e.compressor()->setParamByName("ratio", 0.9f);
    CHECK(!sameStart(e, fresh));

    // Pattern mode plays no automation and keeps the panel as the user left it.
    e.setSongMode(false);
    e.play();
    CHECK(e.pcfTarget() == kBass1 && !e.delayUnit() && e.masterVolume == 0.1f);

    // Song mode: Stop then Play starts from the song's start, all of it.
    e.setSongMode(true);
    e.stop();
    e.play();
    CHECK(sameStart(e, fresh));
    CHECK(save(e) == plain);
    // And plays the rides again from there.
    e.process(out, kBarFrames);
    e.process(out, kBarFrames);
    CHECK(e.pcfTarget() == kBass1 && !e.pcfUnit());
}

// Review 2026-09-27, the drums' reset with samples loaded. play() resets both
// drum machines, and with a sample set in (the app's normal state) their reset
// used to leave the sample voices ringing and the sample-path snare's noise
// filter holding its last state. So a song played after another differed from
// a fresh open, and a Stop then Play mid-tail differed from the first play.
static void test_drum_reset_with_samples_loaded() {
    // A stand-in set: three seconds of tone for every voice, longer than a bar,
    // and no noise takes, so both snares keep their synthesised noise burst.
    auto source = [](const std::string& name, int* sr) {
        *sr = 44100;
        if (name.rfind("808_n", 0) == 0 || name.rfind("909_sdn", 0) == 0)
            return std::vector<float>();
        std::vector<float> d(3 * 44100);
        for (std::size_t i = 0; i < d.size(); ++i) d[i] = 0.3f * (float) std::sin(0.03 * double(i));
        return d;
    };
    // BD, SD and a cymbal on each machine; song X puts its snares last in the
    // bar, so their noise filter is left mid-state when X stops.
    auto song = [](int sdStep) {
        Song s;
        s.tempo = 120.0;
        for (DrumPattern* p : { &s.drum808.patterns[0], &s.drum909.patterns[0] }) {
            p->steps[0].setOn(0, true);
            p->steps[sdStep].setOn(1, true);
        }
        s.drum808.patterns[0].steps[8].setOn(12, true);   // 808 CY
        s.drum909.patterns[0].steps[8].setOn(9, true);    // 909 crash
        s.arrangement = { SongStep{}, SongStep{} };
        return s;
    };
    const Song x = song(12), y = song(4);
    auto open = [&source](Engine& e, const Song& s) {
        e.loadSong(s);
        CHECK(loadSampleSetFrom(e, source) > 0);
        e.play();
    };
    auto run = [](Engine& e, int frames) {
        StereoBlock o; o.resize(frames);
        e.process(o, frames);
        return o;
    };

    Engine fresh;
    fresh.prepare(44100.0);
    open(fresh, y);
    const StereoBlock want = run(fresh, 2 * kBarFrames);

    // Y opened after X has played a bar.
    Engine after;
    after.prepare(44100.0);
    open(after, x);
    run(after, kBarFrames);
    open(after, y);
    const StereoBlock got = run(after, 2 * kBarFrames);
    CHECK(got.left == want.left && got.right == want.right);

    // Stop mid-tail, a moment's silence, then Play: the tails do not carry on.
    Engine again;
    again.prepare(44100.0);
    open(again, y);
    run(again, kBarFrames + kBarFrames / 4);
    again.stop();
    run(again, kBarFrames / 4);
    again.play();
    const StereoBlock replay = run(again, 2 * kBarFrames);
    CHECK(replay.left == want.left && replay.right == want.right);
}

// Review follow-up F5(b). A 303 step can carry BOTH octave bits (0x0C). They
// cancel, so it plays at octave 0, and 126 steps in 7 demo songs have them; the
// writer put them back as a plain 0. An octave the step really has wins.
static void test_303_both_octave_bits_round_trip() {
    Song a;
    BassPattern& p = a.bass1.patterns[0];
    p.steps[0] = BassStep{ true,  3, 0, false, false, true };   // 0x0C on a note
    p.steps[1] = BassStep{ false, 5, 0, false, true,  true };   // and on a tied rest
    p.steps[2] = BassStep{ true,  7, 1, false, false, true };   // octave up, edited
    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));
    // The flags bytes of pattern 0, straight from the first 303 chunk (1097
    // bytes): a 9-byte header, then [shuffle][length] and [note][flags] pairs.
    static const unsigned char kId[8] = { '3', '0', '3', ' ', 0, 0, 0x04, 0x49 };
    std::size_t at = 0;
    while (at + 8 < buf.size() && std::memcmp(&buf[at], kId, 8) != 0) ++at;
    CHECK(at + 8 + 9 + 2 + 6 < buf.size());
    if (at + 8 + 9 + 2 + 6 < buf.size()) {
        const unsigned char* flags = &buf[at + 8 + 9 + 3];
        CHECK(flags[0] == 0x0C);
        CHECK(flags[2] == (0x10 | 0x0C | 0x01));
        CHECK(flags[4] == 0x04);
    }
    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));
    const BassPattern& q = b.bass1.at(0);
    CHECK(q.steps[0].octBoth && q.steps[0].octave == 0 && q.steps[0].note == 3);
    CHECK(q.steps[1].octBoth && q.steps[1].octave == 0 && q.steps[1].slide && !q.steps[1].gate);
    CHECK(!q.steps[2].octBoth && q.steps[2].octave == 1);
    CHECK(!q.steps[3].octBoth);
}

// Review follow-up F5(a). ReBirth writes a tick-0 dump from the highest param
// down, the pattern select included: all 64 instrument tracks of the demo songs
// end theirs 2, 1, 0. The writer put the select after param 0.
static void test_tick0_select_in_descending_slot() {
    Song a;
    SongStep row;
    row.bass1Pattern = 3; row.bass2Pattern = 4; row.drum808Pattern = 5; row.drum909Pattern = 6;
    row.repeats = 2;
    a.arrangement = { row };
    std::vector<unsigned char> buf;
    std::string err;
    CHECK(saveRbs(a, buf, &err));
    const std::vector<TrakEvents> tracks = trakEventsOf(buf);
    CHECK(tracks.size() == 9);
    for (std::size_t t = 1; t <= 4 && t < tracks.size(); ++t) {
        std::vector<int> params;
        for (const auto& e : tracks[t]) if (e[0] == 0) params.push_back(e[1]);
        CHECK(params.size() >= 3 && params.back() == 0);
        for (std::size_t i = 1; i < params.size(); ++i) CHECK(params[i - 1] > params[i]);
    }
    Song b;
    CHECK(loadRbs(buf.data(), buf.size(), b, &err));
    CHECK(b.arrangement.size() == 1 && b.arrangement[0].bass1Pattern == 3
          && b.arrangement[0].drum909Pattern == 6);
}

// W3. The GUI edits the engine, not the song, so a save has to read the live
// panel back. A captured song keeps the edits in its PANEL (the chunks), and
// its start state (the tracks' tick-0 dumps) keeps what the song was loaded
// with, which is how ReBirth separates the two.
static void test_live_panel_capture() {
    // A song as loaded from a file: panel and start state present and equal.
    Song src;
    src.tempo = 125.0;
    src.mixerInit.present = true;
    for (int c = 0; c < 4; ++c) {
        src.mixerInit.volume[c] = faderGain(100, kChannelUnityGain);
        src.mixerInit.pan[c]    = panFromByte(64);
    }
    src.effectInit.present       = true;
    src.effectInit.masterVolume  = faderGain(100);
    src.effectInit.delaySteps    = 3;
    src.effectInit.delayPan      = 64;
    src.effectInit.delayFeedback = 40.0f / 127.0f;
    src.effectInit.pcfFreq       = 60.0f / 127.0f;
    src.effectInit.distAmount    = 30.0f / 127.0f;
    src.effectInit.compRatio     = 20.0f / 127.0f;
    src.bass1Knobs.present = true;
    src.bass1Knobs.cutoff  = 50.0f / 127.0f;
    src.drum909Knobs.present = true;
    src.drum909Knobs.bdTune  = 64.0f / 127.0f;
    src.drum808Knobs.present = true;
    src.drum808Knobs.level[1] = 100.0f / 127.0f;
    std::vector<unsigned char> img;
    std::string err;
    CHECK(saveRbs(src, img, &err));
    Song loaded;
    CHECK(loadRbs(img.data(), img.size(), loaded, &err));

    Engine e;
    e.prepare(44100.0);
    e.setInstrument(kBass1, makeBass303(Bass303Engine::Native303Tpt));
    e.loadSong(loaded);

    // Edit through the engine, exactly as the mixer strips and dials do.
    e.setTempo(133.0);
    e.masterVolume = 0.72f * faderGain(85);
    e.channel(kBass1).volume    = faderGain(80, kChannelUnityGain);
    e.channel(kBass2).pan       = panFromByte(20);
    e.channel(kDrum808).delaySend = 90.0f / 127.0f;
    e.setDistEnable(kDrum909, true);
    e.setPcfTarget(kBass2);
    e.setCompTarget(kCompMaster);
    e.pcf()->setParamByName("cutoff", 110.0f / 127.0f);
    e.pcf()->setParamByName("pattern", 12.0f / 54.0f);
    e.delay()->setParamByName("grid", 1.0f);
    e.delay()->setParamByName("pan", 100.0f / 127.0f);
    e.delay()->setParamByName("steps", 5.0f / 16.0f);
    e.distortion()->setParamByName("amount", 99.0f / 127.0f);
    e.compressor()->setParamByName("ratio", 77.0f / 127.0f);
    e.instrument(kBass1)->setParamByName("cutoff", 12.0f / 127.0f);
    e.instrument(kDrum909)->setParamByName("bd_tune", 33.0f / 127.0f);
    e.instrument(kDrum808)->setParamByName("level_Snare", 44.0f / 127.0f);

    Song out = e.song();
    e.captureLivePanel(out);
    CHECK(saveRbs(out, img, &err));
    Song b;
    CHECK(loadRbs(img.data(), img.size(), b, &err));

    // The panel holds every edit.
    CHECK(std::fabs(b.tempo - 133.0) < 1e-6);
    CHECK(faderByteFromGain(b.effectPanel.masterVolume) == 85);
    CHECK(faderByteFromGain(b.mixerPanel.volume[kBass1], kChannelUnityGain) == 80);
    CHECK(panByteFromPan(b.mixerPanel.pan[kBass2]) == 20);
    CHECK(byteOf(b.effectPanel.delaySend[kDrum808]) == 90);
    CHECK(b.effectPanel.distOn[kDrum909]);
    CHECK(b.effectPanel.pcfTarget == kBass2);
    CHECK(b.effectPanel.compTarget == kCompMaster);
    CHECK(byteOf(b.effectPanel.pcfFreq) == 110);
    CHECK(b.effectPanel.pcfPattern == 12);
    CHECK(b.effectPanel.delayStraight);
    CHECK(b.effectPanel.delayPan == 100);
    CHECK(b.effectPanel.delaySteps == 5);
    CHECK(byteOf(b.effectPanel.distAmount) == 99);
    CHECK(byteOf(b.effectPanel.compRatio) == 77);
    CHECK(byteOf(b.bass1Knobs.cutoff) == 12);
    CHECK(byteOf(b.drum909Knobs.bdTune) == 33);
    CHECK(byteOf(b.drum808Knobs.level[1]) == 44);

    // The start state still holds what the song was loaded with, for every
    // field a track carries.
    CHECK(faderByteFromGain(b.mixerInit.volume[kBass1], kChannelUnityGain) == 100);
    CHECK(panByteFromPan(b.mixerInit.pan[kBass2]) == 64);
    CHECK(byteOf(b.effectInit.delaySend[kDrum808]) == 0);
    CHECK(!b.effectInit.distOn[kDrum909]);
    CHECK(b.effectInit.pcfTarget == -1);
    CHECK(b.effectInit.compTarget == -1);
    CHECK(byteOf(b.effectInit.pcfFreq) == 60);
    CHECK(byteOf(b.effectInit.distAmount) == 30);
    CHECK(byteOf(b.effectInit.compRatio) == 20);
    int cutoffAt0 = -1;
    for (const AutoEvent& ev : b.automation)
        if (ev.tick == 0 && ev.target == AutoTarget::Bass1 && ev.param == 3) cutoffAt0 = ev.value;
    CHECK(cutoffAt0 == 50);
    // The master has no track, so what plays is the chunk, which now holds the
    // panel. The delay starts from its track's tick-0 dump, as loaded.
    CHECK(faderByteFromGain(b.effectInit.masterVolume) == 85);
    CHECK(!b.effectInit.delayStraight);
    CHECK(b.effectInit.delayPan == 64);
    CHECK(b.effectInit.delaySteps == 3);

    // The engine's own song is untouched by a capture.
    CHECK(e.song().effectPanel.pcfPattern == loaded.effectPanel.pcfPattern);
    CHECK(byteOf(e.song().bass1Knobs.cutoff) == 50);
}

// The sample loader sets a machine's sd_snap to 0 to mute the synthesised
// snare layer (review P3), and then the device's sd_snap is not the knob, so
// the capture keeps the song's own value. It does that only when both the
// snare body and its noise take load, per machine, so the capture keys on the
// engine's flags rather than on whether any sample loaded: a set with only a
// kick repurposes nothing, and a snappy edit made under it is saved.
static void test_capture_keeps_sd_snap_under_samples() {
    Song s;
    s.drum808Knobs.present  = true;
    s.drum808Knobs.sdSnappy = 70.0f / 127.0f;
    s.drum909Knobs.present  = true;
    s.drum909Knobs.sdSnappy = 80.0f / 127.0f;
    // A stand-in sample set: the kicks always, the snare bodies and noise
    // takes (808_sd*, 808_n*, 909_sd*, 909_sdn*) only when asked.
    auto source = [](bool snares) {
        return [snares](const std::string& name, int* sr) {
            *sr = 44100;
            auto starts = [&name](const char* p) { return name.rfind(p, 0) == 0; };
            const bool kick  = starts("808_bd") || starts("909_bd");
            const bool snare = starts("808_sd") || starts("808_n") || starts("909_sd");
            return (kick || (snares && snare)) ? std::vector<float>(64, 0.1f)
                                               : std::vector<float>();
        };
    };
    Engine e;
    e.prepare(44100.0);

    e.loadSong(s);
    CHECK(loadSampleSetFrom(e, source(true)) > 0);
    CHECK(e.sdSnapRepurposed808 && e.sdSnapRepurposed909);
    e.instrument(kDrum909)->setParamByName("bd_tune", 0.25f);
    Song withSnares = e.song();
    e.captureLivePanel(withSnares);
    CHECK(withSnares.drum808Knobs.sdSnappy == s.drum808Knobs.sdSnappy);
    CHECK(withSnares.drum909Knobs.sdSnappy == s.drum909Knobs.sdSnappy);
    CHECK(withSnares.drum909Knobs.bdTune == 0.25f);            // the rest is still read

    e.loadSong(s);
    CHECK(!e.sdSnapRepurposed808 && !e.sdSnapRepurposed909);    // a new song clears them
    CHECK(loadSampleSetFrom(e, source(false)) == 2);            // the two kicks
    CHECK(!e.sdSnapRepurposed808 && !e.sdSnapRepurposed909);
    e.instrument(kDrum808)->setParamByName("sd_snap", 20.0f / 127.0f);
    e.instrument(kDrum909)->setParamByName("sd_snap", 30.0f / 127.0f);
    Song kickOnly = e.song();
    e.captureLivePanel(kickOnly);
    CHECK(byteOf(kickOnly.drum808Knobs.sdSnappy) == 20);
    CHECK(byteOf(kickOnly.drum909Knobs.sdSnappy) == 30);
}

// A song with no arrangement is saved with one row of the patterns playing,
// rather than the pattern-0 row the writer falls back to.
static void test_capture_fills_an_empty_arrangement() {
    Engine e;
    e.prepare(44100.0);
    e.loadSong(Song());
    e.selectBassPattern(0, 5);
    e.selectBassPattern(1, 6);
    e.selectDrumPattern(0, 7);
    e.selectDrumPattern(1, 8);
    Song out = e.song();
    CHECK(out.arrangement.empty());
    e.captureLivePanel(out);
    CHECK(out.arrangement.size() == 1);
    if (out.arrangement.size() == 1) {
        const SongStep& r = out.arrangement[0];
        CHECK(r.bass1Pattern == 5 && r.bass2Pattern == 6);
        CHECK(r.drum808Pattern == 7 && r.drum909Pattern == 8);
        CHECK(r.repeats == 1);
    }
}

// Review follow-up F1. After loadSong and play() the engine holds the song's
// START state, the tick-0 dumps, and in most real songs that is not the panel
// the chunks hold (G-Spot's PCF chunk says Freq 70, Pattern 24, Decay 90 where
// its track starts at 53, 32, 67). An unedited open, play and save must write
// the file back as loaded, an edit must change exactly the byte of the field
// edited, and a value the automation has moved must reach the panel.
static void test_capture_keeps_an_unedited_panel() {
    // A file whose chunks and tick-0 dumps disagree on every kind of field.
    Song a;
    a.tempo = 131.0;
    a.mixerInit.present = a.mixerPanel.present = true;
    a.effectInit.present = a.effectPanel.present = true;
    for (int c = 0; c < 4; ++c) {
        a.mixerPanel.volume[c]     = faderGain(90 + c, kChannelUnityGain);
        a.mixerInit.volume[c]      = faderGain(110 - c, kChannelUnityGain);
        a.mixerPanel.pan[c]        = panFromByte(30 + c);
        a.mixerInit.pan[c]         = panFromByte(90 + c);
        a.effectPanel.delaySend[c] = float(10 + c) / 127.0f;
        a.effectInit.delaySend[c]  = float(60 + c) / 127.0f;
    }
    a.effectPanel.pcfFreq    = 70.0f / 127.0f;  a.effectInit.pcfFreq    = 53.0f / 127.0f;
    a.effectPanel.pcfPattern = 24;              a.effectInit.pcfPattern = 32;
    a.effectPanel.pcfDecay   = 90.0f / 127.0f;  a.effectInit.pcfDecay   = 67.0f / 127.0f;
    a.effectPanel.pcfTarget  = -1;              a.effectInit.pcfTarget  = kDrum909;
    a.effectPanel.compTarget = kBass2;          a.effectInit.compTarget = kCompMaster;
    a.effectPanel.distAmount = 40.0f / 127.0f;  a.effectInit.distAmount = 0.0f;
    a.effectPanel.compRatio  = 20.0f / 127.0f;  a.effectInit.compRatio  = 80.0f / 127.0f;
    // Trakka Rekka's DELY chunk says feedback 0, pan 127; its track opens at
    // 36 and 63. The steps disagree too, as in the E3 fixtures.
    a.effectPanel.delayFeedback = 0.0f;         a.effectInit.delayFeedback = 36.0f / 127.0f;
    a.effectPanel.delayPan      = 127;          a.effectInit.delayPan      = 63;
    a.effectPanel.delaySteps    = 2;            a.effectInit.delaySteps    = 4;
    a.bass1Knobs.present   = true;
    a.bass1Knobs.cutoff    = 120.0f / 127.0f;   // Calm's chunk: 120 and 127,
    a.bass1Knobs.resonance = 1.0f;              // its track starts at 0 and 0
    a.drum909Knobs.present  = true;
    a.drum909Knobs.sdSnappy = 1.0f;             // Trakka's snare: 127, starts at 0
    a.drum909Knobs.flam     = 64.0f / 127.0f;
    a.drum808Knobs.present  = true;
    a.drum808Knobs.sharedPrimary[SC_LT] = true;
    using T = AutoTarget;
    a.automation = {
        { 0, T::Bass1, 0, 0 }, { 0, T::Bass1, 3, 0 }, { 0, T::Bass1, 4, 0 },
        { 0, T::Drum808, 11, 0 }, { 0, T::Drum909, 10, 0 }, { 0, T::Drum909, 29, 20 },
        { 8, T::Mixer, 6, 50 },                 // bass 1's fader, ridden at step 4
    };
    std::vector<unsigned char> file, plain;
    std::string err;
    CHECK(saveRbs(a, file, &err));
    Song loaded;
    CHECK(loadRbs(file.data(), file.size(), loaded, &err));
    CHECK(saveRbs(loaded, plain, &err));

    auto open = [&loaded](Engine& e) {
        e.prepare(44100.0);
        e.setInstrument(kBass1, makeBass303(Bass303Engine::Native303Tpt));
        e.loadSong(loaded);
        e.play();
    };
    auto save = [](const Engine& e) {
        Song s = e.song();
        e.captureLivePanel(s);
        std::vector<unsigned char> b;
        saveRbs(s, b, nullptr);
        return b;
    };
    auto differing = [&plain](const std::vector<unsigned char>& b) {
        if (b.size() != plain.size()) return -1;
        int n = 0;
        for (std::size_t i = 0; i < b.size(); ++i) n += b[i] != plain[i];
        return n;
    };

    { Engine e; open(e); CHECK(save(e) == plain); }

    {
        Engine e; open(e);
        e.pcf()->setParamByName("cutoff", 100.0f / 127.0f);
        const std::vector<unsigned char> b = save(e);
        CHECK(differing(b) == 1);
        Song r;
        CHECK(loadRbs(b.data(), b.size(), r, &err));
        CHECK(byteOf(r.effectPanel.pcfFreq) == 100);
        CHECK(byteOf(r.effectInit.pcfFreq) == 53);   // the start is the recorded song
        CHECK(r.effectPanel.pcfPattern == 24);
    }
    { Engine e; open(e); e.setFlam909(30.0f / 127.0f); CHECK(differing(save(e)) == 1); }
    {
        Engine e; open(e);
        e.delay()->setParamByName("feedback", 100.0f / 127.0f);
        const std::vector<unsigned char> b = save(e);
        CHECK(differing(b) == 1);
        Song r;
        CHECK(loadRbs(b.data(), b.size(), r, &err));
        CHECK(byteOf(r.effectPanel.delayFeedback) == 100);
        CHECK(byteOf(r.effectInit.delayFeedback) == 36);  // the start is the recorded song
        CHECK(r.effectPanel.delayPan == 127 && r.effectInit.delayPan == 63);
    }
    {
        Engine e; open(e);
        e.instrument(kBass1)->setParamByName("resonance", 60.0f / 127.0f);
        CHECK(differing(save(e)) == 1);
    }

    // Played past the fader ride, a save holds the fader where it now is.
    {
        Engine e; open(e);
        const int frames = (int) (6 * 44100.0 * 15.0 / 131.0);   // six steps
        StereoBlock blk(512);
        for (int done = 0; done < frames; done += 512) e.process(blk, std::min(512, frames - done));
        const std::vector<unsigned char> b = save(e);
        CHECK(differing(b) == 1);
        Song r;
        CHECK(loadRbs(b.data(), b.size(), r, &err));
        CHECK(faderByteFromGain(r.mixerPanel.volume[kBass1], kChannelUnityGain) == 50);
        CHECK(faderByteFromGain(r.mixerInit.volume[kBass1], kChannelUnityGain) == 110);
    }
}

// Review follow-up F4. Every save in the app goes through Engine::songForSave:
// saveSongFile for .rbs, and captureSession for .rbsx sessions and the DAW
// state. This drives it the way PluginProcessor does, with edits made through
// the engine as the GUI makes them, then saves, reloads and checks each edit,
// both as a plain .rbs and inside a session image.
static void test_song_for_save() {
    Engine e;
    e.prepare(44100.0);
    {
        Song s;
        s.tempo = 120.0;
        s.title = "Loaded Title";
        s.drum909.patterns[0].steps[0].setOn(0, true);
        e.loadSong(s);
    }
    e.play();
    e.song().drum909.patterns[0].steps[4].setOn(1, true);                // the pattern grid
    e.channel(kDrum909).volume = faderGain(77, kChannelUnityGain);        // a mixer strip
    e.pcf()->setParamByName("resonance", 99.0f / 127.0f);                 // an effect dial
    e.instrument(kDrum909)->setParamByName("bd_decay", 11.0f / 127.0f);   // a voice dial
    e.setFlam909(40.0f / 127.0f);                                         // the edit bar

    Session sess;
    sess.song = e.songForSave("Saved Title", "Saved Author");
    CHECK(e.song().title == "Loaded Title");            // the engine's own song is untouched
    std::vector<unsigned char> img, session;
    std::string err;
    CHECK(saveRbs(sess.song, img, &err));
    CHECK(saveRbsxToMemory(session, sess, &err));
    Session back;
    CHECK(loadRbsxFromMemory(session.data(), session.size(), back, &err));
    Song plain;
    CHECK(loadRbs(img.data(), img.size(), plain, &err));
    for (Song* b : { &plain, &back.song }) {
        CHECK(b->title == "Saved Title");
        CHECK(b->author == "Saved Author");
        CHECK(b->drum909.at(0).steps[4].voiceOn(1));
        CHECK(faderByteFromGain(b->mixerPanel.volume[kDrum909], kChannelUnityGain) == 77);
        CHECK(byteOf(b->effectPanel.pcfQ) == 99);
        CHECK(byteOf(b->drum909Knobs.bdDecay) == 11);
        CHECK(byteOf(b->drum909Knobs.flam) == 40);
    }
}

// .rbsx round-trip, and the property that makes the design work: the embedded
// RSNG image is a real .rbs, so exporting is extraction rather than a second
// serialisation.
static void test_rbsx_round_trip(const char* tmp) {
    Session a;
    a.song.title = "Session Test";
    a.song.tempo = 141.0;
    a.song.loopBars = 9;
    a.song.skinName = "Metallicon+";
    a.song.bass1.patterns[0].steps[0] = BassStep{ true, 5, 1, true, false };
    a.song.drum808.patterns[0].steps[3].setOn(2, true);
    a.env.sampleDir = "C:/samples/set one";
    a.env.modName   = "Metallicon+";
    a.env.modPath   = "C:/mods/Metallicon.rbm";

    std::string err;
    CHECK(saveRbsx(tmp, a, &err));

    Session b;
    CHECK(loadRbsx(tmp, b, &err));
    CHECK(b.version == kRbsxVersion);
    CHECK(b.song.title == a.song.title);
    CHECK(std::fabs(b.song.tempo - a.song.tempo) < 0.01);
    CHECK(b.song.loopBars == a.song.loopBars);
    CHECK(b.song.skinName == a.song.skinName);
    CHECK(b.song.bass1.at(0).steps[0].gate);
    CHECK(b.song.bass1.at(0).steps[0].note == 5);
    CHECK(b.song.bass1.at(0).steps[0].octave == 1);
    CHECK(b.song.bass1.at(0).steps[0].accent);
    CHECK(b.song.drum808.at(0).steps[3].voiceOn(2));
    // environment survives (paths with spaces included)
    CHECK(b.env.sampleDir == a.env.sampleDir);
    CHECK(b.env.modName   == a.env.modName);
    CHECK(b.env.modPath   == a.env.modPath);

    // The embedded image must itself be a loadable .rbs.
    std::vector<unsigned char> img;
    CHECK(extractRbsImage(tmp, img, &err));
    CHECK(img.size() > 1000);
    Song direct;
    CHECK(loadRbs(img.data(), img.size(), direct, &err));
    CHECK(direct.title == a.song.title);
    CHECK(direct.skinName == a.song.skinName);
    std::remove(tmp);
}

// The 909's own knob and pattern round-trip. Three controls were being decoded
// but never WRITTEN (the mid and hi tom Tune and Decay, the ride Tune, the flam
// amount), so a save silently flattened them, and the flam CELL value was written
// back as a plain accent. The 808 test below covers its own header; this covers
// the 909's, and the per-step flam flag that nothing else pins.
static void test_909_knob_and_flam_round_trip() {
    Song a;
    a.drum909Knobs.present = true;
    a.drum909Knobs.accent  = 0.42f;
    for (int i = 0; i < 11; ++i) a.drum909Knobs.level[i] = float(i + 2) / 16.0f;
    a.drum909Knobs.bdTune = 0.1f; a.drum909Knobs.bdAttack = 0.2f; a.drum909Knobs.bdDecay = 0.3f;
    a.drum909Knobs.sdTune = 0.4f; a.drum909Knobs.sdTone   = 0.5f; a.drum909Knobs.sdSnappy = 0.6f;
    // Distinct per-tom values catch the columns being collapsed onto the low tom.
    a.drum909Knobs.ltTune  = 0.15f; a.drum909Knobs.mtTune  = 0.55f; a.drum909Knobs.htTune  = 0.85f;
    a.drum909Knobs.ltDecay = 0.25f; a.drum909Knobs.mtDecay = 0.65f; a.drum909Knobs.htDecay = 0.95f;
    a.drum909Knobs.chDecay = 0.33f; a.drum909Knobs.ohDecay = 0.66f;
    // Crash and ride tune must not collapse onto one another.
    a.drum909Knobs.cymTune = 0.22f; a.drum909Knobs.rdTune = 0.77f;
    a.drum909Knobs.flam    = 0.88f;

    // Step 0 plain, step 4 accented, step 8 FLAM: all three cell values at once.
    DrumPattern& p = a.drum909.at(0);
    p.steps[0].setOn(0, true);
    p.steps[4].setOn(0, true); p.steps[4].setAccent(0, true);
    p.steps[8].setOn(0, true); p.steps[8].setFlam(0, true);

    std::vector<unsigned char> img;
    std::string err;
    CHECK(saveRbs(a, img, &err));
    Song b;
    CHECK(loadRbs(img.data(), img.size(), b, &err));
    CHECK(b.drum909Knobs.present);

    auto near = [](float x, float y) { return std::fabs(x - y) <= 1.0f / 127.0f + 1e-4f; };
    CHECK(near(b.drum909Knobs.bdAttack, a.drum909Knobs.bdAttack));
    CHECK(near(b.drum909Knobs.sdTone,   a.drum909Knobs.sdTone));
    CHECK(near(b.drum909Knobs.ltTune,   a.drum909Knobs.ltTune));
    CHECK(near(b.drum909Knobs.mtTune,   a.drum909Knobs.mtTune));
    CHECK(near(b.drum909Knobs.htTune,   a.drum909Knobs.htTune));
    CHECK(near(b.drum909Knobs.ltDecay,  a.drum909Knobs.ltDecay));
    CHECK(near(b.drum909Knobs.mtDecay,  a.drum909Knobs.mtDecay));
    CHECK(near(b.drum909Knobs.htDecay,  a.drum909Knobs.htDecay));
    CHECK(near(b.drum909Knobs.cymTune,  a.drum909Knobs.cymTune));
    CHECK(near(b.drum909Knobs.rdTune,   a.drum909Knobs.rdTune));
    CHECK(near(b.drum909Knobs.flam,     a.drum909Knobs.flam));
    // The three cell values must survive distinctly, and must not bleed together.
    const DrumPattern& q = b.drum909.at(0);
    CHECK(q.steps[0].voiceOn(0) && !q.steps[0].voiceAccent(0) && !q.steps[0].voiceFlam(0));
    CHECK(q.steps[4].voiceOn(0) &&  q.steps[4].voiceAccent(0) && !q.steps[4].voiceFlam(0));
    CHECK(q.steps[8].voiceOn(0) && !q.steps[8].voiceAccent(0) &&  q.steps[8].voiceFlam(0));
}

// The drum KNOB HEADER round-trip. The writer built this header with a running
// offset counter, which drifted: it never emitted the 808's five shared-column
// switch bytes, so everything from byte 19 on was written one or two bytes
// early. Nothing caught it, because the .rbs round-trip harness compares
// patterns and tempo but not knobs. This pins every field, at both ends.
static void test_drum_knob_header_round_trip() {
    Song a;
    a.drum808Knobs.present = true;
    a.drum808Knobs.accent  = 0.5f;
    for (int i = 0; i < 11; ++i) a.drum808Knobs.level[i] = float(i + 1) / 16.0f;
    a.drum808Knobs.bdTune = 0.1f;  a.drum808Knobs.bdDecay = 0.2f;
    a.drum808Knobs.sdTone = 0.3f;  a.drum808Knobs.sdSnappy = 0.4f;
    // Distinct per-tom tunes catch a byte swap between the three columns.
    a.drum808Knobs.ltTune = 0.6f; a.drum808Knobs.mtTune = 0.35f; a.drum808Knobs.htTune = 0.72f;
    a.drum808Knobs.cymTune = 0.7f; a.drum808Knobs.crDecay = 0.8f;
    a.drum808Knobs.ohDecay = 0.9f;
    // Alternating switches catch an off-by-one that a uniform pattern hides.
    a.drum808Knobs.sharedPrimary[SC_LT] = true;
    a.drum808Knobs.sharedPrimary[SC_MT] = false;
    a.drum808Knobs.sharedPrimary[SC_HT] = true;
    a.drum808Knobs.sharedPrimary[SC_RS] = false;
    a.drum808Knobs.sharedPrimary[SC_CP] = true;

    std::vector<unsigned char> img;
    std::string err;
    CHECK(saveRbs(a, img, &err));
    Song b;
    CHECK(loadRbs(img.data(), img.size(), b, &err));
    CHECK(b.drum808Knobs.present);

    auto near = [](float x, float y) { return std::fabs(x - y) <= 1.0f / 127.0f + 1e-4f; };
    for (int i = 0; i < 11; ++i) CHECK(near(b.drum808Knobs.level[i], a.drum808Knobs.level[i]));
    CHECK(near(b.drum808Knobs.bdTune,  a.drum808Knobs.bdTune));
    CHECK(near(b.drum808Knobs.bdDecay, a.drum808Knobs.bdDecay));
    CHECK(near(b.drum808Knobs.sdTone,  a.drum808Knobs.sdTone));
    CHECK(near(b.drum808Knobs.sdSnappy,a.drum808Knobs.sdSnappy));
    CHECK(near(b.drum808Knobs.ltTune, a.drum808Knobs.ltTune));
    CHECK(near(b.drum808Knobs.mtTune, a.drum808Knobs.mtTune));
    CHECK(near(b.drum808Knobs.htTune, a.drum808Knobs.htTune));
    CHECK(near(b.drum808Knobs.cymTune, a.drum808Knobs.cymTune));
    CHECK(near(b.drum808Knobs.crDecay, a.drum808Knobs.crDecay));
    CHECK(near(b.drum808Knobs.ohDecay, a.drum808Knobs.ohDecay));
    for (int c = 0; c < SC_COUNT; ++c)
        CHECK(b.drum808Knobs.sharedPrimary[c] == a.drum808Knobs.sharedPrimary[c]);
}

// A shared column must actually sound its ALTERNATE voice when the switch is
// off. Ignoring these bytes made 17 of the 18 shipped songs play at least one
// column with the wrong instrument.
static void test_shared_column_routes_to_alternate() {
    auto peakWith = [](bool primary) {
        Song s;
        s.tempo = 120.0;
        s.drum808Knobs.present = true;
        for (int c = 0; c < SC_COUNT; ++c) s.drum808Knobs.sharedPrimary[c] = primary;
        s.drum808.patterns[0].length = 16;
        s.drum808.patterns[0].steps[0].setOn(2, true);       // the LT column
        Engine e;
        e.prepare(44100.0);
        e.loadSong(s);
        e.play();
        StereoBlock out; out.resize(kBarFrames);
        float pk = 0.0f;
        e.process(out, kBarFrames);
        for (int i = 0; i < kBarFrames; ++i) pk = std::max(pk, std::fabs(out.left[i]));
        return pk;
    };
    const float withTom   = peakWith(true);
    const float withConga = peakWith(false);
    CHECK(withTom  > 1.0e-4f);
    CHECK(withConga > 1.0e-4f);
    // Same step, same level, different voice, so the rendered audio must
    // differ. (Tom 83.6 Hz vs conga 183.5 Hz, and different decays/trims.)
    CHECK(std::fabs(withTom - withConga) > 1.0e-3f);
}

// The app saves a session by capturing the ENGINE's live song, which is what
// carries edits made in the pattern grid. That capture path is what
// RebornAudioProcessor::saveSessionFile relies on, and it is not exercised by
// the pure-format round-trip test above.
static void test_session_captures_live_engine_edits(const char* tmp) {
    Engine e;
    e.prepare(44100.0);
    {
        Song s;
        s.tempo = 128.0;
        s.title = "Live Capture";
        s.drum808.patterns[0].length = 16;
        s.drum808.patterns[0].steps[0].setOn(0, true);
        e.loadSong(s);
    }
    // Edit through the engine's song, exactly as the grid does.
    Song& live = e.song();
    live.drum808.patterns[0].steps[7].setOn(1, true);
    live.drum808.patterns[0].steps[7].setAccent(1, true);
    live.bass1.patterns[0].steps[2] = BassStep{ true, 11, -1, false, true };
    live.drum808Knobs.present = true;
    live.drum808Knobs.sharedPrimary[SC_RS] = true;

    Session out;
    out.song = live;                       // the capture saveSessionFile does
    out.env.sampleDir = "C:/some path/with spaces";
    out.env.modName   = "Massen's MSM 2.0";
    out.env.modPath   = "C:/mods/msm.rbm";

    std::string err;
    CHECK(saveRbsx(tmp, out, &err));

    Session back;
    CHECK(loadRbsx(tmp, back, &err));
    // The edits must survive, not just the originally loaded song.
    CHECK(back.song.drum808.at(0).steps[7].voiceOn(1));
    CHECK(back.song.drum808.at(0).steps[7].voiceAccent(1));
    CHECK(back.song.bass1.at(0).steps[2].gate);
    CHECK(back.song.bass1.at(0).steps[2].note == 11);
    CHECK(back.song.bass1.at(0).steps[2].octave == -1);
    CHECK(back.song.bass1.at(0).steps[2].slide);
    CHECK(back.song.drum808Knobs.sharedPrimary[SC_RS]);
    CHECK(!back.song.drum808Knobs.sharedPrimary[SC_LT]);
    // And the environment, which is what makes a reopened session SOUND the same.
    CHECK(back.env.sampleDir == out.env.sampleDir);
    CHECK(back.env.modName   == out.env.modName);
    CHECK(back.env.modPath   == out.env.modPath);

    // A reloaded session must be playable, not merely parseable.
    Engine e2;
    e2.prepare(44100.0);
    e2.loadSong(back.song);
    e2.play();
    StereoBlock blk; blk.resize(kBarFrames);
    e2.process(blk, kBarFrames);
    CHECK(rmsOf(blk, kBarFrames) > 1e-4);

    std::remove(tmp);
}

// --- Mod unscrambling, on SYNTHETIC archives ------------------------------
//
// ModPacker scrambles every embedded file past its first 1024 bytes with two key
// sequences per archive (docs/FORMAT_RBM.md). The reader generates them (test_rbm_keygen
// below) and, for an archive they do not fit, estimates them from its own content,
// which is what these tests exercise: their keys are random, not ModPacker's. No mod content may enter the repo, so these tests build an
// archive here: drum-like tones and noise whose tails never fall to exact silence (a
// small DC offset under dither, as in real recordings, so the start from zero votes is
// wrong wherever a tail decides it), structurally valid pseudo-JPEGs (one carrying bytes
// after its end marker), a pseudo song, a stereo voice, and one noise voice stored three
// times at the top of the archive (twice identical, once scaled), as the retail mods store
// their snare noise. It is scrambled with deterministic keys in the measured layout. The
// real-mod check, against keys obtained with ModPacker as an oracle, is reborn_rbmkeys
// --truth, and REBORN_TEST_MOD with REBORN_TEST_MOD_KEYS below.
namespace synthmod {

struct Lcg {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    double uni() { return double(next() >> 8) / 16777216.0; }          // [0, 1)
};

inline void be32(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back((unsigned char)(x >> 24)); v.push_back((unsigned char)(x >> 16));
    v.push_back((unsigned char)(x >> 8));  v.push_back((unsigned char) x);
}
inline void tag(std::vector<unsigned char>& v, const char* t) { v.insert(v.end(), t, t + 4); }

// A 16-bit AIFF at 44.1 kHz, frames interleaved when stereo.
std::vector<unsigned char> aiff(const std::vector<int16_t>& pcm, int channels) {
    std::vector<unsigned char> c;
    tag(c, "COMM"); be32(c, 18);
    c.push_back(0); c.push_back((unsigned char) channels);
    be32(c, uint32_t(pcm.size() / std::size_t(channels)));
    c.push_back(0); c.push_back(16);
    const unsigned char rate[10] = { 0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 };   // 44100
    c.insert(c.end(), rate, rate + 10);
    tag(c, "SSND"); be32(c, uint32_t(8 + 2 * pcm.size())); be32(c, 0); be32(c, 0);
    for (int16_t v : pcm) { c.push_back((unsigned char)(uint16_t(v) >> 8)); c.push_back((unsigned char)(uint16_t(v) & 0xFF)); }
    std::vector<unsigned char> f;
    tag(f, "FORM"); be32(f, uint32_t(4 + c.size())); tag(f, "AIFF");
    f.insert(f.end(), c.begin(), c.end());
    return f;
}

// The tail every voice decays into: its own small DC offset under triangular dither.
inline int16_t tailSample(Lcg& r, double v, int dc) {
    const double d1 = r.uni(), d2 = r.uni();
    return (int16_t) std::lround(v + dc + (d1 - d2) * 2.0);
}

// A drum-like hit: a decaying tone with a little noise and a pitch drop.
std::vector<int16_t> tone(Lcg& r, std::size_t n, double hz, double tau, double amp, int dc) {
    std::vector<int16_t> x(n, 0);
    const std::size_t sounding = n * 3 / 4;
    double ph = r.uni() * 6.283;
    for (std::size_t t = 0; t < n; ++t) {
        double v = 0.0;
        if (t < sounding) {
            const double env = std::exp(-double(t) / (tau * 44100.0));
            ph += 6.283185307 * hz * (1.0 + 0.5 * std::exp(-double(t) / 900.0)) / 44100.0;
            const double jitter = r.uni() - 0.5;
            v = amp * env * (std::sin(ph) + 0.02 * jitter);
        }
        x[t] = tailSample(r, v, dc);
    }
    return x;
}

// A noise hit: one-pole filtered noise under a decay.
std::vector<int16_t> noise(Lcg& r, std::size_t n, double tau, double amp, double pole, int dc) {
    std::vector<int16_t> x(n, 0);
    double y = 0.0;
    for (std::size_t t = 0; t < n; ++t) {
        double v = 0.0;
        if (t < n * 4 / 5) {
            const double u = r.uni();
            y = pole * y + (1.0 - pole) * (u - 0.5) * 3.0;
            v = amp * std::exp(-double(t) / (tau * 44100.0)) * y;
        }
        x[t] = tailSample(r, v, dc);
    }
    return x;
}

struct Voice { std::string file, canon; std::vector<double> mono; };   // mono: what the reader should give

struct Built {
    std::vector<unsigned char> plain, scrambled;
    std::vector<uint8_t> keyEven, keyOdd;
    std::vector<Voice> voices;
    std::vector<std::pair<std::string, std::vector<unsigned char>>> jpegs;
};

// The reader's view of a voice: mono, scaled to -1..1, mean removed.
std::vector<double> readerView(const std::vector<int16_t>& pcm, int channels) {
    std::vector<double> m(pcm.size() / std::size_t(channels), 0.0);
    for (std::size_t t = 0; t < m.size(); ++t) {
        for (int c = 0; c < channels; ++c) m[t] += double(pcm[t * std::size_t(channels) + std::size_t(c)]) / 32768.0;
        m[t] /= double(channels);
    }
    double mean = 0.0;
    for (double v : m) mean += v;
    mean /= double(m.size());
    for (double& v : m) v -= mean;
    return m;
}

using Files = std::vector<std::pair<std::string, std::vector<unsigned char>>>;

// An archive in ModPacker's layout holding the files, unscrambled.
std::vector<unsigned char> archive(const Files& files) {
    std::vector<unsigned char> body, out;
    tag(body, "PRBM");
    for (const auto& f : files) {
        std::vector<unsigned char> pay(f.first.begin(), f.first.end());
        pay.push_back(0);
        pay.insert(pay.end(), f.second.begin(), f.second.end());
        tag(body, "EMBF"); be32(body, uint32_t(pay.size()));
        body.insert(body.end(), pay.begin(), pay.end());
        if (pay.size() & 1) body.push_back(0);
    }
    tag(out, "CAT "); be32(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// Random keys as long as the archive needs, in the measured layout: the entry at index i
// takes K[i % 2][(o - 1024) % (140 + 17 i)].
void randomKeys(const std::vector<unsigned char>& plain, Lcg& r, std::vector<uint8_t>& k0, std::vector<uint8_t>& k1) {
    std::size_t lmax[2] = { 0, 0 };
    const std::vector<RbmEntry> es = rbmEntries(plain);
    for (std::size_t i = 0; i < es.size(); ++i)
        if (es[i].length > 1024) lmax[i % 2] = std::max(lmax[i % 2], std::min<std::size_t>(140 + 17 * i, es[i].length - 1024));
    for (std::size_t k = 0; k < lmax[0]; ++k) k0.push_back((uint8_t)(r.next() >> 24));
    for (std::size_t k = 0; k < lmax[1]; ++k) k1.push_back((uint8_t)(r.next() >> 24));
}

Built build() {
    Lcg r { 12345u };
    Files files;
    Built b;
    for (int i = 0; i < 10; ++i) {                     // pseudo-JPEGs, structurally valid
        std::vector<unsigned char> j = { 0xFF, 0xD8,                                    // SOI
            0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 }; // APP0
        j.push_back(0xFF); j.push_back(0xDB); j.push_back(0x00); j.push_back(0x43); j.push_back(0);   // DQT
        for (int q = 0; q < 64; ++q) j.push_back((unsigned char)(1 + (r.next() >> 26)));
        const unsigned char sos[] = { 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00 };
        j.insert(j.end(), sos, sos + sizeof(sos));
        const std::size_t n = 1600 + std::size_t(r.next() % 2400);
        while (j.size() < n - 2) {                       // entropy-coded data, every FF stuffed
            const unsigned char c = (unsigned char)(r.next() >> 24);
            j.push_back(c);
            if (c == 0xFF) j.push_back(0x00);
        }
        j.push_back(0xFF); j.push_back(0xD9);                                           // EOI
        if (i == 3) {                                    // a writer that leaves bytes after the EOI,
            const char* tail = "\0\0trailing bytes after the end marker";
            j.insert(j.end(), tail, tail + 38);
            j[j.size() - 36] = 0xFF; j[j.size() - 35] = 0x01;   // FF 01 among them, illegal in a scan
        }
        files.push_back({ "120" + std::to_string(10 + i) + ".jpg", j });
        b.jpegs.push_back(files.back());
    }
    {                                                  // a pseudo song: mostly zeros
        std::vector<unsigned char> song(3000, 0);
        for (std::size_t k = 0; k < song.size(); k += 7) song[k] = (unsigned char)(r.next() >> 28);
        files.push_back({ "Demo.rbs", song });
    }
    static const char* names[][2] = {
        { "TR808BD.aif", "808_bd" }, { "TR808SD1.aif", "808_sd1" }, { "TR808LT.aif", "808_lt" },
        { "TR808MT.aif", "808_mt" }, { "TR808HT.aif", "808_ht" }, { "TR808CB.aif", "808_cb" },
        { "TR808CL.aif", "808_cl" }, { "TR808RS.aif", "808_rs" }, { "TR808CP.aif", "808_cp" },
        { "TR808CY.aif", "808_cy" }, { "TR808OH.aif", "808_oh" }, { "TR808CH.aif", "808_ch" },
        { "tr909bd1.aif", "909_bd1" }, { "tr909sd1.aif", "909_sd1" }, { "tr909lt.aif", "909_lt" },
        { "tr909mt.aif", "909_mt" }, { "tr909ht.aif", "909_ht" }, { "tr909rs.aif", "909_rs" },
        { "TR909hc.aif", "909_cp" }, { "TR909chh.aif", "909_ch" } };
    for (int i = 0; i < 20; ++i) {
        // Every draw is named and made in a fixed order, so every compiler builds the same archive.
        const std::size_t n = 3000 + std::size_t(r.next() % 6000);
        const int dc = int(r.next() % 7) - 3;
        const double a = r.uni(), c = r.uni(), d = r.uni();
        std::vector<int16_t> x = (i % 4 == 3)
            ? noise(r, n, 0.03 + 0.05 * a, 6000.0 + 8000.0 * c, 0.3 + 0.5 * d, dc)
            : tone(r, n, 50.0 + 400.0 * a, 0.02 + 0.08 * c, 8000.0 + 16000.0 * d, dc);
        b.voices.push_back({ names[i][0], names[i][1], readerView(x, 1) });
        files.push_back({ names[i][0], aiff(x, 1) });
    }
    {                                                  // a stereo voice: two decaying tones
        const std::vector<int16_t> l = tone(r, 5000, 90.0, 0.05, 12000.0, 1);
        const std::vector<int16_t> rr = tone(r, 5000, 140.0, 0.04, 9000.0, -2);
        std::vector<int16_t> st;
        for (std::size_t t = 0; t < l.size(); ++t) { st.push_back(l[t]); st.push_back(rr[t]); }
        b.voices.push_back({ "tr909ccy.aif", "909_crash", readerView(st, 2) });
        files.push_back({ "tr909ccy.aif", aiff(st, 2) });
    }
    {                                                  // the snare noise, three times, last
        const std::vector<int16_t> sdn = noise(r, 6000, 0.08, 14000.0, 0.2, 2);
        std::vector<int16_t> scaled(sdn.size());
        for (std::size_t t = 0; t < sdn.size(); ++t) scaled[t] = (int16_t) std::lround(0.8 * sdn[t]);
        const struct { const char* file; const char* canon; const std::vector<int16_t>* x; } sn[] = {
            { "TR909sdn1.aif", "909_sdn1", &scaled }, { "TR909sdn2.aif", "909_sdn2", &sdn }, { "TR909sdn3.aif", "909_sdn3", &sdn } };
        for (const auto& v : sn) {
            b.voices.push_back({ v.file, v.canon, readerView(*v.x, 1) });
            files.push_back({ v.file, aiff(*v.x, 1) });
        }
    }
    b.plain = archive(files);
    randomKeys(b.plain, r, b.keyEven, b.keyOdd);
    b.scrambled = b.plain;
    applyRbmKeys(b.scrambled, b.keyEven, b.keyOdd, +1);
    return b;
}

// The files an archive holds, as (name, file bytes), read back from its entries.
Files filesOf(const std::vector<unsigned char>& image) {
    Files out;
    for (const RbmEntry& e : rbmEntries(image)) {
        const unsigned char* p = image.data() + e.payload;
        out.push_back({ std::string(p, p + e.nameLength),
                        std::vector<unsigned char>(p + e.nameLength + 1, p + e.length) });
    }
    return out;
}

// The estimator's starting point, recomputed independently: per key position, the most
// common stored byte, read as plain 0. Wrong wherever the archive needs the refinement.
std::size_t zeroVoteMisses(const Built& b) {
    std::size_t miss = 0;
    const std::vector<RbmEntry> es = rbmEntries(b.scrambled);
    for (int par = 0; par < 2; ++par) {
        const std::vector<uint8_t>& truth = par ? b.keyOdd : b.keyEven;
        std::vector<std::array<int, 256>> votes(truth.size());
        for (auto& v : votes) v.fill(0);
        for (std::size_t i = std::size_t(par); i < es.size(); i += 2)
            for (std::size_t o = 1024; o < es[i].length; ++o) {
                const std::size_t q = (o - 1024) % (140 + 17 * i);
                if (q < votes.size()) ++votes[q][b.scrambled[es[i].payload + o]];
            }
        for (std::size_t q = 0; q < truth.size(); ++q)
            miss += std::size_t(std::max_element(votes[q].begin(), votes[q].end()) - votes[q].begin()) != truth[q];
    }
    return miss;
}

} // namespace synthmod

static void test_rbm_unscramble_synthetic() {
    const synthmod::Built b = synthmod::build();
    CHECK(b.scrambled != b.plain);

    // The key layout round-trips.
    {
        std::vector<unsigned char> back = b.scrambled;
        applyRbmKeys(back, b.keyEven, b.keyOdd, -1);
        CHECK(back == b.plain);
    }

    // The archive needs the refinement: the starting point alone is wrong at many positions.
    const std::size_t misses = synthmod::zeroVoteMisses(b);
    CHECK(misses >= 50);

    // The estimate recovers it exactly: every key byte, every voice, every JPEG.
    std::vector<unsigned char> img = b.scrambled;
    RbmUnscrambleReport rep;
    std::vector<uint8_t> k0, k1;
    const bool ok = unscrambleRbm(img, &rep, &k0, &k1);
    CHECK(ok);
    CHECK(!rep.generated && rep.generatedRefused); // random keys are not ModPacker's: estimated
    CHECK(rep.twins >= 2);                         // the noise voice's copies were found and kept
    CHECK(rep.audioStreams == 25);                 // 20 mono, the stereo voice's two channels, 3 copies
    CHECK(rep.check.jpegsChecked == 10 && rep.check.jpegsClean == 10);
    CHECK(rep.check.positionsBad == 0);
    CHECK(k0 == b.keyEven);
    CHECK(k1 == b.keyOdd);
    CHECK(img == b.plain);
    std::size_t wrong = 0;
    for (std::size_t q = 0; q < img.size(); ++q) wrong += img[q] != b.plain[q];
    std::printf("rbm unscramble (synthetic): %zu entries, %d twins, start wrong at %zu positions, "
                "%zu of %zu bytes wrong after, %.2f s\n",
                rbmEntries(img).size(), rep.twins, misses, wrong, img.size(), rep.seconds);

    // The same archive gives the same keys every time, whatever the thread schedule.
    {
        std::vector<unsigned char> again = b.scrambled;
        std::vector<uint8_t> a0, a1;
        unscrambleRbm(again, nullptr, &a0, &a1);
        CHECK(a0 == k0 && a1 == k1);
    }

    // An archive that was never scrambled comes back untouched.
    {
        std::vector<unsigned char> same = b.plain;
        RbmUnscrambleReport r2;
        CHECK(unscrambleRbm(same, &r2));
        CHECK(same == b.plain);
    }

    // The self-check: the archive as built passes, scrambled it fails, and under keys right
    // except for their top 5 percent (a few voices ruined, the rest exact) it fails too.
    {
        const RbmCheck good = checkUnscrambledRbm(b.plain), bad = checkUnscrambledRbm(b.scrambled);
        CHECK(good.ok && good.jpegsClean == good.jpegsChecked && good.positionsBad == 0);
        CHECK(!bad.ok && bad.jpegsClean * 2 < bad.jpegsChecked && bad.residualRatio > 10.0);
        std::vector<uint8_t> t0 = b.keyEven, t1 = b.keyOdd;
        synthmod::Lcg r { 7u };
        for (std::vector<uint8_t>* t : { &t0, &t1 })
            for (std::size_t q = t->size() - t->size() / 20; q < t->size(); ++q) (*t)[q] = (uint8_t)(r.next() >> 24);
        std::vector<unsigned char> part = b.scrambled;
        applyRbmKeys(part, t0, t1, -1);
        const RbmCheck partly = checkUnscrambledRbm(part);
        CHECK(!partly.ok && partly.positionsBad >= 3);
        // Keys right except for 66 added to every other byte of a run of key positions. Under
        // a run of 40 (20 samples moved alike) no sample stands out by level, but every period
        // leaves a step at both ends of the run, which the residual test sees. A run of 200
        // moves 100 samples, more than half the running median's window, which then moves with
        // it, so the position tests lose it; it is still refused, by the JPEG scans it breaks
        // and the samples it wraps (docs/FORMAT_RBM.md).
        const auto chainOffset = [&](std::size_t run) {
            std::vector<uint8_t> c0 = b.keyEven;
            for (std::size_t q = 100; q < 100 + run && q < c0.size(); q += 2) c0[q] = uint8_t(c0[q] + 66);
            std::vector<unsigned char> chained = b.scrambled;
            applyRbmKeys(chained, c0, b.keyOdd, -1);
            return checkUnscrambledRbm(chained);
        };
        const RbmCheck chain = chainOffset(40), longChain = chainOffset(200);
        CHECK(!chain.ok && chain.positionsBad >= 2 && chain.worstResidual > 3.5);
        CHECK(!longChain.ok);
        std::printf("rbm self-check (synthetic): exact worst position %.2f, ratio %.2f; top 5%% wrong: %d bad "
                    "positions (worst %.1f); chain offset: %d bad (worst residual %.1f), over 200 positions %d bad, "
                    "scans clean %d/%d, %d wrapped; scrambled: ratio %.1f, scans clean %d/%d\n",
                    good.worstPosition, good.residualRatio, partly.positionsBad, partly.worstPosition,
                    chain.positionsBad, chain.worstResidual, longChain.positionsBad, longChain.jpegsClean,
                    longChain.jpegsChecked, longChain.voicesWrapped, bad.residualRatio, bad.jpegsClean, bad.jpegsChecked);
    }

    // A wrapped sample. A key byte one off under the high bytes of most of a key position class
    // shifts a voice by 256, which no position test sees, and wraps a sample near one end of
    // the range to the other: two neighbouring samples at opposite ends. Past the readable part
    // of a voice whose readable steps stay under half scale, that alone refuses the decode. A
    // voice that already steps that far inside its readable part (a clipped square) does not.
    {
        const std::vector<RbmEntry> es = rbmEntries(b.plain);
        const RbmEntry& bd = es[11];                     // TR808BD.aif, mono: name, NUL, 12+26+16
        const std::size_t o0 = bd.payload + bd.nameLength + 1 + 54;
        const auto put = [](std::vector<unsigned char>& im, std::size_t o, int16_t v) {
            im[o] = (unsigned char)(uint16_t(v) >> 8); im[o + 1] = (unsigned char)(uint16_t(v) & 0xFF); };
        std::vector<unsigned char> wrapped = b.plain;
        put(wrapped, o0 + 2 * 2000, -32700); put(wrapped, o0 + 2 * 2001, 32700);
        const RbmCheck w = checkUnscrambledRbm(wrapped);
        CHECK(w.voicesWrapped == 1 && w.positionsBad == 0 && w.residualRatio < 4.0 && !w.ok);
        put(wrapped, o0 + 2 * 100, -32700); put(wrapped, o0 + 2 * 101, 32700);
        CHECK(checkUnscrambledRbm(wrapped).voicesWrapped == 0);
    }

    // A partial mod (the manual lets a mod replace only some sounds) scrambled with keys that
    // are not ModPacker's, so they can only be estimated: five voices do not pin a key, so the
    // estimate is refused, and right keys would be too under the evidence minimum. Without it
    // the right decode passes, which is how a partial mod under generated keys is read
    // (test_rbm_keygen).
    {
        const synthmod::Files all = synthmod::filesOf(b.plain);
        const synthmod::Files few(all.begin(), all.begin() + 16);   // 10 JPEGs, the song, 5 voices
        const std::vector<unsigned char> plain = synthmod::archive(few);
        std::vector<uint8_t> f0, f1;
        synthmod::Lcg r { 31u };
        synthmod::randomKeys(plain, r, f0, f1);
        std::vector<unsigned char> img3 = plain;
        applyRbmKeys(img3, f0, f1, +1);
        const std::vector<unsigned char> stored = img3;
        RbmUnscrambleReport r4;
        CHECK(!unscrambleRbm(img3, &r4));
        CHECK(r4.generatedRefused && !r4.generated);
        CHECK(r4.check.tooFewVoices && r4.check.voicesScrambled == 5);
        const RbmCheck exact = checkUnscrambledRbm(plain);
        CHECK(exact.tooFewVoices && !exact.ok);
        CHECK(checkUnscrambledRbm(plain, false).ok);
        const char* path = "reborn_synthetic_partial.rbm";
        if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(stored.data(), 1, stored.size(), f); std::fclose(f); }
        std::string err;
        CHECK(readModSamples(path, &err).empty());
        CHECK(err.find("too few sounds") != std::string::npos && err.find("5 of the 12") != std::string::npos);
        std::remove(path);
    }

    // An archive whose bytes past each first kilobyte are noise has no right key: refused,
    // with a reason, rather than played as noise.
    {
        std::vector<unsigned char> junk = b.scrambled;
        synthmod::Lcg r { 99u };
        for (const RbmEntry& e : rbmEntries(junk))
            for (std::size_t o = 1024; o < e.length; ++o) junk[e.payload + o] = (unsigned char)(r.next() >> 24);
        std::vector<unsigned char> img2 = junk;
        RbmUnscrambleReport r3;
        CHECK(!unscrambleRbm(img2, &r3));
        CHECK(!r3.tooLarge && !r3.check.ok);
        const char* path = "reborn_synthetic_junk.rbm";
        if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(junk.data(), 1, junk.size(), f); std::fclose(f); }
        std::string err;
        CHECK(readModSamples(path, &err).empty());
        CHECK(err.find("self-check") != std::string::npos);
        std::remove(path);
    }

    // Through the mod reader, from a file: each voice under its canonical name decodes to its
    // original samples (mono, mean removed, -1..1; the stereo one mono-summed), each sprite to
    // its original bytes, and a second read, served from the key cache, gives the same.
    {
        const char* path = "reborn_synthetic_test.rbm";
        if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(b.scrambled.data(), 1, b.scrambled.size(), f); std::fclose(f); }
        std::string err;
        const std::vector<ModSample> voices = readModSamples(path, &err);
        CHECK(voices.size() == b.voices.size());
        int matched = 0;
        for (const synthmod::Voice& v : b.voices) {
            int named = 0;
            for (const ModSample& m : voices) {
                if (m.name != v.canon) continue;
                ++named;
                double worst = m.data.size() == v.mono.size() ? 0.0 : 1.0;
                for (std::size_t t = 0; t < v.mono.size() && t < m.data.size(); ++t)
                    worst = std::max(worst, std::fabs(double(m.data[t]) - v.mono[t]));
                matched += worst < 1e-6;
            }
            CHECK(named == 1);
        }
        CHECK(matched == int(b.voices.size()));
        const std::vector<ModSprite> sprites = readModSprites(path, &err);
        int same = 0;
        for (const auto& jp : b.jpegs)
            for (const ModSprite& sp : sprites) same += sp.fileName == jp.first && sp.bytes == jp.second;
        CHECK(same == int(b.jpegs.size()));
        const std::vector<ModSample> again = readModSamples(path, &err);
        bool identical = again.size() == voices.size();
        for (std::size_t k = 0; identical && k < again.size(); ++k)
            identical = again[k].name == voices[k].name && again[k].data == voices[k].data;
        CHECK(identical);
        std::remove(path);
    }
}

static void test_rbm_unscramble_hostile() {
    using namespace synthmod;
    // A name that fills the whole readable part, on a scrambled-length payload.
    {
        std::vector<unsigned char> pay(1100, 'a');
        pay.push_back(0);
        pay.resize(4000, 0x55);
        std::vector<unsigned char> body; tag(body, "PRBM");
        tag(body, "EMBF"); be32(body, uint32_t(pay.size())); body.insert(body.end(), pay.begin(), pay.end());
        std::vector<unsigned char> m; tag(m, "CAT "); be32(m, uint32_t(body.size())); m.insert(m.end(), body.begin(), body.end());
        std::vector<unsigned char> img = m;
        RbmUnscrambleReport rep;
        unscrambleRbm(img, &rep);                  // any answer, so long as it returns
        CHECK(rep.entries == 1);
    }
    // More entries than any ModPacker archive: refused, quickly, before any key is sized.
    {
        std::vector<unsigned char> body; tag(body, "PRBM");
        for (int i = 0; i < 1100; ++i) { tag(body, "EMBF"); be32(body, 2); body.push_back('x'); body.push_back(0); }
        std::vector<unsigned char> m; tag(m, "CAT "); be32(m, uint32_t(body.size())); m.insert(m.end(), body.begin(), body.end());
        std::vector<uint8_t> k0, k1;
        CHECK(!estimateRbmKeys(m, k0, k1));
    }
    // Not a mod at all.
    {
        std::vector<unsigned char> junk(5000, 7);
        std::vector<uint8_t> k0, k1;
        CHECK(!estimateRbmKeys(junk, k0, k1));
    }
    // Past the caps: a voice of 8.5 million samples (17 MB), then an archive over 32 MB. Each
    // refused unread, flagged tooLarge, and named so by the reader, rather than estimated
    // with gigabytes of working memory.
    {
        const auto archive = [&](std::size_t fileBytes) {
            std::vector<unsigned char> pay = { 'T', 'R', '8', '0', '8', 'B', 'D', '.', 'a', 'i', 'f', 0 };
            const std::size_t frames = (fileBytes - 54) / 2;
            tag(pay, "FORM"); be32(pay, uint32_t(46 + 2 * frames)); tag(pay, "AIFF");
            tag(pay, "COMM"); be32(pay, 18); pay.push_back(0); pay.push_back(1); be32(pay, uint32_t(frames));
            pay.push_back(0); pay.push_back(16);
            const unsigned char rate[10] = { 0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 };
            pay.insert(pay.end(), rate, rate + 10);
            tag(pay, "SSND"); be32(pay, uint32_t(8 + 2 * frames)); be32(pay, 0); be32(pay, 0);
            pay.resize(pay.size() + 2 * frames, 0x11);
            std::vector<unsigned char> m; tag(m, "CAT "); be32(m, uint32_t(12 + pay.size())); tag(m, "PRBM");
            tag(m, "EMBF"); be32(m, uint32_t(pay.size())); m.insert(m.end(), pay.begin(), pay.end());
            return m;
        };
        for (const std::size_t bytes : { std::size_t(17) << 20, std::size_t(33) << 20 }) {
            const std::vector<unsigned char> m = archive(bytes);
            RbmUnscrambleReport rep;
            std::vector<uint8_t> k0, k1;
            CHECK(!estimateRbmKeys(m, k0, k1, &rep));
            CHECK(rep.tooLarge);
            // The generated keys are not tried either: past the caps not even the self-check runs.
            std::vector<unsigned char> img = m;
            RbmUnscrambleReport ur;
            CHECK(!unscrambleRbm(img, &ur) && ur.tooLarge && !ur.generatedRefused);
            const char* path = "reborn_large_test.rbm";
            if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(m.data(), 1, m.size(), f); std::fclose(f); }
            std::string err;
            CHECK(readModSamples(path, &err).empty());
            CHECK(err.find("refused unread") != std::string::npos);
            std::remove(path);
        }
    }
    // An AIFF chunk whose size makes a 32-bit step of 8 + size wrap to nothing. Walked in
    // 32 bits, the header scan never advanced and the estimate never returned.
    {
        std::vector<unsigned char> pay = { 'x', 0 };
        tag(pay, "FORM"); be32(pay, 0x1000); tag(pay, "AIFF");
        tag(pay, "JUNK"); be32(pay, 0xFFFFFFF8u);
        pay.resize(3000, 0x11);
        std::vector<unsigned char> body; tag(body, "PRBM");
        tag(body, "EMBF"); be32(body, uint32_t(pay.size())); body.insert(body.end(), pay.begin(), pay.end());
        std::vector<unsigned char> m; tag(m, "CAT "); be32(m, uint32_t(body.size())); m.insert(m.end(), body.begin(), body.end());
        std::vector<uint8_t> k0, k1;
        RbmUnscrambleReport rep;
        estimateRbmKeys(m, k0, k1, &rep);          // returns at all (ctest times the suite out if not)
        CHECK(rep.entries == 1 && rep.audioStreams == 0);
    }
    // Containers nested 50,000 deep, about 600 KB. Every walk recursed once per level and
    // overflowed the stack; now each stops at a fixed depth. Through every reader.
    {
        const int depth = 50000;
        std::vector<unsigned char> m;
        tag(m, "CAT "); be32(m, uint32_t(4 + 12 * depth)); tag(m, "PRBM");
        for (int d = 0; d < depth; ++d) { tag(m, "CAT "); be32(m, uint32_t(4 + 12 * (depth - d - 1))); tag(m, "SUBX"); }
        CHECK(rbmEntries(m).empty());
        const char* path = "reborn_nested_test.rbm";
        if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(m.data(), 1, m.size(), f); std::fclose(f); }
        std::string err;
        CHECK(readModSamples(path, &err).empty());
        CHECK(readModSprites(path, &err).empty());
        CHECK(!readModInfo(path).valid);
        std::remove(path);
    }
    // The same nesting in a song file. The .rbs walk recursed once per level too, and a
    // .rbsx session carries a .rbs image, so a DAW restoring one would crash (review S1).
    // Returning at all is the test; nothing below the cap is a real chunk.
    {
        const int depth = 50000;
        std::vector<unsigned char> s;
        tag(s, "CAT "); be32(s, uint32_t(4 + 12 * depth)); tag(s, "RB40");
        for (int d = 0; d < depth; ++d) { tag(s, "CAT "); be32(s, uint32_t(4 + 12 * (depth - d - 1))); tag(s, "SUBX"); }
        Song song; std::string err;
        loadRbs(s.data(), s.size(), song, &err);
        CHECK(song.globRaw.empty());
    }
}

// --- The key function --------------------------------------------------------
//
// ModPacker's keys are generated from the archive itself (src/io/rbm_keygen.cpp,
// docs/FORMAT_RBM.md, "The key function"). The real-mod check, every key byte of the
// four retail mods and of fresh partial packs against ModPacker's own keys, is local
// only (reborn_rbmkeys --truth). Here the sums are checked against a computation of
// their own, the generator against hashes of the reference it was ported from, and the
// reader on synthetic archives scrambled with their own generated keys.
static void test_rbm_keygen() {
    using namespace synthmod;
    const auto fnv = [](const std::vector<uint8_t>& b) {
        uint64_t h = 1469598103934665603ull;
        for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
        return h;
    };

    // The sums: each payload's first 1024 bytes, filled out with 0x65 past the end of a
    // shorter one, as big-endian words, the first 128 into sum[1] and the rest into sum[0].
    // They lie in the readable part, so scrambling leaves them as they were.
    {
        Lcg r { 4242u };
        Files files;
        for (const std::size_t n : { std::size_t(3), std::size_t(500), std::size_t(700), std::size_t(1500), std::size_t(5000) }) {
            std::vector<unsigned char> f(n);
            for (unsigned char& c : f) c = (unsigned char)(r.next() >> 24);
            files.push_back({ "f" + std::to_string(n) + ".bin", f });
        }
        const std::vector<unsigned char> plain = archive(files);
        uint32_t want[2] = { 0, 0 };
        for (const auto& f : files) {
            std::vector<unsigned char> w(f.first.begin(), f.first.end());
            w.push_back(0);
            w.insert(w.end(), f.second.begin(), f.second.end());
            w.resize(1024, 0x65);
            for (std::size_t k = 0; k < 1024; k += 4)
                want[k < 512 ? 1 : 0] += (uint32_t(w[k]) << 24) | (uint32_t(w[k + 1]) << 16) | (uint32_t(w[k + 2]) << 8) | w[k + 3];
        }
        uint32_t got[2] = { 0, 0 };
        rbmKeySums(plain, got);
        CHECK(got[0] == want[0] && got[1] == want[1]);
        std::vector<uint8_t> g0, g1;
        CHECK(generateRbmKeys(plain, g0, g1));
        std::vector<unsigned char> scrambled = plain;
        applyRbmKeys(scrambled, g0, g1, +1);
        CHECK(scrambled != plain);
        uint32_t again[2] = { 0, 0 };
        rbmKeySums(scrambled, again);
        CHECK(again[0] == got[0] && again[1] == got[1]);
    }

    // The generator against the reference it was ported from, which reproduces ModPacker's
    // keys exactly: every segment type, both steps between chunks, a start at each end of a
    // chunk, two whole chunks each. The parities differ only in the offset added to the
    // sum, so each odd row is its even row's sequence.
    {
        const struct { int parity; uint32_t sum; std::size_t length; uint64_t fnv; } pins[] = {
            { 0, 0x0008E800u, 4096, 0x5675522882D0DFD9ull },   // type 0, step 4, starts at 0
            { 0, 0x022283E8u, 3096, 0xFF779C2347BA67C2ull },   // type 1, step 4, starts at 1000
            { 0, 0x0408EFFFu, 2049, 0xBC81681093F7B18Full },   // type 2, step 4, starts at 2047
            { 0, 0x0619FA05u, 3579, 0x2288BCC488649681ull },   // type 3, step 4, starts at 517
            { 0, 0x09FFC5DCu, 2596, 0x99F8E5FD2E65331Full },   // type 0, step 5, starts at 1500
            { 0, 0xFFFFC803u, 4093, 0xC5498274EC8D46E8ull },   // type 0, the top of the counter
            { 1, 0x0008F800u, 4096, 0x5675522882D0DFD9ull },
            { 1, 0x022293E8u, 3096, 0xFF779C2347BA67C2ull },
            { 1, 0x0408FFFFu, 2049, 0xBC81681093F7B18Full },
            { 1, 0x061A0A05u, 3579, 0x2288BCC488649681ull },
            { 1, 0x09FFD5DCu, 2596, 0x99F8E5FD2E65331Full },
            { 1, 0xFFFFD803u, 4093, 0xC5498274EC8D46E8ull },
        };
        int matched = 0;
        for (const auto& p : pins) matched += fnv(rbmKeySequence(p.parity, p.sum, p.length)) == p.fnv;
        CHECK(matched == int(sizeof(pins) / sizeof(pins[0])));
        // A longer sequence extends a shorter one; the sum wraps modulo 2^32; and the counter
        // repeats every 2048 * 65535 of sum, where the extra step and the seed both come round.
        const std::vector<uint8_t> longer = rbmKeySequence(1, 0x01234567u, 3500), shorter = rbmKeySequence(1, 0x01234567u, 100);
        CHECK(longer.size() == 3500 && std::equal(shorter.begin(), shorter.end(), longer.begin()));
        CHECK(rbmKeySequence(0, 0xFFFFF000u, 4096) == rbmKeySequence(1, 0u, 4096));
        CHECK(rbmKeySequence(0, 0x00ABCDEFu, 4000) == rbmKeySequence(0, 0x00ABCDEFu + 2048u * 65535u, 4000));
        CHECK(rbmKeySequence(0, 0x00ABCDEFu, 4000) != rbmKeySequence(0, 0x00ABCDEFu + 2048u, 4000));
    }

    // A whole archive scrambled with its own generated keys: unscrambled exactly, by the
    // generated keys, not an estimate.
    const Built b = build();
    {
        std::vector<uint8_t> g0, g1;
        RbmUnscrambleReport gr;
        CHECK(generateRbmKeys(b.plain, g0, g1, &gr));
        CHECK(gr.keyLength[0] == int(b.keyEven.size()) && gr.keyLength[1] == int(b.keyOdd.size()));
        std::vector<unsigned char> img = b.plain;
        applyRbmKeys(img, g0, g1, +1);
        RbmUnscrambleReport rep;
        std::vector<uint8_t> k0, k1;
        CHECK(unscrambleRbm(img, &rep, &k0, &k1));
        CHECK(rep.generated && !rep.generatedRefused && rep.check.ok && !rep.check.tooFewVoices);
        CHECK(k0 == g0 && k1 == g1 && img == b.plain);
        CHECK(rep.sum[0] == gr.sum[0] && rep.sum[1] == gr.sum[1]);
        std::printf("rbm key function (synthetic): %d entries, key lengths %d/%d, generated in %.3f ms\n",
                    rep.entries, rep.keyLength[0], rep.keyLength[1], rep.seconds * 1000.0);
    }

    // Partial mods under generated keys, which the evidence minimum used to refuse: five
    // voices with the JPEGs and the song, and one voice alone. Both read exactly, and the
    // five through the mod reader from a file, every voice under its canonical name.
    {
        const Files all = filesOf(b.plain);
        const Files five(all.begin(), all.begin() + 16), one(all.begin() + 11, all.begin() + 12);
        for (const Files* few : { &five, &one }) {
            const std::vector<unsigned char> plain = archive(*few);
            std::vector<uint8_t> g0, g1;
            CHECK(generateRbmKeys(plain, g0, g1));
            std::vector<unsigned char> img = plain;
            applyRbmKeys(img, g0, g1, +1);
            const std::vector<unsigned char> stored = img;
            RbmUnscrambleReport rep;
            CHECK(unscrambleRbm(img, &rep));
            CHECK(rep.generated && rep.check.tooFewVoices && img == plain);
            if (few != &five) continue;
            const char* path = "reborn_synthetic_partial_generated.rbm";
            if (std::FILE* f = std::fopen(path, "wb")) { std::fwrite(stored.data(), 1, stored.size(), f); std::fclose(f); }
            std::string err;
            const std::vector<ModSample> voices = readModSamples(path, &err);
            CHECK(voices.size() == 5 && err.empty());
            int matched = 0;
            for (std::size_t v = 0; v < 5; ++v)
                for (const ModSample& m : voices) {
                    if (m.name != b.voices[v].canon || m.data.size() != b.voices[v].mono.size()) continue;
                    double worst = 0.0;
                    for (std::size_t t = 0; t < m.data.size(); ++t) worst = std::max(worst, std::fabs(double(m.data[t]) - b.voices[v].mono[t]));
                    matched += worst < 1e-6;
                }
            CHECK(matched == 5);
            std::remove(path);
        }
    }

    // What the evidence minimum guarded against. A voice of loud noise decoded with ANY key is
    // loud noise, so the self-check passes the right decode and wrong ones alike. What tells
    // them apart is how far the audio is from uniform: an unrelated key spreads every sample
    // past the readable part evenly over the whole range, which a hi-hat, a cymbal or noise
    // capped below full scale is not. So a noise-only partial mod under its own generated keys
    // is read exactly, and stored unscrambled or under random keys (where the generated keys
    // are wrong and pass the check) it is refused. Noise spread evenly up to full scale is what
    // a wrong key gives and cannot be told from it: refused under any key.
    {
        Lcg r { 2026u };
        std::vector<int16_t> capped(6000), hat(5000), full(6000);
        for (int16_t& v : capped) v = (int16_t) std::lround(24000.0 * (2.0 * r.uni() - 1.0));
        double y = 0.0;
        for (std::size_t t = 0; t < hat.size(); ++t) {                // high-passed noise, decaying
            const double u = (r.uni() - 0.5) * 3.0, prev = y;
            y = 0.6 * y + u;
            hat[t] = (int16_t) std::lround(9000.0 * std::exp(-double(t) / 1500.0) * (y - prev));
        }
        for (int16_t& v : full) v = (int16_t) std::lround(32767.0 * (2.0 * r.uni() - 1.0));
        int read = 0, refused = 0, fullRefused = 0;
        for (const std::vector<int16_t>* x : { &capped, &hat, &full }) {
            const std::vector<unsigned char> plain = archive({ { "TR909RCY.aif", aiff(*x, 1) } });
            std::vector<uint8_t> g0, g1, r0, r1;
            CHECK(generateRbmKeys(plain, g0, g1));
            randomKeys(plain, r, r0, r1);
            std::vector<unsigned char> asStored = plain, underRandom = plain, underOwn = plain;
            applyRbmKeys(underRandom, r0, r1, +1);
            applyRbmKeys(underOwn, g0, g1, +1);
            for (std::vector<unsigned char>* img : { &asStored, &underRandom, &underOwn }) {
                const std::vector<unsigned char> before = *img;
                RbmUnscrambleReport rep;
                const bool ok = unscrambleRbm(*img, &rep);
                if (x == &full) { fullRefused += !ok && !rep.generated && *img == before; continue; }
                if (img == &underOwn)
                    read += ok && rep.generated && rep.unrelatedOdds[0] < 1e-6 && *img == plain;
                else
                    refused += !ok && !rep.generated && *img == before;
            }
        }
        CHECK(read == 2 && refused == 4 && fullRefused == 3);
    }

    // Near misses. An archive edited in its readable part after packing by a multiple of 2^22 in a
    // sum keeps the generator's rand bytes, so its generated key differs from the one it was
    // scrambled with in a few bytes only, and every other test passes it. The reader sets the
    // generated key against its near neighbours' keys and refuses it when one of them decodes
    // the voice better. Built here by scrambling with a neighbour's key, which is what the
    // archive's original sum would have given: a decaying hat, a tone and loud capped noise,
    // each under the neighbour differing in the most bytes, are refused as they stand (the hat
    // and the tone by the check's own position test, the noise, where that test is blind, by
    // the near-miss test); under their own keys all three read.
    {
        Lcg r { 4242u };
        std::vector<int16_t> hat(6000), tone(6000), capped(8000);
        double y = 0.0;
        for (std::size_t t = 0; t < hat.size(); ++t) {
            const double u = (r.uni() - 0.5) * 3.0, prev = y;
            y = 0.6 * y + u;
            hat[t] = (int16_t) std::lround(9000.0 * std::exp(-double(t) / 1500.0) * (y - prev));
        }
        for (std::size_t t = 0; t < tone.size(); ++t)
            tone[t] = (int16_t) std::lround(12000.0 * std::exp(-double(t) / 3000.0) * std::sin(0.0285 * double(t)));
        for (int16_t& v : capped) v = (int16_t) std::lround(24000.0 * (2.0 * r.uni() - 1.0));
        int refusedNear = 0, byNearMiss = 0, readOwn = 0;
        for (const std::vector<int16_t>* x : { &hat, &tone, &capped }) {
            // Not every sum has a near neighbour (most moves also change the counter's carry); the
            // voice's name is varied until this one does.
            std::vector<unsigned char> plain;
            std::vector<uint8_t> g0, g1, nearest;
            std::size_t most = 0;
            for (int variant = 0; variant < 64 && most == 0; ++variant) {
                plain = archive({ { "TR909OH" + std::to_string(variant) + ".aif", aiff(*x, 1) } });
                CHECK(generateRbmKeys(plain, g0, g1));
                uint32_t sum[2];
                rbmKeySums(plain, sum);
                for (int m = -31; m <= 31; ++m) {
                    if (m == 0) continue;
                    const std::vector<uint8_t> k = rbmKeySequence(0, sum[0] + uint32_t(m) * (uint32_t(1) << 22), g0.size());
                    std::size_t differ = 0;
                    for (std::size_t p = 0; p < k.size(); ++p) differ += k[p] != g0[p];
                    if (differ <= 64 && differ > most) { most = differ; nearest = k; }
                }
            }
            CHECK(most > 0);
            std::vector<unsigned char> edited = plain, own = plain;
            applyRbmKeys(edited, nearest, g1, +1);
            applyRbmKeys(own, g0, g1, +1);
            const std::vector<unsigned char> before = edited;
            RbmUnscrambleReport rep;
            refusedNear += !unscrambleRbm(edited, &rep) && !rep.generated && edited == before;
            byNearMiss += rep.nearMiss[0];
            RbmUnscrambleReport repOwn;
            readOwn += unscrambleRbm(own, &repOwn) && repOwn.generated && repOwn.nearKeys > 0 && own == plain;
        }
        CHECK(refusedNear == 3 && byNearMiss >= 1 && readOwn == 3);
    }

    // The two sums set the two keys independently, so an archive edited after packing in one
    // half of an entry's readable kilobyte has generated keys right on one parity and wrong on
    // the other. A parity that holds audio the check cannot judge (here loud noise) must not be
    // passed on the strength of the other's evidence: each parity with audio is confirmed
    // alone. Edited in its second half (the even key), [noise, voice] is refused as it stands,
    // and 24 voices whose even ones are all loud noise go to the estimate. [song, voice] is
    // read: its even parity holds no audio, its voice is on the odd one and decodes exactly,
    // and only the song, which nothing plays, decodes wrong (judged by the check alone, as an
    // estimate always was).
    {
        const auto edited = [&](const Files& files, std::size_t entry, std::size_t at) {
            std::vector<unsigned char> img = archive(files);
            std::vector<uint8_t> g0, g1;
            generateRbmKeys(img, g0, g1);
            applyRbmKeys(img, g0, g1, +1);
            const RbmEntry e = rbmEntries(img)[entry];
            img[e.payload + at] = (unsigned char)(img[e.payload + at] + 1);   // the low byte of a word
            return img;
        };
        std::vector<unsigned char> song;                 // an IFF FORM too, but not audio
        tag(song, "FORM"); be32(song, 2992); tag(song, "SONG");
        song.resize(3000, 0);
        for (std::size_t k = 12; k < song.size(); k += 7) song[k] = (unsigned char)(k * 37);
        Lcg r { 77u };
        std::vector<int16_t> hiss(6000);
        for (int16_t& v : hiss) v = (int16_t) std::lround(24000.0 * (2.0 * r.uni() - 1.0));
        const Files all = filesOf(b.plain);
        Files many;
        for (int i = 0; i < 24; ++i) {
            std::vector<int16_t> x(4000);
            for (int16_t& v : x) v = (int16_t) std::lround(24000.0 * (2.0 * r.uni() - 1.0));
            if (i % 2) many.push_back(all[11 + std::size_t(i / 2) % 20]);
            else many.push_back({ "N" + std::to_string(i) + ".aif", aiff(x, 1) });
        }
        {
            const Files files{ { "Demo.rbs", song }, all[11] };
            const std::vector<unsigned char> plain = archive(files);
            std::vector<unsigned char> img = edited(files, 0, 700 + 3);
            RbmUnscrambleReport rep;
            CHECK(unscrambleRbm(img, &rep) && rep.generated);
            const RbmEntry v = rbmEntries(img)[1], pv = rbmEntries(plain)[1];
            CHECK(std::equal(img.begin() + long(v.payload), img.begin() + long(v.payload + v.length),
                             plain.begin() + long(pv.payload)));
        }
        int taken = 0, unconfirmed = 0;
        for (const auto& c : { std::make_pair(Files{ { "TR909CY.aif", aiff(hiss, 1) }, all[11] }, std::size_t(0)),
                               std::make_pair(many, std::size_t(4)) }) {
            std::vector<unsigned char> img = edited(c.first, c.second, 700 + 3);
            const std::vector<unsigned char> before = img;
            RbmUnscrambleReport rep;
            const bool ok = unscrambleRbm(img, &rep);
            taken += rep.generated;
            unconfirmed += rep.generatedUnconfirmed && rep.generatedCheck.ok;
            if (!ok) CHECK(img == before);
        }
        CHECK(taken == 0 && unconfirmed == 2);
    }

    // An archive whose top-level CAT does not span the file, as ModPacker writes it from a
    // folder without a ModName.txt (length 0, under other keys): the generated keys are not
    // tried at all, and the five voices are too few to estimate.
    {
        const Files all = filesOf(b.plain);
        std::vector<unsigned char> img = archive(Files(all.begin(), all.begin() + 16));
        std::vector<uint8_t> g0, g1;
        CHECK(generateRbmKeys(img, g0, g1));
        applyRbmKeys(img, g0, g1, +1);
        img[4] = img[5] = img[6] = img[7] = 0;
        RbmUnscrambleReport rep;
        CHECK(!unscrambleRbm(img, &rep));
        CHECK(!rep.generated && !rep.generatedRefused && rep.check.tooFewVoices);
    }

    // A decode that moves the NUL ending a name longer than the readable part changes the
    // entries rbmEntries finds. The generated keys are not taken then, and the image the
    // estimate goes on to is the archive exactly as stored: the whole call ends as the
    // estimate alone does.
    {
        std::vector<unsigned char> pay(1100, 'a');
        pay.push_back(0);
        pay.resize(4000, 0x55);
        const Files all = filesOf(b.plain);
        std::vector<unsigned char> body; tag(body, "PRBM");
        tag(body, "EMBF"); be32(body, uint32_t(pay.size())); body.insert(body.end(), pay.begin(), pay.end());
        std::vector<unsigned char> bd(all[11].first.begin(), all[11].first.end());
        bd.push_back(0);
        bd.insert(bd.end(), all[11].second.begin(), all[11].second.end());
        tag(body, "EMBF"); be32(body, uint32_t(bd.size())); body.insert(body.end(), bd.begin(), bd.end());
        if (bd.size() & 1) body.push_back(0);
        std::vector<unsigned char> stored; tag(stored, "CAT "); be32(stored, uint32_t(body.size()));
        stored.insert(stored.end(), body.begin(), body.end());
        std::vector<unsigned char> viaGenerated = stored, estimated = stored;
        RbmUnscrambleReport r1, r2;
        const bool ok1 = unscrambleRbm(viaGenerated, &r1), ok2 = unscrambleRbm(estimated, &r2, nullptr, nullptr, true);
        CHECK(!r1.generated && r1.generatedRefused);
        CHECK(ok1 == ok2 && viaGenerated == estimated && r1.entries == r2.entries);
    }
}

// Sprite extraction, checked against a REAL mod when one is available.
//
// Mods are third-party artwork and are not in the repo, so this is opt-in via
// REBORN_TEST_MOD=<path to a .rbm>. It is still worth having as a test rather
// than a one-off script: the audio and sprite walks must partition the same
// EMBF set, and a regression in either would otherwise be silent.
static void test_mod_sprite_extraction() {
    const char* path = std::getenv("REBORN_TEST_MOD");
    if (!path || !*path) {
        std::printf("skip: mod sprite test (set REBORN_TEST_MOD to a .rbm)\n");
        return;
    }
    std::string err;
    const std::vector<ModSprite> sprites = readModSprites(path, &err);
    const std::vector<ModSample> voices  = readModSamples(path, &err);

    // REBORN_TEST_MOD_KEYS=<keys.bin>: exact keys for that mod (local only, from the
    // ModPacker oracle; two big-endian u32-length-prefixed strings). The reader's keys
    // (generated, or estimated if those fail) are then scored byte for byte, which is
    // what the self-check alone cannot do.
    if (const char* kp = std::getenv("REBORN_TEST_MOD_KEYS")) {
        auto slurpFile = [](const char* f) {
            std::vector<unsigned char> b;
            if (std::FILE* h = std::fopen(f, "rb")) {
                std::fseek(h, 0, SEEK_END); const long n = std::ftell(h); std::fseek(h, 0, SEEK_SET);
                if (n > 0) { b.resize(std::size_t(n)); if (std::fread(b.data(), 1, b.size(), h) != b.size()) b.clear(); }
                std::fclose(h);
            }
            return b;
        };
        const std::vector<unsigned char> kb = slurpFile(kp), mod = slurpFile(path);
        std::vector<uint8_t> t[2];
        std::size_t off = 0;
        for (std::vector<uint8_t>& k : t) {
            if (off + 4 > kb.size()) break;
            const std::size_t n = (std::size_t(kb[off]) << 24) | (std::size_t(kb[off + 1]) << 16) | (std::size_t(kb[off + 2]) << 8) | kb[off + 3];
            off += 4;
            if (off + n > kb.size()) break;
            k.assign(kb.begin() + long(off), kb.begin() + long(off + n)); off += n;
        }
        CHECK(!t[0].empty() && !t[1].empty());
        std::vector<unsigned char> exact = mod, est = mod;
        applyRbmKeys(exact, t[0], t[1], -1);
        RbmUnscrambleReport rep;
        unscrambleRbm(est, &rep);
        std::size_t wrong = 0;
        for (std::size_t q = 0; q < exact.size() && q < est.size(); ++q) wrong += exact[q] != est[q];
        const long allowed = std::getenv("REBORN_TEST_MOD_WRONG") ? std::atol(std::getenv("REBORN_TEST_MOD_WRONG")) : 0;
        CHECK(long(wrong) <= allowed);
        std::printf("mod keys test: %zu of %zu decoded bytes differ from the exact decode (allowed %ld)\n",
                    wrong, exact.size(), allowed);
    }

    CHECK(!sprites.empty());
    CHECK(!voices.empty());
    // The two readers split one EMBF set: a mod is documented as 106 chunks,
    // so sprites + voices should account for essentially all of them. The
    // audio side drops nothing silently; that COMM bug cost 19 voices once.
    CHECK(sprites.size() + voices.size() >= 100);

    int looksLikeImage = 0, endsLikeImage = 0;
    for (const ModSprite& s : sprites) {
        CHECK(!s.fileName.empty());
        CHECK(!s.bytes.empty());
        // No AIFF must leak into the sprite list.
        if (s.bytes.size() >= 12)
            CHECK(std::memcmp(s.bytes.data(), "FORM", 4) != 0);
        if (s.bytes.size() > 3 && s.bytes[0] == 0xFF && s.bytes[1] == 0xD8) {
            ++looksLikeImage;
            // Unscrambled, a JPEG ends with its EOI (scrambled, almost never). The estimate
            // no longer forces it, so this is evidence the key is right where JPEGs reach.
            if (s.bytes[s.bytes.size() - 2] == 0xFF && s.bytes.back() == 0xD9) ++endsLikeImage;
        }
    }
    CHECK(endsLikeImage == looksLikeImage);
    // Most payloads should carry JPEG's SOI marker; a mod may also embed its
    // demo song and a startup sound, so this is a majority test not an all test.
    CHECK(looksLikeImage * 2 > (int) sprites.size());
    std::printf("mod sprite test: %zu sprites (%d JPEG-magic, %d ending FF D9), %zu voices\n",
                sprites.size(), looksLikeImage, endsLikeImage, voices.size());
}

// The 303 TUNE knob is a 32-slot lookup table with a 24-wide centre detent and
// 8-wide end plateaus, NOT a continuous law. Measured 2026-07-27 by exporting
// all 128 knob values from ReBirth; our render then matched every one of them
// to 0.000 semitones.
//
// The law this replaced was fitted from the two EXTREME fixtures only, and both
// of those sit inside the end plateaus where a continuous law happens to agree.
// It was wrong by up to 1.38 semitones mid-range and mistuned 7 of the 16
// shipped songs. This test pins every boundary so that cannot come back.
static void test_tune_slot_table() {
    // the plateaus
    for (int b = 0; b <= 7; ++b)    CHECK(tuneSemitonesFromByte(b) == -12);
    for (int b = 120; b <= 127; ++b) CHECK(tuneSemitonesFromByte(b) == 12);
    // the 24-wide centre detent
    for (int b = 52; b <= 75; ++b)  CHECK(tuneSemitonesFromByte(b) == 0);
    // every interior step is exactly 4 knob values wide
    CHECK(tuneSemitonesFromByte(8) == -11 && tuneSemitonesFromByte(11) == -11);
    CHECK(tuneSemitonesFromByte(12) == -10);
    CHECK(tuneSemitonesFromByte(48) == -1 && tuneSemitonesFromByte(51) == -1);
    CHECK(tuneSemitonesFromByte(76) == 1 && tuneSemitonesFromByte(79) == 1);
    CHECK(tuneSemitonesFromByte(116) == 11 && tuneSemitonesFromByte(119) == 11);
    // boundaries: the exact points a continuous law gets wrong
    CHECK(tuneSemitonesFromByte(35) == -5 && tuneSemitonesFromByte(36) == -4);
    CHECK(tuneSemitonesFromByte(51) == -1 && tuneSemitonesFromByte(52) == 0);
    CHECK(tuneSemitonesFromByte(75) == 0 && tuneSemitonesFromByte(76) == 1);
    // the inverse round-trips for every semitone
    for (int s = -12; s <= 12; ++s)
        CHECK(tuneSemitonesFromByte(tuneByteFromSemitones(s)) == s);
    // out-of-range input is clamped rather than reading past the table
    CHECK(tuneSemitonesFromByte(-5) == -12 && tuneSemitonesFromByte(999) == 12);
}

// ---- The 303 sequencer-to-voice boundary ----------------------------------
//
// Regression locks on measured behaviour. THREE of these shipped wrong, and all
// three were caught by Luke's ear rather than by any metric, because each is an
// interaction between two flags and a one-control-at-a-time sweep cannot see
// one BY CONSTRUCTION. See include/reborn/step_events.h.

// Compact pattern spec, so the shapes stay readable at a glance:
//   N note        n note tying onward       A accented note   a accented + tie
//   R rest        r rest tying onward       X accented rest   x accented + tie
static BassPattern patternFromSpec(const char* spec, int note = 9) {
    BassPattern p;
    int i = 0;
    for (; spec[i] != '\0' && i < kMaxSteps; ++i) {
        BassStep& s = p.steps[(std::size_t) i];
        const char c = spec[i];
        s.note   = note;
        s.gate   = (c == 'N' || c == 'n' || c == 'A' || c == 'a');
        s.accent = (c == 'A' || c == 'a' || c == 'X' || c == 'x');
        s.slide  = (c == 'n' || c == 'a' || c == 'r' || c == 'x');
    }
    p.length = i;
    return p;
}

// PITCH FOLLOWS EVERY STEP, INCLUDING RESTS. A rest names a note and ReBirth
// tunes to it; the rest flag gates and does nothing else. We used to discard
// the note byte on a rest, so a slide INTO a rest glided nowhere and
// Clodhopper's step 3 played G where ReBirth plays D. That is a shipped song
// playing a wrong note, and no spectral metric ever flagged it.
static void test_bass_step_pitch_follows_rests() {
    BassPattern p;
    p.length = 2;
    p.octaveBase = 4;
    p.steps[0] = BassStep{ true,  0, 0, false, true  };   // C, ties onward
    p.steps[1] = BassStep{ false, 7, 0, false, false };   // REST naming G

    BassEvent ev[2];
    decideBassSteps(p, 0, 2, false, ev);
    CHECK(ev[0].note == 48);                              // C3
    CHECK(ev[1].note == 55);                              // the rest still names G
    CHECK(ev[1].kind == BassEvent::Kind::Hold);           // and is not a note-off
}

// A REST THAT IS TIED INTO IS NOT A NOTE-OFF. Measured on Clodhopper's 303a:
// ReBirth sustains the tied rest where we cut it, a 32 dB hole at 50% through
// the step. A rest that is NOT tied into still releases.
static void test_bass_step_tied_rest_is_not_a_note_off() {
    BassEvent ev[3];
    decideBassSteps(patternFromSpec("NnR"), 0, 3, false, ev);
    CHECK(ev[1].kind == BassEvent::Kind::Strike);
    CHECK(ev[2].kind == BassEvent::Kind::Hold);

    decideBassSteps(patternFromSpec("NRR"), 0, 3, false, ev);
    CHECK(ev[1].kind == BassEvent::Kind::Release);
    CHECK(ev[2].kind == BassEvent::Kind::Release);
}

// A REST CAN TIE ONWARD. Forcing the tie false on a rest was the second half of
// the same bug: it broke the chain at the first rest, so the note after a tied
// rest re-plucked instead of gliding.
static void test_bass_step_rest_can_tie_onward() {
    BassEvent ev[4];
    decideBassSteps(patternFromSpec("NnrN"), 0, 4, false, ev);
    CHECK(ev[2].kind == BassEvent::Kind::Hold);
    CHECK(ev[2].tie);                                     // the rest carries it
    CHECK(ev[3].kind == BassEvent::Kind::Strike);
    CHECK(ev[3].slideIn);                                 // so the next note glides
}

// THE ACCENT ROW IS READ ON A REST, and lands on whatever is sounding through.
// With nothing sounding through there is nothing to land on and the voice is
// never told, which is faithful to what was measured rather than a claim about
// what ReBirth does there.
static void test_bass_step_accent_lands_on_a_tied_rest() {
    BassEvent ev[2];
    decideBassSteps(patternFromSpec("nX"), 0, 2, false, ev);
    CHECK(ev[1].kind == BassEvent::Kind::Hold);
    CHECK(ev[1].accent);

    decideBassSteps(patternFromSpec("NX"), 0, 2, false, ev);
    CHECK(ev[1].kind == BassEvent::Kind::Release);
    CHECK(!ev[1].accent);
}

// THE SLIDE FLAG SITS ON THE NOTE YOU SLIDE FROM. A flag on the destination
// does nothing, and fixtures built the wrong way round measure nothing at all.
static void test_bass_step_slide_sits_on_the_source_note() {
    BassEvent ev[2];
    decideBassSteps(patternFromSpec("nN"), 0, 2, false, ev);
    CHECK(ev[1].slideIn);                                 // flag on the source
    decideBassSteps(patternFromSpec("Nn"), 0, 2, false, ev);
    CHECK(!ev[1].slideIn);                                // flag on the destination
}

// A TIED RUN IS ONE FRESH PLUCK. Clodhopper's steps 13/14/15 carry identical
// flags and ReBirth spans 30 dB across them, decaying, which is one envelope
// fired at the retrigger rather than three. Every step of the run still gates;
// what makes it one note is that only the first arrives without slideIn, and
// slideIn is what tells the voice not to re-pluck.
static void test_bass_step_tied_run_plucks_once() {
    BassEvent ev[4];
    decideBassSteps(patternFromSpec("nnnN"), 0, 4, false, ev);
    int strikes = 0, glided = 0;
    for (int i = 0; i < 4; ++i) {
        if (ev[i].kind == BassEvent::Kind::Strike) ++strikes;
        if (ev[i].slideIn) ++glided;
    }
    CHECK(strikes == 4);
    CHECK(glided == 3);
    CHECK(!ev[0].slideIn);
}

// THE TIE CROSSES THE LOOP SEAM. The slide state is not reset at the bar line,
// so a slide on the last step ties into step 0 of the next pass.
static void test_bass_step_tie_crosses_the_pattern_loop() {
    BassEvent ev[5];
    decideBassSteps(patternFromSpec("NRRn"), 0, 5, false, ev);   // 5 over a 4-loop
    CHECK(ev[4].kind == BassEvent::Kind::Strike);
    CHECK(ev[4].slideIn);
}

// Records the call sequence, because the ORDER is load-bearing: setStepNote()
// must precede the gate decision or a slide into a rest glides nowhere, and
// setStepTiming() must precede trigger(), which is what arms the countdown.
struct RecordingVoice : Instrument {
    std::vector<std::string> calls;
    void prepare(double) override {}
    void reset() override {}
    int numParams() const override { return 0; }
    ParamInfo paramInfo(int) const override { return ParamInfo{}; }
    float getParam(int) const override { return 0.0f; }
    void setParam(int, float) override {}
    void trigger(int n, float, bool acc, bool sl) override {
        calls.push_back("trigger n=" + std::to_string(n) +
                        (acc ? " acc" : "") + (sl ? " slide" : ""));
    }
    void release() override { calls.push_back("release"); }
    void setStepTiming(double, bool tie) override {
        calls.push_back(tie ? "timing tie" : "timing");
    }
    void setStepNote(int n) override { calls.push_back("note=" + std::to_string(n)); }
    void holdStep(double, bool tie, bool acc) override {
        calls.push_back(std::string("hold") + (tie ? " tie" : "") + (acc ? " acc" : ""));
    }
    void process(StereoBlock&, int) override {}
};

// A SLIDE OUT OF SILENCE IS NOT A SLIDE, under BassRules::slideNeedsVoice.
// Measured 2026-08-02: ReBirth's accent lift on a note slid into from a
// RELEASED rest is +5.89 dB and on a genuine retrigger +5.95, agreeing to 0.06
// dB, so it treats the two the same. Clodhopper's step 10 is this shape.
// Default OFF, so both behaviours are locked here.
static void test_bass_step_slide_out_of_silence() {
    // N R r N : step 2 releases, step 3 is a rest that ties onward, step 4 is
    // the note. There is nothing sounding for step 4 to glide from.
    BassEvent ev[4];
    const BassPattern p = patternFromSpec("NRrN");

    decideBassSteps(p, 0, 4, false, ev);              // shipped default
    CHECK(ev[1].kind == BassEvent::Kind::Release);
    CHECK(!ev[1].sounding);
    CHECK(ev[3].slideIn);                             // glides in from nothing

    BassRules rules;
    rules.slideNeedsVoice = true;
    decideBassSteps(p, 0, 4, false, ev, false, rules);
    CHECK(!ev[3].slideIn);                            // a fresh pluck instead

    // The control that must NOT change: a slide with the voice still sounding.
    const BassPattern q = patternFromSpec("nnnN");
    decideBassSteps(q, 0, 4, false, ev, false, rules);
    CHECK(ev[3].slideIn);
}

// AN ACCENT ON A PLAIN REST, over a note still ringing, fires the accent
// envelope under BassRules::accentOnPlainRest. Measured: ReBirth lifts
// +10.61 dB where our release path drops the flag and lifts +0.00.
// Clodhopper's step 12. Default OFF.
static void test_bass_step_accent_on_plain_rest() {
    BassEvent ev[2];
    const BassPattern p = patternFromSpec("NX");      // note, then accented rest

    decideBassSteps(p, 0, 2, false, ev);              // shipped default
    CHECK(ev[1].kind == BassEvent::Kind::Release);
    CHECK(!ev[1].accent);
    CHECK(!ev[1].accentOnly);

    BassRules rules;
    rules.accentOnPlainRest = true;
    decideBassSteps(p, 0, 2, false, ev, false, rules);
    CHECK(ev[1].kind == BassEvent::Kind::Release);    // still does not gate
    CHECK(ev[1].accent);
    CHECK(ev[1].accentOnly);

    // With nothing sounding through it there is nothing to land on.
    decideBassSteps(patternFromSpec("RX"), 0, 2, false, ev, false, rules);
    CHECK(!ev[1].accentOnly);
}

static void test_bass_event_call_order() {
    RecordingVoice v;
    BassEvent ev;
    ev.kind = BassEvent::Kind::Strike;
    ev.note = 57; ev.accent = true; ev.slideIn = true; ev.tie = true;
    applyBassEvent(&v, ev, 0.125);
    CHECK(v.calls.size() == 3);
    CHECK(v.calls[0] == "note=57");                       // pitch before the gate
    CHECK(v.calls[1] == "timing tie");                    // timing before trigger
    CHECK(v.calls[2] == "trigger n=57 acc slide");

    v.calls.clear();
    ev = BassEvent{};
    ev.kind = BassEvent::Kind::Release;
    ev.note = 60;
    applyBassEvent(&v, ev, 0.125);
    CHECK(v.calls.size() == 2);
    CHECK(v.calls[0] == "note=60");                       // a REST still retunes
    CHECK(v.calls[1] == "release");
}

// THE VOICE-SLOT HOT-SWAP SEAM. Engine::setInstrument replaces a channel's voice
// at runtime, and the sequencer must drive the NEW voice from the next step:
// this is exactly what the app's 303 engine picker (Open303 <-> TPT <-> hosted)
// relies on. Swap in a recording probe, play, and confirm the engine (a) handed
// back the displaced voice for off-thread disposal, (b) now reports the probe as
// the slot's voice, and (c) drives it through the note/timing/trigger boundary.
static void test_engine_voice_hot_swap() {
    Song song;
    song.tempo = 120.0;
    song.bass1.patterns[0].length = 16;
    song.bass1.patterns[0].steps[0] = BassStep{ true, 9, 0, false, false };  // A on step 0

    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);

    auto probe = std::make_unique<RecordingVoice>();
    RecordingVoice* rv = probe.get();
    auto displaced = e.setInstrument(kBass1, std::move(probe));
    CHECK(displaced != nullptr);                 // old voice handed back to free off-thread
    CHECK(e.instrument(kBass1) == rv);           // the slot now holds the probe

    e.play();                                    // fires step 0 into the swapped voice
    StereoBlock out; out.resize(1024);
    e.process(out, 1024);

    bool triggered = false;
    for (const auto& c : rv->calls)
        if (c.rfind("trigger", 0) == 0) triggered = true;
    CHECK(triggered);                            // the sequencer drives the new voice
}

// setInstrument must REFUSE a null voice. Swapping one in would empty the slot,
// and the sequencer's fire path (fireBass) dereferences it with no guard the way
// renderSlice does. makeBass303(HostedPlugin) returns null by contract, so a
// caller could forward it here; the current voice must survive untouched.
static void test_engine_set_instrument_rejects_null() {
    Song song;
    song.tempo = 120.0;
    song.bass1.patterns[0].length = 16;
    song.bass1.patterns[0].steps[0] = BassStep{ true, 9, 0, false, false };

    Engine e;
    e.prepare(44100.0);
    e.loadSong(song);

    auto probe = std::make_unique<RecordingVoice>();
    RecordingVoice* rv = probe.get();
    e.setInstrument(kBass1, std::move(probe));

    auto handedBack = e.setInstrument(kBass1, nullptr);   // refuse the null
    CHECK(handedBack == nullptr);                          // nothing displaced
    CHECK(e.instrument(kBass1) == rv);                     // slot still holds the probe

    e.play();                                              // must not crash on a null slot
    StereoBlock out; out.resize(1024);
    e.process(out, 1024);
    bool triggered = false;
    for (const auto& c : rv->calls)
        if (c.rfind("trigger", 0) == 0) triggered = true;
    CHECK(triggered);                                      // the kept voice still plays
}

// THE OPEN303 PLUGIN'S NOTE CONTRACT. The standalone Open303 instrument plugin
// drives the shared makeOpen303() voice from MIDI in "sustain mode": it calls
// setStepTiming(tie=true) so a struck note holds until the DAW note-off, then
// release()s it. Lock that contract here (no JUCE needed): a held note sounds,
// and after release it decays to silence.
static void test_open303_sustain_then_release() {
    auto v = makeOpen303();
    v->prepare(44100.0);
    v->setStepTiming(0.0, /*tie=*/true);        // sustain until an explicit release
    v->trigger(57, 1.0f, /*accent=*/false, /*slide=*/false);   // A3
    StereoBlock held; held.resize(44100);       // one second, note held
    v->process(held, 44100);
    const double heldRms = rmsOf(held, 44100);
    CHECK(heldRms > 1.0e-3);                     // a held note sounds

    v->release();
    StereoBlock settle; settle.resize(8820);     // let the amp release finish (0.2 s)
    v->process(settle, 8820);
    StereoBlock tail; tail.resize(22050);        // then measure 0.5 s of near-silence
    v->process(tail, 22050);
    CHECK(rmsOf(tail, 22050) < heldRms * 0.1);   // note-off silences the voice
}

// The 303 voice-slot records (RINS chunk) must round-trip through a session: the
// engine choice per slot, and for a hosted plugin its identifier and opaque
// state blob. Exercises the memory helpers, which also back the plugin's DAW
// state (getStateInformation / setStateInformation).
static void test_rbsx_instrument_slots_round_trip() {
    Session a;
    a.song.tempo = 120.0;
    a.song.bass1.patterns[0].steps[0] = BassStep{ true, 9, 0, false, false };
    a.bass[0].engine = 0;                        // native TPT
    a.bass[1].engine = 2;                        // hosted plugin
    a.bass[1].pluginId = "VST3-JC303-abcdef";
    const unsigned char blob[] = { 0xDE, 0xAD, 0x00, 0xBE, 0xEF };   // includes a NUL
    a.bass[1].pluginState.assign(blob, blob + sizeof(blob));

    std::vector<unsigned char> buf;
    CHECK(saveRbsxToMemory(buf, a));
    Session b;
    CHECK(loadRbsxFromMemory(buf.data(), buf.size(), b));
    CHECK(b.bass[0].engine == 0);
    CHECK(b.bass[1].engine == 2);
    CHECK(b.bass[1].pluginId == "VST3-JC303-abcdef");
    CHECK(b.bass[1].pluginState == a.bass[1].pluginState);   // exact bytes, NUL and all
    CHECK(b.song.bass1.at(0).steps[0].gate && b.song.bass1.at(0).steps[0].note == 9);

    // A session that records nothing about its voices leaves the slots at -1, so
    // the app applies its default rather than forcing a voice (backward compat
    // with sessions written before the RINS chunk existed).
    Session c; c.song.tempo = 130.0;             // bass[*].engine defaults to -1
    std::vector<unsigned char> buf2;
    CHECK(saveRbsxToMemory(buf2, c));
    Session d;
    CHECK(loadRbsxFromMemory(buf2.data(), buf2.size(), d));
    CHECK(d.bass[0].engine == -1 && d.bass[1].engine == -1);
}

// A SLIDE INTO THE SAME PITCH must not silence the note. Open303's note-off
// matches by key only, so the old "noteOn(new); noteOn(old,0)" legato pair wiped
// both same-key list entries and killed a still-ringing note. Regression lock.
static void test_open303_slide_into_same_note_keeps_sounding() {
    auto v = makeOpen303();
    v->prepare(44100.0);
    v->setStepTiming(0.0, /*tie=*/true);
    v->trigger(57, 1.0f, /*accent=*/false, /*slide=*/false);   // strike A3
    StereoBlock a; a.resize(4410);  v->process(a, 4410);       // 0.1 s, sounding
    CHECK(rmsOf(a, 4410) > 1.0e-3);

    v->setStepTiming(0.0, true);
    v->trigger(57, 1.0f, false, /*slide=*/true);               // slide into the SAME note
    StereoBlock b; b.resize(22050); v->process(b, 22050);      // 0.5 s afterward
    CHECK(rmsOf(b, 22050) > 1.0e-3);                           // must still be sounding
}

// Three parser-hardening regressions (external review 2026-08-31, item 2). Each
// crafts a hostile file that read out of bounds before the fix and asserts the
// parser now returns empty. These pin the return-empty CONTRACT, which is the
// right thing to assert, but they are HARD guards only under a sanitizer: the
// pre-fix failure is either UB (wav, rbm over-reads that a normal build may
// absorb into adjacent heap) or a large allocation a machine with enough
// commit can satisfy (rbs 34 GB reserve), so a revert can still pass here on a
// bare build. The reviewer proved all three under AddressSanitizer, and BACKLOG
// entry 9 owns adding an ASan job so they become real guards in CI.
static void test_parsers_reject_hostile_input() {
    auto beU32 = [](std::vector<unsigned char>& v, uint32_t x) {
        v.push_back((unsigned char)(x >> 24)); v.push_back((unsigned char)(x >> 16));
        v.push_back((unsigned char)(x >> 8));  v.push_back((unsigned char) x);
    };
    auto leU32 = [](std::vector<unsigned char>& v, uint32_t x) {
        v.push_back((unsigned char) x);        v.push_back((unsigned char)(x >> 8));
        v.push_back((unsigned char)(x >> 16)); v.push_back((unsigned char)(x >> 24));
    };
    auto tag = [](std::vector<unsigned char>& v, const char* s) {
        for (int i = 0; i < 4; ++i) v.push_back((unsigned char) s[i]);
    };
    auto writeFile = [](const char* path, const std::vector<unsigned char>& b) {
        std::FILE* f = std::fopen(path, "wb");
        if (f) { std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); }
    };

    // 2c, wav.cpp: a "fmt " chunk header sitting in the last eight bytes, so its
    // declared 16-byte body runs off the end and the fmt branch read out to
    // pos+23. The 44-byte floor hid this from a naive short file, so the buffer
    // is padded to exactly 44 with the trap at the very end.
    {
        std::vector<unsigned char> w;
        tag(w, "RIFF"); leU32(w, 36); tag(w, "WAVE");        // 0..11
        tag(w, "JUNK"); leU32(w, 16);                        // 12..19 dummy chunk
        for (int i = 0; i < 16; ++i) w.push_back(0);         // 20..35 dummy body
        tag(w, "fmt ");  leU32(w, 16);                       // 36..43 trap, body absent
        CHECK(w.size() == 44);
        const char* p = "reborn_hostile_test.wav";
        writeFile(p, w);
        int sr = 0;
        CHECK(readWav16Mono(p, &sr).empty());
        std::remove(p);
    }

    // 2a, rbm.cpp: an embedded AIFF whose SSND dataOffset points far past the
    // chunk, which underflowed ssndLen to a huge size_t and read wild memory.
    // The name must canonicalise to a real voice or decodeAiff is never reached.
    {
        std::vector<unsigned char> aiff;
        tag(aiff, "FORM"); beU32(aiff, 0); tag(aiff, "AIFF");
        tag(aiff, "COMM"); beU32(aiff, 8);
        aiff.push_back(0); aiff.push_back(1);                // channels = 1
        beU32(aiff, 8);                                      // frames = 8
        aiff.push_back(0); aiff.push_back(16);               // bits = 16
        tag(aiff, "SSND"); beU32(aiff, 8);
        beU32(aiff, 0xFFFFFFFFu);                            // dataOffset, hostile
        beU32(aiff, 0);                                      // blockSize

        std::vector<unsigned char> embf;
        const char* nm = "TR808BD";                          // -> 808_bd
        for (const char* c = nm; *c; ++c) embf.push_back((unsigned char) *c);
        embf.push_back(0);
        embf.insert(embf.end(), aiff.begin(), aiff.end());

        std::vector<unsigned char> m;
        tag(m, "CAT "); beU32(m, 0); tag(m, "PRBM");         // header, walk from 12
        tag(m, "EMBF"); beU32(m, (uint32_t) embf.size());
        m.insert(m.end(), embf.begin(), embf.end());

        const char* p = "reborn_hostile_test.rbm";
        writeFile(p, m);
        std::string err;
        CHECK(readModSamples(p, &err).empty());
        std::remove(p);
    }

    // 2b, rbs.cpp: a well-formed song with one TRAK's event count overwritten to
    // 0xFFFFFFFF. parseTrak did reserve(count) before the fix, a ~32 GB request.
    // The count is the first four payload bytes, eight after the tag (4 tag +
    // 4 IFF size). Deterministic where allocation throws (MSVC, CI Windows).
    {
        Song a; a.title = "hostile"; a.bass1.patterns[0].length = 4;
        std::vector<unsigned char> buf; std::string err;
        CHECK(saveRbs(a, buf, &err));
        size_t k = 0; bool found = false;
        for (; k + 12 <= buf.size(); ++k)
            if (!std::memcmp(&buf[k], "TRAK", 4)) { found = true; break; }
        CHECK(found);
        if (found) {
            buf[k + 8] = buf[k + 9] = buf[k + 10] = buf[k + 11] = 0xFF;
            Song b; bool threw = false;
            try { loadRbs(buf.data(), buf.size(), b, &err); }
            catch (...) { threw = true; }
            CHECK(!threw);   // bounded reservation, no 32 GB allocation
        }
    }
}

int main() {
    test_parsers_reject_hostile_input();
    test_tune_slot_table();
    test_rbm_unscramble_synthetic();
    test_rbm_unscramble_hostile();
    test_rbm_keygen();
    test_mod_sprite_extraction();
    test_session_captures_live_engine_edits("reborn_live_session_test.rbsx");
    test_drum_knob_header_round_trip();
    test_909_knob_and_flam_round_trip();
    test_shared_column_routes_to_alternate();
    test_rbsx_round_trip("reborn_session_test.rbsx");
    test_rbsx_instrument_slots_round_trip();
    test_rbs_round_trip();
    test_fader_and_pan_inverses();
    test_rbs_arrangement_and_automation_round_trip();
    test_dely_grid_and_pan_round_trip();
    test_delay_plays_its_track();
    test_delay_switch_off_clears_it();
    test_song_mode_play_restores_the_start();
    test_drum_reset_with_samples_loaded();
    test_tick0_select_in_descending_slot();
    test_303_both_octave_bits_round_trip();
    test_live_panel_capture();
    test_capture_keeps_sd_snap_under_samples();
    test_capture_fills_an_empty_arrangement();
    test_capture_keeps_an_unedited_panel();
    test_song_for_save();
    test_pcf_chunk_round_trip();
    test_rack_routing_exclusivity();
    test_pcf_routing_is_automatable();
    test_distortion_switch_is_automatable();
    test_distortion_is_panned_per_section();
    test_distortion_knob_automation();
    test_compressor_rides_play();
    test_master_compressor_sees_the_output_level();
    test_compressor_detects_the_lr_sum();
    test_compressor_detector_reads_peaks_and_releases_linearly_in_db();
    test_section_compressor_precedes_the_fader();
    test_section_compressor_runs_while_the_section_is_down();
    test_channel_peak_metering();
    test_master_peak_follows_fader();
    test_song_carries_skin_reference();
    test_note_to_hz();
    test_clamp_and_denormal();
    test_stereo_block();
    test_shuffle_timing();
    test_edit_drum_step_makes_sound();
    test_pattern_length_bounds_playback();
    test_bass_note_row_mapping();
    test_drum_voice_bits_independent();
    test_transport_stop_silences();
    test_bass_step_pitch_follows_rests();
    test_bass_step_tied_rest_is_not_a_note_off();
    test_bass_step_rest_can_tie_onward();
    test_bass_step_accent_lands_on_a_tied_rest();
    test_bass_step_slide_sits_on_the_source_note();
    test_bass_step_tied_run_plucks_once();
    test_bass_step_tie_crosses_the_pattern_loop();
    test_bass_step_slide_out_of_silence();
    test_bass_step_accent_on_plain_rest();
    test_bass_event_call_order();
    test_engine_voice_hot_swap();
    test_engine_set_instrument_rejects_null();
    test_open303_sustain_then_release();
    test_open303_slide_into_same_note_keeps_sounding();
    if (g_failures == 0) std::printf("All core tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
