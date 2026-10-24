// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Role A verification harness: run ONE effect (the compressor) over a WAV.
// Feeds ReBirth's own uncompressed render through our compressor so the input
// level, spectrum and crest all match ReBirth exactly, removing the source
// confound that fixture renders carry. Not a shipped tool; a measurement aid.
//   comp_wav <in.wav> <out.wav> <ratio01> <threshold01>
#include "reborn/factories.h"
#include "reborn/audio.h"
#include "reborn/wav.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: comp_wav in out ratio01 thr01\n"); return 2; }
    int sr = 0;
    std::vector<float> mono = reborn::readWav16Mono(argv[1], &sr);
    if (mono.empty()) { std::fprintf(stderr, "comp_wav: cannot read %s\n", argv[1]); return 1; }
    if (sr <= 0) sr = 44100;
    reborn::StereoBlock blk(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) { blk.left[i] = mono[i]; blk.right[i] = mono[i]; }
    auto comp = reborn::makeCompressor();
    comp->prepare(sr);
    comp->setParamByName("ratio",     (float) std::atof(argv[3]));
    comp->setParamByName("threshold", (float) std::atof(argv[4]));
    // Process in blocks of 512 to mirror real-time block behaviour.
    const int N = (int) blk.size();
    for (int s = 0; s < N; s += 512) {
        int n = (s + 512 <= N) ? 512 : (N - s);
        reborn::StereoBlock sub(n);
        for (int i = 0; i < n; ++i) { sub.left[i] = blk.left[s+i]; sub.right[i] = blk.right[s+i]; }
        comp->process(sub, n);
        for (int i = 0; i < n; ++i) { blk.left[s+i] = sub.left[i]; blk.right[s+i] = sub.right[i]; }
    }
    if (!reborn::writeWav16(argv[2], blk, sr)) { std::fprintf(stderr, "comp_wav: write fail\n"); return 1; }
    return 0;
}
