// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The 303 instrument block (docs/PLAN_GUI.md Stage 2 + section 4 stretch): voice
// dials, a mixer strip, a pattern selector, and a note programmer that toggles
// between the ReBirth-style keyboard and the semitone-column grid (the stretch
// goal some players prefer). Both programmers edit the same engine data, and the
// shared GRID overlay shows the same grid, so everything stays in sync.
//
// Driven by a bare reborn::Engine (+ optional lock). Header-only, all-inline.
//
#include "DevicePanel.h"
#include "KeyboardProgrammer.h"
#include "BassGrid.h"
#include "MixerStrip.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace reborn::dock {

class Bass303Block : public juce::Component {
public:
    Bass303Block(reborn::Engine& engine, int device /*0=bass1,1=bass2*/,
                 juce::String title, juce::Colour accent, juce::SpinLock* lock = nullptr)
        : engine_(engine), device_(device), accent_(accent), lock_(lock) {
        auto* proc = engine.instrument(device == 0 ? reborn::kBass1 : reborn::kBass2);
        dials_ = std::make_unique<DevicePanel>(proc, title, accent, DevicePanel::Kind::Bass,
                                               /*showHeader*/ false);
        prog_  = std::make_unique<KeyboardProgrammer>(engine, device, accent, lock);
        grid_  = std::make_unique<BassGrid>(engine, device, accent, lock);
        mixer_ = std::make_unique<MixerStrip>(engine, channelId(), accent, lock);
        addAndMakeVisible(*dials_);
        addAndMakeVisible(*mixer_);
        addAndMakeVisible(*prog_);
        addChildComponent(*grid_);            // hidden until toggled
    }

    // Called by the editor when the block's own height changes (the KEY/GRID
    // toggle), so it can resize the hosting dock window.
    std::function<void()> onLayoutChanged;

    int programmerHeight() const { return showGrid_ ? grid_->preferredHeight() : kProgH; }
    int preferredHeight(int width) const {
        return dials_->preferredHeight(width) + MixerStrip::kH + kPatH + programmerHeight() + 8;
    }

    void tick() {
        dials_->refresh(); mixer_->refresh();
        if (showGrid_) grid_->tick(); else prog_->tick();
    }

    void resized() override {
        auto a = getLocalBounds();
        dials_->setBounds(a.removeFromTop(dials_->preferredHeight(getWidth())));
        mixer_->setBounds(a.removeFromTop(MixerStrip::kH));
        patRow_  = a.removeFromTop(kPatH).reduced(6, 2);
        auto sel = patRow_;
        toggleR_ = sel.removeFromLeft(52);
        sel.removeFromLeft(4);
        prevR_   = sel.removeFromLeft(24);
        nextR_   = sel.removeFromRight(24);
        patLblR_ = sel;
        auto prog = a.reduced(4, 0);
        prog_->setBounds(prog);
        grid_->setBounds(prog);
    }

    void paint(juce::Graphics& g) override {
        // KEY / GRID toggle
        const bool grid = showGrid_;
        g.setColour(grid ? accent_.withAlpha(0.26f) : RebornTheme::panelHi);
        g.fillRoundedRectangle(toggleR_.toFloat(), 3.0f);
        g.setColour((grid ? accent_ : RebornTheme::bg).withAlpha(0.85f));
        g.drawRoundedRectangle(toggleR_.toFloat(), 3.0f, 1.0f);
        g.setColour(grid ? RebornTheme::text : RebornTheme::textDim.brighter(0.2f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        g.drawText(grid ? "GRID" : "KEYS", toggleR_, juce::Justification::centred, false);

        // Pattern selector: < PATTERN N >
        g.setColour(RebornTheme::bg.brighter(0.03f));
        g.fillRoundedRectangle(juce::Rectangle<int>(prevR_.getX(), patRow_.getY(),
                                                    nextR_.getRight() - prevR_.getX(),
                                                    patRow_.getHeight()).toFloat(), 3.0f);
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("<", prevR_, juce::Justification::centred, false);
        g.drawText(">", nextR_, juce::Justification::centred, false);
        g.setColour(RebornTheme::text);
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText("PATTERN " + juce::String(activePattern() + 1), patLblR_,
                   juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (toggleR_.contains(e.getPosition())) {
            showGrid_ = !showGrid_;
            prog_->setVisible(!showGrid_);
            grid_->setVisible(showGrid_);
            resized();
            repaint();
            if (onLayoutChanged) onLayoutChanged();   // resize the dock window
        } else if (prevR_.contains(e.getPosition())) {
            selectPattern(activePattern() - 1);
        } else if (nextR_.contains(e.getPosition())) {
            selectPattern(activePattern() + 1);
        }
    }

private:
    static constexpr int kPatH = 24, kProgH = 118;

    int channelId() const { return device_ == 0 ? reborn::kBass1 : reborn::kBass2; }
    int activePattern() const { return engine_.activePattern(channelId()); }

    void selectPattern(int p) {
        p = juce::jlimit(0, 31, p);
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); doSelect(p); }
        else doSelect(p);
        repaint();
        prog_->repaint();
        grid_->repaint();
    }
    void doSelect(int p) {
        if (engine_.songMode()) engine_.setSongMode(false);
        engine_.selectBassPattern(device_, p);
    }

    reborn::Engine& engine_;
    int device_ = 0;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    bool showGrid_ = false;
    std::unique_ptr<DevicePanel>        dials_;
    std::unique_ptr<KeyboardProgrammer> prog_;
    std::unique_ptr<BassGrid>           grid_;
    std::unique_ptr<MixerStrip>         mixer_;
    juce::Rectangle<int> patRow_, prevR_, nextR_, patLblR_, toggleR_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Bass303Block)
};

} // namespace reborn::dock
