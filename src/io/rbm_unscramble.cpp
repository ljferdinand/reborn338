// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Key estimation, the self-check and unscrambling for `.rbm` mods (the key function that
// generates exact keys is in rbm_keygen.cpp). See include/reborn/rbm_unscramble.h and
// docs/FORMAT_RBM.md for what was measured and how.
#include "reborn/rbm_unscramble.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <tuple>

namespace reborn {
namespace {

constexpr std::size_t kReadable = 1024;   // payload bytes stored as written
constexpr int kOrder = 4;                 // linear-prediction order for the audio model
constexpr int kScaleWindow = 65;          // samples in the local residual-scale median
constexpr int kMaxSweeps = 8;             // sweeps per settle before moving on
constexpr int kMaxRounds = 12;            // chain-and-sweep rounds at most
constexpr int kChainR = 2;                // a chain pass shifts each link by m * {-2..2}
constexpr int kChainScales[] = { 16, 4, 1 };
constexpr std::size_t kBeam = 24;         // chain states kept per link
constexpr double kBeamMargin = 40.0;      // and only those within this cost of the best
constexpr long kFocusReach = 12;          // links re-solved either side of one that moved
constexpr int kNear = 8;                  // a re-sweep tries this far either side first
constexpr std::size_t kHead = 32;         // first positions swept alone (every period wraps onto them)
constexpr std::size_t kBlock = 64;        // positions per parallel sweep block
constexpr std::size_t kMaxTwins = 6;      // twin links kept per voice
constexpr std::size_t kMaxTwinPairs = 32; // and in the whole archive (the retail mods keep 3 to 10)
constexpr std::size_t kMaxSnaps = 2048;   // distinct twin-snap proposals tried per round
// A ModPacker mod holds 106 files in a few MB (the retail ones 2.2 to 5.1 MB, at most 2.3
// million audio samples). Past these limits the archive is not one ModPacker made, and the
// key length and the estimator's working memory (about 90 bytes per audio sample) would
// grow with it, so it is refused.
constexpr std::size_t kMaxEntries = 1024;
constexpr std::size_t kMaxImage = std::size_t(32) << 20;
constexpr std::size_t kMaxSamples = std::size_t(8) << 20;
constexpr std::size_t kMaxListed = 65536;  // EMBF entries listed at most, whatever the file holds
constexpr int kMaxDepth = 16;             // IFF containers nested at most
// The self-check (checkUnscrambledRbm). Calibrated on the four retail mods under exact keys
// and under no key, random keys, keys randomised in their top 2, 5 or 20 percent or at
// 100 scattered positions, and keys offset alike along a run (docs/FORMAT_RBM.md).
constexpr double kMinCleanJpegs = 0.9;    // share of judged JPEGs whose scan must be clean
constexpr double kMaxResidualRatio = 4.0; // median residual growth past the readable start
constexpr double kMaxPositionLevel = 4.0; // a key position whose samples sit this far above their surroundings
constexpr double kMaxPositionResidual = 3.5; // or whose prediction residuals sit this far above theirs
constexpr int kMaxBadPositions = 2;       // that many such positions and the key is wrong
constexpr double kLevelFloor = 16.0;      // LSB added to a local level, so silence is not divided by
constexpr double kResidualFloor = 4.0;    // and to a local residual
constexpr double kQuietStart = 64.0;      // mean |sample| below which a readable start cannot anchor a ratio
constexpr int kMinVoices = 12;            // voices with audio past the readable part needed to trust an estimate
constexpr double kWrapJump = 49152.0;     // a step between samples this large past the readable part
constexpr double kWrapReadable = 32768.0; // in a voice whose readable steps all stay below this, is a wrap

// Run f(0..n-1) over the machine's cores. Every call writes only its own item, so the result
// does not depend on how the items are scheduled.
// An exception thrown by any f (say bad_alloc) stops the remaining work, every thread is
// joined, and the first exception is rethrown on the calling thread.
template <class F> void parallelFor(std::size_t n, F f) {
    const std::size_t hw = std::max<std::size_t>(1, std::min<std::size_t>(16, std::thread::hardware_concurrency()));
    const std::size_t workers = std::min(hw, n);
    if (workers < 2) { for (std::size_t i = 0; i < n; ++i) f(i); return; }
    std::atomic<std::size_t> next { 0 };
    std::exception_ptr failure;
    std::mutex failureMutex;
    auto work = [&]() {
        try {
            for (std::size_t i; (i = next.fetch_add(1)) < n;) f(i);
        } catch (...) {
            next.store(n);                                       // the others stop at their next item
            std::lock_guard<std::mutex> g(failureMutex);
            if (!failure) failure = std::current_exception();
        }
    };
    std::vector<std::thread> pool;
    for (std::size_t w = 1; w < workers; ++w) {
        try { pool.emplace_back(work); } catch (...) { break; }   // fewer threads, same result
    }
    work();
    for (std::thread& t : pool) t.join();
    if (failure) std::rethrow_exception(failure);
}

inline uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline std::size_t period(std::size_t i) { return 140 + 17 * i; }

// An AIFF's sample layout, read from the part of its header stored in the clear: where the
// sample data starts in the file, how many bytes the SSND chunk gives it, COMM's frame count,
// channels and bit depth. f is the file, n the bytes of it that may be read.
struct AiffLayout { std::size_t start = 0, dataBytes = 0, frames = 0; int channels = 0, bits = 0; };

bool aiffLayout(const unsigned char* f, std::size_t n, AiffLayout& a) {
    if (n < 12 || std::memcmp(f, "FORM", 4) != 0
        || (std::memcmp(f + 8, "AIFF", 4) != 0 && std::memcmp(f + 8, "AIFC", 4) != 0)) return false;
    std::size_t j = 12;
    a = AiffLayout{};
    while (j + 8 <= n) {
        const uint32_t sz = be32(f + j + 4);
        if (std::memcmp(f + j, "COMM", 4) == 0 && j + 16 <= n) {
            a.channels = (f[j + 8] << 8) | f[j + 9];
            a.frames = be32(f + j + 10);
            a.bits = (f[j + 14] << 8) | f[j + 15];
        }
        if (std::memcmp(f + j, "SSND", 4) == 0) {
            if (j + 16 > n) return false;
            const std::size_t offset = be32(f + j + 8);
            a.start = j + 16 + offset;
            a.dataBytes = std::size_t(sz) >= 8 + offset ? std::size_t(sz) - 8 - offset : 0;
            return a.bits != 0 && a.channels != 0;
        }
        j += 8 + std::size_t(sz) + (sz & 1);   // in size_t: a 32-bit sum can wrap to no step at all
    }
    return false;
}

// A 16-bit mono or stereo voice in an EMBF payload: the offset of its first frame in the
// payload, its frame count (bounded by the payload, the SSND chunk and COMM, as a player
// reads it) and its channels. False for anything else, or a header not in the clear.
struct Voice16 { std::size_t o0 = 0, frames = 0; int channels = 0; };

bool voice16(const unsigned char* payload, const RbmEntry& e, Voice16& v) {
    const std::size_t h = e.nameLength + 1;
    const std::size_t clear = std::min(e.length, kReadable);
    if (h + 12 > clear) return false;
    AiffLayout a;
    if (!aiffLayout(payload + h, clear - h, a) || a.bits != 16 || a.channels < 1 || a.channels > 2) return false;
    v.channels = a.channels;
    v.o0 = h + a.start;
    if (v.o0 >= e.length) return false;
    const std::size_t frame = 2 * std::size_t(a.channels);
    v.frames = std::min({ (e.length - v.o0) / frame, a.dataBytes / frame, a.frames });
    return v.frames > 0;
}

// Any PCM AIFF voice a player would decode (FORM AIFF, 8, 16 or 24 bits), for the check:
// where its first frame is, its frame count, channels and bytes per sample. Each sample's
// first byte is its most significant, and is what a wrong key byte damages most.
struct VoicePcm { std::size_t o0 = 0, frames = 0; int channels = 0, bytes = 0; };

bool voicePcm(const unsigned char* payload, const RbmEntry& e, VoicePcm& v) {
    const std::size_t h = e.nameLength + 1;
    const std::size_t clear = std::min(e.length, kReadable);
    if (h + 12 > clear || std::memcmp(payload + h + 8, "AIFF", 4) != 0) return false;
    AiffLayout a;
    if (!aiffLayout(payload + h, clear - h, a) || a.bits < 1 || a.bits > 24) return false;
    v.channels = a.channels;
    v.bytes = (a.bits + 7) / 8;
    v.o0 = h + a.start;
    if (v.o0 >= e.length || v.channels < 1) return false;
    const std::size_t frame = std::size_t(v.bytes) * std::size_t(v.channels);
    v.frames = std::min({ (e.length - v.o0) / frame, a.dataBytes / frame, a.frames });
    return v.frames > 0;
}

bool isJpeg(const unsigned char* payload, const RbmEntry& e) {
    const std::size_t h = e.nameLength + 1;
    return e.length >= h + 2 && payload[h] == 0xFF && payload[h + 1] == 0xD8;
}

inline double decodeSample(uint8_t hi, uint8_t lo) {
    const int v = (int(hi) << 8) | int(lo);
    return double(v >= 32768 ? v - 65536 : v);
}

// out[t] = median of v over the kScaleWindow samples centred on t, the ends held at the edge
// value. The window is kept sorted and slid one sample at a time.
void runningMedian(const std::vector<double>& v, std::vector<double>& out) {
    const long n = long(v.size());
    out.assign(std::size_t(n), 0.0);
    if (n == 0) return;
    const long half = kScaleWindow / 2;
    auto at = [&](long q) { q = q < 0 ? 0 : (q >= n ? n - 1 : q); return v[std::size_t(q)]; };
    std::vector<double> w;
    w.reserve(kScaleWindow);
    for (long k = -half; k <= half; ++k) w.push_back(at(k));
    std::sort(w.begin(), w.end());
    for (long t = 0; t < n; ++t) {
        out[std::size_t(t)] = w[std::size_t(half)];
        if (t + 1 == n) break;
        const double gone = at(t - half), come = at(t + half + 1);
        w.erase(std::lower_bound(w.begin(), w.end(), gone));
        w.insert(std::upper_bound(w.begin(), w.end(), come), come);
    }
}

// Least-squares order-kOrder linear predictor for x[0..n): x[t] ~ sum a[k] x[t-k-1].
// Left at zero when there is too little signal to fit.
void fitPredictor(const double* x, std::size_t n, std::array<double, kOrder>& a) {
    a.fill(0.0);
    if (n <= kOrder + 10) return;
    double A[kOrder][kOrder + 1] = {};
    for (std::size_t t = kOrder; t < n; ++t)
        for (int r = 0; r < kOrder; ++r) {
            const double xr = x[t - std::size_t(r) - 1];
            for (int c = 0; c < kOrder; ++c) A[r][c] += xr * x[t - std::size_t(c) - 1];
            A[r][kOrder] += xr * x[t];
        }
    for (int col = 0; col < kOrder; ++col) {             // Gaussian elimination, partial pivot
        int piv = col;
        for (int r = col + 1; r < kOrder; ++r) if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        if (std::fabs(A[piv][col]) < 1e-9) return;
        if (piv != col) for (int c = 0; c <= kOrder; ++c) std::swap(A[piv][c], A[col][c]);
        for (int r = 0; r < kOrder; ++r) {
            if (r == col) continue;
            const double f = A[r][col] / A[col][col];
            for (int c = col; c <= kOrder; ++c) A[r][c] -= f * A[col][c];
        }
    }
    for (int r = 0; r < kOrder; ++r) a[std::size_t(r)] = A[r][kOrder] / A[r][r];
}

// A voice stored twice, or once scaled: this voice is about g times that one, sample for sample.
struct Twin { int par; int32_t stream; double g; };

// One 16-bit channel of a voice: per sample, its two stored bytes and their key positions
// (-1 where the byte lies in the readable part).
struct Stream {
    int par = 0;
    std::vector<uint8_t> hiC, loC;
    std::vector<int32_t> hiP, loP;
    std::size_t plainSamples = 0;     // samples wholly inside the readable part
    std::vector<uint8_t> ph, pl;      // decoded high and low bytes under the current key
    std::vector<double> x, e, sc;     // decoded samples, prediction residual, local scale
    std::vector<double> xs;           // x as it stood when a parallel sweep phase began
    std::array<double, kOrder> a {};  // fitted predictor
    std::vector<Twin> twins;
};

struct Use { int32_t stream; int32_t sample; uint8_t role; };   // role 0 = high byte

class KeyEstimator {
public:
    KeyEstimator(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries)
        : img_(image.data()), entries_(entries) {}

