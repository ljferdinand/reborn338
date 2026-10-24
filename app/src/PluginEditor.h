// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "RebornLookAndFeel.h"
#include "Skin.h"
#include "ModernSkin.h"
#include "dock/DockManager.h"    // BlockWindow + magnetic docking (PLAN_GUI Stage 1)
#include "dock/DevicePanel.h"    // dockable device/effect panel
#include "dock/Bass303Block.h"   // 303 reference block: dials + keyboard programmer
#include "dock/DrumBlock.h"      // drum block: dials + step grid + mixer
#include "dock/GridOverlay.h"    // shared step grid, switchable between engines

class PluginWindow;   // hosted-plugin editor window (PluginWindow.h), fork P5

// Performance + editing UI:
//   header  : play/stop, song/pattern mode, tempo, shuffle, song + sample loaders
//   channels: per-channel volume knob + Mute/Solo (bass1, bass2, 808, 909)
//   editor  : device tabs, pattern select, length, and an EDITABLE step grid
//             (drum voices, or 303 semitones + accent/slide/octave rows)
//   panels  : every device/effect's parameters as labelled knobs (generic,
//             built from the engine's parameter interface). A bitmap skin
//             layer (the classic "Mods" concept) can later replace the look
//             without changing this control model.
class RebornAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer {
public:
    explicit RebornAudioProcessorEditor(RebornAudioProcessor&);
    ~RebornAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void buildPanel(reborn::Processor* p, const juce::String& title, juce::Colour accent,
                    bool isDrum = false);
    // Tear down and rebuild every instrument/effect knob panel from the engine's
    // current voices. Called once at construction and again after a 303 voice
    // swap, since the incoming voice may expose a different parameter set.
    void rebuildPanels();
    // Pop the per-slot 303 voice-engine picker for a bass channel (0 or 1) and
    // apply the choice (Engine::setInstrument via the processor, then rebuild).
    void chooseBassEngine(int channelId);
    // Fork P5: host a third-party plugin in a bass slot. chooseHostedPlugin pops
    // the plugin sub-picker (scan / load file / known list); loadHostedPlugin
    // instantiates and installs a chosen description; pickPluginFile adds one by
    // path; openHostedEditor shows the hosted plugin's own window.
    void chooseHostedPlugin(int channelId);
    void loadHostedPlugin(int channelId, const juce::PluginDescription& desc);
    void pickPluginFile(int channelId);
    void openHostedEditor(int channelId);
    // Refresh the two bass strips' engine buttons to show the current voice.
    void updateEngineButtons();
    void refreshKnobs();
    void setEditDevice(int device);
    void selectPattern(int pattern);
    void syncPatternUi();
    void togglePlay();
    // Arrangement editing: repaint the chip strip from the song, append a step
    // capturing the live pattern selection, delete the selected step, and set
    // the selected step's repeat count.
    void refreshArrangement();
    void captureArrangementStep();
    void deleteArrangementStep();
    void setSelectedRepeats(int r);

    // ---- standalone docking (PLAN_GUI Stage 1) -----------------------------
    // In the STANDALONE app the device/effect panels can pop out into floating,
    // magnetically-snapping top-level windows, the instrument taking shape.
    // The plugin editor stays a single embedded window (a DAW gives us one), so
    // all of this is gated on wrapperType == Standalone and off by default.
    void toggleDock();
    void enterDockedMode();
    void exitDockedMode();
    void buildDeviceBlocks();      // (re)create the floating device/effect panels
    void refreshDeviceBlocks();    // re-read params into the docked panels
    void toggleGrid();             // open/close the shared step-grid overlay
    void focusInstrument(int idx); // an instrument block was activated: point the grid at it
    void layoutArrangement(juce::Rectangle<int> arr);   // the song chip strip + edit controls
    // Show/hide the classic single-window widgets (mixer strips, edit bar, grid)
    // that move into the floating blocks in docked mode.
    void setClassicComponentsVisible(bool visible);

