// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Review harness. Not part of the project; verifies three claims empirically.
#include "reborn/engine.h"
#include "reborn/factories.h"
#include "reborn/pattern.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace reborn;

// Crude band energy: run the effect on white noise, report low/high split.
static void bandEnergy(Effect* fx, double& lowE, double& highE) {
    const int N = 44100;
    StereoBlock b((std::size_t) N);
    unsigned s = 12345u;
    for (int i = 0; i < N; ++i) {
        s = s * 1664525u + 1013904223u;
        float v = (float) ((int) (s >> 8) % 2000 - 1000) / 1000.0f;
        b.left[(std::size_t) i] = b.right[(std::size_t) i] = v;
    }
    fx->process(b, N);
    // one-pole split at ~1 kHz
    float lp = 0.0f, a = 0.135f;   // ~1 kHz at 44.1 k
    lowE = highE = 0.0;
    for (int i = N / 4; i < N; ++i) {          // skip startup
        lp += a * (b.left[(std::size_t) i] - lp);
        lowE  += (double) lp * lp;
        const float hp = b.left[(std::size_t) i] - lp;
        highE += (double) hp * hp;
    }
}

static void claim1_modePolarity() {
    std::printf("\n=== CLAIM 1: mode polarity through the loader ===\n");
    for (int byte6 = 0; byte6 <= 1; ++byte6) {
        Song s;
        s.tempo = 120.0;
        s.effectInit.present   = true;
        s.effectInit.pcfFreq   = 0.5f;      // Freq 64
        s.effectInit.pcfQ      = 0.0f;
        s.effectInit.pcfAmt    = 0.0f;      // envelope out of the path
        s.effectInit.pcfDecay  = 0.5f;
        s.effectInit.pcfPattern= 0;
        s.effectInit.pcfBandpass = (byte6 != 0);   // rbs.cpp: p[6] != 0
        s.effectInit.pcfOn     = true;
        s.effectInit.pcfTarget = 0;
        s.effectInit.masterVolume = 1.0f;
        Engine e;
        e.prepare(44100.0);
        e.loadSong(s);
        Effect* pcf = e.pcf();
        const float mode = pcf->getParamByName("mode");
        pcf->prepare(44100.0);
        double lo = 0, hi = 0;
        bandEnergy(pcf, lo, hi);
        const double ratio = 10.0 * std::log10(hi / lo);
        std::printf("  chunk byte6=%d  pcfBandpass=%s  internal mode param=%.1f  "
                    "high/low = %+.1f dB  -> %s\n",
                    byte6, (byte6 ? "true" : "false"), mode, ratio,
                    (ratio < -6.0 ? "LOWPASS" : "BANDPASS-ish"));
    }
    std::printf("  Measured truth (PCF_FINDINGS s3): byte 0 = BANDPASS, byte 1 = LOWPASS.\n");
}

static void claim2_resonanceZipper() {
    std::printf("\n=== CLAIM 2: resonance is not de-zippered in gain or corner ===\n");
    auto pcf = makePCF();
    pcf->prepare(44100.0);
    pcf->setParamByName("cutoff", 0.5f);
    pcf->setParamByName("envmod", 0.0f);
    pcf->setParamByName("resonance", 0.0f);
    // settle
    StereoBlock warm(2048);
    for (std::size_t i = 0; i < warm.size(); ++i)
        warm.left[i] = warm.right[i] = 0.25f;      // DC-ish, easy to read
    pcf->process(warm, (int) warm.size());
    const float before = warm.left[warm.size() - 1];

    pcf->setParamByName("resonance", 1.0f);        // one automation event
    StereoBlock after(64);
    for (std::size_t i = 0; i < after.size(); ++i)
        after.left[i] = after.right[i] = 0.25f;
    pcf->process(after, (int) after.size());

    std::printf("  last sample before Q event: %.6f\n", before);
    std::printf("  first sample after       : %.6f  (step of %.6f)\n",
                after.left[0], after.left[0] - before);
    std::printf("  input gain gainFor(): Q0 = %+.2f dB, Q127 = %+.2f dB, so a full-range\n"
                "  Q ride steps the signal by 8.48 dB in ONE sample.\n", 4.33, -4.15);
}

static void claim3_envFreezeWhileBypassed() {
    std::printf("\n=== CLAIM 3: the envelope does not decay while the unit is bypassed ===\n");
    auto pcf = makePCF();
    pcf->prepare(44100.0);
    pcf->setTempo(120.0);
    pcf->setParamByName("cutoff", 0.0f);
    pcf->setParamByName("envmod", 1.0f);
    pcf->setParamByName("decay", 0.5f);      // tau ~ 88 ms
    pcf->setParamByName("pattern", 0.0f);    // preset 0, trigger every 32 thirty-seconds

    // Drive onStep repeatedly WITHOUT calling process(), which is what the engine
    // does whenever the PCF is routed nowhere or its unit switch is off.
    for (int step = 0; step < 200; ++step) pcf->onStep(step);

    // Now hand it one block and see what cutoff the first samples use, by looking
    // at how much of a DC input survives. A high env = open filter = DC passes.
    StereoBlock b(16);
    for (std::size_t i = 0; i < b.size(); ++i) b.left[i] = b.right[i] = 1.0f;
    pcf->process(b, (int) b.size());
    std::printf("  after 200 bypassed steps, first output samples: %.5f %.5f %.5f\n",
                b.left[0], b.left[1], b.left[2]);
    std::printf("  (onStep keeps retriggering env_ to the pattern velocity, but only\n"
                "   process() decays it, so env_ is frozen at a trigger level rather\n"
                "   than having decayed away over the bypassed span.)\n");
}

static void claim4_resetOmitsSinceTick() {
    std::printf("\n=== CLAIM 4: reset() leaves sinceTick_ alone ===\n");
    std::printf("  pcf.cpp reset() clears ic1/ic2, env_, tick32_, started_, cutSm_,\n"
                "  envSm_, kSm_ and NOT sinceTick_. Inspected, not renderable in\n"
                "  isolation because sinceTick_ is private and onStep() rezeroes it\n"
                "  on the next sixteenth, which is what keeps it from mattering.\n");
}

int main() {
    claim1_modePolarity();
    claim2_resonanceZipper();
    claim3_envFreezeWhileBypassed();
    claim4_resetOmitsSinceTick();
    return 0;
}