    bool run(RbmUnscrambleReport& rep);
    std::vector<uint8_t> key[2];

private:
    const unsigned char* img_;
    const std::vector<RbmEntry>& entries_;
    std::size_t lmax_[2] = { 0, 0 };
    std::vector<Stream> streams_[2];
    std::vector<std::vector<Use>> uses_[2];
    std::vector<uint8_t> dirty_[2];   // positions whose neighbourhood changed since their last sweep
    std::vector<uint8_t> moved_[2];   // positions changed this round, around which chains are re-solved
    std::vector<std::vector<std::pair<int32_t, int32_t>>> bucket_[2][2];   // per chain link, its residuals
    bool twinsOn_ = false;
    bool snap_ = false;               // twins read from xs: other threads may be writing x

    void initialise(RbmUnscrambleReport& rep);
    bool buildStreams();
    void buildBuckets();
    void findTwins();
    int confirmTwins();

    uint8_t plain(const Stream& s, std::size_t t, int role) const {
        const int32_t p = role == 0 ? s.hiP[t] : s.loP[t];
        const uint8_t c = role == 0 ? s.hiC[t] : s.loC[t];
        return (p >= 0 && std::size_t(p) < key[s.par].size()) ? uint8_t(c - key[s.par][std::size_t(p)]) : c;
    }
    // Sample t with the key byte at (par, p) moved by d, where the stream uses it.
    double sampleShifted(const Stream& s, std::size_t t, int par, int32_t p, int d) const {
        uint8_t hi = s.ph[t], lo = s.pl[t];
        if (s.par == par) {
            if (s.hiP[t] == p) hi = uint8_t(hi - d);
            if (s.loP[t] == p) lo = uint8_t(lo - d);
        }
        return decodeSample(hi, lo);
    }
    void decode(Stream& s) {
        const std::size_t n = s.hiC.size(); s.x.resize(n); s.ph.resize(n); s.pl.resize(n);
        for (std::size_t t = 0; t < n; ++t) {
            s.ph[t] = plain(s, t, 0); s.pl[t] = plain(s, t, 1); s.x[t] = decodeSample(s.ph[t], s.pl[t]);
        }
    }
    void residuals(Stream& s) {
        const std::size_t n = s.x.size(); s.e = s.x;
        for (int k = 0; k < kOrder; ++k)
            for (std::size_t t = std::size_t(k) + 1; t < n; ++t) s.e[t] -= s.a[std::size_t(k)] * s.x[t - std::size_t(k) - 1];
    }
    double residualAt(const Stream& s, std::size_t t) const {
        double e = s.x[t];
        for (int k = 0; k < kOrder; ++k)
            if (t >= std::size_t(k + 1)) e -= s.a[std::size_t(k)] * s.x[t - std::size_t(k) - 1];
        return e;
    }
    void fit(Stream& s);
    void scale(Stream& s);
    // Decode every voice under the current key; with refit, also refit its predictor and scale.
    void decodeAll(bool refit) {
        const std::size_t n0 = streams_[0].size();
        parallelFor(n0 + streams_[1].size(), [&](std::size_t i) {
            Stream& s = i < n0 ? streams_[0][i] : streams_[1][i - n0];
            decode(s); if (refit) fit(s); residuals(s); if (refit) scale(s);
        });
    }
    // The twin terms one sample of a voice takes part in, if its value were v while the key
    // byte at (par, p) moved by d. A twin that shares that key byte at the same sample moves
    // with it, and then its own use counts the reverse link, so each link is counted once.
    double twinCost(const Stream& s, std::size_t t, double v, int par, int32_t p, int d) const {
        double c = 0.0;
        for (const Twin& tw : s.twins) {
            const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
            const bool shared = o.par == par && (o.hiP[t] == p || o.loP[t] == p);
            const double w = shared ? sampleShifted(o, t, par, p, d) : (snap_ ? o.xs[t] : o.x[t]);
            c += std::fabs(v - tw.g * w);
            if (!shared) c += std::fabs(w - v / tw.g);
        }
        return c;
    }
    // The whole objective: prediction residuals against their local scale, plus twin links.
    double totalCost() {
        decodeAll(false);
        double c = 0.0;
        for (int par = 0; par < 2; ++par)
            for (const Stream& s : streams_[par]) {
                const std::size_t n = s.x.size();
                for (std::size_t t = 0; t < n; ++t) c += std::fabs(s.e[t]) / s.sc[t];
                if (twinsOn_)
                    for (const Twin& tw : s.twins) {
                        const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
                        for (std::size_t t = 0; t < n; ++t) c += std::fabs(s.x[t] - tw.g * o.x[t]);
                    }
            }
        return c;
    }
    // A key byte changed: everything within the predictor's reach of its samples, and the
    // samples' twins, must be looked at again.
    void markAround(int par, std::size_t p) {
        moved_[par][p] = 1;
        for (const Use& u : uses_[par][p]) {
            const Stream& s = streams_[par][std::size_t(u.stream)];
            const long t0 = u.sample, n = long(s.hiC.size());
            for (long t = std::max(0L, t0 - kOrder); t <= std::min(n - 1, t0 + kOrder); ++t) {
                if (s.hiP[std::size_t(t)] >= 0) dirty_[par][std::size_t(s.hiP[std::size_t(t)])] = 1;
                if (s.loP[std::size_t(t)] >= 0) dirty_[par][std::size_t(s.loP[std::size_t(t)])] = 1;
            }
            for (const Twin& tw : s.twins) {
                const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
                if (o.hiP[std::size_t(t0)] >= 0) dirty_[tw.par][std::size_t(o.hiP[std::size_t(t0)])] = 1;
                if (o.loP[std::size_t(t0)] >= 0) dirty_[tw.par][std::size_t(o.loP[std::size_t(t0)])] = 1;
            }
        }
    }
    // Re-decode the samples a key byte feeds, and the residuals they reach.
    void refreshPosition(int par, std::size_t p) {
        for (const Use& u : uses_[par][p]) {
            Stream& s = streams_[par][std::size_t(u.stream)];
            const std::size_t t0 = std::size_t(u.sample), n = s.x.size();
            s.ph[t0] = plain(s, t0, 0); s.pl[t0] = plain(s, t0, 1); s.x[t0] = decodeSample(s.ph[t0], s.pl[t0]);
            for (std::size_t t = t0; t <= t0 + kOrder && t < n; ++t) s.e[t] = residualAt(s, t);
        }
    }
    void costAt(int par, std::size_t p, const int* ds, int nd, double* cost);
    bool sweepOne(int par, std::size_t p, bool full);
    int sweep(int par, bool full);
    double localCost(int par, const std::vector<std::size_t>& ps);
    int twinSnap();
    std::vector<int> chainSolve(int par, int c, int m, const std::vector<uint8_t>* focus);
    int chainApply(int par, int c, int m, const std::vector<int>& delta);
};

void KeyEstimator::fit(Stream& s) { fitPredictor(s.x.data(), s.x.size(), s.a); }

// Local scale: the running median of |residual|, floored at 2 LSB.
void KeyEstimator::scale(Stream& s) {
    std::vector<double> mag(s.e.size());
    for (std::size_t t = 0; t < mag.size(); ++t) mag[t] = std::fabs(s.e[t]);
    runningMedian(mag, s.sc);
    for (double& v : s.sc) v = std::max(v, 2.0);
}

// 1. Start: the most common stored byte at each key position, read as plain 0 (much of a
//    mod is silence and zero runs). JPEG end markers are left out on purpose: the audio
//    evidence alone gives the same keys on all four retail mods, a file may carry bytes
//    after its FF D9, and an unforced end marker stays free to confirm the result.
void KeyEstimator::initialise(RbmUnscrambleReport& rep) {
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].length > kReadable) {
            ++rep.scrambled;
            lmax_[i % 2] = std::max(lmax_[i % 2], std::min(period(i), entries_[i].length - kReadable));
        }
    for (int par = 0; par < 2; ++par) {
        std::vector<std::array<uint32_t, 256>> votes(lmax_[par]);
        for (auto& v : votes) v.fill(0);
        for (std::size_t i = std::size_t(par); i < entries_.size(); i += 2) {
            const RbmEntry& e = entries_[i]; const std::size_t L = period(i);
            for (std::size_t o = kReadable; o < e.length; ++o) ++votes[(o - kReadable) % L][img_[e.payload + o]];
        }
        key[par].resize(lmax_[par]);
        for (std::size_t p = 0; p < lmax_[par]; ++p)
            key[par][p] = (uint8_t) (std::max_element(votes[p].begin(), votes[p].end()) - votes[p].begin());
    }
}

// 2. Audio evidence: every channel of every 16-bit AIFF voice whose sample data begins in the
//    readable part. Mono and stereo; wider voices are left out.
bool KeyEstimator::buildStreams() {
    std::size_t total = 0;                                  // audio samples, counted before any is stored
    for (const RbmEntry& e : entries_) {
        Voice16 v;
        if (e.length > kReadable && voice16(img_ + e.payload, e, v)) total += v.frames * std::size_t(v.channels);
    }
    if (total > kMaxSamples) return false;
    for (int par = 0; par < 2; ++par) uses_[par].resize(lmax_[par]);
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const RbmEntry& e = entries_[i];
        Voice16 v;
        if (e.length <= kReadable || !voice16(img_ + e.payload, e, v) || v.frames < 8) continue;
        const int channels = v.channels;
        const std::size_t o0 = v.o0, frame = 2 * std::size_t(channels), ns = v.frames;
        const std::size_t L = period(i); const int par = int(i % 2);
        for (int ch = 0; ch < channels; ++ch) {
            Stream s; s.par = par;
            s.hiC.resize(ns); s.loC.resize(ns); s.hiP.resize(ns); s.loP.resize(ns);
            for (std::size_t t = 0; t < ns; ++t) {
                const std::size_t oh = o0 + frame * t + 2 * std::size_t(ch), ol = oh + 1;
                s.hiC[t] = img_[e.payload + oh]; s.loC[t] = img_[e.payload + ol];
                s.hiP[t] = oh >= kReadable ? int32_t((oh - kReadable) % L) : -1;
                s.loP[t] = ol >= kReadable ? int32_t((ol - kReadable) % L) : -1;
                if (s.hiP[t] < 0 && s.loP[t] < 0) s.plainSamples = t + 1;
            }
            const int32_t si = (int32_t) streams_[par].size();
            for (std::size_t t = 0; t < ns; ++t) {
                if (s.hiP[t] >= 0) uses_[par][std::size_t(s.hiP[t])].push_back({ si, int32_t(t), 0 });
                if (s.loP[t] >= 0) uses_[par][std::size_t(s.loP[t])].push_back({ si, int32_t(t), 1 });
            }
            streams_[par].push_back(std::move(s));
        }
    }
    return true;
}

