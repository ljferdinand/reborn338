"""Whole-song A/B against a ReBirth export: level, envelope agreement and
spectral balance, computed the SAME way on both files.

Never a null test. ReBirth does not reset oscillator or noise phase per render,
so its exports are not sample-deterministic and a sample difference means
nothing. What survives that are levels, envelopes and band energies.

Stereo throughout: L and R are reported separately as well as summed, because
mono-summing hid a pan law seven times too wide for months.
"""
import sys, math, wave
import numpy as np

BANDS = [(20, 100), (100, 300), (300, 1200), (1200, 4000), (4000, 12000), (12000, 20000)]


def read(path):
    w = wave.open(path, "rb")
    n, sr, ch = w.getnframes(), w.getframerate(), w.getnchannels()
    x = np.frombuffer(w.readframes(n), dtype="<i2").astype(np.float64) / 32768.0
    return x.reshape(-1, ch), sr


def db(v):
    return 20.0 * math.log10(max(float(v), 1e-20))


def envelope(mono, sr, hop_ms=20.0):
    """RMS in fixed hops. Power domain, never a time-domain sum: coherent
    averaging of step-locked hits cancels exactly the content ReBirth jitters."""
    hop = max(1, int(sr * hop_ms / 1000.0))
    n = len(mono) // hop
    return np.sqrt(np.mean(mono[: n * hop].reshape(n, hop) ** 2, axis=1) + 1e-20)


def band_shares(mono, sr):
    from numpy.fft import rfft, rfftfreq
    nper = 8192
    hop = nper // 2
    win = np.hanning(nper)
    acc = np.zeros(nper // 2 + 1)
    k = 0
    for i in range(0, len(mono) - nper, hop * 8):      # every 8th frame is plenty
        acc += np.abs(rfft(mono[i:i + nper] * win)) ** 2
        k += 1
    acc /= max(k, 1)
    f = rfftfreq(nper, 1.0 / sr)
    tot = acc.sum()
    return [(lo, hi, 100.0 * acc[(f >= lo) & (f < hi)].sum() / tot) for lo, hi in BANDS]


def main(ref_path, ours_path, head_s=20.0):
    a, sr = read(ref_path)
    b, sr2 = read(ours_path)
    assert sr == sr2, "sample rates differ"
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]

    print("frames %d  (%.2f s)   ref=%s  ours=%s\n"
          % (n, n / sr, ref_path.split("/")[-1], ours_path.split("/")[-1]))

    print("  %-22s %10s %10s %9s" % ("", "ReBirth", "Reborn", "delta dB"))
    for name, ia in (("RMS L", 0), ("RMS R", 1)):
        print("  %-22s %10.2f %10.2f %+9.2f"
              % (name, db(np.sqrt(np.mean(a[:, ia] ** 2))), db(np.sqrt(np.mean(b[:, ia] ** 2))),
                 db(np.sqrt(np.mean(b[:, ia] ** 2))) - db(np.sqrt(np.mean(a[:, ia] ** 2)))))
    ma, mb = a.mean(axis=1), b.mean(axis=1)
    print("  %-22s %10.2f %10.2f %+9.2f"
          % ("RMS mono-sum", db(np.sqrt(np.mean(ma ** 2))), db(np.sqrt(np.mean(mb ** 2))),
             db(np.sqrt(np.mean(mb ** 2))) - db(np.sqrt(np.mean(ma ** 2)))))
    print("  %-22s %10.2f %10.2f" % ("peak dBFS", db(np.max(np.abs(a))), db(np.max(np.abs(b)))))
    print("  %-22s %10d %10d" % ("clipped samples", int(np.sum(np.abs(a) >= 0.999)),
                                 int(np.sum(np.abs(b) >= 0.999))))

    ea, eb = envelope(ma, sr), envelope(mb, sr)
    m = min(len(ea), len(eb))
    ea, eb = ea[:m], eb[:m]
    r = float(np.corrcoef(ea, eb)[0, 1])
    rl = float(np.corrcoef(np.log(ea), np.log(eb))[0, 1])
    print("\n  envelope correlation   linear %.3f    log %.3f" % (r, rl))

    h = int(head_s * 1000 / 20)
    print("  first %.0f s only        linear %.3f    log %.3f"
          % (head_s, float(np.corrcoef(ea[:h], eb[:h])[0, 1]),
             float(np.corrcoef(np.log(ea[:h]), np.log(eb[:h]))[0, 1])))
    print("  first %.0f s level       ReBirth %.2f   Reborn %.2f   delta %+.2f dB"
          % (head_s, db(np.sqrt(np.mean(ma[: int(head_s * sr)] ** 2))),
             db(np.sqrt(np.mean(mb[: int(head_s * sr)] ** 2))),
             db(np.sqrt(np.mean(mb[: int(head_s * sr)] ** 2)))
             - db(np.sqrt(np.mean(ma[: int(head_s * sr)] ** 2)))))

    print("\n  band share of each file's OWN total power (level offset is separate)")
    print("  %-16s %10s %10s %9s" % ("band Hz", "ReBirth", "Reborn", "delta pp"))
    for (lo, hi, pa), (_, _, pb) in zip(band_shares(ma, sr), band_shares(mb, sr)):
        print("  %-16s %9.2f%% %9.2f%% %+9.2f" % ("%d-%d" % (lo, hi), pa, pb, pb - pa))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 20.0)
