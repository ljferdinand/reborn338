// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// DevicePanel: one device or effect's parameters as a self-contained, dockable
// panel (docs/PLAN_GUI.md). It builds the Reborn control language (RebornDial /
// RebornToggle / RebornStepper, see RebornControls.h) from a reborn::Processor's
// parameter list, applying the same hidden-param rules and functional grouping
// the classic editor used, and owns its own paint, layout and refresh. It can
// live inside the single-window plugin editor or as the content of a floating
// dock BlockWindow. Driven by a bare reborn::Processor*, so a plain reborn::Engine
// populates it with no JUCE AudioProcessor wrapper. Header-only, all-inline.
//
#include "RebornControls.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace reborn::dock {

class DevicePanel : public juce::Component {
public:
    enum class Kind { Bass, Drum808, Drum909, Pcf, Delay, Dist, Comp, Master, Other };

    // showHeader draws the panel's own letterspaced title bar. Set false when the
    // panel is the content of a dock block, whose grab strip already titles it.
    DevicePanel(reborn::Processor* proc, juce::String title, juce::Colour accent, Kind kind,
                bool showHeader = true)
        : proc_(proc), title_(std::move(title)), accent_(accent), kind_(kind),
          showHeader_(showHeader) {
        build();
    }

    int preferredHeight(int width) const {
        const int perRow = juce::jmax(1, (width - kPad - 16) / kKnobW);
        int y = headH();
        const bool showGroups = groups_.size() > 1;
        for (const auto& grp : groups_) {
            if (showGroups) y += kGroupHead;
            const int rows = (grp.count + perRow - 1) / juce::jmax(1, perRow);
            y += rows * kKnobH;
        }
        return y + 8;
    }

    void refresh() {
        for (auto& c : ctls_) c->refreshFromParam();
    }

    void paint(juce::Graphics& g) override {
        RebornTheme::drawPanel(g, getLocalBounds(), accent_);
        if (showHeader_)
            RebornTheme::drawTitle(g, getLocalBounds().withHeight(kPanelHead).reduced(10, 4),
                                   title_, accent_);
        for (const auto& grp : groups_) {
            if (grp.labelBounds.isEmpty()) continue;
            g.setColour(RebornTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText(grp.name, grp.labelBounds, juce::Justification::centredLeft, false);
            const int tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), grp.name) + 8;
            const int ry = grp.labelBounds.getCentreY();
            g.setColour(accent_.withAlpha(0.18f));
            if (grp.labelBounds.getWidth() > tw)
                g.fillRect(grp.labelBounds.getX() + tw, ry,
                           grp.labelBounds.getWidth() - tw, 1);
        }
    }

    void resized() override {
        const int W = getWidth();
        const int perRow = juce::jmax(1, (W - kPad - 16) / kKnobW);
        int y = headH();
        const bool showGroups = groups_.size() > 1;
        for (auto& grp : groups_) {
            if (showGroups) {
                grp.labelBounds = juce::Rectangle<int>(kPad + 2, y, W - 12, kGroupHead - 2);
                y += kGroupHead;
            } else {
                grp.labelBounds = {};
            }
            for (int j = 0; j < grp.count; ++j) {
                const int col = j % perRow, row = j / perRow;
                const int kx = kPad + col * kKnobW;
                const int ky = y + row * kKnobH;
                ctls_[(size_t) grp.first + (size_t) j]
                    ->setBounds(kx + 1, ky + 1, kKnobW - 2, kKnobH - 3);
            }
            y += ((grp.count + perRow - 1) / juce::jmax(1, perRow)) * kKnobH;
        }
    }

private:
    static constexpr int kKnobW = 60, kKnobH = 64;
    static constexpr int kPanelHead = 22, kGroupHead = 13, kPad = 6;

    int headH() const { return showHeader_ ? kPanelHead : 4; }

    struct Group { juce::String name; int first = 0, count = 0; juce::Rectangle<int> labelBounds; };

    bool isDrum() const { return kind_ == Kind::Drum808 || kind_ == Kind::Drum909; }

    bool hidden(const juce::String& n) const {
        switch (kind_) {
            case Kind::Bass:    return n == "volume" || n == "drive";
            case Kind::Drum808: return n == "tom_tune" || n == "tom_decay"
                                    || n == "cb_tune" || n == "cp_spread" || n == "ch_decay";
            case Kind::Drum909: return n == "cr_decay" || n == "rd_decay"
                                    || n == "hh_tune" || n == "accent_cell";
            case Kind::Pcf:     return n == "mod";
            default:            return false;
        }
    }

    juce::String groupFor(const juce::String& n) const {
        if (n.startsWith("level_")) return "CHANNEL LEVELS";
        if (isDrum()) return n == "accent" ? "ACCENT" : "VOICE CHARACTER";
        if (n == "tuning" || n == "waveform") return "OSCILLATOR";
        if (n == "volume" || n == "drive")    return "OUTPUT";
        return "FILTER";
    }

    void add(std::unique_ptr<ParamControl> c, const juce::String& gname) {
        addAndMakeVisible(*c);
        if (groups_.empty() || groups_.back().name != gname)
            groups_.push_back({ gname, (int) ctls_.size(), 0, {} });
        groups_.back().count++;
        ctls_.push_back(std::move(c));
    }

    void build() {
        for (int i = 0; i < proc_->numParams(); ++i) {
            ParamInfo info = proc_->paramInfo(i);
            const juce::String name(info.name);
            if (hidden(name)) continue;
            const juce::String label(info.label.empty() ? info.name : info.label);
            const juce::String gname = groupFor(name);

            const bool isToggle  = (kind_ == Kind::Pcf   && name == "mode")
                                || (kind_ == Kind::Delay && name == "grid");
            const bool isStepper = (kind_ == Kind::Delay && name == "steps")
                                || (kind_ == Kind::Pcf   && name == "pattern");

            if (isToggle) {
                if (name == "mode") add(std::make_unique<RebornToggle>(proc_, i, "BP", "LP", label, accent_), gname);
                else                add(std::make_unique<RebornToggle>(proc_, i, "Triplet", "Straight", label, accent_), gname);
            } else if (isStepper) {
                const int lo = (name == "steps") ? 1 : 0;
                const int scale = (name == "steps") ? 16 : 54;
                add(std::make_unique<RebornStepper>(proc_, i, lo, scale, label, accent_), gname);
            } else {
                const bool isLevel = name.startsWith("level_");
                const juce::Colour a = isLevel ? accent_.withSaturation(0.35f).withBrightness(0.62f)
                                               : accent_;
                add(std::make_unique<RebornDial>(proc_, i, label, a), gname);
            }
        }
    }

    reborn::Processor* proc_ = nullptr;
    juce::String       title_;
    juce::Colour       accent_;
    Kind               kind_ = Kind::Other;
    bool               showHeader_ = true;
    std::vector<std::unique_ptr<ParamControl>> ctls_;
    std::vector<Group> groups_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DevicePanel)
};

} // namespace reborn::dock