// Each residual under the highest link of each chain it depends on. Fixed by the layout.
void KeyEstimator::buildBuckets() {
    for (int par = 0; par < 2; ++par)
        for (int c = 0; c < 2; ++c) {
            const long nsteps = (long(lmax_[par]) - c + 1) / 2;
            bucket_[par][c].assign(std::size_t(std::max(0L, nsteps)), {});
            for (std::size_t si = 0; si < streams_[par].size(); ++si) {
                const Stream& s = streams_[par][si]; const std::size_t n = s.hiC.size();
                for (std::size_t t = 0; t < n; ++t) {
                    long km = -1;
                    for (int j = 0; j <= kOrder && std::size_t(j) <= t; ++j)
                        for (int role = 0; role < 2; ++role) {
                            const int32_t q = role ? s.loP[t - std::size_t(j)] : s.hiP[t - std::size_t(j)];
                            if (q >= 0 && (q & 1) == c) km = std::max(km, long((q - c) / 2));
                        }
                    if (km >= 0) bucket_[par][c][std::size_t(km)].push_back({ int32_t(si), int32_t(t) });
                }
            }
        }
}

// Candidate twins: voices of equal length whose readable samples agree to rounding after
// one gain. Mods duplicate voices freely (all four retail mods do), and the duplicates sit
// at the top of the archive, where little else constrains the key.
void KeyEstimator::findTwins() {
    struct Ref { int par; int32_t si; };
    std::vector<Ref> all;
    for (int par = 0; par < 2; ++par)
        for (std::size_t si = 0; si < streams_[par].size(); ++si) all.push_back({ par, int32_t(si) });
    std::size_t pairs = 0;
    for (std::size_t i = 0; i < all.size() && pairs < kMaxTwinPairs; ++i)
        for (std::size_t j = i + 1; j < all.size() && pairs < kMaxTwinPairs; ++j) {
            Stream& A = streams_[all[i].par][std::size_t(all[i].si)];
            Stream& B = streams_[all[j].par][std::size_t(all[j].si)];
            if (A.hiC.size() != B.hiC.size() || A.twins.size() >= kMaxTwins || B.twins.size() >= kMaxTwins) continue;
            const std::size_t np = std::min(A.plainSamples, B.plainSamples);
            if (np < 64) continue;
            double ab = 0.0, bb = 0.0, aa = 0.0;
            for (std::size_t t = 0; t < np; ++t) {
                const double a = decodeSample(A.hiC[t], A.loC[t]), b = decodeSample(B.hiC[t], B.loC[t]);
                ab += a * b; bb += b * b; aa += a * a;
            }
            if (aa < double(np) * 64.0 * 64.0 || bb < double(np) * 64.0 * 64.0) continue;   // too quiet to tell
            const double g = ab / bb;
            if (!(g > 0.2 && g < 5.0)) continue;
            double worst = 0.0;
            for (std::size_t t = 0; t < np && worst <= 2.5; ++t)
                worst = std::max(worst, std::fabs(decodeSample(A.hiC[t], A.loC[t]) - g * decodeSample(B.hiC[t], B.loC[t])));
            if (worst > 2.5) continue;
            A.twins.push_back({ all[j].par, all[j].si, g });
            B.twins.push_back({ all[i].par, all[i].si, 1.0 / g });
            ++pairs;
        }
}

// Keep a twin only if the whole voice agrees with it once the well-evidenced part of the key
// has settled, so two voices that merely share an attack are not tied together. Returns
// the number of twin pairs kept.
int KeyEstimator::confirmTwins() {
    for (int par = 0; par < 2; ++par)
        for (Stream& s : streams_[par]) {
            std::vector<Twin> keep;
            for (const Twin& tw : s.twins) {
                const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
                std::size_t agree = 0; const std::size_t n = s.x.size();
                for (std::size_t t = 0; t < n; ++t) agree += std::fabs(s.x[t] - tw.g * o.x[t]) <= 3.0;
                if (agree * 10 >= n * 8) keep.push_back(tw);
            }
            s.twins.swap(keep);
        }
    for (int par = 0; par < 2; ++par)                       // a link stays only if both ends kept it
        for (std::size_t si = 0; si < streams_[par].size(); ++si) {
            Stream& s = streams_[par][si];
            s.twins.erase(std::remove_if(s.twins.begin(), s.twins.end(), [&](const Twin& tw) {
                const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
                for (const Twin& b : o.twins) if (b.par == par && b.stream == int32_t(si)) return false;
                return true;
            }), s.twins.end());
        }
    int links = 0;
    for (int par = 0; par < 2; ++par) for (const Stream& s : streams_[par]) links += int(s.twins.size());
    return links / 2;
}

// 3a. Per-position sweep: each key byte set to make the decoded audio most predictable.
//     The cost of moving the byte at (par, p) by each of ds[0..nd).
void KeyEstimator::costAt(int par, std::size_t p, const int* ds, int nd, double* cost) {
    double dv[256];
    std::fill(cost, cost + nd, 0.0);
    for (const Use& u : uses_[par][p]) {
        const Stream& s = streams_[par][std::size_t(u.stream)];
        const std::size_t t0 = std::size_t(u.sample), n = s.x.size();
        const uint8_t hi0 = s.ph[t0], lo0 = s.pl[t0];
        const double x0 = s.x[t0];
        if (u.role == 0) for (int i = 0; i < nd; ++i) dv[i] = decodeSample(uint8_t(hi0 - ds[i]), lo0) - x0;
        else             for (int i = 0; i < nd; ++i) dv[i] = decodeSample(hi0, uint8_t(lo0 - ds[i])) - x0;
        const double coef[kOrder + 1] = { 1.0, -s.a[0], -s.a[1], -s.a[2], -s.a[3] };
        for (int j = 0; j <= kOrder && t0 + std::size_t(j) < n; ++j) {
            const std::size_t t = t0 + std::size_t(j);
            const double e = s.e[t], c = coef[j], w = 1.0 / s.sc[t];
            for (int i = 0; i < nd; ++i) cost[i] += std::fabs(e + c * dv[i]) * w;
        }
        if (twinsOn_ && !s.twins.empty())
            for (int i = 0; i < nd; ++i) cost[i] += twinCost(s, t0, x0 + dv[i], par, int32_t(p), ds[i]);
    }
}

// A full sweep tries all 256 values of a byte. Otherwise it is tried within kNear of its
// value, and over all 256 only when the best lies at that window's edge.
bool KeyEstimator::sweepOne(int par, std::size_t p, bool full) {
    static const struct Cands {
        int all[256], near[2 * kNear + 1];
        Cands() { for (int d = 0; d < 256; ++d) all[d] = d; for (int i = 0; i <= 2 * kNear; ++i) near[i] = i - kNear; }
    } cands;
    if (!dirty_[par][p]) return false;
    dirty_[par][p] = 0;
    if (uses_[par][p].empty()) return false;
    double cost[256];
    int best = 0;
    bool wide = full;
    if (!wide) {
        costAt(par, p, cands.near, 2 * kNear + 1, cost);
        int bi = kNear;
        for (int i = 0; i <= 2 * kNear; ++i) if (cost[i] < cost[bi]) bi = i;
        best = cands.near[bi];
        wide = bi == 0 || bi == 2 * kNear;
    }
    if (wide) {
        costAt(par, p, cands.all, 256, cost);
        best = 0;
        for (int d = 1; d < 256; ++d) if (cost[d] < cost[best]) best = d;
    }
    if (best == 0) return false;
    key[par][p] = uint8_t(key[par][p] + best);
    refreshPosition(par, p);
    return true;
}

// One sweep over a parity. A key byte reaches the residuals of samples within the predictor's
// order of its own, at most seventeen positions either side for a stereo voice, so blocks of
// kBlock positions two apart touch disjoint data and run in parallel, even blocks then odd.
// Every period wraps onto the first positions, so those go first and alone. Twins are read
// from a snapshot and the follow-up marks are applied after each phase, so the result does
// not depend on how the threads are scheduled.
int KeyEstimator::sweep(int par, bool full) {
    const std::size_t n = lmax_[par];
    int changed = 0;
    for (std::size_t p = 0; p < std::min(n, kHead); ++p)
        if (sweepOne(par, p, full)) { ++changed; markAround(par, p); }
    if (n <= kHead) return changed;
    const std::size_t nb = (n - kHead + kBlock - 1) / kBlock;
    for (int phase = 0; phase < 2; ++phase) {
        if (twinsOn_)
            for (int q = 0; q < 2; ++q) for (Stream& st : streams_[q]) if (!st.twins.empty()) st.xs = st.x;
        snap_ = true;
        std::vector<std::vector<std::size_t>> took(nb);
        parallelFor((nb + 1 - std::size_t(phase)) / 2, [&](std::size_t i) {
            const std::size_t b = 2 * i + std::size_t(phase);
            for (std::size_t p = kHead + b * kBlock; p < std::min(n, kHead + (b + 1) * kBlock); ++p)
                if (sweepOne(par, p, full)) took[b].push_back(p);
        });
        snap_ = false;
        for (const auto& tb : took) for (std::size_t p : tb) { ++changed; markAround(par, p); }
    }
    return changed;
}

// The cost terms a set of key positions (one parity) reaches: every residual within the
// predictor's reach of a sample using them, and every twin link at those samples.
double KeyEstimator::localCost(int par, const std::vector<std::size_t>& ps) {
    std::vector<std::pair<int32_t, int32_t>> res, smp;
    for (std::size_t p : ps)
        for (const Use& u : uses_[par][p]) {
            const std::size_t n = streams_[par][std::size_t(u.stream)].x.size();
            smp.push_back({ u.stream, u.sample });
            for (std::size_t t = std::size_t(u.sample); t <= std::size_t(u.sample) + kOrder && t < n; ++t)
                res.push_back({ u.stream, int32_t(t) });
        }
    std::sort(res.begin(), res.end()); res.erase(std::unique(res.begin(), res.end()), res.end());
    std::sort(smp.begin(), smp.end()); smp.erase(std::unique(smp.begin(), smp.end()), smp.end());
    double c = 0.0;
    for (const auto& r : res) {
        const Stream& st = streams_[par][std::size_t(r.first)];
        c += std::fabs(st.e[std::size_t(r.second)]) / st.sc[std::size_t(r.second)];
    }
    if (twinsOn_)
        for (const auto& q : smp) {             // both directions of each link, once
            const Stream& st = streams_[par][std::size_t(q.first)]; const std::size_t t = std::size_t(q.second);
            for (const Twin& tw : st.twins) {
                const Stream& o = streams_[tw.par][std::size_t(tw.stream)];
                const bool both = tw.par == par
                    && std::binary_search(smp.begin(), smp.end(), std::make_pair(tw.stream, q.second));
                c += std::fabs(st.x[t] - tw.g * o.x[t]);
                if (!both) c += std::fabs(o.x[t] - st.x[t] / tw.g);
            }
        }
    return c;
}

