// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
#include "Skin.h"
#include "RebornLookAndFeel.h"

//
// The procedural backend: today's "symbolic skeuomorphic" look, promoted from
// loose helpers into a real Skin.
//
// Nothing about the appearance changes here. The drawing lives where it always
// did, in RebornLookAndFeel.h / RebornTheme; this class is the adapter that
// makes it one interchangeable implementation among (eventually) two. Keeping
// the drawing in place rather than moving it means this step cannot alter a
// single pixel, which is the point of doing it as its own change.
//
namespace reborn_ui {

class ModernSkin : public Skin {
public:
    juce::String name() const override { return "Modern"; }

    juce::LookAndFeel& lookAndFeel() override { return laf_; }

    LayoutHints layout() const override {
        LayoutHints h;              // the defaults ARE the modern layout
        h.reflows = true;           // resolution independent, so it rescales
        return h;
    }

    juce::Colour ink(Ink i) const override {
        switch (i) {
            case Ink::Background: return RebornTheme::bg;
            case Ink::Panel:      return RebornTheme::panel;
            case Ink::PanelHi:    return RebornTheme::panelHi;
            case Ink::PanelLo:    return RebornTheme::panelLo;
            case Ink::CellOff:    return RebornTheme::cellOff;
            case Ink::Text:       return RebornTheme::text;
            default:              return RebornTheme::textDim;
        }
    }

    juce::Colour deviceHue(int device) const override { return RebornTheme::device(device); }
    juce::Colour beatGroup(int step)   const override { return RebornTheme::beatGroup(step); }

    void drawPanel(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour accent) override {
        RebornTheme::drawPanel(g, r, accent);
    }

    void drawTitle(juce::Graphics& g, juce::Rectangle<int> r,
                   const juce::String& text, juce::Colour accent) override {
        RebornTheme::drawTitle(g, r, text, accent);
    }

    // Transcribed verbatim from the editor's grid painter so this change cannot
    // move a pixel. The 1.5 inset, the interpolation weights and the accent cap
    // geometry are all exactly what shipped.
    void drawStepCell(juce::Graphics& g, juce::Rectangle<float> fullCell,
                      const ControlState& s) override {
        const float rh = fullCell.getHeight(), cw = fullCell.getWidth();
        const auto cell = fullCell.reduced(1.5f);
        const juce::Colour grp = beatGroup(s.index);

        // Empty cell, tinted by its BEAT GROUP: the classic four-colour run of
        // sixteen. Barely-there when off, so it reads as panel engraving rather
        // than decoration, but it puts the beat where your eye can find it.
        g.setColour(ink(Ink::CellOff).interpolatedWith(grp, s.dimmed ? 0.03f : 0.10f)
                                     .withAlpha(s.dimmed ? 0.45f : 1.0f));
        g.fillRoundedRectangle(cell, 2.5f);

        if (s.on) {
            // A lit step glows in its DEVICE colour, leaning toward the
            // beat-group hue so the row still scans as sixteen steps.
            const juce::Colour base = deviceHue(s.device).interpolatedWith(grp, 0.22f);
            const juce::Colour lit  = s.accent ? base.brighter(0.45f) : base;
            g.setColour(s.dimmed ? lit.withAlpha(0.35f) : lit);
            g.fillRoundedRectangle(cell, 2.5f);
            if (s.accent && !s.dimmed) {               // accent: a bright cap
                g.setColour(juce::Colours::white.withAlpha(0.75f));
                g.fillRoundedRectangle(cell.withHeight(juce::jmax(2.0f, rh * 0.16f))
                                           .reduced(cw * 0.18f, 0.0f), 1.0f);
            }
        }
    }

private:
    RebornLookAndFeel laf_;
};

} // namespace reborn_ui
