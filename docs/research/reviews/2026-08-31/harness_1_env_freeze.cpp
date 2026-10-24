// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "reborn/factories.h"
#include "reborn/audio.h"
#include <cstdio>
#include <cmath>
using namespace reborn;
// DC was a bad probe: a lowpass passes DC at any cutoff. Use a 2 kHz tone, whose
// amplitude reports the corner directly.
static double run(bool bypass) {
    auto pcf = makePCF();
    pcf->prepare(44100.0);
    pcf->setTempo(120.0);
    pcf->setParamByName("cutoff",    0.0f);
    pcf->setParamByName("envmod",    1.0f);
    pcf->setParamByName("decay",     0.5f);    // tau ~ 88.6 ms
    pcf->setParamByName("pattern",   0.0f);    // one trigger per whole note
    pcf->setParamByName("resonance", 0.0f);
    const int spb = 11025;
    long ph = 0;
    StereoBlock blk((std::size_t) spb);
    for (int step = 0; step < 24; ++step) {
        pcf->onStep(step);
        for (int i = 0; i < spb; ++i, ++ph) {
            float v = 0.5f * std::sin(2.0 * 3.141592653589793 * 2000.0 * ph / 44100.0);
            blk.left[(std::size_t) i] = blk.right[(std::size_t) i] = v;
        }
        if (!bypass) pcf->process(blk, spb);
    }
    // Unit switched in. Peak of the 2 kHz tone over the first 5 ms.
    const int n = 220;
    StereoBlock tail((std::size_t) n);
    for (int i = 0; i < n; ++i, ++ph) {
        float v = 0.5f * std::sin(2.0 * 3.141592653589793 * 2000.0 * ph / 44100.0);
        tail.left[(std::size_t) i] = tail.right[(std::size_t) i] = v;
    }
    pcf->process(tail, n);
    double pk = 0.0;
    for (int i = 20; i < n; ++i) pk = std::fmax(pk, std::fabs((double) tail.left[(std::size_t) i]));
    return pk;
}
int main() {
    const double a = run(false), b = run(true);
    std::printf("  probe: 2 kHz tone, amplitude 0.5, read over the 5 ms after switch-in\n");
    std::printf("  0.69 s (7.8 tau) after the pattern's last trigger.\n");
    std::printf("  PCF in the path throughout   : peak %.6f  (%.1f dB)\n", a, 20*std::log10(a/0.5));
    std::printf("  PCF bypassed, then switched in: peak %.6f  (%.1f dB)\n", b, 20*std::log10(b/0.5));
    std::printf("  difference: %.1f dB\n", 20*std::log10(b/a));
    return 0;
}
