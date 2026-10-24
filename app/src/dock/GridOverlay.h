// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The shared GRID overlay (docs/PLAN_GUI.md section 13.4, Luke's idea): one large
// step grid that shows a single engine at a time and flips between the four with
// the arrow keys (or the header arrows). The 303s appear as semitone-column grids
// (BassGrid), the drums as voice-row grids (DrumGrid). It edits the same engine
// pattern data as the per-block programmers, so the two stay in sync for free.
// It also follows the focused instrument: the editor calls setEngine() when an
// instrument block is activated. Header-only, all-inline.
//
#include "BassGrid.h"
#include "DrumGrid.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

namespace reborn::dock {

class GridOverlay : public juce::Component {
public:
    GridOverlay(reborn::Engine& engine, const std::array<juce::Colour, 4>& accents,
                juce::SpinLock* lock = nullptr)
        : engine_(engine), accents_(accents) {
        grids_[0] = std::make_unique<BassGrid>(engine, 0, accents[0], lock);
        grids_[1] = std::make_unique<BassGrid>(engine, 1, accents[1], lock);
        grids_[2] = std::make_unique<DrumGrid>(engine, 2, accents[2], lock);
        grids_[3] = std::make_unique<DrumGrid>(engine, 3, accents[3], lock);
        for (auto& g : grids_) addChildComponent(*g);
        setWantsKeyboardFocus(true);
        setEngine(0);
    }

    static constexpr int kHeadH = 26;
    int preferredHeight() const { return kHeadH + 300; }

    int engineIndex() const { return current_; }

    void setEngine(int e) {
        current_ = juce::jlimit(0, 3, e);
        for (int i = 0; i < 4; ++i) grids_[(size_t) i]->setVisible(i == current_);
        resized();
        repaint();
    }

    void tick() { repaint(); }

    std::function<void()> onClose;   // the G key closes the grid

    void resized() override {
        auto a = getLocalBounds();
        a.removeFromTop(kHeadH);
        for (int i = 0; i < 4; ++i)
            if (i == current_) grids_[(size_t) i]->setBounds(a.reduced(2, 0));
    }

    void paint(juce::Graphics& g) override {
        auto head = getLocalBounds().removeFromTop(kHeadH);
        g.setColour(RebornTheme::panelHi);
        g.fillRect(head);
        g.setColour(accents_[(size_t) current_]);
        g.fillRect(head.removeFromBottom(2));
        auto h = getLocalBounds().removeFromTop(kHeadH);
        prevR_ = h.removeFromLeft(28);
        nextR_ = h.removeFromRight(28);
        g.setColour(RebornTheme::text);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText("<", prevR_, juce::Justification::centred, false);
        g.drawText(">", nextR_, juce::Justification::centred, false);
        g.setColour(accents_[(size_t) current_].brighter(0.1f));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("GRID  .  " + kName[(size_t) current_], h, juce::Justification::centred, false);
    }

    bool keyPressed(const juce::KeyPress& k) override {
        if (k == juce::KeyPress::leftKey)  { setEngine((current_ + 3) % 4); return true; }
        if (k == juce::KeyPress::rightKey) { setEngine((current_ + 1) % 4); return true; }
        if (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G') {
            if (onClose) onClose();
            return true;
        }
        return false;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (prevR_.contains(e.getPosition())) setEngine((current_ + 3) % 4);
        else if (nextR_.contains(e.getPosition())) setEngine((current_ + 1) % 4);
    }

private:
    static const inline juce::String kName[4] { "BASS 1", "BASS 2", "808", "909" };

    reborn::Engine& engine_;
    std::array<juce::Colour, 4> accents_;
    std::array<std::unique_ptr<juce::Component>, 4> grids_;   // BassGrid x2, DrumGrid x2
    int current_ = 0;
    juce::Rectangle<int> prevR_, nextR_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridOverlay)
};

} // namespace reborn::dock
