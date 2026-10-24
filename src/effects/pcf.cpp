// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Pattern-Controlled Filter (clean-room).
//
// A resonant lowpass whose cutoff is driven step-by-step by the sequencer:
// the engine writes the "mod" parameter (0..1) once per 16th-note step and the
// filter GLIDES toward
//
//     cutoff_param = base_cutoff + envmod * mod
//
// resonating up toward (but provably never past) self-oscillation.
//
// Design notes
// ------------
// * Core: zero-delay-feedback (TPT / trapezoidal) state-variable filter after
//   Zavalishin, "The Art of VA Filter Design". The linear core is
//   unconditionally stable for any damping k > 0 and any finite cutoff below
//   Nyquist, so it cannot ring away to NaN/Inf on its own. ReBirth's is one
//   filter with two outputs: the bandpass and the lowpass share a corner
//   frequency to within 0.2 dB at every setting measured, so the MODE switch
//   picks an output and retunes nothing.
//
// * Every law below was measured on 2026-08-27 against 49 ReBirth exports,
//   with Amt = 0 so the envelope is out of the path. See docs/research/PCF_FINDINGS.md
//   section 3 for the fixtures, the two sources, and the residuals.
//
// * The envelope is an attack/decay retriggered by the pattern, per the manual
//   (p63): the attack comes from the pattern and the decay from the panel. It
//   sums with the Freq knob in PARAMETER space and the sum is clamped, which is
//   what makes Amt inert once Freq is at maximum (p65).
//
//     cutoff_param = freq + amt * env,   clamped to [0, 1]
//
//   Decay measured 2026-08-29: the knob is QUANTISED to sixteen steps and its
//   rates are a 1-2-3-4-5-7 series. See kDecayRate below.
//
// * The 55 preset patterns in pcf_presets.inc were measured, not transcribed
//   from Appendix D. See that file for how, and for what is least certain in
//   them.
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>
#include <cstdlib>   // std::getenv, for the REBORN_PCF_RESO A/B flag

#include "pcf_presets.inc"

