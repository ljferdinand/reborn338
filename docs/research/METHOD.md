# How a component was taken to parity

Each component (303, 808, 909, PCF, delay, compressor, distortion, in that order)
was worked on alone, as one "supertask". This is the method, written down so that
someone picking up a residual can follow the same discipline. Only the component
under work changed; anything noticed about another component went into
`docs/COMPONENT_MATRIX.md` or `docs/BACKLOG.md` and was left alone. A component was
finished when the maintainer signed it off, not when a measurement turned green.

## 1. Inventory

Before any DSP work, list every control from the manual (chapter 15, Reference, is
the authoritative list; the tutorial chapters carry behaviour and interactions that
Reference leaves out). For each control: name, panel legend, range, documented
behaviour, documented interactions. Also list documented behaviours that are not
controls (the closed hat choking the open hat; only one section able to take the
PCF). Then check each against three questions and write down the gap:

1. Is it decoded from `.rbs`? (`src/io/rbs.cpp`)
2. Is it applied by the engine and the device?
3. Is it reachable from the UI, and does it survive a save and reload?

Missing decode is as much a parity failure as a wrong filter curve, and usually
cheaper to fix. Audit both directions: a control ReBirth has that is never decoded
(dropped), and a control Reborn modulates that ReBirth does not have (phantom).

## 2. Fixtures

Design the fixture set that pins every control down (`docs/MEASUREMENT_PROTOCOL.md`).
Sweeps, not pairs; cross designs where controls interact; every fixture regenerable
from `tools/rbsgen/` with one command; isolation proved with
`tools/rbsgen/isolation_check.py`. **Measure a plane where controls interact, not a
line through it**: the 303's cutoff was once swept at four points at one resonance,
and the two defects hiding in that surface only separated once nine cutoffs by five
resonances were measured.

## 3. Measure and fit

One control at a time, in dependency order: level and pitch before timbre, timbre
before fine spectral shape. Fitting a shape while the level is wrong fits the level
error into the shape. After each fit, re-measure the controls already fitted.

## 4. Interactions and edge cases

The documented behaviours from the inventory, each with a test or a measured
fixture. Parity is usually lost here, because nobody sweeps these: two sounds on one
step, a control at its exact minimum, a retrigger mid-decay, a pattern of non-default
length, a shared column switched mid-song.

## 4b. The ear, on an isolated pair

Before close-out, a listening set: the null pair first, then the component alone,
level-matched and trimmed to the same length, with the ReBirth file sorted beside the
candidates. Every large defect found in August 2026 came from listening to material
a summary statistic had already passed. A report that something "sounds like a
different instrument" is an instruction to widen the measurement, not refine it.

## 5. Close out

Update `docs/COMPONENT_MATRIX.md`; write the measured laws into `docs/PARITY.md`
with the conditions they were measured under; record anything still imperfect as an
accepted residual with its magnitude ("close enough" without a number is not a
finding); confirm the fixtures regenerate and the tests pass.

## The clean-room wall used for the effects

From September 2026 the effects were re-derived in two roles. Role A measured
ReBirth and wrote a behavioural specification with no coefficients in it
(`docs/specs/`). Role B, a fresh context that never saw anything but the
specification and the measured data, wrote and fitted the implementation. Role A
then verified the result independently against ReBirth's exports. See
`docs/PROVENANCE.md` for why, and for the limits of this arrangement.
