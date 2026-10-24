// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The drum step grid (docs/PLAN_GUI.md section 2): an always-visible 16-step x
// voice-row programmer for the 808 / 909, the drum counterpart to the 303
// keyboard programmer. Click a cell to place a hit; shift or right-click for
// accent; alt for the 909's soft hit and cmd/ctrl for its flam; drag to paint.
// The beat-group tint marks the fours and the playhead follows playback.
//
// Driven by a bare reborn::Engine (+ optional juce::SpinLock), so it edits under
// the audio thread in the app and lock-free in the harness. Header-only.
//
#include "RebornControls.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace reborn::dock {

class DrumGrid : public juce::Component {
public:
    DrumGrid(reborn::Engine& engine, int device /*2=808,3=909*/, juce::Colour accent,
             juce::SpinLock* lock = nullptr)
        : engine_(engine), device_(device), accent_(accent), lock_(lock) {}

    void tick() { repaint(); }

    int numRows() const {
        auto* inst = engine_.instrument(device_);
        return inst ? juce::jmax(1, inst->numVoices()) : 1;
    }
    int preferredHeight() const { return numRows() * kRowH + 6; }

    void paint(juce::Graphics& g) override {
        auto& song = engine_.song();
        const int rows = numRows();
        const int pat = currentPattern();
        const auto& p = (device_ == 2) ? song.drum808.at((size_t) pat) : song.drum909.at((size_t) pat);
        const int len = juce::jlimit(1, 16, p.length);
        const int cur = engine_.isPlaying() ? engine_.currentStep() : -1;
        const bool is909 = (device_ == 3);

        auto area = getLocalBounds().reduced(3);
        const float gx = (float) area.getX() + (float) kGutter;
        const float gw = (float) area.getRight() - gx;
        const float cw = gw / 16.0f;
        const float rh = (float) area.getHeight() / (float) rows;
        auto* inst = engine_.instrument(device_);

        g.setFont(juce::Font(juce::FontOptions(juce::jmin(11.0f, rh - 3.0f))));
        for (int row = 0; row < rows; ++row) {
            const float y = (float) area.getY() + row * rh;
            g.setColour(RebornTheme::textDim);
            g.drawText(inst ? juce::String(inst->voiceName(row)) : juce::String(),
                       juce::Rectangle<float>((float) area.getX(), y, (float) kGutter - 4, rh),
                       juce::Justification::centredRight, false);
            const auto& st = (device_ == 2) ? song.drum808.at((size_t) pat).steps
                                            : song.drum909.at((size_t) pat).steps;
            for (int s = 0; s < 16; ++s) {
                auto cell = juce::Rectangle<float>(gx + s * cw, y, cw, rh).reduced(1.2f);
                const bool past = s >= len;
                g.setColour(RebornTheme::beatGroup(s).withAlpha(past ? 0.05f : 0.11f));
                g.fillRoundedRectangle(cell, 2.0f);
                const auto& step = st[(size_t) s];
                if (step.voiceOn(row) && !past) {
                    const bool acc = step.voiceAccent(row);
                    const bool sft = is909 && step.voiceSoft(row);
                    juce::Colour cc = acc ? accent_ : accent_.withAlpha(sft ? 0.35f : 0.62f);
                    g.setColour(cc);
                    g.fillRoundedRectangle(cell.reduced(0.5f), 2.0f);
                    if (is909 && (sft || step.voiceFlam(row))) {
                        g.setColour(juce::Colours::black.withAlpha(0.8f));
                        g.drawText(step.voiceFlam(row) ? "f" : "s", cell,
                                   juce::Justification::centred, false);
                    }
                }
                if (s == cur) {
                    g.setColour(juce::Colours::white.withAlpha(step.voiceOn(row) ? 0.9f : 0.28f));
                    g.drawRoundedRectangle(cell, 2.0f, 1.2f);
                }
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int row = 0, s = 0;
        if (!cellAt(e.position, row, s)) return;
        int mode = 0;                                  // 0 toggle,1 accent,2 soft,3 flam
        if (e.mods.isRightButtonDown() || e.mods.isShiftDown()) mode = 1;
        else if (e.mods.isAltDown())     mode = 2;
        else if (e.mods.isCommandDown()) mode = 3;
        editCell(row, s, mode, -1);
        paintValue_ = (mode == 0) ? (cellOn(row, s) ? 1 : 0) : -1;
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (paintValue_ < 0) return;
        int row = 0, s = 0;
        if (!cellAt(e.position, row, s)) return;
        if (cellOn(row, s) == (paintValue_ != 0)) return;
        editCell(row, s, 0, paintValue_);
        repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { paintValue_ = -1; }

private:
    static constexpr int kRowH = 15, kGutter = 66;

    int channelId() const { return device_ == 2 ? reborn::kDrum808 : reborn::kDrum909; }
    int currentPattern() const { return engine_.activePattern(channelId()); }

    reborn::DrumPattern& pattern() {
        auto& s = engine_.song();
        const int p = currentPattern();
        return device_ == 2 ? s.drum808.at((size_t) p) : s.drum909.at((size_t) p);
    }
    bool cellOn(int row, int s) {
        return pattern().steps[(size_t) s].voiceOn(row);
    }

    bool cellAt(juce::Point<float> pos, int& row, int& s) const {
        auto area = getLocalBounds().reduced(3);
        const int rows = numRows();
        if (rows <= 0 || area.getHeight() <= 0) return false;
        const float gx = (float) area.getX() + (float) kGutter;
        const float gw = (float) area.getRight() - gx;
        if (gw <= 0) return false;
        const float cw = gw / 16.0f;
        const float rh = (float) area.getHeight() / (float) rows;
        if (pos.x < gx || pos.y < (float) area.getY()) return false;
        s   = (int) ((pos.x - gx) / cw);
        row = (int) ((pos.y - (float) area.getY()) / rh);
        return s >= 0 && s < 16 && row >= 0 && row < rows;
    }

    void editCell(int row, int s, int mode, int forceValue) {
        auto apply = [&] {
            reborn::DrumStep& st = pattern().steps[(size_t) s];
            const bool is909 = (device_ == 3);
            if (mode == 1) {                            // accent
                const bool a = !st.voiceAccent(row);
                st.setAccent(row, a);
                if (a) st.setOn(row, true);
            } else if (mode == 2 && is909) {            // soft
                const bool v = !st.voiceSoft(row);
                st.setSoft(row, v);
                if (v) { st.setOn(row, true); st.setFlam(row, false); }
            } else if (mode == 3 && is909) {            // flam
                const bool v = !st.voiceFlam(row);
                st.setFlam(row, v);
                if (v) { st.setOn(row, true); st.setSoft(row, false); }
            } else {                                    // toggle
                const bool on = (forceValue >= 0) ? (forceValue != 0) : !st.voiceOn(row);
                st.setOn(row, on);
                if (!on) { st.setAccent(row, false); st.setSoft(row, false); st.setFlam(row, false); }
            }
        };
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); apply(); }
        else apply();
    }

    reborn::Engine& engine_;
    int device_ = 2;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    int paintValue_ = -1;
};

} // namespace reborn::dock