namespace reborn {
namespace {

enum { C_CUT, C_RES, C_ENV, C_DEC, C_MOD, C_MODE, C_PATTERN, kNumParams };

struct PDesc { const char* name; const char* label; float def; };
const std::array<PDesc, kNumParams> kParams = {{
    {"cutoff",    "Cutoff",    0.5f},
    {"resonance", "Resonance", 0.0f},
    {"envmod",    "Env Mod",   0.0f},
    {"decay",     "Decay",     0.5f},
    {"mod",       "Mod In",    0.0f},
    {"mode",      "Mode",      0.0f},   // < 0.5 lowpass, >= 0.5 bandpass
    {"pattern",   "Pattern",   0.0f},   // 0..1 maps to preset 0..54
}};

// THE DECAY KNOB IS QUANTISED TO SIXTEEN STEPS, one per `byte >> 3`, and the
// sixteen decay RATES are the classic 1-2-3-4-5-7 logarithmic series. Measured
// 2026-08-29 over rounds 7 and 8 by fitting each trigger's whole trajectory
// rather than one crossing time, and confirmed at two-byte resolution across a
// whole decade: tau is bit-identical within each group of eight bytes and steps
// at every multiple of eight.
//
//     tau = 88.6 ms / rate
//
// Thirteen of the sixteen entries are measured directly, worst error 1.2
// percent and 0.6 rms. Entries 0 and 1 sit below the 11.6 ms analysis window and
// come from the series (tau multiplies by exactly 10 every 48 bytes, confirmed
// seven times over from 9.79 to 10.06).
//
// THE TOP BAND (byte >> 3 == 15, bytes 120-127) HOLDS: no decay. Re-measured
// 2026-09-10 (PCF_FINDINGS 15, renders/pcf-probes-2026-09-10): a single trigger's
// cutoff centroid stays ~6500-7000 Hz flat across a whole 2.667 s bar at the top
// byte, where byte 96 and 112 clearly fall. The old inferred rate 0.04 (tau
// 2.215 s) would darken ~70 percent over that bar, so it was wrong; the band is a
// hold. Rate 0 below is the sentinel for it; the value is forced by the measured
// no-decay.
//
// The old law was `tau = 11.1 ms * e^(5.338 * knob)`, a smooth exponential from
// a threshold-crossing metric. It implied a decade every 54.8 bytes, so its
// exponent was 14 percent wrong, and being smooth it could not have the steps
// at all: it ran 43 percent long at byte 32 and 41 percent short at byte 96.
//
// The knob is in SECONDS, not beats. The same bytes at 30 BPM and at 90 BPM
// agree within 0.5 percent across a threefold tempo change.
constexpr int   kDecaySteps = 16;
constexpr float kDecayUnit  = 0.0886f;    // seconds, the rate-1 entry
constexpr float kDecayRate[kDecaySteps] = {
    10.0f, 7.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.7f,
    0.5f,  0.4f, 0.3f, 0.2f, 0.1f, 0.07f, 0.05f, 0.0f   // top band (rate 0) = HOLD
};

// Decay knob (0..1) -> tau in seconds, through the measured table. Rate 0 is the
// top-band hold: a tau far longer than any render, so its per-sample envDecay
// rounds to exactly 1.0 (the envelope does not decay).
static float decayTau(float knob) {
    int i = static_cast<int>(clampf(knob, 0.0f, 1.0f) * 127.0f + 0.5f) >> 3;
    if (i < 0) i = 0;
    if (i >= kDecaySteps) i = kDecaySteps - 1;
    const float rate = kDecayRate[i];
    return (rate > 0.0f) ? (kDecayUnit / rate) : 1.0e9f;
}

// ENVELOPE -> CUTOFF: DEPTH-CAPPED (re-derived 2026-09-13, PCF_FINDINGS 22,
// docs/specs/SPEC_PCF_ENVELOPE.md). This supersedes both the shipped additive+clamp law
// and the first re-derivation on this branch (headroom-multiplicative), which
// over-opened at very low base.
//
// The shipped law summed the envelope into the cutoff parameter additively and
// HARD-CLAMPED at 1.0: cparam = clamp(base + 0.95*amt*env, 0, 1). On a high-Amt or
// high-base sweep the sum exceeded 1.0 at the trigger, so fc PINNED at the ceiling
// (~15.7 kHz) and HELD there until env decayed back under the clamp: the audible
// plateau. A pure headroom law (base + D*Asat*env*(1-base)) removed the plateau but
// OVER-OPENED at very low base, because ReBirth's reach ROLLS OFF there (base 10 at
// Amt 110 reaches ~6.3 kHz, not the ceiling): more headroom did not mean more reach.
//
// Measured law (renders/pcf-envsweep-2026-09-13 dense base sweep + the original
// renders/pcf-sweep-2026-09-11, tools/measure/pcf_trajectory.py, one method both
// sides). Converting ReBirth's tracked peak-cutoff(t) back to the parameter, each
// fixture's descent is LINEAR in env and UNCLAMPED, cparam = base + g*env, and g is
// the MIN of an additive depth cap and a headroom line:
//
//     cparam = base + g * env,   g = min( gcap(amt), D * (1 - base) )   // NO clamp
//
// At LOW base (lots of headroom) g = gcap(amt): the reach is additive so it does NOT
// over-open. Measured g at Amt 110 is flat ~0.795 across base bytes 5..20. At HIGH
// base (little headroom) g = D*(1 - base): the reach shrinks so base + g*env stays
// below 1.0 and the cutoff descends immediately, no plateau. D fit from the base
// sweep at base >= 30 (g/(1-base) = 0.98..1.00, converting peaks through our own
// filter's measured peak-vs-cparam map, calibrated on the non-clamping renders whose
// cparam we know exactly); D = 0.99 central. The base-30 a110 == a127 that looked
// like Amt saturation in the first re-derivation is really this headroom limit: the
// min picks D*(1-base) for both.
//
// gcap(amt) is the additive depth cap and it does NOT saturate: at base 15 the reach
// grows Amt 110 -> ~8 kHz, Amt 127 -> ~14 kHz (back-out gcap ~0.65/0.79/0.87 at Amt
// 90/110/127). It is pinned to the shipped additive depth 0.95*amt at and below Amt
// 90, so at the fixtures' base Freq 30 (where the cap binds for Amt <= 90 and the min
// is a no-op) the law is byte-identical to the shipped one and the low/mid patterns
// do not move. Above Amt 90 it bends to a gentler slope through the measured
// ~0.795 at Amt 110 (kGcapHiSlope), still monotonic (no saturation). Only the ends
// are fixture-constrained (0.95*amt at/below byte 90, ~0.795 at byte 110); the Amt-90
// knee's exact place is not, and gcap above byte 110 only sets the low-base Amt 127
// reach (a qualitative "grows, no saturation" check), never a named clamp fixture,
// which is headroom-limited.
//
// REBORN_PCF_ENVLAW=old (or =0) selects the shipped additive+clamp law for A/B; any
// other value or unset uses the depth-cap law. This is the only behavioural change:
// the AD envelope, the decay law, the cutoff law incl. the top-bend, and the
// resonator are all untouched.
constexpr float kEnvHeadroom = 0.99f;    // D: headroom coefficient, g <= D*(1-base)
constexpr float kGcapLo      = 0.95f;    // additive depth per unit Amt at/below the knee (== shipped depth)
constexpr float kGcapKneeAmt = 90.0f/127.0f;  // Amt where gcap leaves the shipped 0.95*amt line
constexpr float kGcapHiSlope = 0.773f;   // gcap slope above the knee (fits measured ~0.795 at Amt 110)
constexpr float kEnvDepthOld = 0.95f;    // the shipped additive depth (A/B only)

// COMPRESSIVE VELOCITY CURVE (measured 2026-09-14, PCF_FINDINGS 23). The pattern
// step sets the envelope level through env0 = f(velocity), not env0 = velocity.
// ReBirth boosts the quiet steps: a velocity-0.65 step reaches the cutoff a full-
// velocity-0.88-equivalent would, so a low-velocity pattern sweeps as one wide
// continuous arc rather than the staircase of small resets a linear map gives.
// Measured on the pattern-motion probe (renders/pcf-patternmotion-2026-09-14): at
// F70 Q90 A90 the per-trigger peak reach saturates the analyzer by velocity ~0.68
// in ReBirth (pat19), where ours (linear) still climbed slowly, and pat16 (peak
// velocity 0.652) reaches ~7.3 kHz where ours reached ~3.7.
//
// Model (refit 2026-09-16, FINDINGS 26 / SPEC addendum 4): f(v) is an S-SHAPED
// Naka-Rushton / Hill curve, not the power law it replaces. The power law
// (gamma < 1, concave) LIFTS every sub-full velocity toward the ceiling, most
// aggressively right at the toe (v near 0), which was fit against the
// under-reaching pre-FINDINGS-25 model and, once the reach was corrected,
// over-boosted the quiet pattern steps: pat16's cutoff stayed half-open on its
// quiet steps where ReBirth drops it nearly shut (a percussive "click-and-clack"
// that the power law cannot produce, because it has no gentle toe). ReBirth's
// curve is S-shaped: quiet steps stay quiet (a gentle toe, v^n for n > 1 near
// v = 0), the boost is concentrated in the MIDDLE, and it converges to unity
// at full velocity.
//
//     h(v) = v^n / (v^n + K^n)                     the raw Hill fraction
//     f(v) = Vmax * h(v),  Vmax = kVelFull / h(kVelFull)
//
// ANCHORED at the preset full velocity kVelFull = 1.025 by construction, so
// f(1.025) = 1.025 exactly and every full-velocity trigger (pattern 0/2/5, and
// so the whole base sweep, the clamp fixtures and the low/mid set, all velocity
// 1.025) is BYTE-identical: this curve moves only the sub-full-velocity steps.
// f(0) = 0, monotonic increasing for n > 0.
//
// kVelHillN and kVelHillK were fit against pmo_p16's and pmo_p19's rendered
// cutoff(t) TROUGH/median/peak (not a single-trigger velocity inversion, which
// FINDINGS 23 found too noisy under retrigger overlap): pat16's velocities are
// 0.337/0.473/0.617/0.652, and matching ReBirth's trough (~1.4-1.6 kHz, the
// filter dropping nearly shut on the quiet steps) against its peak (~6.5-6.7
// kHz) pins both parameters. See PCF_FINDINGS section 26/27 for the fit.
constexpr float kVelFull   = 1.025f;     // preset full velocity; f(kVelFull) == kVelFull (full velocity unmoved)
constexpr float kVelHillN  = 2.08f;      // Hill coefficient (steepness); > 1 gives the gentle toe
constexpr float kVelHillK  = 0.397f;     // half-saturation velocity (raw h(K) = 0.5, before the Vmax anchor)

// ATTACK RAMP / GLIDE (measured 2026-09-14, PCF_FINDINGS 24). A trigger used to
// set the envelope LEVEL to f(velocity) in a single sample (an instant jump), which
// gave a vertical sawtooth leading edge. ReBirth RAMPS to the target over ~30-40 ms
// (a one-pole approach) into a rounded top: a wave, not a sawtooth. So on a trigger
// the envelope TARGET jumps to f(velocity) (retrigger still REPLACES, and a lower
// trigger pulls the target DOWN); the LEVEL then GLIDES toward that target with a
// one-pole attack of time constant kAttackTau, while the target decays under the
// measured decay law as before. The level catches the (decaying) target near the
// peak and then follows it down, so the DESCENT is still the measured decay (the
// level lags the target by tau_a/tau_decay, ~1% at decay 96, so the descent-based
// reach fits are essentially unchanged; the rounding caps the fast-trigger peak,
// which is why the velocity gamma is refit lower here to keep pat16's peak on RB).
// Applied only on the new law; REBORN_PCF_ENVLAW=old keeps the instant jump.
constexpr float kAttackTau = 0.008f;     // seconds; one-pole attack toward the trigger target, fit to RB's pmo_p16 rise (~8 ms)
// How close env_ must get to the held envTgt_ (as a fraction of envTgt_) before
// the attack is considered to have REACHED the peak and the target is allowed to
// start decaying (ADDENDUM 3). NOT the small value the spec seeded (0.02): a
// one-pole glide takes -tau*ln(eps) to close to within eps, so a small eps holds
// the target undecayed for a material fraction of the decay tau (at decay 64,
// tau 177 ms, eps 0.02 holds ~31 ms), which reads brighter than RB at 40-150 ms
// on the base-sweep and clamp fixtures (measured up to +26.8%, where section 24
// matched within ~7-9%). Refit 2026-09-16 against the base sweep
// (esw_b005..b110), the named clamp fixtures (psw_a110/a127/f050/f070) and the
// pattern-motion p95 metric together: 0.13 (hold ~15 ms) brings the base sweep
// and all four clamp fixtures back within the ~15% regression tolerance (worst
// 13.5%) while keeping pmo_p05 within tolerance too. It does not fully close
// pmo_p16 (+19.9% vs RB) or pmo_p19 (-17.7%); no single value here satisfies
// both those and the regression set at once; PARITY conflict recorded in
// PCF_FINDINGS section 25. Not called final.
constexpr float kReachEps = 0.13f;

// Additive depth cap gcap(amt), amt in 0..1. Below the knee it is the shipped
// 0.95*amt (so the min is a no-op at the fixtures' base Freq 30 for Amt <= 90 and
// the low/mid stay byte-identical); above the knee it bends to a gentler,
// still-rising slope through the measured ~0.795 at Amt 110. Monotonic, no
// saturation.
static float gcapFor(float amt) {
    const float a = clampf(amt, 0.0f, 1.0f);
    if (a <= kGcapKneeAmt) return kGcapLo * a;
    return kGcapLo * kGcapKneeAmt + kGcapHiSlope * (a - kGcapKneeAmt);
}

// S-shaped velocity -> envelope-level curve (Naka-Rushton / Hill). f(kVelFull) =
// kVelFull exactly (so full-velocity triggers do not move), f(0) = 0, monotonic,
// a gentle toe near v = 0 (quiet steps stay quiet) with the boost concentrated
// in the middle. See the note above.
static float velocityCurve(float v) {
    if (v <= 0.0f) return 0.0f;
    // Exact fast path at and above full velocity: the algebra guarantees
    // f(kVelFull) == kVelFull, but the Hill formula's division does not round
    // to bit-exact kVelFull in float, which broke byte-identity on every
    // full-velocity pattern (pmo_p05/p00, the whole base sweep, the clamp
    // fixtures). No preset velocity exceeds kVelFull (1.025), so this only
    // ever fires at exactly the anchor.
    if (v >= kVelFull) return v;
    const float vn   = std::pow(v, kVelHillN);
    const float kn   = std::pow(kVelHillK, kVelHillN);
    const float fulln = std::pow(kVelFull, kVelHillN);
    const float vmax = kVelFull * (fulln + kn) / fulln;
    return vmax * vn / (vn + kn);
}

// Cutoff: fc = 9.01 * 2^(0.08388 * byte), fitted over Freq bytes 48..127 with
// a worst residual of 0.45 dB. fc(0) = 9.0 Hz, fc(64) = 372 Hz,
// fc(127) = 14.5 kHz, a span of 10.65 octaves. Bytes 0, 16 and 32 are NOT
// measured: their corner sits below the drone fixture's 64.6 Hz fundamental,
// so the fit could only trade cutoff against gain. This extrapolates them.
constexpr float kFreqLo      = 9.01f;
constexpr float kLog2Span    = 10.653f;   // 0.08388 oct/byte * 127
constexpr float kMinCutoffHz = 5.0f;

// THE CUTOFF LAW BENDS UP ABOVE ~4.4 kHz. The log-linear law above was fitted on
// the 303 drone, which cannot resolve a corner above its own harmonics (~8 kHz),
// so its top (Freq bytes ~106..127) was extrapolation. Measured 2026-09-10 with a
// BROADBAND probe (crash+hat 16ths, static Amt 0, Q 0) whose rolloff corner reads
// directly to ~13 kHz (fixtures/renders pcf-ceiling-2026-09-10, gen_pcf_ceiling.py):
// ReBirth's corner tracks the log-linear law to byte ~106 and then climbs FASTER,
// reaching ~11.3 kHz at byte 120 (law 9.66) and full-open by byte 127 (law 14.5).
// A quadratic bend in log-frequency above the knee matches the measured corners
// within ~1.2 percent (byte 108..122), and is exactly zero below the knee so every
// MEASURED lower/mid cutoff is untouched. See PCF_FINDINGS. This corrects a static
// parity gap only; the swept-resonance over-open (a documented residual) is
// unaffected in character since it already sits above the source band.
constexpr float kTopBendKnee = 0.831f;    // base-Freq fraction where the law departs (byte ~106, ~4.4 kHz)
constexpr float kTopBendGain = 18.0f;     // extra octaves = gain * (baseFreq - knee)^2 above the knee

// Resonance: 1/Q = 0.1055 + 1.3378 * (1 - r)^1.77, residuals within 0.18 dB
// over Q bytes 0 to 112. Q(0) = 0.693, which is Butterworth to within a
// percent, so minimum resonance is maximally flat and not damped.
//
// THE KNOB SATURATES AT BYTE 112. Round 2 could not tell: its 64.6 Hz drone
// could not resolve a peak narrower than its harmonic spacing, so bytes 112 and
// 127 both fitted to the same value and that was read as a measurement failure.
// Re-measured 2026-08-28 with a drone an octave lower, twice the grid density:
// bytes 112, 120 and 127 are BIT-IDENTICAL, 0.00 dB rms between their transfer
// curves, while 104 to 112 differ by 5.72 dB at the peak. The law used to
// extrapolate to Q = 9.5 at byte 127; the real answer is 7.35 from 112 upward,
// and two shipped songs sit at 127.
constexpr float kDampMin = 0.1055f;
constexpr float kDampAmt = 1.3378f;
constexpr float kDampExp = 1.77f;
constexpr float kResMaxByte = 112.0f / 127.0f;

// THE CORNER MOVES WITH RESONANCE. A TPT state-variable filter holds its corner
// where the coefficient puts it, by construction. ReBirth's does not: fitting
// its corner across the resonance sweep at a fixed Freq gives 373, 354, 350,
// 359, 373, 393, 413, 440 Hz for bytes 0 to 112, a smooth dip and then a rise
// to +1.44 dB. Same estimator on both sides, so it is the filter and not the
// method. Quadratic in the knob and CONSTRAINED to 1.0 at zero resonance, so
// the cutoff law above stays exactly what it was fitted as: that law was
// measured with the resonance knob at 0, and an unconstrained fit here moved
// its anchor by 0.1 dB. Worst residual 0.16 dB.
constexpr float kResFcB = -0.33441f;
constexpr float kResFcC =  0.62565f;

// ===================================================================
// RE-DERIVED RESONATOR (2026-09-11), behind REBORN_PCF_RESO.
// ===================================================================
// The shipped resonator matched ReBirth at low/mid cutoff but was measured
// (PCF_FINDINGS 17-18, renders/pcf-surface-2026-09-11) to be 10-15 dB
// UNDER-resonant at high cutoff and high Q, and its corner ran high there. Two
// facts drive the re-derivation, both from the measured (Q band x cutoff)
// prominence+corner surface read by tools/measure/pcf_surface.py:
//
//   1. QUANTISED RESONANCE (PCF_FINDINGS 15 P3). The resonance control is 8
//      discrete bands of 16 bytes (band = byte >> 4); the transfer is identical
//      within a band. So the knob selects one of 8 damping settings, not a
//      continuous 1/Q. The per-band base damping is the shipped law evaluated at
//      the band centre (byte b*16+8), which reproduces the shipped low/mid Q
//      that already matched ReBirth to 0.3 dB.
//
//   2. DAMPING DECREASES WITH CUTOFF. ReBirth's resonant-peak prominence GROWS
//      with cutoff (band 7: 27.8 -> 32.2 dB across Freq 96..122) where a plain
//      TPT SVF holds a flat ~17 dB (peak height = Q, independent of fc). So the
//      damping k = 1/Q must fall as the cutoff opens. The per-band multiplier
//      table kResoMult[band][anchor] below was fit by inverting the exact TPT
//      transfer to land each (band, cutoff) on ReBirth's measured prominence.
//      Anchors are in cutoff-parameter (cparam) space; the multiplier is 1.0 at
//      and below the knee, so every Freq <= 90 cutoff is byte-identical to the
//      shipped filter. The reduction rides the LIVE cutoff (envelope included),
//      which is what makes a swept envelope's resonant peak stay sharp as it
//      descends from the top, closing the "flangy chirp" of PCF_FINDINGS 16.
//
// The corner ran high because the Q0-fitted top-bend (kTopBend*) double-counts
// with the resonance corner shift (kResFc*) once resonance is up: at band 7
// Freq 122 the bend pushed our peak to 15.9 kHz where ReBirth sits at 12.5 kHz.
// Removing the bend on the resonant path (band > 0) lands the corner within
// ~3.5% of ReBirth across the surface (TPT plain*fcScale), while band 0 keeps
// the bend so the Q0 broadband ceiling of PCF_FINDINGS 14 is preserved.
//
// A sin-warped Chamberlin SVF was tried as the structure (its natural warp both
// grows the peak and lowers the corner toward Nyquist); it was rejected on
// measurement: it goes unstable as 2*sin(pi*fc/sr) -> 2 near Nyquist and its
// prominence growth was well short of the surface. The tan-prewarped TPT with
// this banded, cutoff-dependent damping matches the surface and stays stable.
//
// Verified 2026-09-11 against the ReBirth renders with pcf_surface.py (surface +
// hires): prominence within 0.1 dB across Freq 96..122 x the four Q bands (it now
// grows 27.8 -> 32.2 dB at band 7 where the old filter held a flat 17), corner
// within 1.8%, Freq 64 low/mid byte-identical to the shipped filter, and with
// REBORN_PCF_RESO=0 the whole surface is byte-identical to the shipped exe. On
// the swept chirp psw_a110/psw_a090 the resonant peak now descends prominently
// from the top at 30-42 dB (the shipped filter left it a faint ~19 dB stuck near
// the ceiling), closing the flangy chirp of PCF_FINDINGS 16. The remaining
// swept top-ceiling over-open (peak ~10% high for ~40 ms at the very top) is the
// separate cutoff-reach residual of PCF_FINDINGS 16, not this resonator change.
constexpr int   kResoAnchorN = 6;
// Cutoff-parameter anchors: knee (Freq 90) then the four measured surface
// cutoffs (Freq 96/108/116/122) and a full-open extrapolation (cparam 1.0).
constexpr float kResoAnchorCp[kResoAnchorN] = {
    90.0f/127.0f, 96.0f/127.0f, 108.0f/127.0f, 116.0f/127.0f, 122.0f/127.0f, 1.0f
};
// Per-band damping MULTIPLIER (k_eff = bandBaseDamping * mult), 1.0 at/below the
// knee. Bands 1/3/5/7 are fit directly to the measured prominence surface; the
// even bands are the mean of their neighbours; band 0 (no resonance) never
// reduces. The last column extrapolates the F116->F122 slope to full open.
constexpr float kResoMult[8][kResoAnchorN] = {
    { 1.0f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f },  // band 0
    { 1.0f, 0.963f, 0.909f, 0.872f, 0.810f, 0.758f },  // band 1  (Q byte 24)
    { 1.0f, 0.799f, 0.733f, 0.700f, 0.646f, 0.601f },  // band 2  (mean 1,3)
    { 1.0f, 0.635f, 0.556f, 0.528f, 0.482f, 0.444f },  // band 3  (Q byte 56)
    { 1.0f, 0.544f, 0.468f, 0.442f, 0.396f, 0.358f },  // band 4  (mean 3,5)
    { 1.0f, 0.453f, 0.379f, 0.356f, 0.310f, 0.272f },  // band 5  (Q byte 88)
    { 1.0f, 0.372f, 0.308f, 0.287f, 0.245f, 0.211f },  // band 6  (mean 5,7)
    { 1.0f, 0.291f, 0.236f, 0.217f, 0.180f, 0.149f },  // band 7  (Q byte 120)
};
// PER-BAND CORNER PULL-DOWN. With the damping fit above, the resonant peak of
// the tan-prewarped TPT lands a few percent HIGH of ReBirth's, more so for the
// lower bands (whose peak sits below fc): measured RB_corner/ours across the
// surface is ~0.89 at band 1 rising to ~0.97 at band 7, near-constant over Freq
// 96..122. This per-band factor multiplies fc to pull the peak onto ReBirth's,
// bringing the corner within ~2% across the surface. It ramps in from 1.0 at the
// knee to its full value by Freq 96 (kResoAnchorCp[0..1]), so Freq<=90 is
// byte-identical; band 0 (no resonance) never pulls. This shifts only the peak
// LOCATION, not its height, so it does not disturb the fitted prominence.
constexpr float kResoCorner[8] = {
    1.000f, 0.891f, 0.909f, 0.927f, 0.943f, 0.959f, 0.964f, 0.969f
};
// Floor for k under the re-derived law: band 7 near full open reaches Q ~ 55, so
// keep a little headroom below that. The linear TPT core is stable for any k>0;
// this only bounds how sharp the peak can get. The shipped law never falls below
// 0.1055, so lowering this floor leaves every shipped-path value untouched.
constexpr float kResoKMin = 0.010f;

// Level. Routing a section through the PCF costs nothing at all: with the unit
// switched off but the section still routed, the render is 0.00 dB from the
// unrouted reference, flat across every bin. So this gain is the filter's own.
// An open filter at minimum resonance is +4.33 dB, and that falls as resonance
// rises and then stops falling near -4.1 dB. Over Q bytes 0..64 the fall
// tracks the damping as about k^0.56, which is the shape of an input scaling
// meant to hold the resonant peak in bounds; from byte 80 up it plateaus while
// Q keeps climbing, in both fixture rounds and across four settings. The
// plateau is recorded as measured rather than explained.
[[maybe_unused]] constexpr float kOpenGainDb = 4.33f;   // recorded, not applied

// The measured passband gain at each resonance setting, at Freq 64. Over the
// first half this tracks the damping as about k^0.56, but from byte 80 up it
// plateaus near -4.1 dB while Q keeps climbing, in both fixture rounds and
// across four settings. A two-parameter fit to that shape was 1.2 dB out at
// resonance 96, so the table is interpolated directly instead.
constexpr int   kResGainN = 9;
constexpr float kResGainByte[kResGainN] = { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
constexpr float kResGainDb[kResGainN]   = { 4.33f, 3.23f, 1.93f, 0.38f, -1.65f,
                                            -4.04f, -4.05f, -4.15f, -4.15f };

// The bandpass tap gets a CONSTANT output gain, not one scaled by k. The old
// `k * kBpTrim` normalised the BP peak to unity at every resonance, which was
// fit only at Q 0 (k = 1.44) on the wrong premise that ReBirth's BP is a
// unity-gain band. It is not: the manual's own block diagram (p63) draws BP as
// a full resonant peak, and measurement across resonance (2026-09-06, drone
// static, Freq 64, Q 0..112, `renders/pcf-bp-2026-09-06`) shows ReBirth's BP
// peak GROWS with Q (+5 dB at Q 0 to +15 dB at Q 112), tracking its own lowpass
// resonant peak about 3 dB higher. Scaling by k = 1/Q cancelled exactly that
// growth, so at high resonance ours sat up to 19 dB below ReBirth. The raw TPT
// `v1` tap already grows its peak with Q correctly, so a single constant gain
// matches ReBirth's BP peak to about 0.5 dB across the whole range (worst ~0.6
// dB at Q 112). See PCF_FINDINGS 12.
constexpr float kBpTrim = 1.53f;       // constant BP output gain (was k*1.147)

class PCF final : public Effect {
public:
    void prepare(double sampleRate) override {
        sr_ = (sampleRate > 1000.0) ? sampleRate : kDefaultSampleRate;
        // ~5 ms one-pole for knob / damping de-zippering.
        knobSlew_ = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(sr_)));
        // One-pole attack glide (new law): the envelope level eases to the trigger
        // target over ~kAttackTau instead of jumping. See kAttackTau above.
        attackSlew_ = 1.0f - std::exp(-1.0f / (kAttackTau * static_cast<float>(sr_)));
        // The re-derived banded, cutoff-dependent resonator is the default.
        // REBORN_PCF_RESO=0 (or =old) selects the shipped continuous resonator
        // for A/B. Any other value (or unset) uses the new one.
        useNewReso_ = true;
        if (const char* e = std::getenv("REBORN_PCF_RESO")) {
            if (e[0] == '0' || e[0] == 'o' || e[0] == 'O') useNewReso_ = false;
        }
        // Depth-capped envelope->cutoff is the default. REBORN_PCF_ENVLAW=old (or =0)
        // selects the shipped additive+clamp law for A/B testing.
        useNewEnvLaw_ = true;
        if (const char* e = std::getenv("REBORN_PCF_ENVLAW")) {
            if (e[0] == '0' || e[0] == 'o' || e[0] == 'O') useNewEnvLaw_ = false;
        }
        reset();
    }

