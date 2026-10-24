// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Clean-room TR-909-style drum machine.
//
// Every voice is synthesised from documented analog-circuit behaviour and
// standard DSP; there are NO samples, wavetables, or ROM data anywhere in
// this file. The classic parameter names (level_*, bd_tune, bd_decay,
// bd_attack, sd_tune, sd_snap) are preserved so existing songs keep loading;
// additional per-voice controls are appended AFTER the originals. The
// 11-voice order (BD SD LT MT HT RS CP OH CH CR RD) is frozen.
//
// Character notes (909 vs 808): the 909 is punchier, brighter and more
// aggressive. The kick has a steep ~3-octave pitch drop and a noisy click at
// the attack instead of the 808's long boom; the snare is crackly with a
// prominent high-passed noise burst; hats and cymbals are bright metallic
// square-bank sounds rather than mellow washes.
//
// Voice sketches (all analog-inspired, no ROM emulation):
//   BD  sine VCO with a strong fast pitch envelope (~3 octaves over 30-60 ms,
//       scaled by bd_attack/bd_tune) into gentle saturation, plus a separate
//       high-passed noise click at the attack (bd_attack sets click amount).
//       bd_decay sets the exponential amp decay (~0.1 .. 1 s).
//   SD  two detuned body oscillators (~185 / ~330 Hz, both shifted by
//       sd_tune) with a fast pitch envelope, plus a prominent HIGH-PASSED
//       noise burst. Tone and noise have independent decays; sd_snap sets
//       noise level, decay and brightness.
//   LT/MT/HT  tuned sines with a pitch envelope and a small high-passed
//       noise spit at the attack; medium decays.
//   RS  two resonant band-passes (~500 Hz and ~1 kHz) rung by a 1.5 ms noise
//       burst; very fast overall decay, no pitch envelope.
//   CP  three rapid noise pre-bursts ~10 ms apart into a longer band-passed
//       (~1 kHz) noise tail.
//   OH/CH  bank of six inharmonic square oscillators
//       (1 : 1.4471 : 1.6170 : 1.9265 : 2.5028 : 2.6637) through a ~7.5 kHz
//       high-pass plus a shimmer band-pass. CH is short (~50 ms); OH is
//       longer (~300-400 ms) with a two-stage envelope. Triggering CH chokes
//       a ringing OH (shared-envelope hardware behaviour).
//   CR/RD  the same metallic bank blended with noise through a lower
//       (~5-6 kHz) high-pass. Crash: ~1.5-2 s, bright and washy with an
//       extra splash transient. Ride: ~1 s with a distinct tonal bell ping.
//
// Robustness: all resonant paths use a topology-preserving (TPT) state
// variable filter which is unconditionally stable (no feedback coefficient
// can reach 1); cutoffs are clamped below sr*0.45, phase increments are
// bounded, and every accumulator is denormal-flushed so no parameter
// combination can produce NaN/Inf/denormals. No heap allocation, locks or
// I/O inside trigger()/process().
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>
#include <string>
#include <cstdint>

