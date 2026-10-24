// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Reborn dock harness (docs/PLAN_GUI.md, Stage 1).
//
// Stage-1 spike: prove the "every block is its own frameless top-level window,
// magnetically snapping to its neighbours" model on real Windows (DockManager /
// BlockWindow / DockGeometry). Stage-1 proper: prove the model with REAL content,
// not placeholders. This harness now does both: it drives a bare reborn::Engine
// and hosts REAL DevicePanels (live, param-bound voice and effect controls, the
// same controls the shipped editor's buildPanel produces) as dock blocks. The
// composite blocks whose final layout is dials-plus-grid-plus-mixer (the two drum
// machines) and the non-parameter blocks (transport, master) stay as labelled
// placeholders until their block layout is designed in later stages.
//
// What to try (interactive; drag feel is the whole question):
//   - Drag a block by its title strip: neighbours magnetise flush and aligned.
//   - Drag a block flush with others: the whole group moves as one.
//   - ALT-drag a block: it tears out of its group on its own.
//   - Turn a dial / flip a switch on a real panel: it drives the live engine.
//   - Right-click any strip: Reset layout (dock all) / Save layout / Quit.
//   - Move a block to a second monitor, quit, relaunch: it returns there. Unplug
//     that monitor first and it comes back onto an available screen instead.
//
#include <juce_gui_extra/juce_gui_extra.h>
#include "reborn/engine.h"
#include "../src/RebornLookAndFeel.h"
#include "../src/dock/BlockWindow.h"
#include "../src/dock/DockManager.h"
#include "../src/dock/DevicePanel.h"

using namespace reborn::dock;

// Placeholder for blocks whose composite layout is not built yet (transport,
// master, and the two drum machines' dials-plus-grid).
struct Placeholder : juce::Component {
    juce::String hint;
    juce::Colour accent;
    Placeholder(juce::String h, juce::Colour a) : hint(std::move(h)), accent(a) {}
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff232329));
        g.setColour(accent.withAlpha(0.18f));
        g.fillRect(getLocalBounds().reduced(3));
        g.setColour(juce::Colours::white.withAlpha(0.85f));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawFittedText(hint, getLocalBounds().reduced(9, 7),
                         juce::Justification::topLeft, 4);
        g.setColour(juce::Colours::white.withAlpha(0.28f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(juce::String(getWidth()) + " x " + juce::String(getHeight()),
                   getLocalBounds().reduced(6), juce::Justification::bottomRight);
    }
};

class DockSpikeApp : public juce::JUCEApplication, private juce::Timer {
public:
    const juce::String getApplicationName() override    { return "Reborn Dock Spike"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise(const juce::String&) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&lnf_);
        engine_.prepare(44100.0);

        juce::PropertiesFile::Options o;
        o.applicationName     = "RebornDockSpike";
        o.filenameSuffix      = ".settings";
        o.folderName          = "RebornProject";
        o.osxLibrarySubFolder = "Application Support";
        props_ = std::make_unique<juce::PropertiesFile>(o);