    void reset() override {
        ic1L_ = ic2L_ = ic1R_ = ic2R_ = 0.0f;
        env_ = 0.0f;
        envTgt_ = 0.0f;
        tick32_ = 0;
        started_ = false;
        cutSm_ = p_[C_CUT];
        envSm_ = p_[C_ENV];
        kSm_   = useNewReso_ ? bandBaseDamping(resoBand(p_[C_RES]))
                             : dampingFor(p_[C_RES]);
    }

    // Passband gain for a resonance setting, interpolated across the measured
    // table rather than fitted, because no simple curve reproduces both the
    // k^0.56 first half and the plateau after it.
    static float gainFor(float res) {
        const float b = clampf(res, 0.0f, 1.0f) * 127.0f;
        float db = kResGainDb[kResGainN - 1];
        for (int i = 1; i < kResGainN; ++i) {
            if (b <= kResGainByte[i]) {
                const float t = (b - kResGainByte[i - 1])
                              / (kResGainByte[i] - kResGainByte[i - 1]);
                db = kResGainDb[i - 1] + t * (kResGainDb[i] - kResGainDb[i - 1]);
                break;
            }
        }
        return std::pow(10.0f, db / 20.0f);
    }

    int numParams() const override { return kNumParams; }

    ParamInfo paramInfo(int i) const override {
        if (i < 0 || i >= kNumParams) return {};
        return { kParams[i].name, kParams[i].label, 0.0f, 1.0f, kParams[i].def, "" };
    }

