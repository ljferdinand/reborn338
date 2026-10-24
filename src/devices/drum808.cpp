// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Clean-room TR-808-style analog-modelled drum machine.
//
// Every voice is synthesised from documented analog-circuit behaviour and
// standard DSP: there are NO samples, wavetables, or ROM data anywhere in
// this file. The 16-instrument panel layout and the classic parameter names
// (level_*, bd_tune, bd_decay, sd_tone, sd_snap) are preserved so existing
// songs keep loading; additional per-voice tune/decay controls are appended
// after the originals.
//
// Voice sketches (all analog-inspired):
//   BD  bridged-T style near-sinusoid, fast pitch drop + long resonant decay,
//       plus a short click transient.
//   SD  two tuned body oscillators (~180 / ~330 Hz) + high-passed noise snap,
//       with independent tone/snappy decays.
//   Toms/Congas  tuned decaying sines with a short downward pitch envelope.
//   CB  two square oscillators (~540 / ~810 Hz) through a cascaded bandpass.
//   CP  three-to-four rapid band-passed noise bursts then a short diffuse tail.
//   MA  a very short high-passed noise burst.
//   RS/CL  short resonant tone bursts.
//   CY/OH/CH  a bank of six inharmonic square oscillators through a high-pass
//       with metallic decays (closed short, open medium, cymbal long).
//
// Robustness: all resonant paths use a topology-preserving (TPT) state-variable
// filter which is unconditionally stable; cutoffs are clamped below sr*0.45 and
// every accumulator is denormal-flushed so no parameter combination can produce
// NaN/Inf/denormals.
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>
#include <string>
#include <cstdint>
#include <algorithm>

namespace reborn {
namespace {

// 16-voice panel order (frozen; do not reorder).
enum Voice808 { BD, SD, LT, MT, HT, LC, MC, HC, RS, CP, MA, CB, CY, OH, CH, CL, NV808 };

const char* kNames[NV808] = {
    "Bass Drum","Snare","Low Tom","Mid Tom","Hi Tom","Low Conga","Mid Conga","Hi Conga",
    "Rim Shot","Hand Clap","Maracas","Cowbell","Cymbal","Open Hat","Closed Hat","Claves"
};

// Appended per-voice controls (after the four classic params).
enum ExtraParam {
    EX_TOM_TUNE, EX_TOM_DECAY, EX_CB_TUNE, EX_CP_SPREAD,
    EX_CH_DECAY, EX_OH_DECAY, EX_CY_DECAY, EX_CY_TUNE, EX_ACCENT,
    EX_LT_TUNE, EX_MT_TUNE, EX_HT_TUNE,   // 808: independent per-tom tune (sample path)
    EX_COUNT
};
struct ExtraDesc { const char* name; const char* label; float def; };
const ExtraDesc kExtra[EX_COUNT] = {
    { "tom_tune",  "Tom/Conga Tune",  0.5f },     // SUPERSEDED 2026-08-25 and now unread: both paths resolve the per-column lt/mt/ht_tune below. Kept only so the extra-param INDICES stay stable for saved state; nothing writes it.
    { "tom_decay", "Tom/Conga Decay", 0.5f },
    { "cb_tune",   "Cowbell Tune",    0.5f },
    { "cp_spread", "Clap Spread",     0.5f },
    { "ch_decay",  "Closed Hat Decay",0.4f },
    { "oh_decay",  "Open Hat Decay",  0.5f },
    { "cy_decay",  "Cymbal Decay",    0.6f },
    { "cy_tune",   "Cymbal Tune",     0.5f },
    { "accent",    "Accent Amount",   0.25f },   // global ACCENT knob depth
    { "lt_tune",   "Low Tom Tune",    0.5f },
    { "mt_tune",   "Mid Tom Tune",    0.5f },
    { "ht_tune",   "Hi Tom Tune",     0.5f },
};

// Column tune lookup: each of the three tom columns has its own Tune knob,
// shared between the tom and the conga in that column (LT/LC, MT/MC, HT/HC).
// Both the sample and the synthesis path resolve through this.
inline int tomTuneEx808(int v) {
    return (v == LT || v == LC) ? EX_LT_TUNE
         : (v == MT || v == MC) ? EX_MT_TUNE
                                : EX_HT_TUNE;
}

// Per-voice output balance, fitted 2026-07-25 to the isolated single-voice
// exports (comp off): our synthesis/sample levels ran hot and unbalanced vs
// ReBirth: BD ~+16 dB, hats/cymbal ~+13..19 dB, toms ~+3..6 dB, while the
// snare ran quiet. These bring each voice's isolated peak to the export's.
// BD's reduction also lifts it out of the device tanh so accent can breathe.
// Order = Voice808: BD SD LT MT HT LC MC HC RS CP MA CB CY OH CH CL.
// Values are the 2026-07-25 fit x 0.78: the sequencer used to scale unaccented
// hits by 0.78 before calling trigger(), and the accent rework removed that.
// Folding it in here keeps every measured unaccented level exactly where it was.
const float kTrim808[NV808] = {
    // Re-fit 2026-08-18 by POWER averaging (per-hit RMS, not coherent waveform
    // averaging). ReBirth's hits are not sample-aligned, so a tonal or HF voice
    // partly cancels under coherent averaging and reads far too quiet; that is
    // the "never null-test" trap (docs/MEASUREMENT_PROTOCOL.md), and score808's old avg_hit walked
    // into it. Under the correct measure BD is unchanged but SD/CB/CY/OH/CH were
    // 4-16 dB off, and the six tom/conga voices land on one shared ~0.385 (they
    // were spread only because higher pitches cancelled more). Every voice below
    // is power-fit too; the percussion cluster lands near ~0.24-0.25.
    0.2415f, 1.1700f, 0.3859f, 0.3843f, 0.3846f, 0.3890f, 0.3865f, 0.3885f,   // SD 1.17 after the round-9 noise re-fit (louder snare noise)
    0.2402f, 0.2424f, 0.2492f, 0.2503f, 0.7004f, 0.0624f, 0.1070f, 0.2336f    // CY 0.70 after the brightness lowpass (cuts ~9.7 dB HF); RS/CP/MA/CL power-refit 2026-08-18
};

// The SYNTHESIS path needs its own table. Trims are applied at the output
// stage, so one table cannot serve both: a synthesized voice and a sampled one
// have no reason to arrive at the same level, and the numbers above were fitted
// with the sample set loaded. Fitted 2026-07-25 the same way, rendering each
// isolated fixture with NO sample directory.
//
// Re-fit 2026-08-24 by POWER averaging (tools/measure/fit_trims808.py --synth)
// after the synth-pass shape work, which moved most voices' energy. Every voice
// is measured from its isolated default-knob fixture. This corrected the round-3
// levels, which were set through the same coherent-averaging bug that had mis-set
// the sample trims: the largest were MA +11.2 dB (a plain high-pass had shelved
// its band to Nyquist), BD +7.1 dB (its tone level curve was wrong), CP +3.3,
// SD -3.3 (louder noise), CB -3.2 (never power-fit), OH -4.2. The six tom/conga
// voices, whose shapes were untouched, moved under 1 dB. The three toms came from
// batch F (2026-07-25), the first fixtures to put a tom-column switch at 1.
// Re-fit again 2026-08-24 after the ear-correction pass on Luke's listening
// (RS click, CB/SD/OH/CY reshaped). CB +8.6 dB and OH +8.5 dB moved most, their
// filters and envelopes having changed; the rest under 0.5 dB. Every voice again
// within ~0.1 dB of its export at the default knob.
const float kTrimSyn808[NV808] = {
    0.1034f, 0.1845f, 0.1578f, 0.1999f, 0.1722f, 0.1870f, 0.1403f, 0.1859f,
    0.1483f, 0.2664f, 0.4124f, 0.0606f, 0.3357f, 0.1762f, 0.2637f, 0.1243f
};

// Accent depth, re-measured 2026-08-26 (steps 0/8 accented, knob swept, one
// render so the ratio needs no level matching):
//
//   knob     0     32     64     96    127
//   gain 1.011  2.026  3.041  4.054  5.034
//
// A free fit gives 1.0121 + 4.0233 * knob with residuals under 0.0015, so the
// law is exactly linear; the intercept sits a hair above 1 because the
// unaccented window catches the previous accented hit's tail. Unaccented steps
// do not move at all and the row alone does nothing at knob 0.
//
// The 3.95 this replaces was fitted through the device's own tanh, which
// compressed the accented hits it was measuring. The 808's accent cell value
// is 1, which is what an 808 writes, so unlike the 909's law this one needed
// only the saturator removed, not a different fixture.
constexpr float kAccentDepth = 4.02f;

// White-noise source (xorshift32). Deterministic, allocation-free.
struct Rng {
    uint32_t s = 0x808u;
    inline float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return int32_t(s) * (1.0f / 2147483648.0f); // [-1, 1)
    }
};

// Per-sample amplitude decay coefficient: env *= coef gives an exp decay whose
// time-to-1/e is 't' seconds. Bounded so it is always in (0, 1).
inline float envCoef(double t, double sr) {
    if (t < 1.0e-4) t = 1.0e-4;
    return (float)std::exp(-1.0 / (t * sr));
}

// Topology-preserving-transform state-variable filter. Unconditionally stable
// across the full audio range; coefficients are computed once per hit (never in
// the inner render loop). Produces low/band/high outputs simultaneously.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;   // integrator state
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    void set(double fc, double Q, double sr) {
        fc = clampf((float)fc, 10.0f, (float)(sr * 0.45)); // guard < sr*0.45
        if (Q < 0.05) Q = 0.05;
        double g  = std::tan(kPi * fc / sr);
        double kk = 1.0 / Q;
        a1 = (float)(1.0 / (1.0 + g * (g + kk)));
        a2 = (float)(g * (double)a1);
        a3 = (float)(g * (double)a2);
        k  = (float)kk;
    }