        buildBlocks();
        if (!dock_.loadLayout(*props_)) dock_.resetLayout();
        dock_.showAll();
        startTimerHz(4);   // poll for display-config changes (portable displaysChanged)
    }

    void shutdown() override {
        if (props_) dock_.saveLayout(*props_);
        blocks_.clear();
        contents_.clear();
        props_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void timerCallback() override {
        if (dock_.displayConfigChanged()) dock_.validateAllOntoDisplays();
    }

private:
    static constexpr int kStrip = BlockWindow::kStripH;

    // Add a block wrapping an arbitrary content component of a known content size.
    void addBlock(const juce::String& title, juce::Colour accent,
                  std::unique_ptr<juce::Component> content,
                  int x, int y, int w, int contentH) {
        auto win = std::make_unique<BlockWindow>(title, content.get(), accent);
        win->setIntrinsicContentSize(w, contentH);
        win->onMenu = [this](BlockWindow* b){ showMenu(b); };
        dock_.addBlock(win.get(), Rect{ x, y, w, contentH + kStrip });
        contents_.push_back(std::move(content));
        blocks_.push_back(std::move(win));
    }

    // A real device/effect panel block, sized to the panel's intrinsic height.
    void addPanel(const juce::String& title, juce::Colour accent, reborn::Processor* proc,
                  DevicePanel::Kind kind, int x, int y, int w) {
        auto panel = std::make_unique<DevicePanel>(proc, title, accent, kind);
        const int h = panel->preferredHeight(w);
        addBlock(title, accent, std::move(panel), x, y, w, h);
    }

    void addPlaceholder(const juce::String& title, juce::Colour accent, const juce::String& hint,
                        int x, int y, int w, int contentH) {
        addBlock(title, accent, std::make_unique<Placeholder>(hint, accent), x, y, w, contentH);
    }

    void buildBlocks() {
        auto& e = engine_;
        const juce::Colour steel { 0xff9aa7b4 };
        const int x0 = 200, y0 = 96;
        const int instW = 392, effW = 300;
        const int effX = x0 + instW + 10;

        // Transport spans the top (composite, placeholder for now).
        addPlaceholder("TRANSPORT", juce::Colour(0xffb4b4bc),
                       "shuffle   tempo   sync / MIDI   pat / song   < play >   loop  bar#",
                       x0, y0, instW + 10 + effW, 40);

        // Left column: the instruments. Bass panels are REAL device panels (voice
        // dials); the drum machines stay placeholders until their block is dials +
        // grid + mixer (a later stage).
        int ly = y0 + 40 + kStrip + 6;
        auto stackLeft = [&](const juce::String& t, juce::Colour a, reborn::Processor* p,
                             DevicePanel::Kind k) {
            DevicePanel probe(p, t, a, k);           // measure height at this width
            const int h = probe.preferredHeight(instW);
            addPanel(t, a, p, k, x0, ly, instW);
            ly += h + kStrip + 6;
        };
        stackLeft("BASS 1", RebornTheme::device(0), e.instrument(reborn::kBass1), DevicePanel::Kind::Bass);
        stackLeft("BASS 2", RebornTheme::device(1), e.instrument(reborn::kBass2), DevicePanel::Kind::Bass);
        addPlaceholder("808", RebornTheme::device(2), "dials + 16-step x16 voices + pat + mixer\n(composite block, later stage)",
                       x0, ly, instW, 96); ly += 96 + kStrip + 6;
        addPlaceholder("909", RebornTheme::device(3), "dials + 16-step +soft/flam + pat + mixer\n(composite block, later stage)",
                       x0, ly, instW, 96);

        // Right column: master (placeholder) then the four REAL effect panels.
        int ry = y0 + 40 + kStrip + 6;
        auto stackRight = [&](const juce::String& t, juce::Colour a, reborn::Processor* p,
                              DevicePanel::Kind k) {
            DevicePanel probe(p, t, a, k);
            const int h = probe.preferredHeight(effW);
            addPanel(t, a, p, k, effX, ry, effW);
            ry += h + kStrip + 6;
        };
        addPlaceholder("MASTER", juce::Colour(0xffd0a020), "master vol\ncomp trigger (LED)\n(non-param block, later stage)",
                       effX, ry, effW, 72); ry += 72 + kStrip + 6;
        stackRight("PCF",   steel, e.pcf(),        DevicePanel::Kind::Pcf);
        stackRight("DELAY", steel, e.delay(),      DevicePanel::Kind::Delay);
        stackRight("DIST",  steel, e.distortion(), DevicePanel::Kind::Dist);
        stackRight("COMP",  steel, e.compressor(), DevicePanel::Kind::Comp);
    }

    void showMenu(BlockWindow*) {
        juce::PopupMenu m;
        m.addItem(1, "Reset layout (dock all)");
        m.addItem(2, "Save layout now");
        m.addSeparator();
        m.addItem(3, "Quit");
        m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
            if      (r == 1) dock_.resetLayout();
            else if (r == 2) { if (props_) dock_.saveLayout(*props_); }
            else if (r == 3) quit();
        });
    }

    RebornLookAndFeel                              lnf_;
    reborn::Engine                                 engine_;
    std::unique_ptr<juce::PropertiesFile>          props_;
    std::vector<std::unique_ptr<juce::Component>>  contents_;
    std::vector<std::unique_ptr<BlockWindow>>      blocks_;
    DockManager                                    dock_;
};

START_JUCE_APPLICATION(DockSpikeApp)