    float getParam(int i) const override {
        return (i >= 0 && i < kNumParams) ? p_[i] : 0.0f;
    }

    void setParam(int i, float v) override {
        if (i >= 0 && i < kNumParams) p_[i] = clampf(v, 0.0f, 1.0f);
    }

    // The sequencer calls this once per SIXTEENTH, but PCF patterns are
    // specified in thirty-seconds and several of them trigger on both halves of
    // a sixteenth. So the unit keeps its own thirty-second clock, re-synced
    // here on every sixteenth so it can never drift from the sequencer, and
    // fires the in-between tick itself once half a step of samples has passed.
    void onStep(int stepIndex) override {
        tick32_ = stepIndex * 2;
        sinceTick_ = 0.0;
        // THE FIRST TICK OF A RENDER DOES NOT TRIGGER. Measured 2026-08-29 on
        // patterns 0, 1 and 2, whose every trigger is velocity 1.0: ReBirth's
        // step 0 reads 0.10 and every trigger after it reads 1.03. The envelope
        // fires on the transition INTO a step and the first step is entered
        // without one. Patterns 40, 49 and 51 show the same thing.
        if (!started_) { started_ = true; return; }
        fireTick(tick32_);
    }

    void setTempo(double bpm) override {
        tempo_ = (bpm > 1.0) ? bpm : 120.0;
    }