    struct Knob {
        // Rotary for continuous controls; Stepper (a slider that reads out an
        // integer, e.g. delay Steps 1..16, PCF Pattern 0..54); Toggle (a
        // two-state button, e.g. PCF Mode BP/LP, delay Straight/Triplet) --
        // matching ReBirth's own control types instead of a bare 0..1 knob.
        enum Kind { Rotary, Stepper, Toggle };
        Kind kind = Rotary;
        std::unique_ptr<juce::Slider>     slider;   // Rotary + Stepper
        std::unique_ptr<juce::TextButton> toggle;   // Toggle
        std::unique_ptr<juce::Label>      label;
        reborn::Processor* proc = nullptr;
        int   paramIndex = 0;
        float scale = 1.0f;                 // Stepper: param = sliderValue / scale
        juce::String offText, onText;       // Toggle: label for param<0.5 / >=0.5
    };
    // Knobs inside a panel are clustered by FUNCTION, the way the hardware
    // groups them: a 303's filter section reads as one unit, a drum machine's
    // channel levels sit apart from its per-voice character controls.
    struct KnobGroup {
        juce::String name;
        int firstKnob = 0, count = 0;
        juce::Rectangle<int> labelBounds;
    };
    struct Panel { juce::String title; int firstKnob = 0; int count = 0;
                   juce::Colour accent = RebornTheme::device(0);
                   juce::Rectangle<int> bounds;
                   std::vector<KnobGroup> groups; };

    // ---- editable step grid -------------------------------------------------
    // Rows are drum voices, or (for a 303) 12 semitones plus ACC / SLD / OCT.
    // Left-click toggles a cell, dragging paints, shift- or right-click sets
    // accent. Steps past the pattern length are dimmed.
    struct PatternGrid : juce::Component {
        RebornAudioProcessor* proc = nullptr;
        // The grid needs the skin, not the editor: cell appearance is the only
        // thing it asks for, so a pointer to the backend keeps the coupling to
        // exactly that.
        reborn_ui::Skin* skin = nullptr;
        int  device = 0;            // 0 = 303 #1, 1 = 303 #2, 2 = 808, 3 = 909
        int  paintValue = -1;       // drag-paint state: -1 none, 0 clear, 1 set

        // What a click writes into a cell. The 909's soft (quieter) and flam
        // (grace-note) cell values are what the shipped songs lean on, and the
        // grid could not enter them before: alt-click sets soft, cmd/ctrl sets
        // flam, on the 909 only. Shift or right-click is accent, as before.
        enum CellMode { CellToggle = 0, CellAccent, CellSoft, CellFlam };

        bool isBass() const { return device < 2; }
        int  numRows() const;
        juce::String rowLabel(int row) const;
        int  patternLength() const;
        int  currentPattern() const;
        bool cellIsOn(int row, int step) const;
        bool cellAt(juce::Point<float> pos, int& row, int& step) const;
        void editCell(int row, int step, int mode, int forceValue);

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
    };

    struct Content : juce::Component {
        RebornAudioProcessorEditor* owner = nullptr;
        void paint(juce::Graphics&) override;
    };

    // ---- arrangement (song) editor -----------------------------------------
    // A horizontal strip of chips, one per arrangement step, each showing the
    // four device pattern numbers (b1/b2/808/909) and the repeat count. A song
    // is built by capturing the live pattern selection into steps and setting
    // their repeats; the currently-playing row is outlined during song playback.
    struct ArrangementBar : juce::Component {
        RebornAudioProcessor* proc = nullptr;
        int selected = -1;
        std::function<void(int)> onSelect;    // a chip was clicked (its index)
        std::function<void()>    onAppend;    // the trailing "+" chip was clicked
        int  chipCount() const;
        int  preferredWidth() const;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
    };

    RebornAudioProcessor& proc_;

    // Header
    juce::TextButton playButton_ { "Play" };
    juce::TextButton loadButton_ { "Load .rbs..." };
    juce::TextButton samplesButton_ { "Samples..." };
    juce::TextButton modButton_ { "Mod..." };
    juce::TextButton saveButton_ { "Save..." };
    juce::TextButton modeButton_ { "Pattern" };
    juce::Slider     tempoSlider_, shuffleSlider_;
    juce::Label      titleLabel_, statusLabel_, shuffleLabel_;
    juce::Label      bpmLabel_, bpmUnitLabel_, masterLbl_;
    juce::Slider     masterSlider_;
    juce::Rectangle<int> bpmBox_, transportDiv_, fileDiv_;

    // A post-fader peak meter: fast rise, slow fall, with a held peak cap.
    struct Meter : juce::Component {
        RebornAudioProcessor* proc = nullptr;
        int channel = 0;
        float shown = 0.0f, hold = 0.0f, holdAge = 0.0f;
        void tick();                       // called from the editor's timer
        void paint(juce::Graphics&) override;
    };