    inline void tick(float x, float& lp, float& bp, float& hp) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal(2.0f * v1 - ic1);
        ic2 = flushDenormal(2.0f * v2 - ic2);
        bp = v1;
        lp = v2;
        hp = x - k * v1 - v2;
    }
};

struct VoiceState {
    bool  active = false;
    float vel = 1.0f;
    // Amplitude envelopes (meaning is per-voice; unused ones stay 0).
    float env1 = 0.0f, env2 = 0.0f, env3 = 0.0f;
    float pEnv = 0.0f;                       // pitch envelope (BD, toms)
    // Oscillator phases.
    double ph1 = 0.0, ph2 = 0.0;
    double mph[6] = { 0,0,0,0,0,0 };          // metallic partial phases
    // Resonant filters.
    Svf f1, f2;
    // Clap burst bookkeeping.
    int   burst = 0;
    float burstT = 0.0f;
    // Open-hat decay (synth path): a two-phase envelope. It decays gently (a long
    // tau) for ohHold seconds, then faster (ohTau), matching ReBirth's plateau-
    // then-drop shape. Both set at trigger from the decay knob; a same-step CH
    // choke overrides them to a fast release.
    float ohHold = 0.0f;
    float ohTau  = 0.05f;
};

class Drum808 : public Instrument {
public:
    Drum808() {
        for (int v = 0; v < NV808; ++v) level_[v] = 0.85f;
        level_[CY] = 0.6f; level_[OH] = 0.7f; level_[CH] = 0.7f;
        tune_ = 0.5f; bdDecay_ = 0.6f; sdTone_ = 0.5f; sdSnap_ = 0.5f;
        for (int e = 0; e < EX_COUNT; ++e) extra_[e] = kExtra[e].def;
    }

    void prepare(double sr) override { sr_ = (sr > 1000.0 ? sr : kDefaultSampleRate); reset(); }
    // Everything a hit leaves running starts again from silence, so play()
    // renders the same whatever played before it: the synthesis voices, the
    // noise source's seed, and with samples loaded the sample voices, the
    // sample-path snare's noise filter and the BD and CY one-poles. Without the
    // seed a song loaded after another rendered differently from the same song
    // opened fresh; without the sample half, a sample ringing at play() rang on
    // over the new start, and the snare filter's state carried from song to
    // song (review 2026-09-27). Whether ReBirth cuts a ringing drum at Play is
    // unmeasured; the synthesis voices always have been cut here.
    void reset() override {
        for (auto& v : vs_) v = VoiceState{};
        rng_ = Rng{};
        for (int v = 0; v < NV808; ++v) { smpOn_[v] = false; smpPos_[v] = 0.0; smpEnv_[v] = 0.0f; }
        snNoiseEnv_ = 0.0f;
        snSvf_.reset();
        bdLp_ = cyLp_ = cyBrt_ = 0.0f;
    }

    int numVoices() const override { return NV808; }
    std::string voiceName(int v) const override { return (v >= 0 && v < NV808) ? kNames[v] : ""; }

    int numParams() const override { return NV808 + 4 + EX_COUNT; }

    ParamInfo paramInfo(int i) const override {
        if (i >= 0 && i < NV808)
            return { std::string("level_") + kNames[i], std::string(kNames[i]) + " Level", 0, 1, 0.85f, "" };
        int j = i - NV808;
        switch (j) {
            case 0: return { "bd_tune",  "BD Tune",   0, 1, 0.5f, "" };
            case 1: return { "bd_decay", "BD Decay",  0, 1, 0.6f, "" };
            case 2: return { "sd_tone",  "SD Tone",   0, 1, 0.5f, "" };
            case 3: return { "sd_snap",  "SD Snappy", 0, 1, 0.5f, "" };
            default: break;
        }
        int e = j - 4;
        if (e >= 0 && e < EX_COUNT)
            return { kExtra[e].name, kExtra[e].label, 0, 1, kExtra[e].def, "" };
        return { "unused", "Unused", 0, 1, 0, "" };
    }

    float getParam(int i) const override {
        if (i >= 0 && i < NV808) return level_[i];
        int j = i - NV808;
        switch (j) {
            case 0: return tune_;
            case 1: return bdDecay_;
            case 2: return sdTone_;
            case 3: return sdSnap_;
            default: break;
        }
        int e = j - 4;
        return (e >= 0 && e < EX_COUNT) ? extra_[e] : 0.0f;
    }

    void setParam(int i, float x) override {
        x = clampf(x, 0, 1);
        if (i < 0) return;
        if (i < NV808) { level_[i] = x; return; }
        int j = i - NV808;
        switch (j) {
            case 0: tune_ = x; return;
            case 1: bdDecay_ = x; return;
            case 2: sdTone_ = x; return;
            case 3: sdSnap_ = x; return;
            default: break;
        }
        int e = j - 4;
        if (e >= 0 && e < EX_COUNT) extra_[e] = x;
    }

    // Optional per-voice PCM sample (plays INSTEAD of the synthesis model;
    // level/accent still apply). Loader-thread only, not RT-safe.
    void setSample(int v, std::vector<float> data, double rootRate) override {
        if (v < 0 || v >= NV808) return;
        smp_[v] = std::move(data);
        smpRate_[v] = (rootRate > 1000.0) ? rootRate : 44100.0;
        smpOn_[v] = false; smpPos_[v] = 0.0;
    }