    void process(StereoBlock& io, int numFrames) override {
        int n = numFrames;
        const int cap = static_cast<int>(io.size());
        if (n > cap) n = cap;
        if (n <= 0) return;

        // Recover silently if some upstream module ever handed us garbage.
        sanitize(ic1L_); sanitize(ic2L_); sanitize(ic1R_); sanitize(ic2R_);
        sanitize(env_); sanitize(envTgt_); sanitize(cutSm_); sanitize(envSm_); sanitize(kSm_);

        const float sr       = static_cast<float>(sr_);
        const float nyqLimit = sr * 0.45f;      // keep tan() well-conditioned

        // --- Envelope decay, from the measured law ---------------------------
        const float dec = clampf(p_[C_DEC], 0.0f, 1.0f);
        const float tau = decayTau(dec);
        const float envDecay = std::exp(-1.0f / (tau * sr));

        // --- Resonance -> damping k = 1/Q (smoothed per sample below) --------
        // New model: the knob selects one of 8 quantised bands and the damping
        // is that band's BASE value, reduced per-sample as the cutoff opens
        // (resoMult below). Old model: the continuous shipped law.
        const int   resBand = resoBand(p_[C_RES]);
        const float kTarget = useNewReso_ ? bandBaseDamping(resBand)
                                          : dampingFor(p_[C_RES]);
        // Band 0 (and the whole old path) keeps the Q0-fitted top-bend; the
        // resonant bands drop it, because it double-counts with the resonance
        // corner shift and pushed the high-cutoff peak well above ReBirth's.
        const bool  keepBend = !useNewReso_ || (resBand == 0);

        const float base   = clampf(p_[C_CUT], 0.0f, 1.0f);
        const double samplesPer32 = (60.0 / tempo_ / 8.0) * sr_;
        const float envAmt = clampf(p_[C_ENV], 0.0f, 1.0f);

        for (int i = 0; i < n; ++i) {
            // De-zipper the knobs and the damping coefficient (~5 ms).
            cutSm_ += (base    - cutSm_) * knobSlew_;
            envSm_ += (envAmt  - envSm_) * knobSlew_;
            kSm_   += (kTarget - kSm_)   * knobSlew_;

            // Measured level law, not a guess: +4.33 dB with the filter open
            // at minimum resonance, falling as about k^0.56 and flooring near
            // -4.1 dB. This used to be `0.6 + 0.2 * k`, which had no
            // measurement behind it and was 4.4 dB light at the open end.
            const float gIn = gainFor(p_[C_RES]);

            // Advance the thirty-second clock and retrigger on its ticks. The
            // one landing on the sixteenth is fired by onStep, so only the
            // in-between tick is fired from here.
            sinceTick_ += 1.0;
            if (sinceTick_ >= samplesPer32) {
                sinceTick_ -= samplesPer32;
                fireTick(++tick32_);
            }
            // The envelope decays; the pattern retriggers it. New law: attack THEN
            // decay. On a trigger the TARGET is HELD at f(velocity) and the LEVEL
            // glides to it with the one-pole attack, so it REACHES the full peak
            // (the target does not decay while the level is still climbing). Once
            // the level has reached the held target (within kReachEps), the target
            // begins to decay under the measured law and the level tracks it down.
            // The held rise rounds the leading edge (the wave) without capping the
            // peak, which the earlier "decay the target from the trigger instant"
            // version did (it left dense full-velocity patterns 15-20% dark). The
            // glide smooths both edges, so a lower trigger still pulls the target
            // down smoothly. Old law: the level itself decays from the instant-jump
            // peak (shipped behaviour, byte-identical).
            if (useNewEnvLaw_) {
                if (attacking_) {
                    env_ = flushDenormal(env_ + (envTgt_ - env_) * attackSlew_);
                    if (std::fabs(envTgt_ - env_) <= kReachEps * envTgt_ + 1e-5f)
                        attacking_ = false;
                } else {
                    envTgt_ = flushDenormal(envTgt_ * envDecay);
                    env_    = flushDenormal(env_ + (envTgt_ - env_) * attackSlew_);
                }
            } else {
                env_ = flushDenormal(env_ * envDecay);
            }

            // Envelope -> cutoff. Depth-capped: the per-env-unit depth g is the min
            // of an additive cap gcap(amt) (binds at low base, so the reach does not
            // over-open) and a headroom line D*(1-base) (binds at high base, so the
            // sum stays below the ceiling and descends immediately). No hard clamp,
            // no plateau at any base. At the fixtures' base Freq 30 with Amt <= 90 the
            // cap binds and the min is a no-op, so the law is byte-identical to the
            // shipped additive one and the low/mid patterns do not move. See
            // kEnvHeadroom / gcapFor above. Shipped additive+clamp law behind
            // REBORN_PCF_ENVLAW=old for A/B.
            float cparam;
            if (useNewEnvLaw_) {
                const float head = kEnvHeadroom * (1.0f - cutSm_);
                const float gcap = gcapFor(envSm_);
                const float g    = (gcap < head) ? gcap : head;
                cparam = cutSm_ + g * env_;
            } else {
                cparam = clampf(cutSm_ + kEnvDepthOld * envSm_ * env_, 0.0f, 1.0f);
            }

            // Damping. The new model reduces the band's base damping as the LIVE
            // cutoff opens, so the resonant peak sharpens toward high fc to match
            // ReBirth's growing prominence; below the knee the multiplier is 1.0,
            // so every Freq<=90 setting is byte-identical to the shipped filter.
            const float kReduced = useNewReso_ ? (kSm_ * resoMult(resBand, cparam))
                                               : kSm_;
            const float k = clampf(kReduced, useNewReso_ ? kResoKMin : 0.05f, 2.0f);

            // The corner rides the resonance knob (see kResFc* above).
            const float rr = clampf(p_[C_RES], 0.0f, kResMaxByte);
            const float fcScale = 1.0f + rr * (kResFcB + rr * kResFcC);
            // The measured top-end bend is driven by the BASE Freq knob (cutSm_),
            // not the envelope-inflated cparam. Only the static Freq->cutoff law was
            // measured to bend up above ~4.4 kHz (broadband corners, Amt 0); there
            // is no evidence the envelope excursion shares that curvature, and
            // applying the bend to the swept peak instead measurably darkens the
            // high-Amt patterns (it pushes the over-opened resonant peak further
            // out of band). So the bend corrects the knob's static mapping and is
            // zero for any base Freq below the knee, which leaves every Freq<=~106
            // pattern and the resonant chirp byte-identical. See PCF_FINDINGS.
            float bendOct = 0.0f;
            if (keepBend && cutSm_ > kTopBendKnee) {
                const float d = cutSm_ - kTopBendKnee;
                bendOct = kTopBendGain * d * d;
            }
            float fc = kFreqLo * std::exp2(cparam * kLog2Span + bendOct) * fcScale;
            // Per-band corner pull-down (new resonator only); 1.0 below the knee
            // and for band 0, so low/mid stays byte-identical.
            if (useNewReso_) fc *= resoCorner(resBand, cparam);
            if (fc > nyqLimit)    fc = nyqLimit;
            if (fc < kMinCutoffHz) fc = kMinCutoffHz;

            // TPT (zero-delay-feedback) coefficients, shared by both channels.
            const float g  = std::tan(static_cast<float>(kPi) * fc / sr);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            // One filter, two taps. The bandpass tap gets a constant gain: its
            // resonant peak already grows with Q like ReBirth's (see kBpTrim).
            const bool  bp    = p_[C_MODE] >= 0.5f;
            const float bpAmp = kBpTrim;

            // --- Left core ---
            {
                const float x  = io.left[i] * gIn;
                const float v3 = x - ic2L_;
                const float v1 = a1 * ic1L_ + a2 * v3;          // bandpass
                const float v2 = ic2L_ + a2 * ic1L_ + a3 * v3;  // lowpass
                ic1L_ = flushDenormal(2.0f * v1 - ic1L_);
                ic2L_ = flushDenormal(2.0f * v2 - ic2L_);
                io.left[i] = flushDenormal(bp ? v1 * bpAmp : v2);
            }
            // --- Right core ---
            {
                const float x  = io.right[i] * gIn;
                const float v3 = x - ic2R_;
                const float v1 = a1 * ic1R_ + a2 * v3;
                const float v2 = ic2R_ + a2 * ic1R_ + a3 * v3;
                ic1R_ = flushDenormal(2.0f * v1 - ic1R_);
                ic2R_ = flushDenormal(2.0f * v2 - ic2R_);
                io.right[i] = flushDenormal(bp ? v1 * bpAmp : v2);
            }
        }
    }

private:
    // Resonance 0..1 -> damping k = 1/Q, from the measured law. At 0 this is
    // 1.4435, i.e. Q = 0.693, which is Butterworth. At 1 it is 0.1050, Q = 9.5,
    // and that end is extrapolated rather than measured. k stays strictly
    // positive so the linear core remains unconditionally stable, which is why
    // the resonance path no longer needs a saturator to hold it: the previous
    // `satState` soft-limiter had no measurement behind it, and by the rule of
    // 2026-08-26 an unmeasured nonlinearity is a bug until one exists.
    static float dampingFor(float res) {
        const float r = clampf(res, 0.0f, kResMaxByte);   // the knob saturates
        return kDampMin + kDampAmt * std::pow(1.0f - r, kDampExp);
    }

