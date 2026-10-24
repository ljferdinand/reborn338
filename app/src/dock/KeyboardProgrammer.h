// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The ReBirth-style 303 keyboard programmer (docs/PLAN_GUI.md section 4): a
// 16-step line above a one-octave piano keyboard. Selecting a step and playing a
// key sets that step's note and gate and advances, the hardware's own idiom; the
// per-step accent, slide and octave are toggled from the buttons below. A stretch
// goal in the plan is a switch to the semitone-column grid; this is the primary
// entry.
//
// Driven by a bare reborn::Engine with an optional juce::SpinLock, so it edits
// safely under the audio thread in the plugin/standalone (pass the processor's
// lock) and runs lock-free in the GUI-only harness (pass nullptr). Header-only,
// all-inline.
//
#include "RebornControls.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace reborn::dock {

class KeyboardProgrammer : public juce::Component {
public:
    KeyboardProgrammer(reborn::Engine& engine, int device /*0=bass1,1=bass2*/,
                       juce::Colour accent, juce::SpinLock* lock = nullptr)
        : engine_(engine), device_(device), accent_(accent), lock_(lock) {}

    // Owner calls this from its timer so the playhead follows playback.
    void tick() { repaint(); }

    void resized() override {
        auto a = getLocalBounds().reduced(2);
        btnRow_   = a.removeFromBottom(20);
        a.removeFromBottom(3);
        keysRow_  = a.removeFromBottom(46);
        a.removeFromBottom(3);
        stepsRow_ = a;
    }

