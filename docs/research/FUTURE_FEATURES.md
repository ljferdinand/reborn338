# Future features, after parity

**Nothing in this file is work. It is a holding pen.**

`docs/research/GOALS.md` is explicit that features wait until parity is reached, and that
the project has already lost time to polishing while gross errors sat
underneath. This file exists so that ideas worth keeping stop being argued about
mid-round and stop being lost.

**The gate on everything here is the same:** all seven component supertasks
(303, 808, 909, PCF, delay, compressor, distortion) plus the cross-cutting mix
work are done and signed off. Then maintenance and features.

**And the rule that outranks every entry:** ReBirth's behaviour stays the
default, exactly, forever. Every item below is opt-in, off by default, and must
be provably byte-identical to the shipped build when it is off. A feature that
changes a default is a parity regression no matter how good it sounds. The
existing `REBORN_*` probes are the model: default-off and verified byte-identical
over the fixture inventory.

---

## 1. A "truer 303" mode: expose the envelope generators ReBirth hides

**Raised by Luke, 2026-08-03.**

### The observation

ReBirth surfaces one envelope control, DECAY, and even that reaches only the
filter. The hardware it emulates has more structure than that, and our engine
already models most of the structure internally because parity forced it to.
Everything needed is behind a `REBORN_*` probe today and simply is not reachable
from the UI or the file format.

### What the hardware has that ReBirth does not surface

Established from published circuit analyses during the 2026-08-03 research pass,
and recorded in `docs/research/FINDINGS_AUDIT.md` under that date.

**Two envelope generators, not one.** A Main Envelope Generator drives the filter
cutoff and a separate VCA Envelope Generator drives the amplifier. ReBirth's
DECAY knob reaches only the first. We already carry both as `mainEnv_` and
`ampEnv_`.

**A gate length that is a real parameter.** A normal 16th gate on the hardware
runs about 0.81 of a step, which is what gives the 303 its staccato feel.
**ReBirth uses 0.500**, measured against five tempos and calibrated against a
known-value render. That difference is not small and it is audible as
articulation. `REBORN_GATE_FRAC` already exists.

**An accent that accumulates.** Published descriptions of the accent sweep
circuit say consecutive accented notes build on a rising floor. **ReBirth's does
not**: four consecutive accents stay within 0.23 dB with a slight droop. A
hardware-accurate accumulating accent is a genuinely different musical
behaviour and a strong candidate for this mode.

**Separate attack control.** The hardware's filter envelope has a fast fixed
attack and no knob. We carry `REBORN_ENV_ATK` and `REBORN_ACC_ATK`.

### What this would expose

Candidates, roughly in order of musical value:

| control | what it does | probe that already exists |
|---|---|---|
| VCA decay, independent of filter decay | separates the two envelopes the hardware separates | `REBORN_AMP_DECAY`, `REBORN_AMP_SUS` |
| gate length | 0.500 ReBirth against about 0.81 hardware, and anything between | `REBORN_GATE_FRAC` |
| accent accumulation | hardware-style rising floor across consecutive accents | none yet |
| filter envelope attack | the hardware is fast and fixed; making it variable is a real sound | `REBORN_ENV_ATK` |
| accent attack and sweep rate | the "wow" | `REBORN_ACC_ATK`, `REBORN_ACC_SWEEP` |
| env mod polarity | ReBirth's swing is bipolar about a centre; unipolar is the other option | `REBORN_EM_CENTRE` |

### Why this is cheap when it comes

Almost all of it is already implemented and measured. The work is not DSP, it is
**plumbing and restraint**: a mode flag, UI, persistence in `.rbsx` but never in
`.rbs`, and a hard guarantee that the ReBirth path is untouched. The measurements
that would justify each default are already in the audit.

### The trap to avoid

**Do not let this mode become a place to put things that failed parity.** If a
constant is wrong against ReBirth, the answer is to fix it, not to expose it as a
feature and let the user dial around the error. Every control here must be
something the hardware genuinely had or genuinely did, with a citation, and its
OFF position must be ReBirth's measured behaviour.

---

## 2. Other things deliberately deferred

Recorded in `docs/research/GOALS.md` and repeated here so this file is the single place to
look: a vintage 303 mode (the switch exists in the file format and is decoded but
unimplemented), a better filter, more patterns, a longer sequencer.

Also outstanding and closer to maintenance than feature: implementing the vintage
switch properly, the `.rbm` sprite decode that blocks the classic skin, and the
two undecoded bytes at the head of every device chunk.
