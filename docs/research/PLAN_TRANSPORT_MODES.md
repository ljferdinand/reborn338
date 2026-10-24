# Pattern mode and Song mode: plan to ship both with ReBirth's behaviour

Written 2026-09-26. It began as review finding E1: at the end of a song we wrap to
bar 1, where ReBirth goes silent. Luke's direction was to check whether that was a
Pattern mode versus Song mode question, and to ship both modes, each reproducing
ReBirth's own behaviour.

Three kinds of evidence are used and tagged throughout:
- **DOC**: the v2.01 manual, by printed page (`manualPNGs/`).
- **MEAS**: measured from ReBirth's own exports or its files.
- **UNK**: not established.

Every claim below survived an adversarial pass (three independent skeptics over
all 50 claims, 2026-09-26). Where a skeptic corrected the first draft, the
corrected version is the one written here.

## 1. The short answer

**The Trakka Rekka comparison was like for like.** ReBirth's Export is a Song-mode
operation: the manual's procedure starts with "Make sure you are in Song mode"
(DOC p142). `reborn_render` is Song mode for every `.rbs`, because the loader never
leaves the arrangement empty (`src/io/rbs.cpp:766-775`) and a non-empty
arrangement sets `songMode_` (`src/engine/engine.cpp:116`).

**The mode matters in general, not for this result.** Pattern mode has no song end
at all: it always loops the selected patterns (DOC p17, p144). The Trakka result
comes from two Song-mode defects of ours:

1. **We end the song in the wrong place.** ReBirth has no end, and "the Song
   doesn't really have an end" (DOC p73). Playback runs toward bar 999 (DOC p73,
   p78), and Export renders exactly the Loop, GLOB+11 / 3 bars (MEAS, section 3).
   We size the arrangement from the last instrument or mixer event, which gives
   123 bars for Trakka against its 124-bar loop (`rbs.cpp:738`).
2. **We wrap where ReBirth carries on.** At our end we jump to row 0 and reset the
   tick counter and automation cursor (`engine.cpp:348-352`), so our bar 124
   replays bar 1.

Trakka goes silent because the song ends itself: its automation switches 303a
and the 808 off at bar 121 and 303b and the 909 off at bar 123 (MEAS). That is
the effect the manual's "Creating an End" recipe produces (DOC p78, six steps,
the last being "Deactivate Recording"). One caveat from the adversarial pass: the
export is already exactly zero from the middle of bar 122, before the bar-123
switch-off. So the final silence may come from pattern content or a ride to zero
rather than the switch-off itself.

## 2. The chunk is the Pattern-mode panel, the track is the Song (MEAS today)

The manual says a saved song holds "all the Patterns, all settings in Pattern
mode, the entire Song" (DOC p137). It does not say how they are stored. We now
have measurement.

On 2026-09-26 four isolated fixtures were exported from ReBirth. Each is one 909
rimshot through the delay, with the DELY chunk patched to disagree with the DELY
track's tick-0 dump on steps, feedback and pan at once. The fixtures are in
`fixtures-2026-09-26/delay-chunk-vs-track`, the exports in
`renders/delay-chunk-vs-track-2026-09-26`.

- In Song mode, ReBirth played the **track** values in every case, to 0.1 dB.
- The copy naming the MSM2 mod loaded that mod's rimshot (the dry hit's
  fingerprint changed) and still played the track's delay.

So the chunk is not "the song start with a mod applied". It is the Pattern-mode
state, and the track is the Song. Our engine played the delay from the chunk
(`rbs.cpp`, DELY parse), which was wrong in 8 of the 16 demo songs, 6 of which
name no mod. Luke approved the fix, and it landed after the save-path work
(Stage A2 below, 57c7cfb).

The save path (review W1 to W3, fixed on this branch) is built on the same
split: the writer keeps the chunks as the Pattern-mode panel and the tracks as
the Song, and live GUI edits are captured into the panel.

## 3. Behaviour tables

### Pattern mode