    void trigger(int v, float vel, bool accent, bool) override {
        if (v < 0 || v >= NV808) return;
        if (!smp_[v].empty()) {                      // sample mode
            if (v == CH && smpOn_[OH]) {
                if (smpPos_[OH] < 2.0) {
                    // CH and OH on the SAME step: ReBirth plays a very short OPEN
                    // hat, not the closed hat (manual p149; measured 2026-08-18,
                    // choh_same is spectrally the open hat cut to ~60 ms). OH is
                    // voice 13 and fires just before CH (14) in the same block, so
                    // its position is still zero here. Keep OH, choke it fast, and
                    // suppress this CH hit. See docs/research/DRUM808_FINDINGS.md round 8.
                    ohGate_ = 0.0f;                      // release begins at once
                    ohRelC_ = envCoef(0.028, sr_);       // ~28 ms choke
                    return;
                }
                smpOn_[OH] = false;                      // later CH chokes a ringing OH
            }
            smpOn_[v]  = true;
            smpPos_[v] = 0.0;
            smpRatio_[v] = sampleTuneRatio(v);       // knob varispeed (was missing)
            smpVel_[v] = clampf(vel, 0.0f, 1.5f)
                       * (accent ? (1.0f + extra_[EX_ACCENT] * kAccentDepth) : 1.0f);
            if (v == BD) {
                // Tone-minimum gate. The kick is identical across tone 16..127, but
                // at the very bottom ReBirth cuts it short: measured 2026-08-23,
                // tone 0..8 gate to silence at ~190 ms (the pre-gate sound matches
                // tone 16) while tone 16 already rings the full length. So the tone
                // knob's only sample-path effect is to shorten the tail near zero.
                // A level attenuation matched the RMS but not the sound (Luke's ear
                // caught the long ring); this gate is the real mechanism.
                bdGate_ = (tune_ < 0.10f) ? (float)(0.183 * sr_) : 1.0e9f;
                bdRelC_ = envCoef(0.002, sr_);       // reaches silence by ~200 ms
                smpVel_[v] *= bdToneGain(tune_);      // body level (see helper)
                // Hi-cut: corner 100 Hz at tone 0..8, opening up through tone 16
                // as the harmonics roll back in. bdLpA_ == 1 is an EXACT passthrough
                // (renderSample skips the filter), so the kick is byte-identical at
                // tone 16..127; these three effects touch only the bottom of the knob.
                if (tune_ >= 0.126f) {
                    bdLpA_ = 1.0f;
                } else {
                    double lpfc = (tune_ < 0.063f) ? 100.0
                                : 100.0 + (tune_ - 0.063f) / 0.063f * (20000.0 - 100.0);
                    bdLpA_ = 1.0f - (float)std::exp(-2.0 * kPi * lpfc / sr_);
                }
                bdLp_ = 0.0f;
            }
            // Only BD, OH and CY have a Decay knob on the 808 panel (MIDI map
            // p196: BD 40, OH 62, CY 60; toms carry Level/Tune/Switch and CH only
            // Level). Those three shape the sample the way the original applied
            // its panel knobs to its PCM voices; every other voice plays its
            // recording at natural length. The toms and CH used to run a decay
            // envelope here with no knob behind it, which truncated the natural
            // tom ring ~19 dB low by 250-400 ms (measured 2026-08-18 on the tune
            // sweep), so those cases are gone. See docs/research/DRUM808_FINDINGS.md r8.
            double tau = 0.0;
            switch (v) {
                case BD: {
                    // BD decay re-fit 2026-08-17 against the bdd sweep. ReBirth's
                    // observed decay tracks 0.040 * 7.063^(decay^0.776) s (40/82/
                    // 120/287 ms at decay 0/.25/.5/1). The BD sample rings ~0.287 s
                    // on its own and the envelope multiplies it, so the envelope
                    // tau is the reciprocal remainder, going flat near the top
                    // where the sample alone already gives the full length. The old
                    // 0.028*165^(decay^2) decayed ~2x too fast in the low-mid.
                    const double tauObs = 0.040 * std::pow(7.063, std::pow((double)bdDecay_, 0.776));
                    const double tauSmp = 0.287;
                    tau = (tauObs < tauSmp * 0.97) ? 1.0 / (1.0/tauObs - 1.0/tauSmp) : 100.0;
                    break;
                }
                // CY is an exponential ring. Re-fit 2026-08-18 against the
                // wide-spaced cyd sweep (RB -30 dB length 85/170/230/395/820 ms
                // across the knob), WITH the brightness lowpass in place: the
                // lowpass strips the fast-decaying HF so the slower low-mid tail
                // dominates the envelope, which also all but removed the old
                // max-decay sample-limit. A single exponential cannot hit both the
                // mid and the top exactly, so this balances them (mid ~20% long,
                // top ~8% short); the residual is the cymbal's non-exponential
                // envelope shape.
                case CY: tau = 0.0400 * std::pow(33.00, (double)extra_[EX_CY_DECAY]); break;
                // OH is NOT exponential; it is a gate (see renderSample). Falls
                // through to natural (tau 0) here; the gate is applied per sample.
                default: break;                      // OH/toms/CH/SD/RS/CP/MA/CB/CL: natural
            }
            smpEnvC_[v] = (tau > 0.0) ? envCoef(tau, sr_) : 1.0f;
            if (v == OH) {
                // OH decay gate: hold the natural sample to T_gate(decay), then a
                // fast 16 ms release to silence. Fitted 2026-08-18 to the ohd
                // sweep: hold = min(0.018 + 0.721*decay, 0.585) s tracks RB's -30
                // dB length (75/245/415/565 ms at decay 0/.25/.5/.75) near exactly.
                const double hold = std::min(0.033 + 0.721 * (double)extra_[EX_OH_DECAY], 0.585);
                ohGate_ = (float)(hold * sr_);
                ohRelC_ = envCoef(0.016, sr_);
            }
            smpEnv_[v]  = 1.0f;
            // CY tone on the sample path. ReBirth's cymbal Tone knob is a
            // first-order TILT whose PIVOT MOVES with the knob, measured and
            // fitted 2026-08-18 from the cyt sweep in absolute band energy. Tone
            // up cuts the body broadly (0-1 kHz -9.1 dB, cut reaching ~7 kHz) and
            // lifts the sizzle (16-20 kHz +4.0 dB); tone down adds a NARROW low
            // thump (below ~2 kHz) and slightly dulls the top. The 11.6 kHz
            // dominant never moves, and the hit runs ~2.7 dB louder at full tone
            // because the peak sits above the pivot. A fixed pivot cannot do both
            // the broad cut and the narrow boost, so fc rises 1.5 -> 8.5 kHz
            // across the knob. Fit residual 1.2 dB RMS. Sample-only; OH/CH have
            // no tone knob. See docs/research/DRUM808_FINDINGS.md round 8.
            if (v == CY) {
                const double t    = (double) clampf(extra_[EX_CY_TUNE], 0.0f, 1.0f);
                const double th   = std::tanh(2.7 * (t - 0.5));
                const double gloD = -9.9 * th;                          // low asymptote dB
                const double ghiD = (t >= 0.5 ? 8.5 : 6.0) * th;        // high asymptote dB
                cyGLo_ = (float) std::pow(10.0, gloD / 20.0);
                cyGHi_ = (float) std::pow(10.0, ghiD / 20.0);
                const double fc = 1500.0 + 7000.0 * t;                  // pivot rises with tone
                cyLpA_ = 1.0f - (float)std::exp(-2.0 * kPi * fc / sr_);
                cyLp_  = 0.0f;
                // Cymbal brightness lowpass, ~3 kHz one-pole. Fitted 2026-08-18 to
                // the cyd sweep: RB darkens the cymbal vs its own raw recording
                // (85% rolloff 13867 -> 12155) while leaving CH/OH untouched, and
                // this matches both the windowed centroid/rolloff and the centroid
                // fall across the whole decay. Tone-independent, so it sits after
                // the tilt. See docs/research/DRUM808_FINDINGS.md round 8.
                cyBrtA_ = 1.0f - (float)std::exp(-2.0 * kPi * 3000.0 / sr_);
                cyBrt_  = 0.0f;
            }
            // The original's snare samples are TONE BODIES; snappy mixes in a
            // separate noise burst (same hybrid as the hardware/909 path).
            if (v == SD) {
                snNoiseEnv_ = 1.0f;
                // Snare noise decay RE-FIT 2026-07-25: the whole 808 snare is a
                // SHORT hit (ReBirth tau ~0.027 s at snappy 0.5). The old
                // 0.05..0.25 s let the noise RING and dominate the integrated
                // spectrum (our centroid 4976 vs ReBirth 1888, dom on the noise
                // not the 180 Hz body). ~15..40 ms tracks the export.
                snNoiseC_   = envCoef(0.015 + (double)sdSnap_ * 0.025, sr_);
                // Snappy is the noise-burst LEVEL and must reach SILENCE at 0
                // (ReBirth's snappy-0 snare is pure body: dom 180 Hz, no hiss).
                // Squared taper keeps the body dominant until the knob is well
                // up, matching the export's centroid climb (1888 -> 5456 Hz).
                snNoiseLvl_ = sdSnap_ * sdSnap_ * 1.1f * smpVel_[v];
                snSvf_.set(2200.0 + sdSnap_ * 1200.0, 0.6, sr_);
            }
            return;
        }
        if (v == CH) {
            // Closed hat and open hat interaction, ported to the synth path from
            // the sample path (manual p149, measured round 8). CH and OH on the
            // SAME step give a very short OPEN hat, not the closed hat; a CH
            // landing on a still-ringing OH from an EARLIER step just chokes it. A
            // same-step OH is still fresh here, since its startVoice ran earlier in
            // this block (OH is voice 13, CH is 14) so its gate timer is still 0
            // and its envelope full. The synth path used to kill OH and play the
            // closed hat, the behaviour round 8 proved wrong.
            if (vs_[OH].active && vs_[OH].env1 > 0.999f) {
                vs_[OH].ohHold = 0.0f;               // no plateau: choke at once
                vs_[OH].ohTau  = 0.020f;             // ~69 ms to -30 dB, per choh_same
                return;                              // suppress this CH hit
            }
            vs_[OH].active = false;                  // later CH chokes a ringing OH
        }
        VoiceState& s = vs_[v];
        s = VoiceState{};
        s.active = true;
        s.vel = clampf(vel, 0.0f, 1.5f)
              * (accent ? (1.0f + extra_[EX_ACCENT] * kAccentDepth) : 1.0f);
        s.env1 = 1.0f;
        s.pEnv = 1.0f;
        const double sr = sr_;

        switch (v) {
            case BD:
                s.env3 = 1.0f;                       // click transient
                break;
            case SD:
                s.env2 = 1.0f;                       // noise snap
                // The snare noise has a FIXED bright spectrum and snappy sets its
                // LEVEL, not its colour. Re-measured 2026-08-24 under power
                // averaging: ReBirth's composite centroid climbs 177 -> 3619 ->
                // 6010 Hz across the snappy knob purely because more of one bright
                // noise burst is mixed over the constant 180 Hz body, so the noise
                // itself sits near centroid 6500 / rolloff 10500. The old
                // 2200-3400 Hz band-pass (and its snappy-dependent centre) capped
                // the composite at 5201 even at full snappy, against ReBirth's
                // 6010, and left the default setting far too dark (1982 vs 3619).
                // Two stages form a wide band (a single high-pass would shelf to
                // Nyquist, the maraca/hat defect); the centre no longer moves with
                // snappy. The low corner was lowered from 2.6 kHz to 1.3 kHz
                // 2026-08-24 (Luke's ear: the snare sounded hollow/low): our body is
                // two pure sines and the noise started at 2.6 kHz, leaving a -5 dB
                // hole in 500-2000 Hz where ReBirth's recorded snare has continuous
                // energy. The 1.3-9.5 kHz band fills it (that whole region now within
                // ~2 dB) while the snappy sweep still tracks (centroid 3646 vs 3619).
                s.f1.set(1300.0, 0.5, sr);
                s.f2.set(9500.0, 0.7, sr);
                break;
            case LT: case MT: case HT: case LC: case MC: case HC:
                s.env3 = 1.0f;                       // tiny noise attack
                break;
            case RS:
                s.env3 = 1.0f;                       // attack click
                // DC blocker: a decaying sine that starts at phase 0 is an
                // asymmetric pulse and carries a large DC/sub-bass term (our
                // summed partials read a runaway 0-100 Hz band); ReBirth's
                // rimshot rings through a resonator and has none. A ~45 Hz
                // high-pass on the output removes it without touching the 484 Hz
                // fundamental. Fitted 2026-08-24.
                s.f1.set(45.0, 0.7, sr);
                break;
            case CP:
                s.burst = 0; s.burstT = 0.0f; s.env2 = 0.0f;
                // Clap band-pass widened and raised 2026-08-24 to d808_cp_ref:
                // ReBirth's clap peaks ~1195 Hz but carries a broad band, 62% in
                // 1-3 kHz and another 29% from 3-16 kHz, for a centroid of 3160
                // and an 85% rolloff of 5539. The old 1050 Hz / Q 1.3 pass capped
                // the centroid at 1902 and the rolloff at 2772, far too dark. A
                // lower Q at a higher centre passes the upper band.
                s.f1.set(1400.0, 0.62, sr);
                break;
            case MA:
                // Maraca band 2026-08-24 to d808_ma_ref: ReBirth's maraca sits at
                // 8-16 kHz (88% of its energy) peaking ~12.3 kHz, centroid 12079,
                // 85% rolloff 16161. A single high-pass passes everything to
                // Nyquist and gave rolloff 19552 (the same shelf defect fixed on
                // the hats); a band-pass at 12 kHz rolls off both sides.
                s.f1.set(12600.0, 0.6, sr);
                break;
            case CB: {
                s.env3 = 1.0f;                       // attack emphasis
                // A high-pass then a low-pass, framing the 807 dominant. Re-fit
                // 2026-08-24 (Luke's ear: the cowbell sounded an octave low). The
                // old cascaded pair at 810 Hz Q 1.6 was so narrow it killed the
                // square harmonics: ours read 99.7% in 300-1000 Hz with a strong
                // 538 Hz sub-partial, while ReBirth carries 9% above 1 kHz (band
                // split 90.6/8.8/0.6, harmonics to ~2.7 kHz). A high-pass at 700 Hz
                // cuts the 540 fundamental, a low-pass at 2400 rolls off the top to
                // ReBirth's ~9% harmonic level, and the 810 oscillator is weighted
                // higher so 807 dominates. Lands the band split at 89/10/0.8 with
                // the rolloff exact; the 538 sub-partial is pulled to -10.5 dB
                // (ReBirth's is -14.9, it sits a fifth under the dominant and cannot
                // be fully removed without cutting 807, an accepted residual).
                double t = std::pow(2.0, (extra_[EX_CB_TUNE] - 0.5) * 0.5);
                s.f2.set(700.0 * t, 0.7, sr);        // high-pass: cut the 540 fundamental
                s.f1.set(2400.0 * t, 0.7, sr);       // low-pass: roll off the top
                break;
            }
            case CY: case OH: case CH: {
                // CY TUNE is a spectral TILT, not a pitch control: across the knob
                // ReBirth's dominant stays pinned (measured 7267.5 on cyl_064, and
                // held across the whole cyt sweep to within a bin) while the
                // centroid rises 8053 -> 8463 -> 10207. The synth used to multiply
                // the filter cutoffs by a pitch factor, which DRAGGED the dominant
                // 5599 -> 10400 across the knob, the wrong dimension. The tune tilt
                // is now a brightness mix in renderVoice; the filters are fixed.
                if (v == CH) {                       // brightest, top-weighted
                    s.f1.set(9500.0, 0.7, sr);
                    s.f2.set(16500.0, 1.0, sr);       // sets the 85% rolloff
                } else if (v == OH) {                // clean high band, ~7.5 kHz peak
                    // ReBirth's open hat has essentially NO energy below 3.5 kHz
                    // (measured 2026-08-24: 0.00% in every band under 2 kHz) and
                    // peaks in 6-9 kHz (60%). Our square oscillators have strong
                    // sub-800 Hz fundamentals, and a single 12 dB/oct high-pass let
                    // them leak +23 to +35 dB where ReBirth is silent. A CASCADED
                    // high-pass at 6 kHz (24 dB/oct, both SVFs high-pass) cuts the
                    // fundamentals and the low skirt hard, so below 3.5 kHz is clean
                    // (<0.25%) as ReBirth is. The trade with only two poles is that
                    // the top stays open, so ours runs a little hissier above 13 kHz
                    // than ReBirth's gentler rolloff (an accepted residual; the mud
                    // and the envelope were the audible faults Luke flagged).
                    s.f1.set(6000.0, 0.7, sr);
                    s.f2.set(6000.0, 0.7, sr);
                    // Two-phase decay (see renderVoice): a gentle plateau for
                    // ohHold s, then a faster fall. Fitted 2026-08-24 to the ohd
                    // sweep (RB -30 dB 71/239/409/558/565 ms); this lands 69 and
                    // 409 ms at decay 0 and 0.5 and runs a little long at the top.
                    s.ohHold = 0.35f * (float)extra_[EX_OH_DECAY];
                    s.ohTau  = 0.020f + 0.115f * (float)extra_[EX_OH_DECAY];
                } else {                             // CY: fixed low corner, tune moves the top
                    // The three metal voices share ReBirth's ~7268 Hz dominant.
                    // OH's high-pass at 7000 lands it (7197); CY's sat at 5000 and
                    // read a low 5599. The low corner is fixed at 6800 2026-08-24
                    // so the dominant matches and stays pinned across the knob,
                    // without touching OH or CH (filters are per-voice). The tune
                    // knob tilts brightness by moving the band TOP (f2) and the raw
                    // high-pass mix in renderVoice, not by pitch-shifting.
                    const double cyT = clampf(extra_[EX_CY_TUNE], 0.0f, 1.0f);
                    s.f1.set(6800.0, 0.7, sr);
                    s.f2.set(10500.0 + 7000.0 * cyT, 1.0, sr);
                }
                break;
            }
            default:
                break;
        }
    }