namespace reborn {
namespace {

// 11-voice panel order (frozen: the song loader maps columns to these).
enum Voice909 { BD, SD, LT, MT, HT, RS, CP, OH, CH, CR, RD, NV909 };

const char* kNames[NV909] = {
    "Bass Drum","Snare","Low Tom","Mid Tom","Hi Tom","Rim",
    "Clap","Open Hat","Closed Hat","Crash","Ride"
};

// Per-voice output balance. Order = Voice909: BD SD LT MT HT RS CP OH CH CR RD.
//
// RE-FIT 2026-08-25 by POWER averaging against the 909completeness batch
// (tools/measure/fit_trims909.py), after the tune-ladder and snare-mix fixes moved
// several voices' energy. The 2026-07-25 values it replaces were set with the same
// coherent-averaging harness that mis-set every 808 trim, and the errors are the
// same shape: the kick read 13.1 dB quiet, the crash 10.7 dB hot and the ride
// 8.2 dB quiet. Fit shape BEFORE trim, then re-run this; a level deficit that is
// really a decay or variant error must not be baked in here.
//
// Re-run after the round-2 BD decay re-fit and snare mix law: only BD and SD
// moved, every other voice landing within 0.12 dB of the previous fit, which is
// the consistency check that those two changes stayed inside their own voices.
// Re-run again after removing the spurious crash/ride envelopes: only CR and RD
// moved (their full-length tails carry more energy), the other nine within 0.05 dB.
// Re-run once more after the decay-law rewrite: the six voices with a decay knob
// (BD, the three toms, OH, CH) now hold their tails longer and so carry more energy,
// while SD, RS, CP, CR and RD, which have no decay knob, did not move at all.
const float kTrim909[NV909] = {
    0.6459f, 0.6504f, 0.7980f, 0.7301f, 0.9325f,
    0.7000f, 1.5112f, 0.7257f, 0.4836f, 0.4268f, 0.4483f
};

// The SYNTHESIS path needs its own balance, exactly as the 808 does: the two
// paths make their sounds differently and cannot share one set of trims. Until
// 2026-08-26 the 909 had only the table above, fitted to the sample player,
// so every synthesised voice inherited a number that meant nothing for it and
// the whole kit sat 5 to 17 dB out. Fitted by fit_trims909.py --synth.
const float kTrimSyn909[NV909] = {
    0.3158f, 1.0388f, 0.5251f, 0.5103f, 0.6228f,
    3.0513f, 0.9682f, 0.2377f, 0.1847f, 0.0896f, 0.1191f
};

// Accent depth, RE-MEASURED 2026-08-25 with CLEAN fixtures. The first fit was
// confounded: its "accented" steps set the accent row AND carried cell value 3,
// which is a FLAM, so the flam's retrigger was folded into the accent law and
// produced the nonsense of an accented hit reading QUIETER than a normal one at
// knob 0. Isolating the accent row (cells left at normal) gives measured gains of
// 0.999 / 1.999 / 2.999 / 3.998 / 4.967 across the knob: exactly linear through
// 1.0 at knob 0, as it must be. The 808's independently measured depth is 3.95,
// so both machines share one accent law, which the manual's "equal effect on all
// sounds" already implied.
// This is the depth for the accent cell a 909 actually writes, value 2.
// Re-measured 2026-08-26 on fixtures carrying that value (the earlier sweep
// wrote 1, which is what an 808 writes and which no 909 part contains):
// gain = 1 + 7.934 * knob reproduces 2.000 / 3.001 / 5.001 / 6.987 / 8.846 at
// knobs 16 / 32 / 64 / 96 / 127 to within 0.09%. A value-1 cell halves it,
// which is what EX_ACCENT_CELL carries (1.0 = value 2, 0.5 = value 1).
constexpr float kAccentDepth909 = 7.934f;




// Appended controls (AFTER the five classic params; order is stable).
enum ExtraParam {
    // Independent per-tom Tune and Decay (the 909 has these per column; the
    // earlier shared tom_tune/tom_decay dropped the mid and hi tom controls).
    EX_LT_TUNE, EX_MT_TUNE, EX_HT_TUNE,
    EX_LT_DECAY, EX_MT_DECAY, EX_HT_DECAY,
    EX_SD_TONE,                                    // snare Tone (was unimplemented)
    EX_HH_TUNE, EX_CH_DECAY, EX_OH_DECAY,
    EX_CYM_TUNE,                                   // CRASH tune
    EX_RD_TUNE,                                    // RIDE tune (separate; was conflated)
    EX_CR_DECAY, EX_RD_DECAY, EX_ACCENT, EX_ACCENT_CELL, EX_COUNT
};
struct ExtraDesc { const char* name; const char* label; float def; };
const ExtraDesc kExtra[EX_COUNT] = {
    { "lt_tune",   "Low Tom Tune",     0.5f },
    { "mt_tune",   "Mid Tom Tune",     0.5f },
    { "ht_tune",   "Hi Tom Tune",      0.5f },
    { "lt_decay",  "Low Tom Decay",    0.5f },
    { "mt_decay",  "Mid Tom Decay",    0.5f },
    { "ht_decay",  "Hi Tom Decay",     0.5f },
    { "sd_tone",   "Snare Tone",       0.5f },
    { "hh_tune",   "Hat Tune",         0.5f },
    { "ch_decay",  "Closed Hat Decay", 0.4f },
    { "oh_decay",  "Open Hat Decay",   0.5f },
    { "cym_tune",  "Crash Tune",       0.5f },
    { "rd_tune",   "Ride Tune",        0.5f },
    { "cr_decay",  "Crash Decay",      0.6f },
    { "rd_decay",  "Ride Decay",       0.5f },
    { "accent",    "Accent Amount",    0.25f },   // global ACCENT knob depth
    { "accent_cell", "Accent Cell",    1.0f  },   // column-0 value / 2 (see above)
};

// Per-tom extra-param index helpers (LT/MT/HT -> the right EX_* slot).
inline int tomTuneEx(int v)  { return v == LT ? EX_LT_TUNE  : v == MT ? EX_MT_TUNE  : EX_HT_TUNE;  }
inline int tomDecayEx(int v) { return v == LT ? EX_LT_DECAY : v == MT ? EX_MT_DECAY : EX_HT_DECAY; }

// Inharmonic partial ratios for the metallic bank (documented 909-style
// hat/cymbal oscillator ratio set; ratios only, no ROM data).
// The metallic voices' partials, measured 2026-08-26 off the ReBirth exports
// (strongest six peaks between 2 and 14 kHz, power-averaged across the hits):
//
//   CH  4770 5507 5620 6481 7617 8166      CR  3521 4027 5012 5905 6804 8909
//   OH  3779 3838 5566 5663 6864 7709      RD  3704 6928 6944 7424 9044 9458
//
// Each voice has its own set and its own fundamental near 3.5 to 4.8 kHz. One
// shared ratio table over a 245 to 337 Hz base could not put a partial anywhere
// near any of them: what reached the top came from the SQUARES' odd harmonics,
// which is why every metallic voice read an octave too bright (the crash's
// centroid was 12.5 kHz against ReBirth's 6.2). Sine partials rather than
// squares, so a measured partial set is what comes out and nothing else.
const double kMetalCH[6] = { 1.0000, 1.1546, 1.1783, 1.3589, 1.5971, 1.7122 };
const double kMetalOH[6] = { 1.0000, 1.0157, 1.4729, 1.4986, 1.8162, 2.0399 };
const double kMetalCR[6] = { 1.0000, 1.1437, 1.4235, 1.6774, 1.9327, 2.5306 };
const double kMetalRD[6] = { 1.0000, 1.8706, 1.8750, 2.0044, 2.4419, 2.5538 };

inline const double* metalSet(int v) {
    return (v == CH) ? kMetalCH : (v == OH) ? kMetalOH
         : (v == CR) ? kMetalCR : kMetalRD;
}

// White-noise source (xorshift32). Deterministic, allocation-free.
struct Rng {
    uint32_t s = 0x909u;
    inline float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return int32_t(s) * (1.0f / 2147483648.0f); // [-1, 1)
    }
};

// Per-sample amplitude decay coefficient: env *= coef gives an exponential
// decay whose time-to-1/e is 't' seconds. Bounded so it is always in (0, 1).
inline float envCoef(double t, double sr) {
    if (t < 1.0e-4) t = 1.0e-4;
    return (float)std::exp(-1.0 / (t * sr));
}

// Sample-path decay knobs: ONE law serves all of them.
//
// Measured 2026-08-25 across every 909 decay knob. At its MAXIMUM ReBirth applies
// no envelope at all and the voice plays its recording to natural length: at knob
// 127 the observed tau equals the raw sample's own tau to within 0.1 ms on all five
// (CH .0390/.0390, OH .1296/.1297, LT .1491/.1492, MT .0899/.0900, HT .0984/.0984,
// and the kick likewise). Below that, dividing the sample's own decay out leaves an
// envelope whose RECIPROCAL is a clean power law in (1 - knob), which reproduces
// every measured point within 1 ms.
//
// The laws this replaced were plain exponentials, all far too fast at the bottom of
// their travel (our CH ran 0.0053 s against ReBirth's 0.0120, our OH 0.0104 against
// 0.0678) and none reaching the natural length at the top. Fit the decay BEFORE the
// trim: a level deficit that is really a decay error must not be baked into a trim.
inline double knobDecayTau(double knob, double A, double p) {
    if (knob < 0.0) knob = 0.0;
    if (knob > 1.0) knob = 1.0;
    const double inv = A * std::pow(1.0 - knob, p);
    return (inv > 0.05) ? 1.0 / inv : 100.0;      // 100 s = effectively natural
}

// Topology-preserving-transform state-variable filter. Unconditionally
// stable across the full audio range; coefficients are computed once per hit
// (never in the inner render loop). Produces low/band/high simultaneously.
struct Svf {
    float ic1 = 0.0f, ic2 = 0.0f;   // integrator state
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;

    void reset() { ic1 = ic2 = 0.0f; }

    void set(double fc, double Q, double sr) {
        const double maxFc = sr * 0.45;           // guard < Nyquist margin
        if (fc > maxFc) fc = maxFc;
        if (fc < 10.0)  fc = 10.0;
        if (Q < 0.05)   Q = 0.05;
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
    float vel    = 1.0f;   // velocity * accent gain
    float bright = 1.0f;   // accent brightness factor (>1 on accented hits)

    // Amplitude envelopes (meaning is per-voice; unused ones stay 0) and the
    // per-hit decay coefficients that drive them.
    float env1 = 0.0f, env2 = 0.0f, env3 = 0.0f, pEnv = 0.0f;
    float c1 = 0.0f, c2 = 0.0f, c3 = 0.0f, cp = 0.0f;

    // Per-hit oscillator frequencies / modulation depths.
    double f1Hz = 0.0, f2Hz = 0.0, f3Hz = 0.0;
    double pAmt = 0.0;          // pitch-envelope depth (multiplier on top)
    double metalBase = 0.0;     // metallic bank base frequency

    // Oscillator phases.
    double ph1 = 0.0, ph2 = 0.0, ph3 = 0.0;
    double mph[6] = { 0,0,0,0,0,0 };

    // Resonant filters (coefficients set once per hit).
    Svf sv1, sv2;

    // Extra per-voice scalars.
    float noiseLvl = 0.0f;

    // Clap burst bookkeeping.
    int   burst = 0;
    float burstT = 0.0f;
};

class Drum909 : public Instrument {
public:
    Drum909() {
        for (int v = 0; v < NV909; ++v) level_[v] = 0.85f;
        level_[CR] = 0.55f; level_[RD] = 0.6f;
        bdTune_ = 0.5f; bdDecay_ = 0.5f; bdAttack_ = 0.6f;
        sdTune_ = 0.5f; sdSnap_ = 0.6f;
        for (int e = 0; e < EX_COUNT; ++e) extra_[e] = kExtra[e].def;
    }

