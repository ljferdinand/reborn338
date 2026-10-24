# What ReBirth is

Written 2026-07-26 as the context pass for the component supertasks. ReBirth is
the parity target, so what matters is not whether it is faithful to Roland but
what it does.

ReBirth RB-338 shipped in 1997 from Propellerhead, who had made ReCycle in 1994.
It was the first software emulation of classic hardware to be taken seriously as
an instrument rather than a curiosity. Development stopped in January 1999,
desktop support ended in September 2005, and the last desktop build, 2.0.1, was
released free through the ReBirth Museum. Version 1.0 had two 303s and the 808;
2.0 added the 909, the PCF, and the mod system.

That history sets the constraints it was built under: a 1997 consumer CPU had to
run two synths, two drum machines and four effects in real time at 44.1 kHz.
Almost every departure below follows from that budget.

## The drum machines are sample players

**This is the most important finding of the research pass and it changes how the
808 and 909 supertasks should be run.**

ReBirth.dll embeds exactly 46 AIFF recordings, 24 for the 808 and 22 for the
909. Mods replace them with their own AIFFs. There is no circuit model in
ReBirth for either drum machine. Both are samplers, and parity means matching
the sample-selection and sample-processing chain, not a bridged-T network.

The variant structure, measured from the extracted set:

| family | files | measurement | reading |
|---|---|---|---|
| `808_sd1..5` | 5 | dominant fixed at **179.0 Hz** for all five; centroid climbs 780, 928, 1068, 1162, 1163 Hz; peak climbs 0.204 to 0.278 | a **brightness and level ladder at constant pitch** |
| `808_n1..4` | 4 | near-identical spectra and levels, centroid all near 8.2 kHz | round-robin noise, not a knob axis |
| `808_bd` | 1 | 1.65 s, 48.4 Hz | one recording; TONE and DECAY must be DSP on it |
| `808` toms, congas, metals | 1 each | | TUNE must be varispeed |
| `909_sd1..5` | 5 | dominant climbs **122.5, 148.0, 180.3, 215.3, 240.9 Hz**, duration identical at 14007 frames | a **pitch ladder**, roughly 1.2x per step, about an octave total, pre-rendered so length does not change |
| `909_sdn1..3` | 3 | identical spectra, peaks 0.446, 0.844, 0.484 | noise layers or round-robin |
| `909_bd0..4` | 5 | bd1..4 are 0.5 s, dominant 53.8 Hz, centroid **falling** 332 to 237; bd0 is 1.9 s, 41.7 Hz, centroid 1368 | bd1..4 are an **attack or brightness ladder at constant pitch**; bd0 is a different articulation entirely |

Two things follow immediately. First, **the 808 snare has no tune control at
all**. Appendix C's MIDI map lists SD Level, SD **Tone** and SD Snappy at CC
41-43 and nothing else, and the five recordings are a brightness ladder at a
fixed 179.0 Hz, so the control is tone. Chapter 15 is simply wrong when it says
on page 149 that the snare has "Tune and Snappy" and describes a Tune control on
page 150. This also dissolves a conflict that was on record as unresolved: our
measurement showed the knob changing the body-to-noise balance, which is exactly
what a brighter and louder body does without touching the noise. On real 808
hardware TUNE does move the two bridged-T oscillators, so the missing control is
a ReBirth simplification rather than a documentation slip.

Second, the 909 snare TUNE *does* change pitch, in five discrete pre-rendered
steps rather than continuously, and the 909 additionally has a separate SD Tone
at CC 72. ReBirth distinguishes the two controls where it has both, which is why
the 808's single control being Tone is credible rather than a naming accident.

Current state of our implementation, from `src/io/sampleset.cpp`: variant
selection exists for `909_bd`, `909_sd`, `909_sdn` and `808_sd`, but it runs
**once at load time** from the song's initial knob values. Turning a knob in the
app or automating it in a song cannot change the variant. `808_n1..4` are never
loaded at all and the 808 snare noise is synthesised instead. `909_bd0` is never
used. These are concrete, cheap parity items for the 808 and 909 supertasks.

