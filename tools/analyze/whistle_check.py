#!/usr/bin/env python3
"""
whistle_check.py: ring-vs-whistle discriminant for the 303 velocity gate.

The question the peak-prominence metric cannot answer: when REBORN_VEL opens the
HF Q-loss gate, does the extra resonance energy on the stress patch decay with the
note (a bright transient RING, safe) or sustain/build independent of the envelope
(a self-oscillating WHISTLE, which is exactly what the hfQ damping guarded)?

This measures the two renders in the time-frequency domain and reports the DELTA
the gate introduces in the decay tail. It is a spectral proxy for the ear check,
not a replacement for it; confirm the verdict by listening to gate_on.wav.

Usage:
    python3 whistle_check.py baseline.wav gate_on.wav [--hf 2500] [--png out.png]

Renders to produce (from a clean checkout of main, gate probe re-applied):
    baseline:  (no env vars)                    reborn_render stress_303.rbs --solo bass1 --no-dist --seconds 12
    gate_on:   REBORN_VEL=1.0 REBORN_VEL_REF=0.35 REBORN_GATE_PIVOT=0.55  (same render)

NOTE (staged, untested): written against score303.py's documented conventions
before a real render existed. Shake it out against the first real pair (verify
the note-region detection lands and the tail window is sane) before trusting
the flag.
"""
import sys, argparse, wave
import numpy as np
from scipy.signal import stft


# ---- robust WAV loader (16/24/32-bit PCM + 32-bit float), mono-summed --------
def load_wav(path):
    with wave.open(path, "rb") as w:
        n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
        v[v >= (1 << 23)] -= (1 << 24)
        x = v.astype(np.float64) / (1 << 23)
    elif sw == 4:
        # try float32 first; fall back to int32
        f = np.frombuffer(raw, "<f4")
        x = f.astype(np.float64) if np.isfinite(f).all() and np.max(np.abs(f)) <= 4.0 \
            else np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"unsupported sample width {sw*8}-bit")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def analyze(path, hf_hz):
    x, sr = load_wav(path)
    nperseg = 2048
    f, t, Z = stft(x, fs=sr, window="hann", nperseg=nperseg, noverlap=nperseg - 512)
    P = (np.abs(Z) ** 2)                      # power spectrogram [freq, frame]
    total = P.sum(axis=0) + 1e-20
    hf = f >= hf_hz
    hf_e = P[hf, :].sum(axis=0)               # high-band energy per frame

    # active note region: frames within 60 dB of the loudest frame
    db = 10 * np.log10(total / total.max())
    active = np.where(db > -60)[0]
    if active.size < 8:
        active = np.arange(len(t))
    a0, a1 = active[0], active[-1]

    # tail = everything after the high-band energy peak, inside the active region
    hf_active = hf_e.copy()
    hf_active[:a0] = 0; hf_active[a1 + 1:] = 0
    peak = int(np.argmax(hf_active))
    tail = np.arange(max(peak, a0), a1 + 1)
    if tail.size < 6:
        tail = np.arange(a0, a1 + 1)

    # (1) tail slope of high-band energy, dB/s  (ring: strongly negative; whistle: ~0 or +)
    te = 10 * np.log10(hf_e[tail] + 1e-20)
    slope = np.polyfit(t[tail] - t[tail][0], te, 1)[0]

    # (2) sustain ratio: median HF energy in last 25% of the note / peak HF energy
    last = tail[int(0.75 * tail.size):] if tail.size >= 4 else tail
    sustain = float(np.median(hf_e[last]) / (hf_e[peak] + 1e-20))

    # (3) tonality of the late tail: spectral flatness of the high band
    #     (whistle: one narrow peak -> flatness << 1; ring/harmonics: broader)
    hp = P[hf][:, last].mean(axis=1) + 1e-20
    flatness = float(np.exp(np.mean(np.log(hp))) / np.mean(hp))

    # (4) fixed-ridge test: does one HF bin stay put while the note decays?
    #     dominant HF bin per late-tail frame; low spread = a pinned tone.
    fbins = f[hf]
    dom = fbins[np.argmax(P[hf][:, last], axis=0)]
    ridge_hz = float(np.median(dom))
    ridge_spread = float(np.median(np.abs(dom - ridge_hz)))  # Hz, MAD

    return dict(sr=sr, dur=len(x) / sr, slope=slope, sustain=sustain,
                flatness=flatness, ridge_hz=ridge_hz, ridge_spread=ridge_spread,
                f=f, t=t, P=P, hf_e=hf_e, tail=tail)


