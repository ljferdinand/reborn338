# Reborn 338 GUI: the instrument (design plan)

Status: APPROVED (Luke's sign-off 2026-09-18). Section 11 records the settled
decisions. Nothing is built yet; implementation waits for Luke's explicit go.
This supersedes the current single-window editor (`app/src/PluginEditor.*`),
which reaches 1-to-1 control parity but is not yet an instrument.

ReBirth's own GUI is the lodestar: it compresses a great deal of control into a
small window and we honour that. Condense hard; keep everything legible and
usable by eyes, mouse and multi-touch.

## 1. Principles

- **ReBirth density.** Small, tightly-packed panels; no wasted space; a control
  is as small as it can be while staying hittable by finger and mouse.
- **Every block is a rectangle that can undock, float and snap.** The default
  interface is those blocks locked together into one cohesive instrument.
- **Control language, uniform across the app:**
  - Continuous value -> **dial**, labelled with its numeric start and end at the
    arc ends, tick markers along the arc, and a live value readout.
  - Binary -> **toggle**, with an **LED** that lights when active.
  - Discrete count/selector (delay Steps 1..16, PCF Pattern 0..54) -> a **numeric
    stepper**.
  - Any active button or function shows an **LED**, ReBirth-style (playing,
    routed, mode engaged, section active, loop on).
- **Never strand a component.** Placement persists, but is always validated
  against the live displays; a lost or changed screen never makes a component
  unreachable.

## 2. Dockable components (the blocks)

Ten independently dockable/floatable units:

1. **Transport** (the top bar, and the master dock): shuffle, tempo, sync +
   MIDI, pattern/song mode, play/stop, and the bar-loop toggle with start and
   length. Can float on its own, or act as the dock every other block attaches to.
2. **Bass 1** (303 #1) 3. **Bass 2** (303 #2): voice dials, an always-visible
   16-step programmer, a pattern selector, and a mixer strip (level, pan, delay
   send, DIST/PCF/COMP routing).
3. **808** 5. **909**: voice dials, always-visible 16-step grid, pattern
   selector, mixer strip. (The 808 grid carries all 16 voices; the 909 carries
   soft/flam per step.)
6. **Master**: master volume + the master compressor trigger (engage + LED).
7. **PCF**: Pattern (stepper), Mode (BP/LP toggle), Freq, Q, Amount, Decay dials,
   unit on/off (LED).
8. **Delay**: Steps (stepper), Straight/Triplet (toggle), Feedback, Pan dials.
9. **Distortion**: Amount, Shape dials, unit on/off (LED).
10. **Compressor**: Ratio, Threshold dials, on/off + target (LED).

Each block owns a fixed intrinsic size (its condensed ReBirth-dense layout). The
block does not reflow; it docks and floats as a unit.

## 3. Default layout

On first launch the blocks are snapped together into one instrument:

```
+-----------------------------------------------------------------------+
|  TRANSPORT: shuffle | tempo | sync/MIDI | pat/song | <play> | loop    |
+---------------------------------------------------+-------------------+
|  BASS 1  dials | 16-step programmer | pat | mixer |  MASTER  (+comp)  |
+---------------------------------------------------+-------------------+
|  BASS 2  dials | 16-step programmer | pat | mixer |  PCF              |
+---------------------------------------------------+-------------------+
|  808     dials | 16-step grid       | pat | mixer |  DELAY            |
+---------------------------------------------------+-------------------+
|  909     dials | 16-step grid       | pat | mixer |  DIST  |  COMP    |
+---------------------------------------------------+-------------------+
```

Instruments stack on the left; the effect/master blocks form a column on the
right. Exact pixel dimensions are set in Stage 2 against the real dense controls.
Everything here is snapped; any block can be pulled out.

## 4. Visual and control language

A new look-and-feel layer draws every control, so the modern skin and (later) a
bitmap mod skin share one control model.

- **Dial.** A rotary arc from ~7 o'clock to ~5 o'clock. The numeric start value
  sits at the left arc end, the end value at the right, with 4-8 tick marks along
  the arc. The current value shows in the dial's centre (or just below) and
  updates live. Drag vertical/rotary to change; double-click resets to default;
  fine-drag with a modifier. Touch: the whole dial rect is the target (>= ~44 px).
- **Toggle.** A rectangular switch with an inset LED. Off = dark LED; on = lit in
  the block's accent colour. Label beside it. Two-state text where it helps
  (BP/LP, Straight/Triplet).
- **Stepper.** A small numeric field with up/down affordances (delay Steps 1..16,
  PCF Pattern 0..54). Shows the integer; scrolls/drags/taps to change.
- **LED.** A small round or bar indicator used everywhere a state is active:
  the transport play LED, per-section DIST/PCF/COMP routing LEDs, unit-on LEDs,
  the loop LED, the current-step playhead in the programmers.
- **Step programmers.**
  - 303: a **ReBirth-style keyboard programmer** is the primary note entry (a
    piano keyboard drives the 16-step line, ReBirth's own idiom). Per step: note,
    accent, slide, octave. **Stretch goal:** a toggle that switches the block to
    the semitone-column grid we have now, which some users find more intuitive.
  - Drums: 16 columns x voice rows; cell LED lit when a hit is placed; accent,
    and for the 909 soft ('s') and flam ('f') per step.
- **Colour + type.** Each instrument keeps its accent hue; effects a neutral
  steel. Dense small type, high contrast, legible at the condensed size.

## 5. Docking and floating architecture

Recommendation: **every block is its own top-level window** (a lightweight
frameless `juce::TopLevelWindow` subclass), magnetically snapping to its
neighbours. On launch they are positioned adjacent so they read as one
instrument. This is the only model that truly satisfies "moved arbitrarily around
the screen" and "multiple screens"; a single OS window cannot span displays.

- **Snapping.** While a block is dragged, its four edges are tested against every
  other block's edges. Within a threshold (~12 px) the block snaps so the edges
  are flush and aligned, on any side. Snapped blocks form a **group**.
- **Groups move together.** Dragging any block in a group moves the whole group.
  Dragging a block away from its group past the threshold detaches it.
- **The transport as master dock.** The transport is an ordinary block that can
  float, but it is also the natural anchor: "dock everything" re-snaps all blocks
  into the default layout around it, and it carries the app's menu/close.
- **Chrome.** Frameless blocks with a thin title/grab strip (drag handle, undock/
  dock affordance). Right-click a block for dock/undock/reset.
- **Windowing detail (for sign-off):** grouped blocks stay as separate OS windows
  that track each other (simplest, most flexible), rather than merging into one
  OS window. They raise/lower together and share a taskbar presence via the
  transport.

Fallback if cross-window snapping proves too fiddly on Windows: a single dock
window that tiles docked blocks, with pop-out child windows for undocked ones
(re-docking on snap). It loses nothing functionally except per-block placement
precision across a shared screen. Recommendation is the all-windows model; the
fallback is noted so a Stage-1 spike can decide with evidence.

## 6. Multi-screen safety (a hard requirement)

- Positions are stored in absolute virtual-desktop coordinates, but **every
  position is validated on load and on any display change** against
  `juce::Desktop::getDisplays()`.
- A block whose grab strip would not land inside any display's usable area is
  relocated onto the nearest available display (clamped fully on-screen); if no
  saved position is usable, it returns to its default-layout slot on the primary
  display.
- We subscribe to display changes (`Desktop::addGlobalMouseListener` /
  `displaysChanged`) and re-validate every block the moment a screen is
  added/removed or the arrangement changes.
- Invariant: **there is always a path back to a visible screen.** We never save a
  position we would not be willing to restore, and restore always clamps.

## 7. Persistence

- The baseline is a **per-user default layout file** (a `juce::PropertiesFile`/
  ValueTree under the app's user data dir) that persists across sessions. Loading
  a different song does not rearrange the user's windows.
- **An `.rbsx` MAY carry its own saved layout, which OVERRIDES the per-user
  default for that song** (so a song can ship with a purpose-built window
  arrangement). Resolution order on open: the `.rbsx` layout if present, else the
  per-user file, else the section-3 default. Every source is validated per
  section 6 before use.
- Stored per block: docked/floating, group membership, position, size, visible/
  hidden. "Reset layout" restores the section-3 default.

## 8. Mods keep a fixed layout

When a ReBirth `.rbm` mod skin is active, the layout is FIXED to the mod's bitmap
skin (one window, no undocking); docking is a feature of the modern skin only.
(Mod SKIN rendering is currently blocked, since the sprites are not decodable
JPEGs, so today the modern skin is the only skin and docking is always on. The
architecture reserves the fixed-skin path so mods slot in when/if the sprites are
decoded.)

## 9. Input: mouse and multi-touch

- Touch targets: dials >= ~44 px, toggles/steppers >= ~28 px tall, step cells
  >= ~20 px. JUCE delivers touch as mouse plus multi-touch sources.
- Multi-touch: more than one control can be manipulated at once (JUCE
  `MouseInputSource` per finger). Drag-to-snap works by finger.
- No hover-only affordances that a touch user cannot reach; value readouts are
  always visible or appear on touch-down, not on hover alone.

## 10. Implementation stages (after sign-off)

- **Stage 1 - Docking framework.** The block-window abstraction, undock/float,
  magnetic snapping, grouping, group movement, multi-screen validation +
  recovery, and layout persistence. Proven first with the CURRENT panel visuals
  wrapped as blocks, so the hard architecture is validated before the visual
  redesign. Includes the cross-window-snapping spike (section 5) to confirm the
  model or trigger the fallback.
- **Stage 2 - Control language + one instrument.** The dial/toggle/stepper/LED
  look-and-feel, and one instrument block (a 303) redesigned to full ReBirth
  density as the reference block, including its ReBirth-style keyboard programmer.
- **Stage 3 - Replicate the blocks.** The other three instruments, the master and
  the four effect panels, in the new control language; the default snapped
  layout. Stretch: the 303 keyboard/semitone-column toggle.
- **Stage 4 - Transport + loop.** The top bar: shuffle, tempo, sync/MIDI, pattern/
  song, transport, and the bar-loop toggle with start/length (loop needs a small
  engine addition: a loop range over the arrangement).
- **Stage 5 - Polish + the mod fixed-layout hook.** Touch pass, LED pass, reset/
  dock-all, and the reserved fixed-skin path.

Each stage builds, launches without crash, and is shown to Luke before the next.
GUI has no headless coverage, so each stage ends with a real launch.

## 11. Settled decisions (Luke, 2026-09-18)

1. **Docking model:** SPIKE the all-blocks-are-windows magnetic model in Stage 1;
   fall back to single-dock + pop-out only if Windows fights the cross-window
   snapping. Evidence decides.
2. **Default arrangement:** the section-3 tiling (instruments stacked left,
   effects column right) is approved.
3. **303 programmer:** a ReBirth-style **keyboard** programmer is the primary
   entry; a toggle to the semitone-column grid is a **stretch goal**.
4. **Bar-loop:** IN SCOPE (Stage 4) - a loop range over the arrangement, with a
   toggle and start/length. New engine feature.
5. **Persistence:** a per-user default layout file that persists across sessions,
   **overridable by a layout saved in an `.rbsx`** (section 7 resolution order).
6. **Dial readout:** always-on, small.

## 12. Stage 1 progress

### 12.1 Cross-window snapping spike (2026-09-18)

The spike (`app/dock_spike/`, built as `reborn_dock_spike` under
`-DREBORN_BUILD_APP=ON`) stands up all ten blocks as separate frameless
`juce::TopLevelWindow`s driven by the real docking classes, with placeholder
content sized to the section-3 tiling. Its purpose is section-5's one question:
does the all-blocks-are-windows magnetic model hold on Windows, or do we fall
back to a single dock window with pop-outs.

The geometry it relies on is a dependency-free core, `app/src/dock/DockGeometry.h`
(snapping, group detection, multi-screen recovery), proven headlessly by
`reborn_dock_tests` (15 cases, including a recovery sweep over thousands of
saved positions across a two-monitor layout with a negative-origin screen). The
JUCE glue is `app/src/dock/BlockWindow.h` (a frameless block window with a grab
strip) and `app/src/dock/DockManager.h` (drag, snap, group move, recovery,
`juce::PropertiesFile` persistence). Both graduate into the editor in Stage 1
proper; the spike is not throwaway.

**What the spike confirmed autonomously:** ten frameless top-level windows
instantiate and render on Windows; the section-3 default tiling is exact and
flush (adjacent edges align to the pixel, so blocks group cleanly); multi-screen
recovery fires on load, verified end to end by saving TRANSPORT at x=8000 (off
every screen), relaunching, and reading back its real window rect at x=1120
(clamped fully on-screen) while every other block sat in its default slot; the
`PropertiesFile` load path works on real disk; no launch crash.

**What needs Luke's hands (drag feel cannot be self-verified):** magnetic
snapping while dragging; whole-group movement; Alt-drag to tear one block out of
a group; and the round trip of moving a block to a second monitor, relaunching
(it returns) versus unplugging that monitor (it recovers).

**One interaction choice made in the spike, for Luke to confirm by feel:**
section 5 says both "dragging any block moves the whole group" and "dragging a
block away past the threshold detaches it," which pull against each other. The
spike resolves it as: a normal drag moves the whole group, and **Alt-drag tears
a single block out**. If a distance-based tear-out feels better in the hand, it
is a small change.

**Model verdict:** the all-blocks-are-windows model is confirmed on every
criterion measurable without a human dragging; the fallback (section 5) stays
unused pending Luke's drag-feel check. Stage 1 proper (wrapping the real panels
as blocks) proceeds on this model unless the drag-feel check says otherwise.

### 12.2 Real panels wrapped as blocks (2026-09-18)

The framework is now proven against REAL content, not placeholders.
`app/src/dock/DevicePanel.h` is a self-contained, dockable panel Component that
renders one `reborn::Processor`'s parameters as the exact controls the shipped
editor's `buildPanel` produces today: rotary dials, the delay-Steps / PCF-Pattern
integer steppers, and the BP/LP and Straight/Triplet toggles, with the same
hidden-param rules, the same functional grouping, and the same param binding. It
owns its paint, layout and `refresh()`, so the same component can live in the
single-window plugin editor or as the content of a floating dock block. It is
driven by a bare `reborn::Processor*`, so a plain `reborn::Engine` populates it
with no JUCE AudioProcessor wrapper.

The dock harness now drives a live `reborn::Engine` and hosts real DevicePanels
for BASS 1, BASS 2, PCF, DELAY, DIST and COMP as dock blocks (turning a dial or
flipping a switch drives the engine); the two drum machines and the transport /
master stay placeholders until their composite block layout (dials plus grid plus
mixer, and the non-parameter transport controls) is designed in Stages 2 to 4.
`RebornLookAndFeel.h` was narrowed from `juce_audio_utils` to `juce_gui_basics`
so the look can be reused in a GUI-only build without the audio stack; the shipped
editor includes `juce_audio_utils` itself and was rebuilt and launch-checked to
confirm no regression.

`DevicePanel` is the reusable panel unit for the whole rebuild: Stage 2 restyles
it into the new dial/toggle/LED control language, and the single-window plugin
editor migrates onto it so both paths share one panel implementation.

### 12.3 Docked mode wired into the real editor (2026-09-18)

The drag feel was approved on the spike ("feels great"), so the all-blocks model
is confirmed and the fallback is retired. The shipped editor now carries a
standalone-only docked mode:

- A **Dock** toggle appears in the transport, but only when
  `wrapperType == Standalone`. A plugin editor is one window the host embeds, so
  docking never activates there; the plugin path is unchanged.
- Toggling Dock pops the eight device/effect panels out into floating
  `BlockWindow`s hosting real `DevicePanel`s bound to the live processor, compacts
  the main window to the performance surface (transport, mixer strips, edit bar,
  grid, arrangement) at the primary display's top-left, and flow-packs the panels
  into columns beside it. Tall panels (the drum dial banks) are capped and scroll
  inside their block. Toggling off returns them and restores the window.
- The choice persists (`Reborn338.layout`, a per-user `PropertiesFile`) and is
  restored on the next launch; block positions save and reload; a 303 voice swap
  or a session restore rebuilds the blocks while keeping their positions (stable
  dock keys). Multi-screen recovery runs on every load.
- Verified on the real Standalone by DPI-aware window-rect enumeration and a
  4K capture: main compact at the top-left, all eight panels placed on-screen in
  columns, no launch crash, plugin build unaffected. (The display is 4K at 200%;
  a DPI-unaware capture only sees the left half, which is why an earlier capture
  looked wrong.)

Still to come in Stage 1's spirit but deferred to the redesign stages: the mixer
strips, the step grid and the transport themselves becoming blocks, so the main
window can shrink to just the transport (the master dock). That is Stage 2 to 4
work; the docking architecture it needs is now proven end to end.

## 13. Stage 2 progress

### 13.1 The control language (2026-09-19)

`app/src/dock/RebornControls.h` is the control language from section 4, as
reusable controls that bind straight to a `reborn::Processor` parameter:

- **RebornDial:** a rotary arc from ~7 to ~5 o'clock with its numeric start and
  end at the arc ends, tick marks, and an always-on value readout. Vertical/
  rotary drag, double-click resets to the parameter default, Shift for a fine
  drag, wheel to nudge. Values carry the module's unit when it has one (st, dB,
  :1) and otherwise read 0..100 across the travel.
- **RebornToggle:** a switch with an inset LED that lights in the block accent,
  with two-state text (BP/LP, Straight/Triplet).
- **RebornStepper:** a -/+ numeric field for discrete counts (Steps 1..16,
  Pattern 0..54).
- **drawLed:** the shared indicator used across the controls.

`DevicePanel` builds these instead of raw JUCE widgets, so every panel in the
docked instrument reads in one control language while keeping the same
hidden-param rules and grouping.

### 13.2 The 303 reference block (2026-09-19)

`app/src/dock/Bass303Block.h` is the reference instrument block: the voice dials
(a `DevicePanel` in the new control language), a pattern selector, and the
ReBirth-style keyboard programmer. `app/src/dock/KeyboardProgrammer.h` is that
programmer: a 16-step line above a one-octave piano keyboard, with per-step note
entry (select a step, play a key, it sets the note and gate and advances) and
ACC / SLD / OCT- / OCT+ / REST for the selected step, showing the beat-group
tint, the current-step playhead and octave markers. It is driven by a bare
`reborn::Engine` with an optional `juce::SpinLock`, so it edits safely under the
audio thread in the app and lock-free in the GUI harness.

Both 303 slots in the docked editor now use `Bass303Block`. Verified on the real
Standalone (DPI-aware 4K capture): the block shows the dials, the PATTERN
selector and the keyboard programmer with the loaded pattern's notes; the other
panels carry the new control language; no launch crash, plugin build clean, tests
green.

Folding each instrument's mixer strip into its block came in 13.3; the 303
semitone-column programmer toggle (section 4 stretch goal) came in 13.6.

### 13.3 Fully modular: the instrument blocks and a transport-only main (2026-09-19)

Every instrument is now a self-contained block and the main window is the
transport master dock.

- The dial and toggle bind to any value SOURCE (a getter + setter), not only a
  `reborn::Processor` parameter, so they drive a plain `MixerChannel` field too.
  `LedButton` is a compact square button with an LED for routing and mute/solo.
- `MixerStrip.h`: level, pan and delay-send dials plus the DIST / PCF / COMP
  routing and mute / solo, bound to the engine channel and its routing (held
  under the lock). The level reads in dB (the fader is an exponential gain that
  runs past unity).
- `DrumGrid.h`: the always-visible 16-step x voice-row grid (the drum counterpart
  to the 303 keyboard) with accent, and the 909's soft and flam; beat-group tint
  and playhead. `DrumBlock.h`: dials + pattern selector + grid + mixer strip.
- `Bass303Block` gains a mixer strip. The docked editor now uses `Bass303Block`
  for the two 303s and `DrumBlock` for the 808/909; each block carries its own
  dials, mixer, pattern selector and step programmer.
- **The docked main window is now transport-only:** the header (transport) plus
  the song arrangement. The mixer strips, edit bar and step grid left the main
  window for the blocks, and docking is the default interface, so the app opens
  as the modular instrument rather than the classic single window.

Verified on the real Standalone (DPI-aware capture): transport-only main, each
instrument a full block (BASS with dials + mixer + pattern + keyboard, drums with
dials + mixer + pattern + grid), effects in the control language; no crash, tests
green. Deferred polish: the drum dial banks make a tall block, and the 303
semitone-column toggle (stretch) is still open.

### 13.4 Dense dial redesign (2026-09-19)

Luke's call: shrink every dial, drop the accent value-arc ("energy bar"), and let
the clock hand, hash marks and numbers carry the position, made bolder. Now the
`RebornDial`:
- draws no value arc; the position reads from a bold accent HAND, bold hash marks
  (the mark nearest the value lit in accent), and bold endpoint numbers at the
  arc ends;
- is compact (`DevicePanel` cell 60x64, was 82x92; mixer strip 66 tall, was 78)
  with tightened spacing and bold value/label text.
This shrinks every block markedly (the 909's full dial bank drops from ~5 rows to
~3-4), which also relieves the tall-drum-block problem. The channel LEVEL reads in
dB.

Next (Luke's idea, 2026-09-19): keep these ReBirth-style per-instrument controls,
and add a "GRID" button that opens one large step grid switchable between the four
engines with the arrow keys, two-way synced with the per-block programmers. The
sync is free: the block programmers and the grid all edit the same engine pattern
data, so an edit in one shows in the other with no extra plumbing.

### 13.5 The shared GRID overlay (2026-09-19)

Built. `app/src/dock/BassGrid.h` is the 303 semitone-column grid (notes as rows
plus ACCENT / SLIDE / OCTAVE), the grid form of a 303 pattern.
`app/src/dock/GridOverlay.h` is the shared overlay: it holds a grid per engine
(BassGrid x2, DrumGrid x2), shows one at a time, and switches with the left/right
arrow keys or the header arrows. A **GRID** button in the transport (and the G
key) opens/closes it as a floating dock block; right-click the strip or press G to
close.

It follows the focused instrument. Each block is its own top-level window, so
exactly one is active at a time; `BlockWindow` now reports activation
(`onActivated`), and the editor tracks the last-active instrument (ignoring the
transport taking focus when its GRID button is clicked) and points the grid at it.
Clicking the 909 block re-points an open grid to the 909; the arrows switch without
the mouse. Sync is automatic because the grid and the block programmers edit the
same engine pattern data. `BlockWindow` also gained a self-move fallback so the
grid window drags even though it is not registered with the DockManager.

Verified on the real Standalone (DPI-aware capture): GRID opens a floating grid
showing the focused engine (drums as voice grids, 303s as semitone grids), header
names the engine, arrows switch; no crash.

### 13.6 Polish (2026-09-19)

- **Global G:** `BlockWindow` now takes keyboard focus and forwards keys
  (`onKey`), so G toggles the shared grid from any block, not just the transport.
- **Tighter drum grid:** `DrumGrid` row height 17->15 and gutter 74->66.
- **303 KEY/GRID toggle (the section-4 stretch goal):** `Bass303Block` holds both
  the keyboard programmer and the `BassGrid`, with a KEYS/GRID toggle in the
  pattern row; switching resizes the block's dock window (the block reports the
  height change to the editor). Both edit the same engine data, so it stays in
  sync with the keyboard and the shared GRID overlay. Verified on the real
  Standalone (DPI-aware capture): toggling swaps the programmer and grows the
  block to the semitone grid.