// 3c. Twin snap. Where a voice has twins, their consensus says what each of its samples
//     should be to within rounding, so propose the key pair that decodes the sample to it.
//     This reaches a carry error, a high byte one out and a low byte compensating, which no
//     single-byte move and no single-chain move can undo.
int KeyEstimator::twinSnap() {
    // Every disagreeing sample proposes a key pair. Identical copies disagree in the same way
    // at every sample a wrong byte reaches, so proposals are merged, ranked by how many
    // samples ask for them, and at most kMaxSnaps are tried, whatever the archive holds.
    struct Snap { int par; uint32_t qh, ql; uint8_t kh, kl; uint32_t votes; };
    std::vector<Snap> props;
    for (int par = 0; par < 2; ++par)
        for (const Stream& s : streams_[par]) {
            if (s.twins.empty()) continue;
            const std::size_t n = s.x.size();
            for (std::size_t t = s.plainSamples; t < n; ++t) {
                double y = 0.0;
                for (const Twin& tw : s.twins) y += tw.g * streams_[tw.par][std::size_t(tw.stream)].x[t];
                y /= double(s.twins.size());
                if (std::fabs(s.x[t] - y) <= 3.0 || std::fabs(y) > 32767.0) continue;
                const int32_t ph = s.hiP[t], pl = s.loP[t];
                if (ph < 0 || pl < 0) continue;
                const uint16_t Y = uint16_t(int(std::lround(y)) & 0xFFFF);
                const uint8_t kh = uint8_t(s.hiC[t] - (Y >> 8)), kl = uint8_t(s.loC[t] - (Y & 0xFF));
                if (key[par][std::size_t(ph)] == kh && key[par][std::size_t(pl)] == kl) continue;
                props.push_back({ par, uint32_t(ph), uint32_t(pl), kh, kl, 1 });
            }
        }
    auto order = [](const Snap& a, const Snap& b) {
        return std::tie(a.par, a.qh, a.ql, a.kh, a.kl) < std::tie(b.par, b.qh, b.ql, b.kh, b.kl);
    };
    std::sort(props.begin(), props.end(), order);
    std::vector<Snap> merged;
    for (const Snap& sp : props) {
        if (!merged.empty() && !order(merged.back(), sp)) ++merged.back().votes;
        else merged.push_back(sp);
    }
    std::stable_sort(merged.begin(), merged.end(), [](const Snap& a, const Snap& b) { return a.votes > b.votes; });
    if (merged.size() > kMaxSnaps) merged.resize(kMaxSnaps);
    int taken = 0;
    for (const Snap& sp : merged) {
        const int par = sp.par;
        const std::size_t qh = sp.qh, ql = sp.ql;
        if (key[par][qh] == sp.kh && key[par][ql] == sp.kl) continue;
        const std::vector<std::size_t> ps = { qh, ql };
        const double before = localCost(par, ps);
        const uint8_t oh = key[par][qh], ol = key[par][ql];
        key[par][qh] = sp.kh; key[par][ql] = sp.kl;
        refreshPosition(par, qh); refreshPosition(par, ql);
        if (localCost(par, ps) < before - 1e-9) {
            ++taken; markAround(par, qh); markAround(par, ql);
        } else {
            key[par][qh] = oh; key[par][ql] = ol;
            refreshPosition(par, qh); refreshPosition(par, ql);
        }
    }
    return taken;
}

// 3b. Chain pass. Every other key position is the same byte of consecutive samples in every
//     voice, so one parity of positions forms a chain, and an order-4 predictor couples only
//     five neighbouring links of it. A beam search over a shift of m * {-R..R} on every link
//     at once finds the best joint move, which a run of samples offset alike needs: such a
//     run costs only at its two ends, so no single-position move can undo it. Read-only, so
//     the two parities are solved side by side.
std::vector<int> KeyEstimator::chainSolve(int par, int c, int m, const std::vector<uint8_t>* focus) {
    constexpr int R = kChainR, S = 2 * R + 1, S4 = S * S * S * S;
    const long nsteps = long(bucket_[par][c].size());
    if (nsteps <= 0) return {};
    const auto& bucket = bucket_[par][c];
    // Links free to move: every one, or those near a position in focus.
    std::vector<uint8_t> active(static_cast<std::size_t>(nsteps), focus ? 0 : 1);
    if (focus) {
        for (std::size_t p = 0; p < focus->size(); ++p) {
            if (!(*focus)[p]) continue;
            const long k = (long(p) - c) / 2;
            for (long kk = std::max(0L, k - kFocusReach); kk <= std::min(nsteps - 1, k + kFocusReach); ++kk)
                active[std::size_t(kk)] = 1;
        }
        if (std::find(active.begin(), active.end(), 1) == active.end()) return {};
    }
    // The best few joint shifts of the last four links, extended a link at a time.
    struct Node { int code; double cost; int prev; };    // code: shifts of links k-3..k, base S
    std::vector<std::vector<Node>> beams(static_cast<std::size_t>(nsteps));
    int zero4 = 0; for (int d = 0; d < 4; ++d) zero4 = zero4 * S + R;
    std::vector<Node> prevBeam = { { zero4, 0.0, -1 } };
    std::vector<double> tab(std::size_t(5 * S)), unary(static_cast<std::size_t>(S)), cand;
    std::vector<int> dig;                                // per candidate: digits for links k..k-4
    std::vector<int> slot(static_cast<std::size_t>(S4), -1);
    for (long k = 0; k < nsteps; ++k) {
        const int32_t q = int32_t(c + 2 * k);
        const bool free = active[std::size_t(k)] && std::size_t(q) < lmax_[par] && !uses_[par][std::size_t(q)].empty();
        const int nd = free ? S : 1;
        const std::size_t nc = prevBeam.size() * std::size_t(nd);
        cand.assign(nc, 0.0); dig.resize(nc * 5);
        for (std::size_t b = 0; b < prevBeam.size(); ++b)
            for (int j = 0; j < nd; ++j) {
                const std::size_t ci = b * std::size_t(nd) + std::size_t(j);
                const int o = prevBeam[b].code;
                dig[ci * 5 + 0] = free ? j : R;
                dig[ci * 5 + 1] = o % S; dig[ci * 5 + 2] = (o / S) % S;
                dig[ci * 5 + 3] = (o / (S * S)) % S; dig[ci * 5 + 4] = o / (S * S * S);
            }
        bool reach = false;                              // a residual here depends on a free link
        for (long d = 0; d <= 4 && d <= k; ++d) reach = reach || active[std::size_t(k - d)];
        if (reach)
            for (const auto& st : bucket[std::size_t(k)]) {
                const Stream& s = streams_[par][std::size_t(st.first)]; const std::size_t t = std::size_t(st.second);
                std::fill(tab.begin(), tab.end(), 0.0);
                const double coef[kOrder + 1] = { 1.0, -s.a[0], -s.a[1], -s.a[2], -s.a[3] };
                for (int j = 0; j <= kOrder && std::size_t(j) <= t; ++j) {
                    const std::size_t tj = t - std::size_t(j);
                    for (int role = 0; role < 2; ++role) {
                        const int32_t qq = role ? s.loP[tj] : s.hiP[tj];
                        if (qq < 0 || (qq & 1) != c) continue;
                        const long d = k - long((qq - c) / 2);
                        if (d < 0 || d > 4) continue;            // beyond the window: held where it is
                        for (int dl = -R; dl <= R; ++dl)
                            tab[std::size_t(d * S + dl + R)] += coef[j] * (sampleShifted(s, tj, par, qq, m * dl) - s.x[tj]);
                    }
                }
                const double e0 = s.e[t], w = 1.0 / s.sc[t];
                for (std::size_t ci = 0; ci < nc; ++ci) {
                    const int* g = &dig[ci * 5];
                    cand[ci] += std::fabs(e0 + tab[std::size_t(g[0])] + tab[std::size_t(S + g[1])] + tab[std::size_t(2 * S + g[2])]
                                          + tab[std::size_t(3 * S + g[3])] + tab[std::size_t(4 * S + g[4])]) * w;
                }
            }
        if (twinsOn_ && free) {                          // twin terms depend on this link alone
            std::fill(unary.begin(), unary.end(), 0.0);
            for (const Use& u : uses_[par][std::size_t(q)]) {
                const Stream& s = streams_[par][std::size_t(u.stream)];
                if (s.twins.empty()) continue;
                const std::size_t t = std::size_t(u.sample);
                for (int dl = -R; dl <= R; ++dl)
                    unary[std::size_t(dl + R)] += twinCost(s, t, sampleShifted(s, t, par, q, m * dl), par, q, m * dl);
            }
            for (std::size_t ci = 0; ci < nc; ++ci) cand[ci] += unary[std::size_t(dig[ci * 5])];
        }
        std::vector<Node>& beam = beams[std::size_t(k)];
        beam.clear();
        for (std::size_t ci = 0; ci < nc; ++ci) {
            const std::size_t b = ci / std::size_t(nd);
            const int d0 = dig[ci * 5];
            const int code = (prevBeam[b].code * S + d0) % S4;
            const double v = prevBeam[b].cost + cand[ci] + 1e-6 * std::abs(d0 - R);   // ties go to no move
            int& sl = slot[std::size_t(code)];
            if (sl < 0) { sl = int(beam.size()); beam.push_back({ code, v, int(b) }); }
            else if (v < beam[std::size_t(sl)].cost) beam[std::size_t(sl)] = { code, v, int(b) };
        }
        for (const Node& nd2 : beam) slot[std::size_t(nd2.code)] = -1;
        if (beam.size() > kBeam) {
            std::nth_element(beam.begin(), beam.begin() + long(kBeam), beam.end(),
                             [](const Node& a, const Node& b) { return a.cost < b.cost; });
            beam.resize(kBeam);
        }
        // Where the evidence is strong one state runs far ahead; keep only those near it.
        double lead = beam[0].cost;
        for (const Node& nd2 : beam) lead = std::min(lead, nd2.cost);
        beam.erase(std::remove_if(beam.begin(), beam.end(), [&](const Node& a) { return a.cost > lead + kBeamMargin; }),
                   beam.end());
        prevBeam = beam;
    }
    std::vector<int> delta(static_cast<std::size_t>(nsteps));
    std::size_t bi = 0;
    const std::vector<Node>& last = beams[std::size_t(nsteps - 1)];
    for (std::size_t b = 1; b < last.size(); ++b) if (last[b].cost < last[bi].cost) bi = b;
    for (long k = nsteps - 1; k >= 0; --k) {
        const Node& nd2 = beams[std::size_t(k)][bi];
        delta[std::size_t(k)] = nd2.code % S - R;
        bi = std::size_t(nd2.prev);
    }
    return delta;
}