    void process(StereoBlock& out, int n) override {
        for (int i = 0; i < n; ++i) {
            float mix = 0.0f;
            for (int v = 0; v < NV808; ++v) {
                // Balance comes from whichever path this voice is actually
                // rendering through: a kit can be part sampled, part synthesized.
                const float trim = smp_[v].empty() ? kTrimSyn808[v] : kTrim808[v];
                mix += renderVoice(v) * level_[v] * level_[v] * trim;  // taper + balance
            }
            // Reference-tuned device gain (see drum909 note). LINEAR, and it
            // must stay linear: this was tanh(mix * 0.8) * 1.6, same 1.28
            // small-signal gain but asymptotic at 1.6, so it began compressing
            // almost immediately and hard-capped the device. Driven with all
            // eleven voices at 127 and accent 127 our peak sat at -26.1 dBFS
            // where ReBirth reached -17.5. ReBirth's 808 bus has no saturator:
            // measured 2026-08-26, its accent gain on that stacked fixture is
            // 5.041 against 5.034 on a single voice, i.e. identical.
            float s = flushDenormal(mix * 1.28f);
            out.left[i] = s;
            out.right[i] = s;
        }
    }

private:
    inline float noise() { return rng_.next(); }

    static inline void wrap(double& p) { while (p >= 1.0) p -= 1.0; }

