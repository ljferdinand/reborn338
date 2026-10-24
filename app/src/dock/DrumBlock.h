// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// The drum instrument block (docs/PLAN_GUI.md section 2): voice dials, a pattern
// selector, the always-visible 16-step grid, and a mixer strip, the drum
// counterpart to Bass303Block. Driven by a bare reborn::Engine (+ optional lock).
// Header-only, all-inline.
//
#include "DevicePanel.h"
#include "DrumGrid.h"
#include "MixerStrip.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace reborn::dock {

class DrumBlock : public juce::Component {
public:
    DrumBlock(reborn::Engine& engine, int device /*2=808,3=909*/,
              juce::String title, juce::Colour accent, juce::SpinLock* lock = nullptr)
        : engine_(engine), device_(device), accent_(accent), lock_(lock) {
        auto kind = (device == 2) ? DevicePanel::Kind::Drum808 : DevicePanel::Kind::Drum909;
        dials_ = std::make_unique<DevicePanel>(engine.instrument(device), title, accent, kind,
                                               /*showHeader*/ false);
        grid_  = std::make_unique<DrumGrid>(engine, device, accent, lock);
        mixer_ = std::make_unique<MixerStrip>(engine, device, accent, lock);
        addAndMakeVisible(*dials_);
        addAndMakeVisible(*mixer_);
        addAndMakeVisible(*grid_);
    }

    int preferredHeight(int width) const {
        return dials_->preferredHeight(width) + MixerStrip::kH + kPatH + grid_->preferredHeight() + 8;
    }

    void tick() { grid_->tick(); dials_->refresh(); mixer_->refresh(); }

    void resized() override {
        auto a = getLocalBounds();
        dials_->setBounds(a.removeFromTop(dials_->preferredHeight(getWidth())));
        mixer_->setBounds(a.removeFromTop(MixerStrip::kH));
        patRow_ = a.removeFromTop(kPatH).reduced(6, 2);
        auto sel = patRow_;
        prevR_   = sel.removeFromLeft(26);
        nextR_   = sel.removeFromRight(26);
        patLblR_ = sel;
        grid_->setBounds(a.reduced(2, 0));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(RebornTheme::bg.brighter(0.03f));
        g.fillRoundedRectangle(patRow_.toFloat(), 3.0f);
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
        if (prevR_.contains(e.getPosition())) selectPattern(activePattern() - 1);
        else if (nextR_.contains(e.getPosition())) selectPattern(activePattern() + 1);
    }

private:
    static constexpr int kPatH = 24;

    int activePattern() const { return engine_.activePattern(device_); }

    void selectPattern(int p) {
        p = juce::jlimit(0, 31, p);
        auto go = [&] {
            if (engine_.songMode()) engine_.setSongMode(false);
            engine_.selectDrumPattern(device_ - 2, p);   // deviceIndex 0=808,1=909
        };
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); go(); }
        else go();
        repaint(); grid_->repaint();
    }

    reborn::Engine& engine_;
    int device_ = 2;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    std::unique_ptr<DevicePanel> dials_;
    std::unique_ptr<DrumGrid>    grid_;
    std::unique_ptr<MixerStrip>  mixer_;
    juce::Rectangle<int> patRow_, prevR_, nextR_, patLblR_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumBlock)
};

} // namespace reborn::dock