// Apply a solved chain move, and keep it only if the whole cost falls.
int KeyEstimator::chainApply(int par, int c, int m, const std::vector<int>& delta) {
    const long nsteps = long(delta.size());
    int moved = 0;
    for (long k = 0; k < nsteps; ++k) moved += delta[std::size_t(k)] != 0;
    if (!moved) return 0;
    const double before = totalCost();
    const std::vector<uint8_t> saved = key[par];
    for (long k = 0; k < nsteps; ++k)
        if (delta[std::size_t(k)])
            key[par][std::size_t(c + 2 * k)] = uint8_t(key[par][std::size_t(c + 2 * k)] + m * delta[std::size_t(k)]);
    if (totalCost() > before) { key[par] = saved; decodeAll(false); return 0; }
    for (long k = 0; k < nsteps; ++k) if (delta[std::size_t(k)]) markAround(par, std::size_t(c + 2 * k));
    return moved;
}

bool KeyEstimator::run(RbmUnscrambleReport& rep) {
    initialise(rep);
    if (!buildStreams()) { rep.tooLarge = true; return false; }
    rep.audioStreams = int(streams_[0].size() + streams_[1].size());
    findTwins();
    buildBuckets();
    for (int par = 0; par < 2; ++par) { dirty_[par].assign(lmax_[par], 1); moved_[par].assign(lmax_[par], 0); }

    // Sweeps until nothing moves. The first ever sweep tries every value of every byte, and
    // the model is refitted between the opening sweeps while the decode is changing fast.
    auto settle = [&](bool opening) {
        int moved = 0;
        for (int sw = 0; sw < kMaxSweeps; ++sw) {
            if (opening) decodeAll(true);
            const bool full = opening && sw == 0;
            const int changed = sweep(0, full) + sweep(1, full);
            rep.sweeps += 1; moved += changed;
            if (!changed) break;
        }
        return moved;
    };
    settle(true);
    decodeAll(true);
    rep.twins = confirmTwins();
    twinsOn_ = rep.twins > 0;

    // Rounds of chain passes, coarse shifts to fine, then twin snaps and sweeps. After the
    // first round only the neighbourhoods of what moved are solved again.
    std::vector<uint8_t> focus[2];
    for (int round = 0; round < kMaxRounds; ++round) {
        int moved = 0;
        for (int par = 0; par < 2; ++par) { focus[par] = moved_[par]; std::fill(moved_[par].begin(), moved_[par].end(), 0); }
        decodeAll(true);
        for (int m : kChainScales)
            for (int c = 0; c < 2; ++c) {
                std::vector<int> delta[2];
                parallelFor(2, [&](std::size_t par) {
                    delta[par] = chainSolve(int(par), c, m, round > 0 ? &focus[par] : nullptr);
                });
                for (int par = 0; par < 2; ++par) {
                    moved += chainApply(par, c, m, delta[par]);
                    for (std::size_t q = 0; q < focus[par].size(); ++q) focus[par][q] |= moved_[par][q];
                }
            }
        decodeAll(true);
        if (twinsOn_) moved += twinSnap();
        moved += settle(false);
        rep.rounds = round + 1;
        if (!moved) break;
    }

    rep.keyLength[0] = int(key[0].size()); rep.keyLength[1] = int(key[1].size());
    return true;
}

// The scan of an unscrambled JPEG holds FF only as a stuffed FF 00, a restart marker FF D0
// to D7, fill before a marker, or a marker; a wrong key byte that decodes to FF is almost
// always followed by something else (three times in four). Only the scrambled part is
// judged. Returns -1 when there is nothing to judge, 0 for a clean scan, 1 otherwise.
int jpegScanVerdict(const unsigned char* payload, const RbmEntry& e) {
    const std::size_t h = e.nameLength + 1;
    if (e.length <= kReadable || e.length < h + 4) return -1;
    const unsigned char* f = payload + h;
    std::size_t n = e.length - h;
    while (n > 2 && f[n - 1] == 0) --n;                 // trailing padding after the EOI
    if (f[0] != 0xFF || f[1] != 0xD8) return -1;
    std::size_t j = 2, scan = 0;
    while (j + 4 <= n && !scan) {                        // marker segments up to the first SOS
        if (f[j] != 0xFF) return 1;
        const unsigned char m = f[j + 1];
        if (m == 0xFF) { ++j; continue; }
        if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { j += 2; continue; }
        if (m == 0xD9) return j + 2 + h > kReadable ? 1 : -1;
        const std::size_t len = (std::size_t(f[j + 2]) << 8) | f[j + 3];
        if (len < 2) return 1;
        if (m == 0xDA) scan = j + 2 + len;
        j += 2 + len;
    }
    if (!scan) return j + h > kReadable ? 1 : -1;
    const std::size_t from = std::max(scan, kReadable > h ? kReadable - h : 0);
    if (from + 2 >= n) return -1;
    for (std::size_t t = from; t + 1 < n; ++t) {
        if (f[t] != 0xFF) continue;
        const unsigned char m = f[t + 1];
        if (m == 0xD9) return 0;                         // the image ends; what follows is not scan
        if (m == 0x00 || m >= 0xC0) continue;            // stuffing, restart, marker or fill
        return 1;
    }
    return 0;
}

// One voice channel's part of checkUnscrambledRbm, worked out on its own so that the channels
// can be judged in parallel and merged in their archive order, which gives the same verdict as
// judging them one after another.
struct ChannelVerdict {
    std::vector<std::pair<uint32_t, float>> level, resid;   // (key position, value) per sample
    bool wrapped = false;
    bool hasRatio = false;
    double ratio = 0.0;
};

void judgeChannel(const unsigned char* pay, const VoicePcm& vc, std::size_t L, int ch, ChannelVerdict& out) {
    const std::size_t frame = std::size_t(vc.bytes) * std::size_t(vc.channels), ns = vc.frames;
    // Each sample as a 16-bit value (8-bit scaled up, 24-bit cut to its top 16 bits).
    std::vector<double> x(ns), mag(ns), local;
    std::size_t np = 0;
    uint32_t bitsUsed = 0;
    for (std::size_t t = 0; t < ns; ++t) {
        const std::size_t o = vc.o0 + frame * t + std::size_t(vc.bytes) * std::size_t(ch);
        x[t] = vc.bytes == 1 ? double(int8_t(pay[o])) * 256.0 : decodeSample(pay[o], pay[o + 1]);
        mag[t] = std::fabs(x[t]);
        if (o + std::size_t(vc.bytes) <= kReadable) { np = t + 1; bitsUsed |= uint32_t(int(x[t]) & 0xFFFF); }
    }
    // The smallest step the readable samples use: 1 for 16-bit audio, 256 for audio
    // that came from 8 bits. A floor below it would make every step look like an error.
    const double step = bitsUsed ? double(bitsUsed & (~bitsUsed + 1u)) : 1.0;
    const auto pastReadable = [&](std::size_t t, std::size_t& q) {
        const std::size_t oh = vc.o0 + frame * t + std::size_t(vc.bytes) * std::size_t(ch);
        if (oh < kReadable) return false;
        q = (oh - kReadable) % L;
        return true;
    };
    // (a) Each sample past the readable part against the running median of its
    //     neighbours, under the key position of its most significant byte. A wrong
    //     byte puts a full-scale value there in every period of every voice it reaches.
    runningMedian(mag, local);
    for (std::size_t t = np, q = 0; t < ns; ++t) {
        if (!pastReadable(t, q)) continue;
        out.level.emplace_back(uint32_t(q), float(mag[t] / (local[t] + std::max(kLevelFloor, step))));
    }
    // (d) A key byte off by one under the high byte of most of a key position class
    //     shifts the voice by 256 there, which (a), (b) and (c) cannot see, and it wraps
    //     samples near full scale to the other end. A voice whose readable steps all stay
    //     under half scale and that then steps three quarters of scale has wrapped. The
    //     retail mods' largest such step under exact keys is 37215.
    if (np >= 2) {
        double readableStep = 0.0, pastStep = 0.0;
        for (std::size_t t = 1; t < ns; ++t)
            (t < np ? readableStep : pastStep) = std::max(t < np ? readableStep : pastStep, std::fabs(x[t] - x[t - 1]));
        out.wrapped = readableStep < kWrapReadable && pastStep > kWrapJump;
    }
    if (np < 64 || ns - np < 64) return;
    // (b) The same for the prediction residual, the predictor fitted to the readable
    //     samples alone. A run of key bytes offset alike moves its samples together, so
    //     (a) cannot see it, but it leaves a step at both ends in every period.
    std::array<double, kOrder> a;
    fitPredictor(x.data(), np, a);
    std::vector<double> r(ns), rmag(ns), rloc;
    for (std::size_t t = 0; t < ns; ++t) {
        double v = x[t];
        for (int k = 0; k < kOrder && std::size_t(k) < t; ++k) v -= a[std::size_t(k)] * x[t - std::size_t(k) - 1];
        r[t] = v; rmag[t] = std::fabs(v);
    }
    runningMedian(rmag, rloc);
    for (std::size_t t = np, q = 0; t < ns; ++t) {
        if (!pastReadable(t, q)) continue;
        out.resid.emplace_back(uint32_t(q), float(rmag[t] / (rloc[t] + std::max(kResidualFloor, step))));
    }
    // (c) For keys wrong everywhere: the mean residual past the readable part against
    //     inside it. A readable start that is near silence cannot anchor this.
    double inside = 0.0, past = 0.0, startLevel = 0.0;
    for (std::size_t t = 0; t < np; ++t) startLevel += mag[t];
    if (startLevel / double(np) < kQuietStart) return;
    for (std::size_t t = kOrder; t < ns; ++t) (t < np ? inside : past) += rmag[t];
    inside /= double(np - kOrder); past /= double(ns - np);
    out.hasRatio = true;
    out.ratio = (past + 1.0) / (inside + 1.0);
}