    // Per-voice sample playback pitch ratio. A voice with a real TUNE knob (the
    // three tom columns, cowbell) repitches its PCM via varispeed at natural
    // pitch at knob centre; a voice whose panel knob is TONE not tune does NOT
    // repitch. Measured against the exports 2026-08-17: the tom pitches track
    // ReBirth within an FFT bin, and BD holds 48.4 Hz across its whole knob.
    // BD sample-path body level vs its Tone knob. Separate from the tone-min gate:
    // ReBirth also quietens the kick's body near tone 0 (peak ~-5 dB, first-150 ms
    // RMS ~-4.5 dB vs tone 64), measured 2026-08-23 in the pre-gate region. Holds
    // ~-4.6 dB flat across tone 0..8, ramps up through tone 16 (-0.5 dB), clears by
    // tone 32. Piecewise; the gate handles the tail, this handles the body level.
    static float bdToneGain(float t) {
        float db;                                    // -3.4 at the min: the hi-cut
        if      (t < 0.063f) db = -3.1f;             // lowpass takes the other ~0.9
        else if (t < 0.126f) db = -3.1f + (t - 0.063f) / 0.063f * (-0.53f - (-3.1f));
        else if (t < 0.25f)  db = -0.53f + (t - 0.126f) / 0.124f * (0.0f - (-0.53f));
        else                 db = 0.0f;
        return std::pow(10.0f, db / 20.0f);
    }

    float sampleTuneRatio(int v) const {
        auto semis = [](double s) { return (float)std::exp2(s / 12.0); };
        switch (v) {
            // BD's panel knob is TONE, not tune: one recording, fundamental pinned
            // at 48.4 Hz across the knob. A repitch here was wrong; any BD-tone
            // brightness shaping belongs in a filter, measured before adding.
            case BD: return 1.0f;
            // Each tom/conga plays its sample at NATURAL pitch at knob centre
            // (ratio 1.0), tuned +/- 2 semitones by its own knob. The old ~2.1x
            // factors pitched every tom ~an octave high against the export.
            case LT: case LC: return semis((extra_[EX_LT_TUNE] - 0.5) * 4.0);
            case MT: case MC: return semis((extra_[EX_MT_TUNE] - 0.5) * 4.0);
            case HT: case HC: return semis((extra_[EX_HT_TUNE] - 0.5) * 4.0);
            case CB: return semis((extra_[EX_CB_TUNE] - 0.5) * 4.0);
            // NOT the cymbal: its tune knob is a spectral tilt, not varispeed.
            // ReBirth's dominant holds at 7742.7 Hz across the whole knob (and
            // across the decay knob), so resampling it here was wrong in the
            // sample path too, not only in synthesis.
            default: return 1.0f;
        }
    }