| Behaviour | ReBirth | Reborn today | Gap |
|---|---|---|---|
| Song end | None; the selected patterns always loop (DOC p144) | Bar wrap only (`engine.cpp:338`) | None for the end itself |
| Loop, FF, Rew, Record | FF, Rew and Record are unused (DOC p144, p146); the Loop "has no function" (DOC p17) | No loop exists | The GUI must disable these in Pattern mode |
| Pattern change timing | The body of p21 says a change waits for "the end of the Pattern", for each pattern "regardless of its length". Its heading says "Next Downbeat". The two disagree for patterns under 16 steps (DOC p21, UNK in detail) | Switches at the next 16th step, mid-pattern (`engine.cpp:274-276`); a shuffle-delayed step fires from the new pattern (`:913-915`) | Parity gap. A per-section pending-pattern queue, whose exact trigger L1 settles |
| Independent lengths | "Each one loops independently. Different Patterns can have different lengths" (DOC p147); length edits apply immediately (DOC p36, p46) | All sections read the bar-local `step_`, reset every 16 steps (`engine.cpp:338`, `:717-741`), so a 12-step pattern restarts every bar | Parity gap. Per-section step counters |
| Play and position | UNK: p145's "continue play" is stated for Song mode only | Play restarts at bar 1 (`engine.cpp:94-104`) | Minor; L3 |
| Automation | Not played; controls are manual and stay put (DOC p17) | Not dispatched (`engine.cpp:593`) | Matches |
| State source | The Pattern-mode panel: the chunks (DOC p137, MEAS section 2) | A mixture: 303 and drum knobs from the chunk, mixer and effects from track tick-0 (the delay too since 2026-09-26; it played its chunk before). The app loads straight into Song mode (`app/src/PluginProcessor.cpp:82`) | Parity gap. Pattern mode should run on the panel |
| Switching mode while playing | UNK: whether the panel jumps to Pattern-mode settings or keeps the current ones | The toggle restarts from bar 1 without `engineLock` (`app/src/PluginEditor.cpp:771-778`); any manual pattern pick forces Pattern mode (`:914`, `dock/Bass303Block.h:124`, `dock/DrumBlock.h:76`) | L4 |
| Export | The procedure is Song mode only (DOC p142). Whether a Pattern-mode export renders anything is UNK | No Pattern-mode render path | F6 |

### Song mode

