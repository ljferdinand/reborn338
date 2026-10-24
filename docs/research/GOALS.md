# Goals

## The single ranked priority

**One-to-one parity with ReBirth RB-338's sound and rhythm.** Everything else is
subordinate. When a choice is between parity and anything else (cleaner code, a
better-sounding filter, a modern feature, a faster path), parity wins and the
alternative gets recorded as a possible later option, not taken.

This is deliberate and worth restating because it inverts normal engineering
instinct. ReBirth's 303 is not the best 303 emulation available in 2026. Its
909 is a sample player. Its effects are simple. We are not trying to beat it.
We are trying to make a file that plays in ReBirth play identically here, so
that the music people made in it survives the platform it was made on.

"Identically" means a listener who knows the song cannot tell which one is
playing, and a measurement of the two renders agrees within the tolerances in
`docs/COMPONENT_MATRIX.md`.

## What parity covers

**Sound.** Every voice, at every setting of every control, matches the
corresponding ReBirth export. Not just the default position, and not just the
extremes.

**Rhythm.** Step timing, shuffle, pattern length, pattern and bank switching,
song-mode arrangement, and the exact tick on which an automation event lands.

**Behaviour.** The parts that are neither DSP nor sequencing: the closed hat
choking the open hat, the shared-instrument switches, only one mixer section
being able to use the PCF at a time, what happens when two sounds land on the
same step.

**Files.** Reading and writing `.rbs` losslessly enough that a round trip is
byte-stable in every field we claim to support, and honestly reported where it
is not.

## What we are not doing

We are not decompiling ReBirth as part of this project, and nothing recovered
from its binary enters the codebase. Findings come from measuring its audio
output, inspecting the data files it writes, and reading its manual. This is a
legal constraint and also a practical one: a decompilation would produce 1998
fixed-point machine logic instead of a maintainable codebase.

A separate third-party decompilation does now exist outside this repository
(produced 2026-09 at the maintainer's direction). It is used only to decide which
black-box measurements are worth running; every shipped finding is still sourced
from measurement, no recovered constant is ever copied in, and a two-role wall
keeps re-derived code measurement-sourced. See `docs/PROVENANCE.md` for the full
statement.

We are not shipping anything derived from the product: no samples, no songs, no
mod contents, no artwork.

We are not adding features until parity is reached. A "vintage 303 mode", a
better filter, more patterns, a longer sequencer: all of these are interesting
and all of them wait. The project has already lost time to polishing spectral
gradations while two gross errors sat underneath, and feature work has the same
failure mode.

Ideas worth keeping go in `docs/research/FUTURE_FEATURES.md` rather than being argued
about mid-round or lost. Nothing in that file is work until every supertask is
signed off, and everything in it must be opt-in and byte-identical to the
shipped build when off.

This is not-for-profit software preservation. It is not for sale.

## Order of work

Components are built one at a time, as supertasks. A component is not finished
until Luke says so.

1. **303** (both instances)
2. **808**
3. **909**
4. **PCF**
5. **Delay**
6. **Compressor**
7. **Distortion**

Then, and only then, the whole-mix work: summing, pan law, master chain, and the
demo songs as regression cases.

The order is not arbitrary. The two synths and the drum machines are sources, so
their errors propagate into every downstream measurement; fitting an effect
against a wrong source fits the wrong thing. The PCF comes before the other
effects because it is the only one still completely unwired from the file
format, so it is a decode task as well as a DSP task.

## Definition of done, per component

A component is a candidate for done when all of these hold. Luke makes the call.

1. Every control in `docs/COMPONENT_MATRIX.md` for that component is
   implemented, reachable from the file format, reachable from the UI, and
   round-trips through save and load.
2. Every control has been A/B measured against ReBirth exports at no fewer than
   three points across its range, and the law relating knob value to the
   measured quantity is written down. `docs/PARITY.md` holds the settled
   cross-component and whole-mix laws; a component under active work keeps its
   live laws in its own worklist, which for the 303 filter is
   `docs/research/PLAN_303_ENVELOPE.md`.
3. Documented interactions (choke, shared columns, accent scaling, retrigger
   behaviour) are implemented and each has a test or a measured fixture.
4. The component's fixtures are regenerable from `tools/rbsgen/` with one
   command, so a future change can be re-measured without asking Luke for
   exports again.
5. Nothing in the audit is left open against it, or what remains open is
   explicitly written down as an accepted residual with its magnitude.

## Known limits, stated up front

The 909 is sample-based in ReBirth and sample-based here. Parity for it means
the knob-driven pitch, decay, level and choke handling, plus a sample set the
user supplies. It is not a synthesis problem and will not be solved by one.

ReBirth itself is an emulation, and an imperfect one. Where it departs from the
real Roland hardware we follow ReBirth, because ReBirth is the target. Hardware
research tells us what the circuit was trying to do, which is useful for
choosing a model topology, but the hardware never wins an argument against a
measurement of a ReBirth export. See `docs/REFERENCE_HARDWARE.md`.
