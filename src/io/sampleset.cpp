// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "reborn/sampleset.h"
#include "reborn/wav.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace reborn {
namespace {

// DC-block: several original recordings carry a DC offset; the rimshot reads
// as dark rumble without this. Applied to every source, WAV or mod-embedded.
void dcBlock(std::vector<float>& d) {
    if (d.empty()) return;
    double mean = 0.0;
    for (float v : d) mean += v;
    mean /= (double) d.size();
    for (float& v : d) v = (float)(v - mean);
}

bool give(Instrument* inst, int voice, const SampleSource& src, const std::string& name) {
    int sr = 0;
    std::vector<float> d = src(name, &sr);
    if (d.empty()) return false;
    inst->setSample(voice, std::move(d), sr);
    return true;
}

} // namespace

int loadSampleSet(Engine& engine, const std::string& dir) {
    return loadSampleSetFrom(engine, [&dir](const std::string& name, int* sr) {
        std::vector<float> d = readWav16Mono(dir + "/" + name + ".wav", sr);
        dcBlock(d);
        return d;
    });
}

int loadSampleSetFrom(Engine& engine, const SampleSource& dir) {
    const Song& song = engine.song();
    Instrument* d9 = engine.instrument(kDrum909);
    Instrument* d8 = engine.instrument(kDrum808);
    int loaded = 0;

    if (d9) {
        // BD/SD Tune selects a recorded variant, and the variants ARE an
        // ASCENDING pitch/brightness ladder (measured 2026-08-25: bd1..4 at
        // 54/54/59/65 Hz, sd1..5 at 124/145/178/215/242 Hz). Max tune picks the
        // HIGHEST body, matching ReBirth: bd_tune 0/64/127 -> bd1/bd3/bd4 and
        // sd_tune 0/64/127 -> sd1/sd3/sd5. The earlier code inverted this (max
        // tune -> lowest) which ran the kick and snare backwards up the knob.
        // BD's four variants split the knob into EQUAL QUARTERS (measured
        // 2026-08-25: ReBirth reads 53.8/53.8/59.2/64.6/64.6 Hz at tune
        // 0/32/64/96/127, so tune 96 already sits on bd4). Rounding put the
        // bd3/bd4 boundary too high and left tune 96 on bd3.
        int bdv = std::min(4, 1 + (int) std::floor(song.drum909Knobs.bdTune * 4.0));
        // SD's five variants ARE round-based: ReBirth reads 124/145/178/215/242 Hz
        // at tune 0/32/64/96/127, which 1+round(t*4) reproduces exactly.
        int sdv = 1 + (int) std::lround(song.drum909Knobs.sdTune * 4.0);
        loaded += give(d9, 0, dir, "909_bd" + std::to_string(bdv));

        // Snare = tone body + the REAL SDN noise recording, pre-mixed by the
        // song's snappy knob (the original's own construction); the engine's
        // synthesized noise hybrid is muted via sd_snap when this succeeds.
        {
            int sr1 = 0, sr2 = 0;
            std::vector<float> body = dir("909_sd" + std::to_string(sdv), &sr1);
            int ndv = 1 + (int) std::lround(song.drum909Knobs.sdSnappy * 2.0);
            std::vector<float> nz = dir("909_sdn" + std::to_string(ndv), &sr2);
            if (!body.empty() && !nz.empty() && sr1 == sr2) {
                float snap = song.drum909Knobs.sdSnappy;
                float tone = song.drum909Knobs.sdTone;
                // Mix law SOLVED 2026-08-25 rather than fitted by eye: a
                // non-negative least squares decomposition of each ReBirth export
                // onto the raw body and raw noise recordings (power domain) gives
                // the gains the original actually uses.
                //
                // The decisive result is that the BODY GAIN IS CONSTANT at 0.253
                // across the whole tone AND snappy range. Only the noise moves, so
                // Tone is not a body/noise crossfade as an earlier eyeball fit
                // assumed; it is a noise-level control like Snappy, and the body
                // must not be attenuated by it.
                //
                //   noise/body vs TONE   (snappy 64): .165 .369 .419 .427 .431
                //   noise/body vs SNAPPY (tone 64)  : .000 .203 .419 .464 .621
                //
                // Snappy reaches a TRUE ZERO (its snappy-0 snare is pure body) and
                // also steps the noise VARIANT (sdn1/2/3, selected above). Tone
                // saturates hard, most of its travel happening by knob 32, which is
                // why it barely moves above the middle. The raw body carries NO high
                // frequency at all (0.00% above 1.2 kHz), so every bit of the
                // snare's brightness is this noise layer.
                float gsnap = 1.023f * std::pow(snap, 0.778f);   // x1.38: the device tanh compresses the mix
                // RE-FIT 2026-08-25 once the tone GATE below was in place. The
                // gate carries most of Tone's audible effect, so this level term
                // only needs a gentle floor; leaving it at the pre-gate 0.615 made
                // the two effects compound and left the tone-0 corners far too dark
                // (at tune 0 / tone 0 / snappy 127 our centroid read 1483 against
                // ReBirth's 3688). Fitted against the centroid across the three
                // sweeps and the four high-snappy corners, worst case now 446 Hz.
                float gtone = 1.0f - 0.25f * std::exp(-5.5f * tone);
                float g = gsnap * gtone;
                float toneBody = 1.0f;               // constant, see above
                // The noise layer is GATED, and TONE SETS THE GATE'S LENGTH.
                // This is the knob's main audible effect and the average-power fit
                // above cannot see it at all, because the total energy is dominated
                // by the first 100 ms whatever the tail does.
                //
                // Decomposing ReBirth onto the body and noise recordings in
                // successive 20 ms windows (rather than over the whole hit) shows
                // the body gain flat throughout while the noise gain holds and then
                // collapses, and the hold moves with tone:
                //
                //   tone   0: already -16 dB by 80 ms, -33 by 120
                //   tone  64: flat to ~120 ms, then -4.3 / -12.7 / -20.0 / -28.3
                //             at 140 / 160 / 180 / 200 ms
                //   tone 127: still flat at 220 ms
                //
                // Fitted across seven fixtures at five points each, the hold is
                // exponential in tone and the release is a fixed ~30 ms:
                // 60 ms at tone 0, 134 at tone 64, 297 at tone 127 (past the end of
                // the 276 ms noise recording, so the top of the knob is effectively
                // ungated). Every fixture lands within about 1 dB.
                //
                // Before this the noise ran at fixed gain for its whole length,
                // which left our snare 3 to 7 dB hot through the 100-200 ms tail and
                // ringing audibly longer than ReBirth's at every setting (Luke's
                // ear, 2026-08-25).
                const double holdS = 0.060 * std::exp(1.60 * (double) tone);
                const std::size_t holdN = (std::size_t)(holdS * (double) sr1);
                const float relC = (float) std::exp(-1.0 / (0.030 * (double) sr1));
                float nenv = 1.0f;
                if (nz.size() > body.size()) body.resize(nz.size(), 0.0f);
                for (std::size_t i = 0; i < body.size(); ++i) {
                    float n = 0.0f;
                    if (i < nz.size()) {
                        if (i > holdN) nenv *= relC;
                        n = nz[i] * g * nenv;
                    }
                    body[i] = body[i] * toneBody + n;
                }
                d9->setSample(1, std::move(body), sr1);
                d9->setParamByName("sd_snap", 0.0f);
                engine.sdSnapRepurposed909 = true;   // not the knob now (captureLivePanel)
                ++loaded;
            } else if (!body.empty()) {
                d9->setSample(1, std::move(body), sr1);
                ++loaded;
            }
        }
        loaded += give(d9, 2, dir, "909_lt");
        loaded += give(d9, 3, dir, "909_mt");
        loaded += give(d9, 4, dir, "909_ht");
        loaded += give(d9, 5, dir, "909_rs");
        loaded += give(d9, 6, dir, "909_cp");
        loaded += give(d9, 7, dir, "909_oh");
        loaded += give(d9, 8, dir, "909_ch");
        loaded += give(d9, 9, dir, "909_crash");
        loaded += give(d9, 10, dir, "909_ride");
    }
    if (d8) {
        int sdv = 1 + (int) std::lround(song.drum808Knobs.sdTone * 4.0);
        loaded += give(d8, 0, dir, "808_bd");
        // SD = a tone body variant + the REAL snappy-noise recording, pre-mixed
        // as ReBirth does (the device's synth noise is then muted). The noise
        // gain was RE-FIT 2026-08-23 (round 9) under POWER averaging, which
        // corrected the round-6/7 fit: coherent averaging had cancelled ReBirth's
        // non-aligned snare noise and made it read dark, so the old gain came out
        // ~4x too quiet at mid snappy (our snare a dull thud, centroid 788 vs
        // ReBirth 3913 at snappy 64). Two corrections: the gain is TONE-INDEPENDENT
        // (the tone brightness comes from the body variant sdv, not the noise), and
        // it is an S-curve in snappy fitted to the per-hit centroid across the sweep
        // (needed g 0 / 0.06 / 0.29 / 0.67 / 0.81 at snappy 0 / 32 / 64 / 96 / 127).
        {
            int sr1 = 0, sr2 = 0;
            std::vector<float> body = dir("808_sd" + std::to_string(sdv), &sr1);
            const float snap = song.drum808Knobs.sdSnappy;
            const int ndv = 1 + (int) std::lround(snap * 3.0);   // 1 of 4 noise takes
            std::vector<float> nz = dir("808_n" + std::to_string(ndv), &sr2);
            if (!body.empty() && !nz.empty() && sr1 == sr2) {
                float g = -0.4896f * snap + 2.9215f * snap * snap
                          - 1.6170f * snap * snap * snap;
                if (g < 0.0f) g = 0.0f;                // non-negative near zero
                if (nz.size() > body.size()) body.resize(nz.size(), 0.0f);
                for (std::size_t i = 0; i < nz.size(); ++i) body[i] += nz[i] * g;
                d8->setSample(1, std::move(body), sr1);
                d8->setParamByName("sd_snap", 0.0f);   // mute the device's synth noise
                engine.sdSnapRepurposed808 = true;
                ++loaded;
            } else if (!body.empty()) {
                d8->setSample(1, std::move(body), sr1); ++loaded;
            }
        }
        loaded += give(d8, 2, dir, "808_lt");
        loaded += give(d8, 3, dir, "808_mt");
        loaded += give(d8, 4, dir, "808_ht");
        loaded += give(d8, 5, dir, "808_lc");
        loaded += give(d8, 6, dir, "808_mc");
        loaded += give(d8, 7, dir, "808_hc");
        loaded += give(d8, 8, dir, "808_rs");
        loaded += give(d8, 9, dir, "808_cp");
        loaded += give(d8, 10, dir, "808_ma");
        loaded += give(d8, 11, dir, "808_cb");
        loaded += give(d8, 12, dir, "808_cy");
        loaded += give(d8, 13, dir, "808_oh");
        loaded += give(d8, 14, dir, "808_ch");
        loaded += give(d8, 15, dir, "808_cl");
    }
    return loaded;
}

} // namespace reborn
