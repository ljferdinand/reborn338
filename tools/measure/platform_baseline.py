#!/usr/bin/env python3
"""Prove a fresh machine renders the same audio as the reference machine.

`reborn_core` is plain C++17 with no platform code, so the same fixture should
render the same audio on Windows, macOS and Linux. This records a baseline on
one machine and checks another against it, which turns "did my setup work?" from
a feeling into a number.

Bit-identical output is NOT required and should not be expected: MSVC and clang
make different choices about FMA contraction and reassociation, which moves the
last few bits. What must hold is that the MEASUREMENTS agree, since those are
what every parity conclusion rests on.

    python platform_baseline.py record  <renderer> <fixture-dir> <out.json>
    python platform_baseline.py check   <renderer> <fixture-dir> <baseline.json>

Exit code is non-zero if any metric drifts past tolerance, so it works in CI.
"""
import hashlib
import json
import os
import platform
import subprocess
import sys
import tempfile
import wave

import numpy as np

# Tolerances. Peak and rms in dB; centroid and rolloff as a fraction. Generous
# enough for compiler differences, tight enough that a real behavioural change
# cannot hide: every law measured in this project is stated to 0.01 dB or better.
TOL_DB = 0.05
TOL_FRAC = 0.005

# A deliberately varied handful: a resonance extreme, an accent case, a slide, a
# waveform pair, and two register ends. Enough to catch a broken build without
# rendering the whole corpus.
FIXTURES = [
    "h303_cut127_reso000", "h303_cut127_reso127", "h303_cut016_reso060",
    "g303_acc127_run4", "g303_acc000_none", "g303_slide_on_up",
    "g303_wave0_cut064", "g303_wave1_cut064", "g303_note00", "g303_note12",
]


# The native TPT voice, never Open303: Open303 cannot see a single TPT
# constant, so a baseline taken on it would pass while the voice that carries
# every 303 parity law had changed (docs/NON_OBVIOUS_BEHAVIOUR.md). TPT is also
# the default voice since 2026-09-18; the explicit setting keeps the baseline
# independent of that choice.
BASS_ENGINE = "tpt"


def render(renderer, song, out, seconds=4.0):
    subprocess.run([renderer, "--song", song, "--solo", "bass1", "--no-fx",
                    "--seconds", str(seconds), "--out", out],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   env=dict(os.environ, REBORN_BASS_ENGINE=BASS_ENGINE))


def metrics(path):
    with wave.open(path, "rb") as w:
        n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
        raw = w.readframes(n)
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    x = x.reshape(-1, ch)
    mono = x.mean(axis=1)
    db = lambda v: -999.0 if v <= 0 else 20 * np.log10(v)

    N = 1 << 15
    y = np.zeros(N)
    m = min(N, len(mono))
    y[:m] = mono[:m] * np.hanning(m)
    P = np.abs(np.fft.rfft(y)) ** 2
    f = np.fft.rfftfreq(N, 1.0 / sr)
    mag = np.sqrt(P)
    c = np.cumsum(mag)
    band = (f >= 40) & (f <= 15000)
    return {
        "frames": int(n), "rate": int(sr), "channels": int(ch),
        "peak_db": round(db(np.abs(mono).max()), 4),
        "rms_db": round(db(np.sqrt((mono ** 2).mean())), 4),
        "centroid_hz": round(float((f[band] * mag[band]).sum() / mag[band].sum()), 3),
        "rolloff85_hz": round(float(f[np.searchsorted(c, 0.85 * c[-1])]), 3),
        "sha256": hashlib.sha256(raw).hexdigest()[:32],
    }


def gather(renderer, fixture_dir):
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        for stem in FIXTURES:
            song = None
            for root, _d, files in os.walk(fixture_dir):
                if stem + ".rbs" in files:
                    song = os.path.join(root, stem + ".rbs")
                    break
            if song is None:
                print(f"  SKIP {stem} (fixture not found under {fixture_dir})")
                continue
            wav = os.path.join(tmp, stem + ".wav")
            render(renderer, song, wav)
            out[stem] = metrics(wav)
    return out


def commit():
    """The checkout this script sits in. A baseline is only comparable with a
    build of the same engine source, so it records which one it was."""
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              cwd=os.path.dirname(os.path.abspath(__file__)),
                              capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def env():
    return {
        "system": platform.system(), "machine": platform.machine(),
        "python": platform.python_version(),
        "numpy": np.__version__,
        "commit": commit(),
        "bass_engine": BASS_ENGINE,
    }


def main():
    if len(sys.argv) != 5:
        print(__doc__)
        return 2
    mode, renderer, fixture_dir, jpath = sys.argv[1:5]
    got = gather(renderer, fixture_dir)

    if mode == "record":
        with open(jpath, "w") as f:
            json.dump({"environment": env(), "fixtures": got}, f, indent=1)
        print(f"recorded {len(got)} fixtures to {jpath}")
        print(f"  on {env()['system']} / {env()['machine']}")
        return 0

    if mode != "check":
        print(__doc__)
        return 2

    ref = json.load(open(jpath))
    base = ref["fixtures"]
    renv, here = ref["environment"], env()
    print(f"baseline recorded on {renv['system']} / {renv['machine']} "
          f"at {renv.get('commit', 'an unrecorded commit')}")
    print(f"checking on         {here['system']} / {here['machine']} "
          f"at {here['commit']}")
    if renv.get("commit") not in (None, here["commit"]):
        print("  NOTE: different commits. A drift may be an engine change, not the "
              "platform;\n  build the baseline's commit to separate the two.")
    if renv.get("bass_engine", "default") != here["bass_engine"]:
        print(f"  NOTE: baseline bass engine {renv.get('bass_engine', 'default')!r}, "
              f"this run {here['bass_engine']!r}.")
    print()
    bad = 0
    same_bits = 0
    for stem, m in sorted(got.items()):
        if stem not in base:
            print(f"  ?    {stem}: not in baseline")
            continue
        b = m_ref = base[stem]
        problems = []
        for k in ("frames", "rate", "channels"):
            if m[k] != b[k]:
                problems.append(f"{k} {b[k]} -> {m[k]}")
        for k in ("peak_db", "rms_db"):
            if abs(m[k] - b[k]) > TOL_DB:
                problems.append(f"{k} {b[k]:+.3f} -> {m[k]:+.3f}")
        for k in ("centroid_hz", "rolloff85_hz"):
            if b[k] > 0 and abs(m[k] / b[k] - 1.0) > TOL_FRAC:
                problems.append(f"{k} {b[k]:.1f} -> {m[k]:.1f} "
                                f"({(m[k]/b[k]-1)*100:+.2f}%)")
        if m["sha256"] == b["sha256"]:
            same_bits += 1
        if problems:
            bad += 1
            print(f"  FAIL {stem}: " + "; ".join(problems))
        else:
            print(f"  ok   {stem}")
    print()
    print(f"{len(got)-bad}/{len(got)} within tolerance; "
          f"{same_bits}/{len(got)} bit-identical")
    if bad:
        print("\nA metric drifted. That is a REAL difference in the engine's output,")
        print("not a rounding artifact: the tolerances are far wider than compiler")
        print("float variation. Do not fit anything against this build until it is")
        print("understood.")
    elif same_bits < len(got):
        print("\nMetrics agree; some files differ in the last bits. Expected across")
        print("compilers, and harmless: every parity metric here is far coarser.")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
