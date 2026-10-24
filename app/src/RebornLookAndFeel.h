// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
// Only gui_basics is needed here (LookAndFeel_V4, Slider, Graphics). The plugin
// editor pulls juce_audio_utils itself; keeping this header light lets the dock
// panels reuse the look without dragging the whole audio stack into a GUI-only
// build (the Stage-1 dock harness).
#include <juce_gui_basics/juce_gui_basics.h>

//
// Reborn's "symbolic skeuomorphic" look.
//
// The aim is software that REMINDS you of the hardware, not a note-perfect
// recreation of it. So:
//   * controls read as physical objects (bevelled knob bodies, value arcs,
//     indicator lines) but are sized for fingers, not for 1983 panel space;
//   * each device carries its own hue, so you know which machine you are
//     looking at without reading a label;
//   * the one literal borrowing is the 16-step BEAT GROUP colouring (the
//     classic red / orange / yellow / cream run of four), because it is both
//     the most recognisable thing about these boxes AND genuinely useful:
//     it shows you where the beat is at a glance.
// Everything else stays software-native: scrolling, resizing, a real editable
// grid, and no fake screws.
//
namespace RebornTheme {

// --- surfaces -------------------------------------------------------------
const juce::Colour bg        { 0xff141619 };   // rack shadow
const juce::Colour panel     { 0xff20242b };   // device face
const juce::Colour panelHi   { 0xff2b3038 };   // top bevel highlight
const juce::Colour panelLo   { 0xff171a1f };   // bottom shade
const juce::Colour cellOff   { 0xff2c313a };
const juce::Colour text      { 0xffd8dce3 };
const juce::Colour textDim   { 0xff7d8794 };

// --- device hues ----------------------------------------------------------
// 303s take the silver box's warm amber; the 808 its orange-red; the 909 a
// steel blue so the two drum machines never read as the same instrument.
inline juce::Colour device(int d) {
    switch (d) {
        case 0:  return juce::Colour(0xffffa23d);   // 303 #1  amber
        case 1:  return juce::Colour(0xffffc95c);   // 303 #2  gold
        case 2:  return juce::Colour(0xffff6b4a);   // 808     orange-red
        default: return juce::Colour(0xff5fb0e0);   // 909     steel blue
    }
}

// The iconic four-group step colouring, used as a *tint* behind the grid so it
// suggests the hardware's button rows without pretending to be them.
inline juce::Colour beatGroup(int step) {
    switch ((step / 4) & 3) {
        case 0:  return juce::Colour(0xffe04a3a);   // red
        case 1:  return juce::Colour(0xffe8863a);   // orange
        case 2:  return juce::Colour(0xffe0a63c);   // amber: a true yellow
                                                    // reads olive on the dark face
        default: return juce::Colour(0xffe8e0d0);   // cream
    }
}

} // namespace RebornTheme

