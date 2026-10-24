#!/usr/bin/env python3
"""PCF swept-cutoff TRAJECTORY analyzer (clean-room, 2026-09-13).

Tracks the moving resonant peak after a single PCF envelope trigger and reports
peak-cutoff(t), which is the domain the envelope->cutoff residual lives in. Built
for the swept fixtures (gen_pcf_sweep.py: pattern 0, one trigger per bar at a slow
tempo, so each bar is one clean envelope event). ONE method applied to the ReBirth
export and to ours identically (docs/MEASUREMENT_PROTOCOL.md).

Method: per-frame magnitude spectrum divided by the time-averaged reference
spectrum (the same fixture with the PCF routed nowhere, psw_ref) to flatten the
drum coloration, then follow the peak of that ratio above 1.5 kHz. Triggers are
found from the surge in >5 kHz energy; the first (render-ramp) trigger is dropped
and the rest are median-averaged, which is robust to ReBirth's per-render
non-determinism (the filter, not the phase, sets the peak).

Usage:
    python pcf_trajectory.py <signal.wav> <ref.wav> [label]
    python pcf_trajectory.py <signal.wav> <ref.wav> --csv     # dense grid to stdout

Reports peak-freq (Hz) at 17/40/80/150/300 ms after the trigger, plus prominence
(dB above the 200-800 Hz passband). --csv emits ms,peak_hz on a dense grid.
"""
import sys, numpy as np, wave

SR_DEFAULT = 44100

def load(path):
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float) / 32768.0
    ch = w.getnchannels()
    return (x.reshape(-1, ch).mean(1) if ch == 2 else x), w.getframerate()

def stft(x, nper=1024, hop=128):
    win = np.hanning(nper)
    return np.array([np.abs(np.fft.rfft(x[s:s+nper]*win)) for s in range(0, len(x)-nper, hop)]), hop

def trajectory(sig_path, ref_path, ms_grid):
    x, SR = load(sig_path); r, _ = load(ref_path)
    S, hop = stft(x); R, _ = stft(r)
    f = np.fft.rfftfreq(1024, 1.0/SR)
    ratio = S / (R.mean(0) + 1e-9)                 # flatten the drum shape
    band = f > 1500; fb = f[band]
    pb = (f >= 200) & (f <= 800)
    hb = f > 5000; he = S[:, hb].sum(1); thr = he.max()*0.25
    trig = []; i = 0
    while i < len(he):
        if he[i] > thr:
            k = i + int(np.argmax(he[i:i+int(0.06*SR/hop)+1]))
            if not trig or (k-trig[-1])*hop/SR > 1.5: trig.append(k)
            i = k + int(1.5*SR/hop)
        else:
            i += 1
    trigs = trig[1:] if len(trig) > 2 else trig    # drop the render-ramp trigger
    pkf = np.full(len(ms_grid), np.nan); pkdb = np.full(len(ms_grid), np.nan)
    for j, ms in enumerate(ms_grid):
        fv, dv = [], []
        for t0 in trigs:
            fr = t0 + int((ms/1000.0)*SR/hop)
            if fr < S.shape[0]:
                col = ratio[fr, band]
                fv.append(fb[np.argmax(col)])
                dv.append(20*np.log10(col.max()+1e-9) - 20*np.log10(np.median(ratio[fr, pb])+1e-9))
        if fv: pkf[j] = np.median(fv); pkdb[j] = np.median(dv)
    return pkf, pkdb, len(trigs)

def main():
    args = [a for a in sys.argv[1:] if a != "--csv"]
    csv = "--csv" in sys.argv
    sig, ref = args[0], args[1]
    label = args[2] if len(args) > 2 else sig
    if csv:
        grid = np.arange(10, 400, 6)
        pkf, _, n = trajectory(sig, ref, grid)
        print("ms,peak_hz")
        for ms, v in zip(grid, pkf):
            if np.isfinite(v): print(f"{ms},{v:.0f}")
        return 0
    grid = [17, 40, 80, 150, 300]
    pkf, pkdb, n = trajectory(sig, ref, grid)
    print(f"=== {label} : {n} clean triggers")
    print("    peak-freq (Hz) / prominence (dB):")
    for ms, fv, dv in zip(grid, pkf, pkdb):
        print(f"      {ms:4d} ms : {fv:7.0f} Hz   {dv:5.1f} dB")
    return 0

if __name__ == "__main__":
    sys.exit(main())
