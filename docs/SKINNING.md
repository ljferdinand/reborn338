# Skinning plan: classic mods on modern displays, and a modern look

Answers the standing question: *the original's GUI is very limited on modern
resolutions: can we load classic skins, and what should the modern UI be?*

Grounded in the measured `.rbm` decode ([FORMAT_RBM.md](FORMAT_RBM.md)), not
guesswork.

## The problem, quantified

A classic skin is ~60 **fixed-pixel JPEGs**: device panels at 366 × 115,
buttons at 67 × 25, and film-strips (1674 × 30, 15 × 792, …) whose frames are
indexed by a control's value. The classic window interior is about 640 px wide.

On a 3840 × 2160 display that UI occupies roughly **one sixth of the screen
width** and cannot reflow, because the layout is baked into the bitmaps. So:

- classic skins can be **scaled**, never **reflowed**;
- a resizable, reflowing UI can never be pixel-faithful to a classic skin.

These are two different products, and trying to make one component do both is
what makes skinning engines rot. Hence two backends behind one interface.

## Songs choose the skin

A `.rbs` **names** the mod it was authored against (`GLOB+15`) and never embeds
one (verified across all 40 shipped songs); the relationship is one-way, since
mods embed a demo song but songs only reference a mod. See
[FORMAT_RBS.md](FORMAT_RBS.md).

So skin selection is **song-driven by default**, and the same reference already
drives sample loading (a song written with a mod was composed using that mod's
drums: 32 of the 40 shipped songs). One resolution path serves both:

```
  load song ──▶ song.skinName ──▶ findModByName(modsDir) ──▶ mod path
                                        │
                        ┌───────────────┴───────────────┐
                   drum samples                     ClassicSkin
                   (implemented)                    (phase 4)
```

`findModByName()` matches on the mod's canonical name at `INFO+1003`, **not**
its credits blurb; they disagree for half the retail mods (see
[FORMAT_RBM.md](FORMAT_RBM.md)).

Rules the UI should follow:

- The song's referenced mod is a **default, not a lock**: the user can always
  override the skin and the sample set by hand.
- A referenced mod that is missing must degrade gracefully: load stock samples,
  render the modern skin, and say so rather than failing. The app surfaces this
  as a `needs mod: <name>` note.
- `Standard ReBirth` means the stock look; it resolves to no file by design.

## Architecture

```
                    ┌──────────────────────┐
  control model  ──▶│      Skin (iface)    │◀── engine params (unchanged)
  (already exists)  └──────────┬───────────┘
                       ┌───────┴────────┐
                 ClassicSkin        ModernSkin
             (.rbm bitmaps,      (procedural/vector,
              integer-scaled)     fully resizable)
```

The control model is the part we already have: the editor builds every knob
from the engine's parameter interface, and the pattern grid reads and writes
the pattern model. **Neither backend changes engine or pattern code**: a skin
only decides how a control is drawn and hit-tested.

### `ClassicSkin`: pixel-faithful, integer-scaled

- Loads a user-supplied `.rbm`, decodes `EMBF` JPEGs by resource id.
- Composes the classic layout at its native ~640-px width into an offscreen
  image, then blits it scaled to the window.
- **Integer scale factors only (1×, 2×, 3×, 4×) with nearest-neighbour.**
  Fractional or smoothed scaling turns crisp 1998 pixel art into mush; integer
  nearest-neighbour keeps every pixel exact and is what pixel-art upscaling
  wants. Pick the largest integer factor that fits the window, letterbox the
  remainder.
- Optional "sharp-bilinear" (nearest to the next integer up, then bilinear to
  fit) as a preference for people who prefer a filled window to exact pixels.
- Film-strips: frame count = strip length ÷ frame size, derived per sprite id
  in a small descriptor table (the container does not store frame size).
- Hit-testing happens in **skin space**: transform the mouse point by the
  inverse scale, then use the classic layout's rectangles. One transform, so
  clicks stay exact at any zoom.

### `ModernSkin`: resizable, high-DPI native

- What ships today, promoted to a real backend: procedural drawing via a JUCE
  `LookAndFeel`, laid out with relative bounds so it reflows at any size.
- Resolution-independent: draw with paths and fonts, not bitmaps, so it is
  crisp at any DPI without asset variants.
- Gets the affordances a 1998 fixed layout could not have: the resizable
  pattern editor we just built, scrollable device panels, and room for more
  than 16 steps per row.
- Themeable by a small token set (background, panel, accent, LED on/off/accent,
  text) so alternate modern themes are a data change, not a code change.

## Why not reflow a classic skin?

Two rejected options, recorded so they are not re-proposed:

- **9-slice the classic panels.** Their artwork is not separable into
  stretchable regions (screws, bevels and labels sit mid-panel), so slicing
  produces visibly broken graphics.
