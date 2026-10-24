// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cstdio>
#include <cmath>
using namespace reborn;
// Largest single-sample discontinuity when one resonance automation event lands,
// as a function of the Freq knob. Signal is a 300 Hz sine so there is real
// content either side of the corner.
int main() {
    std::printf("  Freq  fc(Hz)   step at the Q event   as dB of the local level\n");
    for (float cut : { 0.25f, 0.5f, 0.75f, 0.9f, 1.0f }) {
        auto pcf = makePCF();
        pcf->prepare(44100.0);
        pcf->setParamByName("cutoff", cut);
        pcf->setParamByName("envmod", 0.0f);
        pcf->setParamByName("resonance", 0.0f);
        const int N = 8192;
        StereoBlock a((std::size_t) N);
        for (int i = 0; i < N; ++i) {
            float v = 0.3f * std::sin(2.0f * 3.14159265f * 300.0f * i / 44100.0f);
            a.left[(std::size_t) i] = a.right[(std::size_t) i] = v;
        }
        pcf->process(a, N);                       // settle at Q = 0
        const float last = a.left[(std::size_t) (N - 1)];
        const float prev = a.left[(std::size_t) (N - 2)];
        const float slope = last - prev;          // expected next increment
        pcf->setParamByName("resonance", 1.0f);   // ONE automation event
        StereoBlock b(8);
        for (int i = 0; i < 8; ++i) {
            float v = 0.3f * std::sin(2.0f * 3.14159265f * 300.0f * (N + i) / 44100.0f);
            b.left[(std::size_t) i] = b.right[(std::size_t) i] = v;
        }
        pcf->process(b, 8);
        const float jump = b.left[0] - (last + slope);   // departure from smooth continuation
        const float fc = 9.01f * std::exp2(cut * 10.653f);
        std::printf("  %.2f  %7.0f   %+.6f              %+.2f dB\n",
                    cut, fc, jump, 20.0f * std::log10(std::fabs(jump) / 0.3f + 1e-12f));
    }
    return 0;
}