    // The 8 quantised resonance bands (PCF_FINDINGS 15): band = byte >> 4, byte
    // = round(res * 127). The transfer is identical within a band.
    static int resoBand(float res) {
        int byte = static_cast<int>(clampf(res, 0.0f, 1.0f) * 127.0f + 0.5f);
        int b = byte >> 4;
        if (b < 0) b = 0;
        if (b > 7) b = 7;
        return b;
    }

    // Per-band BASE damping: the shipped continuous law at the band centre
    // (byte b*16 + 8). Band centres 24/56/88/120 are exactly the surface Q
    // representatives, so the low/mid static is byte-identical to the shipped
    // filter at those settings; dampingFor's own saturation covers band 7.
    static float bandBaseDamping(int band) {
        return dampingFor((band * 16 + 8) / 127.0f);
    }

    // Cutoff-dependent damping MULTIPLIER for the re-derived resonator. 1.0 at
    // and below the knee (so every Freq<=90 cutoff is unchanged), then the
    // measured per-band reduction, interpolated linearly in cutoff-parameter and
    // clamped/extrapolated flat above the top anchor.
    static float resoMult(int band, float cparam) {
        const float* a = kResoMult[band];
        if (cparam <= kResoAnchorCp[0]) return a[0];
        if (cparam >= kResoAnchorCp[kResoAnchorN - 1]) return a[kResoAnchorN - 1];
        for (int i = 1; i < kResoAnchorN; ++i) {
            if (cparam <= kResoAnchorCp[i]) {
                const float t = (cparam - kResoAnchorCp[i - 1])
                              / (kResoAnchorCp[i] - kResoAnchorCp[i - 1]);
                return a[i - 1] + t * (a[i] - a[i - 1]);
            }
        }
        return a[kResoAnchorN - 1];
    }

