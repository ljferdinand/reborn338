# Handover

This is for whoever picks Reborn up next. It says how the project worked, what
keeps it honest, and which open work is most worth doing, in order. Development
stopped in October 2026; `docs/STATUS.md` is the state it stopped in.

## How the work was done

Reborn was built by measurement. A small song was generated to exercise one
control of one device (`tools/rbsgen/`), ReBirth exported it, Reborn rendered
it, and the two were compared in spectra, envelopes and levels; then a law was
fitted and checked against fixtures it was not fitted on. One component was
worked on at a time, and anything found about another component went into
`docs/BACKLOG.md` instead of being fixed on the spot. Three documents carry the
method and must be read before changing any DSP:

- `docs/MEASUREMENT_PROTOCOL.md`: what makes a comparison valid (one device, one
  effect or none, song mode, stereo, the same method on both sides, never a null
  test against ReBirth);
- `docs/NON_OBVIOUS_BEHAVIOUR.md`: behaviours in the code that look like bugs and
  were each established by measurement, and are cheap to break by accident;
- `docs/research/METHOD.md`: the one-component-at-a-time method and the two-role
  procedure used for the effects (`docs/PROVENANCE.md` explains why).

The research record (`docs/research/`) is a lab notebook, dated throughout;
`docs/research/README.md` says how to read it. Before reviving an old number as a
target, check `docs/research/FINDINGS_AUDIT.md`: retired findings stay retired.

New measurements need your own copy of ReBirth 2.0.1, which runs on Windows. The
exports the project used are not published, and neither is the tool that drove
ReBirth to make them.

## What keeps it honest

- **CI and the badge.** Every push builds and tests the engine on Linux, macOS
  and Windows, checks each platform against the recorded baseline, round-trips
  generated songs, builds the app and plugins on Windows, and checks the
  documentation. CI went red unnoticed once in this project's life, for weeks; a
  red badge is the first thing to fix.
- **The platform baseline.** `docs/baselines/windows-x86_64.json` holds
  measurements of ten generated fixtures on the TPT voice. Any change to the 303's
  DSP moves it, and must re-record it on Windows (`tools/measure/platform_baseline.py
  record`) in the same commit. Never re-record it on another platform.
- **The byte-neutral check.** `tools/render/byte_neutral.py` renders the 16 demo
  songs from your own ReBirth install, each soloed to both 303s, and prints a
  pooled hash. At the release the hash on the reference Windows machine was
  `5dbe81f8bb45a9e471213d9f01c10518` (with `REBORN_BASS_ENGINE=tpt`). A change
  meant to alter no sound must leave it unchanged; one that alters sound should
  say by how much (`byte_neutral.py diff`).
- **Round-trips.** `reborn_roundtrip` over the demo songs and the generated
  fixtures must report no problems.
- **The documentation check.** `tools/check_docs.py` fails on a broken link, a
  personal path or the house style's banned dashes.

## Open work, most useful first

Each item names the document that holds its evidence.

1. **The compressor's detector.** On loud compressed passages Reborn's compressor
   cuts 2.5 to 8.1 dB harder than ReBirth's while the uncompressed mixes agree
   within 0.54 dB (`docs/BACKLOG.md`, entry 16; `docs/specs/SPEC_COMP.md`, round
   5). Do it together with the TPT saw's crest factor, 2.3 dB short of ReBirth's
   (entry 22), because a peak detector reads the 303 by its peaks: the two decide
   the level of every compressed 303 together.
2. **Automation timing.** Rides land on the 16th-note grid and step; ReBirth
   applies them at their 1/32 tick and glides (entry 18, with a proposed design).
   Measure the glide first. The maintainer approved this in principle; it moves
   every song's render and the byte-neutral hash. The two smaller timing findings
   belong with it: the delay's far channel at pan 127 (entry 19) and ReBirth's
   64-sample hit grid and late first hit (entry 20).
3. **Transport and song editing.** `docs/research/PLAN_TRANSPORT_MODES.md` has the
   measured gaps and a plan: sections looping on their own lengths, pattern
   changes waiting for the pattern end in Pattern mode, the end of a song, the
   loop range, playing from a host's position. ReBirth's Edit-menu commands are
   the agreed direction for song editing: "Copy touched controls" to the loop or
   song, Initialize Song and Initialize Loop from Pattern mode, and Cut, Copy and
   Paste of the loop that move automation with the bars (decisions of 2026-09-27
   in `docs/research/STATUS_2026-09-27.md`).
4. **Real-time safety.** The TPT voice calls `getenv` about 75 times per audio
   block, the measurement probes in `docs/ENVIRONMENT_PROBES.md`; the 909 allocates
   on every step and the engine on every automation event
   (`docs/research/REVIEW_2026-09.md`, RT1 and RT2; `docs/BACKLOG.md`, entry 10).
   Read the probes once at load and preallocate. Each change can be proved
   byte-neutral with the pooled hash. The concluded probe branches in the 303 could
   then be pruned (review item D4).
5. **The app's controls.** Expose the effect units' own on/off switches, so the
   compressor can be switched on and the strip buttons show whether an effect runs
   (review G1); make solo silence the other sections instead of freezing them
   (G2); add a master-compressor routing button. Patterns longer than 16 steps,
   which the pattern model has room for, would need the grid, the length control
   and the sequencer extended together: the sequencer wraps every pattern at
   step 16 (`src/engine/engine.cpp`, `advanceStep`).
6. **MIDI and automation in the plugin.** The main plugin advertises MIDI input and
   ignores it. Mapping notes to the 303s and drums, ReBirth's MIDI remote map
   (the manual's Appendix C), and host-automatable parameters would make it a
   usable DAW instrument. `docs/PLUGINS.md`.
7. **Effect hosting.** The `.rbsx` format reserves chunks for effect chains
   (`RFXC`, `RFXS`, `docs/FORMAT_RBSX.md`); nothing implements them.
8. **An AddressSanitizer CI job** for the parsers, which read untrusted files and
   were hardened after a review (entry 9).
9. **The app on Linux and macOS.** Both compile in CI on request and have never
   been run.
10. **Skins.** Draw the classic look, or a mod's own skin from the user's copy
    (`docs/SKINNING.md`).
11. **Smaller parity items.** The Open303 voice's 15.65 dB level gap (entry 15; a
    fixed output trim is the most it should get, since it is not a parity voice);
    the distortion's position relative to the channel fader (entry 21); the PCF's
    accepted residuals (entry 14); the 303's retriggered chirp and the Vintage
    switch (`docs/COMPONENT_MATRIX.md`); how ReBirth plays a mod's recordings
    (`docs/MODS.md`).

`docs/BACKLOG.md` holds everything else that was found and not acted on.

## Practicalities

- The house style, if you want to match it: plain prose, no em-dashes, figures
  with their conditions, and uncertainty stated rather than smoothed over.
- The measurement tools read `REBORN_WORKSPACE` and `REBORN_SONGS_DIR`
  (`docs/BUILDING.md`).
- `docs/GLOSSARY.md` explains the vocabulary of the research notes: fixtures,
  batches, cells, supertasks, Role A and Role B.
- Nothing derived from ReBirth may enter the repository: no samples, songs, mods,
  keys, exports, screenshots or bytes of the program (`CONTRIBUTING.md`).
