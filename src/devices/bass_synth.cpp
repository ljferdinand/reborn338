// SPDX-License-Identifier: AGPL-3.0-only AND MIT
// Copyright (C) 2026 Luke Ferdinand
// Parts adapted from Open303, Copyright (c) 2009 Robin Schmidt, under the MIT
// licence in third_party/open303/LICENSE.txt (see the attribution below).
//
// Clean-room TB-303-style monophonic acid-bass synthesizer.
//
// Modelled entirely from the publicly documented *analog circuit behaviour* of
// the classic silver box. No code, wavetables, or ROM/sample data from any
// existing product is used here. Everything is synthesized from standard DSP.
//
// Signal chain (authentic order):  VCO -> VCF -> VCA
//
//   * VCO   : single oscillator, band-limited saw or square (polyBLEP).
//   * VCF   : 4-pole diode-ladder-voiced resonant lowpass. A saturating,
//             unit-delayed resonance feedback path lets it self-oscillate at
//             high resonance with a bounded (limit-cycle) amplitude (the
//             squelchy acid whistle) while staying NaN/Inf/denormal-safe
//             across the whole parameter range.
//   * VCA   : gate-shaped amplifier (post-filter), boosted by accent.
//
//   * Main envelope : fast attack, exponential decay set by the decay knob.
//                     Drives the filter cutoff via env-mod (the tonal "pluck").
//   * Accent circuit: a SEPARATE, faster envelope that both boosts the VCA
//                     level AND adds an extra filter sweep. Its sweep depth and
//                     the resonance "bite" scale with the resonance control, so
//                     accented notes on a resonant patch really pump/squelch.
//   * Slide         : exponential pitch glide, 18 ms time constant (measured,
//                     see GLIDE_SEC), with NO envelope retrigger (legato / tied
//                     note).
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cmath>
#include <array>
#include <string>
#include <algorithm>
#include <cstdlib>   // TEMP: env-var probes for A (fundamental HP) and B (HF shelf)

namespace reborn {
namespace {

// polyBLEP anti-aliasing residual for a discontinuity at phase t (dt = phase inc).
inline double polyBlep(double t, double dt) {
    if (t < dt)            { double x = t / dt;         return x + x - x * x - 1.0; }
    else if (t > 1.0 - dt) { double x = (t - 1.0) / dt; return x * x + x + x + 1.0; }
    return 0.0;
}

// Bounded hyperbolic-tangent-ish saturator (soft, symmetric, never overflows).
inline double satTanh(double x) { return std::tanh(x); }

// ASYMMETRIC saturator for the VCO drive stage.
//
// tanh is an ODD function, so it cannot create even harmonics, and by
// flattening a sawtooth toward a square it destroys the even harmonics the
// oscillator already had. Measured: the default drive knob cost 6 dB of h2 and
// steepened the harmonic ladder from -6.1 to -7.0 dB/octave, away from the
// reference's -4.8. See docs/research/DESIGN_MEMO_303_HARMONICS.md.
//
// Biasing the operating point off zero (as a real single-ended transistor stage
// sits) makes the curve asymmetric, so it generates even-order content instead
// of cancelling it. The trailing subtraction removes the DC the bias introduces,
// which would otherwise thump the filter and offset the whole voice.
inline double satAsym(double x, double bias) {
    return std::tanh(x + bias) - std::tanh(bias);
}

struct Param { const char* name; const char* label; float mn, mx, def; const char* unit; };

// NOTE: parameter NAMES and their ORDER are part of the public contract used by
// the song loader; keep them stable. New parameters may only be appended.
const std::array<Param, 9> kParams = {{
    {"tuning",    "Tuning",    -12.f, 12.f, 0.0f,  "st"},
    {"waveform",  "Waveform",   0.f,  1.f,  0.0f,  ""  },  // 0 = saw, 1 = square
    {"cutoff",    "Cutoff",     0.f,  1.f,  0.35f, ""  },
    {"resonance", "Resonance",  0.f,  1.f,  0.55f, ""  },
    {"envmod",    "Env Mod",    0.f,  1.f,  0.55f, ""  },
    {"decay",     "Decay",      0.f,  1.f,  0.55f, ""  },
    {"accent",    "Accent",     0.f,  1.f,  0.50f, ""  },
    {"volume",    "Volume",     0.f,  1.f,  0.85f, ""  },
    {"drive",     "Drive",      0.f,  1.f,  0.20f, ""  },
}};

enum { P_TUNING, P_WAVE, P_CUTOFF, P_RESO, P_ENVMOD, P_DECAY, P_ACCENT, P_VOLUME, P_DRIVE };

class BassSynth303 : public Instrument {
public:
    BassSynth303() { for (int i = 0; i < (int)kParams.size(); ++i) p_[i] = kParams[i].def; }

    void prepare(double sr) override { sr_ = (sr > 1000.0 ? sr : kDefaultSampleRate); reset(); }

    void reset() override {
        phase_ = 0.0;
        gate_ = false;
        accentLatched_ = false;
        freqCur_ = freqTarget_ = 55.0;
        // envelopes
        mainEnv_ = 0.0; mainAttacking_ = false;
        accEnv_  = 0.0; accAttacking_  = false; accLevelScale_ = 1.0;
        ampEnv_  = 0.0; ampPlucking_ = false;
        // filter state
        z1_ = z2_ = z3_ = z4_ = 0.0;
        fbPrev_ = 0.0;
        dcLp_ = 0.0;
        hpLp_ = 0.0;
        shelfLp_ = 0.0;
        fcPrevV_ = 0.0;
        velLp_ = 0.0;
        fbHpLp_ = 0.0;
        dcap1_ = dcap2_ = dcap3_ = dcap4_ = 0.0;
        lfHpLp_ = 0.0; oscHpLp_ = 0.0; oscHsLp_ = 0.0;
        dFbHp_ = 0.0;
        gateLeft_ = 0.0;
        // The plucky VCA is OFF by default as of 2026-07-27. It decayed with a
        // 60 ms time constant toward a 0.08 floor for as long as the gate was
        // held, and ReBirth's VCA does not decay at all: it holds flat for the
        // gate and then releases. See the fit in setStepTiming() below.
        //
        // It was not a bad guess, it was fitted on the wrong kind of evidence:
        // two summary statistics of a whole musical patch, envelope
        // coefficient-of-variation 0.83 against 0.79 and silence fraction 0.32
        // against 0.33. Two scalars cannot tell a decay from a hold-then-release,
        // and both shapes can hit them. Slicing one note per 5 ms separates them
        // immediately. Kept as probe overrides; 0 disables.
        //   REBORN_AMP_DECAY - VCA decay time in seconds while held.
        //   REBORN_AMP_SUS   - VCA sustain floor the decay falls toward.
        { const char* e = std::getenv("REBORN_AMP_DECAY"); ampDecSec_ = e ? std::atof(e) : 0.0;  }
        { const char* e = std::getenv("REBORN_AMP_SUS");   ampSus_    = e ? std::atof(e) : 0.08; }
    }

    int numParams() const override { return (int)kParams.size(); }
    ParamInfo paramInfo(int i) const override {
        if (i < 0 || i >= (int)kParams.size())
            return { "unused", "Unused", 0.0f, 1.0f, 0.0f, "" };
        const Param& q = kParams[i];
        return { q.name, q.label, q.mn, q.mx, q.def, q.unit };
    }
    float getParam(int i) const override {
        return (i >= 0 && i < (int)kParams.size()) ? p_[i] : 0.0f;
    }
    void setParam(int i, float v) override {
        if (i >= 0 && i < (int)kParams.size())
            p_[i] = clampf(v, kParams[i].mn, kParams[i].mx);
    }

    void setStepNote(int note) override {
        // Only the glide TARGET moves. freqCur_ is left alone so a struck note
        // still jumps (trigger() does that) and a tied or rested one glides.
        double f = noteToHz(double(note) + (double)p_[P_TUNING]);
        if (!(f > 1.0))  f = 1.0;
        if (f > 20000.0) f = 20000.0;
        freqTarget_ = f;
    }

    void trigger(int note, float /*vel*/, bool accent, bool slide) override {
        double f = noteToHz(double(note) + (double)p_[P_TUNING]);
        if (!(f > 1.0))    f = 1.0;      // also catches NaN
        if (f > 20000.0)   f = 20000.0;  // pathological note values must stay finite,
                                         // or the glide's pow(inf/inf, c) turns NaN
        freqTarget_ = f;

        if (!slide) {
            // Struck note: pitch jumps instantly and the main (filter) envelope
            // retriggers from zero with a fast attack.
            freqCur_       = freqTarget_;
            mainEnv_       = 0.0;
            mainAttacking_ = true;
            accentLatched_ = accent;          // sustained accent VCA lift for this note
            // Re-pluck the VCA, but as a fast RAMP not a step. Snapping ampEnv_
            // straight to 1.0 here was a discontinuity in an amplitude envelope,
            // i.e. a broadband click on every struck note. Measured: it put a
            // flat ~-38 dB shelf across the whole spectrum, 19-34 dB above
            // ReBirth, and it survived every gain change because it is not a
            // gain. See docs/research/DESIGN_MEMO_303_HARMONICS.md round 6.
            if (ampDecSec_ > 0.0) ampPlucking_ = true;
        } else {
            // Tied / slide note: glide toward the new pitch, DO NOT retrigger the
            // main envelope. An accent may still (re)fire on a tied note.
            if (accent) accentLatched_ = true;
            // ACCENT LATCH EXPIRY (probe, default off). accentLatched_ was never
            // cleared on this path when the glide's OWN step carries no accent,
            // so it stays true across an arbitrarily long run of ties/holds once
            // set by one accented step, and the sustained VCA lift (ACC_SUS) and
            // the shortened filter decay (mainDecCAcc) both key off it. task 75:
            // on Clodhopper 303-2, every glide-in Strike that follows an accented
            // Hold shows 4.5 to 8.1 dB more onset energy than ReBirth even with
            // ZERO pitch change, which is what an unbounded sustain leak predicts
            // and a pitch-glide artefact does not. Unmeasured whether ReBirth's
            // accent actually expires this way; this only tests the hypothesis.
            else if (ACC_LATCH_EXPIRE()) accentLatched_ = false;
        }

        // A STRUCK note fires the accent to full; a tied/slid one splits it, see
        // fireAccentTie(). See fireAccent() for why this is a depth and not a gate.
        if (accent) { if (!slide) fireAccent(1.0, 1.0); else fireAccentTie(); }
        gate_ = true;
        // Arm the gate countdown. Zero means "hold until release()", which is
        // what a host driving the voice by MIDI wants; the sequencer always sets
        // it through setStepTiming().
        if (gateSec_ > 0.0) gateLeft_ = gateSec_;
    }

    void release() override { gate_ = false; gateLeft_ = 0.0; }

    // Fire the accent envelope and nothing else: no gate, no retrigger, no
    // retune. For an accent flag on a step that sounds no note of its own while
    // one is still ringing through it. Full depth deliberately: measured
    // 2026-08-02, ReBirth's accented plain rest, accented tied rest and
    // accented retrigger all land within 0.31 dB of each other, so when its
    // accent fires it reaches the same place however the step is articulated.
    void accentOnly() override { accentLatched_ = true; fireAccent(1.0); }

    // ReBirth's 303 VCA gates for HALF a step and then releases. Measured on the
    // batch G wave fixtures at resonance 20 (a resonant filter goes on ringing
    // after the gate closes, and that ring lands in exactly the window the
    // release has to be fitted from), sliced at 1.25 ms:
    //
    //   the envelope is flat to within +/-1 dB out to 62 ms and starts falling
    //   at 63 to 65 ms. A step at 120 BPM is 125 ms, so the gate is 0.500 of it.
    //   With the gate pinned there the release fits tau = 21.92 ms across all
    //   six fixtures, spread 0.40 ms, residual ~1.1 dB which is the slice-to-
    //   slice ripple of peak-detecting a 110 Hz wave in 2.5 ms windows.
    //
    // OPEN: every batch G fixture is at 120 BPM, so whether the gate is half a
    // STEP or a fixed ~62 ms cannot be told from the data we have. Proportional
    // is implemented because that is what a step-clocked sequencer does and what
    // the hardware does. A three-tempo fixture settles it; see
    // docs/COMPONENT_MATRIX.md section 1.
    void setStepTiming(double stepSeconds, bool tie) override {
        const double GATE_FRAC = []{ const char* e=std::getenv("REBORN_GATE_FRAC");
                                     return e?std::atof(e):0.500; }();
        // A tied step must hold past its own end so the next trigger picks the
        // gate up unbroken; anything >= one step does that, since the next step
        // either retriggers or calls release().
        gateSec_ = tie ? stepSeconds * 1.5 : stepSeconds * GATE_FRAC;
    }

    // See the comment at the rest branch of fireBass(). Only meaningful while a
    // note is already sounding; a tie into silence stays silent.
    void holdStep(double stepSeconds, bool tie, bool accent) override {
        if (!gate_) return;
        setStepTiming(stepSeconds, tie);
        gateLeft_ = gateSec_;
        // THE ACCENT ROW IS READ ON A REST. Measured on Clodhopper 303a: of its
        // three tied-into rests, the two carrying the accent flag swell 6.4 and
        // 5.0 dB in ReBirth and the one without it is flat, while a rest that
        // carries accent with nothing sounding through it does nothing at all.
        // Three positives and two negatives, no exceptions. Same handling as an
        // accent on a tied NOTE: latch it and refire the accent envelope, but
        // leave pitch and the main envelope alone.
        if (accent) { accentLatched_ = true; fireAccentTie(); }
        else if (ACC_LATCH_EXPIRE()) accentLatched_ = false;
    }

    // task 75 probe: default false reproduces the shipped behaviour, where
    // accentLatched_ is a one-way latch that only a plain unaccented STRUCK
    // note clears. See the call sites in trigger() and holdStep().
    static bool ACC_LATCH_EXPIRE() {
        static const bool v = []{ const char* e = std::getenv("REBORN_ACC_LATCH_EXPIRE");
                                  return e && std::atof(e) != 0.0; }();
        return v;
    }

    // Start an accent fire toward `target`, but never CUT one already louder.
    // A small tie-accent arriving while a big struck one is still decaying must
    // leave it alone, not drag it down to the smaller ceiling.
    void fireAccent(double target, double levelScale = 1.0) {
        if (accEnv_ < target) { accTarget_ = target; accAttacking_ = true;
                                accLevelScale_ = levelScale; }
    }

    // Accent on a step that does NOT retrigger (a slid note or a tied-into rest).
    // task 83, found by Luke's ear 2026-08-06: ReBirth's resonance is spread
    // across the bar because its accent fires a bright CORNER chirp on every
    // accented rest, while ours collapsed to one loud accent because ACC_TIE 0.05
    // suppressed the corner along with the level. accEnv_ drives BOTH the corner
    // (accOct) and the VCA level (accentLift), so the split fires accEnv_ to the
    // strong CORNER target and scales the level term back down by
    // ACC_TIE_TARGET/corner, keeping the +2.8 dB level ACC_TIE was fitted on while
    // the corner rings. Default off (REBORN_ACC_CORNER_SPLIT) reproduces the
    // pre-split fireAccent(ACC_TIE_TARGET()) byte-for-byte.
    void fireAccentTie() {
        if (ACC_CORNER_SPLIT()) {
            const double c = ACC_CORNER_TIE();
            fireAccent(c, (c > 1.0e-6) ? ACC_TIE_TARGET() / c : 0.0);
        } else {
            fireAccent(ACC_TIE_TARGET(), 1.0);
        }
    }
    static bool ACC_CORNER_SPLIT() {
        static const bool v = []{ const char* e=std::getenv("REBORN_ACC_CORNER_SPLIT");
                                  return e && std::atof(e) != 0.0; }();
        return v;
    }
    static double ACC_CORNER_TIE() {
        static const double v = []{ const char* e=std::getenv("REBORN_ACC_CORNER_TIE");
                                    return e?std::atof(e):0.90; }();
        return v;
    }

    // Accent depth on a step that does NOT retrigger: a slid note, or a rest
    // tied into. Default 1.0 reproduces the behaviour before 2026-08-01.
    //
    // Measured on fixtures-2026-08-01/accentprobe, high band 1500-4000 Hz:
    // ReBirth gives +22.4 dB on a real retrigger and +2.8 dB on ANY
    // non-retriggering step, rest or slid note alike, the two agreeing to the
    // decimal. We gave +10.9 and +10.2, barely distinguishing them, where
    // ReBirth separates the cases by 19.6 dB.
    //
    // Note this is a DEPTH, not a suppression. The earlier hypothesis was that
    // we re-fire where ReBirth does not; that is wrong in kind, ReBirth does
    // respond on a tied step by about an eighth as much. Removing the re-arm
    // would have re-broken the rest-accent behaviour, which was itself
    // established by measurement after Luke caught it by ear.
    // APPLIED 2026-08-01 at 0.05, on measurement plus Luke's ear.
    //
    // Measured: ReBirth gives +2.8 dB on a non-retriggering step and +22.4 on a
    // real retrigger. 0.05 reproduces BOTH non-retriggering cases exactly, the
    // slid step and the rest, at +2.8 and +2.8. Neutral on batch G, which does
    // contain non-retriggering accented steps, so that is a real cross-check:
    // centroid-ratio rms 0.215 -> 0.222 and level-lift rms 0.55 -> 0.51 dB.
    //
    // Heard: on Clodhopper's steps 13-16, a tied accented run, the within-step
    // dip goes 4.01 dB to 2.21 against ReBirth's 2.69, and the decay across the
    // run 5.2 dB to 7.2 against ReBirth's 7.5. Luke had reported the run
    // reading as separate sixteenths rather than one contiguous note.
    static double ACC_TIE_TARGET() {
        static const double v = []{ const char* e=std::getenv("REBORN_ACC_TIE");
                                    return e?std::atof(e):0.05; }();
        return v;
    }

