// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

//
// The skin seam.
//
// Two backends will live behind this, and they are genuinely different
// products rather than two themes (see docs/SKINNING.md for the measurements
// behind that claim):
//
//   ModernSkin   procedural drawing, reflows and rescales freely, resolution
//                independent. What ships today.
//   ClassicSkin  a user's own `.rbm` bitmaps, composed at the classic ~640 px
//                width and blitted at INTEGER scale only. Pixel-faithful and
//                therefore fixed-layout: the artwork bakes the layout in, so it
//                can be scaled but never reflowed.
//
// The interface is deliberately narrow. A skin decides how a control is DRAWN
// and, for the bitmap backend, how a click maps back into skin space. It never
// touches the engine, the pattern model, or which controls exist; the editor
// still builds every knob from the engine's parameter interface. That boundary
// is what keeps a second backend from turning into a second application.
//
// Why an interface at all when only one backend exists yet: the classic backend
// cannot be bolted on later without one. Its coordinate space, its scaling rule
// and its hit-testing are all incompatible with drawing straight into component
// bounds, which is what the editor did before this.
//

namespace reborn_ui {

// What a control needs to say about itself for any backend to draw it. This is
// the whole vocabulary: normalised value, whether it is engaged, and which
// device it belongs to (for hue). Anything more specific belongs in the editor.
struct ControlState {
    float value = 0.0f;        // 0..1
    bool  on = false;          // toggles/steps: engaged
    bool  accent = false;      // step is accented
    bool  dimmed = false;      // beyond the pattern's length: shown but inactive
    bool  highlight = false;   // transient emphasis (playhead, hover)
    int   device = 0;          // 0/1 = 303s, 2 = 808, 3 = 909, -1 = none
    int   index = 0;           // step number, voice number, ...
};

// Where a skin wants things placed. The modern backend computes these from the
// available bounds; the classic backend reads them from its descriptor table,
// which is why layout has to be ASKED FOR rather than assumed by the editor.
struct LayoutHints {
    int knobSize      = 46;
    int rowHeight     = 22;
    int stepWidth     = 26;
    int panelPadding  = 8;
    int titleHeight   = 18;
    // A fixed-layout skin cannot honour arbitrary window sizes. When this is
    // false the editor must letterbox rather than stretch, and must not offer
    // continuous resizing.
    bool reflows      = true;
    // Native design size, meaningful only when reflows == false.
    juce::Point<int> nativeSize { 0, 0 };
};

class Skin {
public:
    virtual ~Skin() = default;

    virtual juce::String name() const = 0;

    // JUCE hook for the stock widgets the editor still uses (sliders, buttons).
    // A bitmap backend returns its own LookAndFeel that blits film-strips.
    virtual juce::LookAndFeel& lookAndFeel() = 0;

    virtual LayoutHints layout() const = 0;

    // --- colours ----------------------------------------------------------
    // Named rather than exposed as a palette so a bitmap backend can answer
    // with colours sampled from its own artwork.
    enum class Ink { Background, Panel, PanelHi, PanelLo, CellOff, Text, TextDim };
    virtual juce::Colour ink(Ink) const = 0;
    virtual juce::Colour deviceHue(int device) const = 0;
    virtual juce::Colour beatGroup(int step) const = 0;

    // --- drawing ----------------------------------------------------------
    virtual void drawPanel(juce::Graphics&, juce::Rectangle<int>, juce::Colour accent) = 0;
    virtual void drawTitle(juce::Graphics&, juce::Rectangle<int>,
                           const juce::String& text, juce::Colour accent) = 0;
    // One pattern-grid cell. The hottest drawing path in the editor, and the
    // one place the two backends differ most: procedural rounded rects versus a
    // blitted sprite frame.
    //
    // Takes the FULL cell rect, not an inset one: how much air a cell leaves
    // around itself is a skin decision, and a bitmap backend has none to give.
    virtual void drawStepCell(juce::Graphics&, juce::Rectangle<float> fullCell,
                              const ControlState&) = 0;

    // --- coordinates ------------------------------------------------------
    // Identity for a reflowing skin. A bitmap skin returns the transform that
    // maps its native space onto the window, so the editor can invert it for
    // hit-testing instead of every component doing its own arithmetic.
    virtual juce::AffineTransform contentTransform(juce::Rectangle<int> windowBounds) const {
        juce::ignoreUnused(windowBounds);
        return {};
    }
};

} // namespace reborn_ui