## Where ReBirth departs from the hardware

**The 303 filter range is extended at both ends**, further down and further up
than a real unit. Contemporary reviews consistently note ReBirth reads slightly
brighter than hardware, with a digital edge at the top of the cutoff range that
the analog filter does not have. Since we match ReBirth, that edge is a feature
to reproduce, not an artifact to clean up.

**The 303 accent is weaker than hardware**, in both range and punch. Reviewers
who compared the two directly single this out. The history-dependent accent
sweep of the real accent circuit is very likely not modelled at all. Worth
testing directly with a fixture of consecutive accented steps: if ReBirth's
accent is per-note rather than accumulating, our model should be too.

**The 808 and 909 are recordings**, so every circuit interaction that is not
baked into a recording has to be applied as DSP or not at all. This is where the
shared columns, the hat choke, the accent bus and the tune knobs live.

**The effect rack is simple** by modern standards: one distortion, one
tempo-synced delay, one compressor on the master, and the PCF. The PCF is a
2-pole, 12 dB per octave filter switchable between lowpass and bandpass, driven
by an attack/decay envelope whose triggers come from one of 55 preset patterns.
Only one mixer section at a time can use it, and it sits after the distortion in
series.

**Tempo tops out at 300** in our implementation against a documented higher
limit in ReBirth. Recorded in the audit and still open.

## Where ReBirth excels

**It gets the feel right.** The direct comparisons of the era agree that with
the same control positions on hardware and software you get the same sound to
most ears. Whatever the filter is doing, it is doing the musically important
part.

**The sequencer is exact and the workflow is fast.** 32 patterns per section in
four banks of eight, independent pattern lengths per section, per-section
shuffle with one global amount, and knob rides recorded into the song. The
pattern editing functions in chapter 7 do a lot with very little UI.

**The PCF is its own invention** and has no hardware counterpart. 55 preset
rhythmic filter patterns with per-step velocity, routable to any one section, is
a good idea that ReBirth got to before the plugin world did.

**The mod system** decouples the sound set and the artwork from the engine, and
mods are still findable today. It is the reason songs from 1998 still resolve
their intended sounds.

**It is honest about its scope.** Two synths, two drum machines, four effects,
16 steps. Nothing is half-implemented. That is worth holding onto as a design
value for Reborn.

## Open questions this pass raised

1. Does ReBirth's 303 accent accumulate across consecutive accented steps, or is
   it per-note? Needs a fixture of four consecutive accents at one pitch.
2. What are the 808 BD TONE and DECAY laws, given there is exactly one BD
   recording? Envelope, filter, or a click blend, and in what proportion.
3. What selects `909_bd0`, which is unused by us and unlike the other four?
4. Are `808_n1..4` round-robin, velocity layers, or the SNAPPY axis?
5. Does variant selection in ReBirth follow a knob live, or latch when the sound
   is triggered? This determines whether automation can change a variant
   mid-song, and it is directly testable with a fixture that sweeps the knob.

## Sources

Hardware and history: [ReBirth RB-338 on Wikipedia](https://en.wikipedia.org/wiki/ReBirth_RB-338),
[Sound On Sound review of RB-338 v2](https://www.soundonsound.com/reviews/steinbergpropellerhead-rebirth-rb338-v2),
[MusicTech Vintage Rewind](https://musictech.com/reviews/rewind-propellerhead-rebirth-rb-338/),
[Vintage Synth Explorer](https://www.vintagesynth.com/propellerhead-software/rebirth-rb-338).
Filter analysis: [Tim Stinchcombe, TB-303 diode ladder model](https://www.timstinchcombe.co.uk/index.php?pge=diode2).
808 circuit modelling: [The TR-808 Cymbal, a physically-informed digital model](https://pure.qub.ac.uk/files/125044847/tr_808_cymbal_a_physically_informed_circuit_bendable_digital.pdf).
Everything about ReBirth's own behaviour comes from the v2.01 manual (read as
page images kept outside the repository) and from measurement of ReBirth's exports
and of its drum recordings, made from the author's own copy.