    void process(StereoBlock& out, int n) override {
        const double sr    = sr_;
        const bool   square = (p_[P_WAVE] >= 0.5f);
        const double reso   = clampf(p_[P_RESO],   0.0f, 1.0f);
        const double envmod = clampf(p_[P_ENVMOD], 0.0f, 1.0f);
        const double accKnob= clampf(p_[P_ACCENT], 0.0f, 1.0f);
        const double vol    = clampf(p_[P_VOLUME], 0.0f, 1.0f);
        const double driveK = clampf(p_[P_DRIVE],  0.0f, 1.0f);

        // Base cutoff from the knob. EXPONENTIAL (log-frequency) law tuned
        // against clean (compressor-off) ReBirth cutoff-sweep exports: ~554 Hz
        // to ~5.1 kHz, matching ReBirth's cutoff-knob-to-85%-rolloff curve to
        // within ~3% across the knob (reso 120, note A). Lowered from ~620 Hz
        // when the resonance rework below made reso 120 correctly more resonant
        // (a stronger resonant peak piles energy near the corner, so less cutoff
        // frequency is needed to hit the same rolloff). The earlier linear
        // 300-2400 span under-opened badly at the top. Envelopes add octaves on
        // top, clamped below Nyquist.
        // TEMP probes: REBORN_CUT_SCALE multiplies the base cutoff, REBORN_EM_OCT
        // sets the env-mod octave depth (default 3.0), for fitting against the
        // batch-D env-mod/decay fixtures.
        //
        // 1.0 -> 1.05, part of the 2026-07-31 refit. That refit moved six
        // constants together (CUT_SCALE, CUT_SLOPE, RESO_OCT, RESO_OCT_CUT,
        // K_B, HFQ_ON) and each of them carries the same note: they were fitted
        // JOINTLY and no one of them means anything on its own. Do not revert a
        // single value here without rerunning tools/analyze/curve_fit.py.
        //
        // Why the refit happened at all: the instrument doing the previous fits
        // was miscounting. find_f0() snapped the fundamental to the nearest FFT
        // bin, harmonics() then searched a FIXED +/-4 bins around each predicted
        // harmonic, and since the f0 error multiplies by harmonic number while
        // the window does not, from about k = 15 the window had walked off the
        // harmonic entirely. Every slope above that read too steep on both
        // sides. See docs/research/FINDINGS_AUDIT.md for 2026-07-31 and skirt_calib.py
        // for the calibration that caught it.
        //
        // 1.05 -> 0.78, 2026-08-02. THIS IS NOT A REFIT AND THE CORNER HAS NOT
        // MOVED. When the env mod swing became symmetric (EM_CENTRE at the use
        // site) this constant stopped meaning "the corner with the envelope
        // closed" and started meaning "the CENTRE the envelope swings about",
        // which sits half the env-mod-0 depth lower. That is 2^(-EM_FLOOR/2) =
        // 2^-0.425, and 1.05 x 2^-0.425 = 0.7815. The value scanned out of a
        // 140-cell grid search independently at 0.78, agreeing with the
        // bookkeeping to one percent, so no degree of freedom was spent here.
        //
        // The same reasoning is why CUT_SLOPE below did NOT move: re-anchoring
        // shifts the whole curve and cannot tilt it.
        //
        // CORRECTED 2026-08-05. An earlier version of this comment said ReBirth
        // measures 3.09 octaves per knob against our 2.90 and called that an
        // unresolved 6 percent shortfall. It is not a shortfall and there is
        // nothing to resolve: OUR SLOPE IS NOT A CONSTANT. RESO_OCT_CUT adds
        // slope with resonance, so the effective slope is
        // CUT_SLOPE + RESO_OCT_CUT * reso, which at resonance 60 is
        // 2.90 + 0.35 * (60/127) = 3.065. ReBirth's 3.09 was measured on the
        // envfloor grid, which is resonance 60 throughout. The two agree to
        // 0.025 octaves per knob. The old line compared a resonance-60
        // measurement against a resonance-0 constant.
        //
        // That also explains the puzzle it left behind. Setting CUT_SLOPE to
        // 3.09 scored worse at every scale tried because it would put the
        // effective slope at resonance 60 at 3.255, overshooting by 0.165.
        const double CUT_SCALE = []{ const char* e=std::getenv("REBORN_CUT_SCALE"); return e?std::atof(e):0.78; }();
        // Env-mod sweep depth in octaves, RE-FIT 2026-07-24 against the batch-D
        // env-mod sweep: ReBirth's per-note centroid runs 353/468/613/790 Hz at
        // env-mod 0/40/80/127, i.e. the whole knob spans only ~1.16 octaves.
        // The old 3.0 (tuned against the confounded full-mix references) more
        // than doubled that and overshot to 1142 Hz at env-mod 80. At 1.3 the
        // progression reads 354/461/590/836. RE-FIT to 1.6 after the cutoff
        // slope came down to 2.2 (a lower base cutoff means the sweep starts
        // lower, so it needs more depth): now 357/412/467/808.
        // RE-FIT again 2026-07-27, 1.6 -> 2.2, against batch I2's nine-point env
        // mod sweep crossed with three decays. Error over all 27 cells fell from
        // 0.2215 to 0.0384 octaves rms. See the EM_CURVE note at the use site for
        // why no curvature term ships with it.
        //
        // 2.2 -> 4.45, APPLIED 2026-08-02 on sign-off, together with EM_FLOOR and
        // EM_CENTRE at the use site. All three belong to one law and none of them
        // means anything alone. The 2.2 above is not wrong for what it was fitted
        // on: it was scored on the peak POSITION at a moment, and a knob that
        // opens the filter by 2.2 octaves can land the corner in the right place
        // at one instant while travelling less than half as far as ReBirth's over
        // a held note. What was missing was never a gain, it was the downward
        // half of the swing. See the EM_CENTRE block at the use site.
        const double EM_OCT    = []{ const char* e=std::getenv("REBORN_EM_OCT");    return e?std::atof(e):4.45; }();
        // Cutoff knob span, RE-FIT 2026-07-24 against the clean 5-point cutoff
        // sweep (reso 127, env-mod 0) plus the static reso-90 reference. The
        // old 3.2-octave span was too STEEP: we ran bright everywhere and the
        // error GREW with the knob (centroid ratio 1.23 at cutoff 20 rising to
        // 1.72 at 95), climbing 3.33 octaves per unit knob where ReBirth climbs
        // 2.50. At 2.2 the sweep reads 325/454/551/879/1181 Hz against
        // ReBirth's 329/459/659/917/1244, and the static reference holds
        // (349 vs 353). ~554 Hz .. ~2.5 kHz before the envelopes add octaves.
        // 2.5 -> 2.9 in the joint refit of 2026-07-31. See CUT_SCALE above.
        const double CUT_SLOPE = []{ const char* e=std::getenv("REBORN_CUT_SLOPE"); return e?std::atof(e):2.9; }();
        // 554 -> 582 Hz, refitted 2026-07-27 JOINTLY with the resonance feedback
        // against the batch H cutoff x resonance grid. The old value was
        // calibrated at resonance 120 alone, so it had absorbed part of the
        // resonance error; correcting the feedback without moving it would only
        // have relocated the error.
        // 582 -> 407 Hz, 2026-07-27, the third refit and the first one with the
        // right mechanism in place. Inverting ReBirth's batch H grid through our
        // own engine says its corner at ZERO resonance is far lower than ours:
        // 1938 Hz against our 3126 at cutoff 127, 662 against 1323 at cutoff 64.
        // The base corner was carrying error that belonged to the missing
        // resonance-to-corner coupling (see RESO_OCT at the use site), which is
        // exactly the trap the previous note here warned about and did not escape.
        const double baseCut = CUT_SCALE * 407.0 * std::exp2(CUT_SLOPE * clampf(p_[P_CUTOFF], 0.0f, 1.0f));

        // Input drive into the filter (pre-saturation gain). Gentle -> gnarly.
        // Scaled to 0.6x the original law and paired with an ASYMMETRIC
        // saturator (see satAsym): fitted 2026-07-26 over a bias x drive grid
        // against ReBirth's detroit_303 export. The old symmetric tanh at this
        // level was destroying the sawtooth's even harmonics: mean harmonic
        // ladder error 5.83 dB, slope -6.47 against the reference's -4.81.
        // This lands at 2.68 dB error, slope -5.68, and a HIGHER crest factor
        // (4.82 vs 4.14), so it wins on richness and peakiness together and
        // gives up only 1.3 dB of inter-harmonic floor.
        const double driveGain = 0.78 + 2.4 * driveK; // hot osc: harmonics near fc must
                                                      // dominate the resonant ring so the
                                                      // formant QUANTIZES to the note (the
                                                      // vocoder-buzz mechanism)

        // Resonance. Two quantities:
        //   kDet  - a stable reference used ONLY for the cutoff-detune below, so
        //           the calibrated cutoff mapping does not move when the feedback
        //           law is retuned. It reproduces the earlier capped value.
        //   kBase - the actual resonance feedback. It now RISES across the whole
        //           knob (dynamics rework, WIP): the earlier min(2.45, ...) cap
        //           froze every setting above knob ~51 to one value, so on a
        //           static note the resonance control did nothing over its top
        //           half. The self-oscillation a rising feedback would otherwise
        //           cause on env-swept notes (cutoff pinned at fcMax) is tamed by
        //           a high-frequency Q loss applied per-sample below, not by
        //           capping the knob. Onset TUNED against the clean ReBirth
        //           static resonance sweeps (cutoff 90/120, note A): peak
        //           prominence matches to ~1.7 dB, peak positions to within ~6%,
        //           and the env-swept sweep to ~1.1 dB. Residual: at high cutoff
        //           AND high resonance ReBirth's resonance is more spectrally
        //           concentrated (steeper tail, thinner low end); Reborn runs a
        //           touch brighter in the tail but heavier in the fundamental, a
        //           filter-shape difference the feedback constants cannot close.
        const double kDet  = std::min(2.45, 2.05 + 1.0 * reso);
        // Resonance feedback, RE-FIT 2026-07-24 against the batch-D static
        // reference (env-mod 0, cutoff 40, reso 90). Raising the resonance,
        // NOT the cutoff, is what fixes our passband balance: the peak near
        // the corner pulls energy off the fundamental. At K_A/K_B below, the
        // 500-1000 Hz band lands exactly on ReBirth (-5.6 dB) and the centroid
        // matches (354 vs 353); raising the CUTOFF instead matched the centroid
        // but destroyed the rolloff shape (dumped 2 kHz energy ReBirth lacks).
        //
        // REFIT 2026-07-27 against the batch H cutoff x resonance GRID (36
        // cells, env mod 0, held note, so the filter is static). The previous
        // K_A/K_B were fitted from 1-D slices, which cannot separate a wrong
        // cutoff law from a wrong resonance law: a resonance error necessarily
        // reads as a cutoff error in whichever slice you view it through.
        //
        // The grid showed two defects. FLOOR: K_A = 2.8 is 70% of the way to
        // self-oscillation on a 4-pole ladder, so we resonated even at knob 0,
        // where ReBirth is essentially a plain lowpass (peak prominence -43.8 to
        // -24.3 dB against our 9 to 24 dB more). RANGE: 2.8 to 3.9 is a 1.4x
        // span in feedback, while ReBirth's 85% rolloff climbs 4.4x across the
        // knob. Our rolloff ratio ran 2.09 at resonance 0 down to 0.79 at 127.
        //
        // Fitted on 15 cells and validated on all 36, so the fit cannot flatter
        // itself. Peak prominence error fell from 10.10 to 3.59 dB rms and
        // rolloff from 0.572 to 0.409 octaves rms, with the resonance-0 ratio
        // coming from 2.09 to 1.13.
        //
        // ROUND 2, 2026-07-27. The refit above fixed the floor but left a
        // cutoff-dependent residual, and the grid says exactly what it is:
        // ReBirth's resonance opens the filter PROGRESSIVELY MORE at higher
        // cutoffs, while ours opened it by a near-constant amount.
        //
        //   rolloff at reso 127 / reso 0      cut 16   cut 64   cut 110  cut 127
        //     ReBirth                          1.92     2.99     3.96     4.45
        //     ours, constant-k                 1.95     2.01     2.16     2.33
        //
        // Peak prominence said the same: 6.9 dB too STRONG at cutoff 16 and up
        // to 4.6 dB too WEAK at cutoff 127. So the feedback needs a cutoff term.
        // K_CUT is that term; the ladder self-oscillates near k = 4, which is
        // what bounds K_B + K_CUT and is presumably why a constant k was chosen
        // originally: no single value can be right at both ends.
        const double K_A   = []{ const char* e=std::getenv("REBORN_K_A");   return e?std::atof(e):0.0; }();
        // REFIT AGAIN 2026-07-27, jointly with the base corner and the new
        // resonance-to-corner coupling. K_B 3.0 -> 3.70 and K_CUT 0.6 -> 0.0.
        //
        // K_CUT is REMOVED rather than retuned. It was added in the previous
        // round to buy the cutoff-dependent part of the span and the matrix
        // recorded plainly that it was not the mechanism, only "a small term that
        // survived a fit". With the real mechanism in place it earns nothing, so
        // it goes. A parameter that only ever absorbed someone else's error is
        // worse than no parameter.
        //
        // 3.70 and not the marginally better 3.85: a 4-pole ladder self-
        // oscillates at k = 4.0, the two score within 0.005 of each other on the
        // grid, and 3.85 leaves 0.15 of headroom to an audible squeal on patches
        // the grid does not cover. Deliberate margin, not a fit.
        // 3.70 -> 3.40 in the joint refit of 2026-07-31. See CUT_SCALE above.
        // The self-oscillation headroom argument made just above still holds and
        // is improved by the move: 3.40 sits 0.60 below the k = 4.0 threshold
        // rather than 0.30.
        const double K_B   = []{ const char* e=std::getenv("REBORN_K_B");   return e?std::atof(e):3.40; }();
        // K_B RESO-DEPENDENT (task 85). Flat at the shipped 3.40 up to K_B_PIVOT,
        // then rising K_B_SLOPE per unit reso above it, so the resonance opens at
        // high reso (where ours ran short of ReBirth) without over-resonating at mid
        // reso (where 3.40 already matched, and a flat bump to 3.60 regressed
        // accentdepth/batchQ). Stage-0 measurement, batchN reso sweep vs ReBirth:
        // matched by K_B ~3.40 at reso 60, ~3.55 at 90, ~3.65 at 127. Cutoff
        // dependence (reso 90 wants more at cutoff 127) is a follow-up via K_CUT.
        //
        // 0.0 -> 0.5, APPLIED 2026-09-03. This is defect 5, the resonant peak
        // standing 4 to 6 dB less prominent than ReBirth at high resonance (the
        // "muted chirp" Luke heard). Raising the feedback above the pivot lifts the
        // reso-127 prominence toward ReBirth (it matches at ~0.9, but 0.5 is the
        // safe partial short of the self-oscillation cap). It IMPROVES the net
        // (-0.023, nothing regressed, no whistle) and nudges the songs. It is a
        // PARTIAL fix: reso 90 to 110 stays 3 to 4 dB short because the shortfall is
        // uniform in resonance while this term is reso-proportional, and closing it
        // there would drive reso 127 past the k=4.0 cap. The diode fork was
        // considered for the full match and CLOSED 2026-09-03: the high-res comb it
        // was built for is disproven, the remaining richness is aliasing, not
        // topology. See docs/research/BASS303_FINDINGS.md sections 23-25.
        const double K_B_SLOPE = []{ const char* e=std::getenv("REBORN_K_B_SLOPE"); return e?std::atof(e):0.5; }();
        const double K_B_PIVOT = []{ const char* e=std::getenv("REBORN_K_B_PIVOT"); return e?std::atof(e):0.50; }();
        const double K_CUT = []{ const char* e=std::getenv("REBORN_K_CUT"); return e?std::atof(e):0.0; }();
        // K_CUT cutoff-dependent resonance boost (task 85 follow-up), WINDOWED to
        // the resonant band. The high-cutoff prominence gap is at reso 90-110, so
        // the boost must be gated at BOTH ends of the reso range:
        //   - top: our k=4.0 self-oscillation cap already OVER-resonates reso 127
        //     vs ReBirth's controlled ceiling (23.4 dB / crest 13.8 against RB's
        //     20.8 / 15.9), so a boost there drives the corner into the cap and
        //     wrecks the spectral shape (batchN reso-127 cells blow up). cutGate
        //     ramps to zero from K_CUT_TOP up to reso 1.0.
        //   - bottom: below the pivot ReBirth is a near-flat lowpass with no peak,
        //     so injecting resonance there regressed the bright low-reso/high-cut
        //     corner (batchQ/accentdepth reso-030 cells, +0.05 to +0.11 on the
        //     net). resoAbove weights the boost by (reso - pivot), zero below it.
        // So the term is nonzero only in the reso 60-110 band, where a peak exists
        // and there is headroom below self-oscillation. Default K_CUT 0 makes it
        // vanish, byte-identical to shipped.
        const double K_CUT_TOP = []{ const char* e=std::getenv("REBORN_K_CUT_TOP"); return e?std::atof(e):0.87; }();
        const double cutGate = (reso <= K_CUT_TOP) ? 1.0
            : std::max(0.0, 1.0 - ((double)reso - K_CUT_TOP) / (1.0 - K_CUT_TOP));
        const double resoAbove = std::max(0.0, (double)reso - K_B_PIVOT);
        const double kBeff = K_B + K_B_SLOPE * resoAbove;
        const double kBase = K_A + reso * kBeff
            + resoAbove * K_CUT * clampf(p_[P_CUTOFF], 0.0f, 1.0f) * cutGate;

        // Passband makeup: a resonant ladder loses low end (DC gain ~ 1/(1+k)),
        // so scale the output back up as resonance rises.
        // (computed per-sample from the live k below).

        // --- envelope coefficients (per block) ---
        // TEMP probes for the trajectory-shape investigation:
        //   REBORN_ENV_ATK    filter-env attack seconds        (default 0.002)
        //   REBORN_ENV_OVER   attack overshoot target          (default 1.10)
        //   REBORN_ENV_SHAPE  exponent applied to mainEnv_ before it drives the
        //                     cutoff; >1 makes the sweep fall faster early      (1.0)
        //   REBORN_ENV_DEC_MUL multiplier on the fitted decay time             (1.0)
        const double ENV_ATK     = []{ const char* e=std::getenv("REBORN_ENV_ATK");     return e?std::atof(e):0.002; }();
        const double ENV_OVER    = []{ const char* e=std::getenv("REBORN_ENV_OVER");    return e?std::atof(e):1.10; }();
        const double ENV_SHAPE   = []{ const char* e=std::getenv("REBORN_ENV_SHAPE");   return e?std::atof(e):1.0; }();
        const double ENV_DEC_MUL = []{ const char* e=std::getenv("REBORN_ENV_DEC_MUL"); return e?std::atof(e):1.0; }();
        const double mainAtkC = 1.0 - std::exp(-1.0 / (std::max(ENV_ATK, 1.0e-5) * sr));
        // ~30 ms .. ~2.2 s. RE-FIT 2026-07-24 against the clean env-mod/decay
        // fixture exports, which showed the filter env was not sweeping AT ALL
        // within a note: ReBirth's per-note centroid falls 646 -> 352 Hz (0.87
        // octaves) at decay 20 and flattens to 0.08 octaves at decay 120, i.e.
        // the knob sets the SWEEP SPEED. The old 200 ms..2 s law could not move
        // inside a 125 ms sixteenth, so our centroid stayed flat (0.10..0.20
        // octaves) and the resonant peak parked instead of painting the
        // spectrum, the cause of the mid-scoop vs ReBirth's flat spectrum.
        // Law fitted to both endpoints (60 ms at knob 20, ~1.8 s at knob 120).
        // The earlier "faster law is too briefly bright" note came from the
        // full-mix references the findings audit retired as confounded.
        // DECAY LAW, made quadratic 2026-08-02 (probe, defaults = the old line).
        //
        // The straight line in log tau is right at the top of the knob and much
        // too fast at the bottom. Measured taus, ReBirth against ours at the old
        // constants, decay knob 0/16/32/64/96/127:
        //
        //   ReBirth  0.07  0.09  0.14  0.28  0.81  2.14 s
        //   ours     0.03  0.05  0.09  0.26  0.79  2.22
        //
        // The top three agree to within 0.03 s and knob 0 is 2.3x fast. That was
        // a known second-order residual for a week and it stopped being
        // second-order the moment the envelope got deep: with 5.3 octaves of
        // swing instead of 2.2, a tau error at the fast end moves the corner
        // through several octaves inside a note. It shows up exactly there, as
        // the two worst cells in batchG after the swing landed, both decay 0.
        //
        // A quadratic through knob 0, 64 and 127 gives DEC_B 0.901 and DEC_C
        // 0.584 on a base of 0.070 s, which then predicts the three knobs it was
        // not fitted on to 0.09 / 0.13 / 0.73 against 0.09 / 0.14 / 0.81.
        //
        // APPLIED 2026-08-29 as batch B4, after sitting here as a probe with the
        // old line still the default for four weeks. Three independent sources
        // now agree on it and none of them is the one it was fitted on.
        //
        //   1. The measured taus above, from the decayprobe exports.
        //   2. B2's 25-cell env-mod by decay grid, scored on the TRAJECTORY of
        //      brightness through a step rather than on any statistic of a note.
        //      Timing error over the grid falls 2.267 to 0.425 dB rms, and the
        //      decay-0 column, which carried all of it, falls from 8.54 dB at
        //      env mod 127 to 1.30. Every cell at decay 64 and above was already
        //      inside 0.36 dB and stays there, so this moves only the end of the
        //      knob that was wrong. Scanning the knob-0 constant on its own
        //      gives a clean interior minimum at 0.070 with 0.060 and 0.080 both
        //      worse, so it is a fitted time and not a shoulder.
        //   3. Fitting a time constant directly to ReBirth's own B2 trajectories
        //      gives 0.080 s at knob 0 and 0.129 at knob 32 against this law's
        //      0.070 and 0.129. Past knob 32 that fit goes degenerate, because a
        //      time constant longer than the step cannot be read from one step,
        //      which is exactly where the old straight line was already right.
        //
        // The regression net over all 373 cells we hold an export for is -0.036
        // dB, better, and it is not uniform: the sets that test envelope SHAPE
        // improve most (batchG -0.179, envshape -0.232, batchI -0.116) while
        // envfloor +0.146, envdomain +0.132 and batchQ +0.035 get slightly
        // worse. Those three run at or near env mod 0, where EM_FLOOR is the
        // whole envelope, and they get worse for a reason that is measured and
        // is not this law: our filter's brightness-per-octave is 0 to 25 percent
        // HIGHER than ReBirth's, so holding the corner up for the right length
        // of time costs more spectral error than holding it up for too short a
        // time did. That is the corner and the skirt, defects 3 and 5, and it is
        // the next fit in docs/research/BASS303_FINDINGS.md section 5. Fitting it here by
        // bending the decay law is the exact trade the audit is full of.
        const double DEC_A = []{ const char* e=std::getenv("REBORN_DEC_A"); return e?std::atof(e):0.070; }();
        const double DEC_B = []{ const char* e=std::getenv("REBORN_DEC_B"); return e?std::atof(e):0.901; }();
        const double DEC_C = []{ const char* e=std::getenv("REBORN_DEC_C"); return e?std::atof(e):0.584; }();
        const double decK = clampf(p_[P_DECAY], 0.0f, 1.0f);
        const double decaySec = DEC_A * std::pow(10.0, DEC_B * decK + DEC_C * decK * decK)
                              * ENV_DEC_MUL;
        const double mainDecC = std::exp(-1.0 / (std::max(decaySec, 1.0e-4) * sr));
        // An ACCENTED note runs a SHORTER filter envelope, and does so whether
        // or not the accent knob is up. Measured 2026-07-27 from batch G by
        // tracking the centroid inside one note in 15 ms slices: at accent knob
        // 0, where the level lift is nil (-0.12 dB), the accented note still
        // falls 1.57 octaves over 105 ms against the unaccented note's 0.85,
        // so its filter envelope decays about 1.85x faster. That knob-
        // independent shortening is what makes a lightly-accented note read
        // DARKER overall, and no amount of level-only accent can produce it.
        //
        // 0.45 -> 0.34, REFIT 2026-08-29 against batch B3, which measures the
        // accent's whole TRAJECTORY across the step it fires on and the step
        // after, rather than a statistic per note. ReBirth's accented note is
        // brighter for about 40 ms and then drives 16.7 dB DARKER than the
        // unaccented one for the rest of the step; ours only reached -5.0.
        // ACC_SHORT is what produces that squelch and it was much too gentle.
        const double ACC_SHORT = []{ const char* e=std::getenv("REBORN_ACC_SHORT"); return e?std::atof(e):0.24; }();
        // CUTOFF term on the accent's shortening (batch B7, 2026-08-29). Probe,
        // default 0, which reproduces the pre-B7 build byte for byte.
        //
        // B7's KNOB-0 COLUMN isolates this constant completely, because the
        // accent knob multiplies the corner push by zero there and what is left
        // is the knob-independent shortening and nothing else. Read that column
        // against cutoff and resonance separately:
        //
        //     ours minus ReBirth, dB      cut 32: +5.97   cut 64: +2.63   cut 96: -0.03
        //     at cutoff 64                reso 16: +2.05  reso 60: +2.63  reso 100: +2.62
        //
        // **Our squelch is 6 dB too weak at low cutoff, right at high cutoff,
        // and flat in resonance.** That is a cutoff law and this constant has
        // none. Fitting `ACC_SWEEP` and a cutoff term ON IT instead was tried
        // first and every direction scored worse on both fixture designs, which
        // is what sent the search here: the sweep cannot fix an error that is
        // already there at knob zero.
        // APPLIED 2026-08-29 at 0.60, with ACC_SHORT 0.34 -> 0.24, so the
        // effective shortening runs 0.276 at cutoff 32, 0.313 at 64 and 0.349
        // at 96: it reproduces the old constant where that constant was right
        // and shortens where it was not. Set REBORN_ACC_SHORT_CUT=0 to disable.
        //
        // Scored on BOTH fixture designs at once, which is the rule this batch
        // established: B7's streamed sixteenths 2.153 -> 1.292 dB rms and
        // accentdepth's isolated notes 2.115 -> 2.143, with the B3 trajectory at
        // knob 96 going 0.77 -> 0.74. Two of three improve and the third moves
        // by 0.03. The regression net over 373 cells is -0.001 with accentdepth
        // -0.014 and accentrate -0.038, so nothing else in the instrument moves.
        //
        // ACC_ATK and ACC_DEC MOVED WITH IT, 0.008 to 0.010 and 0.040 to 0.044,
        // and that is not a coincidence. They were fitted in August against a
        // squelch that was too weak at low cutoff, so they had absorbed part of
        // that error. Correcting it with a law rather than a constant freed them
        // to sit where the trajectory actually wants them, which is why the
        // single-point shape score came out BETTER than shipped rather than
        // merely recovered. A constant that was absorbing someone else's error
        // moves when that error is fixed; if it does not, suspect the fix.
        static const double ACC_SHORT_CUT = []{ const char* e=std::getenv("REBORN_ACC_SHORT_CUT"); return e?std::atof(e):0.60; }();
        const double accShortEff = ACC_SHORT
            * (1.0 + ACC_SHORT_CUT * (double)clampf(p_[P_CUTOFF], 0.0f, 1.0f));
        const double mainDecCAcc = std::exp(-1.0 / (std::max(decaySec * accShortEff, 1.0e-4) * sr));
        // ACCENT ENVELOPE TIMES. Both were hardcoded until 2026-08-02 and both
        // are wrong in the same direction: ReBirth's accent is much SLOWER than
        // ours at each end.
        //
        // Differencing an accented note against its matched no-accent control,
        // so the note's own filter envelope cancels, at accent knob 127 cutoff 64
        // resonance 60. The accent's own contribution to brightness, in dB:
        //
        //     ms       0    20    41    61    81   102   122   142
        //     ReBirth  5.77  6.56  7.14  7.25  6.08  3.09 -5.97 -7.53
        //     ours     4.45  3.55  1.53  0.03 -1.26 -2.44 -3.32 -4.39
        //
        // ReBirth RISES to a peak of +7.25 dB at 52 ms. Ours peaks at 0 ms and
        // is already falling. Read only the first four columns, which is inside
        // the gate: past 62 ms the difference mixes the accent with the VCA
        // release and with ACC_SHORT, and cannot be attributed to either.
        //
        // So the accent has a real ATTACK, tens of milliseconds, not the 2 ms
        // placeholder. That is the same defect as the main envelope in a
        // different place: a shape that opens through the note, read as a level.
        // 0.002 -> 0.010, APPLIED 2026-08-02. Fitted with ACC_SWEEP on the four
        // accent knobs of the accentdepth grid, scored ONLY over the first 62 ms
        // because that is where the gate is still open. In-gate trajectory error
        // fell from 3.722 dB rms to 0.924. The attack has a clean interior
        // minimum, 0.010 beating both 0.002 and 0.020, so it is a real time
        // constant and not a shoulder of the depth fit.
        //
        // REFIT 2026-08-29 against batch B3: ACC_ATK 0.010 -> 0.008 and
        // ACC_DEC 0.065 -> 0.040. The 2026-08-02 fit above scored only the
        // first 62 ms, because past the gate it could not separate the accent
        // from the VCA release. B3 removes that limit by differencing an
        // accented pattern against the SAME pattern with no accented steps, so
        // the VCA cancels and the whole step is readable, including the step
        // after. Trajectory error over both steps fell from 5.74 dB rms to
        // 1.00, and ACC_SWEEP came out unchanged at 3.30 when included in the
        // search, so the depth was already right and only the times were wrong.
        //
        // This also improves the 2026-08-02 accentdepth regression at EVERY
        // point on BOTH its statistics, so it is not a trade: brightness law
        // error 0.45/0.04/-0.86/-1.93 becomes 0.37/0.04/-0.66/-1.56 and level
        // law error 0.60/0.90/0.87/0.69 becomes 0.21/0.34/0.24/0.02.
        // 0.008 -> 0.010 and 0.040 -> 0.044, RE-SCANNED 2026-08-29 after
        // ACC_SHORT_CUT landed. See the note there: these had absorbed part of
        // the cutoff error and moved once it was fixed properly.
        static const double ACC_ATK = []{ const char* e=std::getenv("REBORN_ACC_ATK");
                                          return e?std::atof(e):0.010; }();
        static const double ACC_DEC = []{ const char* e=std::getenv("REBORN_ACC_DEC");
                                          return e?std::atof(e):0.044; }();
        const double accAtkC  = 1.0 - std::exp(-1.0 / (std::max(ACC_ATK, 1.0e-5) * sr));
        const double accDecC  = std::exp(-1.0 / (std::max(ACC_DEC, 1.0e-5) * sr));
        const double ampAtkC  = 1.0 - std::exp(-1.0 / (0.002 * sr));           // ~2 ms
        // Plucky re-attack. Short enough to stay percussive, long enough that it
        // is not a step: 1.2 ms band-limits the onset to ~1.3 kHz instead of
        // spraying to Nyquist. REBORN_AMP_ATK overrides for probing.
        const double ampPluckAtk = []{ const char* e=std::getenv("REBORN_AMP_ATK");
                                       return e?std::atof(e):0.0012; }();
        const double ampPluckAtkC = 1.0 - std::exp(-1.0 / (std::max(1.0e-5, ampPluckAtk) * sr));
        // VCA release, 21.92 ms. Fitted against ReBirth on all six batch G wave
        // fixtures with the gate pinned at half a step: spread 0.40 ms, so it is
        // a real time constant and not a per-patch number. The old 6 ms was a
        // placeholder that never mattered, because the gate never closed between
        // consecutive notes and the plucky decay above did all the articulating.
        const double ampRelSec = []{ const char* e=std::getenv("REBORN_AMP_REL");
                                     return e?std::atof(e):0.02192; }();
        const double ampRelC  = 1.0 - std::exp(-1.0 / (std::max(1.0e-5, ampRelSec) * sr));
        // SLIDE time constant. Measured 2026-07-27 across five intervals
        // (+1, +2, +4, +9 and -5 semitones) against ReBirth: it reaches 63% of
        // the interval in 17 to 22 ms, interval-independent as a real time
        // constant should be. Ours was 0.055, which measured 44 to 59 ms, about
        // 3x too slow.
        //
        // The old value came from the HARDWARE, whose glide is ~60 ms. ReBirth
        // is much faster, and ReBirth is the target (docs/research/GOALS.md). This is the
        // second control where following Roland rather than ReBirth was the bug,
        // after the leaky VCA that ReBirth takes to exact digital zero.
        //
        // Precision note: the estimator resolves one cycle, ~9 ms at 110 Hz, so
        // 18 ms carries real uncertainty. Do not fit this finer without a
        // higher-resolution method; the 3x correction is what the data supports.
        const double GLIDE_SEC = []{ const char* e=std::getenv("REBORN_GLIDE"); return e?std::atof(e):0.018; }();
        const double glideC   = 1.0 - std::exp(-1.0 / (std::max(GLIDE_SEC, 1.0e-4) * sr));

        // TEMP probes (do not commit): A = fundamental-thinning highpass; B =
        // resonance-scaled HF high-shelf (cool low reso, warm high reso).
        //   REBORN_A_HP    - A highpass corner Hz (0 = off).
        //   REBORN_B_GAIN  - B shelf dB per unit resonance (0 = off).
        //   REBORN_B_FC    - B shelf corner Hz (default 4000).
        //   REBORN_B_PIVOT - resonance at which B shelf gain is 0 (default 0.40).
        const double A_HP    = []{ const char* e=std::getenv("REBORN_A_HP");    return e?std::atof(e):0.0;    }();
        // OSCILLATOR-SIDE highpass, corner in Hz, 0 = off (probe, 2026-08-30).
        //
        // Batch B8 says ReBirth's saw carries harmonics 2.6 dB ABOVE an ideal
        // sawtooth relative to its fundamental while ours sits 1.8 dB below,
        // and ours with the drive backed off is an ideal saw to 0.1 dB. A
        // fundamental that weak against its own harmonics is a highpass with
        // its corner near the note, and the pitch dependence agrees: ReBirth's
        // fundamental gains 2.2 dB from 65 Hz to 118 Hz where ours is flat.
        //
        // `A_HP` already exists for this and sits at the very OUTPUT, after the
        // ladder and the VCA. Put there at 90 Hz it fixes B8 and costs the
        // regression net +0.776 with 286 of 373 cells worse, which is the
        // question this probe asks: a highpass BEFORE the ladder is shaped by
        // the ladder afterwards, and one after it is not, and those are only
        // the same thing when the ladder is wide open, which is exactly the
        // condition B8 measures under and no other fixture uses.
        const double OSC_HP  = []{ const char* e=std::getenv("REBORN_OSC_HP");  return e?std::atof(e):0.0;    }();
        const double aOscHp  = (OSC_HP > 0.0) ? 1.0 - std::exp(-2.0 * kPi * OSC_HP / sr) : 0.0;
        // OSCILLATOR-SIDE HIGH SHELF, source-level highs (probe, 2026-09-03).
        // Section 15 settled that the saw is 2 to 4 dB too dark on real music,
        // missing HIGHS rather than carrying excess lows, and asked for a
        // source-level highs probe INDEPENDENT of the resonance-scaled B shelf
        // below. This is it: a one-pole high shelf on the RAW oscillator, applied
        // before the drive stage and the ladder, so the ladder shapes the added
        // highs afterward. That makes it self-gating by how open the filter is,
        // unlike the output B shelf, which is the property section 15 asked for.
        // GAIN in dB above the corner (0 = off), corner in Hz. NOT resonance-
        // scaled, on purpose, so it can be told apart from B_GAIN.
        const double OSC_HS_GAIN = []{ const char* e=std::getenv("REBORN_OSC_HS_GAIN"); return e?std::atof(e):0.0;    }();
        const double OSC_HS_FC   = []{ const char* e=std::getenv("REBORN_OSC_HS_FC");   return e?std::atof(e):2000.0; }();
        const double oscHsG1 = (OSC_HS_GAIN != 0.0) ? std::pow(10.0, OSC_HS_GAIN / 20.0) - 1.0 : 0.0;
        const double aOscHs  = (oscHsG1 != 0.0) ? 1.0 - std::exp(-2.0 * kPi * OSC_HS_FC / sr) : 0.0;
        const double B_GAIN  = []{ const char* e=std::getenv("REBORN_B_GAIN");  return e?std::atof(e):0.0;    }();
        const double B_FC    = []{ const char* e=std::getenv("REBORN_B_FC");    return e?std::atof(e):4000.0; }();
        const double B_PIVOT = []{ const char* e=std::getenv("REBORN_B_PIVOT"); return e?std::atof(e):0.40;   }();
        // TEMP probe (do not commit): REBORN_DRIVE - multiplier on the pre-filter
        // saturation drive (harmonic generation). 1.0 = off.
        const double DRIVE_MUL = []{ const char* e=std::getenv("REBORN_DRIVE"); return e?std::atof(e):1.0; }();
        // Asymmetry of the VCO drive saturator. 0 = the old symmetric tanh,
        // which suppressed the sawtooth's even harmonics. Fitted below.
        const double DRIVE_BIAS = []{ const char* e=std::getenv("REBORN_DRIVE_BIAS"); return e?std::atof(e):0.9; }();

        // --- ReBirth's "square" is a PULSE, and it is quieter than the saw ---
        //
        // Measured 2026-07-27 on the batch G wave fixtures, by a method that
        // needs no model of the filter at all. Divide the square's harmonic
        // series by the saw's, each relative to its own fundamental, and the
        // filter cancels exactly, leaving
        //
        //     R(k) = |sin(k.pi.d)| / sin(pi.d)
        //
        // which depends only on the duty cycle d. At d = 0.5 that is 1 for odd
        // k and ZERO for even k, so any even harmonic at all is a pulse-width
        // offset. ReBirth's R(k) fits d = 0.52286 to 0.03 dB rms over 12
        // harmonics at three cutoffs, and the fit is identical at every cutoff,
        // which is what proves it belongs to the oscillator and not the filter.
        //
        // The same fit says two more things worth keeping. ReBirth's SAW is an
        // ideal 1/k sawtooth: the model assumes it and could not have matched
        // otherwise. And the whole path is LINEAR at these settings, for the
        // same reason: a waveshaper would break the cancellation. See
        // docs/COMPONENT_MATRIX.md section 1 item 2.
        //
        // 0.52286 and 0.47714 are spectrally identical (a pulse and its
        // complement differ only in sign), so this is the width, not the phase.
        const double SQR_DUTY = []{ const char* e=std::getenv("REBORN_SQR_DUTY"); return e?std::atof(e):0.52286; }();
        // Level: a same-amplitude pulse would put the square's fundamental
        // 6.00 dB above the saw's. ReBirth measures +0.79 dB, at all three
        // cutoffs to 0.01 dB, so its square oscillator runs 5.20 dB below its
        // saw. Applied at the OSCILLATOR, before the drive stage, because that
        // is where the two waveforms differ in the hardware this models.
        const double SQR_GAIN = []{ const char* e=std::getenv("REBORN_SQR_GAIN"); return e?std::atof(e):0.5493; }();
        // Output DC blocker corner, Hz (0 = off). ReBirth's exports are DC-free
        // to six decimal places; ours were not, because satAsym() only cancels
        // the bias DC at x = 0 and leaves a signal-dependent offset that the
        // VCA then gates. Measured -0.0055 (saw) and -0.0140 (square) against
        // ReBirth's +/-0.000003. At 5 Hz this costs 0.01 dB and 3 degrees at the
        // 110 Hz fundamental, and a real 303 is AC-coupled throughout anyway.
        const double DC_HP = []{ const char* e=std::getenv("REBORN_DC_HP"); return e?std::atof(e):5.0; }();
        const double aDcHp = (DC_HP > 0.0)
            ? 1.0 - std::exp(-2.0 * kPi * DC_HP / sr) : 0.0;

        // Oversampling of the oscillator + drive + TPT ladder. The resonance
        // generates a sharp transient once per oscillator cycle; at the host
        // rate that spike's high-frequency content folds and rounds the peak
        // off, which is what held our crest factor at 4.82 against ReBirth's
        // 6.99. Rendering the whole engine at 4x measured crest 6.65, so the
        // transient was always there and only aliasing was hiding it. See
        // docs/research/DESIGN_MEMO_303_HARMONICS.md.
        const int TPT_OS = []{ const char* e=std::getenv("REBORN_TPT_OS");
                               int v = e ? std::atoi(e) : 4;
                               return v < 1 ? 1 : (v > 16 ? 16 : v); }();
        const double HFQ_MUL    = []{ const char* e=std::getenv("REBORN_HFQ");    return e?std::atof(e):1.0; }();
        const double MAKEUP_MUL = []{ const char* e=std::getenv("REBORN_MAKEUP"); return e?std::atof(e):1.0; }();
        // CUTOFF-DEPENDENT PASSBAND GAIN AT LOW RESONANCE (batch B6).
        //
        // Measured 2026-08-29 on B1's 45-cell plane, as OUTPUT LEVEL with each
        // side normalised by its own mean over the plane, so gain staging,
        // mixer levels and export calibration cannot enter and what is left is
        // how much the knobs move the output. At RESONANCE 0 ReBirth's 303 gets
        // 10.8 dB louder as the cutoff knob opens and ours gets 2.4. Every other
        // resonance row is flat on both sides to within about 2 dB. B5
        // reproduces it independently and Luke's ear on the isolated listening
        // set reports it in words: "ReBirth's are louder and by my ear fuller
        // underneath... harmonics are spot on though", which is a level
        // difference with the resonant character intact.
        //
        // IT IS A GAIN AND NOT A CORNER, and that took two wrong attempts to
        // establish. A lowpass only loses that much of a saw when its corner
        // falls below the fundamental, so the obvious fix is to widen the
        // cutoff knob: CUT_SLOPE 2.9 to 4.25 with CUT_SCALE 0.78 to 0.306 does
        // take the level surface from 6.47 to 1.40 dB rms. It also drags the
        // resonance-96 corner 0.79 octaves low, and adding a resonance-to-corner
        // coupling to put that back (RESO_OCT 1.60, RESO_OCT_CUT -1.05) still
        // leaves batch B2's envelope trajectory at 2.80 dB rms against 0.43 and
        // its onset 11.35 dB DARK. Three independent measurements say the corner
        // is where it should be; only the level is wrong. So the level is what
        // moves.
        //
        //     dB = LVL_CUT * (cutoff - LVL_PIVOT) * exp(-reso / LVL_RGATE)
        //
        // LVL_CUT is the tilt across the whole cutoff knob at resonance 0.
        // LVL_RGATE is how fast resonance takes it away: the residual tilt is
        // 8.4 dB at resonance 0, about 1.7 at 32 and nil from 64 up, which is a
        // decay constant near 0.16 of the knob.
        //
        // THE TILT IS CENTRED, which is not cosmetic. Written as LVL_CUT*cutoff
        // the term is a tilt PLUS a net gain of half its span, and the net gain
        // is not something this measurement can see: the surface is normalised
        // by its own mean precisely so that absolute level cannot enter, and a
        // term that moves the mean scores worse even when its tilt is right. It
        // did: the uncentred form took the surface from 1.84 dB to 2.45.
        // Whether our 303 is also globally quiet is a separate question, it is
        // not answerable from a self-normalised surface, and Luke's ear says it
        // probably is. That belongs to its own fit, not to this one.
        //
        // APPLIED 2026-08-29 at 8.4 dB, gate 0.10, pivot 0.65. Set
        // REBORN_LVL_CUT=0 to disable.
        //
        // On B1's plane the level surface goes 1.844 to 1.228 dB rms and the
        // resonance-0 row, which is the whole defect, goes 3.08 to 1.41. Every
        // other resonance row moves by less than 0.05, so it does what it is
        // for and nothing else. The corner is untouched at 0.065 octaves,
        // batch B2's envelope trajectory holds at 0.442 against 0.425 and the
        // accent at 0.84 against 0.78, both inside the noise.
        //
        // THE INDEPENDENT CHECK IS `tools/analyze/level_ab.py` ON BATCH H, a
        // different fixture set with a different measurement, and it is the one
        // that matters. `COMPONENT_MATRIX.md` has recorded for weeks that our
        // output runs 2.2 to 4.8 dB under ReBirth's with the deficit GROWING
        // with cutoff, and has said plainly not to fix it with a scalar because
        // the cutoff-dependent part is filter error and a gain would only
        // absorb it. Batch H before: peak tilt -1.38 dB across the knob, rms
        // tilt -1.59. After: **-0.03 and -0.25**, with the spread down from
        // 0.47 to 0.11.
        //
        // So the tilt is gone and what is left is a FLAT offset of about -3.1 dB
        // peak, -3.6 rms. That is the condition level_ab.py was written to test
        // for, and it is now met: the remaining deficit is a one-number
        // calibration rather than filter error. Making that call is a separate
        // change and needs Luke, because it moves the level of every render.
        //
        // REBORN_LVL_CUT=0 makes the term vanish and reproduces the pre-2026-08-29
        // build byte for byte.
        const double LVL_CUT   = []{ const char* e=std::getenv("REBORN_LVL_CUT");   return e?std::atof(e):8.4;  }();
        const double LVL_RGATE = []{ const char* e=std::getenv("REBORN_LVL_RGATE"); return e?std::atof(e):0.10; }();
        const double LVL_PIVOT = []{ const char* e=std::getenv("REBORN_LVL_PIVOT"); return e?std::atof(e):0.65; }();
        // HIGH-RESONANCE cutoff level tilt (probe, default 0 = off). The mirror of
        // LVL_CUT. Measured 2026-09-03 (section 25): at reso 127 ReBirth GAINS ~2 dB
        // of level across the cutoff knob where ours DROOPS ~3.5, a 5.7 dB gap, with
        // a small opposite wiggle at reso 64. LVL_CUT gates OFF by reso 64
        // (exp(-reso/0.10)), so nothing corrects the high end; the level up there is
        // the ladder DC loss and the makeup, which under-compensate as the corner
        // opens at high k. This adds a cutoff tilt gated ON above LVL_HI_ON, so it
        // engages at reso ~110 to 127 and leaves reso 64/96 alone.
        const double LVL_HI       = []{ const char* e=std::getenv("REBORN_LVL_HI");       return e?std::atof(e):0.0;  }();
        const double LVL_HI_PIVOT = []{ const char* e=std::getenv("REBORN_LVL_HI_PIVOT"); return e?std::atof(e):0.0;  }();
        const double LVL_HI_ON    = []{ const char* e=std::getenv("REBORN_LVL_HI_ON");    return e?std::atof(e):0.85; }();
        // ENV MOD BELONGS IN THE TILT'S ARGUMENT, and a song is what proved it.
        //
        // The tilt above was fitted on batch B1, where EVERY fixture is env mod
        // 0, so the cutoff KNOB and the corner the filter actually sits at agree
        // and nothing could tell them apart. Real music does not oblige. Calm
        // opens with bass 1 alone at cutoff knob 0, resonance 0 and ENV MOD 78:
        // the knob is shut, the filter is not, and the law took 5.46 dB out of a
        // voice that was already open.
        //
        // Heard on the rack-off export it is exactly that: our level on that
        // window went from 4.09 dB under ReBirth to 6.22 under, a change of
        // 2.13, against the law's predicted 8.4*(0 - 0.65) + 3.29 = 2.17. To a
        // hundredth of a dB, so it is the law and not noise.
        //
        // The tilt follows HOW OPEN THE FILTER IS, and the env mod knob opens it
        // as surely as the cutoff knob does. LVL_EM weights env mod into the
        // same argument, clamped at the top of the range because that is the
        // only range the tilt was measured over. B1 cannot constrain it, being
        // env mod 0 throughout; the sets that can are the ones with env mod
        // movement, and Calm's clean window is the check.
        // APPLIED 2026-08-29 at 1.0. Set REBORN_LVL_EM=0 to disable.
        //
        // On Calm's clean window the octave error goes 8.81 to 6.81 dB rms and
        // the level from 6.22 dB under ReBirth to 1.12 under, against 4.09 under
        // where the day started. The regression net is -0.001 and every set on
        // it reads exactly 0.000 EXCEPT batchI, which is the only one with env
        // mod movement in it and improves by 0.006. A constant that moves only
        // the set that can see it, and nothing else, is behaving.
        //
        // 1.0 and not the 1.2 that would zero the level error on that window:
        // one 28 second window of one song is not something to fit a constant
        // to three digits on, the whole-mix window mildly prefers less, and the
        // round number sits inside the flat part of the scan. Refine it when
        // there is a fixture set with env mod movement and a level target,
        // which B1 is not.
        const double LVL_EM = []{ const char* e=std::getenv("REBORN_LVL_EM"); return e?std::atof(e):1.0; }();
        // FLAT OUTPUT TRIM, the one-number calibration that the tilt above
        // finally makes legitimate. APPLIED 2026-08-29 at 1.46, on Luke's
        // sign-off, with his instruction to take the accent regression it
        // exposes and fix it rather than back this out. Set REBORN_LVL_TRIM=1.0
        // to disable.
        //
        // With the tilt in and this at 1.46, batch H's level goes from -3.00 dB
        // peak and -3.54 rms under ReBirth to **+0.11 and -0.39**, with the
        // cutoff tilt at -0.10 and the spread at 0.11. That is the 303's output
        // level matched to a tenth of a dB on a set it was not fitted on.
        //
        // It is NOT cosmetic, which was a surprise. `softClipUnity` sits at the
        // end of the voice, so the output stage is mildly nonlinear and running
        // 3 dB under ReBirth means driving it 3 dB more gently than ReBirth
        // does. Measured on the regression net, a pure gain is NOT spectrally
        // neutral: +2.3 dB alone takes batchI -0.281 and batchQ -0.291, both
        // better. So the level deficit has been costing spectral parity as well
        // as level, and the tilt ALONE costs +0.066 on the net because it moves
        // the drive without correcting the deficit. The two belong together.
        //
        // The full net over 373 cells goes 2.075 to 1.950, better, with eight
        // sets improving and five regressing. EVERY REGRESSING SET IS AN ACCENT
        // SET (accentdepth +0.213, accentprobe +0.171, accentrate +0.058, plus
        // batchG +0.118), which is not a coincidence: an accented note is the
        // loudest thing this voice makes, so it drives softClipUnity hardest,
        // and 3.3 dB pushes accented notes into it further than unaccented ones.
        // Section 4 of BASS303_FINDINGS records an open accent defect of about
        // 0.8 dB of uniform excess depth. This exposes it rather than causing it.
        const double LVL_TRIM  = []{ const char* e=std::getenv("REBORN_LVL_TRIM");  return e?std::atof(e):1.46; }();
        const double lvlCutEff = clampf((float)((double)clampf(p_[P_CUTOFF], 0.0f, 1.0f)
                                                 + LVL_EM * envmod), 0.0f, 1.0f);
        double lvlDb = (LVL_CUT != 0.0)
            ? LVL_CUT * (lvlCutEff - LVL_PIVOT) * std::exp(-reso / std::max(1.0e-3, LVL_RGATE))
            : 0.0;
        if (LVL_HI != 0.0) {
            const double hiGate = std::max(0.0, ((double)reso - LVL_HI_ON)
                                                / std::max(1.0e-6, 1.0 - LVL_HI_ON));
            lvlDb += LVL_HI * (lvlCutEff - LVL_HI_PIVOT) * hiGate;
        }
        const double lvlGain = std::pow(10.0, lvlDb / 20.0);
        const double lvlOut = lvlGain * LVL_TRIM;
        // Resonance-engaged low-end highpass. APPLIED 2026-08-02 at 119.9 Hz
        // with knee 0.141, on Luke's sign-off. Set REBORN_LF_HP=0 to disable.
        // The law and the evidence are at the use site.
        const double LF_HP_MAX  = []{ const char* e=std::getenv("REBORN_LF_HP");      return e?std::atof(e):119.9; }();
        const double LF_HP_KNEE = []{ const char* e=std::getenv("REBORN_LF_HP_KNEE"); return e?std::atof(e):0.141; }();
        // RESONANCE-0 FLOOR for the low-end highpass. Probe, default 0, which
        // reproduces the existing law exactly.
        //
        // The law is fc(r) = LF_HP_MAX * r / (r + LF_HP_KNEE), so fc(0) = 0 and
        // at resonance 0 there is no highpass at all. The comment below has said
        // since 2026-08-02 that this is KNOWN INCOMPLETE and that ReBirth still
        // loses up to 5.7 dB there, and that the shape could not be identified
        // from the fixtures of the time.
        //
        // Batch B8 identifies it, by accident, while chasing something else. Its
        // open cells run resonance 0, and there our harmonics sit 4.45 dB under
        // ReBirth's relative to the fundamental. Its ENGAGED cells run resonance
        // 16, where this law is alive at 57 Hz, and there the same measurement
        // reads -0.22 dB. Switch the law off in the engaged cells and the gap
        // reopens to +1.30. So the entire "our oscillator is 4.45 dB short"
        // reading is this hole, seen at the one resonance where nothing fills
        // it, and it is not the oscillator at all: with the filter engaged our
        // saw matches ReBirth's to a fifth of a dB.
        const double LF_HP_MIN = []{ const char* e=std::getenv("REBORN_LF_HP_MIN"); return e?std::atof(e):0.0; }();
        // TEMP probe (do not commit): 303-style feedback path. REBORN_FB_HP =
        // highpass corner Hz in the feedback loop (0 = bypass); REBORN_FB_DRIVE =
        // drive into the last-stage feedback nonlinearity (1.0 = current).
        const double FB_HP    = []{ const char* e=std::getenv("REBORN_FB_HP");    return e?std::atof(e):0.0; }();
        const double FB_DRIVE = []{ const char* e=std::getenv("REBORN_FB_DRIVE"); return e?std::atof(e):1.0; }();
        const double KHI_CEIL = []{ const char* e=std::getenv("REBORN_KHI_CEIL"); return e?std::atof(e):2.45; }();
        const double OSC_SINE = []{ const char* e=std::getenv("REBORN_OSC_SINE"); return e?std::atof(e):0.0; }();
        const double RAW_TAP  = []{ const char* e=std::getenv("REBORN_RAW_TAP");  return e?std::atof(e):0.0; }();
        // Headroom of the ladder-input saturator. satTanh bounds u to +-1, which
        // also bounds how tall a transient the resonance can push through, a
        // suspect for the crest ceiling. H>1 saturates later: satTanh(x/H)*H.
        const double U_SAT = []{ const char* e=std::getenv("REBORN_U_SAT"); return e?std::atof(e):1.0; }();
        // Separated resonance feedback. The OLD path summed input and feedback
        // then clipped them together (satTanh((xin - k*fb)/U_SAT)*U_SAT), which
        // DUCKED the resonance when the oscillator was hot, and is why the buffered
        // ladder would not ride the reso-90-110 "round chirp" (BASS303_FINDINGS
        // 29-30). FB_SEP clips the input ALONE and subtracts the already-saturated
        // feedback (fbPrev_) after, so the resonance rings freely like ReBirth's
        // coupled ladder. It rides the HELD chirp (reso-100 slid peak 215->557,
        // reso-127 tail matched) and Luke's ear confirmed 2026-09-04 it is
        // "remarkably close to ReBirth" with the voice intact at reso 88 and no
        // whistle. The RETRIGGERED per-note curve still does not ride (TPT's
        // zero-delay stages settle too fast to ring up on a fast pluck); that is
        // the coupled-ladder item on branch exp/diode-ladder-chirp.
        // 0.0 -> 1.0, APPLIED 2026-09-04 on Luke's sign-off. Costs the net +0.74,
        // which is largely the time-domain ride the band-shape net cannot credit
        // (sections 29-30); the resonance params are UNCHANGED, this is the best
        // held-chirp ride at the least static over-resonance. REBORN_FB_SEP=0
        // restores the old shared clipper.
        const double FB_SEP = []{ const char* e=std::getenv("REBORN_FB_SEP"); return e?std::atof(e):1.0; }();
        const double aFbHp = (FB_HP > 0.0) ? (1.0 - std::exp(-2.0 * kPi * FB_HP / sr)) : 0.0;
        // TEMP probe (do not commit): REBORN_STAGE_NL - Huovilainen-style saturator
        // inside EACH ladder stage (distributed nonlinearity -> harmonic comb).
        // 0 = off (linear stages); higher = harder per-stage soft-clip.
        const double STAGE_NL = []{ const char* e=std::getenv("REBORN_STAGE_NL"); return e?std::atof(e):0.0; }();
        auto sNL = [STAGE_NL](double a){ return STAGE_NL > 0.0 ? std::tanh(STAGE_NL * a) / STAGE_NL : a; };
        // TEMP probe (do not commit): REBORN_POST_DRIVE - drive into a post-filter
        // waveshaper (distortion AFTER the ladder). 0 = off.
        const double POST_DRIVE = []{ const char* e=std::getenv("REBORN_POST_DRIVE"); return e?std::atof(e):0.0; }();
        // EXPERIMENTAL (do not commit to main): coupled nonlinear diode ladder,
        // selected by REBORN_FILTER=diode. Clean-room from the diode-ladder circuit.
        const bool   useDiode = []{ const char* e=std::getenv("REBORN_FILTER"); return e && e[0]=='d'; }();
        // EXPERIMENTAL: "ReBirth-era" ladder, REBORN_FILTER=rb. Design memo
        // 2026-07-23: coupled forward-Euler diode-ladder STRUCTURE run at the
        // NATIVE rate with no prewarp and no oversampling (the Euler warp and
        // the aliasing are the sound being matched), exactly ONE cheap clipper
        // on the resonance tap, and a one-pole HP inside the loop. Structure
        // follows the published Zavalishin Eq.5.18 / Open303 (MIT) form.
        // ATTRIBUTION (task 84, resolved 2026-08-08): the b0 coefficients (below)
        // and the resonance skew are adapted from Open303 (RobinSchmidt/rosic),
        // MIT-licensed. Permitted and attributed per docs/PROVENANCE.md. This is
        // the default-OFF =rb probe, an alternative core, NOT the shipped TPT
        // ladder, whose constants are fitted only from ReBirth exports. Open303
        // is a synthesis input here, never a ReBirth-parity input.
        // Defaults below ARE the 2026-07-23 OS=2 fit against the five clean
        // ReBirth sysid exports: with OS=2 the emphasis frequency tracks ReBirth
        // across the whole cutoff sweep (c070 916 vs 914, c095 1439 vs 1306,
        // c120 1962 vs 2090; the forward-Euler frequency compression is gone).
        // Open residual: prominence is uneven (too weak at low cutoff, slightly
        // strong at the top), a clipper/loop-HP balance still being fit.
        const bool   useRB   = []{ const char* e=std::getenv("REBORN_FILTER"); return e && e[0]=='r'; }();
        const double RB_A     = []{ const char* e=std::getenv("REBORN_RB_A");     return e?std::atof(e):430.0; }();  // cutoff anchor Hz
        const double RB_SLOPE = []{ const char* e=std::getenv("REBORN_RB_SLOPE"); return e?std::atof(e):7.6300; }(); // top/bottom ratio
        // Quadratic-in-knob term on the cutoff exponent: curves the corner map
        // STEEPER at the top so the forward-Euler warp (which pulls the
        // resonant peak increasingly below the corner as cutoff rises) still
        // lands the emphasis at ReBirth's 2090 Hz at knob 120 without
        // over-opening the already-matched low end. 0 = pure exponential.
        const double RB_CURVE = []{ const char* e=std::getenv("REBORN_RB_CURVE"); return e?std::atof(e):0.0; }();
        // Extra loop gain vs knob (keeps the resonance from damping at the top,
        // where prominence ran ~2.8 dB shy). Adds RB_KBOOST*knob to kd.
        const double RB_KBOOST= []{ const char* e=std::getenv("REBORN_RB_KBOOST");return e?std::atof(e):0.0; }();
        const double RB_OS    = []{ const char* e=std::getenv("REBORN_RB_OS");    return e?std::atof(e):2.0; }();  // ladder oversample (OS=2 fit)
        // Base loop gain that resonance scales onto. ~17 is the 1x self-osc
        // threshold; the OVERSAMPLED loop integrates less per substep and so
        // needs a higher base gain to reach the same resonant peak (OS=2 with
        // gain 17 rings weakly). Fit this up for OS>1.
        const double RB_KGAIN = []{ const char* e=std::getenv("REBORN_RB_KGAIN"); return e?std::atof(e):30.0; }();  // OS=2 fit
        // Resonance-to-feedback curve exponent. The Open303 skew (used for the
        // makeup below) saturates too early: reso 20 and 60 mapped to nearly the
        // same feedback, so the peak climbed only ~6 dB across the knob where
        // ReBirth climbs ~20 dB (and reso 115 over-resonated on musical patches,
        // hollowing the mids). A power curve kd = KGAIN * reso^RESP spreads it:
        // RESP>1 weakens low reso, and the higher KGAIN strengthens the top.
        const double RB_RESP  = []{ const char* e=std::getenv("REBORN_RB_RESP");  return e?std::atof(e):1.3; }();
        const double RB_KMAX  = []{ const char* e=std::getenv("REBORN_RB_KMAX");  return e?std::atof(e):40.0; }();   // gain cap (OS=2 fit)
        const double RB_HP    = []{ const char* e=std::getenv("REBORN_RB_HP");    return e?std::atof(e):120.0; }();  // loop HP corner Hz
        const double RB_B0S   = []{ const char* e=std::getenv("REBORN_RB_B0S");   return e?std::atof(e):0.7; }();    // b0 refit scale (OS=2 fit)
        const double RB_CLIP  = []{ const char* e=std::getenv("REBORN_RB_CLIP");  return e?std::atof(e):0.0; }();    // 0 cubic, 1 hard
        const double RB_NAIVE = []{ const char* e=std::getenv("REBORN_RB_NAIVE"); return e?std::atof(e):1.0; }();    // naive (aliased) osc
        const double RB_ACC   = []{ const char* e=std::getenv("REBORN_RB_ACC");   return e?std::atof(e):0.0; }();    // accent->k depth
        // Input normalization: the ladder must run SMALL so k*y4 crosses the
        // clipper knee only at high resonance (full-scale input pins the
        // clipper flat and kills the ring entirely; first fit run).
        const double RB_IN    = []{ const char* e=std::getenv("REBORN_RB_IN");    return e?std::atof(e):0.06; }();  // OS=2 fit
        const double RB_OUT   = []{ const char* e=std::getenv("REBORN_RB_OUT");   return e?std::atof(e):22.0; }();  // level-matched to detroit export
        const double aRbHp    = 1.0 - std::exp(-2.0 * kPi * RB_HP / sr);
        // Open303-style resonance skew: fast early rise, saturating.
        const double rbR      = (1.0 - std::exp(-3.0 * reso)) / (1.0 - std::exp(-3.0));
        const double D_VT   = []{ const char* e=std::getenv("REBORN_D_VT");   return e?std::atof(e):0.30; }();
        const double D_K    = []{ const char* e=std::getenv("REBORN_D_K");    return e?std::atof(e):1.0;  }();
        const double D_P1   = []{ const char* e=std::getenv("REBORN_D_P1");   return e?std::atof(e):2.0;  }();
        const double D_OS   = []{ const char* e=std::getenv("REBORN_D_OS");   return e?std::atof(e):4.0;  }();
        const double D_FBHP = []{ const char* e=std::getenv("REBORN_D_FBHP"); return e?std::atof(e):0.0;  }();
        const double D_G    = []{ const char* e=std::getenv("REBORN_D_G");    return e?std::atof(e):1.0;  }();
        const double D_ITER = []{ const char* e=std::getenv("REBORN_D_ITER"); return e?std::atof(e):4.0;  }();
        const double aDFbHp = (D_FBHP > 0.0) ? (1.0 - std::exp(-2.0 * kPi * D_FBHP / sr)) : 0.0;
        const double aHp     = A_HP  > 0.0   ? 1.0 - std::exp(-2.0 * kPi * A_HP / sr) : 0.0;
        const double aShelf  = B_GAIN != 0.0 ? 1.0 - std::exp(-2.0 * kPi * B_FC / sr) : 0.0;
        const double shelfG1 = std::pow(10.0, (B_GAIN * (reso - B_PIVOT)) / 20.0) - 1.0;  // (linear gain - 1)

        // TEMP probe: reso-scaled ladder-tap blend (broaden the filter's HF at high
        // reso by mixing in a lower-order, gentler-slope tap = real HF harmonics).
        //   REBORN_TAP       - tap to blend toward: 2 (12 dB/oct) or 3 (18 dB/oct); 0 = off.
        //   REBORN_TAP_GAIN  - blend fraction per unit resonance above the pivot.
        //   REBORN_TAP_PIVOT - resonance below which no blend (default 0.30).
        const int    TAP       = []{ const char* e=std::getenv("REBORN_TAP");       return e?std::atoi(e):0;    }();
        const double TAP_GAIN  = []{ const char* e=std::getenv("REBORN_TAP_GAIN");  return e?std::atof(e):0.0;  }();
        const double TAP_PIVOT = []{ const char* e=std::getenv("REBORN_TAP_PIVOT"); return e?std::atof(e):0.30; }();
        const double tapBlend  = TAP != 0 ? std::min(1.0, std::max(0.0, TAP_GAIN * (reso - TAP_PIVOT))) : 0.0;

        // TEMP probe: velocity-aware HF Q-loss gate. When the cutoff is MOVING
        // (a sweep), scale down the hfQ damping so the resonance rings bright;
        // keep full damping only when the cutoff is stationary near the ceiling.
        //   REBORN_VEL     - depth 0..1 (0 = off / original behaviour).
        //   REBORN_VEL_REF - cutoff velocity (Hz/sample) at which motion saturates.
        //   REBORN_VEL_TC  - velocity smoothing time constant, ms (default 5).
        const double velDepth = []{ const char* e=std::getenv("REBORN_VEL");     return e?std::atof(e):0.0;  }();
        const double velRef   = []{ const char* e=std::getenv("REBORN_VEL_REF"); return e?std::atof(e):0.30; }();
        const double velTcMs  = []{ const char* e=std::getenv("REBORN_VEL_TC");  return e?std::atof(e):5.0;  }();
        const double aVel     = 1.0 - std::exp(-1.0 / (std::max(0.1, velTcMs) * 0.001 * sr));
        // Optional resonance-scaling of the gate: open more at high reso, less at
        // mid, matching ReBirth's steep brightness climb. REBORN_GATE_PIVOT>0 enables.
        const double gatePivot = []{ const char* e=std::getenv("REBORN_GATE_PIVOT"); return e?std::atof(e):0.0; }();
        const double velDepthEff = gatePivot > 0.0
            ? velDepth * std::min(1.0, std::max(0.0, (reso - gatePivot) / std::max(1e-6, 1.0 - gatePivot)))
            : velDepth;

        for (int i = 0; i < n; ++i) {
            // --- exponential pitch glide (linear in log-frequency) ---
            if (freqCur_ < 1.0e-6) freqCur_ = freqTarget_;
            freqCur_ *= std::pow(freqTarget_ / freqCur_, glideC);
            double dt = freqCur_ / sr;
            if (dt > 0.49) dt = 0.49;
            if (dt < 0.0)  dt = 0.0;

            // --- band-limited oscillator ---
            // Factored into a step-sized generator so the oversampled ladder
            // below can run it per substep. The oversampled path deliberately
            // does NOT call it here: it advances the phase itself.
            auto genOsc = [&](double step) {
                phase_ += step;
                if (phase_ >= 1.0) phase_ -= 1.0;
                double o;
                // Probe: a pure SINE input. A 4-pole lowpass fed a sine cannot
                // produce broadband HF, so if the -38 dB shelf survives this the
                // oscillator is not its source. See the harmonics memo.
                if (OSC_SINE > 0.5) return std::sin(phase_ * 2.0 * kPi);
                if (!square) {
                    o = 2.0 * phase_ - 1.0;
                    o -= polyBlep(phase_, step);
                } else {
                    // Pulse, not square: rising edge at phase 0, falling edge at
                    // SQR_DUTY. The falling-edge BLEP is evaluated at the phase
                    // measured FROM that edge, (phase - duty) mod 1.
                    o = phase_ < SQR_DUTY ? 1.0 : -1.0;
                    o += polyBlep(phase_, step);
                    double p2 = phase_ + (1.0 - SQR_DUTY); if (p2 >= 1.0) p2 -= 1.0;
                    o -= polyBlep(p2, step);
                    // A pulse off 50% carries DC of (2d - 1); ReBirth's does not
                    // (measured -52 dB, the noise floor). Subtract it at source
                    // rather than let the DC blocker chase it.
                    o = (o - (2.0 * SQR_DUTY - 1.0)) * SQR_GAIN;
                }
                return o;
            };
            const int osTpt = (!useRB && !useDiode) ? TPT_OS : 1;
            double osc = (osTpt > 1) ? 0.0 : genOsc(dt);
            if (aOscHp > 0.0 && osTpt <= 1) {
                oscHpLp_ += aOscHp * (osc - oscHpLp_);
                oscHpLp_  = flushDenormal((float)oscHpLp_);
                osc      -= oscHpLp_;
            }
            if (oscHsG1 != 0.0 && osTpt <= 1) {
                oscHsLp_ += aOscHs * (osc - oscHsLp_);
                oscHsLp_  = flushDenormal((float)oscHsLp_);
                osc      += oscHsG1 * (osc - oscHsLp_);
            }

            // --- main envelope: fast attack, exponential decay (decay knob) ---
            if (mainAttacking_) {
                mainEnv_ += (ENV_OVER - mainEnv_) * mainAtkC;
                if (mainEnv_ >= 1.0) { mainEnv_ = 1.0; mainAttacking_ = false; }
            } else {
                mainEnv_ *= (accentLatched_ ? mainDecCAcc : mainDecC);
            }
            mainEnv_ = flushDenormal((float)mainEnv_);

            // --- accent envelope: separate, faster attack/decay ---
            if (accAttacking_) {
                accEnv_ += (accTarget_ * 1.10 - accEnv_) * accAtkC;
                if (accEnv_ >= accTarget_) { accEnv_ = accTarget_; accAttacking_ = false; }
            } else {
                accEnv_ *= accDecC;
            }
            accEnv_ = flushDenormal((float)accEnv_);

            // --- gate countdown: close the VCA partway through the step ---
            // ReBirth releases at half a step even when the next step is also a
            // note, which is what articulates a run of sixteenths. Ours used to
            // hold the gate open across a whole run and lean on the plucky decay
            // for separation, which is a different shape (see setStepTiming).
            if (gateLeft_ > 0.0) {
                gateLeft_ -= 1.0 / sr;
                if (gateLeft_ <= 0.0) { gateLeft_ = 0.0; gate_ = false; }
            }

            // --- amp (VCA) envelope: gate-shaped trapezoid ---
            if (gate_ && ampDecSec_ > 0.0) {
                // Plucky VCA: a short ATTACK to full, then decay toward the
                // sustain floor while held. Struck notes request the attack in
                // trigger(); slid notes tie and keep decaying.
                if (ampPlucking_) {
                    ampEnv_ += (1.0 - ampEnv_) * ampPluckAtkC;
                    if (ampEnv_ >= 0.999) { ampEnv_ = 1.0; ampPlucking_ = false; }
                } else {
                    const double ampDecC = std::exp(-1.0 / (ampDecSec_ * sr));
                    ampEnv_ = ampSus_ + (ampEnv_ - ampSus_) * ampDecC;
                }
            } else {
                const double ampTarget = gate_ ? 1.0 : 0.0;
                ampEnv_ += (ampTarget - ampEnv_) * (ampTarget > ampEnv_ ? ampAtkC : ampRelC);
            }
            ampEnv_ = flushDenormal((float)ampEnv_);

            // --- cutoff modulation (octaves above base cutoff) ---
            // Main env-mod plus the accent sweep. The accent sweep depth grows
            // with resonance, so accents bite harder on resonant patches.
            // 3.0 octaves at full env-mod (reference-tuned: 6.0 kept the
            // filter camped at peak brightness on long-decay patches).
            // Accent DOES sweep the cutoff, and not slightly. The old 0.02
            // octaves came from a single-knob measurement (knob 110, centroid
            // 1.041x) that averaged over the whole note, where the accent's
            // bright attack and its fast decay very nearly cancel. Batch G
            // resolves them: sliced in 15 ms windows, an accented note at knob
            // 127 starts 1.80x brighter (~0.85 octaves) and is BELOW the
            // unaccented note by 60 ms. The chirp worry in the retired note
            // above is handled by the short envelope, not by a shallow depth.
            // 0.35 -> 0.80, APPLIED 2026-08-02, fitted jointly with ACC_ATK on
            // the in-gate accent trajectory. Across the whole export inventory
            // this improved 33 cells and made ZERO worse, because every fixture
            // without an accent is untouched by it: accentdepth 6.142 -> 3.988,
            // accentprobe 5.917 -> 4.432, accentrate 3.445 -> 2.738, batchG
            // 3.086 -> 2.842.
            //
            // THE KNOB LAW IS STILL THE WRONG SHAPE and this does not fix it.
            // Measured lift at cutoff 64 resonance 60, accent knob 16 to 127:
            //
            //             16    32    48    64    80    96   112   127
            //   ReBirth  4.37  7.18  8.66  9.50  9.98 10.27 10.50 10.65 dB
            //   ours     0.84  1.64  2.40  3.09  3.72  4.28  4.76  5.14  (at 0.35)
            //
            // ReBirth is at 41 percent of its travel by knob 16 and 89 percent
            // by knob 64; ours is near linear. No scalar fixes that, and it is
            // why knob 16 is the worst cell at every depth tried. The LEVEL half
            // of the accent needs no such curve, being within about 1 dB across
            // the whole knob, so the curvature belongs to the filter half alone.
            //
            // DO NOT FIT THAT CURVE ON THIS TABLE. Brightness here is a band
            // ratio, 1 to 5 kHz over 100 to 400, so a corner that climbs past
            // the upper band stops registering and the measurement saturates
            // whether or not the knob does. Separating the two needs the corner
            // itself, which means the peak estimator at a frame short enough to
            // resolve a 10 ms attack. That is the next piece of work here.
            // 0.80 -> 3.30 APPLIED 2026-08-06 on Luke's ear-check sign-off, paired
            // with ACC_RESO_SLOPE -0.708 below (task 72). The two together make the
            // accent corner push DECREASE with resonance as ReBirth's does, instead
            // of grow; the larger sweep restores the mid-reso depth ReBirth has and
            // the negative slope cuts the high-reso over-push. Set REBORN_ACC_SWEEP
            // and REBORN_ACC_RESO_SLOPE to 0.80 / 1.0 to restore the old behaviour.
            static const double ACC_SWEEP = []{ const char* e=std::getenv("REBORN_ACC_SWEEP"); return e?std::atof(e):3.30; }();
            const double envShaped = (ENV_SHAPE == 1.0) ? mainEnv_
                                   : std::pow(std::max(0.0, mainEnv_), ENV_SHAPE);
            // ENV MOD depth, refitted 2026-07-27 against batch I2: nine settings
            // crossed with three decays, where every earlier fit had three.
            // ReBirth's centroid rise above its own env-mod-0 fixture fits
            //
            //     octaves = 0.968 k + 0.324 k^2      (k = knob/127, decay 64)
            //
            // to better than 0.005 octaves at all nine points. The quadratic term
            // is a quarter of the total at full knob, and a straight line through
            // 0 and 127 misses the middle by 0.08 octaves, which is exactly the
            // kind of thing three points reports as "a gain that is too small".
            //
            // EM_CURVE adds that curvature to OUR knob law and the fit says it is
            // not needed: 2.2/0.00 scores 0.0384 octaves rms and 2.1/0.15 scores
            // 0.0374, a tie. Shipping 0.0 rather than buying 0.001 octaves with a
            // free parameter. The reason it is unnecessary is worth keeping: the
            // measured quantity is CENTROID, not corner frequency, and our own
            // cutoff-to-centroid mapping is already expansive enough to produce
            // ReBirth's curve from a linear cutoff law. If the cutoff law is ever
            // refitted (item 1), re-check this, because that is what supplies it.
            static const double EM_CURVE = []{ const char* e=std::getenv("REBORN_EM_CURVE"); return e?std::atof(e):0.0; }();
            // ENV MOD x RESONANCE cross term (probe, default 0.0 = off).
            //
            // BASS303_FINDINGS section 26/27: at high resonance our env-mod corner
            // opens LESS than ReBirth's, and the shortfall grows with env mod (B9
            // at resonance 85 runs 88 to 246 Hz dark from env mod 64 to 127). A
            // global EM_OCT/EM_CURVE lift closes it but regresses the env-mod net
            // fixtures, which sit at resonance 20 to 60 where our law is right
            // (section 27). Only a reso-gated term can open resonance 85 while
            // leaving 20 to 60 alone. This deepens the env-mod SWING in proportion
            // to resonance: zero effect at reso 0, so every low-reso fixture and
            // every static (env mod 0) fit is untouched, and it preserves the
            // fitted EM_OCT/EM_CURVE shape rather than reshaping it. Fitted 2026-09-04.
            static const double EM_RESO = []{ const char* e=std::getenv("REBORN_EM_RESO"); return e?std::atof(e):0.0; }();
            // ENV MOD FLOOR (probe, default 0.0 = off, i.e. current behaviour).
            //
            // ReBirth fires a filter envelope on every note even with the env mod
            // knob at ZERO, and the decay knob sets its decay time. Measured
            // 2026-08-01 at cutoff 64 resonance 45 env mod 0, brightness being
            // 880-2200 Hz relative to the 220 Hz harmonic so level divides out:
            // across decay 0/32/64/96/127 ReBirth reads -18.32/-16.36/-14.45/
            // -12.99/-12.50 dB while ours reads -12.32 at every setting, inert to
            // two decimals.
            //
            // Slicing one 125 ms note into eight and averaging over 56 notes
            // shows it is an envelope and not a static offset: at decay 0 ReBirth
            // goes -12.79 dB at onset to -22.61 by 93 ms, and at decay 127 it
            // stays near -12. The two START within 1.1 dB of each other, which is
            // one envelope of fixed depth with a knob-set decay rather than two
            // different depths. For a 4-pole, 9.8 dB above the corner is about
            // 0.41 octaves of corner movement.
            //
            // This is the "chirp" Luke has described since the first ear check
            // and has confirmed is the same quality as the "snap". It also
            // explains why he cannot distinguish drv_closed and drv_open from
            // ReBirth: both are filter-closed or low-resonance, where 0.4 octaves
            // of corner movement has almost nothing to act on.
            //
            // It multiplies envShaped, so it decays with the DECAY knob exactly
            // as the measurement requires. NOT the VCA attack, which was the
            // obvious suspect given the note about softening it to 1.2 ms
            // (docs/NON_OBVIOUS_BEHAVIOUR.md):
            // swept from 1.2 ms to 20 microseconds it moves this by 0.02 dB.
            // SIGN CONVENTION, and it is not the obvious one. The first attempt
            // ADDED the floor as the envelope opened, and it made every cell
            // worse (rms error 3.39 dB to 7.30 at EM_FLOOR 0.4). The reason is
            // in the measurement: our flat brightness of -12.32 dB already
            // matches ReBirth's DECAY-127 value of -12.50, the setting where its
            // envelope barely decays within the note. Our corner is not sitting
            // at ReBirth's resting position waiting to be lifted; it is already
            // sitting where ReBirth's is with the envelope OPEN.
            //
            // So the envelope SUBTRACTS as it closes. At envShaped = 1 the corner
            // is unchanged, which preserves every existing fit at the note onset
            // where they were anchored, and as the envelope decays the corner
            // falls by up to EM_FLOOR octaves.
            // 0.0 -> 0.85, APPLIED 2026-08-02 on sign-off. This is F in the law
            // below: the envelope's depth in octaves when the env mod knob is at
            // ZERO, which ReBirth has and we had none of. It is meaningless
            // without EM_CENTRE, and every earlier attempt to fit it alone picked
            // a different value from every other (0.60, 1.00, ~0.52) because a
            // single scalar was being asked to stand in for a missing degree of
            // freedom. Fitted here jointly, on both endpoints of ten cells.
            static const double EM_FLOOR = []{ const char* e=std::getenv("REBORN_EM_FLOOR"); return e?std::atof(e):0.85; }();
            // ENVELOPE DOMAIN (probe, default "oct" = current behaviour).
            //
            // Our envelope is exponential in OCTAVES: it adds to log2(fc), so the
            // corner falls by a fixed number of octaves per time constant.
            // ReBirth's is exponential in FREQUENCY: it adds to fc in Hz, which
            // is what a software filter written as `fc = fc0 + env * amount`
            // does, and is the shape measured on the envdomain fixture of
            // 2026-08-02.
            //
            // The two differ only in the MIDDLE of the sweep, and the difference
            // has a sign. With e decaying 1 to 0 and fc = fc0 + B e,
            //
            //     d(log fc)/de = B / (fc0 + B e)
            //
            // which is SMALL while e is large. So a frequency-domain envelope
            // holds the corner up early and then falls away, where an octave one
            // falls at a constant rate throughout. Measured as a fraction of its
            // own total fall, ReBirth is 31.6 percent done at 50 ms where we are
            // 41.0: it is the slow-then-fast shape.
            //
            // Fitting the resonant-peak trajectory both ways, residual as a
            // percent of span, at env mod 127 cutoff 16 where the corner sweeps
            // furthest: Hz 1.4 percent against octaves 7.1. Hz wins in five of
            // six cells and by two to five times wherever the sweep exceeds about
            // two octaves. At env mod 0 the same test gives 3.6 against 3.5 and
            // cannot decide, because the corner only travels 0.9 octaves there
            // and over less than an octave the two curves are nearly the same.
            // That is why this needed its own fixture and could not be settled on
            // the envshape grid.
            //
            // THE PARAMETRISATION IS ENDPOINT-PRESERVING, which is the whole
            // reason it is safe to switch. Writing D for the total envelope
            // travel in octaves, the octave law contributes
            //
            //     D e - EM_FLOOR                     octaves
            //
            // and this contributes
            //
            //     log2(1 + (2^D - 1) e) - EM_FLOOR   octaves
            //
            // The two agree exactly at e = 0 and at e = 1 and differ only in
            // between. So the switch changes the SHAPE of the sweep and nothing
            // else: every static constant fitted at the note onset, where the
            // anchor is, keeps its meaning, and the env mod depth EM_OCT still
            // means what it says. Steps 2 and 3 of docs/research/PLAN_303_ENVELOPE.md
            // refit the depth and the travel; this changes neither.
            //
            // THE ACCENT SWEEP IS DELIBERATELY LEFT IN OCTAVES. It is a second
            // envelope on the same corner and if ReBirth sums its whole filter
            // control in Hz then this belongs there too, but that has not been
            // measured and guessing it now would confound the accent refit that
            // is step 5. Revisit it there, not here.
            //
            // COST: the hz branch calls exp2 and log2 per sample. Both operands
            // are per-block constants, so when this becomes the default, hoist
            // (2^D - 1) to the block where envmod is read.
            static const bool ENV_HZ = []{ const char* e=std::getenv("REBORN_ENV_DOMAIN");
                                           return e && (*e == 'h' || *e == 'H'); }();
            const double envDepthOct = EM_FLOOR
                + envmod * (EM_OCT + EM_CURVE * envmod) * (1.0 + EM_RESO * reso);
            // Accent corner push. The reso factor was (1 + reso), i.e. the push
            // GREW with resonance. Measured 2026-08-06 (accentdepth + accentreso,
            // env_traj corner at reso 60/90/110): ReBirth's push DECREASES with
            // reso (knob 127: 1.34, 0.90, 0.78 oct) while ours grew, so the sign
            // was wrong. REBORN_ACC_RESO_SLOPE (default 1.0 reproduces 1+reso
            // byte-for-byte) parameterises it; a negative slope with a larger
            // ACC_SWEEP boosts mid-reso and cuts high-reso to match. Task 72.
            static const double ACC_RESO_SLOPE = []{ const char* e=std::getenv("REBORN_ACC_RESO_SLOPE"); return e?std::atof(e):-0.708; }();
            // CUTOFF term on the accent push (batch B7, 2026-08-29). Probe,
            // default 0, which reproduces the pre-B7 build byte for byte.
            //
            // B7 crosses the accent knob against cutoff and resonance
            // separately, which no earlier accent batch did, and it says the
            // deficit is CUTOFF. Span of the accent's brightness effect across
            // the knob, ReBirth minus ours: resonance at cutoff 64 gives 4.02,
            // 4.54, 5.25 over reso 16 to 100, while cutoff at resonance 60 gives
            // 5.69, 4.54, 1.34 over cutoff 32 to 96. Resonance moves it 1.2 dB
            // across its whole range and cutoff moves it 4.4.
            //
            // As a RATIO, which is what an octave-denominated law cares about,
            // our sweep is 0.64 of ReBirth's at cutoff 32, 0.53 at 64 and 0.42
            // at 96, so the correction needed GROWS with cutoff where the dB
            // deficit shrinks. There is no cutoff term in the law to carry that,
            // and gridding the two constants that do exist confirms shipped is
            // the best the current law can do: every move of ACC_SWEEP or
            // ACC_RESO_SLOPE makes B7's 25-cell surface worse, in both
            // directions.
            static const double ACC_SWEEP_CUT = []{ const char* e=std::getenv("REBORN_ACC_SWEEP_CUT"); return e?std::atof(e):0.0; }();
            const double accOct = accKnob * accEnv_ * ACC_SWEEP
                                * (1.0 + ACC_RESO_SLOPE * reso)
                                * (1.0 + ACC_SWEEP_CUT * (double)clampf(p_[P_CUTOFF], 0.0f, 1.0f));
            // ENV MOD IS BIPOLAR. APPLIED 2026-08-02 on Luke's sign-off.
            // Set REBORN_EM_CENTRE=-1 to get the pre-2026-08-02 anchoring back.
            //
            // ReBirth's env mod knob does not just open the filter. Measured
            // 2026-08-02 on tied notes at cutoff 64 resonance 60, tabulating the
            // envelope's two ENDPOINTS separately instead of the travel between
            // them, which is what hid this for a week:
            //
            //   env mod    ReBirth onset   ReBirth floor   ours onset   ours floor
            //        0          987 Hz         517 Hz        903 Hz       902 Hz
            //       32         1498            400          1081         903
            //       64         2164            283          1576         903
            //       96         3180        below guard      2549         903
            //      127         4763        below guard      3585         903
            //
            // ReBirth's FLOOR FALLS as the knob rises, 1.72 octaves per unit
            // knob, while its onset rises 2.25. Ours does not move at all. That
            // is a missing degree of freedom, not a gain error: our env mod term
            // is anchored at the closed end and can only ever open the filter, so
            // no value of any scalar reproduces it.
            //
            // The law, refitted the same day on the envfloor fixtures, which drop
            // the note an octave to 55 Hz so ReBirth's floor stays inside the
            // measurable band at every env mod setting instead of only three:
            //
            //     log2 fc = log2(centre) + (F + A k)(env - c)
            //     F = 0.85 oct    A = 4.45 oct/knob    c = 0.5
            //
            // which is exactly envDepthOct * (envShaped - EM_CENTRE) with
            // EM_FLOOR 0.85, EM_OCT 4.45 and EM_CENTRE 0.5.
            //
            // c FITS FREE AT 0.495 AND FORCING IT TO 0.5 COSTS 0.0003 OCTAVES.
            // The swing is SYMMETRIC about the cutoff knob's own corner: the env
            // mod knob spreads the filter equally either side of where the panel
            // says it is. Ten cells and twenty endpoints, rms 0.106 octaves, and
            // held out at cutoff 40, cutoff 90 and resonance 90 it gives 0.14 to
            // 0.22. A factor of 1.3 to 2.1, where the scalar fit this replaces
            // degraded by a factor of eight.
            //
            // Applied, our floor error against ReBirth across the env mod sweep
            // goes from +0.74 / +1.13 / +1.55 / +2.36 / +2.70 octaves, a monotone
            // error that grows with the knob, to +0.13 / +0.10 / +0.02 / +0.18 /
            // +0.04, flat.
            //
            // WHAT THE CURRENT DEFAULT ACTUALLY DOES, since it is not "c = 0".
            // Ours is envDepthOct * envShaped - EM_FLOOR, whose implied centre is
            // EM_FLOOR / envDepthOct: 1.0 at env mod zero, falling toward zero as
            // the knob opens. A centre that slides with the knob, where ReBirth's
            // is fixed. So the sentinel is -1.0 for "leave it alone" rather than
            // 0.0, and the two are not the same setting.
            //
            // WHAT IS LEFT AFTER IT is a near-pure OFFSET, mean +0.09 octaves at
            // the floor and -0.09 at the onset, which is a static corner error
            // and belongs to CUT_SCALE rather than to this. The same fixtures
            // measure that independently: ReBirth's fitted centre runs 500 Hz at
            // cutoff knob 40, 781 at 64 and 1162 at 90, a straight line of 3.09
            // octaves per unit knob to 0.040 octaves, against our CUT_SLOPE of
            // 2.90. So the cutoff law is about 6 percent shallow, which is a
            // separate and much smaller finding than this one.
            //
            // Do not fit F, A and c without holding out another cutoff. The
            // scalar fit this replaces scored 0.071 octaves on its own six cells
            // and 0.542 held out, and its held-out failure is what led here.
            // 0.5 -> 0.45, APPLIED 2026-09-03 with FC_MAX 6000 -> 9000 (see below).
            // The env-mod corner was about 0.5 octaves short of ReBirth's at reso
            // 60 under deep env mod, which is the "saw too dark" on real music:
            // ReBirth's corner reaches 5203 Hz at env mod 127 where ours reached
            // 3585. Lowering the anchor raises the whole env-mod swing, scaled by
            // env mod, so it opens the driven regime (where the songs are dark) and
            // barely touches env mod 0 (where the static net already matches). The
            // gentler 0.45, half the move that was tried and rejected at 0.40,
            // IMPROVES the 373-cell net (-0.037) while closing all four songs
            // (tour saw 3.95 -> 2.6). A phase-invert null confirmed the change is
            // band-limited to the corner region and self-gated to the driven
            // sections, and Luke's ear on the env-mod chunk found it matches
            // ReBirth. See docs/research/BASS303_FINDINGS.md sections 19-22.
            static const double EM_CENTRE = []{ const char* e=std::getenv("REBORN_EM_CENTRE");
                                                return e?std::atof(e):0.45; }();
            const double envOct = ENV_HZ
                ? std::log2(1.0 + (std::exp2(envDepthOct) - 1.0) * envShaped) - EM_FLOOR
                  + accOct
                : (EM_CENTRE >= 0.0
                   ? envDepthOct * (envShaped - EM_CENTRE) + accOct
                   : EM_FLOOR * (envShaped - 1.0)
                     + envmod * envShaped * (EM_OCT + EM_CURVE * envmod)
                     + accOct);
            // Diode-ladder resonance loading detunes the effective corner
            // downward as feedback rises. Softened (0.030 -> 0.012) against the
            // clean cutoff sweep: the old value pulled high-resonance cutoffs
            // down too far, compounding the under-open cutoff mapping.
            // RESONANCE RAISES THE CORNER. Measured 2026-07-27 by inverting
            // ReBirth's 36-cell batch H grid through our own engine as a forward
            // model: sweep CUT_SCALE and a constant K_A to build
            // F(fc, k) -> (85% rolloff, peak prominence), then find the (fc, k)
            // that reproduces each of ReBirth's cells. Its implied corner climbs
            // +0.26 / +1.07 / +1.71 / +0.95 octaves across the resonance knob at
            // cutoff 40 / 64 / 90 / 127, where ours moved -0.03.
            //
            // What made this findable was dropping the "resonance span" ratio and
            // looking at the two raw surfaces together. At cutoff 127 resonance
            // 127 our resonant peak is MORE prominent than ReBirth's (-3.5 dB
            // against -7.2) while our spectrum is DARKER (2859 Hz against 3903).
            // A taller peak at a lower frequency cannot be a Q error: it can only
            // be a corner sitting in the wrong place. Every earlier round read
            // that as "not enough resonance" and pushed the feedback, which is why
            // two refits moved the rms errors without moving the span.
            //
            // Direction matters. A real diode ladder loses corner as feedback
            // rises, which is what the detune term below models and what the
            // hardware does. ReBirth goes the OTHER WAY. That is the third
            // control where following Roland rather than ReBirth was the bug,
            // after the glide and the leaky VCA.
            //
            // This one term also predicts the other two open residuals, which is
            // the reason to believe it: a corner that opens with resonance passes
            // more energy (the passband LEVEL that rises 3.54 dB across ReBirth's
            // cutoff sweep where ours rose 0.98) and puts the measured slope
            // window further above the corner (ReBirth STEEPENS to -22 dB/oct as
            // resonance rises where ours stays at -18).
            // Fitted JOINTLY with the base corner and the feedback, because the
            // three cannot be separated: raising the corner and raising the
            // feedback both lift the 85% rolloff, but pull the peak prominence in
            // opposite directions relative to it. Fitting any one alone only
            // relocates the error, which is what the two previous rounds did.
            // 0.70 octaves at full resonance. See the fit table in
            // docs/COMPONENT_MATRIX.md section 1.
            //
            // 0.70 -> 0.25 in the joint refit of 2026-07-31, with the
            // cutoff-dependent term below taking up the difference. See
            // CUT_SCALE for why everything was refitted. Measured on peaked
            // cells only, the corner now rises +0.69 octaves over resonance
            // 60..127 against ReBirth's +0.53, so this is still about 0.16
            // octaves too aggressive and is the largest known residual in the
            // filter.
            static const double RESO_OCT = []{ const char* e=std::getenv("REBORN_RESO_OCT"); return e?std::atof(e):0.25; }();
            // Cutoff-dependent part of the same coupling, default 0 so the
            // shipped build is unchanged until it is fitted.
            //
            // Measured 2026-07-30 in the HARMONIC domain, which samples the
            // filter's transfer function at 110*k Hz and so is immune to the
            // comb quantisation that makes rolloff() jump up to 0.36 octaves
            // for a 2% change of threshold. Peak-frequency error by cutoff
            // knob 16/40/64/90/110/127 runs +0.37 +0.13 +0.05 -0.15 -0.17
            // -0.31 octaves: our corner sits too HIGH at low cutoff and too
            // LOW at high cutoff, monotonically, crossing zero near knob 64.
            // A constant coupling cannot express that; this term can.
            //
            // 0.0 -> 0.35, fitted and APPLIED 2026-07-31. The per-cutoff
            // peak-frequency errors quoted just above were measured through the
            // miscounting instrument, so treat their exact values as historical;
            // the monotone SHAPE they describe survives re-measurement and is
            // what this term corrects. With it applied the error by cutoff knob
            // 16/40/64/90/110/127 runs +0.07 +0.04 +0.05 +0.05 +0.03 +0.04
            // octaves: flat, and uniformly a touch high, where it used to swing
            // 0.67 octaves end to end.
            static const double RESO_OCT_CUT = []{ const char* e=std::getenv("REBORN_RESO_OCT_CUT"); return e?std::atof(e):0.35; }();
            const double resoOctEff = RESO_OCT
                + RESO_OCT_CUT * (double)clampf(p_[P_CUTOFF], 0.0f, 1.0f);
            double fc = baseCut * std::exp2(envOct + resoOctEff * reso)
                      * (1.0 - 0.012 * kDet * kDet);
            // Hardware-realistic ceiling: the VCF (cutoff + env + accent) tops
            // out around ~6 kHz; without this, big env sweeps drive the
            // resonance to whistle at the Nyquist clamp.
            //
            // MADE PROBEABLE 2026-08-02, because it is a suspect. The 6 kHz was
            // chosen when the env mod swing was 2.2 octaves and one-sided. It is
            // now 5.3 octaves and symmetric, so a bright patch drives the corner
            // into this clamp and PINS it, and a pinned filter does not move.
            // Measured on Clodhopper's 303-2, counting upward brightness
            // gestures of 3 dB or more over twelve seconds:
            //
            //                            gestures   median depth   total
            //   ReBirth                       64        13.5 dB     1007
            //   ours, CUT_SCALE 0.78          48         9.9         504
            //   ours, CUT_SCALE 1.05          25         9.0         181
            //
            // The corner measurement says 1.05 is the right base and 0.78 is
            // 0.4 octaves low, yet 1.05 halves the filter movement on real
            // music. That is this clamp: 1.05 puts the sweep above it. So
            // CUT_SCALE 0.78 is very likely compensating for a ceiling that is
            // too low rather than for anything about the corner itself.
            //
            // ReBirth's own corner is measured at 5830 Hz at cutoff knob 90 with
            // env mod 127, which is already at this clamp, and its fitted centre
            // extrapolates far above it at cutoff 127. Raising it is not free:
            // the resonance has to shed enough Q up there not to whistle, which
            // is what K_HFQ below exists for.
            // 6000 -> 9000, APPLIED 2026-09-03 with EM_CENTRE 0.5 -> 0.45 above.
            // The 6 kHz ceiling capped the env-mod sweep below ReBirth's reach (its
            // corner runs to 5203 Hz at env mod 127 and its fitted centre
            // extrapolates above 6 kHz at high cutoff). Raising it alone does
            // nothing, because the shipped sweep did not reach 6000; it only bites
            // paired with the opened corner above, which then tracks ReBirth to
            // within ~0.03 octaves at env mod 127. Measured no whistle at reso 60;
            // the one residual is a 7-9 kHz overshoot on the STATIC reso-88 groove
            // that pre-dates this change and is invariant to the ceiling, filed
            // separately. See docs/research/BASS303_FINDINGS.md section 22.
            static const double FC_MAX = []{ const char* e=std::getenv("REBORN_FC_MAX");
                                             return e?std::atof(e):9000.0; }();
            const double fcMax = std::min(FC_MAX, sr * 0.45);
            if (fc > fcMax) fc = fcMax;
            if (fc < 20.0)  fc = 20.0;
            if (useRB) {
                // RB mode has its own knob->Hz law (exponential, slope from the
                // measured real-303 map) and its own ceiling (fs/6: forward
                // Euler at 1x needs headroom below Nyquist to stay stable).
                const double ck = (double)clampf(p_[P_CUTOFF], 0.0f, 1.0f);
                fc = RB_A * std::exp2(std::log2(RB_SLOPE) * ck + RB_CURVE * ck * ck)
                          * std::exp2(envOct);
                const double rbTop = std::min(sr / 6.0, 7000.0);
                if (fc > rbTop) fc = rbTop;
                if (fc < 20.0)  fc = 20.0;
            }

            // Live resonance: accent also adds resonance "bite", scaled by the
            // resonance control. Clamp strictly below the runaway point.
            // Accent adds RESONANCE too, but ReBirth's accented notes are only
            // 1.041x brighter, and cutting the accent cutoff-sweep alone left us
            // stuck at 1.27x, so this was the other brightness source.
            static const double ACC_RESO = []{ const char* e=std::getenv("REBORN_ACC_RESO"); return e?std::atof(e):0.1; }();
            // Envelope-coupled resonance: the feedback rides the main (filter)
            // envelope so the ring DAMPS as the note decays instead of ringing at
            // constant Q the whole note. A static Q keeps the resonant peak alive
            // as the cutoff sweeps DOWN through the harmonics, brightening the
            // note tail (the w2 rise vs ReBirth's monotonic fall). K_ENVFLOOR=1
            // disables (constant Q); <1 scales k from kBase at the attack down to
            // kBase*K_ENVFLOOR as the envelope decays.
            static const double K_ENVFLOOR = []{ const char* e=std::getenv("REBORN_K_ENVFLOOR"); return e?std::atof(e):1.0; }();
            const double kEnvScale = K_ENVFLOOR + (1.0 - K_ENVFLOOR) * mainEnv_;
            // Env-mod trades against resonance: high-env-mod patches ring less
            // (the sweep-vs-resonance interaction brightens the note tail only
            // when the cutoff is moving; envmod=0 is untouched, preserving the
            // static match). K_EMTRADE=0 disables.
            static const double K_EMTRADE = []{ const char* e=std::getenv("REBORN_K_EMTRADE"); return e?std::atof(e):0.0; }();
            const double kEmScale = 1.0 - K_EMTRADE * envmod;
            double k = kBase * kEnvScale * kEmScale + accKnob * accEnv_ * reso * ACC_RESO;
            if (k > 4.0) k = 4.0;
            if (k < 0.0) k = 0.0;
            // High-frequency Q loss: the ladder sheds resonance as its corner
            // approaches the ceiling (analog-realistic). This lets the feedback
            // rise across the whole knob without env-swept notes, whose cutoff
            // pins at fcMax, locking into a sustained whistle; static and
            // mid/low-cutoff notes keep their full resonance.
            // Onset and span are probe-able because this damper's scope has
            // drifted. Its comment below claims "static and mid/low-cutoff
            // notes keep their full resonance", and that was true when the
            // static corner at cutoff 127 sat near 3055 Hz. The corner-rise
            // change raised it to ~3471 Hz, inside the ramp, and a fitted
            // RESO_OCT_CUT pushes it past 4600 where hfQ reaches 0.66 and cuts
            // feedback from 3.70 to 2.87. Measured cost at REBORN_RESO_OCT=1.10:
            // disabling hfQ moves the cutoff-127 resonance span 4.30 -> 4.81
            // and cutoff-110 4.29 -> 4.43, against ReBirth's 4.45. The damper
            // exists to stop ENV-SWEPT notes pinned at fcMax from whistling;
            // batch H is entirely static, so it is now firing where it was
            // never meant to.
            // 3000 -> 4000 in the joint refit of 2026-07-31, which moves the
            // damper's onset back above the static corner at high cutoff so it
            // stops firing on the static grid it was never meant to touch. The
            // paragraph above describes exactly that problem; this is the fix.
            static const double HFQ_ON   = []{ const char* e=std::getenv("REBORN_HFQ_ON");   return e?std::atof(e):4000.0; }();
            static const double HFQ_SPAN = []{ const char* e=std::getenv("REBORN_HFQ_SPAN"); return e?std::atof(e):2500.0; }();
            double hfQ = (fc - HFQ_ON) / HFQ_SPAN;   // full loss by ~5.5 kHz
            if (hfQ < 0.0) hfQ = 0.0;
            if (hfQ > 1.0) hfQ = 1.0;
            hfQ *= HFQ_MUL;   // TEMP probe: scale/disable HF Q-loss damping (1.0 = off)
            // Velocity-aware gate (probe): relax the Q loss while the cutoff moves.
            if (velDepth > 0.0) {
                const double velInst = std::fabs(fc - fcPrevV_);
                velLp_ += aVel * (velInst - velLp_);
                velLp_ = flushDenormal((float)velLp_);
                const double motionF = std::min(1.0, velLp_ / velRef);
                hfQ *= (1.0 - velDepthEff * motionF);
            }
            fcPrevV_ = fc;
            // As the corner nears the ceiling, cap the feedback at kHiCeil rather
            // than scale it: this leaves a strong RISING resonance at static and
            // mid/low cutoffs but holds the pinned-at-fcMax env-swept case just
            // below the sustained-whistle onset.
            // Probe-able: this is one of three deliberate resonance dampers, and
            // it is a suspect for the crest ceiling (see task #40 / the harmonics
            // memo). A very large value disables the cap.
            const double kHiCeil = KHI_CEIL;
            double kEff = k - hfQ * std::max(0.0, k - kHiCeil);

            // --- 4-pole zero-delay (TPT) lowpass ladder, saturating feedback ---
            // Per-stage one-pole coefficient (prewarped). fc < 0.45*sr keeps the
            // tan() argument comfortably below pi/2, so g stays finite.
            const double g = std::tan(kPi * fc / sr);
            const double G = g / (1.0 + g);          // in (0,1)

            // VCO drive into the filter. Asymmetric on purpose: see satAsym().
            const double xin = satAsym(driveGain * DRIVE_MUL * osc, DRIVE_BIAS);

            // Resonance feedback uses the previous, saturated ladder output
            // (unit delay); this bounds the self-oscillation into a stable
            // limit cycle instead of an exponential blow-up.
            // Diode-ladder behaviour: the INPUT and the resonance feedback
            // compete inside the same saturating stack, so a hot oscillator
            // DUCKS the resonance instead of being buried under it. Without
            // this, max-resonance patches degenerate into a self-oscillation
            // whistle chirping along the cutoff (reference: Clodhopper's
            // buzzy max-reso bassline).
            const double u = (FB_SEP > 0.5)
                ? satTanh(xin / U_SAT) * U_SAT - kEff * fbPrev_
                : satTanh((xin - kEff * fbPrev_) / U_SAT) * U_SAT;

            // Cascade of four TPT one-poles (default), or an experimental core:
            // REBORN_FILTER=rb (ReBirth-era Euler ladder) / =diode (BE diode).
            double y1 = 0.0, y2 = 0.0, y3 = 0.0, y4 = 0.0;
            if (useRB) {
                // --- ReBirth-era ladder: coupled forward Euler ---
                // Optional integer oversampling of ONLY the ladder integrator
                // (RB_OS): reduces the Euler warp that pulls the resonant peak
                // below the corner as cutoff rises, so the peak stays sharp AND
                // climbs to ReBirth's 2090 Hz at max cutoff instead of
                // compressing to ~1570. The clipper still folds each substep, so
                // the aliasing comb is preserved. b0 uses the oversampled rate.
                const int    os = (int)RB_OS < 1 ? 1 : (int)RB_OS;
                const double osr = sr * (double)os;
                const double fx = fc / (osr * 1.41421356237);
                // b0 rational fit: adapted from Open303 (RobinSchmidt/rosic, MIT).
                // Attributed per docs/PROVENANCE.md; see the =rb attribution note
                // above. RB_B0S is our own OS=2 refit scale.
                double b0 = (0.00045522346 + 6.1922189 * fx) /
                            (1.0 + 12.358354 * fx + 4.4156345 * fx * fx) * RB_B0S;
                if (b0 > 0.30) b0 = 0.30;
                // Loop gain: ~17 = ideal self-osc threshold; cap just below so
                // max resonance rings near-critically but never sustains.
                const double rbRp = std::pow(reso, RB_RESP);   // spread resonance curve
                double kd = RB_KGAIN * rbRp + RB_ACC * accKnob * accEnv_ * RB_KGAIN
                          + RB_KBOOST * (double)clampf(p_[P_CUTOFF], 0.0f, 1.0f);
                if (kd > RB_KMAX) kd = RB_KMAX;
                // Naive (trivially sampled, aliasing) oscillator, ZOH across the
                // substeps; RB_NAIVE=0 falls back to the polyBLEP osc.
                const double xr = RB_IN * ((RB_NAIVE > 0.5)
                    ? driveGain * (square
                        ? ((phase_ < SQR_DUTY ? 1.0 : -1.0)
                           - (2.0 * SQR_DUTY - 1.0)) * SQR_GAIN
                        : (2.0 * phase_ - 1.0))
                    : xin);
                for (int oi = 0; oi < os; ++oi) {
                    // Resonance tap -> ONE cheap clipper -> one-pole HP, then in.
                    double u0 = kd * dcap4_;
                    if (RB_CLIP < 0.5) {                   // cubic soft clip
                        if (u0 >  1.41421356) u0 =  1.41421356;
                        if (u0 < -1.41421356) u0 = -1.41421356;
                        u0 = u0 - u0 * u0 * u0 * (1.0 / 6.0);
                    } else {                               // hard clamp
                        if (u0 >  1.0) u0 =  1.0;
                        if (u0 < -1.0) u0 = -1.0;
                    }
                    dFbHp_ += aRbHp * (u0 - dFbHp_);
                    const double fbRB = u0 - dFbHp_;
                    const double y0 = xr - fbRB;
                    dcap1_ += 2.0 * b0 * (y0     - dcap1_ + dcap2_);  // double-rate first pole
                    dcap2_ +=       b0 * (dcap1_ - 2.0 * dcap2_ + dcap3_);
                    dcap3_ +=       b0 * (dcap2_ - 2.0 * dcap3_ + dcap4_);
                    dcap4_ +=       b0 * (dcap3_ - 2.0 * dcap4_);     // last stage leaks to ground
                }
                dFbHp_  = flushDenormal((float)dFbHp_);
                // Safety net: forward Euler; the clipper bounds loop energy but
                // a hard state clamp guards the extremes.
                for (double* c : { &dcap1_, &dcap2_, &dcap3_, &dcap4_ }) {
                    if (*c >  4.0) *c =  4.0;
                    if (*c < -4.0) *c = -4.0;
                }
                dcap1_ = flushDenormal((float)dcap1_); dcap2_ = flushDenormal((float)dcap2_);
                dcap3_ = flushDenormal((float)dcap3_); dcap4_ = flushDenormal((float)dcap4_);
                y1 = dcap1_; y2 = dcap2_; y3 = dcap3_; y4 = dcap4_;
            } else if (useDiode) {
                // Coupled nonlinear diode ladder (clean-room: capacitor voltages
                // driven by differential-pair / diode currents; stages COUPLED, not
                // buffered). BACKWARD-EULER fixed-point solve per oversampled substep
                // (unconditionally stable, so the cutoff can open without the
                // forward-Euler blow-up). wT set so the small-signal pole sits at fc.
                const double drive_in = driveGain * DRIVE_MUL * osc;
                const double inv2vt = 1.0 / (2.0 * D_VT);
                const double wT  = D_G * 2.0 * D_VT * (2.0 * kPi * fc / (sr * D_OS));
                const double kd  = D_K * k;
                const int    nit = (int)D_ITER;
                for (int os = 0; os < (int)D_OS; ++os) {
                    // fixed-point iterate v[n] = v[n-1] + wT * rhs(v[n]) (backward Euler)
                    double a1 = dcap1_, a2 = dcap2_, a3 = dcap3_, a4 = dcap4_;
                    for (int it = 0; it < nit; ++it) {
                        double fbv = a4;
                        if (D_FBHP > 0.0) fbv = a4 - dFbHp_;
                        const double ud = drive_in - kd * std::tanh(fbv * inv2vt);
                        const double d0 = std::tanh((ud - a1) * inv2vt);
                        const double d1 = std::tanh((a1 - a2) * inv2vt);
                        const double d2 = std::tanh((a2 - a3) * inv2vt);
                        const double d3 = std::tanh((a3 - a4) * inv2vt);
                        const double d4 = std::tanh( a4       * inv2vt);
                        a1 = dcap1_ + (wT * D_P1) * (d0 - d1);   // first pole different (1+3)
                        a2 = dcap2_ +  wT         * (d1 - d2);
                        a3 = dcap3_ +  wT         * (d2 - d3);
                        a4 = dcap4_ +  wT         * (d3 - d4);
                    }
                    dcap1_ = a1; dcap2_ = a2; dcap3_ = a3; dcap4_ = a4;
                    if (D_FBHP > 0.0) dFbHp_ += aDFbHp * (dcap4_ - dFbHp_);
                }
                dFbHp_ = flushDenormal((float)dFbHp_);
                dcap1_ = flushDenormal((float)dcap1_); dcap2_ = flushDenormal((float)dcap2_);
                dcap3_ = flushDenormal((float)dcap3_); dcap4_ = flushDenormal((float)dcap4_);
                y1 = dcap1_; y2 = dcap2_; y3 = dcap3_; y4 = dcap4_;
            } else if (osTpt > 1) {
                // --- OVERSAMPLED TPT ladder ---
                // Oscillator, drive and ladder all run at osTpt x the host rate,
                // and crucially the resonance feedback updates EVERY substep:
                // that feedback is what generates the per-cycle transient, so
                // running it once per host sample is what was smearing it.
                //
                // Decimation is just "take the last substep": the ladder is a
                // 4-pole lowpass at fc (typically well under 5 kHz), so it has
                // already attenuated everything near the host Nyquist by ~80 dB.
                // It IS the anti-imaging filter, and adding another would only
                // cost phase.
                const double gOs = std::tan(kPi * fc / (sr * (double)osTpt));
                const double GOs = gOs / (1.0 + gOs);
                const double dtOs = dt / (double)osTpt;
                const double aOscHpOs = (OSC_HP > 0.0)
                    ? 1.0 - std::exp(-2.0 * kPi * OSC_HP / (sr * (double)osTpt)) : 0.0;
                const double aOscHsOs = (oscHsG1 != 0.0)
                    ? 1.0 - std::exp(-2.0 * kPi * OSC_HS_FC / (sr * (double)osTpt)) : 0.0;
                for (int oi = 0; oi < osTpt; ++oi) {
                    double oscOs = genOsc(dtOs);
                    if (aOscHpOs > 0.0) {
                        oscHpLp_ += aOscHpOs * (oscOs - oscHpLp_);
                        oscHpLp_  = flushDenormal((float)oscHpLp_);
                        oscOs    -= oscHpLp_;
                    }
                    if (oscHsG1 != 0.0) {
                        oscHsLp_ += aOscHsOs * (oscOs - oscHsLp_);
                        oscHsLp_  = flushDenormal((float)oscHsLp_);
                        oscOs    += oscHsG1 * (oscOs - oscHsLp_);
                    }
                    const double xinOs = satAsym(driveGain * DRIVE_MUL * oscOs, DRIVE_BIAS);
                    const double uOs   = (FB_SEP > 0.5)
                        ? satTanh(xinOs / U_SAT) * U_SAT - kEff * fbPrev_
                        : satTanh((xinOs - kEff * fbPrev_) / U_SAT) * U_SAT;
                    double v;
                    v = (sNL(uOs) - z1_) * GOs; y1 = v + z1_; z1_ = y1 + v;
                    v = (sNL(y1)  - z2_) * GOs; y2 = v + z2_; z2_ = y2 + v;
                    v = (sNL(y2)  - z3_) * GOs; y3 = v + z3_; z3_ = y3 + v;
                    v = (sNL(y3)  - z4_) * GOs; y4 = v + z4_; z4_ = y4 + v;
                    double fbS = y4;
                    if (FB_HP > 0.0) {
                        fbHpLp_ += aFbHp * (fbS - fbHpLp_);
                        fbHpLp_  = flushDenormal((float)fbHpLp_);
                        fbS      = fbS - fbHpLp_;
                    }
                    fbPrev_ = flushDenormal((float)satTanh(FB_DRIVE * fbS));
                    z1_ = flushDenormal((float)z1_); z2_ = flushDenormal((float)z2_);
                    z3_ = flushDenormal((float)z3_); z4_ = flushDenormal((float)z4_);
                }
            } else {
                double v;
                v = (sNL(u)  - z1_) * G; y1 = v + z1_; z1_ = y1 + v;
                v = (sNL(y1) - z2_) * G; y2 = v + z2_; z2_ = y2 + v;
                v = (sNL(y2) - z3_) * G; y3 = v + z3_; z3_ = y3 + v;
                v = (sNL(y3) - z4_) * G; y4 = v + z4_; z4_ = y4 + v;
            }

            // Update saturated feedback memory for the next sample.
            // TEMP probe: 303-style feedback highpass + overdrive into the
            // last-stage nonlinearity (the broadband-bloom mechanism). With
            // FB_HP=0 and FB_DRIVE=1 this is exactly satTanh(y4).
            // The oversampled TPT path already did this per substep, which is the
            // whole point of it; doing it again here would apply the feedback
            // twice for one host sample.
            if (osTpt <= 1) {
                double fbSig = y4;
                if (FB_HP > 0.0) {
                    fbHpLp_ += aFbHp * (fbSig - fbHpLp_);
                    fbHpLp_  = flushDenormal((float)fbHpLp_);
                    fbSig    = fbSig - fbHpLp_;
                }
                fbPrev_ = satTanh(FB_DRIVE * fbSig);
            }

            // Keep the integrator states denormal-free.
            z1_ = flushDenormal((float)z1_);
            z2_ = flushDenormal((float)z2_);
            z3_ = flushDenormal((float)z3_);
            z4_ = flushDenormal((float)z4_);
            fbPrev_ = flushDenormal((float)fbPrev_);

            // Passband makeup gain (restores body lost to high resonance).
            // Near-full DC compensation: the original keeps the fundamental
            // FAT even at singing resonance (verified vs a solo export).
            const double makeup = useRB
                ? RB_OUT * 2.0 * (1.0 + rbR) * MAKEUP_MUL     // memo passband makeup, RB mode
                : (1.0 + kEff * 0.75) * MAKEUP_MUL;  // TEMP probe: scale makeup

            // --- VCA (post-filter): gate env + accent level boost ---
            // The lift DECAYS with the accent envelope rather than sitting flat:
            // at knob 127 the accented note's level advantage falls from
            // +10.3 dB at the attack to +2.3 dB by 105 ms (batch G, 15 ms
            // slices). The old 0.35 sustained share held it up and made the
            // depth curve compressive, which read as +5.49 dB at knob 32 where
            // ReBirth gives +4.14 and +10.43 at knob 127 where it gives +11.37.
            // Level is knob-scaled with no knob-independent part: at knob 0
            // ReBirth's lift is -0.12 dB, i.e. nil. (The BRIGHTNESS change at
            // knob 0 is real, and comes from ACC_SHORT above, not from here.)
            static const double ACC_LVL = []{ const char* e=std::getenv("REBORN_ACC_LVL"); return e?std::atof(e):3.2; }();
            static const double ACC_SUS = []{ const char* e=std::getenv("REBORN_ACC_SUS"); return e?std::atof(e):0.10; }();
            // ACCENT LIFT DECAYS WITH accEnv_. Applied as the default 2026-08-06
            // on sign-off (task 78). The old flat ACC_SUS*latched term held 0.10
            // of lift for as long as accentLatched_ stayed set, an unbounded run
            // of ties, so it LEAKED the between-note level too high: at Clodhopper's
            // gap steps ReBirth's floor is -74.8 dB against our -70.4, and dropping
            // the flat term moves us toward it on 9 of 9. accEnv_ (decays ~65 ms)
            // now carries the whole lift at coefficient 1.0; at the attack, where
            // accEnv_ sits at target, that equals the old 0.10 + 0.90*accEnv_, so
            // the struck level is unchanged and only the tail decays. regress_ab
            // across 373 cells improved 2.325 -> 2.274 (29 better, 1 worse;
            // accentprobe -1.70 dB). onset_ab reads it as 55->64 but that is a
            // FLOOR artefact (task 53): all 9 newly-crossing steps keep their peak
            // and only drop their floor. Set REBORN_ACC_FLAT_LATCH=1 to restore
            // the pre-2026-08-06 flat-latch behaviour for A/B.
            static const bool ACC_FLAT_LATCH = []{ const char* e=std::getenv("REBORN_ACC_FLAT_LATCH"); return e && std::atoi(e)!=0; }();
            // accLevelScale_ is 1.0 except on a split tie/rest accent (task 83),
            // where it holds the level at ACC_TIE_TARGET while accEnv_ (and so the
            // corner) fires strong. It only scales the accEnv_-driven level.
            const double accentLift = ACC_FLAT_LATCH
                ? ACC_LVL * accKnob * (ACC_SUS * (accentLatched_ ? 1.0 : 0.0)
                                                 + 0.90 * accEnv_ * accLevelScale_)
                : ACC_LVL * accKnob * accEnv_ * accLevelScale_;
            const double level = ampEnv_ * (1.0 + accentLift);

            // softClipUnity bounds the output no matter how hard the resonance
            // squeals. Apply the level/volume scaling BEFORE the clip so the
            // makeup gain doesn't drive it into flattening the fundamental
            // (that was stripping the lows out of high-resonance patches).
            // 0.25: absolute level calibrated against original-program solo
            // exports (ours ran ~7.5 dB hot at 0.60).
            // Tap blend (probe): mix a gentler-slope tap in at high reso.
            const double yTap = (TAP == 2) ? y2 : (TAP == 3) ? y3 : y4;
            double yOut = y4 + tapBlend * (yTap - y4);
            // A FIXED ZERO, to shallow the stopband. Default 0 = off.
            //
            // ReBirth's skirt SHALLOWS as resonance rises (at cutoff 110, about
            // -33.8 dB/oct at resonance 60 to -29.5 at 127, reproducible to
            // 1.2 dB/oct across two independent export batches). Ours does not
            // move. No constant can fix that: for G^4/(1+k G^4), far above the
            // corner G^4 vanishes, the k G^4 term becomes negligible against 1,
            // and the feedback drops out of the far field entirely. Verified
            // both ways in tools/analyze/skirt_model.py: the ideal ladder's
            // slope moves 2.5 dB/oct across the WHOLE feedback range and our
            // engine's moves 0.2 across the knob.
            //
            // Mixing the 1-pole tap into the output puts a zero in the transfer
            // function. y1 falls at only 6 dB/oct, so above the frequency where
            // it overtakes y4 the composite shallows toward -6.
            //
            // The prediction that made this worth building: a FIXED c needs no
            // resonance law of its own. As feedback grows the peak grows, the
            // crossover moves down into the measurement band on its own, and
            // c = 0.04 reproduces ReBirth's whole row to 1.0 dB/oct rms against
            // a measurement whose own reproducibility is 1.2. The resonance
            // dependence falls out of the interaction rather than being fitted.
            //
            // Two things the model says to watch. The slope is very sensitive to
            // c in the useful region (0.03 to 0.05 moves it 8 dB/oct at k=3.4),
            // and below about c=0.012 the zero makes the skirt slightly STEEPER
            // before the 6 dB/oct tail takes over, so small values are worse
            // than none. Predicted cost is under 1 dB of peak prominence.
            static const double SKIRT_ZERO = []{ const char* e=std::getenv("REBORN_SKIRT_ZERO"); return e?std::atof(e):0.0; }();
            if (SKIRT_ZERO != 0.0) yOut += SKIRT_ZERO * y1;
            // TEMP probe: distortion after the filter (harmonic/intermod spray).
            if (POST_DRIVE > 0.0) yOut = std::tanh(POST_DRIVE * yOut) / POST_DRIVE;
            // Probe: emit the LADDER OUTPUT directly, bypassing makeup, the VCA
            // and the output clipper. Every earlier test varied a gain; this
            // varies the TAP POINT, which is what actually separates "the filter
            // passes too much HF" from "something after it adds HF".
            // --- LOW-END HIGHPASS, engaged by resonance (probe, default OFF) ---
            //
            // Measured 2026-07-31 on batch N, which crosses two tied low notes
            // (C1 32.703 Hz, E1 41.203) so their harmonic combs interleave and
            // give 18 points below 340 Hz. ReBirth loses low end as resonance
            // rises and we do not: at resonance 127 its 32.7 Hz harmonic sits
            // 9.1 dB below its own 230..340 Hz level while ours sits 1.2 dB
            // ABOVE. A one-pole highpass fits ReBirth's curve to 0.45 dB, and
            // ours fits at the search grid's minimum corner, meaning none at
            // all, with the SAME residual. That equality is what says 0.45 dB is
            // the measurement's own scatter rather than a bad model.
            //
            // The corner SATURATES by resonance 60: ReBirth's curves at 60, 90
            // and 127 agree within 0.25 dB even though its resonant peak keeps
            // growing well past 60. So this is switched on, not scaled by
            // feedback, and the law is a saturating one fitted to 1.02 Hz rms:
            //
            //     fc(r) = LF_HP_MAX * r / (r + LF_HP_KNEE),   r = reso knob 0..1
            //
            // giving 0 Hz at resonance 0, 92 at 60, 100 at 90 and 105 at 127
            // against measured 0, 91.7, 101.4 and 104.3.
            //
            // A NOTE ON MECHANISM, so the obvious guess is not retried. A
            // highpass on the resonance FEEDBACK path, which the `rb` core
            // already implements, produces the WRONG SIGN: with the feedback
            // highpassed, low frequencies see no feedback and therefore no
            // 1/(1+k) loss, so they come out LOUDER than the midband. This is
            // the observable fitted directly, not a mechanism claim, which is
            // legitimate here because the parity target is ReBirth's output as
            // a black box.
            //
            // Applied BEFORE the VCA deliberately. A sustained tone cannot tell
            // pre-VCA from post-VCA, so the conservative choice is the one that
            // does not highpass the envelope's own transients and thin the
            // pluck. If a later transient measurement can separate them, this is
            // the line to move.
            //
            // SWITCHED ON BY DEFAULT 2026-08-02, on Luke's sign-off, after the
            // batch L + L2 grid cross-validated it on 30 PLUCKED cells at six
            // cutoffs that it was never fitted on. Batch N, which fitted it, was
            // two TIED notes at two cutoffs. Every instrument improves or holds
            // and none regresses, which is the test it had to pass:
            //
            //   low-end gap, 30 cells       5.232 -> 1.976 dB rms
            //   batch H rolloff             0.1584 -> 0.1504 oct rms
            //   batch H prominence          3.83  -> 2.65  dB rms
            //   whole-curve peak frequency  0.0996 -> 0.0997 oct rms (neutral)
            //   whole-curve prominence      3.19  -> 2.32  dB rms
            //   corner-rise excess          +0.16 -> +0.14 oct
            //   whistle articulation        -23.3 -> -30.7 dB, ReBirth -29.4
            //
            // The resonance span row does not move, which is expected: this
            // works below the corner and the span is measured above it.
            //
            // KNOWN INCOMPLETE AT RESONANCE 0, and deliberately shipped that
            // way. fc(0) = 0 while ReBirth still loses up to 5.7 dB there, and
            // the miss grows with cutoff, -0.46 dB at cutoff 40 to -5.73 at 127.
            // Resonance 127 is closed to 0.34 dB at every cutoff. The
            // resonance-0 SHAPE is not identifiable from six harmonics over 2.6
            // octaves; task 62 builds the fixture that could identify it. Do not
            // fit that column from batch L alone. See docs/research/FINDINGS_AUDIT.md,
            // 2026-08-02.
            //
            // NOTE FOR TASK 53. This moves the gap-articulation baseline a long
            // way, -23.3 to -30.7 dB, because it removes low-frequency energy
            // that was filling the gaps between notes. That is the same
            // observable the DC blocker's 32 ms ring is measured on, so task 53
            // must re-measure rather than reuse its recorded numbers.
            double preVca = yOut * makeup * lvlOut;
            if (LF_HP_MAX > 0.0) {
                const double rk = (double)clampf(p_[P_RESO], 0.0f, 1.0f);
                const double fcHp = LF_HP_MIN
                    + (LF_HP_MAX - LF_HP_MIN) * rk / (rk + LF_HP_KNEE);
                const double aLfHp = 1.0 - std::exp(-2.0 * kPi * fcHp / sr);
                lfHpLp_ += aLfHp * (preVca - lfHpLp_);
                lfHpLp_  = flushDenormal((float)lfHpLp_);
                preVca  -= lfHpLp_;
            }
            double sig = (RAW_TAP > 0.5)
                       ? yOut * 0.25
                       : softClipUnity((float)(preVca * level * vol * 0.25));
            double sample = flushDenormal((float)sig);

            // DC blocker. The drive stage is asymmetric on purpose, and an
            // asymmetric saturator rectifies: satAsym() subtracts tanh(bias),
            // which is the offset for a signal sitting at zero, not the mean of
            // a signal that swings. What is left is gated by the VCA, so every
            // note carried a step. ReBirth's exports have no DC at all.
            if (aDcHp > 0.0) {
                dcLp_ += aDcHp * (sample - dcLp_);
                dcLp_  = flushDenormal((float)dcLp_);
                sample = sample - dcLp_;
            }

            // A (probe): fundamental-thinning highpass (subtract a one-pole lowpass).
            if (aHp > 0.0) {
                hpLp_ += aHp * (sample - hpLp_);
                hpLp_ = flushDenormal((float)hpLp_);
                sample = sample - hpLp_;
            }
            // B (probe): resonance-scaled HF high-shelf above B_FC. Broadband and
            // above the resonant peak, so it lifts the centroid without spiking it.
            if (aShelf > 0.0) {
                shelfLp_ += aShelf * (sample - shelfLp_);
                shelfLp_ = flushDenormal((float)shelfLp_);
                sample = sample + shelfG1 * (sample - shelfLp_);
            }

            out.left[i]  = (float)sample;
            out.right[i] = (float)sample;
        }
    }

private:
    double sr_ = kDefaultSampleRate;
    std::array<float, 9> p_{};