// checkUnscrambledRbm, its voice channels judged over the machine's cores when `parallel` (the
// decoy checks, already run in parallel, judge theirs one after another).
RbmCheck checkImage(const std::vector<unsigned char>& image, bool requireEvidence, bool parallel) {
    RbmCheck chk;
    if (image.size() > kMaxImage) return chk;                // too large to have been estimated
    const std::vector<RbmEntry> entries = rbmEntries(image);
    if (entries.size() > kMaxEntries) return chk;
    bool scrambled = false;
    struct Job { std::size_t entry; VoicePcm vc; int ch; };
    std::vector<Job> jobs;
    std::size_t samples = 0;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RbmEntry& e = entries[i];
        const unsigned char* pay = image.data() + e.payload;
        scrambled = scrambled || e.length > kReadable;
        if (isJpeg(pay, e)) {
            const int v = jpegScanVerdict(pay, e);
            if (v >= 0) { ++chk.jpegsChecked; chk.jpegsClean += v == 0; }
            continue;
        }
        VoicePcm vc;
        if (!voicePcm(pay, e, vc)) continue;
        const std::size_t frame = std::size_t(vc.bytes) * std::size_t(vc.channels);
        if (vc.o0 + frame * vc.frames <= kReadable) continue;   // wholly readable: nothing to judge
        if (vc.channels > 2) { ++chk.voicesUnjudged; continue; }
        for (int ch = 0; ch < vc.channels; ++ch) {
            if (samples + vc.frames > kMaxSamples) { ++chk.voicesUnjudged; continue; }
            samples += vc.frames;
            jobs.push_back({ i, vc, ch });
        }
    }
    std::vector<ChannelVerdict> verdicts(jobs.size());
    const auto judgeJob = [&](std::size_t j) {
        const Job& job = jobs[j];
        judgeChannel(image.data() + entries[job.entry].payload, job.vc, period(job.entry), job.ch, verdicts[j]);
    };
    if (parallel) {
        std::vector<std::size_t> order(jobs.size());          // the longest first, so none is left last
        for (std::size_t j = 0; j < order.size(); ++j) order[j] = j;
        std::stable_sort(order.begin(), order.end(),
                         [&](std::size_t a, std::size_t b) { return jobs[a].vc.frames > jobs[b].vc.frames; });
        parallelFor(order.size(), [&](std::size_t k) { judgeJob(order[k]); });
    } else {
        for (std::size_t j = 0; j < jobs.size(); ++j) judgeJob(j);
    }
    std::vector<double> ratios;
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        ++chk.voicesScrambled;
        chk.voicesWrapped += verdicts[j].wrapped;
        if (verdicts[j].hasRatio) ratios.push_back(verdicts[j].ratio);
    }
    // Per parity, by level and by residual: every key position's samples, gathered in archive
    // order into one buffer (position p holds values[start[p] .. start[p + 1])).
    struct Positions { std::vector<std::size_t> start; std::vector<float> values; };
    Positions level[2], resid[2];
    const auto gather = [&](Positions* lists, std::vector<std::pair<uint32_t, float>> ChannelVerdict::* part) {
        for (std::size_t par = 0; par < 2; ++par) {
            Positions& L = lists[par];
            std::vector<std::size_t> count;
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                if (jobs[j].entry % 2 != par) continue;
                for (const auto& qv : verdicts[j].*part) {
                    if (qv.first >= count.size()) count.resize(std::size_t(qv.first) + 1, 0);
                    ++count[qv.first];
                }
            }
            L.start.assign(count.size() + 1, 0);
            for (std::size_t q = 0; q < count.size(); ++q) L.start[q + 1] = L.start[q] + count[q];
            L.values.resize(L.start.back());
            std::vector<std::size_t> at(L.start.begin(), L.start.end() - 1);
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                if (jobs[j].entry % 2 != par) continue;
                for (const auto& qv : verdicts[j].*part) L.values[at[qv.first]++] = qv.second;
            }
        }
    };
    gather(level, &ChannelVerdict::level);
    gather(resid, &ChannelVerdict::resid);
    verdicts.clear();
    verdicts.shrink_to_fit();
    // A position's verdict is the lower median of its samples, so one loud transient (or an
    // even count split down the middle) does not condemn it.
    const auto judge = [&](Positions* lists, double limit, double& worst) {
        constexpr std::size_t kSpan = 128;                     // positions judged per block
        int bad = 0;
        for (int par = 0; par < 2; ++par) {
            Positions& L = lists[par];
            const std::size_t n = L.start.size() - 1, blocks = (n + kSpan - 1) / kSpan;
            std::vector<int> blockBad(blocks, 0);
            std::vector<double> blockWorst(blocks, 0.0);
            const auto run = [&](std::size_t b) {
                for (std::size_t q = b * kSpan; q < std::min(n, (b + 1) * kSpan); ++q) {
                    const std::size_t size = L.start[q + 1] - L.start[q];
                    if (size < 3) continue;
                    float* v = L.values.data() + L.start[q];
                    const std::size_t mid = (size - 1) / 2;
                    std::nth_element(v, v + mid, v + size);
                    blockWorst[b] = std::max(blockWorst[b], double(v[mid]));
                    blockBad[b] += v[mid] > limit;
                }
            };
            if (parallel) parallelFor(blocks, run);
            else for (std::size_t b = 0; b < blocks; ++b) run(b);
            for (std::size_t b = 0; b < blocks; ++b) { bad += blockBad[b]; worst = std::max(worst, blockWorst[b]); }
        }
        return bad;
    };
    for (int par = 0; par < 2; ++par)
        for (std::size_t q = 0; q + 1 < level[par].start.size(); ++q)
            chk.positionsChecked += level[par].start[q + 1] - level[par].start[q] >= 3;
    chk.positionsBad = judge(level, kMaxPositionLevel, chk.worstPosition)
                     + judge(resid, kMaxPositionResidual, chk.worstResidual);
    chk.voicesChecked = int(ratios.size());
    if (!ratios.empty()) {
        std::nth_element(ratios.begin(), ratios.begin() + long(ratios.size() / 2), ratios.end());
        chk.residualRatio = ratios[ratios.size() / 2];
    }
    chk.tooFewVoices = chk.voicesScrambled > 0 && chk.voicesScrambled < kMinVoices;
    const bool judged = chk.jpegsChecked > 0 || chk.voicesChecked > 0 || chk.positionsChecked > 0;
    chk.ok = (!scrambled || judged)
             && !(requireEvidence && chk.tooFewVoices) && chk.voicesUnjudged == 0
             && (chk.jpegsChecked == 0 || chk.jpegsClean >= kMinCleanJpegs * chk.jpegsChecked)
             && (chk.voicesChecked == 0 || chk.residualRatio < kMaxResidualRatio)
             && chk.positionsBad < kMaxBadPositions && chk.voicesWrapped == 0;
    return chk;
}


} // namespace

RbmCheck checkUnscrambledRbm(const std::vector<unsigned char>& image, bool requireEvidence) {
    return checkImage(image, requireEvidence, true);
}

std::vector<RbmEntry> rbmEntries(const std::vector<unsigned char>& image) {
    std::vector<RbmEntry> out;
    if (image.size() < 12 || std::memcmp(image.data(), "CAT ", 4) != 0
        || std::memcmp(image.data() + 8, "PRBM", 4) != 0) return out;
    struct Walker {
        const unsigned char* p; std::vector<RbmEntry>* out;
        void walk(std::size_t off, std::size_t end, int depth) {
            while (off + 8 <= end && out->size() < kMaxListed) {
                const uint32_t len = be32(p + off + 4);
                const std::size_t body = off + 8;
                if (body + len > end) return;
                if (std::memcmp(p + off, "CAT ", 4) == 0 || std::memcmp(p + off, "FORM", 4) == 0
                    || std::memcmp(p + off, "LIST", 4) == 0 || std::memcmp(p + off, "PROP", 4) == 0) {
                    if (depth < kMaxDepth) walk(body + 4, body + len, depth + 1);
                } else if (std::memcmp(p + off, "EMBF", 4) == 0) {
                    std::size_t i = 0;
                    while (i < len && p[body + i] != 0) ++i;
                    if (i > 0 && i < len) out->push_back({ body, len, i });
                }
                off = body + std::size_t(len) + (len & 1);
            }
        }
    } w{ image.data(), &out };
    w.walk(12, image.size(), 1);        // to the end of the file, as the mod readers walk
    return out;
}

namespace {

// Confirming generated keys, parity by parity (unscrambleRbm). The two sums set the two keys
// independently, so an archive can be right on one parity and wrong on the other (one edited in
// one half of an entry's readable kilobyte after packing), and each parity needs evidence of
// its own. A wrong key decodes its parity's scrambled bytes to values unrelated to the right ones.
constexpr std::size_t kMinWrapSteps = 280;  // wrap-test steps that confirm a parity outright: one step
                                            // under a wrong key escapes with odds 15/16 for independent
                                            // samples, 19/20 allowed for their correlation, and
                                            // 0.95^280 is under 6e-7
constexpr double kMaxWrongOdds = 1e-6;      // the most an unrelated key's odds may be to confirm a parity
constexpr std::size_t kDecoys = 64;         // unrelated keys a parity short of them must be refused under
constexpr std::size_t kDecoyMemory = std::size_t(512) << 20;   // working memory decoys may take at once
constexpr int kNearMissReach = 31;          // neighbouring sums sum + m 2^22, m = -31..31, set against the generated one
constexpr std::size_t kNearMissBytes = 64;  // a neighbour whose key differs in more bytes is unrelated, not near
constexpr int kNearWindow = 32;             // samples either side in the local residual scale
constexpr double kNearClip = 16.0;          // the most one scaled residual counts for
constexpr double kNearLift = 50.0;          // summed squared lift that refuses the generated key: a
                                            // right one exceeds it by chance at about 6 standard deviations
constexpr std::size_t kNearBeyond = 3;      // or samples beyond the voice's range the near key brings back

// Add or remove keys over the given entries (applyRbmKeys over a list fixed beforehand: a decode
// can move the NUL that ends a long name and so change what rbmEntries finds).
void applyKeys(std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries,
               const std::vector<uint8_t>& keyEven, const std::vector<uint8_t>& keyOdd, int sign) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RbmEntry& e = entries[i];
        const std::vector<uint8_t>& key = (i % 2) ? keyOdd : keyEven;
        const std::size_t L = period(i);
        for (std::size_t o = kReadable; o < e.length; ++o) {
            const std::size_t p = (o - kReadable) % L;
            if (p >= key.size()) continue;
            unsigned char& b = image[e.payload + o];
            b = (unsigned char) (sign > 0 ? b + key[p] : b - key[p]);
        }
    }
}

bool sameEntries(const std::vector<RbmEntry>& a, const std::vector<RbmEntry>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].payload != b[i].payload || a[i].length != b[i].length || a[i].nameLength != b[i].nameLength) return false;
    return true;
}

// The top-level CAT spans the file, as in every archive ModPacker writes from a folder with a
// ModName.txt; without one it writes the length as 0, and scrambles with other keys.
bool catSpansFile(const std::vector<unsigned char>& image) {
    return image.size() >= 12 && std::size_t(be32(image.data() + 4)) + 8 == image.size();
}

// Whether a parity holds audio that its key decides: an entry past the readable part whose file
// starts as an AIFF or AIFC does, whether or not the check can read its layout.
bool scrambledAudio(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries, std::size_t parity) {
    for (std::size_t i = parity; i < entries.size(); i += 2) {
        const RbmEntry& e = entries[i];
        const unsigned char* f = image.data() + e.payload + e.nameLength + 1;
        if (e.length > kReadable && e.nameLength + 13 <= kReadable && std::memcmp(f, "FORM", 4) == 0
            && (std::memcmp(f + 8, "AIFF", 4) == 0 || std::memcmp(f + 8, "AIFC", 4) == 0)) return true;
    }
    return false;
}

// Audio samples (every channel, any bit depth) of the voices that reach past the readable part:
// what the self-check would have to read.
std::size_t audioSamples(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries) {
    std::size_t total = 0;
    for (const RbmEntry& e : entries) {
        VoicePcm v;
        if (e.length > kReadable && voicePcm(image.data() + e.payload, e, v)) total += v.frames * std::size_t(v.channels);
    }
    return total;
}

