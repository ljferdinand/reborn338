// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// BlockWindow: one dockable block as its own frameless top-level window (see
// docs/PLAN_GUI.md section 5). A thin grab strip at the top carries the title and
// the drag gesture; the rest is the block's content component. The window knows
// nothing about snapping or grouping; it just reports drag gestures in screen
// coordinates to callbacks the DockManager wires. Header-only and all-inline so
// it can be included by both the dock spike and the editor without an ODR clash.
//
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace reborn::dock {

class BlockWindow : public juce::TopLevelWindow {
public:
    static constexpr int kStripH = 22;

    BlockWindow(const juce::String& title, juce::Component* content, juce::Colour accent)
        : juce::TopLevelWindow(title, /*addToDesktop*/ true),
          title_(title), content_(content), accent_(accent) {
        setUsingNativeTitleBar(false);
        setDropShadowEnabled(true);
        setWantsKeyboardFocus(true);   // so a global key (G for the grid) works here
        if (content_) addAndMakeVisible(content_);
        strip_.owner = this;
        addAndMakeVisible(strip_);
    }

    // A global key routed to this block when it (and no focus-grabbing child) is
    // the active window. Used for the G grid toggle from any block.
    std::function<bool(const juce::KeyPress&)> onKey;
    bool keyPressed(const juce::KeyPress& k) override { return onKey ? onKey(k) : false; }

    // Set the content's intrinsic size; the window is that plus the grab strip.
    void setIntrinsicContentSize(int w, int h) {
        contentW_ = w; contentH_ = h;
        setSize(w, h + kStripH);
    }

    // Wired by the DockManager. `soloDrag` (Alt held at grab) tears this one
    // block out of its group instead of moving the whole group. When onDrag is
    // not wired (a standalone block not managed by a DockManager, e.g. the GRID
    // overlay), the strip falls back to moving this window directly.
    std::function<void(BlockWindow*, bool /*soloDrag*/)>   onDragStart;
    std::function<void(BlockWindow*, juce::Point<int>)>    onDrag;      // screen delta
    std::function<void(BlockWindow*)>                      onDragEnd;
    std::function<void(BlockWindow*)>                      onMenu;      // right-click strip
    std::function<void()>                                  onActivated; // window became active

    void activeWindowStatusChanged() override {
        if (isActiveWindow() && onActivated) onActivated();
    }

    const juce::String& blockTitle() const { return title_; }

    void resized() override {
        auto r = getLocalBounds();
        strip_.setBounds(r.removeFromTop(kStripH));
        if (content_) content_->setBounds(r);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1a1a1e));
        g.setColour(juce::Colour(0xff000000));
        g.drawRect(getLocalBounds(), 1);
    }

private:
    // The draggable title strip. Reports screen-space gestures to the window's
    // callbacks; a right-click opens the dock/undock/reset menu.
    struct Strip : juce::Component {
        BlockWindow* owner = nullptr;
        juce::Point<int> dragOrigin;
        juce::Rectangle<int> startWin;   // for the standalone self-move fallback

        void paint(juce::Graphics& g) override {
            g.setColour(owner->accent_.withAlpha(0.85f));
            g.fillRect(getLocalBounds());
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.fillRect(getLocalBounds().removeFromBottom(1));
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText(owner->title_, getLocalBounds().reduced(8, 0),
                       juce::Justification::centredLeft, true);
            // Grab dots on the right, so the strip reads as a handle.
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            int rx = getWidth() - 14;
            for (int i = 0; i < 3; ++i) g.fillRect(rx, 7 + i * 4, 8, 2);
        }
        void mouseDown(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) { if (owner->onMenu) owner->onMenu(owner); return; }
            dragOrigin = e.getScreenPosition();
            startWin = owner->getScreenBounds();
            owner->toFront(true);
            if (owner->onDragStart) owner->onDragStart(owner, e.mods.isAltDown());
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (e.mods.isPopupMenu()) return;
            const auto delta = e.getScreenPosition() - dragOrigin;
            if (owner->onDrag) owner->onDrag(owner, delta);        // DockManager path
            else owner->setBounds(startWin.translated(delta.x, delta.y));  // self-move
        }
        void mouseUp(const juce::MouseEvent&) override {
            if (owner->onDragEnd) owner->onDragEnd(owner);
        }
    };

    friend struct Strip;
    juce::String  title_;
    juce::Component* content_ = nullptr;   // not owned
    juce::Colour  accent_;
    int contentW_ = 200, contentH_ = 120;
    Strip strip_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockWindow)
};

} // namespace reborn::dock
