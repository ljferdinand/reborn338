#!/usr/bin/env python3
"""Delay-tap analysis, applied identically to a ReBirth export directory and to
an ours-render directory (the "one method both sides" rule of
docs/MEASUREMENT_PROTOCOL.md).

For each `dl_*.rbs`/`dss_*`/`dsn_*` fixture rendered into DIR it reports the echo
train: the first-echo offset in samples (delay length), successive tap peak
ratios (feedback), per-tap L/R peak (pan / ping-pong), and the echo-vs-dry
spectral centroid (filtering). Within a single render the delay is deterministic,
so tap offsets are exact even though ReBirth is not sample-deterministic across
renders; compare offsets and ratios, never samples, between ReBirth and ours.

Usage:
    python delay_taps.py <renders_dir>            # summary for every fixture found
    python delay_taps.py <renders_dir> dl_s04     # one fixture, verbose
"""
import os, sys, glob, numpy as np, wave
SR = 44100

def load(path):
    w = wave.open(path); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768
    ch = w.getnchannels(); return x.reshape(-1, ch) if ch == 2 else np.repeat(x.reshape(-1, 1), 2, 1)

def onsets(mono, thr_frac=0.25, gap_s=0.06):
    env = np.abs(mono); thr = thr_frac * env.max(); out = []; i = 0; gap = int(gap_s * SR)
    while i < len(env):
        if env[i] > thr:
            j = min(len(env), i + int(0.03 * SR)); out.append(i + int(np.argmax(env[i:j]))); i = out[-1] + gap
        else:
            i += 1
    return out

def peak(mono, pos, w=int(0.025 * SR)):
    return np.abs(mono[max(0, pos - 80):pos + w]).max()

def peak_lr(x, pos, w=int(0.025 * SR)):
    a, b = max(0, pos - 80), min(len(x), pos + w); seg = x[a:b]
    return np.abs(seg[:, 0]).max(), np.abs(seg[:, 1]).max()

def centroid(mono, pos):
    n = int(0.03 * SR); seg = mono[pos:pos + n] * np.hanning(n)
    X = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(len(seg), 1 / SR)
    return (f * X).sum() / (X.sum() + 1e-9)

def report(path, verbose=False):
    x = load(path); mono = x.mean(1); on = onsets(mono)
    name = os.path.splitext(os.path.basename(path))[0]
    if len(on) < 2:
        print("%-10s no echo (dry onsets=%d)" % (name, len(on))); return
    N = on[1] - on[0]
    dry, e1 = peak(mono, on[0]), peak(mono, on[1])
    e2 = peak(mono, on[0] + 2 * N)
    fb = e2 / e1 if e1 else 0
    l0, r0 = peak_lr(x, on[1]); l1, r1 = peak_lr(x, on[0] + 2 * N)
    cd, ce = centroid(mono, on[0]), centroid(mono, on[1])
    print("%-10s len=%6dsm echo1/dry=%.3f fb(e2/e1)=%.3f echo1(L,R)=(%.3f,%.3f) echo2(L,R)=(%.3f,%.3f) cen dry/echo=%.0f/%.0f"
          % (name, N, e1 / dry if dry else 0, fb, l0, r0, l1, r1, cd, ce))
    if verbose:
        for k, o in enumerate(on[:8]):
            print("    tap %d @ %d (%.1f ms)  peak=%.3f" % (k, o, 1000 * o / SR, peak(mono, o)))

def report_taps(path, N):
    """Sample taps at the KNOWN spacing N (from the measured length law), robust
    for quiet echoes where a threshold detector would latch the next dry hit."""
    x = load(path); mono = x.mean(1); on = onsets(mono); t0 = on[0]
    name = os.path.splitext(os.path.basename(path))[0]
    print("%-10s N=%d" % (name, N))
    prev = None
    for k in range(5):
        l, r = peak_lr(x, t0 + k * N); m = peak(mono, t0 + k * N)
        ratio = ("%.3f" % (m / prev)) if prev else "-"
        print("    tap %d: mono=%.3f L,R=(%.3f,%.3f)  ratio/prev=%s" % (k, m, l, r, ratio)); prev = m

def main(argv):
    d = argv[0]
    if len(argv) >= 3 and argv[2].lstrip("-").isdigit():
        report_taps(os.path.join(d, argv[1] + ".wav"), int(argv[2]))
    elif len(argv) > 1:
        report(os.path.join(d, argv[1] + ".wav"), verbose=True)
    else:
        for p in sorted(glob.glob(os.path.join(d, "d*.wav"))):
            report(p)

if __name__ == "__main__":
    main(sys.argv[1:])
