#!/usr/bin/env python3
"""Peak at the SLID (glide) step relative to the STRUCK step, step-locked.

Task 77. The glide-jump fixture strikes the base note at step 0 of every bar and
slides to base+jump at step 1 (a tie, so no VCA re-attack). Any energy at step 1
is the glide sweeping the oscillator, not a re-pluck. This measures, per bar and
averaged, the PEAK in a tight window at the slid step against the peak at the
struck step, plus the magnitude-weighted spectral centroid in the glide window.

Step-locked, not onset-detected: the sequencer position is known exactly for a
generated fixture, so a fixed window at each step avoids the level-dependent
threshold drift docs/MEASUREMENT_PROTOCOL.md warns about. Peak, not the onset_ab ratio,
because task 53 showed that ratio's floor is confounded by the DC blocker.

STEREO pooled in power, never mono-summed. Usage:
  python glide_peak.py <wav> --bpm 140 --bars 4 [--win-ms 45]
"""
import argparse, wave, numpy as np

def load(p):
    with wave.open(p, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        x = np.frombuffer(w.readframes(n), "<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--bpm", type=float, default=140.0)
    ap.add_argument("--bars", type=int, default=4)
    ap.add_argument("--win-ms", type=float, default=45.0)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    x, sr = load(a.wav)
    mono = x.mean(axis=1)                       # for peak/centroid shape only
    env = np.convolve(np.abs(mono), np.ones(int(0.003*sr))/int(0.003*sr), mode="same")
    step = 60.0 / a.bpm / 4.0
    w = int(a.win_ms * 1e-3 * sr)
    def win_peak(step_idx):
        s = int(round(step_idx * step * sr))
        return float(env[s:s+w].max()) if s+w < len(env) else np.nan
    def win_centroid(step_idx):
        s = int(round(step_idx * step * sr))
        seg = mono[s:s+w]
        if len(seg) < 8: return np.nan
        F = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1.0/sr)
        return float((f*F).sum() / (F.sum()+1e-12))
    p0, p1, cent = [], [], []
    for b in range(a.bars):
        base = b * 16
        p0.append(win_peak(base + 0))          # struck
        p1.append(win_peak(base + 1))          # slid / glide
        cent.append(win_centroid(base + 1))
    p0 = np.nanmean(p0); p1 = np.nanmean(p1); cent = np.nanmean(cent)
    ratio_db = 20*np.log10((p1+1e-9)/(p0+1e-9))
    if not a.quiet:
        print(f"struck peak {20*np.log10(p0+1e-9):6.1f} dBFS, "
              f"glide peak {20*np.log10(p1+1e-9):6.1f} dBFS, "
              f"glide/struck {ratio_db:+5.1f} dB, glide centroid {cent:6.0f} Hz")
    # machine-readable last line
    print(f"RESULT {20*np.log10(p0+1e-9):.2f} {20*np.log10(p1+1e-9):.2f} "
          f"{ratio_db:.2f} {cent:.1f}")

if __name__ == "__main__":
    main()