    void prepare(double sr) override { sr_ = (sr > 1000.0 ? sr : kDefaultSampleRate); reset(); }
    // Everything a hit leaves running starts again from silence, so play()
    // renders the same whatever played before it: the synthesis voices, the
    // noise source's seed, and with samples loaded the sample voices, the
    // sample-path snare's noise filter and the BD attack one-pole. Without the
    // seed a song loaded after another rendered differently from the same song
    // opened fresh; without the sample half, a sample ringing at play() rang on
    // over the new start, and the snare filter's state carried from song to
    // song (review 2026-09-27). Whether ReBirth cuts a ringing drum at Play is
    // unmeasured; the synthesis voices always have been cut here.
    void reset() override {
        for (auto& v : vs_) v = VoiceState{};
        rng_ = Rng{};
        for (int v = 0; v < NV909; ++v) { smpOn_[v] = false; smpPos_[v] = 0.0; smpEnv_[v] = 0.0f; }
        snNoiseEnv_ = 0.0f;
        snSvf_.reset();
        bdLp_ = 0.0f;
    }

    int numVoices() const override { return NV909; }
    std::string voiceName(int v) const override { return (v >= 0 && v < NV909) ? kNames[v] : ""; }

    int numParams() const override { return NV909 + 5 + EX_COUNT; }

    ParamInfo paramInfo(int i) const override {
        if (i >= 0 && i < NV909)
            return { std::string("level_") + kNames[i], std::string(kNames[i]) + " Level", 0, 1, 0.85f, "" };
        int j = i - NV909;
        switch (j) {
            case 0: return { "bd_tune",   "BD Tune",   0, 1, 0.5f, "" };
            case 1: return { "bd_decay",  "BD Decay",  0, 1, 0.5f, "" };
            case 2: return { "bd_attack", "BD Attack", 0, 1, 0.6f, "" };
            case 3: return { "sd_tune",   "SD Tune",   0, 1, 0.5f, "" };
            case 4: return { "sd_snap",   "SD Snappy", 0, 1, 0.6f, "" };
            default: break;
        }
        int e = j - 5;
        if (e >= 0 && e < EX_COUNT)
            return { kExtra[e].name, kExtra[e].label, 0, 1, kExtra[e].def, "" };
        return { "unused", "Unused", 0, 1, 0, "" };
    }

    float getParam(int i) const override {
        if (i >= 0 && i < NV909) return level_[i];
        int j = i - NV909;
        switch (j) {
            case 0: return bdTune_;
            case 1: return bdDecay_;
            case 2: return bdAttack_;
            case 3: return sdTune_;
            case 4: return sdSnap_;
            default: break;
        }
        int e = j - 5;
        return (e >= 0 && e < EX_COUNT) ? extra_[e] : 0.0f;
    }

    void setParam(int i, float x) override {
        x = clampf(x, 0, 1);
        if (i < 0) return;
        if (i < NV909) { level_[i] = x; return; }
        int j = i - NV909;
        switch (j) {
            case 0: bdTune_ = x;   return;
            case 1: bdDecay_ = x;  return;
            case 2: bdAttack_ = x; return;
            case 3: sdTune_ = x;   return;
            case 4: sdSnap_ = x;   return;
            default: break;
        }
        int e = j - 5;
        if (e >= 0 && e < EX_COUNT) extra_[e] = x;
    }

    // Optional per-voice PCM sample (plays INSTEAD of the synthesis model;
    // level/accent and the CH->OH choke still apply). Loader-thread only.
    void setSample(int v, std::vector<float> data, double rootRate) override {
        if (v < 0 || v >= NV909) return;
        smp_[v] = std::move(data);
        smpRate_[v] = (rootRate > 1000.0) ? rootRate : 44100.0;
        smpOn_[v] = false; smpPos_[v] = 0.0;
    }