- **Auto-vectorise the JPEGs.** Lossy source, dithered gradients, no clean
  edges; the output would be neither faithful nor good.

Integer scaling is the honest answer for classic skins.

## Phasing

1. **`Skin` interface + `ModernSkin`** extracted from the current editor. No
   visible change; purely the seam that makes the rest possible. Low risk.
   **DONE 2026-07-26**: `app/src/Skin.h`, `app/src/ModernSkin.h`. The editor
   owns a `Skin` by pointer (so the choice can become a runtime one) and the
   pattern grid holds one directly, since cell appearance is the only thing it
   asks for. `ModernSkin::drawStepCell` is a verbatim transcription of the old
   grid painter, inset and interpolation weights included, so the step cannot
   move a pixel.

   One design note worth keeping: `drawStepCell` takes the **full** cell rect,
   not an inset one. How much air a cell leaves around itself is a skin
   decision, and a bitmap backend has none to give. The first draft of the
   interface got this wrong; it was written before reading the real call site
   and could not express accent or the past-the-length dim at all.
2. **`.rbm` reader**: container parse, sample extraction, sprite extraction.
   **DONE.** `readModSprites()` returns each non-audio EMBF payload's bytes
   verbatim, for a front-end to decode; the core deliberately gains no image
   dependency. Every chunk is now accounted for, identically across all four
   retail mods:

   | | count |
   |---|---|
   | JPEG skin graphics | 59 |
   | AIFF | 46, of which **45 are drum voices** |
   | embedded `Demo.rbs` | 1 |
   | **total EMBF** | **106** |

   The 46th AIFF is `Startup.aif`, a chime rather than a voice; both readers
   skip it, which is why sprites + voices sums to 105 and not 106. Earlier
   comments said "46 drum voices" and were off by that one.

   Sprite identity is the stored **file name**: EMBF has no numeric resource
   id and no image dimensions, so film-strip frame sizes must come from the
   phase-3 descriptor table, not from the container.
3. **Sprite-id → widget descriptor table**, built by measuring each id against
   a known-good mod. **STARTED 2026-07-26; the decode problem that blocked it is
   resolved (2026-10-06, below).**

   What is established, measured across all four retail mods:

   - The stored file name **is** a numeric resource id (`12500.jpg`,
     `12507.JPG`; case varies between mods). An earlier note in `rbm.h` said
     EMBF had no resource id; that was wrong and is corrected.
   - Each mod carries the **same set of 58 sprite ids**, plus `Demo.rbs` and a
     splash image, making the 60 non-audio payloads. No id is missing from any
     mod, so one descriptor table can serve every mod as the plan assumed.
   - Dimensions are readable from the JPEG `SOF0` marker without decoding
     pixels. From MSM2, ids cluster by size in a way that matches the plan's
     expectations: `12500` / `12520` / `12564` are 366×115 device panels,
     `12559`–`12563` are 67×25 buttons, and the long thin ones are film-strips
     (`12507` 1674×30, `12557` 15×792, `12554` 68×504, `12521`/`12522` 504×13).

   **THE BLOCKER, RESOLVED 2026-10-06: it was the scrambling.** This section
   used to report that the payloads were not standalone JPEGs: valid JFIF, APP,
   DQT, SOF0 and DHT segments, then no SOS, no EOI and unstuffed `FF` bytes.
   Everything it saw was the first 1024 bytes of each payload stored as written
   and the rest scrambled, which ModPacker does to every embedded file
   (docs/FORMAT_RBM.md, "The scrambling, measured"). The headers survived
   because they sit inside the first KB. Once unscrambled, all 236 JPEGs in the
   four retail mods carry their SOS, end with their EOI and decode as ordinary
   images, so `readModSprites` now returns decodable bytes and phase 3 can
   measure pixels. The payloads with more than one `FFD8` are ordinary too:
   every extra marker sits inside an APP13 segment (Photoshop's resource block,
   which carries a thumbnail JPEG), in all 59 of PBE2's and Orbit2's images and
   one of MSM2's, so they say nothing about how film-strips are stored.

4. **`ClassicSkin` renderer**: compose, integer-scale, inverse-transform hit
   testing.
5. **Skin picker** in the UI: Modern (default) / Classic + a `.rbm` chooser,
   plus a scale preference (Auto / 1× / 2× / 3× / Sharp-bilinear).

Steps 1 and 2 are independently valuable and carry no risk to the audio path,
so they are the natural first slice.

## Licensing: important

Mods are **third-party artwork by their original authors**, and the built-in
skin belongs to the original rights holders. This project ships neither.

`ClassicSkin` loads a `.rbm` **the user already owns**, from a path they
choose, exactly like the drum sample-set loader. Nothing is copied into the
repository, and no skin asset is redistributed. See
[PROVENANCE.md](PROVENANCE.md).

The modern skin is our own original artwork and ships with the app.
