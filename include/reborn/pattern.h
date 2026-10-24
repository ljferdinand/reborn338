// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Sequencer data model: patterns, banks, and song arrangement.
//
// Mirrors the classic groovebox structure closely enough to hold everything a
// loaded .rbs song contains, while staying engine-native (not a copy of any
// original file layout).
//
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>
#include <string>

namespace reborn {

constexpr int kMaxSteps = 32;      // patterns are 1..32 steps (classic: up to 16)
constexpr int kNumDrumVoices = 16; // max selectable voices per drum machine

// ---- Bass-synth (TB-303 style) step --------------------------------------
struct BassStep {
    bool  gate   = false;  // is there a note on this step?
    int   note   = 0;      // semitone 0..11 within the octave
    int   octave = 0;      // -1 (down), 0, +1 (up)
    bool  accent = false;
    bool  slide  = false;  // tie/glide into the NEXT step
    // The file set BOTH octave bits (flags 0x0C). Up and down cancel, so the
    // step plays at octave 0, but ReBirth wrote both bits and a save writes
    // them back: 126 steps in 7 of the 16 demo songs. Honoured only while
    // octave is 0; an edit that changes the octave clears it.
    bool  octBoth = false;
};

struct BassPattern {
    int length = 16;
    bool shuffle = false;      // this pattern swings its off-16ths
    std::array<BassStep, kMaxSteps> steps{};
    // Absolute semitone for a step (transpose applied by the engine).
    int absoluteNote(int i) const {
        return 12 * (octaveBase + steps[i].octave) + steps[i].note;
    }
    int octaveBase = 4; // maps note 0 -> C in this octave (A4=69 => octaveBase 4)
};

// ---- Drum-machine step ----------------------------------------------------
// One bit per voice for "hit" and "accent" (classic accent is per-step-per-voice
// in this model; a global accent track can be emulated by setting all bits).
struct DrumStep {
    uint32_t on     = 0;
    uint32_t accent = 0;
    // 909 cell semantics, measured 2026-08-25 by isolating each factor:
    //   1 = SOFT   (-7.96 dB against a normal hit)
    //   2 = normal
    //   3+ = FLAM  (the voice fires twice, the second after a gap the flam knob
    //               sets; 3 and 4 differ slightly in level, 5 and 6 clamp)
    // ACCENT is column 0 on BOTH machines, not a cell value. An earlier reading
    // had 3 as accent and 4 as flam, which was wrong in both halves.
    uint32_t flam   = 0;
    uint32_t soft   = 0;
    // The raw column-0 cell value. It is not a flag: each machine writes its
    // own constant there and ReBirth scales the accent depth by it. Measured
    // 2026-08-26 across all 17 shipped songs: the 808 writes 1 in 2544 cells,
    // the 909 writes 2 in 1442, and no song mixes the two on one machine. A 909
    // accented with value 2 spans 18.93 dB from accent knob 0 to 127 where
    // value 1 spans 13.93: exactly double the depth. Reading this as a boolean
    // ran every 909 part in every song at half its accent.
    uint8_t  accentValue = 0;
    bool voiceOn(int v)     const { return (on     >> v) & 1u; }
    bool voiceAccent(int v) const { return (accent >> v) & 1u; }
    bool voiceFlam(int v)   const { return (flam   >> v) & 1u; }
    bool voiceSoft(int v)   const { return (soft   >> v) & 1u; }
    void setOn(int v, bool b)     { if (b) on     |= (1u << v); else on     &= ~(1u << v); }
    void setAccent(int v, bool b) { if (b) accent |= (1u << v); else accent &= ~(1u << v); }
    void setFlam(int v, bool b)   { if (b) flam   |= (1u << v); else flam   &= ~(1u << v); }
    void setSoft(int v, bool b)   { if (b) soft   |= (1u << v); else soft   &= ~(1u << v); }
};

struct DrumPattern {
    int length = 16;
    bool shuffle = false;      // this pattern swings its off-16ths
    std::array<DrumStep, kMaxSteps> steps{};
};

// ---- Pattern-controlled-filter step --------------------------------------
struct PcfStep {
    bool  gate = false;
    float value = 0.5f;   // 0..1 modulation depth for this step
};
struct PcfPattern {
    int length = 16;
    std::array<PcfStep, kMaxSteps> steps{};
};

// ---- A bank of patterns for one device -----------------------------------
template <typename P>
struct Bank {
    std::vector<P> patterns;   // classic layout: 4 banks x 8 = 32 patterns
    P& at(int i) {
        if (patterns.empty()) patterns.resize(1);
        if (i < 0) i = 0;
        if (i >= (int)patterns.size()) i = (int)patterns.size() - 1;
        return patterns[i];
    }
};

// ---- Song arrangement -----------------------------------------------------
// A song step selects, for the duration of one pattern cycle, which pattern
// each device plays. This is enough to reconstruct pattern chaining and full
// arrangements.
struct SongStep {
    int bass1Pattern = 0;
    int bass2Pattern = 0;
    int drum808Pattern = 0;
    int drum909Pattern = 0;
    int pcfPattern = 0;
    int repeats = 1;      // how many times to loop this row
};

// Knob settings decoded from a song file for one bass synth.
// tuning is in semitones (-12..+12); the rest are normalized 0..1.
// (Device volume is not stored here; the mixer channel carries the level.)
struct BassKnobs {
    float tuning = 0.0f;
    float cutoff = 0.45f, resonance = 0.55f, envmod = 0.55f, decay = 0.55f,
          accent = 0.5f, waveform = 0.0f;
    bool present = false;      // true when loaded from a song file
};

// Drum-machine knob settings decoded from the 808/909 chunk headers
// (panel-order fields; all normalized 0..1). Values -1 mean "not present".
// The five 808 panel columns that each play one of TWO voices, chosen by a
// switch in the song file. Index order matches sharedAlt[] below.
enum SharedColumn808 { SC_LT, SC_MT, SC_HT, SC_RS, SC_CP, SC_COUNT };

struct DrumKnobs {
    float accent = 0.25f;                 // global accent amount