    void trigger(int v, float vel, bool accent, bool /*slide*/) override {
        if (v < 0 || v >= NV909) return;
        const double sr = sr_;

        // Sample mode: (re)start the PCM one-shot for this voice. The decay
        // knobs shape the sample with an exponential envelope, exactly like
        // the original applied its knobs to its PCM voices (knob fully up =
        // the sample's full natural length).
        if (!smp_[v].empty()) {
            if (v == CH && smpOn_[OH]) smpOn_[OH] = false;   // choke
            smpOn_[v]  = true;
            smpPos_[v] = 0.0;
            // ACCENT (MIDI CC 65), measured with accented and unaccented hits
            // inside the SAME render so their ratio needs no level matching and
            // cannot be confounded by a trim: steps 0 and 8 accented, 4 and 12
            // not, with the knob swept across files.
            //
            //   knob        0     16     32     64     96    127
            //   gain    1.000  2.000  3.001  5.001  6.987  8.846
            //
            // Exactly LINEAR: 1 + 7.934 * knob, reproducing every point to
            // within 0.09%. The knob ALONE does nothing: with the accent row
            // clear, every step reads -31.60 dB at both extremes, so it is the
            // row that selects the behaviour and the knob that sets its depth.
            //
            // This law replaces one fitted 2026-08-25 on fixtures whose accent
            // column carried the value 1. A 909 never writes 1 there: it writes
            // 2, in all 1442 accent cells across the shipped songs, and ReBirth
            // scales the depth by that value, so the old fixtures measured a
            // cell no real 909 part contains and came out at exactly half depth
            // (3.967). Every 909 accent in every song was therefore rendered at
            // half strength. EX_ACCENT_CELL carries the step's own value
            // normalised against 2, so a value-1 cell still halves it.
            //
            // The 0.78 on unaccented hits used to come from the sequencer's
            // velocity; it lives here so this device's balance is unchanged.
            smpVel_[v] = vel * 0.78f
                       * (accent ? (1.0f + kAccentDepth909 * extra_[EX_ACCENT_CELL]
                                                          * extra_[EX_ACCENT]) : 1.0f);
            // Per-voice balance moved to kTrim909[] (applied at output), re-fit
            // 2026-07-25 to the isolated exports: the old inline trims left the
            // kick ~+19 dB hot and the crash ~15 dB quiet.
            smpRateMul_[v] = sampleTuneRatio(v);   // knob varispeed (was inert)
            double tau = 0.0;                                // 0 = natural decay
            switch (v) {
                // BD decay RE-FIT 2026-08-25 to the bdd sweep, on Luke's ear (the
                // kick sounded "clearer" than ReBirth's, which reads as a high cut
                // on their side: ours decayed ~2x too fast, and truncating a decay
                // broadens the spectrum upward, so we had +3 dB at 100-200 Hz and
                // +5 dB above 3.5 kHz on the same sample).
                //
                // ReBirth's OBSERVED tau runs 0.0545/0.0614/0.0699/0.0801/0.0881 s
                // across the knob, and at the top it equals the raw sample's own
                // natural tau (0.0882), so MAX DECAY APPLIES NO ENVELOPE. Dividing
                // out the sample's own decay leaves the envelope's reciprocal
                // remainder, a clean power law: 1/tau_env = 7.139*(1-decay)^1.287,
                // which reproduces all five points to within 0.4 ms. Same
                // reciprocal-remainder structure the 808 kick needed, and the same
                // lesson: fit the decay BEFORE the trim, or the level error hides
                // inside it. The old 0.035*15^(decay^2) was ~2.7x too fast at
                // mid-knob and never reached the natural length at the top.
                case BD: tau = knobDecayTau((double)bdDecay_, 8.16, 1.287); break;
                // The three toms share one knob law: fitted separately they land on
                // A 6.72/6.59/6.84 and p 1.06/1.05/1.23, and each tom only has three
                // sweep points, so one shared law is better supported than three
                // overfitted ones. Their different lengths come from their own
                // samples, not from different knob responses.
                case LT: case MT: case HT:
                         tau = knobDecayTau((double)extra_[tomDecayEx(v)], 8.07, 1.11); break;
                // CH is the outlier, being a very short sound: its envelope has to
                // work much harder at the bottom of the knob, hence the large A.
                case CH: tau = knobDecayTau((double)extra_[EX_CH_DECAY], 66.3, 1.40); break;
                case OH: tau = knobDecayTau((double)extra_[EX_OH_DECAY],  8.18, 1.149); break;
                // CR and RD get NO envelope. The 909 panel has no crash or ride
                // decay control (MIDI map p197: CC 88/89 are CC Level/Tune, 90/91
                // RC Level/Tune, and the song header carries no decay byte for
                // either), and ReBirth plays both recordings to their natural
                // length: its ride ends at 0.916 s, exactly the raw sample's own
                // 0.916 s. The envelopes here truncated them to 0.548 s (ride) and
                // 0.789 s (crash) with no knob behind them, the same spurious
                // envelope the 808 carried on its toms and closed hat. Removed
                // 2026-08-25. EX_CR_DECAY / EX_RD_DECAY still shape the SYNTH
                // voices below, which need an envelope to end at all.
                default: break;                              // SD/RS/CP/CR/RD: natural
            }
            smpEnvC_[v] = (tau > 0.0) ? envCoef(tau, sr) : 1.0f;
            smpEnv_[v]  = 1.0f;
            // The original's snare samples are TONE BODIES; its snappy knob
            // mixes in a separate noise burst. Reproduce that hybrid here.
            if (v == BD) {
                // BD ATTACK (MIDI CC 69) is a FADE-IN on the sample onset, not an
                // added click. Measured 2026-08-25 over the first 6 ms, comparing
                // the 1-8 kHz band against the 100-1000 band so the reading is
                // independent of level: the raw recording's own onset ratio is
                // -13.2 dB, ReBirth at attack 127 reads -13.1 (the natural
                // transient, passed through untouched), and at attack 0 it reads
                // -21.7, so the top of the transient is progressively softened as
                // the knob comes down. The peak level moves with it, 4.1 dB across
                // the knob, which is what a fade-in does and what an added click
                // would not.
                //
                // A normalised band mix cannot see any of this: the 1-4 kHz share
                // reads 0.000% at every setting, because the transient carries
                // almost no ENERGY against the long low body. Same trap as the 808
                // kick tone knob.
                //
                // The knob was dead on the sample path before this: only the
                // synthesis branch read bdAttack_, so on the parity path, which is
                // the one measured against ReBirth, the control did nothing at all.
                // Fitted as a one-pole LOWPASS whose corner opens with the knob:
                // 700 Hz at attack 0, rising by a factor of 30 across the travel,
                // reaching passthrough near the top. A pure amplitude fade was tried
                // first and cannot do this, because it attenuates every frequency
                // equally at each instant and so barely moves the ratio; a lowpass
                // removes the transient specifically AND softens the peak, which is
                // both of the things measured. The kick is 97% sub-100 Hz, so even
                // the 700 Hz setting leaves the body untouched.
                const double atkFc = 700.0 * std::pow(30.0, (double)bdAttack_);
                bdLpA_ = (atkFc >= sr * 0.45) ? 1.0f
                       : 1.0f - (float)std::exp(-2.0 * kPi * atkFc / sr);
                bdLp_  = 0.0f;
            }
            if (v == SD) {
                snNoiseEnv_ = 1.0f;
                snNoiseC_   = envCoef(0.045 + (double)sdSnap_ * 0.15, sr);
                // Snappy must reach SILENCE at 0 (ReBirth's snappy-0 909 snare
                // is pure body, centroid 84 Hz; the old 0.10 floor left noise
                // that put our centroid at 2932). Squared taper, re-fit
                // 2026-07-25 to hold the snappy centroid climb (84 -> 6059 Hz).
                snNoiseLvl_ = sdSnap_ * sdSnap_ * 0.60f * smpVel_[v];
                snSvf_.set(1200.0 + sdSnap_ * 900.0, 0.5, sr);
            }
            return;
        }

        // Hardware choke: hitting the closed hat cuts a ringing open hat
        // short (they share the output/envelope circuit on the original).
        if (v == CH) {
            VoiceState& oh = vs_[OH];
            if (smpOn_[OH]) smpOn_[OH] = false;
            if (oh.active) {
                float fast = envCoef(0.004, sr);
                oh.c1 = fast; oh.c2 = fast; oh.c3 = fast;
            }
        }

        VoiceState& s = vs_[v];
        s = VoiceState{};
        s.active = true;
        s.vel    = clampf(vel, 0.0f, 1.5f)
                 * 0.85f * 0.78f                      // see the accent note in the sample branch
                 * (accent ? (1.0f + kAccentDepth909 * extra_[EX_ACCENT_CELL]
                                                     * extra_[EX_ACCENT]) : 1.0f);
        // ACCENT IS PURE GAIN on this machine, so this stays at 1. Measured
        // 2026-08-26: comparing the band profile of accented against unaccented
        // hits inside one ReBirth render, every band from 20 Hz to 8 kHz moves
        // by under 0.6 dB, and by the SAME amount at accent knob 0, 64 and 127 --
        // a window artifact, not the knob. It has to be pure gain: ReBirth is
        // playing one recording louder, and a recording cannot brighten.
        //
        // The 1.3 this replaces drove the kick's body saturation harder on
        // accented hits, and Trakka Rekka accents nearly every kick, so the
        // synth kick manufactured harmonics all through the song: 9.2 points too
        // much 100-300 Hz against 8.7 too little 20-100. Unaccented hits already
        // read 1.0, so no fixture and no fitted trim is affected by this.
        s.bright = 1.0f;
        s.env1 = 1.0f;
        s.pEnv = 1.0f;

        switch (v) {
            case BD: {
                // The 909 kick RESTS AT 54 Hz whatever the tune knob says.
                // Measured 2026-08-26 by tracking the instantaneous frequency
                // through the hit: at knobs 0/32/64/96/127 ReBirth settles on
                // 54.3/54.8/53.5/56.4/56.4 Hz, while its ONSET runs
                // 105/142/163/183/183. Tune sets how far above the resting
                // pitch the sweep starts and how long it takes to arrive, which
                // is what a real 909's tune control does. The old law moved the
                // resting pitch itself over +/-0.4 octaves, so the note changed
                // key across the knob: 420 cents flat at knob 0 and 457 sharp at
                // 127, either side of a middle that happened to be right.
                //
                // It steps rather than glides, because ReBirth picks one of four
                // recorded variants: knobs 96 and 127 measure IDENTICAL, exactly
                // as sampleset.cpp's 1 + floor(tune*4) predicts. Fitting
                // f(t) = 54 * (1 + pAmt * exp(-t/tau)) to each variant, in the
                // SMOOTHED domain (the 25 ms measurement window is modelled into
                // the fit; treating its output as instantaneous underestimates
                // the onset by a factor of three):
                //
                //   variant     1     2     3     4
                //   tau ms    7.4  15.6  23.1  37.6      shared depth 3.396
                //
                // The depth barely moves and only the time constant does, which
                // is what a real 909's tune control is: a decay-time knob on the
                // pitch envelope. All four onsets land near 4.4 x 54 Hz.
                //
                // ATTACK is deliberately absent from all of this. It used to
                // drive the sweep depth and speed; the sample path measured it
                // as a fade-in on the onset, and it keeps only the click level
                // here.
                // Taus trimmed 12% 2026-08-26, same reason as the toms: measured
                // against a correct amplitude envelope the sweep was lingering,
                // reading 73 to 90 cents sharp through its middle at tune 64 and
                // above while arriving correctly.
                static const double kBdSweepTau[4] = { 0.0065, 0.0137, 0.0203, 0.0331 };
                int bdv = (int) std::floor((double) bdTune_ * 4.0);
                if (bdv < 0) bdv = 0;
                if (bdv > 3) bdv = 3;
                s.f1Hz = 54.0;
                s.pAmt = 3.396;
                s.cp   = envCoef(kBdSweepTau[bdv], sr);
                // MEASURED decay. ReBirth's own observed amplitude tau across the
                // knob is 0.0549 / 0.0617 / 0.0709 / 0.0821 / 0.0921 s, which is
                // 0.0546 * e^(0.526*knob) to within 0.5 ms. The law this replaces
                // was invented: 12 ms at knob 0, four and a half times too SHORT,
                // and 232 ms at the top, two and a half times too long. Too short
                // a decay also drags the perceived pitch sharp, because it puts
                // the hit's loudness inside the pitch sweep rather than after it.
                s.c1   = envCoef(0.0546 * std::exp(0.526 * (double) bdDecay_), sr);
                s.c3   = envCoef(0.0025, sr);                    // click burst
                s.env3 = 1.0f;
                s.noiseLvl = (0.35f + bdAttack_ * 1.05f) * s.bright;
                s.sv1.set(3200.0, 0.8, sr);                      // click high-pass
                break;
            }
            case SD: {
                // Body pitch MEASURED 2026-08-26 from the ReBirth tune sweep. Its
                // snare is a fundamental with ONE partial a little over a third
                // above it, well down in level:
                //
                //   tune      0    32    64    96   127
                //   f1 Hz   124   145   178   215   242      span 11.97 st
                //   f2/f1  1.347 1.372 1.360 1.353 1.355     mean 1.358
                //   f2 dB   -7.7  -6.9  -7.2  -8.1  -8.9     mean -7.8
                //
                // We had f2 at 330/185 = 1.784 and only 1.7 dB down, so a loud
                // partial sat a tritone above the fundamental and dragged the
                // whole snare's pitch with it: 662 cents sharp at tune 0 and 455
                // at tune 64, which is four to five semitones. The tune range was
                // 7.2 semitones against a measured 12.0, so the error closed to
                // nothing at the top of the knob and was worst at the bottom.
                double tf = std::pow(2.0, (sdTune_ - 0.5) * (11.97 / 12.0));
                s.f1Hz = 175.22 * tf;
                s.f2Hz = s.f1Hz * 1.358;
                // ReBirth's snare barely sweeps: 186.9 Hz at 15 ms against
                // 183.7 at 80. The old 2.0 lifted the onset a fifth.
                s.pAmt = 0.35;
                s.cp   = envCoef(0.007, sr);
                // MEASURED, and the two knobs were driving the wrong things.
                // The 2x2x2 corner set (d909_sdx_t*o*s*) separates them: tau is
                // 0.033 s at every corner EXCEPT tone and snappy both up, where it
                // reaches 0.079. Tune does nothing at all (0.0331 against 0.0333).
                // So SNAPPY sets the noise LEVEL and TONE sets its LENGTH, and an
                // interaction, not a sum: with no noise there is nothing for tone
                // to lengthen. We had the body driven by tune, which has no
                // effect, at 85-115 ms against a measured 33, and the noise tail
                // driven by snappy at up to 255 ms against a measured 85. That is
                // the worst crest error of any voice, 16.4 dB against our 8.2.
                //
                // TONE was not read at all on this path before now.
                s.c1   = envCoef(0.033, sr);                     // tonal body
                s.c2   = envCoef(0.025 + extra_[EX_SD_TONE] * 0.060, sr);
                s.env2 = 1.0f;
                // Snappy is the noise LEVEL, and it has to reach zero: at snappy 0
                // ReBirth's snare is its body alone, which is what makes the corner
                // set read a clean 0.033 there.
                s.noiseLvl = (0.08f + sdSnap_ * 1.55f) * s.bright;
                // Band-passed snap (hardware noise rolls off up top; raw
                // high-passed white was flooding 10-20 kHz).
                s.sv1.set(3200.0 + sdSnap_ * 1600.0, 0.80, sr);
                break;
            }
            case LT: case MT: case HT: {
                // Base pitch and range measured 2026-08-26 from the ReBirth tom
                // tune sweeps with sub-bin resolution (tools/measure/pitch909.py;
                // score909's 5.4 Hz bins cannot fit a pitch law). ReBirth reads
                //
                //   knob      0     64    127        span
                //   LT    68.01  91.17 121.75    10.08 st
                //   MT    84.79 113.78 152.24    10.13 st
                //   HT    97.02 130.20 174.25    10.14 st
                //
                // so all three share one 10.1-semitone range with knob 64 sitting
                // exactly halfway, and the bases below are those curves evaluated
                // at knob 0.5. The old 95/140/200 over a 12-semitone range put the
                // hi tom a fourth sharp at every setting.
                // MT and HT nudged 2026-08-26 after the decay laws were fitted.
                // The first fit was taken with the decays still 2 to 4 times too
                // long, which weights a pitch measurement toward the late, settled
                // part of the hit; with the decays right the same measurement put
                // MT 44 cents sharp at mid knob and 52 at the top (so its span was
                // 0.57 st too wide as well), and HT about 30 cents sharp
                // throughout. LT needed nothing. A pitch fit is only as good as
                // the envelope underneath it.
                double base = (v == LT) ? 85.65 : (v == MT) ? 106.24 : 119.90;
                double span = (v == LT) ?  8.95 : (v == MT) ?   8.84 :   8.96;
                s.f1Hz = base * std::pow(2.0, (extra_[tomTuneEx(v)] - 0.5) * (span / 12.0));
                // The pitch sweep, fitted 2026-08-26 against ReBirth's own
                // trajectory (instantaneous frequency of the fundamental, not a
                // short-window FFT, which at 90 Hz wanders by hundreds of cents).
                // ReBirth's tom starts about 250 cents above its settled pitch
                // and arrives by roughly 140 ms. A grid over depth and time
                // constant bottoms out here at 14.7 cents rms across all three
                // toms. The old 2.2 over 28 ms swept 556 cents, more than twice
                // too deep and too fast, which is most of why the synth toms
                // read sharp at every setting.
                s.pAmt = 0.40;
                s.cp   = envCoef(0.050, sr);
                // MEASURED decay, per tom. Unlike the sample path these do NOT
                // share one law: the samples carry their own lengths, but a
                // synthesised tom has to be told. ReBirth's observed tau:
                //   LT 0.0694 -> 0.1347 s     0.0687 * e^(0.663*knob)
                //   MT 0.0547 -> 0.0903       0.0544 * e^(0.501*knob)
                //   HT 0.0568 -> 0.0924       0.0562 * e^(0.487*knob)
                // The invented law ran 2.3 to 4.3 times too long, which is most
                // of why the toms measured 6.5 to 7.5 dB flat on crest and read
                // 75 to 272 cents FLAT in perceived pitch: a long decay puts the
                // loudness after the pitch sweep instead of during it.
                const double decA = (v == LT) ? 0.0687 : (v == MT) ? 0.0544 : 0.0562;
                const double decB = (v == LT) ? 0.663  : (v == MT) ? 0.501  : 0.487;
                double dec = decA * std::exp(decB * (double) extra_[tomDecayEx(v)]);
                s.c1   = envCoef(dec, sr);
                s.c3   = envCoef(0.006, sr);                     // attack noise
                s.env3 = 1.0f;
                s.noiseLvl = 0.25f * s.bright;
                s.sv1.set(2500.0, 0.8, sr);                      // noise high-pass
                break;
            }
            case RS: {
                // Two resonant band-passes rung by a very short noise burst.
                // Partials measured 2026-08-26 off the ReBirth export: 221 Hz
                // dominant, 484 Hz at -11.1 dB, 1039 Hz at -25.3, and nothing
                // above that worth modelling. The old 480 and 1030 were the
                // SECOND and THIRD partials with the fundamental missing
                // entirely, which is why our rim read 398 Hz against ReBirth's
                // 221 and sat 9 to 20 dB hot across every band above 400 Hz.
                // Envelope: 0, -16, -29, -45 dB in 20 ms steps, so tau ~11 ms.
                s.c1 = envCoef(0.011, sr);                       // overall gate
                s.c3 = envCoef(0.0015, sr);                      // excitation
                s.env3 = 1.0f;
                s.sv1.set(221.0, 6.0, sr);
                s.sv2.set(484.0, 6.0, sr);
                break;
            }
            case CP: {
                s.burst = 0; s.burstT = 0.0f;
                s.c1 = envCoef(0.005, sr);                       // sharp bursts
                // MEASURED tail: ReBirth's clap falls 30 dB between 50 and 300 ms,
                // a tau of about 72 ms, against the 130 ms we had.
                s.c2 = envCoef(0.072, sr);                       // diffuse tail
                s.sv1.set(1000.0, 1.7, sr);                      // clap band-pass
                s.sv2.set(6000.0, 0.7, sr);                    // top roll-off
                break;
            }
            case OH: case CH: {
                double t = std::pow(2.0, (extra_[EX_HH_TUNE] - 0.5) * 0.5);
                s.metalBase = (v == CH ? 4770.0 : 3779.0) * t;
                s.sv1.set(500.0, 0.75, sr);                   // metal high-pass
                s.sv2.set(16000.0, 0.70, sr);                   // top roll-off
                if (v == CH) {
                    // MEASURED: 0.0119 -> 0.0419 s, i.e. 0.0116 * e^(1.324*knob).
                    // The old linear law was already close, which is why the hat
                    // was one of the few voices whose crest nearly matched.
                    s.c1 = envCoef(0.0116 * std::exp(1.324 * (double) extra_[EX_CH_DECAY]), sr);
                } else {
                    // Two-stage: a fast sizzle drop into the measured tail.
                    // ReBirth's observed tau is 0.0660 -> 0.1345 s, which is
                    // 0.0666 * e^(0.707*knob). Ours ran to 0.35 s at the top,
                    // two and a half times too long.
                    s.c1 = envCoef(0.018, sr);
                    s.c2 = envCoef(0.0666 * std::exp(0.707 * (double) extra_[EX_OH_DECAY]), sr);
                    s.env2 = 1.0f;
                }
                break;
            }
            case CR: {
                double t = std::pow(2.0, (extra_[EX_CYM_TUNE] - 0.5) * 0.4);
                s.metalBase = 3521.0 * t;
                s.sv1.set(500.0 * t, 0.7, sr);                // wash high-pass
                s.sv2.set(12000.0 * t, 0.7, sr);                // top roll-off
                // MEASURED: ReBirth's crash decays with tau about 0.33 s after a
                // short swell, not the 1.1 to 2.1 s we had, which is three to six
                // times too long and left its crest at 2.0 dB against 5.7. The
                // panel has no crash decay control, so EX_CR_DECAY is an
                // advanced-settings knob only; it is recentred so that its default
                // of 0.6 lands exactly on ReBirth's length.
                s.c1 = envCoef(0.33 * (0.4 + (double) extra_[EX_CR_DECAY]), sr);
                s.c2 = envCoef(0.12, sr);                        // splash transient
                s.env2 = 1.0f;
                break;
            }
            case RD: {
                double t = std::pow(2.0, (extra_[EX_RD_TUNE] - 0.5) * 0.4);
                s.metalBase = 3704.0 * t;
                s.f3Hz = 1250.0 * t;                             // bell ping partial
                s.sv1.set(400.0 * t, 0.7, sr);
                s.sv2.set(16000.0 * t, 0.7, sr);
                // MEASURED: tau about 0.23 s. Same recentring as the crash, so the
                // default 0.5 lands on ReBirth's length.
                s.c1 = envCoef(0.23 * (0.5 + (double) extra_[EX_RD_DECAY]), sr);
                s.c3 = envCoef(0.35, sr);                        // bell decay
                s.env3 = 1.0f;
                break;
            }
            default:
                break;
        }
    }

