// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// One instrument's mixer strip (docs/PLAN_GUI.md section 2): level, pan and delay
// send dials plus the DIST / PCF / COMP routing switches and mute / solo, bound
// to the engine's MixerChannel and routing. It folds into each instrument block
// so a block is a self-contained channel, and the classic single mixer row can
// leave the main window. Driven by a bare reborn::Engine with an optional
// juce::SpinLock (the routing/mute edits are held under it in the app).
// Header-only, all-inline.
//
#include "RebornControls.h"
#include "reborn/engine.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace reborn::dock {

class MixerStrip : public juce::Component {
public:
    static constexpr int kH = 66;

    MixerStrip(reborn::Engine& engine, int ch, juce::Colour accent, juce::SpinLock* lock = nullptr)
        : engine_(engine), ch_(ch), accent_(accent), lock_(lock) {
        const juce::Colour util { 0xff9aa7b4 };
        // The channel fader is an exponential gain that runs past unity (a song
        // can boost a section well above 1.0), so the dial covers 0..~3.2 and
        // reads in dB, which is what a mixer level means.
        level_ = std::make_unique<RebornDial>(
            [this]{ return engine_.channel(ch_).volume; },
            [this](float v){ edit([&]{ engine_.channel(ch_).volume = v; }); },
            0.0f, 3.2f, 0.8f, juce::String("__dB"), "LEVEL", accent_);
        pan_ = std::make_unique<RebornDial>(
            [this]{ return engine_.channel(ch_).pan; },
            [this](float v){ edit([&]{ engine_.channel(ch_).pan = v; }); },
            -1.0f, 1.0f, 0.0f, juce::String(), "PAN", util);
        send_ = std::make_unique<RebornDial>(
            [this]{ return engine_.channel(ch_).delaySend; },
            [this](float v){ edit([&]{ engine_.channel(ch_).delaySend = v; }); },
            0.0f, 1.0f, 0.0f, juce::String(), "DELAY", util);
        addAndMakeVisible(*level_); addAndMakeVisible(*pan_); addAndMakeVisible(*send_);

        dist_ = std::make_unique<LedButton>("D",
            [this]{ return engine_.channel(ch_).distEnable; },
            [this](bool on){ edit([&]{ engine_.setDistEnable(ch_, on); if (on) engine_.setDistUnit(true); }); },
            juce::Colour(0xffe08a3a));
        pcf_ = std::make_unique<LedButton>("P",
            [this]{ return engine_.pcfTarget() == ch_; },
            [this](bool on){ edit([&]{ engine_.setPcfTarget(on ? ch_ : -1); engine_.setPcfUnit(on); }); },
            accent_);
        comp_ = std::make_unique<LedButton>("C",
            [this]{ return engine_.compTarget() == ch_; },
            [this](bool on){ edit([&]{ engine_.setCompTarget(on ? ch_ : -1); }); },
            juce::Colour(0xff6aa9d0));
        mute_ = std::make_unique<LedButton>("M",
            [this]{ return engine_.channel(ch_).mute; },
            [this](bool on){ edit([&]{ engine_.channel(ch_).mute = on; }); },
            juce::Colour(0xffd05a4a));
        solo_ = std::make_unique<LedButton>("S",
            [this]{ return (engine_.soloMask & (1 << ch_)) != 0; },
            [this](bool on){ edit([&]{ if (on) engine_.soloMask |= (1 << ch_);
                                       else     engine_.soloMask &= ~(1 << ch_); }); },
            accent_);
        addAndMakeVisible(*dist_); addAndMakeVisible(*pcf_); addAndMakeVisible(*comp_);
        addAndMakeVisible(*mute_); addAndMakeVisible(*solo_);
    }

    void refresh() {
        level_->refreshFromParam(); pan_->refreshFromParam(); send_->refreshFromParam();
        dist_->refreshFromParam(); pcf_->refreshFromParam(); comp_->refreshFromParam();
        mute_->refreshFromParam(); solo_->refreshFromParam();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(accent_.withAlpha(0.10f));
        g.fillRect(getLocalBounds().removeFromTop(1));
    }

    void resized() override {
        auto a = getLocalBounds().reduced(4, 2);
        const int gap = 4;
        const int col1 = 26, col2 = 26;         // D/P/C column, M/S column
        auto btnCol2 = a.removeFromRight(col2); a.removeFromRight(gap);
        auto btnCol1 = a.removeFromRight(col1); a.removeFromRight(gap);
        const int dw = a.getWidth() / 3;
        level_->setBounds(a.removeFromLeft(dw));
        pan_->setBounds(a.removeFromLeft(dw));
        send_->setBounds(a);
        const int h1 = btnCol1.getHeight() / 3;
        dist_->setBounds(btnCol1.removeFromTop(h1).reduced(0, 1));
        pcf_->setBounds(btnCol1.removeFromTop(h1).reduced(0, 1));
        comp_->setBounds(btnCol1.reduced(0, 1));
        const int h2 = btnCol2.getHeight() / 3;
        mute_->setBounds(btnCol2.removeFromTop(h2).reduced(0, 1));
        solo_->setBounds(btnCol2.removeFromTop(h2).reduced(0, 1));
    }

private:
    template <class F> void edit(F&& f) {
        if (lock_) { juce::SpinLock::ScopedLockType l(*lock_); f(); }
        else f();
    }

    reborn::Engine& engine_;
    int ch_ = 0;
    juce::Colour accent_;
    juce::SpinLock* lock_ = nullptr;
    std::unique_ptr<RebornDial> level_, pan_, send_;
    std::unique_ptr<LedButton>  dist_, pcf_, comp_, mute_, solo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerStrip)
};

} // namespace reborn::dock