    // Render a playing PCM sample (linear interpolation, rate = root/host).
    inline float renderSample(int v) {
        if (!smpOn_[v]) return 0.0f;
        const std::vector<float>& s = smp_[v];
        double pos = smpPos_[v];
        std::size_t i0 = (std::size_t) pos;
        if (i0 + 1 >= s.size()) { smpOn_[v] = false; return 0.0f; }
        float frac = float(pos - double(i0));
        float out = s[i0] + (s[i0 + 1] - s[i0]) * frac;
        smpPos_[v] = pos + smpRate_[v] * (double)smpRatio_[v] / sr_;
        if (v == CY) {                               // tone tilt (see trigger)
            cyLp_ += cyLpA_ * (out - cyLp_);
            out = cyGLo_ * cyLp_ + cyGHi_ * (out - cyLp_);
            cyBrt_ += cyBrtA_ * (out - cyBrt_);      // then the brightness lowpass
            out = cyBrt_;
        }
        if (v == OH && pos >= ohGate_)               // decay gate (see trigger)
            smpEnvC_[v] = ohRelC_;                    // start the fast release
        if (v == BD) {
            if (bdLpA_ < 1.0f) {                      // tone-min hi-cut (see trigger)
                bdLp_ += bdLpA_ * (out - bdLp_);      // 1.0 = exact passthrough, skipped
                out = bdLp_;
            }
            if (pos >= bdGate_)                       // tone-min gate
                smpEnvC_[v] = std::min(smpEnvC_[v], bdRelC_);
        }
        out *= smpEnv_[v];
        smpEnv_[v] = flushDenormal(smpEnv_[v] * smpEnvC_[v]);
        if (smpEnv_[v] < 1.0e-4f) smpOn_[v] = false;
        out *= smpVel_[v];
        if (v == SD && snNoiseEnv_ > 1.0e-4f) {   // hybrid sampled snare
            float lp, bp, hp;
            snSvf_.tick(noise(), lp, bp, hp);
            out += bp * snNoiseEnv_ * snNoiseLvl_;
            snNoiseEnv_ = flushDenormal(snNoiseEnv_ * snNoiseC_);
        }
        return out;
    }