    void process(StereoBlock& out, int n) override {
        for (int i = 0; i < n; ++i) {
            float mix = 0.0f;
            for (int v = 0; v < NV909; ++v) {
                // Balance comes from whichever path this voice renders through;
                // a kit can be part sampled and part synthesised.
                const float trim = smp_[v].empty() ? kTrimSyn909[v] : kTrim909[v];
                mix += renderVoice(v) * level_[v] * level_[v] * trim;  // taper + balance
            }
            // Reference-tuned device gain: the original's drum machines sit
            // much hotter in the mix than the basses (kick ~51% of energy).
            //
            // LINEAR, and it must stay linear. This was tanh(mix * 0.9) * 2.15,
            // which has the same small-signal gain of 1.935 but asymptotes at
            // 2.15, so the device output was hard-capped: driven with all eleven
            // voices at level 127 and accent 127 our peak sat at -23.5 dBFS and
            // would not move, where ReBirth reached -3.3, and the accented hit
            // read 9.49 dB quiet. ReBirth's drum bus has no saturator at all --
            // measured 2026-08-26 on that same stacked fixture, its accent gain
            // is 9.026 against a linear 8.934, an error of +0.089 dB and on the
            // LOUD side. It clips at the converter and nowhere before it.
            float s = flushDenormal(mix * 1.935f);
            out.left[i]  = s;
            out.right[i] = s;
        }
    }

private:
    inline float noise() { return rng_.next(); }

