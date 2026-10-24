// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Waveshaping distortion (clean-room re-derivation, distortion supertask,
// 2026-09-17). Rewritten from a `tanh` soft-clip (BACKLOG entry 2: 18-33 dB
// short in the 2-16 kHz bands) to a hard aggressive waveshaper fitted
// directly against measured ReBirth renders (docs/research/PLAN_DIST.md, docs/
// SPEC_DIST.md), using the `dist_wav` harness and `tools/measure/
// dist_spectrum.py` on the 5x5 Amount x Shape byte grid in
// renders/dist-grid-2026-09-17/. No constant here comes from disassembly or
// any third-party analysis; every one is fit from that measured audio.
//
// WHY A PLAIN HARD CLIP IS NOT ENOUGH (measured, not assumed): a memoryless
// symmetric clamp(drive*x, -1, 1) was tried first and driven arbitrarily hard
// (drive up to 800x) against the actual clean drone (renders/dist-grid-
// 2026-09-17/d_off.wav, ~65 Hz fundamental). Its 2-16 kHz energy fraction
// tops out around 0.8% of total: a plain square-wave's odd harmonics decay
// as 1/n, and by the ~30th harmonic (2 kHz at this fundamental) there just
// isn't enough energy left to reach ReBirth's measured 4-8%. ReBirth's
// distortion is producing far more high-frequency energy than any amount of
// simple clipping can generate from this input, which is possible only
// through a mechanism that folds much more of the waveform's excursion back
// into new zero-crossings per cycle (a WAVEFOLDER) and/or reduces output
// resolution enough to add its own broadband content (a bit-depth crush).
// Both are simple, cheap, and exactly the kind of shortcut a late-90s
// software synth would take instead of true harmonic synthesis, and neither
// needs any oversampling to work; the point-wise aliasing IS the character
// (the spec's "if the hard clip must alias to match, let it").
//
// THE MODEL, FIT AGAINST THE MEASURED GRID:
//
//   in -> pre-gain (Amount) -> effective-ratio soft-knee (round 3, now barely
//      engaged; see round 4 below) -> triangle-wave FOLD (Shape sets the
//      fold threshold) -> level-quantize (Shape also sets the step count)
//      -> post one-pole low-pass (round 4, broadband-level trim) -> out
//
// Amount (`amount`, DIST byte 2) is the drive: a power-law multiplier from 1x
// at amount=0 up to ~36x at amount=1 (round 2, see the kDriveMax/kDriveExp
// comment below), so the fold threshold gets crossed harder and more often
// as Amount rises; this is what closes the 18-33 dB gap. A round-3 soft-
// knee (kEffKnee/kEffRatio) taming how much of that drive reaches the fold
// near the Shape peak is still here but loosened almost to nothing in round
// 4, because tools/measure/dist_inharm.py (added by Role A) showed reducing
// FOLD DEPTH cuts the aliased/inharmonic content along with the broadband
// level it was meant to fix. See the kEffKnee/kPostLpHz comments below for
// why round 4 moved the level correction to a post-fold filter instead.
//
// Shape (`tone` in code, kept named `tone` deliberately; the engine, the
// .rbs loader/writer and the plugin all address it by that string, and
// renaming it would touch files outside this round's scope. It is byte1,
// the character control, and is NOT a tone-tilt EQ any more) drives a single
// bell-shaped "character intensity" curve c(shape) that peaks at kCharMu and
// falls off at different rates on either side (kCharSigmaLo/Hi), fit
// directly from the byte1 sweep at Amount=127 (the cleanest row measured:
// HF% 4.3/5.4/7.9/6.0/1.5 and centroid 569/559/720/457/297 Hz at Shape
// 0/32/64/96/127, peaking near Shape 64). c(shape) does two things at once:
//   - shrinks the fold threshold (more character = tighter fold = brighter,
//     more chaotic aliasing), and
//   - is fed by a SEPARATE, independent, monotonic quantizer-step-count law
//     in Shape alone (kLevelsHi at shape=0 down to kLevelsLo at shape=1) so
//     that even where c(shape) is high, the top of the Shape range still
//     coarsens the output; this is what pulls energy back down at Shape
//     127 (the manual's "grungier", not brighter, character) instead of
//     folding forever.
// Two independent shape-only knobs (a bell for the fold and a ramp for the
// quantizer) is the fewest constants that reproduced both halves of the
// measured trend: a peak in the middle and a floor that is LOWER at Shape
// 127 than at Shape 0, which a single symmetric control cannot do.
//
// WHAT DID NOT FIT: even the denser 7x7 grid is not a smooth function of
// Shape or Amount: e.g. at Amount=127, HF% goes 4.33/8.42/9.27/7.86/10.72/
// 3.57/1.53 across Shape 0/21/43/64/85/106/127, more than one hump, not a
// single clean peak. A wavefolder driven this hard is genuinely chaotic in
// exactly this way (small changes in effective drive land a sample sequence
// on very different alias patterns), confirmed round 2 by a joint drive+
// shape search over the full dense grid that could not fit the bumps any
// better than this trend-level fit does. This model targets the per-Amount
// MEDIAN-across-Shape trend and the round-1 Amount=127 Shape sweep, not
// individual cells; per docs/research/PLAN_DIST.md's own anticipated fallback, a
// closer per-cell match would need either a finer grid still or accepting
// that this control is not capturable by a low-parameter smooth surface at
// this drive level. See the commit message for the actual per-cell
// residuals against both grids.
//
// Amount 0 is an EXACT bypass: process() returns without touching the block
// at all when the raw (unsmoothed) Amount parameter is 0, so a distortion-off
// render is byte-identical to the input, not merely "close". No smoothing
// state carries across that boundary, so re-enabling Amount snaps cleanly
// (see reset()/snap_).
//
// STRUCTURAL FOLLOW-UPS (Role A, 2026-09-18). The `level` param is now
// RETIRED (item 5; see the kP comment). The DIST bus pan-law and position-in-
// chain items are RESOLVED, not by a change here but by the per-section
// rewrite that landed during the PCF supertask (engine.h line ~192,
// Engine::renderSlice): there is no distortion bus summing without pan any
// more: every section, distorted or clean, carries its own chain in
// chScratch_ and is panned exactly once through panGains, distortion before
// PCF before the compressor. The absolute output/gain law (BACKLOG 2) is now
// FITTED (2026-09-18, supertask 7 item 3): the old kOutTrim centering constant
// is replaced by outGain(amount,shape) = kOutBase*exp(kOutAmt*amount +
// kOutShp*shape), fitted from ReBirth's measured output level; see the
// kOutBase comment below. The DIST timed-automation decode (item 4) is DONE
// too: AutoTarget::Dist in rbs.cpp/engine.cpp reads the DIST track's Amount/
// Shape/on-off rides. All five distortion structural items are complete; what
// remains before a main fold is Luke's sign-off and the whole-mix regression.
// Because the gain law is a pure output scalar, dist_spectrum.py (per-file-
// normalised band fractions) and dist_inharm.py (a ratio) are unaffected by it,
// so the character match above does not depend on the level.
//
// STABILITY: the fold/quantize stages are memoryless (the fold is an exact
// reflection into [-1,1] for any finite input, std::fmod is finite for
// finite arguments; the quantizer step count is clamped above zero; that
// stage's output is clamped to [-1,1]). Round 4 adds ONE recursive stage,
// the post low-pass, one pole per channel with a fixed coefficient strictly
// < 1 (kPostLpHz is clamped below Nyquist in process()) and denormals
// flushed every sample, so it cannot drift or blow up either. No parameter
// combination can produce NaN/Inf.
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>