class RebornLookAndFeel : public juce::LookAndFeel_V4 {
public:
    RebornLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, RebornTheme::bg);
        setColour(juce::Label::textColourId,                 RebornTheme::text);
        setColour(juce::TextButton::buttonColourId,          RebornTheme::panelHi);
        setColour(juce::TextButton::textColourOffId,         RebornTheme::text);
        setColour(juce::TextButton::textColourOnId,          juce::Colours::black);
        setColour(juce::Slider::textBoxTextColourId,         RebornTheme::text);
        setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    }

    // A knob that reads as a knob: bevelled body, recessed track, value arc in
    // the control's own colour, and a clear indicator line. No bitmaps.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& s) override {
        auto area = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3.0f);
        const float d = juce::jmin(area.getWidth(), area.getHeight());
        auto box = juce::Rectangle<float>(d, d).withCentre(area.getCentre());
        const auto c = box.getCentre();
        const float r = d * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const juce::Colour accent = s.findColour(juce::Slider::rotarySliderFillColourId);

        // recessed track
        const float trackR = r * 0.90f;
        juce::Path track;
        track.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, startAngle, endAngle, true);
        g.setColour(RebornTheme::bg.brighter(0.06f));
        g.strokePath(track, juce::PathStrokeType(d * 0.10f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        // value arc
        if (pos > 0.001f) {
            juce::Path val;
            val.addCentredArc(c.x, c.y, trackR, trackR, 0.0f, startAngle, angle, true);
            g.setColour(accent);
            g.strokePath(val, juce::PathStrokeType(d * 0.10f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
        // body with a soft top-lit bevel
        const float bodyR = r * 0.70f;
        auto body = juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(c);
        g.setGradientFill(juce::ColourGradient(RebornTheme::panelHi.brighter(0.10f),
                                               body.getCentreX(), body.getY(),
                                               RebornTheme::panelLo, body.getCentreX(),
                                               body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(RebornTheme::bg.withAlpha(0.75f));
        g.drawEllipse(body, 1.0f);

        // indicator
        juce::Path ind;
        const float inner = bodyR * 0.30f, outer = bodyR * 0.92f;
        ind.startNewSubPath(c.x + inner * std::sin(angle), c.y - inner * std::cos(angle));
        ind.lineTo(c.x + outer * std::sin(angle), c.y - outer * std::cos(angle));
        g.setColour(s.isEnabled() ? RebornTheme::text : RebornTheme::textDim);
        g.strokePath(ind, juce::PathStrokeType(juce::jmax(1.6f, d * 0.055f),
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // Buttons: soft panel caps that light up in their own colour when engaged.
    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour& /*backgroundColour*/,
                              bool highlighted, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        const float corner = juce::jmin(6.0f, r.getHeight() * 0.30f);
        const bool on = b.getToggleState();
        juce::Colour base = on ? b.findColour(juce::TextButton::buttonOnColourId)
                               : b.findColour(juce::TextButton::buttonColourId);
        if (down) base = base.darker(0.18f);
        else if (highlighted) base = base.brighter(0.13f);

        g.setGradientFill(juce::ColourGradient(base.brighter(0.10f), r.getCentreX(), r.getY(),
                                               base.darker(0.16f), r.getCentreX(), r.getBottom(),
                                               false));
        g.fillRoundedRectangle(r, corner);
        g.setColour(on ? base.brighter(0.35f) : RebornTheme::bg.withAlpha(0.85f));
        g.drawRoundedRectangle(r, corner, 1.0f);
        if (!b.isEnabled()) {
            g.setColour(RebornTheme::panel.withAlpha(0.55f));
            g.fillRoundedRectangle(r, corner);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override {
        return juce::Font(juce::FontOptions(juce::jlimit(11.0f, 14.0f, h * 0.44f),
                                            juce::Font::bold));
    }

    // Horizontal sliders (tempo, shuffle) as slim recessed rails.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float /*min*/, float /*max*/,
                          juce::Slider::SliderStyle style, juce::Slider& s) override {
        if (style != juce::Slider::LinearHorizontal) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, 0, 0, style, s);
            return;
        }
        const float cy = (float) y + h * 0.5f;
        auto rail = juce::Rectangle<float>((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour(RebornTheme::bg.brighter(0.05f));
        g.fillRoundedRectangle(rail, 3.0f);
        const juce::Colour accent = s.findColour(juce::Slider::trackColourId);
        auto filled = rail.withRight(juce::jlimit(rail.getX(), rail.getRight(), sliderPos));
        g.setColour(accent);
        g.fillRoundedRectangle(filled, 3.0f);
        // cap
        auto cap = juce::Rectangle<float>(9.0f, (float) h * 0.62f)
                       .withCentre({ juce::jlimit(rail.getX(), rail.getRight(), sliderPos), cy });
        g.setGradientFill(juce::ColourGradient(RebornTheme::panelHi.brighter(0.25f),
                                               cap.getCentreX(), cap.getY(),
                                               RebornTheme::panelLo, cap.getCentreX(),
                                               cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 2.5f);
        g.setColour(RebornTheme::bg);
        g.drawRoundedRectangle(cap, 2.5f, 1.0f);
    }
};

// Shared panel chrome: a device face with a top bevel and a hardware-style
// letterspaced title over an accent rule.
namespace RebornTheme {

inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour accent) {
    auto f = r.toFloat();
    g.setGradientFill(juce::ColourGradient(panelHi, f.getCentreX(), f.getY(),
                                           panel, f.getCentreX(), f.getBottom(), false));
    g.fillRoundedRectangle(f, 5.0f);
    g.setColour(bg.withAlpha(0.8f));
    g.drawRoundedRectangle(f, 5.0f, 1.0f);
    g.setColour(accent.withAlpha(0.55f));            // top edge catch-light
    g.fillRect(f.getX() + 5.0f, f.getY() + 1.0f, f.getWidth() - 10.0f, 1.4f);
}

inline void drawTitle(juce::Graphics& g, juce::Rectangle<int> r,
                      const juce::String& title, juce::Colour accent) {
    g.setColour(accent);
    g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
    juce::String spaced;                              // letterspacing by hand
    for (int i = 0; i < title.length(); ++i) { spaced += title[i]; spaced += " "; }
    g.drawText(spaced.trim(), r, juce::Justification::centredLeft, false);
    const int ruleY = r.getBottom() - 2;
    g.setColour(accent.withAlpha(0.30f));
    g.fillRect(r.getX(), ruleY, r.getWidth(), 1);
}

} // namespace RebornTheme