    static inline void wrap(double& p) { while (p >= 1.0) p -= 1.0; }

    // Advance a phase by f/sr with a bounded increment (guards any parameter
    // combination from ever pushing an oscillator past the Nyquist margin).
    static inline void adv(double& p, double f, double sr) {
        double inc = f / sr;
        if (inc > 0.45) inc = 0.45;
        if (inc < 0.0)  inc = 0.0;
        p += inc; wrap(p);
    }

    // Per-voice sample varispeed from the tune knobs. ReBirth repitches its
    // PCM 909 voices; the earlier assumption that tune selected multisample
    // variants left every 909 tune knob inert (we load one sample per voice).
    // Ranges calibrated 2026-07-23 against the fixture exports: the tom column
    // spans ~+/-5 st centred on the raw sample (LT 65.7 -> 120 Hz over the
    // knob), BD is a subtle +/-1 st, SD/CR/RD moderate.
    float sampleTuneRatio(int v) const {
        auto semis = [](double s) { return (float)std::exp2(s / 12.0); };
        switch (v) {
            // BD and SD Tune select a recorded pitch-ladder variant (see the
            // sample loader), so the sample already carries the pitch; a varispeed
            // on top would double-shift it. Play at natural rate.
            case BD: return 1.0f;
            case SD: return 1.0f;
            case LT: case MT: case HT:
                     return semis((extra_[tomTuneEx(v)] - 0.5) * 10.0);
            case CR: return semis((extra_[EX_CYM_TUNE] - 0.5) * 10.56);
            case RD: return semis((extra_[EX_RD_TUNE]  - 0.5) * 10.56);
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
        smpPos_[v] = pos + smpRate_[v] * smpRateMul_[v] / sr_;
        out *= smpEnv_[v];
        smpEnv_[v] = flushDenormal(smpEnv_[v] * smpEnvC_[v]);
        if (smpEnv_[v] < 1.0e-4f) smpOn_[v] = false;
        out *= smpVel_[v];
        // Hybrid snare: add the snappy-scaled noise burst to the tone body.
        if (v == BD && bdLpA_ < 1.0f) {
            bdLp_ += bdLpA_ * (out - bdLp_);             // attack softening, see trigger
            out = bdLp_;
        }
        if (v == SD && snNoiseEnv_ > 1.0e-4f) {
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
                // Sine VCO, steep pitch drop, gentle saturation, noisy click.
                double f = s.f1Hz * (1.0 + s.pAmt * (double)s.pEnv);
                s.pEnv = flushDenormal(s.pEnv * s.cp);
                adv(s.ph1, f, sr);
                s.env1 = flushDenormal(s.env1 * s.c1);
                // A saturated sine makes ODD harmonics only. ReBirth's kick is a
                // recording and carries a strong SECOND, so a little even-order
                // content is added alongside rather than more drive.
                float sn = (float) std::sin(s.ph1 * kTwoPi);
                // Drive 0.85 fitted 2026-08-26 against ReBirth's harmonic profile
                // across the tune sweep, measured relative to the 40-80 Hz
                // fundamental band so an overall level offset cannot hide in it.
                // The old 1.55, plus 0.35 more on accents, was heavy saturation:
                // it put the third harmonic 14 dB over ReBirth's at tune 0, and
                // since that lands at 162 Hz it also dragged the whole kick's
                // loudness-weighted pitch about 250 cents sharp.
                float body = std::tanh(sn * 0.85f + 0.25f * sn * sn) * s.env1;
                s.env3 = flushDenormal(s.env3 * s.c3);
                float lp, bp, hp; s.sv1.tick(noise(), lp, bp, hp);
                float click = hp * s.env3 * s.noiseLvl;
                o = body * 1.8f + click;   // reference-tuned kick weight
                break;
            }
            case SD: {
                // Two detuned bodies with a fast pitch env + high-passed snap.
                double sweep = 1.0 + s.pAmt * (double)s.pEnv;
                s.pEnv = flushDenormal(s.pEnv * s.cp);
                adv(s.ph1, s.f1Hz * sweep, sr);
                adv(s.ph2, s.f2Hz * sweep, sr);
                s.env1 = flushDenormal(s.env1 * s.c1);
                // 0.373 is -5.7 dB against the fundamental, not the -7.8 measured
                // off ReBirth, because the saturation below eats another 2.1 dB
                // of the weaker partial. Setting the pre-saturation weights to
                // the measured ratio left it 2.3 dB light, and since a partial a
                // third above the fundamental is what pulls the snare's perceived
                // pitch, that read as 60 cents flat.
                float body = std::tanh(((float)std::sin(s.ph1 * kTwoPi) * 0.72f +
                                        (float)std::sin(s.ph2 * kTwoPi) * 0.373f) * 1.7f)
                             * s.env1;
                s.env2 = flushDenormal(s.env2 * s.c2);
                float lp, bp, hp; s.sv1.tick(noise(), lp, bp, hp);
                float snap = bp * s.env2 * s.noiseLvl;   // band-passed crack
                o = body * 0.30f + snap;
                break;
            }
            case LT: case MT: case HT: {
                double f = s.f1Hz * (1.0 + s.pAmt * (double)s.pEnv);
                s.pEnv = flushDenormal(s.pEnv * s.cp);
                adv(s.ph1, f, sr);
                s.env1 = flushDenormal(s.env1 * s.c1);
                float body = std::tanh((float)std::sin(s.ph1 * kTwoPi) * 1.4f) * s.env1;
                s.env3 = flushDenormal(s.env3 * s.c3);
                float lp, bp, hp; s.sv1.tick(noise(), lp, bp, hp);
                o = body + hp * s.env3 * s.noiseLvl;
                break;
            }
            case RS: {
                // Short band-passed click, no pitch envelope.
                s.env1 = flushDenormal(s.env1 * s.c1);
                s.env3 = flushDenormal(s.env3 * s.c3);
                float exc = noise() * s.env3;
                float lp1, bp1, hp1; s.sv1.tick(exc, lp1, bp1, hp1);
                float lp2, bp2, hp2; s.sv2.tick(exc, lp2, bp2, hp2);
                // 0.28 is the measured -11.1 dB of the second partial. The raw
                // excitation used to leak through at 0.4, which is most of what
                // made this a bright click rather than a low tok.
                o = (bp1 * 1.5f + bp2 * 0.42f) * s.env1 * 2.0f + exc * 0.05f;
                break;
            }
            case CP: {
                // Three pre-bursts ~10 ms apart, then a band-passed tail.
                s.burstT += 1.0f / (float)sr;
                if (s.burst < 2 && s.burstT >= 0.010f) {
                    s.burst++; s.burstT = 0.0f; s.env1 = 1.0f;
                    if (s.burst == 2) s.env2 = 1.0f;   // launch tail
                }
                s.env1 = flushDenormal(s.env1 * s.c1);
                s.env2 = flushDenormal(s.env2 * s.c2);
                float n = noise();
                float lp, bp, hp; s.sv1.tick(n, lp, bp, hp);
                // ReBirth's clap falls away hard above the band: -13.7 dB at
                // 6.4 kHz and -19.9 at 12.8. The raw high-pass tap used to run
                // at 0.25 with nothing above it, which put us +4.7 and +10.8 dB
                // over in exactly those two bands.
                float lp2, bp2, hp2; s.sv2.tick(hp, lp2, bp2, hp2);
                o = bp * (s.env1 * 1.4f + s.env2 * 0.9f)
                  + lp2 * s.env2 * 0.15f * s.bright;
                break;
            }
            case OH: case CH: {
                // Metallic partial bank -> band-limited to ReBirth's own range.
                float accum = 0.0f;
                const double* mr = metalSet(v);
                for (int k = 0; k < 6; ++k) {
                    adv(s.mph[k], mr[k] * s.metalBase, sr);
                    accum += (float) std::sin(s.mph[k] * kTwoPi);
                }
                // Six sine partials alone are six lines with silence between
                // them; a real hat is those partials sitting in a broadband
                // wash, and ReBirth's has measurable energy down to 400 Hz.
                float src = accum * (1.0f / 6.0f) + noise() * 0.6f;
                float lp1, bp1, hp1; s.sv1.tick(src, lp1, bp1, hp1);
                // A BAND, not a high-pass with shimmer stacked above it. The
                // second filter now rolls the top off the high-passed signal.
                float lp2, bp2, hp2; s.sv2.tick(hp1, lp2, bp2, hp2);
                float metal = lp2 * 0.40f + hp1 * 0.35f * s.bright;
                float amp;
                if (v == CH) {
                    s.env1 = flushDenormal(s.env1 * s.c1);
                    amp = s.env1;
                } else {
                    s.env1 = flushDenormal(s.env1 * s.c1);
                    s.env2 = flushDenormal(s.env2 * s.c2);
                    amp = s.env1 * 0.4f + s.env2 * 0.6f;
                }
                o = metal * amp * 3.0f;
                break;
            }
            case CR: {
                // Metallic bank + noise wash; bright splashy onset, long tail.
                float accum = 0.0f;
                const double* mr = metalSet(v);
                for (int k = 0; k < 6; ++k) {
                    adv(s.mph[k], mr[k] * s.metalBase, sr);
                    accum += (float) std::sin(s.mph[k] * kTwoPi);
                }
                float src = accum * (1.0f / 6.0f) + noise() * 0.6f;
                float lp1, bp1, hp1; s.sv1.tick(src, lp1, bp1, hp1);
                float lp2, bp2, hp2; s.sv2.tick(hp1, lp2, bp2, hp2);
                s.env1 = flushDenormal(s.env1 * s.c1);
                s.env2 = flushDenormal(s.env2 * s.c2);
                o = (lp2 * 0.8f + hp1 * 0.15f * s.bright)
                  * (s.env1 + s.env2 * 0.7f) * 2.2f;
                break;
            }
            case RD: {
                // Metallic bank + light noise, plus a distinct tonal bell ping.
                float accum = 0.0f;
                const double* mr = metalSet(v);
                for (int k = 0; k < 6; ++k) {
                    adv(s.mph[k], mr[k] * s.metalBase, sr);
                    accum += (float) std::sin(s.mph[k] * kTwoPi);
                }
                float src = accum * (1.0f / 6.0f) + noise() * 0.5f;
                float lp1, bp1, hp1; s.sv1.tick(src, lp1, bp1, hp1);
                float lp2, bp2, hp2; s.sv2.tick(hp1, lp2, bp2, hp2);
                s.env1 = flushDenormal(s.env1 * s.c1);
                s.env3 = flushDenormal(s.env3 * s.c3);
                adv(s.ph3, s.f3Hz, sr);
                float bell = (float)std::sin(s.ph3 * kTwoPi) * s.env3 * 0.28f * s.bright;
                o = (lp2 * 0.7f + hp1 * 0.15f) * s.env1 * 2.2f + bell;
                break;
            }
            default:
                break;
        }

        // Retire the voice once every amplitude envelope has died away.
        if (s.env1 < 1.0e-4f && s.env2 < 1.0e-4f && s.env3 < 1.0e-4f)
            s.active = false;

        // LINEAR. This was softClipUnity(o * s.vel), a per-voice soft clipper
        // that the SAMPLE branch does not have and ReBirth does not have
        // anywhere. It only bit on loud hits, so the isolated fixtures (accent
        // off, s.vel 0.66) barely saw it and the trims quietly absorbed its
        // 0.6 dB. On real material it was much worse: Trakka Rekka accents
        // nearly every kick, which drives s.vel to 1.76, and the synth kick came
        // out 4.65 dB under ReBirth across the whole song while measuring exact
        // on the fixtures. Third instance of the same defect in one day; see the
        // saturator note in PARITY.md.
        return flushDenormal(o * s.vel);
    }