    // Per-band corner pull-down, 1.0 at/below the knee, ramping to kResoCorner
    // by Freq 96 and held above. Multiplies fc to land the peak on ReBirth's.
    static float resoCorner(int band, float cparam) {
        const float f = kResoCorner[band];
        if (f >= 1.0f || cparam <= kResoAnchorCp[0]) return 1.0f;
        if (cparam >= kResoAnchorCp[1]) return f;
        const float t = (cparam - kResoAnchorCp[0])
                      / (kResoAnchorCp[1] - kResoAnchorCp[0]);
        return 1.0f + t * (f - 1.0f);
    }

    static void sanitize(float& v) {
        if (!std::isfinite(v)) v = 0.0f;
    }

    // Retrigger the envelope if this thirty-second tick carries a trigger.
    // ReBirth's patterns are presets and cannot be edited (p64), so the table
    // is built in rather than loaded from the song.
    void fireTick(int tick) {
        int idx = (int) std::lround(clampf(p_[C_PATTERN], 0.0f, 1.0f) * 54.0f);
        if (idx < 0) idx = 0;
        if (idx > 54) idx = 54;
        const PcfPreset& pat = kPcfPresets[idx];
        if (pat.length32 <= 0 || pat.count <= 0) return;
        int pos = tick % pat.length32;
        if (pos < 0) pos += pat.length32;
        for (int i = 0; i < pat.count; ++i)
            if ((int) pat.steps[i].step32 == pos) {
                // A retrigger REPLACES the target rather than adding to it: the
                // measured traces show every trigger reaching the same height
                // regardless of what the previous one left behind (and a lower
                // trigger pulls the target DOWN). The pattern's velocity sets the
                // target through the compressive curve (new law), and the LEVEL
                // glides to it over the attack (process); full velocity is unmoved.
                // Old law: the level jumps instantly, as shipped.
                const float v = clampf(pat.steps[i].velocity, 0.0f, 2.0f);
                if (useNewEnvLaw_) {
                    envTgt_    = velocityCurve(v);
                    // Re-arm the attack on every trigger, so the target HOLDS
                    // (does not decay) until the level reaches it (ADDENDUM 3).
                    // This also smooths a pull-down: a lower-velocity trigger's
                    // lower target is glided to, not jumped to.
                    attacking_ = true;
                } else {
                    env_ = v;
                }
                return;
            }
    }

