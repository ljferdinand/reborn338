// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Master-bus compressor (clean-room, fit from measured ReBirth exports).
//
// ReBirth exposes exactly two controls on this unit (manual ch.8 p.68: Ratio,
// Threshold), no attack/release/make-up. Everything below was fit to ReBirth's
// own audio output (docs/specs/SPEC_COMP.md, docs/research/PLAN_COMP.md); no constant comes
// from any third-party source.
//
// ROUND 5 (2026-10-01): the level detector, and with it the gain law.
// =================================================================
//
// What the compressor is, per sample (m = (l + r) / 2: that the detector sums
// L and R was measured 2026-10-01 on comp-rides mp_; that it sums SIGNED
// samples rather than |l| + |r| is UNMEASURED, since a hard-panned source
// cannot tell them apart; it needs an antiphase source):
//
//   lp   = m through two cascaded one-pole lowpasses (each tau kLowpassTau)
//   a    = |lp|
//   env  : if a > env:      env += (a - env) * atk   (tau kAttackTau), and the
//                           hold counter restarts
//          else if holding: env unchanged             (kHoldSeconds)
//          else:            env falls at kReleaseDbPerSec dB/s (exponential)
//          env = max(env, F)                          (the threshold floor)
//   g    = (1 - k) + k * T / env                      (LINEAR gain, applied
//                                                      to both channels)
//   k    = kRatioScale * ratioByte / 127
//   T    = (kTargetA + kTargetB * thrByte) * kCrest   (the level g pins to)
//   F    = kFloorA * (thrByte + kFloorB) * kCrest     (where boosting stops)
//
// There is no gain smoother: the gain follows the envelope sample by sample.
// All of the timing is in the detector, which is why a Ratio or Threshold ride
// lands within one 10 ms window both ways (ReBirth's comp-rides cr_ t90s are
// 15 / 15 / 15 / 25 ms), while a release after a hit takes the envelope's own
// time.
//
// The static law, g = (1 - k) + k * T / max(D, F)
// -----------------------------------------------
// Read off the 146 compressed cells of comp-law to comp-law6 (ratio > 0) in
// the saw drone's own RMS units, with the threshold floor F at threshold 0
// pinned by the two below-floor ceilings the kick tails of comp-material
// show (ratio 127 +29.72 dB, ratio 64 +24.08 dB). Least squares on five
// constants (kk, Ta, Tb, Fc, Fb0) fits every one of the 146 cells within
// 0.023 dB (rms 0.005 dB) and both ceilings within 0.02 dB. Read as a
// mechanism: the output is a crossfade, by k, between the dry signal and the
// same signal levelled to T, with the levelling's boost capped at T / F.
// It replaces round 2's dB-domain law (makeup - slope * softknee(over)),
// which reached 129 of the 152 tally cells within 1.0 dB and could not make
// the low-threshold "hump" rounds 3 and 4 chased: in dB, the linear
// crossfade's slope is near 1 just above F and relaxes toward 1 - k far
// above it, which is that hump. The ratio law is k linear in the byte
// (0.99 at 127, 0.748 at 96, 0.4985 at 64, each fit row by row before the
// joint fit), so ratio 0 is k = 0, g = 1: an exact bypass. T and F are both
// linear in the threshold byte in amplitude.
//
// The law was changed because the new detector needs it: the ceiling it
// releases to between hits (g at env = F) is a below-threshold value the
// 2026-09 campaign never measured at ratio 64 (round 2's extrapolation gave
// +8.8 dB at ratio 64 threshold 0; the kick tails show +24.1, which this law
// predicts, +24.04, from the static cells alone).
//
// The detector (fit to comp-material-2026-10-01, all seven sources)
// -----------------------------------------------------------------
// A peak follower, not RMS: on steady material it settles at the PEAK of the
// lowpassed signal, so it reads a square 2.9 dB lower, a resonant drone 2.8
// dB higher and a near-sine 3.5 dB lower than the saw, relative to RMS, as
// ReBirth does (PLAN_COMP.md, material probe). kCrest converts the static
// law, fit in the saw's RMS units, into the detector's units: it is the saw's
// peak-over-RMS as this detector reads it. Its timing was fit to the GAIN
// TRAJECTORIES ReBirth applies to the 909 kick and closed hat (on / off,
// folded per beat, 1 ms windows; the exports are sample-aligned) at all four
// material settings, together with all 28 material GRs, by differential
// evolution on a simulation of exactly this structure (scratch fitting
// harness, not shipped):
//   - The hold, then a linear-in-dB release: after a hit ReBirth's gain sits
//     flat, then rises at a constant ~0.156 dB/ms at ratio 127 threshold 0,
//     and at a rate that grows with the gain at ratio 64 (0.07 to 0.12 dB/ms),
//     which is what a dB-linear envelope fall through the linear law gives.
//   - The floor: the release stops at F, and the release starts EARLIER at a
//     higher threshold (hat at threshold 32: 39.9 ms; at threshold 0: 47.9
//     ms). A floored envelope starts a hit higher, stops being refreshed
//     sooner, and so its hold ends sooner; a smoother on the gain could not
//     also give the 15 ms ride t90s.
//   - The attack is a one-pole on the linear envelope, so it rises fast in dB
//     from a low envelope and slowly near the peak, and a brief hat moves it
//     only part of the way (the hat reads 4.9 dB lower at threshold 0 than at
//     threshold 32, because the envelope starts 15 dB lower there).
//   - The lowpass: without it the hat's sample peaks read far too hot (a
//     peak detector on the raw signal cut the hats by about 12 dB at ratio
//     127 threshold 0 in a trial fit, where ReBirth BOOSTS them by 3.5 dB).
//     Two cascaded one-poles of 6.56 kHz each (-3 dB at about 4.45 kHz at
//     44.1 kHz as implemented; 4.2 kHz is the analog prototype of this tau)
//     read the hat at ReBirth's level and leave the kick, the 303 and the
//     drones' peaks nearly untouched. A single one-pole near 3.8 kHz fit
//     almost as well (fit cost 0.60 against 0.49; hat trajectories 0.26
//     against 0.18 dB rms); the two-pole is kept because it fits better, not
//     because the filter's order was measured.
//
// The fit, from these inputs only: the 28 comp-material GRs, the kick and hat
// gain trajectories at the four material settings (1 ms windows, folded over
// 8 beats, windows where the source is above -45 dB), and the three
// comp-ballistics2 trajectories (2 ms windows, folded per bar). Free: kCrestDb,
// kLowpassTau, kAttackTau, kHoldSeconds, kReleaseDbPerSec. The static law's
// five constants were fit first, from the static cells, and held.
//
// Residuals (comp_wav, ReBirth's own uncompressed exports fed through this
// file; full tables in docs/specs/SPEC_COMP.md, "Round 5 result"):
//   comp-material, 7 sources x 4 settings: every cell within 0.17 dB (steady
//     sources within 0.06, moving within 0.17; round 2: 3.6 and 7.6 dB);
//   comp-law to comp-law6, 152 static cells: all within 0.03 dB (round 2:
//     129 within 1.0 dB, worst 4.20), none worse than round 2;
//   gain trajectories, rms over the windows above: kick 0.03 to 0.08 dB, hat
//     0.01 to 0.39 dB, the ballistics2 hits 0.16 and 0.11 dB, the pump 0.06 dB;
//   comp-ballistics2 analyzer: -10.5 dB of the hit's -10.9 by 34 ms; the pump
//     -3.12 dB at 125 ms and -0.15 at 250 ms (ReBirth -3.22 and -0.23);
//   comp-rides through the engine, corrected for the TPT drone as PARITY.md
//     does: every compressed cell within 0.07 dB; ride t90s 5 / 15 / 15 / 5
//     ms (ratio down / ratio up / threshold down / threshold up; ReBirth
//     15 / 15 / 25 / 15, round 2 95 / 15 / 15 / 85).
// The largest trajectory residual left is the hat at ratio 127 threshold 0:
// ours holds about 0.3 dB less cut and releases about 8 ms later than ReBirth.
//
// Kept from earlier rounds: the detector's input (l + r) / 2, one gain for
// both channels, ratio 0 an exact bypass, the param names, and the stability
// guards (a floor on the envelope, NaN-catching state writes, denormal
// flushing). No heap allocation, locks, or I/O anywhere in process().
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>
#include <algorithm>