// The wrap test's chances to catch a wrong key on one parity, in the decoded image. Under an
// unrelated key every sample past the readable part decodes to a uniform value, and two in a row
// lie more than three quarters of scale apart one time in 16, which in a voice whose readable
// steps all stay under half scale is a wrap the check refuses (checkUnscrambledRbm (d)). Counted
// per channel, at most one key period of frames (the decoded offsets repeat with it), and taken
// from the longest chain alone: the parity's voices share its key bytes, so their chances are
// not independent of one another.
std::size_t wrapSteps(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries, std::size_t parity) {
    std::size_t steps = 0, samples = 0;
    for (std::size_t i = parity; i < entries.size(); i += 2) {
        const RbmEntry& e = entries[i];
        const unsigned char* pay = image.data() + e.payload;
        VoicePcm vc;
        if (e.length <= kReadable || isJpeg(pay, e) || !voicePcm(pay, e, vc) || vc.channels > 2) continue;
        const std::size_t bytes = std::size_t(vc.bytes), frame = bytes * std::size_t(vc.channels);
        if (vc.o0 + frame * vc.frames <= kReadable) continue;
        for (int ch = 0; ch < vc.channels; ++ch) {
            if (samples + vc.frames > kMaxSamples) return steps;    // past it the check reads no more
            samples += vc.frames;
            std::size_t np = 0;
            double prev = 0.0, readableStep = 0.0;
            for (std::size_t t = 0; t < vc.frames; ++t) {
                const std::size_t o = vc.o0 + frame * t + bytes * std::size_t(ch);
                if (o + bytes > kReadable) break;
                const double x = bytes == 1 ? double(int8_t(pay[o])) * 256.0 : decodeSample(pay[o], pay[o + 1]);
                if (np) readableStep = std::max(readableStep, std::fabs(x - prev));
                prev = x;
                ++np;
            }
            if (np < 2 || readableStep >= kWrapReadable) continue;
            steps = std::max(steps, std::min(vc.frames - np, period(i) / frame));
        }
    }
    return steps;
}

// The Chernoff bound on the odds that sum_q w_q B_q <= f, the B_q independent Bernoulli(p) and the
// weights summing to 1: the least over lambda >= 0 of exp(lambda f + sum_q ln(1 - p + p e^(-lambda w_q))).
// The exponent is convex in lambda, so its slope is found to vanish by bisection.
double chernoffBelow(const std::vector<double>& w, double p, double f) {
    if (f >= p) return 1.0;
    if (f <= 0.0) return std::exp(double(w.size()) * std::log(1.0 - p));   // the limit as lambda grows
    const auto slope = [&](double lam) {
        double s = f;
        for (double wq : w) { const double e = p * std::exp(-lam * wq); s -= wq * e / (1.0 - p + e); }
        return s;
    };
    double lo = 0.0, hi = 1.0;
    while (slope(hi) < 0.0 && hi < 1e15) { lo = hi; hi *= 2.0; }
    for (int it = 0; it < 100; ++it) { const double mid = 0.5 * (lo + hi); (slope(mid) < 0.0 ? lo : hi) = mid; }
    double exponent = hi * f;
    for (double wq : w) exponent += std::log(1.0 - p + p * std::exp(-hi * wq));
    return std::min(1.0, std::exp(exponent));
}

// The odds that an unrelated key on one parity would decode its audio as far from uniform as the
// decoded image is. Under such a key every sample past the readable part takes a uniform value,
// so three quarters of them lie at or above a quarter of full scale, half at or above half, and a
// quarter at or above three quarters, while a drum sound, a hi-hat or a cymbal included, decays
// and keeps most of its samples well under. Whether a sample clears a level is decided by its
// high byte, so the samples under one key position share a draw: they are pooled by position over
// the parity's voices the check judges, and each position counts as one Bernoulli draw weighted by
// its samples (an upper bound on the odds whatever the samples under it do, since a share in
// [0, 1] with mean p never has a larger moment generating function below than a Bernoulli(p)). The
// odds are the Chernoff bound on the lower tail at each level, the smallest times three.
double uniformOdds(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries, std::size_t parity) {
    static const double kLevel[3] = { 8192.0, 16384.0, 24576.0 }, kShare[3] = { 0.75, 0.5, 0.25 };
    std::vector<std::size_t> perPosition;
    std::size_t total = 0, samples = 0, above[3] = { 0, 0, 0 };
    for (std::size_t i = parity; i < entries.size(); i += 2) {
        const RbmEntry& e = entries[i];
        const unsigned char* pay = image.data() + e.payload;
        VoicePcm vc;
        if (e.length <= kReadable || isJpeg(pay, e) || !voicePcm(pay, e, vc) || vc.channels > 2) continue;
        const std::size_t bytes = std::size_t(vc.bytes), frame = bytes * std::size_t(vc.channels), L = period(i);
        if (vc.o0 + frame * vc.frames <= kReadable) continue;
        for (int ch = 0; ch < vc.channels; ++ch) {
            if (samples + vc.frames > kMaxSamples) break;            // past it the check reads no more
            samples += vc.frames;
            for (std::size_t t = 0; t < vc.frames; ++t) {
                const std::size_t o = vc.o0 + frame * t + bytes * std::size_t(ch);
                if (o < kReadable) continue;
                const std::size_t q = (o - kReadable) % L;
                if (q >= perPosition.size()) perPosition.resize(q + 1, 0);
                ++perPosition[q];
                ++total;
                const double x = std::fabs(bytes == 1 ? double(int8_t(pay[o])) * 256.0 : decodeSample(pay[o], pay[o + 1]));
                for (int k = 0; k < 3; ++k) above[k] += x >= kLevel[k];
            }
        }
    }
    if (total == 0) return 1.0;
    std::vector<double> weight;
    for (std::size_t c : perPosition)
        if (c) weight.push_back(double(c) / double(total));
    double best = 1.0;
    for (int k = 0; k < 3; ++k)
        best = std::min(best, chernoffBelow(weight, kShare[k], double(above[k]) / double(total)));
    return std::min(1.0, 3.0 * best);
}

// Whether the self-check refuses the archive with one parity's key replaced by each of kDecoys
// unrelated ones (the first no key at all), the other parity's kept as generated. If it passes
// any, it cannot tell keys apart on that parity: loud noise, near silence, or nothing it judges.
// Run in parallel as far as kDecoyMemory allows, stopping after the first batch with a pass.
bool decoysRefused(const std::vector<unsigned char>& stored, const std::vector<RbmEntry>& entries,
                   const std::vector<uint8_t>& keyEven, const std::vector<uint8_t>& keyOdd,
                   std::size_t parity, std::size_t samples, int& run) {
    const std::size_t length = parity ? keyOdd.size() : keyEven.size();
    const std::size_t each = stored.size() + 64 * samples + (std::size_t(1) << 20);
    const std::size_t batch = std::max<std::size_t>(1, std::min<std::size_t>(16, kDecoyMemory / each));
    std::vector<char> passed(kDecoys, 0);
    for (std::size_t first = 0; first < kDecoys; first += batch) {
        const std::size_t n = std::min(batch, kDecoys - first);
        parallelFor(n, [&](std::size_t j) {
            const std::size_t d = first + j;
            std::vector<uint8_t> decoy;
            if (d > 0) {
                uint32_t s = 0x9E3779B9u * uint32_t(d) + uint32_t(parity);
                decoy.resize(length);
                for (uint8_t& b : decoy) { s = s * 1664525u + 1013904223u; b = uint8_t(s >> 24); }
            }
            std::vector<unsigned char> img = stored;
            applyKeys(img, entries, parity ? keyEven : decoy, parity ? decoy : keyOdd, -1);
            passed[d] = checkImage(img, false, false).ok;
        });
        run += int(n);
        if (std::any_of(passed.begin() + long(first), passed.begin() + long(first + n), [](char p) { return p != 0; }))
            return false;
    }
    return true;
}