    // 808 ONLY. Each shared column's switch: true = the column plays its
    // PRIMARY voice (low/mid/hi tom, rimshot, hand clap), false = its
    // ALTERNATE (low/mid/hi conga, clave, maracas). The file stores non-zero
    // for primary and 0 for alternate; measured from fixture batch E, where 0
    // is also ReBirth's factory default. See docs/research/DRUM808_FINDINGS.md.
    bool sharedPrimary[SC_COUNT] = { false, false, false, false, false };

    float level[11]  = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
    float bdTune = 0.5f, bdAttack = 0.5f, bdDecay = 0.5f;
    float sdTune = 0.5f, sdTone = 0.5f, sdSnappy = 0.5f;
    float tomTune = 0.5f, tomDecay = 0.5f;
    // Per-tom Tune and Decay. The 808 tom columns each carry their own tune
    // (shared with the conga in the column); the 909 has independent Tune AND
    // Decay per tom (decoded 2026-08-25, previously dropped onto the shared
    // tomTune/tomDecay, which froze the mid and hi tom).
    float ltTune = 0.5f, mtTune = 0.5f, htTune = 0.5f;
    float ltDecay = 0.5f, mtDecay = 0.5f, htDecay = 0.5f;
    float chDecay = 0.25f, ohDecay = 0.5f;
    // cymTune = crash tune; rdTune = ride tune (909 header byte 28, own control).
    float cymTune = 0.5f, rdTune = 0.5f, crDecay = 0.5f, rdDecay = 0.5f;
    // 909 FLAM amount (MIDI CC 92, header byte 29). Sets the GAP between a flam
    // step's two hits, 0 to one 32nd note. Like accent, the knob alone does
    // nothing: it only affects steps whose cell value is 4.
    float flam = 0.5f;
    bool present = false;
};

// One timed automation event decoded from a song's TRAK data.
// Ticks are 1/32 notes from song start (a 16-step bar = 32 ticks).
// Every target is played. Delay is played like the PCF and DIST rides (review
// E3), and Comp since 2026-10-01, when the comp-rides fixtures showed ReBirth
// plays the COMP track's Ratio and Threshold rides. Append new targets at the
// end; the values are not persisted, but keep them stable anyway.
enum class AutoTarget : uint8_t { Bass1, Bass2, Drum808, Drum909, Mixer, Pcf, Dist,
                                  Delay, Comp };
struct AutoEvent {
    uint32_t tick = 0;
    AutoTarget target = AutoTarget::Bass1;
    uint8_t param = 0;         // raw file param id (see FORMAT_RBS.md section 5)
    uint8_t value = 0;         // raw value (0..127, or 0/1 for switches)
};

// ReBirth's faders are EXPONENTIAL, not linear in the stored byte: a constant
// 0.370 dB per step. Measured 2026-08-26 by sweeping each fader alone on a
// solo-909 export and fitting dBFS against the byte: 0.36981 dB/step for the
// master (7 points, 16 to 90) and 0.37029 for a channel (7 points, 32 to 127),
// each a straight line in dB with residuals under 0.02 dB across 24 and 35 dB
// of range. Two independent measurements of one design constant, so they share
// one here.
//
// Byte 100 is the anchor because that is what rbsgen writes into every fixture:
// pinning unity there leaves every trim ever fitted against those fixtures
// exactly where it was, and changes only the songs that sit away from 100.
// Reading these bytes as value/127 was linear and cost real level: 8.0 dB on
// Clodhopper's master (70), and on Trakka Rekka, whose 909 channel rides 106 to
// 124, 3.5 dB at byte 112 rising to 7.9 at 127.
//
// The echo SEND and the PAN were measured afterwards: the send follows this same
// fader law (delaySendGain below) and the pan has its own law (panGains below).
// Only the SHAPE is measured. The absolute scale is absorbed downstream (the
// engine's headroom constant for the master, the per-voice trims for a
// channel), so each caller passes the gain its own byte 100 used to produce
// under the old linear reading: 1.0 for the master (byte/100) and 100/127 for
// a channel (byte/127). That keeps every trim ever fitted against a rbsgen
// fixture, which writes 100 in both places, exactly where it was.
constexpr float kFaderDbPerStep    = 0.370f;
constexpr int   kFaderUnityByte    = 100;
constexpr float kChannelUnityGain  = 100.0f / 127.0f;

// ReBirth's PAN is not a constant-power law. Measured 2026-08-26 by sweeping
// the 909 channel's pan byte alone across 15 values and reading L and R
// separately: the NEAR side stays at unity for the whole sweep, and only the
// far side is attenuated, by
//
//     far_dB = -45.9 * x^2,   x = |byte - 64| / 63
//
// which reproduces every point from byte 8 to 60 within 0.2% of the constant
// (-45.87 to -46.04 across 36 dB). Centre therefore sits 3.01 dB ABOVE a hard
// pan in total power, where a constant-power law puts it 0 dB. Two departures
// from the curve, both real and both measured: bytes 60 to 68 are a centre
// DETENT that attenuates nothing at all, and bytes at or past 4 from either end
// cut the far side to digital silence rather than to the curve's -42 dB.
//
// Only the shape is measured; the absolute is absorbed by the per-voice trims,
// which were all fitted on rbsgen fixtures that sit at centre. Scaling the law
// so centre still reads 1/sqrt(2), exactly what the old constant-power law gave
// there, therefore leaves every one of those fits untouched and changes only
// what the shape says: panned material, which used to be up to 3 dB too loud
// relative to centred material.
constexpr float kPanFarDb     = 45.9f;
constexpr float kPanDetent    = 0.095f;      // |x| below this: no attenuation
constexpr float kPanCut       = 0.92f;       // |x| above this: far side is silent
constexpr float kPanUnityGain = 0.70710678f; // continuity with the old law at centre

inline void panGains(float pan, float& gl, float& gr) {
    float x = pan < 0.0f ? -pan : pan;
    if (x > 1.0f) x = 1.0f;
    float far;
    if (x <= kPanDetent)   far = 1.0f;
    else if (x >= kPanCut) far = 0.0f;
    else                   far = std::pow(10.0f, -kPanFarDb * x * x / 20.0f);
    if (pan <= 0.0f) { gl = kPanUnityGain;       gr = kPanUnityGain * far; }
    else             { gl = kPanUnityGain * far; gr = kPanUnityGain;       }
}

// The pan byte spans 0..127 with 64 as centre, so the two halves are 64 and 63
// steps wide. Dividing by 63 makes byte 127 exactly hard right; byte 0 lands
// just past hard left and clamps, which costs nothing since the far side is
// already cut to silence there.
inline float panFromByte(int byte) {
    float v = (float(byte) - 64.0f) / 63.0f;
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

inline float faderGain(int byte, float unityGain = 1.0f) {
    if (byte <= 0) return 0.0f;
    return unityGain * std::pow(10.0f, kFaderDbPerStep * float(byte - kFaderUnityByte) / 20.0f);
}

// The inverses, for the writer. Saving a fader as round(gain * 127) put
// nominal songs back +9.99 dB hot and Clodhopper's master at -24.05 dB where it
// had loaded at -11.10 (review W2), because the two laws only agree at byte 100.
// faderByteFromGain(faderGain(b, u), u) == b for every byte at both unities the
// loader uses (test_fader_and_pan_inverses). A gain between two bytes rounds to
// the nearer step, and nothing above zero is written as byte 0, which is silence.
inline int faderByteFromGain(float gain, float unityGain = 1.0f) {
    if (!(gain > 0.0f)) return 0;
    const double steps = 20.0 * std::log10(double(gain) / double(unityGain))
                       / double(kFaderDbPerStep);
    const long b = std::lround(double(kFaderUnityByte) + steps);
    return b < 1 ? 1 : (b > 127 ? 127 : int(b));
}

// panByteFromPan(panFromByte(b)) == b for bytes 1..127. Byte 0 comes back as 1:
// panFromByte clamps both to -1.0, hard left, so the two render identically and
// only the stored byte differs.
inline int panByteFromPan(float pan) {
    const long b = std::lround(64.0 + 63.0 * double(pan));
    return b < 0 ? 0 : (b > 127 ? 127 : int(b));
}

// The delay's echo SEND is the same exponential fader law as the mixer volume,
// NOT a linear byte/127 (delay supertask, measured 2026-09-10 on the delay-taps
// probe): echo1/dry tracks the fader across send bytes 16 / 48 / 96 / 100 ->
// 0.009 / 0.035 / 0.267 / 0.317, where a linear send would read 0.15 at byte 48.
// The byte-100 return level, 0.317 of the post-fader dry at pan centre, is the
// absolute the wet return is anchored to, the same way every other caller
// anchors faderGain's absolute scale to its own byte-100 point (see above). The
// send is applied per channel at the tap, so its nonlinearity cannot be factored
// out to the shared return; the return then only applies the pan law.
constexpr float kDelaySendUnity = 0.317f;   // faderGain(100, this) = the byte-100 return level

inline float delaySendGain(float send01) {
    if (send01 <= 0.0f) return 0.0f;
    int b = (int) std::lround(send01 * 127.0f);
    return faderGain(b, kDelaySendUnity);
}

// Initial mixer state decoded from a song file (per engine channel).
struct MixerInit {
    float volume[4] = { 0.8f, 0.8f, 0.8f, 0.8f };   // 0..1
    float pan[4]    = { 0, 0, 0, 0 };               // -1..1
    bool present = false;
};

// Effect settings + routing decoded from the MIXR/DELY/DIST chunks.
constexpr int kCompMaster = 4;   // compTarget value meaning the master bus

struct EffectInit {
    float masterVolume = 1.0f;      // relative to the engine's default headroom
    bool  channelOn[4] = { true, true, true, true };
    float delaySend[4] = { 0, 0, 0, 0 };            // 0..1 (echo send per channel)
    // The distortion is a per-section FLAG, not a selector: any number of
    // sections can carry it. `MIXR` +1, which the loader used to read as the
    // distortion's target, is the compressor's, so until 2026-08-27 every song
    // sent the distortion to whichever section the compressor was on. Measured
    // by clicking each switch in ReBirth and diffing the save; see
    // docs/FORMAT_RBS.md section 6.
    bool  distOn[4] = { false, false, false, false };
    // The distortion UNIT's own on/off, from its track's param 0, separate from
    // which sections are routed to it. All sixteen demo songs state it, and all
    // sixteen state ON, so for real material this only matters if one ever
    // bypasses. It defaults FALSE because that is what ReBirth does with a file
    // that says nothing: a fixture whose DIST track carried no param 0 came
    // back from ReBirth completely undistorted with two sections routed.
    bool  distortionOn = false;
    float distAmount = 0.5f, distTone = 0.5f;       // 0..1
    // The PCF is an INSERT on exactly one section (manual p61, p64), stored as
    // an exclusive selector. -1 = routed nowhere, else engine channel index.
    int   pcfTarget = -1;
    bool  pcfOn = false;            // the unit's own bypass, PCF track param 0
    float pcfFreq = 0.5f, pcfQ = 0.5f, pcfAmt = 0.5f, pcfDecay = 0.5f;   // 0..1
    int   pcfPattern = 0;           // 0..54
    bool  pcfBandpass = false;      // mode byte: 0 = LP, 1 = BP
    // The compressor's target. `MIXR` +1: 0 = nowhere, 1 = the MASTER strip's
    // own COMP button, 2..5 = sections 1..4, and the four sections and the
    // master are mutually exclusive. Confirmed 2026-08-28 by clicking the
    // master button, which wrote 1 and REPLACED the section value that was
    // there. Here: -1 = nowhere, 0..3 = section, kCompMaster = master.
    int   compTarget = -1;
    int   delaySteps = 0;           // 0 = unknown, else 1..16 sixteenths
    float delayFeedback = 0.4f;     // 0..1
    // DELY +2 (triplet switch) and +4 (wet-return pan), decoded but dropped by
    // the loader until the delay supertask (2026-09-10). Byte 2's sense is
    // inverted from its name: 0 selects the TRIPLET grid (divide the beat by 3),
    // nonzero the STRAIGHT grid (divide by 4). The pan is the mixer pan byte.
    bool  delayStraight = false;    // DELY +2: false = triplet (/3), true = straight (/4)
    int   delayPan = 64;            // DELY +4: wet-return pan byte, 64 = centre
    // The delay UNIT's own on/off, DELY track param 0. The chunk has no byte
    // for it (byte 0 is the version, as on every effect chunk). All sixteen
    // demo songs switch it on at tick 0 and none moves it later, so the ON
    // default is what every real song states.
    bool  delayOn = true;
    // The master compressor's on/off, from the COMP track's param 0 at tick 0.
    // ReBirth's own panel defaults it ON, but a loaded song always states it,
    // and it must be honoured: ours used to run on EVERY render regardless,
    // while every fixture switches ReBirth's off, so we were comparing a
    // compressed render against an uncompressed one. It only bites on loud
    // material, which is why it hid for so long: on a stacked 909 hit at accent
    // 127 it cost 2.3 dB of accent gain and ducked the following unaccented
    // hits by 2.8 dB. Default false, for a song that does not state it. (This
    // once said ratio and threshold were undecoded; they were decoded and
    // fitted on 2026-09-16.)
    bool compressorOn = false;
    // The master compressor's Ratio and Threshold, from the COMP track's params
    // 1 and 2 at tick 0 (byte/127 -> 0..1), decoded 2026-09-16 for supertask 6.
    // The COMP chunk is [ver][ratio][threshold][meter=127]; byte 1 is Ratio
    // (measured monotonic, 0 at ratio 0) and byte 2 Threshold. The param->law
    // mapping (ratio slope, threshold in dBFS, auto make-up) lives in the
    // compressor DSP; here they are just the raw normalised controls. Defaults
    // mirror the DSP defaults so an absent COMP block is inert.
    float compRatio = 0.5f;         // COMP byte 1 / 127
    float compThreshold = 0.6f;     // COMP byte 2 / 127
    bool present = false;
};

struct Song {
    std::string title;
    std::string author;
    // Skin/mod this song was authored against, referenced BY NAME from GLOB+15
    // ("Standard ReBirth" = the stock look). Songs never embed mod content, so
    // this is a lookup key: see rbm.h findModByName() to resolve it against the
    // user's own mods and load the drum samples the song was written with.
    std::string skinName;
    int loopBars = 0;          // transport loop length in bars (0 = not stored)
    double tempo = 120.0;
    float  shuffle = 0.5f;     // 0..1 global shuffle AMOUNT (0.5 = factory
                               // default; applies only to shuffle-flagged patterns)
    // The loaded GLOB payload, byte for byte, and empty for a song built in
    // Reborn. The writer starts from it and patches only the fields modelled
    // above (tempo, loop length, shuffle, skin name), so the bytes nobody has
    // decoded yet survive a save: +0, +1 and +6..+10 (loop on/off, loop start
    // and mode are candidates) and the mod's download URL at +80.
    std::vector<uint8_t> globRaw;

    BassKnobs bass1Knobs, bass2Knobs;
    DrumKnobs drum808Knobs, drum909Knobs;
    // A song holds two kinds of state, and ReBirth keeps them apart. The
    // mixerInit/effectInit pair is the song START as played: the chunks, then
    // each track's tick-0 dump over them where it has one. The *Panel pair is
    // the CHUNKS alone, the panel as it stood at save time, which the writer
    // saves back as chunks. Only the start state reaches the engine. Fields no
    // track carries (the master, the channel on/off) are chunk values in both.
    // The chunks are the Pattern-mode panel (manual p137), and in Song mode
    // ReBirth plays the tracks, measured on the delay (review E3).
    MixerInit mixerInit;
    EffectInit effectInit;
    MixerInit mixerPanel;
    EffectInit effectPanel;
    std::vector<AutoEvent> automation;   // sorted by tick

    Bank<BassPattern> bass1;
    Bank<BassPattern> bass2;
    Bank<DrumPattern> drum808;
    Bank<DrumPattern> drum909;
    Bank<PcfPattern>  pcf;

    std::vector<SongStep> arrangement;  // empty => single-pattern (pattern mode)

    Song() {
        bass1.patterns.resize(1);
        bass2.patterns.resize(1);
        drum808.patterns.resize(1);
        drum909.patterns.resize(1);
        pcf.patterns.resize(1);
    }
};

} // namespace reborn