namespace reborn {
namespace {

struct P { const char* n; const char* l; float def; };
// Param 1 keeps the NAME "tone" (see header comment) but is labelled "Shape"
// since that is what it does now. The old third param `level` was RETIRED
// 2026-09-18: nothing ever set it (ReBirth's distortion has only Shape and
// Amount, manual p60 / Appendix C), it defaulted to 0.7 so it only ever
// scaled the output by 0.7/0.7 = 1, and it showed up as a phantom knob on the
// plugin UI. The output level is the fitted outGain(amount,shape) law now (see
// the kOutBase/kOutAmt/kOutShp comment), which resolved the deferred absolute
// gain law (BACKLOG 2, docs/research/PLAN_DIST.md item 3).
const std::array<P,2> kP = {{ {"amount","Amount",0.5f}, {"tone","Shape",0.5f} }};

constexpr float kTwoPiF = float(kTwoPi);

// --- Fitted constants (renders/dist-grid-2026-09-17, dist_wav + dist_spectrum.py) ---

// Amount -> drive. Power law: 1x at amount=0 (exact identity, see the bypass
// branch below; this law is never evaluated there). Refit 2026-09-17 round
// 2 against a DENSER 7x7 grid (renders/dist-grid2-2026-09-17/, Amount/Shape
// byte in {0,21,43,64,85,106,127}): the round-1 law (kDriveExp=2.99, almost
// all of the drive arriving only in the last few percent of Amount) left the
// fold under-driven through the whole mid-Amount range: ReBirth already
// shows 1.4-4% HF by Amount 64 where round 1 put drive at only ~3.9x. A
// first attempt held Shape at its peak (c=1, foldThresh=kFoldFloor) to fit
// the drive law in isolation and landed on a much lower ceiling (~9x); that
// UNDER-drove every non-peak Shape once plugged back into the real model,
// because away from the peak foldThresh is much larger (up to 1.0 at c=0)
// and needs proportionally more raw drive to cross the fold's turn-on point
// at all; round 1's higher ~24x ceiling was doing real work reaching the
// off-peak Shape values, which is why its Amount=127 row (full drive) fit
// well even though its mid-Amount rows did not. So the drive ceiling stays
// high (~36x) and kDriveExp < 1 (a concave power law, most of its rise
// happening early) pulls that same high ceiling forward into the middle of
// the Amount range instead of holding it back near the top. Fit by grid
// search over (kDriveMax, kDriveExp) against the per-Amount MEDIAN HF across
// all 7 Shape values, evaluated through the FULL model (drive and the
// unchanged Shape bell/quantizer together, not drive alone): targets 0.03/
// 0.28/1.63/2.05/3.43/7.86% at Amount 21/43/64/85/106/127, this law's
// predicted medians 0.20/0.22/1.35/2.92/4.82/7.27%. The raw grid is bumpy
// cell-to-cell (aliasing sensitivity of a hard-driven fold; see WHAT DID
// NOT FIT below); the median is the trend this law targets, not a promise
// of a per-cell match.
constexpr float kDriveMax = 35.0f;
constexpr float kDriveExp = 1.6f;

// Shape -> character intensity c(shape), an asymmetric bell (bi-Gaussian)
// peaking at kCharMu. Fit against the Amount=127 row (see file header).
constexpr float kCharMu    = 0.314f;
constexpr float kCharSigLo = 0.494f;   // width for shape < kCharMu
constexpr float kCharSigHi = 0.525f;   // width for shape >= kCharMu

// c(shape) -> fold threshold: kFoldFloor at c=1 (tightest fold, brightest),
// 1.0 at c=0 (loosest: input has to swing much further before it folds at
// all).
constexpr float kFoldFloor = 0.166f;

// Effective-ratio soft-knee (round 3, 2026-09-17; loosened round 4). Ear
// check on the acid-line fixture (renders/dist-earcheck-2026-09-17/) called
// round 2 "right ballpark, but a little more FRIED than ReBirth's".
// Localized with dist_spectrum.py: at Amount96/Shape43 and Amount106/Shape21
// the low end already matched (+/-0.5 dB through 1 kHz) but 2-16 kHz ran
// +3.6 to +7.4 dB hot, peaking at 4-8 kHz. Round 3 fixed that by applying a
// soft-knee to the EFFECTIVE ratio driveRaw/foldThresh (not raw drive; a
// flat drive cap was tried first and wrecked Shape=127's already-good fit,
// see git history), which tamed exactly the tight-fold Shapes causing the
// "fried" complaint.
//
// Round 4: the round-3 ear check came back "undershot/muted: the vocoder
// texture is missing". `tools/measure/dist_inharm.py` (untracked, added by
// Role A) measures that texture directly: the fraction of 500 Hz-16 kHz
// energy that is INHARMONIC (aliased partials off the f0 grid, not multiples
// of the fundamental). Round 3 undershot it badly (25-47% vs ReBirth's
// 48-69% on the dense grid) because the knee reduces the same fold DEPTH
// that generates that aliasing, not just its broadband level; the two are
// coupled in round 3's design and the ear check is right that trading all of
// one for the other overcorrected. Round 4 decouples them: this knee is
// loosened until it barely engages (kEffKnee raised, kEffRatio taken most of
// the way back to 1:1), so fold depth (and the inharmonic content it
// generates) sits close to round 2 again; kPostLpHz below now does the
// broadband-level correction that round 3 asked the fold depth to do.
constexpr float kEffKnee  = 90.0f;
constexpr float kEffRatio = 1.6f;

// Post-fold low-pass (round 4, 2026-09-17): a plain one-pole rolloff after
// the fold and quantizer, so it attenuates whatever broadband energy they
// produced (harmonic AND inharmonic alike) roughly proportionally,
// trimming the round-2-level "fried" 2-16 kHz overshoot back toward ReBirth
// without preferentially gutting the aliased partials the way reducing fold
// depth does (that ratio is exactly what dist_inharm.py's fraction metric
// exposed round 3 getting wrong). One pole, one cutoff, applied always when
// Amount > 0 (never in the bypass branch). Cutoff is fixed, not swept with
// Amount/Shape: the two ear-check settings that motivated this bracket most
// of the grid's driven range, and a single gentle pole was enough to land
// both within the band-energy target without a second free parameter.
constexpr float kPostLpHz = 4800.0f;

// Shape -> quantizer step count, independent of c(shape): coarse (very few
// levels, grungiest) at Shape=1 even though c(shape) has already fallen from
// its peak by then, which is what pulls Shape=127 BELOW Shape=0 rather than
// symmetric with it.
constexpr float kLevelsHi = 103.0f;   // shape = 0 (fine, near-unquantized)
constexpr float kLevelsLo = 34.8f;    // shape = 1 (coarse, grungiest)

// Output gain LAW (fitted 2026-09-18, walled implementer, supertask 7 item 3;
// docs/specs/SPEC_DIST_GAIN.md). Replaces the old kOutTrim CENTERING CONSTANT
// (0.6105) with a fitted absolute-output-level law
//
//   outGain(amount, shape) = kOutBase * exp(kOutAmt*amount + kOutShp*shape)
//
// a separable exponential (three constants) fitted by least squares in the
// log (dB) domain to the outGain_target column of docs/dist_gain_target.csv
// over every amount>0 cell of the 7x7 Amount x Shape grid. outGain_target =
// 0.6105 * (L_rb / L_ours_current) is the absolute output gain that makes our
// broadband RMS match ReBirth's measured render at each cell (both sides
// distort ReBirth's OWN clean drone, so the ratio is purely the level
// discrepancy). ReBirth's output level FALLS as drive rises (the wavefolder
// gains energy with drive while ReBirth's output does not), so the old
// constant ran up to ~13 dB too LOUD at high drive; both marginals fall
// monotonically and this law follows them: Amount is the dominant, cleanest
// trend (kOutAmt), Shape modulates it more weakly and noisily (kOutShp, fitted
// shallower because the Shape marginal is bumpy: it even rises slightly from
// shape 0 to shape 0.165 before falling, which a monotonic factor cannot and
// is not meant to chase). This is a PURE OUTPUT SCALAR: it multiplies the
// final sample only and touches none of the fold/quantize/LP stages, so the
// per-file-normalized spectrum (dist_spectrum.py) and the inharmonic fraction
// (dist_inharm.py) are unchanged by construction. It collapses the median
// |gap_dB| across the 49 cells from 3.04 to ~1.8 and removes the systematic
// high-drive overshoot; the residual ~1.8 dB is the irreducible cell-to-cell
// aliasing scatter of a hard-driven fold (see WHAT DID NOT FIT above); the
// law targets the trend/marginals, not each chaotic cell. Clamped to a sane
// positive range so it is always finite and > 0 (the clamp never binds inside
// the grid: the law spans ~0.14 at (1,1) to ~1.27 at the never-evaluated
// amount=0 edge).
constexpr float kOutBase    = 1.268f;   // exp(L0) of the log-domain plane fit
constexpr float kOutAmt     = -1.262f;  // Amount slope (dominant trend)
constexpr float kOutShp     = -0.917f;  // Shape slope (weaker modulation)
constexpr float kOutGainMin = 0.05f;
constexpr float kOutGainMax = 2.0f;

// Asymmetric bell (bi-Gaussian): 1 at s=mu, falling off at different rates
// on either side. sigmas are clamped away from 0 for safety.
inline float bellCurve(float s, float mu, float sigLo, float sigHi) {
    const float d = s - mu;
    const float sig = (d < 0.0f) ? sigLo : sigHi;
    const float sigSafe = sig > 1.0e-3f ? sig : 1.0e-3f;
    return std::exp(-(d * d) / (2.0f * sigSafe * sigSafe));
}

// Triangle-wave reflection into [-1, 1], period 4 in v: v=0->0, v=1->1,
// v=2->0, v=3->-1, v=4->0 (and odd: fold(-v) == -fold(v)). This is what a
// passive wavefolder does to anything that exceeds the rails: it reflects
// the excursion back rather than clamping it, so a hard-driven input keeps
// generating new zero-crossings (and, at this drive with no oversampling,
// aliasing) instead of just flattening at the top.
inline float foldTriangle(float v) {
    float m = std::fmod(v + 1.0f, 4.0f);
    if (m < 0.0f) m += 4.0f;     // std::fmod keeps the dividend's sign; wrap positive
    m -= 1.0f;                   // m in [-1, 3)
    return (m > 1.0f) ? (2.0f - m) : m;
}

// Everything a single sample needs; computed once per sample, shared by L/R.
struct SampleParams {
    float drive;
    float foldThresh;
    float halfLevels;   // levels/2, so quantize is round(x*halfLevels)/halfLevels
    float outGain;
    float lpCoef;        // post-fold one-pole low-pass pole (see kPostLpHz)
};

// Per-channel state: just the post-fold low-pass's single pole of memory.
struct Channel {
    float lp = 0.0f;
};

class Distortion : public Effect {
public:
    void prepare(double sr) override {
        sr_ = sr > 8000.0 ? sr : kDefaultSampleRate;
        smooth_ = std::exp(-1.0f / (0.003f * float(sr_)));   // ~3 ms, pole strictly < 1
        reset();
    }