namespace reborn {
namespace {

constexpr int kNumParams = 2;

struct ParamDef { const char* name; const char* label; float def; const char* unit; };
const std::array<ParamDef, kNumParams> kParams = {{
    { "ratio",     "Ratio",     0.0f, ":1" },   // 0..1 = byte/127; 0 = exact bypass
    { "threshold", "Threshold", 1.0f, "dB" },   // 0..1 = byte/127
}};

// ---- The static law (saw-RMS units, fit to the 146 static cells) ---------
// T is -32.73 dBFS at threshold byte 0 and -18.81 at 127; F is -62.55 and
// -16.30 (saw RMS). Ratio 127 gives k = 0.98996.
constexpr float kRatioScale = 0.98996f;     // k = kRatioScale * ratioByte / 127
constexpr float kTargetA    = 0.0230948f;   // T = kTargetA + kTargetB * thrByte
constexpr float kTargetB    = 0.00072119f;
constexpr float kFloorA     = 0.00120028f;  // F = kFloorA * (thrByte + kFloorB)
constexpr float kFloorB     = 0.62096f;

// ---- The detector (fit to comp-material and comp-ballistics2) ------------
constexpr float kCrestDb         = 8.135f;      // saw peak / RMS as read, dB
constexpr float kLowpassTau      = 24.26e-6f;   // s, each of two cascaded one-poles
constexpr float kAttackTau       = 8.94e-3f;    // s, one-pole on the envelope
constexpr float kHoldSeconds     = 26.29e-3f;   // s
constexpr float kReleaseDbPerSec = 150.9f;      // dB/s, after the hold

// One-pole smoothing step (the fraction moved per sample) for a time
// constant. In [0, 1] for any tau and rate, so x += c * (target - x) is
// unconditionally stable.
inline float onePoleStep(float tauSeconds, double sr) {
    if (tauSeconds <= 0.0f || sr <= 0.0) return 1.0f;
    return clampf(1.0f - std::exp(-1.0f / (tauSeconds * static_cast<float>(sr))), 0.0f, 1.0f);
}

class Compressor : public Effect {
public:
    Compressor() {
        for (int i = 0; i < kNumParams; ++i) p_[i] = kParams[i].def;
        // Working coefficients before any prepare(): without them the
        // detector would never release (hold 0, release multiplier 1).
        Compressor::prepare(kDefaultSampleRate);
    }