    float renderVoice(int v) {
        if (!smp_[v].empty()) return renderSample(v);
        VoiceState& s = vs_[v];
        if (!s.active) return 0.0f;
        const double sr = sr_;
        float o = 0.0f;

        switch (v) {
            case BD: {
                // The 808 kick holds ~50 Hz across the knob (fundamental 52 -> 50,
                // one FFT bin). The TONE knob is a LEVEL control, not a
                // body/click trade. Re-measured 2026-08-24 under power averaging:
                // ReBirth's kick stays 99.7% sub-100 Hz at EVERY tone (the 500-2k
                // band is 0.0% throughout), so the round-3 "trades body for click,
                // +12.4 dB in 500-2k" reading was a coherent-averaging artifact.
                // What the knob really does is raise the level 4.6 dB from tone
                // 0-8 up to tone 32, then hold flat. That is the same tone-minimum
                // quirk the sample path carries (a quiet, gated kick at the very
                // bottom of the knob).
                double f = 50.0 * (1.0 + 0.9 * s.pEnv);
                s.pEnv *= envCoef(0.012, sr);
                s.ph1 += f / sr; wrap(s.ph1);
                // Decay: ReBirth spans tau 0.038 s (knob 0) to 0.273 s (knob 127).
                double dec = 0.038 * std::pow(7.2, (double)bdDecay_);
                s.env1 *= envCoef(dec, sr);
                float body = (float)std::sin(s.ph1 * kTwoPi) * s.env1;
                // A small fixed click for attack. It carries negligible power (our
                // kick is 100% sub-100 too), so it is not tone-dependent.
                s.ph2 += 400.0 / sr; wrap(s.ph2);
                s.env3 *= envCoef(0.003, sr);
                float click = ((float)std::sin(s.ph2 * kTwoPi) * 0.6f + noise() * 0.4f) * s.env3;
                // Tone as a level curve, fitted to ReBirth's rise-then-flat RMS:
                // -4.6 dB at tone 0-8, ramping through tone 16 (-0.5 dB) to full by
                // tone 32.
                const float tn = clampf(tune_, 0.0f, 1.0f);
                float toneDb;
                if      (tn < 0.063f) toneDb = -4.57f;
                else if (tn < 0.126f) toneDb = -4.57f + (tn - 0.063f) / 0.063f * (-0.49f - (-4.57f));
                else if (tn < 0.25f)  toneDb = -0.49f + (tn - 0.126f) / 0.124f * (0.0f - (-0.49f));
                else                  toneDb = 0.0f;
                const float toneGain = std::pow(10.0f, toneDb / 20.0f);
                o = (body * 1.9f + click * 0.35f) * toneGain;
                break;
            }
            case SD: {
                // The two bridged-T body partials are FIXED at ~180 and ~330 Hz;
                // TONE balances them rather than tuning them, and it NEVER
                // inverts the pair. Measured 2026-07-26 by reading each partial
                // directly per hit, which settled a disagreement between two
                // earlier metrics: ReBirth is 180-dominant at every setting, the
                // knob only bringing 330 toward parity.
                //
                //   330-minus-180:   tone 0  -23.9 dB
                //                    tone 64 -11.5 dB
                //                    tone 127 -2.3 dB
                //
                // The old crossfade drove the pair to +18.8 dB at full tone, so
                // 330 took over completely. Holding 180 at unity and scaling only
                // the upper partial reproduces the measured curve exactly:
                // r = 0.0638 * exp(3.1849*t - 0.6981*t^2) fits all three points.
                s.ph1 += 180.0 / sr; wrap(s.ph1);
                s.ph2 += 330.0 / sr; wrap(s.ph2);
                s.env1 *= envCoef(0.027, sr);      // body tau, ~flat across tone
                const float tn = clampf(sdTone_, 0.0f, 1.0f);
                const float r330 = 0.0638f * std::exp(3.1849f * tn - 0.6981f * tn * tn);
                float body = ((float)std::sin(s.ph1 * kTwoPi) +
                              (float)std::sin(s.ph2 * kTwoPi) * r330) * s.env1;
                // Snappy must reach SILENCE at zero: ReBirth's snappy-0 snare
                // is pure body (1.5-6 kHz sits 25.6 dB down) and its snappy-127
                // is noise-dominant. Same law the sample path already carries.
                s.env2 *= envCoef(0.015 + sdSnap_ * 0.025, sr);
                // Two-stage band, high-pass then low-pass, so the noise is a wide
                // 2.8-11 kHz band and not a shelf to Nyquist (see startVoice).
                float lp1, bp1, hp1; s.f1.tick(noise(), lp1, bp1, hp1);
                float lp2, bp2, hp2; s.f2.tick(hp1, lp2, bp2, hp2);
                // Snappy sets the noise LEVEL and must reach a true zero at zero
                // (ReBirth's snappy-0 snare is pure body). Near-linear, not
                // squared: at snappy 0.5 the 3-8 kHz band is only a few dB under
                // its snappy-127 value yet at snappy 0 it is absent.
                //
                // The noise prominence DOES rise with tone, and here the synth
                // path parts from the sample path for a structural reason. Under
                // power averaging ReBirth's composite centroid swings 1836 (tone
                // 0) to 4516 (tone 127) at fixed snappy. Round 9 traced that swing
                // on the SAMPLE path to a brighter body RECORDING and made the
                // noise tone-independent. Our synth body is two low sines and
                // cannot brighten the way a different recording does, so a
                // tone-independent noise reads far too bright at tone 0 (4325 vs
                // 1836) and too dark at tone 127. The audible effect is real, so
                // the synth noise carries it as a level term, refit here under
                // power averaging (not the round-3 constants, which were fitted
                // through the coherent-averaging bug).
                const float toneGain = 0.60f + 0.80f * tn;
                float snap = lp2 * s.env2 * (std::pow(sdSnap_, 1.1f) * 3.6f * toneGain);
                o = body * 0.7f + snap;
                break;
            }
            case LT: case MT: case HT: case LC: case MC: case HC: {
                // Tuned decaying sine with a short downward pitch envelope.
                // Base pitches are the measured dominants of the six extracted
                // voices, so a synthesized kit lands on the same notes a sampled
                // one does. (The conga values are also confirmed end-to-end:
                // ReBirth's own tom-column exports read 182.9 / 268.6 / 382.9 Hz,
                // because those fixtures had the shared columns set to conga.)
                double baseFreq;
                switch (v) {
                    case LT: baseFreq = 83.6;  break;
                    case MT: baseFreq = 129.2; break;
                    case HT: baseFreq = 174.6; break;
                    case LC: baseFreq = 183.5; break;
                    case MC: baseFreq = 269.8; break;
                    default: baseFreq = 382.2; break; // HC
                }
                // The tune knob spans +/-2 semitones, not +/-6: ReBirth sweeps
                // the low column 162.9 -> 205.7 Hz, a ratio of 1.263 over the
                // whole knob.
                //
                // Each COLUMN has its own Tune knob (MIDI CC 45/48/51), shared
                // between the tom and the conga in that column. Fixed 2026-08-25:
                // this read the legacy shared EX_TOM_TUNE, which NOTHING writes
                // (grep finds no setter for the name "tom_tune" anywhere), so it
                // sat at its 0.5 default forever and every tom and conga played
                // dead centre no matter what the song asked for. The round-7 split
                // that gave each column its own knob was applied to the sample path
                // only. This is the shipping default path, since no ReBirth-derived
                // samples ship, and 11 of the 16 demo songs move at least one column
                // off centre, 6 of them to the +/-2 semitone limit.
                baseFreq *= std::pow(2.0, (extra_[tomTuneEx808(v)] - 0.5) * (4.0 / 12.0));
                // Shallow bend, not a swoop. At +90% the sweep smeared so much
                // energy above the base that our strongest bin landed at 188 Hz
                // on a 129 Hz mid tom; ReBirth's toms show a tight cluster
                // within ~2 Hz of the base. The short voices (tau ~0.05 s) are
                // where a deep envelope does the damage, since the bend is then
                // a large fraction of the whole hit.
                double f = baseFreq * (1.0 + 0.16 * s.pEnv);
                s.pEnv *= envCoef(0.016, sr);
                s.ph1 += f / sr; wrap(s.ph1);
                // These rang 4-9x too long (tau ~0.30 vs ReBirth's 0.033-0.081).
                // Base tau is the measured value at the knob's centre; toms hold
                // ~1.3-1.5x longer than congas, in the ratio of the extracted
                // samples' own lengths.
                double baseTau;
                switch (v) {
                    case LT: baseTau = 0.094; break;
                    case MT: baseTau = 0.048; break;
                    case HT: baseTau = 0.050; break;
                    case LC: baseTau = 0.073; break;
                    case MC: baseTau = 0.036; break;
                    default: baseTau = 0.033; break; // HC
                }
                double dec = baseTau * std::pow(6.0, (double)extra_[EX_TOM_DECAY] - 0.5);
                s.env1 *= envCoef(dec, sr);
                s.env3 *= envCoef(0.003, sr);
                o = (float)std::sin(s.ph1 * kTwoPi) * s.env1 + noise() * s.env3 * 0.15f;
                break;
            }
            case RS: {
                // Rimshot rebuilt 2026-08-24 from d808_rs_ref (power-averaged).
                // The real voice is dominated by 484 Hz, with secondary partials
                // at ~883 and ~1766 Hz roughly 8 dB down, and essentially no
                // energy above 3 kHz (the 3-8 kHz band is 1.6% of the total). It
                // is very short: -30 dB by ~10 ms, so the tonal tau is ~3 ms. The
                // old model put its dominant at 1700 Hz with a 4250 Hz partial and
                // a broadband noise spit, which read dom 1701 / centroid 2097
                // against ReBirth's 484 / 579. Those frequencies were never
                // measured; the rs_ref fixture only arrived 2026-08-18.
                s.ph1     += 484.0  / sr; wrap(s.ph1);
                s.ph2     += 883.0  / sr; wrap(s.ph2);
                s.mph[0]  += 1766.0 / sr; wrap(s.mph[0]);
                s.env1 *= envCoef(0.0035, sr);
                s.env3 *= envCoef(0.0016, sr);       // attack click
                float tone = (float)std::sin(s.ph1 * kTwoPi)
                           + (float)std::sin(s.ph2 * kTwoPi) * 0.40f
                           + (float)std::sin(s.mph[0] * kTwoPi) * 0.40f;
                // A stronger, brief noise click on Luke's ear (2026-08-24: "a
                // little more click"). Kept short (~1.6 ms) so it adds attack
                // without lifting the steady-state centroid off ReBirth's low 579.
                float sig = tone * s.env1 + noise() * s.env3 * 0.28f;
                float lp, bp, hp; s.f1.tick(sig, lp, bp, hp);
                o = hp;                              // DC-blocked (see startVoice)
                break;
            }
            case CL: {
                // Pure short resonant sine tick.
                s.ph1 += 2500.0 / sr; wrap(s.ph1);
                s.env1 *= envCoef(0.011, sr);
                o = (float)std::sin(s.ph1 * kTwoPi) * s.env1;
                break;
            }
            case CP: {
                // Three-to-four rapid band-passed noise bursts, then a diffuse tail.
                float spread = 0.6f + extra_[EX_CP_SPREAD] * 0.9f;
                float interval = 0.010f * spread;
                s.burstT += 1.0f / (float)sr;
                if (s.burst < 3 && s.burstT >= interval) {
                    s.burst++; s.burstT = 0.0f; s.env1 = 1.0f;
                    if (s.burst == 3) s.env2 = 1.0f;   // launch tail
                }
                s.env1 *= envCoef(0.007, sr);          // sharp bursts
                s.env2 *= envCoef(0.050 * spread, sr); // diffuse tail
                float amp = s.env1 + s.env2 * 0.8f;
                float lp, bp, hp; s.f1.tick(noise(), lp, bp, hp);
                o = bp * amp * 1.2f;
                break;
            }
            case MA: {
                // Very short band-passed noise burst. ReBirth's maraca reaches
                // -30 dB by ~11 ms, so the tau is ~4 ms; the old 18 ms rang too
                // long. Band-pass output (not high-pass) so it rolls off above the
                // band, see startVoice.
                s.env1 *= envCoef(0.006, sr);
                float lp, bp, hp; s.f1.tick(noise(), lp, bp, hp);
                o = bp * s.env1 * 1.3f;
                break;
            }
            case CB: {
                // Two square oscillators (540/810 Hz) through ONE wide band-pass.
                // ReBirth's cowbell has its 807 Hz dominant, a second partial at
                // 1077 Hz, and harmonics to ~3.2 kHz (9% of its energy above 1 kHz).
                // Re-fit 2026-08-24 (Luke's ear: it sounded an octave low): the old
                // 4-pole band-pass at 810 killed the square harmonics and left a
                // strong 538 Hz sub-partial, so it read dull and low. A single wide
                // band-pass centred at ~1100 Hz passes the harmonics and pulls the
                // 538 down under the 807. Decay was also ~2.5x too long (env tau
                // 0.121 gave -20 dB at 187 ms against ReBirth's 96); ~0.06 s tracks.
                double t = std::pow(2.0, (extra_[EX_CB_TUNE] - 0.5) * 0.5);
                double f1 = 540.0 * t, f2 = 810.0 * t;
                s.ph1 += f1 / sr; wrap(s.ph1);
                s.ph2 += f2 / sr; wrap(s.ph2);
                // The 810 oscillator is weighted higher so 807 dominates and the
                // 538 fundamental sits well under it, as ReBirth's does (-14.9 dB).
                float sq = (s.ph1 < 0.5 ? 0.30f : -0.30f) + (s.ph2 < 0.5 ? 0.70f : -0.70f);
                float lp2, bp2, hp2; s.f2.tick(sq,  lp2, bp2, hp2);  // hp: cut the 540 fundamental
                float lp1, bp1, hp1; s.f1.tick(hp2, lp1, bp1, hp1);  // lp: roll off the top
                s.env1 *= envCoef(0.060, sr);  // ring
                s.env3 *= envCoef(0.008, sr);  // attack emphasis
                o = lp1 * s.env1 * (0.8f + s.env3 * 1.5f) * 1.5f;
                break;
            }
            case CY: case OH: case CH: {
                // Six inharmonic square oscillators, high-passed to a metallic hiss.
                static const double partials[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                float acc = 0.0f;                    // NB: no tune factor; see
                for (int k = 0; k < 6; ++k) {        // the tilt note in startVoice
                    s.mph[k] += partials[k] / sr; wrap(s.mph[k]);
                    acc += (s.mph[k] < 0.5 ? 1.0f : -1.0f);
                }
                float src = acc * (1.0f / 6.0f);
                float lp1, bp1, hp1; s.f1.tick(src, lp1, bp1, hp1);
                // f2 is fed from f1's HIGH-PASS output rather than the raw
                // source, so the pair forms a BAND instead of a shelf. A
                // high-pass alone passes everything up to Nyquist, which left
                // our hats fizzy: 85% rolloff 19181 against ReBirth's 15401 on
                // the closed hat.
                // CH and CY form a band as hp1 (high-passed) then low-passed
                // (lp2); OH cascades f1's BAND-PASS into f2 (bp2) for 24 dB/oct
                // skirts that kill the sub-800 Hz oscillator fundamentals ReBirth's
                // open hat does not carry. lp2 is the band-limited path for CH/CY;
                // the small hp1 additions keep some air above the band. CY tune
                // tilts brightness by mixing more of the raw high-pass as the knob
                // rises (fitted to the cyt sweep, RB centroid 8053/8463/10207),
                // while the band-limited path keeps the dominant pinned.
                const float cyHi = 0.06f + 0.42f * clampf(extra_[EX_CY_TUNE], 0.0f, 1.0f);
                float metal;
                double dec;
                if (v == OH) {
                    float lp2, bp2, hp2; s.f2.tick(hp1, lp2, bp2, hp2);
                    metal = hp2 * 1.0f;              // cascaded high-pass (24 dB/oct)
                    // OH decay is a smooth two-phase fall, NOT a flat gate. Measured
                    // 2026-08-24: ReBirth's open hat decays gently (a plateau) for a
                    // knob-dependent time, then faster (envshape 0/-0.9/-2.9/-5.9/
                    // -9.8 dB at 50/100/200/300 ms for decay 0.5). Round 8's gate fit
                    // the -30 dB LENGTH but held our synth voice FLAT at -1 dB then
                    // cliffed, which sounded wrong (Luke's ear). A gentle 0.35 s tau
                    // during the plateau, then ohTau after.
                    s.burstT += 1.0f / (float)sr;
                    dec = (s.burstT < s.ohHold) ? 0.40 : (double)s.ohTau;
                } else {
                    float lp2, bp2, hp2; s.f2.tick(hp1, lp2, bp2, hp2);
                    metal = (v == CH) ? (lp2 * 1.30f + hp1 * 0.12f)
                                      : (lp2 * 1.05f + hp1 * cyHi);
                    // The cymbal is a fast head plus a long tail, and a single
                    // exponential cannot fit both. Re-fit 2026-08-24 to the cyd
                    // sweep (RB -30 dB length 90/236/1130 ms at decay 0/.5/1, a
                    // 12.5x span) to match the ENDPOINTS: the full-decay tail near
                    // 1.1 s is the cymbal's signature and the old 0.0442*4.48^decay
                    // cut it to 642 ms. Lands 90 and ~1130 ms at the ends and runs
                    // ~35% long in the middle, the accepted single-exponential
                    // residual (a two-stage envelope did not help on the sample path).
                    if (v == CH) dec = 0.006 + extra_[EX_CH_DECAY] * 0.035;
                    else         dec = 0.026 * std::pow(12.6, (double)extra_[EX_CY_DECAY]);
                }
                s.env1 *= envCoef(dec, sr);
                o = metal * s.env1 * 1.25f;
                break;
            }
            default:
                break;
        }

        // Retire the voice once every amplitude envelope has died away.
        if (s.env1 < 1.0e-4f && s.env2 < 1.0e-4f && s.env3 < 1.0e-4f)
            s.active = false;

        return flushDenormal(o * s.vel);
    }

    double sr_ = kDefaultSampleRate;
    std::array<VoiceState, NV808> vs_{};

    // Optional PCM samples per voice (see setSample).
    std::vector<float> smp_[NV808];
    double smpRate_[NV808] = {};
    double smpPos_[NV808]  = {};
    float  smpRatio_[NV808] = {};   // tune-knob varispeed ratio (set at trigger)
    bool   smpOn_[NV808]   = {};
    float  smpVel_[NV808]  = {};
    float  smpEnv_[NV808]  = {};    // knob-driven decay envelope
    float  smpEnvC_[NV808] = {};
    // Hybrid sampled-snare noise burst (see trigger).
    float snNoiseEnv_ = 0.0f, snNoiseC_ = 0.0f, snNoiseLvl_ = 0.0f;
    Svf   snSvf_;
    // Sample-path OH decay gate (see trigger/renderSample). ReBirth's open-hat
    // Decay knob is a GATE, not an exponential: the sample plays at natural level
    // until a hold time set by the knob, then a fast release to silence. ohGate_
    // is that hold time in output samples; ohRelC_ is the release coefficient.
    float ohGate_ = 0.0f, ohRelC_ = 0.0f;
    // Sample-path BD tone-minimum quirk (see trigger/renderSample). At tone 0..8
    // ReBirth (a) gates the kick to silence at ~190 ms, (b) hi-cuts the harmonics
    // with a ~100 Hz one-pole lowpass (harmonics 70-200 -6.7 dB, 200-600 -13.7 dB
    // vs the -4 dB fundamental), and (c) drops the body level. All clear by tone 16.
    // bdGate_ = cut time in output samples; bdLp_/bdLpA_ = the hi-cut one-pole.
    float bdGate_ = 1.0e9f, bdRelC_ = 0.0f;
    float bdLp_ = 0.0f, bdLpA_ = 1.0f;
    // Sample-path CY tone tilt (see trigger/renderSample). First-order tilt about
    // a ~7 kHz pivot: out = cyGLo_*lp + cyGHi_*(x - lp), where lp is a one-pole
    // lowpass (state cyLp_, coefficient cyLpA_). Raising Tone cuts the body and
    // lifts the sizzle; lowering it does the reverse. Unity when both gains are 1.
    float cyLp_ = 0.0f, cyLpA_ = 0.0f, cyGLo_ = 1.0f, cyGHi_ = 1.0f;
    // Sample-path CY brightness lowpass (see trigger/renderSample). ReBirth darkens
    // the cymbal with a gentle ~3 kHz one-pole lowpass that CH/OH do not get;
    // without it our cymbal runs ~1.7 kHz too bright in centroid. State + coef.
    float cyBrt_ = 0.0f, cyBrtA_ = 0.0f;
    float level_[NV808];
    float tune_, bdDecay_, sdTone_, sdSnap_;
    float extra_[EX_COUNT];
    Rng rng_;
};

} // namespace

std::unique_ptr<Instrument> makeDrum808() { return std::make_unique<Drum808>(); }

} // namespace reborn
