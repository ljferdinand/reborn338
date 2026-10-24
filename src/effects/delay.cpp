// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Tempo-synced echo (clean-room reimplementation, delay supertask 2026-09-10).
//
// This is the WHOLE DSP, and it is deliberately small: everything here is a law
// measured from ReBirth's exported echo train (docs/specs/SPEC_DELAY.md,
// docs/research/PLAN_DELAY.md), and nothing that ReBirth does not do is added.
//
//   * MONO delay line, INTEGER length, NO interpolation. One mono ring; the echo
//     is read at an integer offset N behind the write head. First-echo offset is
//     exactly N samples.
//   * LENGTH is an integer sample count fit to the measured tables:
//         N = floor( Steps * Fs * 60 / (bpm * D) )
//     with D = 3 on the triplet grid (DELY byte 2 = 0, "divide the beat by 3")
//     and D = 4 on the straight grid (byte 2 nonzero, "divide by 4"). The floor
//     is the truncation ReBirth applies: straight Steps 1 at 120 bpm is
//     floor(5512.5) = 5512, and straight Steps 4 at 123 bpm is floor(21512.19) =
//     21512, both matching the export. (The straight-grid full-bar point,
//     Steps 16 = 88200, reads 88204 in the probe only because the echo lands on
//     the next bar's rimshot; that is a bar-grid confound, not a length law.)
//   * FEEDBACK = byte / 127, linear, reaching exactly 1.0 at byte 127 (no cap):
//     successive taps ratio byte/127, so the train sustains without decay at 127.
//   * WET RETURN LEVEL is the exponential mixer fader on the echo-send byte,
//     applied PER CHANNEL at the send tap in the engine (delaySendGain), so the
//     delay itself receives an already-levelled mono send and adds no gain.
//   * RETURN PAN is the mixer pan law applied to the wet return; BOTH return
//     channels carry the SAME mono echo, panned. It is NOT a ping-pong: the
//     feedback path does not cross channels, so echo 2 sits on the same side as
//     echo 1.
//   * NO filter, no high-cut, no crossfade, no ping-pong, no saturator in the
//     echo path: the echo's spectral centroid equals the dry hit's.
//
// No heap allocation, locks, or I/O in process(); the ring is sized once in
// prepare().
//
#include "reborn/factories.h"
#include "reborn/audio.h"
#include "reborn/pattern.h"   // panGains, panFromByte (the mixer pan law, reused)
#include <array>
#include <vector>
#include <cstddef>
#include <cmath>

namespace reborn {
namespace {

// The ring only has to hold the longest length we can be asked for; anything
// longer is clamped. A full bar at 120 bpm is 88200 samples (2.0 s), so 4.0 s
// covers the measured range with headroom and clamps the rest.
constexpr double kMaxDelaySec = 4.0;

struct P { const char* n; const char* l; float def; };
// Parameter names are stable for the song loader, which addresses them by name:
// it sets "steps" (1..16 as byte/16), "grid" (0 = triplet /3, else straight /4),
// "feedback" (byte/127) and "pan" (the wet-return pan byte / 127).
const std::array<P, 4> kP = {{
    {"feedback", "Feedback", 0.5f},
    {"steps",    "Steps",    4.0f / 16.0f},
    {"grid",     "Grid",     0.0f},
    {"pan",      "Pan",      64.0f / 127.0f},
}};

class Delay : public Effect {
public:
    // ---------------------------------------------------------- lifecycle --
    void prepare(double sr) override {
        sr_ = (sr > 1.0) ? sr : kDefaultSampleRate;
        const std::size_t n = (std::size_t)(sr_ * kMaxDelaySec) + 8;
        ring_.assign(n, 0.0f);
        w_ = 0;
        updateLength();
    }

    void reset() override {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        w_ = 0;
    }

    void setTempo(double bpm) override {
        if (bpm > 1.0) tempo_ = bpm;
        updateLength();
    }

    // --------------------------------------------------------- parameters --
    int numParams() const override { return (int)kP.size(); }

    ParamInfo paramInfo(int i) const override {
        if (i < 0 || i >= (int)kP.size()) i = 0;
        return { kP[i].n, kP[i].l, 0.0f, 1.0f, kP[i].def, "" };
    }

    float getParam(int i) const override {
        return (i >= 0 && i < (int)p_.size()) ? p_[i] : 0.0f;
    }

    void setParam(int i, float v) override {
        if (i < 0 || i >= (int)p_.size()) return;
        p_[i] = clampf(v, 0.0f, 1.0f);
        if (i == 1 || i == 2) updateLength();   // steps or grid changed
    }

    // ------------------------------------------------------------- render --
    // The io bus arrives carrying the summed, per-channel-levelled echo send in
    // both channels. We sum it to mono, run the integer ring, and OVERWRITE the
    // bus with the panned wet return (the engine adds it into the master).
    void process(StereoBlock& io, int n) override {
        const int bufN = (int)ring_.size();
        if (bufN < 4 || n <= 0) return;
        if (n > (int)io.size()) n = (int)io.size();

        const int   len = len_;
        const float fb  = clampf(p_[0], 0.0f, 1.0f);   // byte/127, up to 1.0 at 127
        const int   panByte = (int)std::lround((double)p_[3] * 127.0);
        float gl, gr;
        panGains(panFromByte(panByte), gl, gr);         // same law as the mixer pan

        for (int i = 0; i < n; ++i) {
            const float in = 0.5f * (io.left[i] + io.right[i]);   // mono send
            int r = w_ - len;
            if (r < 0) r += bufN;
            const float echo = ring_[r];
            ring_[w_] = flushDenormal(in + echo * fb);
            if (++w_ >= bufN) w_ = 0;
            // Both channels carry the same mono echo, panned (no ping-pong).
            io.left[i]  = echo * gl;
            io.right[i] = echo * gr;
        }
    }

private:
    // Integer sample-count length fit to the measured tables (see the header
    // comment). floor() is the truncation ReBirth applies. A tiny epsilon keeps
    // exact multiples (e.g. 22050.0) off a floating-point 21999.999... floor;
    // it is far too small to disturb the .5 and .195 truncations that matter.
    void updateLength() {
        int steps = (int)std::lround((double)p_[1] * 16.0);
        if (steps < 1)  steps = 1;
        if (steps > 16) steps = 16;
        const int div = (p_[2] >= 0.5f) ? 4 : 3;   // grid: triplet /3, straight /4
        const double exact = (double)steps * sr_ * 60.0 / (tempo_ * (double)div);
        int len = (int)std::floor(exact + 1e-6);
        if (len < 1) len = 1;
        const int hi = ring_.empty() ? (int)(sr_ * kMaxDelaySec)
                                     : ((int)ring_.size() - 2);
        if (len > hi) len = hi;
        len_ = len;
    }

    // -------------------------------------------------------------- state --
    double sr_    = kDefaultSampleRate;
    double tempo_ = 120.0;
    std::array<float, 4> p_ = { 0.5f, 4.0f / 16.0f, 0.0f, 64.0f / 127.0f };

    std::vector<float> ring_;
    int w_   = 0;
    int len_ = 11025;
};

} // namespace

std::unique_ptr<Effect> makeDelay() { return std::make_unique<Delay>(); }

} // namespace reborn