    void prepare(double sampleRate) override {
        sr_ = (sampleRate > 1.0) ? sampleRate : kDefaultSampleRate;
        lpStep_   = onePoleStep(kLowpassTau, sr_);
        atkStep_  = onePoleStep(kAttackTau, sr_);
        holdLen_  = static_cast<int>(std::lround(kHoldSeconds * sr_));
        // dB/s -> a per-sample multiplier on the linear envelope.
        relMul_   = static_cast<float>(std::pow(10.0, -double(kReleaseDbPerSec) / (20.0 * sr_)));
        reset();
    }

    void reset() override {
        lp1_  = 0.0f;
        lp2_  = 0.0f;
        env_  = 0.0f;     // raised to the floor on the first sample
        hold_ = 0;
    }

    int numParams() const override { return kNumParams; }

    ParamInfo paramInfo(int index) const override {
        const int i = (index >= 0 && index < kNumParams) ? index : 0;
        return { kParams[i].name, kParams[i].label, 0.0f, 1.0f,
                 kParams[i].def, kParams[i].unit };
    }

    float getParam(int index) const override {
        return (index >= 0 && index < kNumParams) ? p_[index] : 0.0f;
    }

    void setParam(int index, float value) override {
        if (index >= 0 && index < kNumParams) p_[index] = clampf(value, 0.0f, 1.0f);
    }

    void process(StereoBlock& io, int numFrames) override {
        // ---- Per-block decode (constant across a block). ----
        const float ratioByte = p_[0] * 127.0f;
        const float thrByte   = p_[1] * 127.0f;
        const float crest     = std::pow(10.0f, kCrestDb / 20.0f);
        const float k         = kRatioScale * ratioByte / 127.0f;
        const float target    = (kTargetA + kTargetB * thrByte) * crest;
        const float floorLin  = kFloorA * (thrByte + kFloorB) * crest;   // > 0 for any byte
        // k == 0 (ratio byte 0) is an exact bypass: the gain is never applied.
        // The detector still runs so a ratio ride picks up a settled envelope.
        const bool bypass = !(k > 0.0f);

        const int n = numFrames > 0 ? numFrames : 0;
        for (int i = 0; i < n; ++i) {
            const float l = io.left[i];
            const float r = io.right[i];

            // (1) Detector input: the L+R sum, halved (comp-rides mp_: a
            //     hard-left drone reads 5.73 to 5.92 dB lower than centred).
            //     Signed versus |l| + |r| is unmeasured (needs an antiphase
            //     source).
            //     Then the two-pole lowpass that keeps a hat's sample peaks
            //     from reading 15 dB hot. Self-healing writes: a NaN or a
            //     runaway value resets a stage to 0.
            const float m = (l + r) * 0.5f;
            const float s1 = lp1_ + lpStep_ * (m - lp1_);
            lp1_ = (std::fabs(s1) < 1.0e30f) ? flushDenormal(s1) : 0.0f;
            const float s2 = lp2_ + lpStep_ * (lp1_ - lp2_);
            lp2_ = (std::fabs(s2) < 1.0e30f) ? flushDenormal(s2) : 0.0f;
            const float a = std::fabs(lp2_);

            // (2) Peak envelope: one-pole attack, hold, dB-linear release,
            //     floored at F. The floor write also catches a NaN envelope,
            //     and a ceiling far above any audio keeps it finite.
            float env = env_;
            if (a > env) {
                env += atkStep_ * (a - env);
                hold_ = holdLen_;
            } else if (hold_ > 0) {
                --hold_;
            } else {
                env *= relMul_;
            }
            env_ = (env >= floorLin) ? (env <= 1.0e6f ? env : 1.0e6f) : floorLin;

            // (3) Linear gain law, both channels alike.
            if (bypass) continue;
            const float gain = (1.0f - k) + k * target / env_;
            io.left[i]  = flushDenormal(l * gain);
            io.right[i] = flushDenormal(r * gain);
        }
    }

private:
    double sr_ = kDefaultSampleRate;
    std::array<float, kNumParams> p_ {};
    float lpStep_  = 1.0f;
    float atkStep_ = 1.0f;
    int   holdLen_ = 0;
    float relMul_  = 1.0f;
    float lp1_     = 0.0f;   // (l + r) / 2, first lowpass stage
    float lp2_     = 0.0f;   // second stage: the detector's input
    float env_     = 0.0f;   // peak envelope, linear amplitude, >= the floor
    int   hold_    = 0;      // samples of hold left
};

} // namespace

std::unique_ptr<Effect> makeCompressor() { return std::make_unique<Compressor>(); }

} // namespace reborn
