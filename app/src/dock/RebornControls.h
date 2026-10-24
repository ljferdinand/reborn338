// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The Reborn control language (docs/PLAN_GUI.md section 4): the dial, toggle,
// stepper and LED the whole instrument is drawn from. One control model so the
// modern skin and (later) a bitmap mod skin can both drive it.
//
//   - Dial:      a rotary arc labelled with its numeric start and end at the arc
//                ends, tick marks, and an always-on value readout. Vertical drag
//                to change; double-click resets to the default; Shift = fine.
//   - Toggle:    a switch with an inset LED that lights in the accent when on,
//                with two-state text (BP/LP, Straight/Triplet).
//   - LedButton: a compact square button with an LED, for the mixer's routing and
//                mute/solo switches.
//   - Stepper:   a small -/+ numeric field for discrete counts.
//
// Continuous and boolean controls bind to an arbitrary value SOURCE (a getter +
// setter), so they drive both a reborn::Processor parameter and a plain
// MixerChannel field (which is not a parameter). Convenience constructors wrap a
// Processor parameter. Header-only, all-inline (ODR-safe across TUs).
//
#include "../RebornLookAndFeel.h"
#include "reborn/processor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <cmath>

namespace reborn::dock {

// A small round LED: dark when off, lit in `accent` (with a soft glow) when on.
inline void drawLed(juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour accent) {
    g.setColour(juce::Colour(0xff0c0d10));
    g.fillEllipse(r);
    if (on) {
        g.setColour(accent.withAlpha(0.30f));
        g.fillEllipse(r.expanded(2.0f));
        g.setColour(accent);
        g.fillEllipse(r.reduced(1.0f));
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse(r.reduced(r.getWidth() * 0.34f).translated(-r.getWidth() * 0.10f,
                                                                 -r.getHeight() * 0.10f));
    } else {
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawEllipse(r.reduced(0.5f), 1.0f);
    }
}

struct ParamControl : juce::Component {
    virtual void refreshFromParam() = 0;
};

// Format a value: honour a unit, show 0..100 for a normalised 0..1 control,
// otherwise a short decimal.
inline juce::String formatParam(float v, float lo, float hi, const juce::String& unit) {
    if (unit == "__dB") {                       // a linear gain shown in decibels
        if (v <= 1.0e-3f) return "-inf";
        const float db = 20.0f * std::log10(v);
        return juce::String(juce::roundToInt(db)) + " dB";
    }
    if (unit.isNotEmpty()) {
        const int dp = (std::abs(hi - lo) >= 20.0f) ? 0 : 1;
        return juce::String(v, dp) + " " + unit;
    }
    if (lo >= 0.0f && hi <= 1.0f)
        return juce::String(juce::roundToInt(v * 100.0f));
    const int dp = (std::abs(hi - lo) >= 20.0f) ? 0 : 1;
    return juce::String(v, dp);
}

// ---------------------------------------------------------------------- dial
class RebornDial : public ParamControl {
public:
    // Value-bound: drives any float via a getter + setter.
    RebornDial(std::function<float()> get, std::function<void(float)> set,
               float lo, float hi, float def, juce::String unit,
               juce::String label, juce::Colour accent)
        : get_(std::move(get)), set_(std::move(set)), label_(std::move(label)),
          unit_(std::move(unit)), accent_(accent), lo_(lo), hi_(hi), def_(def) {
        value_ = get_();
    }
    // Convenience: a reborn::Processor parameter.
    RebornDial(reborn::Processor* proc, int idx, juce::String label, juce::Colour accent) {
        const auto info = proc->paramInfo(idx);
        get_ = [proc, idx] { return proc->getParam(idx); };
        set_ = [proc, idx](float v) { proc->setParam(idx, v); };
        lo_ = info.minValue; hi_ = info.maxValue; def_ = info.defaultValue;
        unit_ = juce::String(info.unit); label_ = std::move(label); accent_ = accent;
        value_ = get_();
    }

    void refreshFromParam() override {
        const float v = get_();
        if (v != value_) { value_ = v; repaint(); }
    }