// Whether a neighbouring sum's key decodes one parity better than the generated key does, which
// means the archive was edited in its readable part after packing and the generated key is close
// to the right one but not it. An edit moves a sum, and the keys of the new sum are unrelated to the
// old ones (which the confirmation above refuses) except when the move is a multiple of 2^22 that
// leaves the generator's chunk counter the same modulo 2^13: the rand bytes then stay as they were
// and only the special words move, so the two keys differ in a few bytes. A key byte that differs
// adds one constant to every byte decoded under it, so on the key positions where the generated key
// and a neighbour's differ, one of the two decodes displaces every sample whose high byte falls
// there, and the other leaves the voice as it was. A displaced sample stands out from its
// neighbours: its prediction residual (the sample against the mean of the two either side), scaled
// by the local residual level of the generated decode, is lifted by the offset. Summed over the
// samples on the differing positions, the squared scaled residual under the generated key less the
// same under the near one is negative under a right generated key (the near decode is the lifted
// one), or close to zero where the offset is small against the local level, and positive in
// proportion to how far the offset rises above the local level when the generated key is the
// displaced one, as it does in the quiet tail of a decaying sound. Over kNearLift refuses it, as do
// kNearBeyond samples beyond the loudest of the voice's other samples (which decode alike under
// both keys) that the near key brings back inside that range, which is how a mid-sized offset shows
// in loud, steady noise. An offset small against the local level, or on low bytes only, leaves no
// mark and passes; it is then 30 dB or more under the voice.
bool nearMissOutscored(const std::vector<unsigned char>& image, const std::vector<RbmEntry>& entries,
                       std::size_t parity, uint32_t sum, const std::vector<uint8_t>& key, int& tried) {
    if (key.empty()) return false;
    // The neighbours' keys, each kept only if it is near: different in 1 to kNearMissBytes bytes.
    std::vector<std::vector<uint8_t>> diffs;            // generated key minus the neighbour's, per position
    std::vector<char> anyDiff(key.size(), 0);
    for (int m = -kNearMissReach; m <= kNearMissReach; ++m) {
        if (m == 0) continue;
        const std::vector<uint8_t> other = rbmKeySequence(int(parity), sum + uint32_t(m) * (uint32_t(1) << 22), key.size());
        std::vector<uint8_t> d(key.size());
        std::size_t differing = 0;
        for (std::size_t p = 0; p < key.size(); ++p) {
            d[p] = uint8_t(key[p] - other[p]);
            differing += d[p] != 0;
        }
        if (differing == 0 || differing > kNearMissBytes) continue;
        for (std::size_t p = 0; p < key.size(); ++p) anyDiff[p] |= d[p] != 0;
        diffs.push_back(std::move(d));
    }
    tried += int(diffs.size());
    if (diffs.empty()) return false;

    // The samples whose high byte falls on a position some near key differs at, with what is needed
    // to decode them and their two neighbours under either key: the payload, the offsets of the
    // three high bytes, the local residual scale under the generated key, the channel's level
    // limit, and whether the sample is anomalous under the generated key. The other samples are
    // decoded alike under every near key, so they give the limit (the loudest of them) and the rate
    // at which a sample's scaled residual spikes in a decode that is right.
    struct Pick { const unsigned char* pay; std::size_t o[3]; std::size_t L; int bytes; double scale, limit, z; std::size_t q; bool beyond; };
    std::vector<Pick> picks;
    std::size_t samples = 0;
    for (std::size_t i = parity; i < entries.size(); i += 2) {
        const RbmEntry& e = entries[i];
        const unsigned char* pay = image.data() + e.payload;
        VoicePcm vc;
        if (e.length <= kReadable || isJpeg(pay, e) || !voicePcm(pay, e, vc) || vc.channels > 2) continue;
        const std::size_t bytes = std::size_t(vc.bytes), frame = bytes * std::size_t(vc.channels), L = period(i);
        if (vc.o0 + frame * vc.frames <= kReadable || vc.frames < 3) continue;
        for (int ch = 0; ch < vc.channels; ++ch) {
            if (samples + vc.frames > kMaxSamples) break;            // past it the check reads no more
            samples += vc.frames;
            const std::size_t n = vc.frames;
            auto at = [&](std::size_t t) { return vc.o0 + frame * t + bytes * std::size_t(ch); };
            auto value = [&](std::size_t o) {
                return bytes == 1 ? double(int8_t(pay[o])) * 256.0 : decodeSample(pay[o], pay[o + 1]);
            };
            std::vector<double> res(n, 0.0), cum(n + 1, 0.0);
            for (std::size_t t = 1; t + 1 < n; ++t)
                res[t] = std::fabs(value(at(t)) - 0.5 * (value(at(t - 1)) + value(at(t + 1))));
            for (std::size_t t = 0; t < n; ++t) cum[t + 1] = cum[t] + res[t];
            auto near = [&](std::size_t t) {                       // the sample's high byte is on a near position
                const std::size_t o = at(t);
                if (o < kReadable) return false;
                const std::size_t q = (o - kReadable) % L;
                return q < key.size() && anyDiff[q] != 0;
            };
            auto scale = [&](std::size_t t) {
                const std::size_t a = t > std::size_t(kNearWindow) + 1 ? t - kNearWindow : 1;
                const std::size_t b = std::min(n - 1, t + kNearWindow + 1);
                return (cum[b] - cum[a] - res[t]) / double(b - a - 1) + kResidualFloor;
            };
            double limit = 0.0;
            for (std::size_t t = 0; t < n; ++t)
                if (!near(t)) limit = std::max(limit, std::fabs(value(at(t))));
            for (std::size_t t = 1; t + 1 < n; ++t) {
                const std::size_t o = at(t);
                if (o < kReadable || !near(t)) continue;
                const double s = scale(t);
                picks.push_back({ pay, { at(t - 1), o, at(t + 1) }, L, int(bytes), s, limit,
                                  std::min(kNearClip, res[t] / s), (o - kReadable) % L, std::fabs(value(o)) > limit });
            }
        }
    }
    if (picks.empty()) return false;

    // Each near key in turn: on the positions it differs at, how many samples are anomalous under
    // the generated key and how many under the near one. The picks are grouped by position so a
    // near key visits only the positions it differs at.
    std::sort(picks.begin(), picks.end(), [](const Pick& a, const Pick& b) { return a.q < b.q; });
    std::vector<std::size_t> first(key.size() + 1, picks.size());
    for (std::size_t j = picks.size(); j-- > 0;) first[picks[j].q] = j;
    for (std::size_t q = key.size(); q-- > 0;) first[q] = std::min(first[q], first[q + 1]);
    for (const std::vector<uint8_t>& d : diffs) {
        double lift = 0.0;
        std::size_t beyondOurs = 0, beyondNear = 0;
        for (std::size_t q = 0; q < key.size(); ++q) {
            if (!d[q]) continue;
            for (std::size_t j = first[q]; j < first[q + 1]; ++j) {
                const Pick& k = picks[j];
                auto shifted = [&](std::size_t b) {
                    return b < kReadable ? k.pay[b] : uint8_t(k.pay[b] + d[(b - kReadable) % k.L]);
                };
                double v[3];
                for (int s = 0; s < 3; ++s) {
                    const std::size_t o = k.o[s];
                    v[s] = k.bytes == 1 ? double(int8_t(shifted(o))) * 256.0 : decodeSample(shifted(o), shifted(o + 1));
                }
                const double zNear = std::min(kNearClip, std::fabs(v[1] - 0.5 * (v[0] + v[2])) / k.scale);
                lift += k.z * k.z - zNear * zNear;
                beyondOurs += k.beyond;
                beyondNear += std::fabs(v[1]) > k.limit;
            }
        }
        // Under the right generated key the near decode lifts each sample by the offset, so
        // the sum is negative, or near zero where the offset is small against the local level.
        // A sum above kNearLift, or samples beyond the voice's range that the near key brings
        // back inside it, says the generated key is the displaced one.
        if (lift > kNearLift || (beyondOurs >= kNearBeyond && beyondNear == 0)) return true;
    }
    return false;
}

} // namespace

void applyRbmKeys(std::vector<unsigned char>& image, const std::vector<uint8_t>& keyEven,
                  const std::vector<uint8_t>& keyOdd, int sign) {
    applyKeys(image, rbmEntries(image), keyEven, keyOdd, sign);
}

bool estimateRbmKeys(const std::vector<unsigned char>& image,
                     std::vector<uint8_t>& keyEven, std::vector<uint8_t>& keyOdd,
                     RbmUnscrambleReport* report) {
    const auto t0 = std::chrono::steady_clock::now();
    RbmUnscrambleReport rep;
    if (image.size() > kMaxImage) { rep.tooLarge = true; if (report) *report = rep; return false; }
    const std::vector<RbmEntry> entries = rbmEntries(image);
    rep.entries = (int) entries.size();
    if (entries.size() > kMaxEntries) rep.tooLarge = true;
    if (entries.empty() || rep.tooLarge) { if (report) *report = rep; return false; }
    KeyEstimator est(image, entries);
    if (!est.run(rep)) { if (report) *report = rep; return false; }
    keyEven = est.key[0]; keyOdd = est.key[1];
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (report) *report = rep;
    return true;
}

bool unscrambleRbm(std::vector<unsigned char>& image, RbmUnscrambleReport* report,
                   std::vector<uint8_t>* keyEven, std::vector<uint8_t>* keyOdd, bool estimate) {
    // Every decode is made in a copy, and the image changes only when one is taken.
    //
    // The generated keys first: exact for an archive ModPacker made from a folder with a
    // ModName.txt, and instant. They are tried on an archive whose top-level CAT spans the file,
    // as every such archive's does, and within the limits an estimate keeps to. They are taken
    // when the decode passes the self-check (without the evidence minimum that guards an
    // estimate, so a partial mod is read too) and leaves the entries where they were, and when
    // each parity that holds scrambled audio is confirmed on its own: by enough chances for the
    // wrap test to catch a wrong key (wrapSteps), by audio too far from uniform for an unrelated
    // key to have decoded it (uniformOdds), or failing both by the check refusing that parity
    // under unrelated keys (decoysRefused). Each such parity's key is then set against the keys
    // of its near neighbours, and refused if one of them decodes the parity better
    // (nearMissOutscored: the archive was edited after packing, and the generated key is close
    // but not right). A parity of pictures and text alone is judged
    // by the check alone, as an estimate always was. A parity that cannot be confirmed means
    // the check cannot tell right keys from wrong there; with 1 to 11 voices the archive is
    // refused as it stands, as an estimate would be under the minimum, and otherwise it is
    // estimated. So are keys that fail: an archive they do not fit (one ModPacker did not make,
    // or one edited after packing), or one whose content the check cannot judge even under
    // exact keys.
    const std::vector<RbmEntry> entries = rbmEntries(image);
    std::vector<uint8_t> k0, k1;
    RbmUnscrambleReport rep;
    rep.entries = int(entries.size());
    const bool small = image.size() <= kMaxImage && entries.size() <= kMaxEntries;
    const std::size_t samples = small ? audioSamples(image, entries) : 0;
    if (!small || samples > kMaxSamples) {                  // more than the check would read
        rep.tooLarge = true;
        if (report) *report = rep;
        return false;
    }
    bool tried = false;
    if (!estimate && catSpansFile(image) && generateRbmKeys(image, k0, k1, &rep)) {
        tried = true;
        std::vector<unsigned char> work = image;
        applyKeys(work, entries, k0, k1, -1);
        rep.generatedCheck = checkUnscrambledRbm(work, false);
        bool confirmed = rep.generatedCheck.ok && sameEntries(rbmEntries(work), entries);
        for (std::size_t par = 0; confirmed && par < 2; ++par) {
            if ((par ? k1 : k0).empty() || !scrambledAudio(image, entries, par)) continue;   // no audio to guard
            rep.wrapSteps[par] = int(wrapSteps(work, entries, par));
            if (std::size_t(rep.wrapSteps[par]) >= kMinWrapSteps) continue;
            rep.unrelatedOdds[par] = uniformOdds(work, entries, par);
            if (rep.unrelatedOdds[par] < kMaxWrongOdds) continue;
            confirmed = decoysRefused(image, entries, k0, k1, par, samples, rep.decoys);
            rep.generatedUnconfirmed = !confirmed;
        }
        // Confirmation shows a parity's key is not unrelated to the right one. A key that is close
        // but not right (an archive edited after packing) is caught only by setting it against
        // its near neighbours.
        for (std::size_t par = 0; confirmed && par < 2; ++par) {
            if ((par ? k1 : k0).empty() || !scrambledAudio(image, entries, par)) continue;
            rep.nearMiss[par] = nearMissOutscored(work, entries, par, rep.sum[par], par ? k1 : k0, rep.nearKeys);
            confirmed = !rep.nearMiss[par];
        }
        if (confirmed) {
            image.swap(work);
            rep.generated = rep.ok = true;
            rep.check = rep.generatedCheck;
            if (keyEven) *keyEven = k0;
            if (keyOdd) *keyOdd = k1;
            if (report) *report = rep;
            return true;
        }
        rep.generatedRefused = true;
        if ((rep.generatedUnconfirmed || rep.nearMiss[0] || rep.nearMiss[1]) && rep.generatedCheck.tooFewVoices) {
            if (keyEven) *keyEven = k0;
            if (keyOdd) *keyOdd = k1;
            if (report) *report = rep;
            return false;
        }
    }
    RbmUnscrambleReport est;
    est.estimated = estimateRbmKeys(image, k0, k1, &est);
    if (tried) {
        est.sum[0] = rep.sum[0]; est.sum[1] = rep.sum[1];
    } else {
        rbmKeySums(image, est.sum);
    }
    est.generatedRefused = rep.generatedRefused;
    est.generatedUnconfirmed = rep.generatedUnconfirmed;
    est.generatedCheck = rep.generatedCheck;
    est.wrapSteps[0] = rep.wrapSteps[0]; est.wrapSteps[1] = rep.wrapSteps[1];
    est.unrelatedOdds[0] = rep.unrelatedOdds[0]; est.unrelatedOdds[1] = rep.unrelatedOdds[1];
    est.nearMiss[0] = rep.nearMiss[0]; est.nearMiss[1] = rep.nearMiss[1];
    est.nearKeys = rep.nearKeys;
    est.decoys = rep.decoys;
    if (est.estimated) {
        if (keyEven) *keyEven = k0;
        if (keyOdd) *keyOdd = k1;
        std::vector<unsigned char> work = image;
        applyKeys(work, entries, k0, k1, -1);
        est.check = checkUnscrambledRbm(work);
        est.ok = est.check.ok;
        if (est.ok) image.swap(work);
    }
    if (report) *report = est;
    return est.ok;
}

} // namespace reborn