    void paint(juce::Graphics& g) override {
        const auto& pat = pattern();
        const int shown = juce::jlimit(1, kSteps, pat.length);
        const int cur = engine_.isPlaying() ? engine_.currentStep() : -1;

        // ---- step line ----
        const float cw = (float) stepsRow_.getWidth() / (float) kSteps;
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        for (int i = 0; i < kSteps; ++i) {
            auto cell = juce::Rectangle<float>(stepsRow_.getX() + i * cw, (float) stepsRow_.getY(),
                                               cw, (float) stepsRow_.getHeight()).reduced(1.0f);
            const bool past = i >= shown;
            const BassStep& s = pat.steps[(size_t) i];
            // beat-group tint behind every fourth
            g.setColour(RebornTheme::beatGroup(i).withAlpha(past ? 0.05f : 0.12f));
            g.fillRoundedRectangle(cell, 2.0f);
            if (s.gate && !past) {
                g.setColour(s.accent ? accent_ : accent_.withAlpha(0.55f));
                g.fillRoundedRectangle(cell.reduced(1.0f), 2.0f);
                g.setColour(juce::Colours::black.withAlpha(0.85f));
                g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
                g.drawText(kNote[(size_t) s.note], cell.withTrimmedBottom(cell.getHeight() * 0.28f),
                           juce::Justification::centred, false);
                if (s.octave != 0) {
                    g.setColour(juce::Colours::black.withAlpha(0.9f));
                    g.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
                    g.drawText(s.octave > 0 ? "+8" : "-8",
                               cell.removeFromBottom(cell.getHeight() * 0.30f),
                               juce::Justification::centred, false);
                }
                if (s.slide) {   // tie mark to the next step
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.fillRect(cell.getRight() - 2.0f, cell.getCentreY() - 1.0f, 4.0f, 2.0f);
                }
            }
            if (i == selected_) {
                g.setColour(accent_.brighter(0.3f));
                g.drawRoundedRectangle(cell, 2.0f, 2.0f);
            }
            if (i == cur) {
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawRoundedRectangle(cell.reduced(0.5f), 2.0f, 1.4f);
            }
        }

        // ---- keyboard ----
        drawKeyboard(g);

        // ---- buttons ----
        const char* labels[5] = { "ACC", "SLD", "OCT-", "OCT+", "REST" };
        const BassStep& sel = pat.steps[(size_t) selected_];
        const bool lit[5] = { sel.accent, sel.slide, sel.octave < 0, sel.octave > 0, !sel.gate };
        const float bw = (float) btnRow_.getWidth() / 5.0f;
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        for (int i = 0; i < 5; ++i) {
            auto b = juce::Rectangle<float>(btnRow_.getX() + i * bw, (float) btnRow_.getY(),
                                            bw, (float) btnRow_.getHeight()).reduced(1.5f, 0.0f);
            g.setColour(lit[i] ? accent_.withAlpha(0.28f) : RebornTheme::panelHi);
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour((lit[i] ? accent_ : RebornTheme::bg).withAlpha(0.85f));
            g.drawRoundedRectangle(b, 3.0f, 1.0f);
            g.setColour(lit[i] ? RebornTheme::text : RebornTheme::textDim);
            g.drawText(labels[i], b.toNearestInt(), juce::Justification::centred, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto p = e.position;
        if (stepsRow_.toFloat().contains(p)) {
            const float cw = (float) stepsRow_.getWidth() / (float) kSteps;
            selected_ = juce::jlimit(0, kSteps - 1, (int) ((p.x - stepsRow_.getX()) / cw));
            repaint();
            return;
        }
        if (keysRow_.toFloat().contains(p)) {
            const int note = keyAt(p);
            if (note >= 0) {
                edit([&]{ auto& s = pattern().steps[(size_t) selected_];
                          s.note = note; s.gate = true; });
                advance();
            }
            return;
        }
        if (btnRow_.toFloat().contains(p)) {
            const float bw = (float) btnRow_.getWidth() / 5.0f;
            const int b = juce::jlimit(0, 4, (int) ((p.x - btnRow_.getX()) / bw));
            edit([&]{
                auto& s = pattern().steps[(size_t) selected_];
                switch (b) {
                    case 0: s.accent = !s.accent; break;
                    case 1: s.slide  = !s.slide;  break;
                    case 2: s.octave = (s.octave == -1) ? 0 : -1; s.octBoth = false; break;
                    case 3: s.octave = (s.octave ==  1) ? 0 :  1; s.octBoth = false; break;
                    case 4: s.gate   = !s.gate;   break;
                }
            });
            if (b == 4) advance();
            repaint();
        }
    }

private:
    static constexpr int kSteps = 16;
    static constexpr const char* kNote[12] =
        { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    // white key note numbers and the black keys that follow each white index
    static constexpr int kWhite[7] = { 0, 2, 4, 5, 7, 9, 11 };

    reborn::BassPattern& pattern() {
        auto& s = engine_.song();
        const int p = engine_.activePattern(device_ == 0 ? reborn::kBass1 : reborn::kBass2);
        return device_ == 0 ? s.bass1.at((size_t) p) : s.bass2.at((size_t) p);
    }

    template <class F> void edit(F&& f) {
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); f(); }
        else f();
    }
    void advance() { selected_ = (selected_ + 1) % juce::jlimit(1, kSteps, pattern().length); repaint(); }

    void drawKeyboard(juce::Graphics& g) {
        auto r = keysRow_.toFloat();
        const float ww = r.getWidth() / 7.0f;
        const float bw = ww * 0.62f, bh = r.getHeight() * 0.62f;
        const int selNote = pattern().steps[(size_t) selected_].note;
        // white keys
        for (int i = 0; i < 7; ++i) {
            auto k = juce::Rectangle<float>(r.getX() + i * ww, r.getY(), ww, r.getHeight()).reduced(1.0f);
            const bool on = (kWhite[i] == selNote);
            g.setColour(on ? accent_ : juce::Colour(0xffe8e8ec));
            g.fillRoundedRectangle(k, 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.drawRoundedRectangle(k, 2.0f, 1.0f);
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.setFont(juce::Font(juce::FontOptions(8.0f)));
            g.drawText(kNote[(size_t) kWhite[i]], k.withTrimmedTop(k.getHeight() - 12.0f),
                       juce::Justification::centred, false);
        }
        // black keys (after white indices 0,1,3,4,5)
        const int blackAfter[5] = { 0, 1, 3, 4, 5 };
        const int blackNote[5]  = { 1, 3, 6, 8, 10 };
        for (int i = 0; i < 5; ++i) {
            const float cx = r.getX() + (blackAfter[i] + 1) * ww;
            auto k = juce::Rectangle<float>(cx - bw * 0.5f, r.getY(), bw, bh);
            const bool on = (blackNote[i] == selNote);
            g.setColour(on ? accent_.darker(0.2f) : juce::Colour(0xff17181c));
            g.fillRoundedRectangle(k, 2.0f);
            g.setColour(juce::Colours::black);
            g.drawRoundedRectangle(k, 2.0f, 1.0f);
        }
    }

    int keyAt(juce::Point<float> p) const {
        auto r = keysRow_.toFloat();
        const float ww = r.getWidth() / 7.0f;
        const float bw = ww * 0.62f, bh = r.getHeight() * 0.62f;
        const int blackAfter[5] = { 0, 1, 3, 4, 5 };
        const int blackNote[5]  = { 1, 3, 6, 8, 10 };
        for (int i = 0; i < 5; ++i) {                 // black keys are on top
            const float cx = r.getX() + (blackAfter[i] + 1) * ww;
            juce::Rectangle<float> k(cx - bw * 0.5f, r.getY(), bw, bh);
            if (k.contains(p)) return blackNote[i];
        }
        const int wi = juce::jlimit(0, 6, (int) ((p.x - r.getX()) / ww));
        return kWhite[wi];
    }

    reborn::Engine& engine_;
    int device_ = 0;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    int selected_ = 0;
    juce::Rectangle<int> stepsRow_, keysRow_, btnRow_;
};

} // namespace reborn::dock