    void paint(juce::Graphics& g) override {
        // Compact, arc-free dial: a clock face read from a bold accent hand, bold
        // hash marks (the one at the value lit in accent), and bold endpoint
        // numbers at the arc ends, with a tight value readout and label. No value
        // arc; the hand + marks carry the position.
        auto area = getLocalBounds();
        auto labelR = area.removeFromBottom(11);
        auto valueR = area.removeFromBottom(12);

        const float t = (hi_ > lo_) ? juce::jlimit(0.0f, 1.0f, (value_ - lo_) / (hi_ - lo_)) : 0.0f;
        const float a0 = juce::degreesToRadians(210.0f);
        const float a1 = juce::degreesToRadians(510.0f);
        const float ang = a0 + t * (a1 - a0);

        auto f = area.toFloat();
        const float d = juce::jmin(f.getWidth(), f.getHeight());
        const juce::Point<float> c(f.getCentreX(), f.getY() + d * 0.5f);
        const float r = d * 0.5f;

        // hash marks: bold, bright; the mark nearest the value lights in accent.
        for (int i = 0; i <= 8; ++i) {
            const float ta = a0 + (float) i / 8.0f * (a1 - a0);
            const bool at = std::abs(ta - ang) < (a1 - a0) / 17.0f;
            const float ri = r * 0.78f, ro = r * 1.0f;
            g.setColour(at ? accent_.brighter(0.2f) : RebornTheme::textDim.brighter(0.35f));
            g.drawLine(c.x + ri * std::sin(ta), c.y - ri * std::cos(ta),
                       c.x + ro * std::sin(ta), c.y - ro * std::cos(ta), at ? 2.4f : 1.5f);
        }
        // knob face: small, dark, thin edge.
        const float br = r * 0.66f;
        auto body = juce::Rectangle<float>(br * 2, br * 2).withCentre(c);
        g.setColour(RebornTheme::panelLo);
        g.fillEllipse(body);
        g.setColour(RebornTheme::bg.brighter(0.15f));
        g.drawEllipse(body, 1.0f);
        // hand: bold, accent.
        g.setColour(accent_.brighter(0.15f));
        g.drawLine(c.x + br * 0.12f * std::sin(ang), c.y - br * 0.12f * std::cos(ang),
                   c.x + br * 0.98f * std::sin(ang), c.y - br * 0.98f * std::cos(ang),
                   juce::jmax(2.4f, d * 0.11f));

        // endpoint numbers at the arc ends (lower corners), bold. Kept inset so
        // adjacent dials' endpoints do not run together.
        const int nH = 9;
        const int cornerW = juce::jmax(14, (int) (f.getWidth() * 0.42f));
        g.setColour(RebornTheme::textDim.brighter(0.25f));
        g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
        g.drawText(formatParam(lo_, lo_, hi_, unit_),
                   juce::Rectangle<int>(area.getX() + 1, area.getBottom() - nH, cornerW, nH),
                   juce::Justification::centredLeft, false);
        g.drawText(formatParam(hi_, lo_, hi_, unit_),
                   juce::Rectangle<int>(area.getRight() - cornerW - 1, area.getBottom() - nH, cornerW, nH),
                   juce::Justification::centredRight, false);

        // value readout: bold, bright.
        g.setColour(isMouseButtonDown() ? accent_.brighter(0.2f) : RebornTheme::text);
        g.setFont(juce::Font(juce::FontOptions(11.5f, juce::Font::bold)));
        g.drawText(formatParam(value_, lo_, hi_, unit_), valueR, juce::Justification::centred, false);
        // label: bold.
        g.setColour(RebornTheme::textDim.brighter(0.25f));
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText(label_, labelR, juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragStartVal_ = value_; dragStartY_ = e.position.y; repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        const float span = hi_ - lo_;
        const float px = dragStartY_ - e.position.y;
        const float sens = e.mods.isShiftDown() ? 600.0f : 130.0f;
        setValue(dragStartVal_ + (px / sens) * span);
    }
    void mouseUp(const juce::MouseEvent&) override { repaint(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { setValue(def_); }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        setValue(value_ + w.deltaY * (hi_ - lo_) * 0.05f);
    }

private:
    void setValue(float v) {
        v = juce::jlimit(lo_, hi_, v);
        if (v == value_) return;
        value_ = v; set_(v); repaint();
    }
    std::function<float()>      get_;
    std::function<void(float)>  set_;
    juce::String label_, unit_;
    juce::Colour accent_;
    float lo_ = 0, hi_ = 1, def_ = 0, value_ = 0;
    float dragStartVal_ = 0, dragStartY_ = 0;
};

// -------------------------------------------------------------------- toggle
class RebornToggle : public ParamControl {
public:
    RebornToggle(std::function<bool()> get, std::function<void(bool)> set,
                 juce::String offText, juce::String onText, juce::String label, juce::Colour accent)
        : get_(std::move(get)), set_(std::move(set)), off_(std::move(offText)),
          on_(std::move(onText)), label_(std::move(label)), accent_(accent) {
        state_ = get_();
    }
    RebornToggle(reborn::Processor* proc, int idx, juce::String offText, juce::String onText,
                 juce::String label, juce::Colour accent)
        : off_(std::move(offText)), on_(std::move(onText)), label_(std::move(label)), accent_(accent) {
        get_ = [proc, idx] { return proc->getParam(idx) >= 0.5f; };
        set_ = [proc, idx](bool s) { proc->setParam(idx, s ? 1.0f : 0.0f); };
        state_ = get_();
    }
    void refreshFromParam() override {
        const bool s = get_();
        if (s != state_) { state_ = s; repaint(); }
    }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        auto labelR = area.removeFromBottom(13);
        auto sw = area.reduced(2).toFloat();
        const float corner = juce::jmin(6.0f, sw.getHeight() * 0.30f);
        g.setColour(state_ ? accent_.withAlpha(0.22f) : RebornTheme::panelHi);
        g.fillRoundedRectangle(sw, corner);
        g.setColour((state_ ? accent_ : RebornTheme::bg).withAlpha(0.85f));
        g.drawRoundedRectangle(sw, corner, 1.0f);
        auto led = juce::Rectangle<float>(sw.getHeight() * 0.42f, sw.getHeight() * 0.42f)
                       .withCentre({ sw.getX() + sw.getHeight() * 0.42f, sw.getCentreY() });
        drawLed(g, led, state_, accent_);
        g.setColour(state_ ? RebornTheme::text : RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(state_ ? on_ : off_, sw.withTrimmedLeft(sw.getHeight() * 0.85f).toNearestInt(),
                   juce::Justification::centredLeft, false);
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.drawText(label_, labelR, juce::Justification::centred, false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!getLocalBounds().contains(e.getPosition())) return;
        state_ = !state_; set_(state_); repaint();
    }
private:
    std::function<bool()>      get_;
    std::function<void(bool)>  set_;
    juce::String off_, on_, label_;
    juce::Colour accent_;
    bool state_ = false;
};

// ---------------------------------------------------------------- LED button
// A compact square button with a letter and an LED, for the mixer's routing and
// mute/solo switches. Bound to a boolean source.
class LedButton : public ParamControl {
public:
    LedButton(juce::String text, std::function<bool()> get, std::function<void(bool)> set,
              juce::Colour accent)
        : text_(std::move(text)), get_(std::move(get)), set_(std::move(set)), accent_(accent) {}
    void refreshFromParam() override {
        const bool s = get_();
        if (s != state_) { state_ = s; repaint(); }
    }
    void paint(juce::Graphics& g) override {
        state_ = get_();
        auto f = getLocalBounds().toFloat().reduced(1.0f);
        const float corner = juce::jmin(4.0f, f.getHeight() * 0.28f);
        g.setColour(state_ ? accent_.withAlpha(0.26f) : RebornTheme::panelHi);
        g.fillRoundedRectangle(f, corner);
        g.setColour((state_ ? accent_ : RebornTheme::bg).withAlpha(0.85f));
        g.drawRoundedRectangle(f, corner, 1.0f);
        auto led = juce::Rectangle<float>(4.0f, 4.0f)
                       .withCentre({ f.getRight() - 4.0f, f.getY() + 4.0f });
        drawLed(g, led, state_, accent_);
        g.setColour(state_ ? RebornTheme::text : RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!getLocalBounds().contains(e.getPosition())) return;
        set_(!get_()); repaint();
    }
private:
    juce::String text_;
    std::function<bool()>     get_;
    std::function<void(bool)> set_;
    juce::Colour accent_;
    bool state_ = false;
};

// ------------------------------------------------------------------- stepper
class RebornStepper : public ParamControl {
public:
    RebornStepper(reborn::Processor* proc, int idx, int lo, int scale,
                  juce::String label, juce::Colour accent)
        : proc_(proc), idx_(idx), lo_(lo), scale_(scale), label_(std::move(label)), accent_(accent) {
        val_ = juce::roundToInt(proc->getParam(idx) * (float) scale_);
    }
    void refreshFromParam() override {
        const int v = juce::roundToInt(proc_->getParam(idx_) * (float) scale_);
        if (v != val_) { val_ = v; repaint(); }
    }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        auto labelR = area.removeFromBottom(13);
        auto f = area.reduced(2).toFloat();
        g.setColour(RebornTheme::bg.darker(0.25f));
        g.fillRoundedRectangle(f, 4.0f);
        g.setColour(accent_.withAlpha(0.30f));
        g.drawRoundedRectangle(f, 4.0f, 1.0f);
        const float bw = f.getHeight();
        minus_ = f.removeFromLeft(bw); plus_ = f.removeFromRight(bw);
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("-", minus_.toNearestInt(), juce::Justification::centred, false);
        g.drawText("+", plus_.toNearestInt(), juce::Justification::centred, false);
        g.setColour(RebornTheme::text);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(juce::String(val_), f.toNearestInt(), juce::Justification::centred, false);
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.drawText(label_, labelR, juce::Justification::centred, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto p = e.position;
        if (minus_.contains(p)) step(-1);
        else if (plus_.contains(p)) step(+1);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        step(w.deltaY > 0 ? +1 : -1);
    }
private:
    void step(int d) {
        const int v = juce::jlimit(lo_, scale_, val_ + d);
        if (v == val_) return;
        val_ = v; proc_->setParam(idx_, (float) val_ / (float) scale_); repaint();
    }
    reborn::Processor* proc_;
    int idx_, lo_, scale_, val_;
    juce::String label_;
    juce::Colour accent_;
    juce::Rectangle<float> minus_, plus_;
};

} // namespace reborn::dock