def verdict(base, gate):
    # The whistle signature is the gate turning a decaying tail into a sustained,
    # tonal, pinned one. Judge on the DELTA, not absolute values.
    d_slope = gate["slope"] - base["slope"]        # + => gate flattened the decay
    d_sustain = gate["sustain"] - base["sustain"]  # + => gate raised tail sustain
    flags = []
    if gate["slope"] > -3.0:            flags.append(f"tail barely decays ({gate['slope']:.1f} dB/s)")
    if gate["sustain"] > 0.25:          flags.append(f"tail HF sustains ({gate['sustain']*100:.0f}% of peak)")
    if gate["flatness"] < 0.02 and gate["ridge_spread"] < 60:
        flags.append(f"pinned tone ~{gate['ridge_hz']:.0f} Hz (flatness {gate['flatness']:.3f})")
    if d_slope > 4.0:                   flags.append(f"gate flattened decay by {d_slope:.1f} dB/s vs baseline")
    if d_sustain > 0.15:                flags.append(f"gate raised tail sustain by {d_sustain*100:.0f} pts")
    return flags


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline"); ap.add_argument("gate")
    ap.add_argument("--hf", type=float, default=2500.0)
    ap.add_argument("--png", default=None)
    a = ap.parse_args()

    base = analyze(a.baseline, a.hf)
    gate = analyze(a.gate, a.hf)

    def line(tag, r):
        print(f"  {tag:9s}  tail-slope {r['slope']:+6.1f} dB/s | "
              f"sustain {r['sustain']*100:5.1f}% | flatness {r['flatness']:.3f} | "
              f"ridge {r['ridge_hz']:5.0f}Hz +/-{r['ridge_spread']:.0f}")

    print(f"\nhigh band >= {a.hf:.0f} Hz\n")
    line("baseline", base); line("gate-on", gate)

    flags = verdict(base, gate)
    print()
    if flags:
        print("  VERDICT: WHISTLE RISK. Do NOT commit on this alone. Signatures:")
        for fl in flags:
            print(f"    - {fl}")
    else:
        print("  VERDICT: consistent with a bright RING, not a sustained whistle.")
        print("           The gate's extra HF energy decays with the note tail.")
    print("\n  Spectral proxy only. Confirm by ear on the gate render before sign-off.\n")

    if a.png:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, ax = plt.subplots(1, 2, figsize=(12, 4), sharey=True)
            for k, (tag, r) in enumerate([("baseline", base), ("gate-on", gate)]):
                S = 10 * np.log10(r["P"] + 1e-12)
                ax[k].pcolormesh(r["t"], r["f"], S, vmin=S.max() - 80, vmax=S.max(), shading="auto")
                ax[k].axvline(r["t"][r["tail"][0]], color="w", ls="--", lw=0.8)
                ax[k].set_title(tag); ax[k].set_ylim(0, 8000); ax[k].set_xlabel("s")
            ax[0].set_ylabel("Hz")
            fig.suptitle("303 stress patch: baseline vs velocity-gate (dashed = tail start)")
            fig.tight_layout(); fig.savefig(a.png, dpi=120)
            print(f"  wrote {a.png}\n")
        except Exception as e:
            print(f"  (png skipped: {e})\n")


if __name__ == "__main__":
    main()
