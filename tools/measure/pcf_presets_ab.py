#!/usr/bin/env python3
"""Put the shipped preset table next to Appendix D's diagrams, pattern by pattern.

Two independent readings of the same 55 patterns. `pcf_presets.inc` was measured
off ReBirth's audio on 2026-08-27; `pcf_appendixd.py` reads the manual's own bar
charts by pixel. Where they agree the pattern is settled. Where they disagree
one of them is wrong and this says which patterns to look at.

    python pcf_presets_ab.py
    python pcf_presets_ab.py --pattern 32
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcf_appendixd import read_all

HERE = os.path.dirname(os.path.abspath(__file__))
INC = os.path.normpath(os.path.join(HERE, "..", "..", "src", "effects",
                                    "pcf_presets.inc"))


def read_inc(path=INC):
    """Parse pcf_presets.inc into {index: (steps32, velocities, length32)}."""
    src = open(path, encoding="utf-8").read()
    arrays = {}
    for m in re.finditer(r"kPcfPat(\d+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        pairs = re.findall(r"\{\s*(\d+)\s*,\s*([0-9.]+)f\s*\}", m.group(2))
        arrays[int(m.group(1))] = [(int(s), float(v)) for s, v in pairs]
    out = {}
    for m in re.finditer(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*kPcfPat(\d+)\s*\}", src):
        length, _, which = int(m.group(1)), int(m.group(2)), int(m.group(3))
        steps = arrays.get(which, [])
        out[which] = ([s for s, _ in steps], [v for _, v in steps], length)
    return out


HORIZON = 384          # thirty-seconds, twelve bars


def expand(steps, vels, length):
    """Roll the pattern out over a fixed horizon, as {step: velocity}.

    The two sources do not agree on what one period is, and they do not have to:
    the audio measurement reports whatever window it analysed while the diagram
    reports the drawn loop. Rolling both out to the same horizon compares the
    thing that matters, which is when the filter actually gets retriggered.
    """
    out = {}
    if not length or length <= 0:
        length = HORIZON
    for base in range(0, HORIZON, length):
        for s, v in zip(steps, vels):
            if s < length and base + s < HORIZON:
                out.setdefault(base + s, v)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pattern", type=int)
    a = ap.parse_args(argv)

    inc = read_inc()
    man = read_all()
    which = [a.pattern] if a.pattern is not None else range(55)

    agree_steps = agree_vel = 0
    print("%4s %-9s %-9s %-13s %s"
          % ("pat", "len32", "ntrig/384", "steps", "velocity"))
    for i in which:
        if i not in inc or i not in man:
            continue
        i_s, i_v, i_len = inc[i]
        m_s_raw, m_v, m_len, _ = man[i]
        m_s = [int(round(s)) for s in m_s_raw]

        A = expand(i_s, i_v, i_len)
        B = expand(m_s, m_v, m_len)
        both = set(A) & set(B)
        jac = len(both) / max(1, len(set(A) | set(B)))
        same_steps = set(A) == set(B)
        if same_steps:
            agree_steps += 1
        if both:
            d = max(abs(A[s] - B[s]) for s in both)
            vel = "max dv %.2f over %d shared" % (d, len(both))
            if same_steps and d <= 0.12:
                agree_vel += 1
        else:
            vel = "no shared steps"
        print("%4d %4s/%-4s %4d/%-4d  steps %-5s  %s"
              % (i, i_len, m_len, len(A), len(B),
                 "same" if same_steps else "%.0f%%" % (100 * jac), vel))
        if a.pattern is not None:
            print("   inc:", "  ".join("%d:%.2f" % (s, A[s])
                                       for s in sorted(A) if s < 96))
            print("   man:", "  ".join("%d:%.2f" % (s, B[s])
                                       for s in sorted(B) if s < 96))

    if a.pattern is None:
        print("\nstep sets identical on %d of 55; of those, velocities within "
              "0.12 on %d" % (agree_steps, agree_vel))
    return 0


if __name__ == "__main__":
    sys.exit(main())
