// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Role A verification harness: run ONE effect (the distortion) over a WAV.
// Feeds ReBirth's own CLEAN (dist-off) render through our distortion so the
// input signal matches ReBirth exactly, removing the source confound that
// fixture renders carry. Compare the output spectrum to ReBirth's distorted
// render of the same fixture. Not a shipped tool; a measurement aid.
//   dist_wav <in.wav> <out.wav> <amount01> <shape01>
// Note: `tone` is the SHAPE control's param name in the current DSP. The old
// `level` argument was dropped 2026-09-18 when the phantom param was retired.
#include "reborn/factories.h"
#include "reborn/audio.h"
#include "reborn/wav.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: dist_wav in out amount01 shape01\n"); return 2; }
    int sr = 0;
    std::vector<float> mono = reborn::readWav16Mono(argv[1], &sr);
    if (mono.empty()) { std::fprintf(stderr, "dist_wav: cannot read %s\n", argv[1]); return 1; }
    if (sr <= 0) sr = 44100;
    reborn::StereoBlock blk(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) { blk.left[i] = mono[i]; blk.right[i] = mono[i]; }
    auto dist = reborn::makeDistortion();
    dist->prepare(sr);
    dist->setParamByName("amount", (float) std::atof(argv[3]));
    dist->setParamByName("tone",   (float) std::atof(argv[4]));   // SHAPE
    const int N = (int) blk.size();
    for (int s = 0; s < N; s += 512) {
        int n = (s + 512 <= N) ? 512 : (N - s);
        reborn::StereoBlock sub(n);
        for (int i = 0; i < n; ++i) { sub.left[i] = blk.left[s+i]; sub.right[i] = blk.right[s+i]; }
        dist->process(sub, n);
        for (int i = 0; i < n; ++i) { blk.left[s+i] = sub.left[i]; blk.right[s+i] = sub.right[i]; }
    }
    if (!reborn::writeWav16(argv[2], blk, sr)) { std::fprintf(stderr, "dist_wav: write fail\n"); return 1; }
    return 0;
}