    double sr_ = kDefaultSampleRate;
    std::array<VoiceState, NV909> vs_{};

    // Optional PCM samples per voice (see setSample).
    std::vector<float> smp_[NV909];
    double smpRate_[NV909] = {};
    double smpPos_[NV909]  = {};
    bool   smpOn_[NV909]   = {};
    float  smpVel_[NV909]  = {};
    float  smpEnv_[NV909]  = {};    // knob-driven decay envelope
    float  smpEnvC_[NV909] = {};
    double smpRateMul_[NV909] = { 1,1,1,1,1,1,1,1,1,1,1 };  // tune rate shift
    // Hybrid sampled-snare noise burst (see trigger).
    float snNoiseEnv_ = 0.0f, snNoiseC_ = 0.0f, snNoiseLvl_ = 0.0f;
    Svf   snSvf_;
    // BD attack softening lowpass (sample path). See the trigger note.
    float bdLp_ = 0.0f, bdLpA_ = 1.0f;
    float level_[NV909];
    float bdTune_, bdDecay_, bdAttack_, sdTune_, sdSnap_;
    float extra_[EX_COUNT];
    Rng rng_;
};

} // namespace

std::unique_ptr<Instrument> makeDrum909() { return std::make_unique<Drum909>(); }

} // namespace reborn
