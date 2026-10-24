// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The 303 semitone-column grid (docs/PLAN_GUI.md section 4 stretch, and the shared
// GRID overlay): notes as rows (B at top down to C), plus ACCENT / SLIDE / OCTAVE
// rows, across 16 steps. This is the grid form of a 303 pattern, the alternative
// to the block's keyboard programmer; both edit the same engine data so they stay
// in sync. Driven by a bare reborn::Engine (+ optional juce::SpinLock).
// Header-only, all-inline.
//
#include "RebornControls.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace reborn::dock {

class BassGrid : public juce::Component {
public:
    BassGrid(reborn::Engine& engine, int device /*0=bass1,1=bass2*/, juce::Colour accent,
             juce::SpinLock* lock = nullptr)
        : engine_(engine), device_(device), accent_(accent), lock_(lock) {}

    void tick() { repaint(); }
    int  numRows() const { return 12 + 3; }               // notes + ACC/SLD/OCT
    int  preferredHeight() const { return numRows() * kRowH + 6; }

    void paint(juce::Graphics& g) override {
        const auto& p = pattern();
        const int rows = numRows();
        const int len = juce::jlimit(1, 16, p.length);
        const int cur = engine_.isPlaying() ? engine_.currentStep() : -1;

        auto area = getLocalBounds().reduced(3);
        const float gx = (float) area.getX() + (float) kGutter;
        const float gw = (float) area.getRight() - gx;
        const float cw = gw / 16.0f;
        const float rh = (float) area.getHeight() / (float) rows;

        g.setFont(juce::Font(juce::FontOptions(juce::jmin(11.0f, rh - 2.0f), juce::Font::bold)));
        for (int row = 0; row < rows; ++row) {
            const float y = (float) area.getY() + row * rh;
            const bool flag = row >= 12;
            g.setColour(flag ? accent_.brighter(0.1f) : RebornTheme::textDim.brighter(0.2f));
            g.drawText(rowLabel(row),
                       juce::Rectangle<float>((float) area.getX(), y, (float) kGutter - 4, rh),
                       juce::Justification::centredRight, false);
            for (int s = 0; s < 16; ++s) {
                auto cell = juce::Rectangle<float>(gx + s * cw, y, cw, rh).reduced(1.2f);
                const bool past = s >= len;
                g.setColour(RebornTheme::beatGroup(s).withAlpha(past ? 0.05f : 0.11f));
                g.fillRoundedRectangle(cell, 2.0f);
                const BassStep& st = p.steps[(size_t) s];
                bool on = false; juce::String txt;
                if (row < 12)       { on = st.gate && st.note == (11 - row); }
                else if (row == 12) { on = st.accent; }
                else if (row == 13) { on = st.slide; }
                else                { on = st.octave != 0; txt = st.octave > 0 ? "+" : (st.octave < 0 ? "-" : ""); }
                if (on && !past) {
                    g.setColour(accent_.withAlpha(flag ? 0.85f : 0.7f));
                    g.fillRoundedRectangle(cell.reduced(0.5f), 2.0f);
                    if (txt.isNotEmpty()) {
                        g.setColour(juce::Colours::black.withAlpha(0.85f));
                        g.drawText(txt, cell, juce::Justification::centred, false);
                    }
                }
                if (s == cur) {
                    g.setColour(juce::Colours::white.withAlpha(on ? 0.9f : 0.28f));
                    g.drawRoundedRectangle(cell, 2.0f, 1.2f);
                }
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int row = 0, s = 0;
        if (!cellAt(e.position, row, s)) return;
        editCell(row, s, -1);
        paintValue_ = (row < 12) ? (cellOn(row, s) ? 1 : 0) : -1;   // only note rows drag-paint
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (paintValue_ < 0) return;
        int row = 0, s = 0;
        if (!cellAt(e.position, row, s) || row >= 12) return;
        if (cellOn(row, s) == (paintValue_ != 0)) return;
        editCell(row, s, paintValue_);
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { paintValue_ = -1; }

private:
    static constexpr int kRowH = 17, kGutter = 74;
    static constexpr const char* kNote[12] =
        { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    reborn::BassPattern& pattern() {
        auto& s = engine_.song();
        const int p = engine_.activePattern(device_ == 0 ? reborn::kBass1 : reborn::kBass2);
        return device_ == 0 ? s.bass1.at((size_t) p) : s.bass2.at((size_t) p);
    }
    juce::String rowLabel(int row) const {
        if (row < 12) return kNote[11 - row];
        return row == 12 ? "ACCENT" : (row == 13 ? "SLIDE" : "OCTAVE");
    }
    bool cellOn(int row, int s) {
        const BassStep& st = pattern().steps[(size_t) s];
        if (row < 12) return st.gate && st.note == (11 - row);
        if (row == 12) return st.accent;
        if (row == 13) return st.slide;
        return st.octave != 0;
    }
    bool cellAt(juce::Point<float> pos, int& row, int& s) const {
        auto area = getLocalBounds().reduced(3);
        const int rows = numRows();
        if (area.getHeight() <= 0) return false;
        const float gx = (float) area.getX() + (float) kGutter;
        const float gw = (float) area.getRight() - gx;
        if (gw <= 0) return false;
        const float cw = gw / 16.0f, rh = (float) area.getHeight() / (float) rows;
        if (pos.x < gx || pos.y < (float) area.getY()) return false;
        s   = (int) ((pos.x - gx) / cw);
        row = (int) ((pos.y - (float) area.getY()) / rh);
        return s >= 0 && s < 16 && row >= 0 && row < rows;
    }
    void editCell(int row, int s, int forceValue) {
        auto apply = [&] {
            BassStep& st = pattern().steps[(size_t) s];
            if (row < 12) {
                const int note = 11 - row;
                const bool want = (forceValue >= 0) ? (forceValue != 0) : !(st.gate && st.note == note);
                if (want) { st.gate = true; st.note = note; } else st.gate = false;
            } else if (row == 12) {
                st.accent = !st.accent;
            } else if (row == 13) {
                st.slide = !st.slide;
            } else {
                st.octave = (st.octave >= 1) ? -1 : st.octave + 1;
                st.octBoth = false;           // the file's 0x0C no longer applies
            }
        };
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); apply(); }
        else apply();
    }

    reborn::Engine& engine_;
    int device_ = 0;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    int paintValue_ = -1;
};

} // namespace reborn::dock