| Behaviour | ReBirth | Reborn today | Gap |
|---|---|---|---|
| Song end | No end; runs to bar 999 (DOC p73, p78). Past the last event it neither stops nor wraps (MEAS, section 4). Whether it holds the last patterns and knob values is a strong inference, not DOC (F1, F2). Bar 999 itself: UNK | Length is last event + 1 over tracks 0 to 4 only (`rbs.cpp:583`, `:601`, `:738`); wraps and resets (`engine.cpp:348-352`) | Parity gap, and the cause of E1 and E2 |
| Loop | Loop On/Off, Start (a bar) and Length (bars) (DOC p144, p146); on, it repeats the range forever (DOC p73, p146); keypad 1 and 2 jump to its ends (DOC p224). Length is GLOB+11 as bars x 3 (MEAS). Start is 0 in every file we have (UNK where it lives; GLOB+6..+10 are zero in all 19 files, so `FORMAT_RBS.md:53`'s "varies per song" is wrong). On/Off: UNK | `loopBars` parsed (`rbs.cpp:411-417`) and read only by the writer and tests. No loop | Parity gap; M0 |
| State on a loop wrap | UNK: whether lap 2 starts from the state at Loop Start or carries the end-of-loop state | n/a; the arrangement wrap restores only part of the state | L2 |
| Pattern change timing | Changes happen at the start of a measure; an early press is moved to the exact bar (DOC p76), and they are recorded only on downbeats (DOC p84). A song's own change is "effectuated immediately", which in context means it overrides a manual pick at once (DOC p72) | Row patterns assigned at the bar boundary (`engine.cpp:340-357`) | Probably matches; F3b |
| Independent lengths | p147's independence is not limited to a mode, and the manual says Song mode plays the patterns back exactly as Pattern mode plays them (DOC p85): it leans toward independence in Song mode too | Restarts every bar | Likely parity gap; F3a measures it first, since it could move every demo song using a length other than 16 |
| Stop, Play, Rew, FF | Play continues from the position (DOC p145, Song mode). Stop while playing halts; stopped, it moves to Loop Start, or to bar 1 when already before it; three presses always reach bar 1 (DOC p145). Rew and FF move 10 bars, the Bar arrows 1 (DOC p145-146). Under sync, SPP sets the start bar (DOC p118, p120), with a possible glitch in the first bars (DOC p118-119) | Play always restarts at bar 1; host position ignored (`PluginProcessor.cpp:374-383`); `prepareToPlay` calls `play()` unconditionally (`:359`) | Parity gap; needs a seek-with-chase primitive |
| Automation | Played. Tempo, the Mixer Mute buttons and Master Level are never automated (DOC p72), nor the Shuffle amount (DOC p73; whether per-section shuffle switches are, UNK). A hand move on an automated control is mixed with the song data and may flicker (DOC p74); under Record a touched control punches in and replaces the song data (DOC p82) | Dispatched (`engine.cpp:593`). A manual pattern pick leaves Song mode | Override semantics missing; L5 in both Record states |
| Manual pattern pick | Allowed; it lasts until the song's next pattern event for that section (DOC p72). When it takes effect: UNK | Forces Pattern mode | Parity gap in the GUI |
| Export | Only the area inside the Loop is saved (DOC p167). Length is exactly GLOB+11 / 3 bars from Loop Start regardless of GLOB+1 (MEAS). Frame count is bars x 44100 x 240 / BPM, truncated, when that is not whole; when it is whole ReBirth gives N or N-1 frames with no rule found (MEAS: Clodhopper 120 bars gives N, Fast Forward 250 gives N-1). Whether the Loop must be on: UNK | `reborn_render` runs `--seconds` (default 8) from bar 1 and wraps past the end (`tools/render/main.cpp:72`, `:213`) | Tool gap; byte-neutral to fix, except the N-1 frame |

## 4. What the exports already show

**Export length follows the loop** (MEAS; re-measured independently in the
adversarial pass):

| Song | GLOB+11 / 3 | Last event bar | Export |
|---|---:|---:|---:|
| Calm | 172 | 176 | 172.000 bars |
| G-Spot | 8 | 209 | 8.000 |
| Trakka Rekka | 124 | 123 | 124.000 (all nine exports) |
| Clodhopper | 120 | 299 | 120.000 |
| Fast Forward | 250 | 249 | 250.000 |
| SuperNova | 210 | 421 | 210.000 |
| Fonda Workout (`rb338-exports`) | 1 | 245 | 1.000 |

The `wholemix-2026-08-27` exports, whose loop was patched to the last event, land
exactly there too.

Three sets break the rule, and nothing observed explains them:
- The Fonda Workout copies of 32 and 240 bars.
- `G-Spot-RB338.wav` at 210 bars.
- The 51 `fixtures-2026-07-23` exports, 20 bars each against a stored loop of 172.

A hand-set loop is the likely reason for all three, but it was never observed, so
they stay out of the length evidence.

**ReBirth does not wrap after the last event.** G-Spot's bar 210 is -113.9 dB
against -28.1 dB at bar 1. Fast Forward's bar 250 is -72.7 dB against -30.1 dB.
Both songs pull faders or mute before the end, so these rule out a wrap but not
hold versus stop. The 07-23 exports rule out a stop at the last event but cannot
tell hold from wrap. F1 separates all three.

**Tooling statements to correct** (byte-neutral):
- `docs/REBIRTH_AUTOMATION.md:185-186`, `tools/rbdrive/README.md:75-77` and
  `tools/rbsgen/rbsgen.py:640-645`, `:678-684` say a Song-mode export renders "the
  entire arrangement". It renders the Loop (`docs/FORMAT_RBS.md:91` has it right).
- `FORMAT_RBS.md:53`, GLOB+6..+10 "varies per song": they are zero in all 19
  files.
- `renders/03-Clodhopper-RB338-303-2-distortion-enabled.wav` is a Trakka Rekka
  export: 9,441,841 frames, per-bar match 0.09 dB. It belongs in
  `FINDINGS_AUDIT.md`.

## 5. Measurement plan

Load the `ab-parity` skill and run `tools/rbsgen/isolation_check.py` on every
fixture. Unless noted, a fixture is one 909 in sample mode with the rack off, our
closest null. rbdrive exports Song mode only, about 15 s each, and takes the
foreground.

rbsgen needs three byte-neutral options first:
- a loop length independent of the events;
- a per-bar pattern list;
- a GLOB override for +0, +1 and +6..+10.

Its DELY chunk can already be patched after generation, as today's E3 fixtures
did.

**M0, GLOB decode (gates F4 to F7).** Save an rbsgen song from ReBirth after
changing exactly one thing each time, then diff the files:
1. the Song/Pattern switch;
2. Loop On/Off;
3. Loop Start +1;
4. Loop Length +1.

rbdrive can press keypad locate keys but has no mode, loop-toggle or length
command. So this needs either four clicks from Luke or a small rbdrive extension
that clicks panel coordinates. What we know narrows it. GLOB+1 is not a mode
Export honours, since Trakka (+1 = 0) and Calm (+1 = 1) both exported with their
automation. GLOB+0 is 1 in all 16 demo songs and 0 in all four default songs,
which makes it the better candidate for the mode. rbsgen and our writer both
write 1, 1.

**F1, end hold.** Pattern A in bar 1, pattern B in bar 2, nothing after, loop 6
bars.
- Bars 3 to 6 playing B means hold.
- Bar 3 playing A means wrap.
- Silence means stop.

**F2a and F2b, state hold.** A 909 level ride at bar 2 (F2a) and a mixer volume
ride (F2b, the folded mixer path), each with a 4-bar loop. Compare bars 3 and 4
against bar 2.

**F3a, Song-mode polymeter.** A 12-step kick pattern for 4 bars. Onsets every 12
steps mean independence; every 16 means a restart at the bar. Before changing
anything, count the demo songs this would touch.

**F3b, Song-mode change timing.** A 12-step A in bar 1, then B in bar 2. Does B
start at bar 2, step 0, or after A completes?

**F4, Loop Start and chase** (needs M0). Loop Start bar 3, length 2, with the
state set in bars 1 and 2. Does the export start at the Loop Start, and does it
chase the earlier state?

**F5, Loop switch** (needs M0). F1 with Loop off: does Export need the Loop on?

**F6a and F6b, Pattern-mode export** (needs M0). A panel/track disagreement on a
909 level (F6a), and a 12-step pattern (F6b).

**F7, limits** (low priority). Loop Start 997 with length 4, and an oversized
GLOB+11.

**Frame count.** Collect every whole-number export and look for the rule that
gives N or N-1 before claiming the render tool reproduces Export's length
exactly.

**What Export cannot observe:**
- Live Pattern-mode changes.
- Lap 2 of a live loop.
- The Stop/Play/Rewind positions.
- A mode switch mid-play.
- Manual override of automated controls.

The route for these is WASAPI loopback (`docs/REBIRTH_AUTOMATION.md:161-177`):
transport from the keypad (DOC p224), patterns from the typewriter keys, and knob
moves from loopMIDI CCs. Loopback gives timing and relative level, not parity
level, and has a buffer of several seconds. So the designs measure against the
audio's own grid (a step-0 kick as metronome) and keep hypotheses at least one
step apart. Under MIDI sync, discard the first bars after Start (DOC p118). The
live set:
- **L1**, Pattern-mode change timing. This is the p21 heading-versus-body question.
- **L2**, loop-wrap state.
- **L3**, Stop/Play/Stop positions.
- **L4**, mode switch mid-play.
- **L5**, manual override, with Record off and on.

## 6. Implementation plan

**Stage A, save path (byte-neutral, done on this branch).** This covers
review W1 to W3:
- round-trip the arrangement and all nine tracks, including the timed DELY and
  COMP events, carried then and not played (the DELY's play since Stage A2);
- keep the chunks as the Pattern-mode panel and the tracks as the Song;
- capture live edits into the panel;
- write faders with the inverse of the exponential law;
- pass GLOB bytes we do not decode straight through.

**Stage A2, the delay plays its track (parity, approved 2026-09-26).** Song mode
plays the DELY track's tick-0 dump and its 4,435 timed rides across the demo
songs; the chunk stays the Pattern-mode panel. Verify against the four E3 exports
and re-measure the delay-heavy demo songs. The compressor's 156 timed rides are
the same class of gap and are waiting on Luke's call.
**Done 2026-09-26** (57c7cfb): our renders of the four E3 fixtures now match
ReBirth's echo times, balance and per-repeat ratio, and full-rack renders change
in 12 demo songs, the 8 disagreeing ones plus 4 with rides inside 90 s
(`FINDINGS_AUDIT.md`).

**Stage B, engine transport (parity changes, sign-off).**
- Replace the arrangement wrap with an absolute song position, bars 1 to 999,
  that holds the last row and state past the last event (F1, F2). The loader stops
  deriving length from the last event. This retires review E1 and E2.
- Add Loop on/off, start and length, wrapping to Loop Start with the state
  semantics L2 finds.
- Add a `seek(tick)` that chases automation and folds the mixer and effect tick-0
  values into the same chase. That fixes the partial restore after a wrap or a
  Stop/Play, and the row clamp that desynchronises automation (`engine.cpp:343`).
- Give Pattern mode per-section step counters (DOC p147) and a per-section
  pending-pattern queue (trigger per L1). It runs on the Pattern-mode panel state.
- Song-mode per-section counters, as the manual leans (DOC p85, p147), once F3a
  confirms and the demo-song impact is counted.
- Leave the PCF's free-running clock alone.

**Stage C, render tool (byte-neutral).**
- Add `reborn_render --mode song|pattern` and `--bars`.
- With no length given, render the Loop range, so a render matches an Export
  frame for frame except the unexplained N-1 case.
- Output that exists today must stay identical: confirm with `regress_ab.py`,
  `REBORN_BASS_ENGINE=tpt` on both sides.

**Stage D, GUI, folded into `PLAN_GUI.md` Stage 4.**
- The Stage 4 "bar-loop" is ReBirth's own Loop, so reword `PLAN_GUI.md:192-194`
  and `:210-211`. It is a Song-mode transport control, it is saved in GLOB, and it
  is the export range.
- The mode switch stops restarting at bar 1 and takes `engineLock`, and so does
  `togglePlay`.
- A pattern pick queues in Pattern mode and overrides until the next song event
  in Song mode, instead of forcing Pattern mode.
- Add:
  - the p145-146 Stop, Play, Rew and FF rules;
  - a Bar display;
  - keypad 1 and 2.
- Loop, Rew, FF and Record are disabled in Pattern mode.
- Map a host's position to the song position the way SPP does, and review the
  unconditional `play()` in `prepareToPlay`.
- Later: the Edit menu's Initialize Song, Initialize Loop and Copy touched
  controls (DOC p75, p83, p176-177). The manual describes Initialize Song
  inconsistently: p75 says the events are inserted at the beginning, p177 says
  every measure is filled.

**Parity changes needing Luke's sign-off:**
- hold at the end;
- loop wrap state;
- Pattern-mode queue and counters;
- Song-mode polymeter;
- manual override.

Byte-neutral: the writer, the loader's loop fields, the render tool, the GUI, the
rbsgen options, and the doc corrections in section 4.

## 7. Risks and open questions

- **The mode may not be stored in the file.** If M0 shows ReBirth keeps no mode in
  the song, the mode a song opens in becomes a Reborn default, and Export stays
  Song mode by construction.
- **F3a could move many renders.** Count the affected demo songs before
  proposing the change.
- **Some behaviour is only observable live.** L2, L4 and L5 give timing and
  relative level, not parity-grade level.
- **Drum track param 0 may be misnamed.** It is probably the section's PATTERN
  on/off switch rather than a mute. The manual excludes the Mixer's Mute buttons
  from automation (DOC p72), while the PATTERN switch is recordable (DOC p78,
  p147). This is inference, and behaviour is identical either way. Correct the
  naming in `reborn/CLAUDE.md` once confirmed.
- **Still unknown after this plan:** what bar 999 does, the loop's maximum length,
  whether Export needs the Loop on, and the N-1 frame.