    double sr_ = kDefaultSampleRate;
    double tempo_ = 120.0;
    int    tick32_ = 0;
    bool   started_ = false;   // the first step of a render does not trigger
    double sinceTick_ = 0.0;
    std::array<float, kNumParams> p_ = { 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f };

    float ic1L_ = 0.0f, ic2L_ = 0.0f;   // left  SVF integrator states
    float ic1R_ = 0.0f, ic2R_ = 0.0f;   // right SVF integrator states

    float env_      = 0.0f;   // AD envelope LEVEL (used for cutoff); glides to envTgt_
    float envTgt_   = 0.0f;   // AD envelope TARGET, set by the trigger, decays (new law)
    bool  attacking_ = false; // true while env_ is climbing to the HELD envTgt_ (new law, ADDENDUM 3)
    float cutSm_    = 0.5f;   // de-zippered cutoff knob
    float envSm_    = 0.0f;   // de-zippered env-mod knob
    float kSm_      = 1.4435f; // de-zippered damping coefficient (Q = 0.693)
    float knobSlew_ = 0.005f; // ~5 ms one-pole coefficient (set in prepare)
    float attackSlew_ = 0.02f; // one-pole attack-glide coefficient (set in prepare)
    bool  useNewReso_ = true;  // re-derived banded resonator (REBORN_PCF_RESO)
    bool  useNewEnvLaw_ = true; // depth-capped env->cutoff (REBORN_PCF_ENVLAW)
};

} // namespace

std::unique_ptr<Effect> makePCF() { return std::make_unique<PCF>(); }

} // namespace reborn