    // Channel strip: identity, level, placement, send, and what it's doing.
    struct ChannelStrip {
        std::unique_ptr<juce::Label>      name;
        std::unique_ptr<juce::Slider>     volume, pan, send;
        std::unique_ptr<juce::Label>      volLbl, panLbl, sendLbl;
        std::unique_ptr<juce::TextButton> mute, solo;
        // The rack's three routing switches, as ReBirth's mixer strip has them.
        // DIST is a per-section flag any number of strips may set; PCF and COMP
        // are exclusive across the four strips, and the engine enforces that so
        // a click and a loaded song cannot disagree.
        std::unique_ptr<juce::TextButton> dist, pcf, comp;
        // 303 voice-engine picker, on the two bass strips only (null elsewhere).
        std::unique_ptr<juce::TextButton> engine;
        std::unique_ptr<Meter>            meter;
        juce::Rectangle<int> bounds;
    };
    std::array<ChannelStrip, 4> strips_;
    void refreshRoutingButtons();     // reflect the engine's routing back into the UI
    std::unique_ptr<Meter> masterMeter_;   // master bus (Meter::channel = -1)

    // Pattern-editor controls
    std::array<std::unique_ptr<juce::TextButton>, 4> deviceTabs_;
    juce::TextButton patPrev_ { "<" }, patNext_ { ">" };
    juce::Label      patLabel_, lengthLabel_, hintLabel_;
    juce::Slider     lengthSlider_;
    // The 909's global FLAM amount (grace-note gap). Lives in the engine
    // (Engine::flam909), not the device's parameter interface, so it is not one
    // of the generic panel knobs; it shows in the edit bar only while the 909
    // tab is up.
    juce::Slider     flamSlider_;
    juce::Label      flamLabel_;
    PatternGrid      grid_;

    // Arrangement (song) editor: a scrollable chip strip plus its edit controls.
    juce::Label      songLbl_;
    juce::TextButton arrCapture_ { "Capture" }, arrDelete_ { "Del" }, arrClear_ { "Clear" };
    juce::Label      repeatsLbl_;
    juce::Slider     repeatsSlider_;
    juce::Viewport   arrViewport_;
    ArrangementBar   arrBar_;

    // The skin BACKEND, not a look-and-feel. Everything visual goes through
    // this so a bitmap backend can replace it without touching the editor:
    // see Skin.h. Owned by pointer because the choice becomes a runtime one.
    std::unique_ptr<reborn_ui::Skin> skin_ { std::make_unique<reborn_ui::ModernSkin>() };
    reborn_ui::Skin& skin() { return *skin_; }
    juce::Viewport   viewport_;
    Content          content_;
    std::vector<Knob>  knobs_;
    std::vector<Panel> panels_;
    std::unique_ptr<juce::FileChooser> chooser_;
    // Open editor windows for hosted plugins, one per bass slot (fork P5).
    std::array<std::unique_ptr<PluginWindow>, 2> pluginWindows_;
    // The processor's voiceRevision() as of our last panel rebuild. When the
    // processor swaps a voice we did not initiate (a host state restore), this
    // falls behind and the timer rebuilds so no knob points at a freed voice.
    std::uint32_t shownVoiceRevision_ = 0;

    // ---- standalone docking state ------------------------------------------
    // devicePanels_ (content) is declared BEFORE deviceBlocks_ (the windows) so
    // the windows destruct first and never reference freed content.
    bool standalone_ = false;
    bool docked_ = false;
    bool autoDockChecked_ = false;   // restore the last docked/undocked choice once
    juce::TextButton dockButton_ { "Dock" };
    juce::TextButton gridButton_ { "Grid" };
    int lastInstrument_ = 0;         // engine the shared grid follows (0..3)
    std::unique_ptr<reborn::dock::GridOverlay>  gridOverlay_;
    std::unique_ptr<reborn::dock::BlockWindow>  gridBlock_;
    // devicePanels_ (content) < deviceViewports_ (scroll wrappers that view them) <
    // deviceBlocks_ (the windows): destruction runs in reverse, so windows die
    // first, then viewports, then the panels they referenced.
    std::vector<std::unique_ptr<reborn::dock::DevicePanel>>   devicePanels_;
    std::vector<std::unique_ptr<reborn::dock::Bass303Block>>  bassBlocks_;   // the two 303s
    std::vector<std::unique_ptr<reborn::dock::DrumBlock>>     drumBlocks_;   // 808 + 909
    std::vector<std::unique_ptr<juce::Viewport>>             deviceViewports_;
    std::vector<std::unique_ptr<reborn::dock::BlockWindow>>  deviceBlocks_;
    reborn::dock::DockManager dockMgr_;
    std::unique_ptr<juce::PropertiesFile> dockProps_;
    juce::Rectangle<int> undockedTop_;   // top-level window bounds to restore on undock

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RebornAudioProcessorEditor)
};