    // oscillator / pitch
    double phase_      = 0.0;
    bool   gate_       = false;
    bool   accentLatched_ = false;   // sustained accent VCA lift for the current note
    double freqCur_    = 55.0;
    double freqTarget_ = 55.0;

    // envelopes
    double mainEnv_ = 0.0; bool mainAttacking_ = false;  // filter env (decay knob)
    double accEnv_  = 0.0; bool accAttacking_  = false;  // separate accent env
    double accTarget_ = 1.0;         // ceiling this accent fire is climbing to
    double accLevelScale_ = 1.0;     // task 83: scales the VCA level of a split tie accent
    double ampEnv_  = 0.0;                               // VCA gate env

    // 4-pole ladder state (TPT integrators + unit-delayed feedback)
    double z1_ = 0.0, z2_ = 0.0, z3_ = 0.0, z4_ = 0.0;
    double lfHpLp_ = 0.0;   // resonance-engaged low-end highpass state
    double oscHpLp_ = 0.0;  // oscillator-side highpass state (REBORN_OSC_HP)
    double oscHsLp_ = 0.0;  // oscillator-side high-shelf state (REBORN_OSC_HS_GAIN)
    double fbPrev_ = 0.0;
    double dcLp_ = 0.0;      // DC-blocker state (see REBORN_DC_HP)
    double hpLp_ = 0.0;      // TEMP probe A: fundamental-thinning highpass state
    double shelfLp_ = 0.0;   // TEMP probe B: HF high-shelf state
    double fcPrevV_ = 0.0;   // TEMP probe: previous fc for velocity gate
    double velLp_ = 0.0;     // TEMP probe: smoothed |dfc| (cutoff velocity)
    bool   ampPlucking_ = false;  // in the plucky VCA's short attack phase
    double gateSec_   = 0.0; // gate length for the next trigger (0 = hold until release())
    double gateLeft_  = 0.0; // seconds of gate remaining on the current note
    double ampDecSec_ = 0.0; // TEMP probe: plucky-VCA decay time sec (0 = off, flat sustain)
    double ampSus_    = 0.0; // TEMP probe: plucky-VCA sustain floor
    double fbHpLp_    = 0.0; // TEMP probe: feedback-highpass one-pole state
    double dcap1_ = 0.0, dcap2_ = 0.0, dcap3_ = 0.0, dcap4_ = 0.0; // diode-ladder caps
    double dFbHp_ = 0.0;     // diode-ladder feedback-highpass state
};

} // namespace

std::unique_ptr<Instrument> makeBassSynth() { return std::make_unique<BassSynth303>(); }

// Dispatcher for the pluggable 303 voice registry (see factories.h). HostedPlugin
// is built by the app layer, not here. This builds EXACTLY the engine asked for:
// the REBORN_BASS_ENGINE A/B override lives in reborn_render, not here, so it can
// never silently override the app's engine picker (which would desync the
// recorded engine from the one actually built).
std::unique_ptr<Instrument> makeBass303(Bass303Engine engine) {
    switch (engine) {
        case Bass303Engine::Open303:       return makeOpen303();
        case Bass303Engine::Native303Tpt:  return makeBassSynth();
        case Bass303Engine::HostedPlugin:  return nullptr;   // app installs it
    }
    return makeBassSynth();
}

} // namespace reborn