    void reset() override {
        snap_ = true;   // next process() snaps the smoother straight to target
        chL_ = Channel{};
        chR_ = Channel{};
    }

    int numParams() const override { return int(kP.size()); }
    ParamInfo paramInfo(int i) const override {
        if (i < 0 || i >= int(kP.size())) return {};
        return { kP[i].n, kP[i].l, 0.0f, 1.0f, kP[i].def, "" };
    }
    float getParam(int i) const override {
        return (i >= 0 && i < int(kP.size())) ? p_[i] : 0.0f;
    }
    void setParam(int i, float v) override {
        if (i >= 0 && i < int(kP.size())) p_[i] = clampf(v, 0.0f, 1.0f);
    }

    void process(StereoBlock& io, int n) override {
        if (n <= 0) return;

        const float amount = p_[0];
        const float shape  = p_[1];

        // Amount 0 = EXACT bypass. Touch nothing (not even a copy through a
        // unity path), so a distortion-off render is byte-identical to the
        // input, and reset the smoother so the next time Amount turns on it
        // snaps cleanly rather than fading up from a stale state.
        if (amount <= 0.0f) {
            snap_ = true;
            return;
        }

        // --- Targets (block rate; smoothed per sample below). ---
        const float driveRaw = 1.0f + kDriveMax * std::pow(amount, kDriveExp);

        const float c = bellCurve(shape, kCharMu, kCharSigLo, kCharSigHi);
        const float foldThreshT = clampf(kFoldFloor + (1.0f - kFoldFloor) * (1.0f - c), 0.02f, 4.0f);

        // Effective-ratio soft-knee (see kEffKnee/kEffRatio comment above),
        // loosened in round 4 (see kEffKnee/kEffRatio comment) so it barely
        // engages; fold DEPTH is restored close to round 2, because depth
        // is what generates the aliased/inharmonic partials; kPostLpHz
        // below now carries the broadband-level correction instead.
        const float effRaw = driveRaw / foldThreshT;
        const float effExcess = effRaw - 1.0f;
        const float effT = 1.0f + (effExcess <= kEffKnee
                                        ? effExcess
                                        : kEffKnee + (effExcess - kEffKnee) / kEffRatio);
        const float driveT = effT * foldThreshT;

        const float levels = clampf(kLevelsHi + (kLevelsLo - kLevelsHi) * shape, 2.0f, kLevelsHi);
        const float halfLevelsT = levels * 0.5f;

        // Fitted absolute-output-gain law (see the kOutBase/kOutAmt/kOutShp
        // comment). A pure output scalar: block-rate target, smoothed per
        // sample through sOutGain_ exactly like the other targets, evaluated
        // only here on the amount>0 path (the amount<=0 bypass above returns
        // before reaching it, so Amount 0 stays an exact byte-identical
        // bypass). Clamped strictly positive and finite.
        const float outGainT = clampf(kOutBase * std::exp(kOutAmt * amount + kOutShp * shape),
                                      kOutGainMin, kOutGainMax);

        const float nyq = 0.45f * float(sr_);
        const float lpCoefT = std::exp(-kTwoPiF * clampf(kPostLpHz, 5.0f, nyq) / float(sr_));

        if (snap_) {
            sDrive_ = driveT; sFoldThresh_ = foldThreshT;
            sHalfLevels_ = halfLevelsT; sOutGain_ = outGainT;
            snap_ = false;
        }

        SampleParams sp;
        sp.lpCoef = lpCoefT;   // fixed cutoff (see kPostLpHz); no need to smooth a constant
        const float a = smooth_;
        for (int i = 0; i < n; ++i) {
            sDrive_      = smoothTo(sDrive_,      driveT,      a);
            sFoldThresh_ = smoothTo(sFoldThresh_, foldThreshT, a);
            sHalfLevels_ = smoothTo(sHalfLevels_, halfLevelsT, a);
            sOutGain_    = smoothTo(sOutGain_,    outGainT,    a);

            sp.drive = sDrive_; sp.foldThresh = sFoldThresh_;
            sp.halfLevels = sHalfLevels_; sp.outGain = sOutGain_;

            io.left[i]  = processSample(io.left[i],  chL_, sp);
            io.right[i] = processSample(io.right[i], chR_, sp);
        }
    }

private:
    // One-pole smoother that snaps onto the target once it is close enough,
    // so the state never decays through denormal-sized residuals.
    static float smoothTo(float cur, float target, float pole) {
        const float next = target + pole * (cur - target);
        return (std::fabs(next - target) < 1.0e-6f) ? target : next;
    }

    static float processSample(float x, Channel& c, const SampleParams& sp) {
        const float v = sp.drive * x;
        const float u = v / sp.foldThresh;
        const float f = foldTriangle(u) * sp.foldThresh;
        float q = std::round(f * sp.halfLevels) / sp.halfLevels;
        q = clampf(q, -1.0f, 1.0f);
        // Post-fold one-pole low-pass (kPostLpHz): trims broadband level
        // without touching the fold/quantize stages that generate the
        // inharmonic (aliased) content those stages are there for.
        c.lp = flushDenormal(q + sp.lpCoef * (c.lp - q));
        return c.lp * sp.outGain;
    }

    double sr_ = kDefaultSampleRate;
    float  smooth_ = 0.9924f;   // overwritten in prepare()
    std::array<float,2> p_ = { kP[0].def, kP[1].def };

    bool  snap_ = true;
    float sDrive_ = 1.0f, sFoldThresh_ = 1.0f, sHalfLevels_ = 64.0f, sOutGain_ = 1.0f;
    Channel chL_, chR_;
};

} // namespace
std::unique_ptr<Effect> makeDistortion(){ return std::make_unique<Distortion>(); }
} // namespace reborn
