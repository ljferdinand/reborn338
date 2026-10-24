// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "PluginEditor.h"
#include "HostedInstrument.h"
#include "PluginWindow.h"
#include "reborn/rbs.h"
#include "reborn/rbsx.h"

using namespace reborn;

namespace {
constexpr int kHeaderH   = 62;
constexpr int kStripH    = 104;
constexpr int kEditBarH  = 30;
constexpr int kGridH     = 248;
constexpr int kPanelHead = 26;
constexpr int kKnobW     = 78;
constexpr int kKnobH     = 92;
constexpr int kPad       = 8;
constexpr int kRowLabelW = 78;   // grid row-label gutter
constexpr int kArrangeH  = 58;   // song-arrangement section height
constexpr int kArrChipW  = 78;   // one arrangement-step chip
constexpr int kArrGap    = 4;
const juce::Colour kBg      = RebornTheme::bg;
const juce::Colour kPanelBg = RebornTheme::panel;
const juce::Colour kAccent  { 0xffffa23d };
const juce::Colour kLedOff  = RebornTheme::cellOff;
const juce::Colour kLedPos  { 0xffffffff };
const char* kChanNames[4] = { "303 #1", "303 #2", "808", "909" };
const char* kNoteNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
constexpr int kGroupHead = 15;   // sub-section label strip

// Which functional cluster a parameter belongs to. Mirrors how the hardware
// lays its panels out, and relies on the engines exposing params in panel
// order so a cluster is always a consecutive run.
juce::String groupFor(const juce::String& n, bool isDrum) {
    if (n.startsWith("level_")) return "CHANNEL LEVELS";
    if (isDrum) {
        if (n == "accent") return "ACCENT";
        return "VOICE CHARACTER";
    }
    if (n == "tuning" || n == "waveform")       return "OSCILLATOR";
    if (n == "volume" || n == "drive")          return "OUTPUT";
    return "FILTER";        // cutoff / resonance / envmod / decay / accent
}
}

void RebornAudioProcessorEditor::Content::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    if (!owner) return;
    for (const auto& panel : owner->panels_) {
        if (panel.bounds.isEmpty()) continue;
        RebornTheme::drawPanel(g, panel.bounds, panel.accent);
        RebornTheme::drawTitle(g, panel.bounds.withHeight(kPanelHead).reduced(10, 4),
                               panel.title, panel.accent);
        for (const auto& grp : panel.groups) {
            if (grp.labelBounds.isEmpty()) continue;
            g.setColour(RebornTheme::textDim);
            g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
            g.drawText(grp.name, grp.labelBounds, juce::Justification::centredLeft, false);
            // hairline rule trailing the label, so a cluster reads as a section
            const int tw = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), grp.name) + 8;
            const int ry = grp.labelBounds.getCentreY();
            g.setColour(panel.accent.withAlpha(0.18f));
            if (grp.labelBounds.getWidth() > tw)
                g.fillRect(grp.labelBounds.getX() + tw, ry,
                           grp.labelBounds.getWidth() - tw, 1);
        }
    }
}

// ============================================================== pattern grid
int RebornAudioProcessorEditor::PatternGrid::numRows() const {
    if (isBass()) return 12 + 3;                 // semitones + ACC / SLD / OCT
    if (!proc) return 1;
    auto* inst = proc->engine().instrument(device);
    return inst ? juce::jmax(1, inst->numVoices()) : 1;
}

juce::String RebornAudioProcessorEditor::PatternGrid::rowLabel(int row) const {
    if (isBass()) {
        if (row < 12) return kNoteNames[11 - row];   // B at the top, C at the bottom
        return row == 12 ? "ACCENT" : (row == 13 ? "SLIDE" : "OCTAVE");
    }
    if (!proc) return {};
    auto* inst = proc->engine().instrument(device);
    if (!inst) return {};
    return juce::String(inst->voiceName(row));
}

int RebornAudioProcessorEditor::PatternGrid::currentPattern() const {
    return proc ? proc->engine().activePattern(device) : 0;
}

int RebornAudioProcessorEditor::PatternGrid::patternLength() const {
    if (!proc) return 16;
    auto& s = proc->engine().song();
    const int pat = currentPattern();
    switch (device) {
        case 0: return s.bass1.at(pat).length;
        case 1: return s.bass2.at(pat).length;
        case 2: return s.drum808.at(pat).length;
        default: return s.drum909.at(pat).length;
    }
}

bool RebornAudioProcessorEditor::PatternGrid::cellIsOn(int row, int step) const {
    if (!proc || step < 0 || step >= kMaxSteps) return false;
    auto& s = proc->engine().song();
    const int pat = currentPattern();
    if (isBass()) {
        const BassStep& st = (device == 0 ? s.bass1.at(pat) : s.bass2.at(pat)).steps[step];
        if (row < 12) return st.gate && st.note == (11 - row);
        if (row == 12) return st.accent;
        if (row == 13) return st.slide;
        return st.octave != 0;
    }
    const DrumStep& st = (device == 2 ? s.drum808.at(pat) : s.drum909.at(pat)).steps[step];
    return st.voiceOn(row);
}

bool RebornAudioProcessorEditor::PatternGrid::cellAt(juce::Point<float> pos,
                                                     int& row, int& step) const {
    auto area = getLocalBounds().reduced(4);
    const int rows = numRows();
    if (rows <= 0 || area.getHeight() <= 0) return false;
    const float gx = (float) area.getX() + (float) kRowLabelW;
    const float gw = (float) area.getRight() - gx;
    if (gw <= 0.0f) return false;
    const float cw = gw / 16.0f;
    const float rh = (float) area.getHeight() / (float) rows;
    if (pos.x < gx || pos.y < (float) area.getY()) return false;
    step = (int) ((pos.x - gx) / cw);
    row  = (int) ((pos.y - (float) area.getY()) / rh);
    return step >= 0 && step < 16 && row >= 0 && row < rows;
}

// forceValue: -1 = toggle, 0 = clear, 1 = set. accentMode edits the accent bit.
void RebornAudioProcessorEditor::PatternGrid::editCell(int row, int step,
                                                       int mode, int forceValue) {
    if (!proc || step < 0 || step >= kMaxSteps) return;
    juce::SpinLock::ScopedLockType lock(proc->engineLock());
    auto& s = proc->engine().song();
    const int pat = currentPattern();

    if (isBass()) {
        BassStep& st = (device == 0 ? s.bass1.at(pat) : s.bass2.at(pat)).steps[step];
        if (row < 12) {
            const int note = 11 - row;
            const bool want = (forceValue >= 0) ? (forceValue != 0)
                                                : !(st.gate && st.note == note);
            if (want) { st.gate = true; st.note = note; }
            else        st.gate = false;
        } else if (row == 12) {
            st.accent = (forceValue >= 0) ? (forceValue != 0) : !st.accent;
        } else if (row == 13) {
            st.slide = (forceValue >= 0) ? (forceValue != 0) : !st.slide;
        } else if (forceValue < 0) {          // octave cycles on click only
            st.octave = (st.octave >= 1) ? -1 : st.octave + 1;
            st.octBoth = false;               // the file's 0x0C no longer applies
        }
        return;
    }

    DrumStep& st = (device == 2 ? s.drum808.at(pat) : s.drum909.at(pat)).steps[step];
    const bool is909 = (device == 3);
    // soft/flam are 909-only cell values; on the 808 those modifiers fall back
    // to a plain toggle. A cell is normal, OR soft, OR flam (setting one clears
    // the other), and any of them implies the voice is on.
    if (mode == CellAccent) {
        const bool a = !st.voiceAccent(row);
        st.setAccent(row, a);
        if (a) st.setOn(row, true);           // an accent implies a hit
    } else if (mode == CellSoft && is909) {
        const bool sft = !st.voiceSoft(row);
        st.setSoft(row, sft);
        if (sft) { st.setOn(row, true); st.setFlam(row, false); }
    } else if (mode == CellFlam && is909) {
        const bool fl = !st.voiceFlam(row);
        st.setFlam(row, fl);
        if (fl) { st.setOn(row, true); st.setSoft(row, false); }
    } else {                                   // CellToggle
        const bool on = (forceValue >= 0) ? (forceValue != 0) : !st.voiceOn(row);
        st.setOn(row, on);
        if (!on) { st.setAccent(row, false); st.setSoft(row, false); st.setFlam(row, false); }
    }
}

void RebornAudioProcessorEditor::PatternGrid::paint(juce::Graphics& g) {
    RebornTheme::drawPanel(g, getLocalBounds(), RebornTheme::device(device));
    if (!proc) return;
    auto& e = proc->engine();
    auto& song = e.song();

    const int rows = numRows();
    const int len  = juce::jlimit(1, 16, patternLength());
    const int pat  = currentPattern();
    const int cur  = e.isPlaying() ? e.currentStep() : -1;

    auto area = getLocalBounds().reduced(4);
    if (rows <= 0 || area.getHeight() <= 0) return;
    const float gx = (float) area.getX() + (float) kRowLabelW;
    const float gw = (float) area.getRight() - gx;
    const float cw = gw / 16.0f;
    const float rh = (float) area.getHeight() / (float) rows;

    g.setFont(juce::Font(juce::FontOptions(juce::jmin(11.0f, rh - 3.0f))));

    for (int row = 0; row < rows; ++row) {
        const float y = (float) area.getY() + row * rh;

        // row label
        g.setColour(isBass() && row >= 12 ? RebornTheme::device(device)
                                          : RebornTheme::textDim);
        g.drawText(rowLabel(row),
                   juce::Rectangle<float>((float) area.getX(), y, (float) kRowLabelW - 4, rh),
                   juce::Justification::centredRight, false);

        for (int stp = 0; stp < 16; ++stp) {
            const auto fullCell = juce::Rectangle<float>(gx + stp * cw, y, cw, rh);
            const auto cell = fullCell.reduced(1.5f);
            const bool past = stp >= len;

            bool on = false, acc = false;
            juce::String text;
            if (isBass()) {
                const BassStep& st = (device == 0 ? song.bass1.at(pat)
                                                  : song.bass2.at(pat)).steps[stp];
                if (row < 12)       { on = st.gate && st.note == (11 - row); acc = st.accent && on; }
                else if (row == 12) { on = st.accent; }
                else if (row == 13) { on = st.slide; }
                else                { on = st.octave != 0;
                                      if (st.octave > 0) text = "+"; else if (st.octave < 0) text = "-"; }
            } else {
                const DrumStep& st = (device == 2 ? song.drum808.at(pat)
                                                  : song.drum909.at(pat)).steps[stp];
                on  = st.voiceOn(row);
                acc = st.voiceAccent(row);
                if (st.voiceFlam(row))      text = "f";   // grace-note (909)
                else if (st.voiceSoft(row)) text = "s";   // quieter hit (909)
            }

            // All cell appearance now goes through the skin backend, so a
            // bitmap skin can blit a sprite frame here instead (see Skin.h).
            reborn_ui::ControlState cs;
            cs.on = on; cs.accent = acc; cs.dimmed = past;
            cs.device = device; cs.index = stp;
            if (skin) skin->drawStepCell(g, fullCell, cs);
            if (text.isNotEmpty()) {
                g.setColour(on ? juce::Colours::black : juce::Colours::grey);
                g.drawText(text, cell, juce::Justification::centred, false);
            }
            if (stp == cur) {                       // playhead column
                g.setColour(kLedPos.withAlpha(on ? 0.85f : 0.30f));
                g.drawRoundedRectangle(cell, 2.0f, 1.6f);
            }
        }
    }
}

void RebornAudioProcessorEditor::PatternGrid::mouseDown(const juce::MouseEvent& ev) {
    int row = 0, step = 0;
    if (!cellAt(ev.position, row, step)) return;
    int mode = CellToggle;
    if (ev.mods.isRightButtonDown() || ev.mods.isShiftDown()) mode = CellAccent;
    else if (ev.mods.isAltDown())     mode = CellSoft;   // 909 only (editCell guards)
    else if (ev.mods.isCommandDown()) mode = CellFlam;   // cmd (mac) / ctrl (win), 909 only
    editCell(row, step, mode, -1);
    // Remember the resulting state so a drag paints the same value; only a plain
    // toggle drag-paints, accent/soft/flam are click-only.
    paintValue = (mode == CellToggle) ? (cellIsOn(row, step) ? 1 : 0) : -1;
    repaint();
}

void RebornAudioProcessorEditor::PatternGrid::mouseDrag(const juce::MouseEvent& ev) {
    if (paintValue < 0) return;                    // accent / octave: click only
    int row = 0, step = 0;
    if (!cellAt(ev.position, row, step)) return;
    if (isBass() && row >= 12) return;             // don't drag-paint the flag rows
    if (cellIsOn(row, step) == (paintValue != 0)) return;
    editCell(row, step, CellToggle, paintValue);
    repaint();
}

void RebornAudioProcessorEditor::PatternGrid::mouseUp(const juce::MouseEvent&) {
    paintValue = -1;
}

// ===================================================================== meter
void RebornAudioProcessorEditor::Meter::tick() {
    if (!proc) return;
    const float pk = juce::jlimit(0.0f, 1.0f,
        channel < 0 ? proc->engine().masterPeak()
                    : proc->engine().channelPeak(channel));
    shown = pk > shown ? pk : shown * 0.80f;      // fast rise, eased fall
    holdAge += 0.05f;
    if (shown >= hold || holdAge > 1.2f) { hold = shown; holdAge = 0.0f; }
    repaint();
}

void RebornAudioProcessorEditor::Meter::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(RebornTheme::bg.brighter(0.04f));
    g.fillRoundedRectangle(r, 2.0f);

    // dB scaling, so quiet material still moves the meter
    auto toY = [&r](float lin) {
        const float db = juce::Decibels::gainToDecibels(juce::jmax(lin, 1.0e-4f), -60.0f);
        return r.getBottom() - r.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };
    const float y = toY(shown);
    if (y < r.getBottom() - 0.5f) {
        auto bar = r.withTop(y).reduced(1.0f, 0.0f);
        juce::ColourGradient grad(juce::Colour(0xff5fd08a), 0.0f, r.getBottom(),
                                  juce::Colour(0xffe04a3a), 0.0f, r.getY(), false);
        grad.addColour(0.72, juce::Colour(0xffe8c53a));
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bar, 1.5f);
    }
    if (hold > 0.001f) {                            // held peak cap
        const float hy = toY(hold);
        g.setColour(hold > 0.98f ? juce::Colour(0xffe04a3a) : RebornTheme::text.withAlpha(0.8f));
        g.fillRect(r.getX() + 1.0f, hy - 1.0f, r.getWidth() - 2.0f, 1.5f);
    }
    g.setColour(RebornTheme::bg.withAlpha(0.9f));
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
}

// ==================================================================== editor
// ============================================================= arrangement bar
int RebornAudioProcessorEditor::ArrangementBar::chipCount() const {
    return proc ? (int) proc->engine().song().arrangement.size() : 0;
}
int RebornAudioProcessorEditor::ArrangementBar::preferredWidth() const {
    return (chipCount() + 1) * (kArrChipW + kArrGap) + kArrGap;   // +1 = trailing "+"
}
void RebornAudioProcessorEditor::ArrangementBar::paint(juce::Graphics& g) {
    if (!proc) return;
    auto& e = proc->engine();
    const auto& arr = e.song().arrangement;
    const int playing = e.currentSongRow();
    const float h = (float) getHeight();
    int x = kArrGap;
    for (int i = 0; i < (int) arr.size(); ++i, x += kArrChipW + kArrGap) {
        auto r = juce::Rectangle<float>((float) x, 3.0f, (float) kArrChipW, h - 6.0f);
        const bool sel = (i == selected);
        g.setColour(sel ? RebornTheme::device(0).withAlpha(0.28f)
                        : RebornTheme::panel.brighter(0.05f));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(i == playing ? kLedPos : RebornTheme::bg.darker(0.2f));
        g.drawRoundedRectangle(r, 3.0f, i == playing ? 2.0f : 1.0f);

        const SongStep& s = arr[(size_t) i];
        auto top = r.reduced(5.0f, 3.0f).removeFromTop(12.0f);
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(9.5f)));
        g.drawText(juce::String(i + 1), top, juce::Justification::centredLeft, false);
        g.drawText("x" + juce::String(s.repeats), top, juce::Justification::centredRight, false);
        g.setColour(RebornTheme::text);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        const juce::String body = juce::String(s.bass1Pattern + 1) + " "
            + juce::String(s.bass2Pattern + 1) + " " + juce::String(s.drum808Pattern + 1)
            + " " + juce::String(s.drum909Pattern + 1);
        g.drawText(body, r.withTrimmedTop(15.0f).reduced(3.0f, 2.0f),
                   juce::Justification::centred, false);
    }
    // Trailing "+" chip appends a step from the live pattern selection.
    auto plus = juce::Rectangle<float>((float) x, 3.0f, (float) kArrChipW, h - 6.0f);
    g.setColour(RebornTheme::panel.brighter(0.02f));
    g.fillRoundedRectangle(plus, 3.0f);
    g.setColour(RebornTheme::textDim);
    g.drawRoundedRectangle(plus, 3.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions(20.0f)));
    g.drawText("+", plus, juce::Justification::centred, false);
    if (arr.empty()) {
        g.setColour(RebornTheme::textDim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("<- capture the live pattern selection into song steps",
                   juce::Rectangle<int>(x + kArrChipW + 12, 0, 360, (int) h),
                   juce::Justification::centredLeft, false);
    }
}
void RebornAudioProcessorEditor::ArrangementBar::mouseDown(const juce::MouseEvent& ev) {
    const int n = chipCount();
    const int idx = (ev.x - kArrGap) / (kArrChipW + kArrGap);
    if (idx >= 0 && idx < n) { selected = idx; if (onSelect) onSelect(idx); }
    else if (idx == n)       { if (onAppend) onAppend(); }
    repaint();
}

RebornAudioProcessorEditor::RebornAudioProcessorEditor(RebornAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p) {

    setLookAndFeel(&skin().lookAndFeel());

    addAndMakeVisible(playButton_);
    playButton_.onClick = [this] { togglePlay(); };

    addAndMakeVisible(loadButton_);
    loadButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Open a session (.rbsx) or a song (.rbs)", juce::File(), "*.rbsx;*.rbs");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f.existsAsFile()) {
                    // .rbsx also restores the sample set / mod it was played
                    // with; .rbs carries only the song.
                    juce::String oerr;
                    if (!proc_.openAnyFile(f, oerr)) {
                        juce::NativeMessageBox::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon, "Could not open", oerr);
                        return;
                    }
                    proc_.transportRunning = true;
                    tempoSlider_.setValue(proc_.engine().tempo(), juce::dontSendNotification);
                    // A .rbsx restore may SWAP the 303 voices (a saved engine
                    // choice or a hosted plugin), which frees the old voice
                    // objects. Rebuild the panels so their knobs point at the
                    // current voices; refreshKnobs alone would read the freed
                    // ones (a use-after-free crash on the next repaint).
                    rebuildPanels();
    refreshRoutingButtons();
                    refreshRoutingButtons();
                    updateEngineButtons();
                    refreshKnobs();
                    syncPatternUi();
                    refreshArrangement();
                    grid_.repaint();
                    shownVoiceRevision_ = proc_.voiceRevision();   // we just rebuilt
                }
            });
    };

    addAndMakeVisible(samplesButton_);
    samplesButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Choose the drum sample-set directory (canonical names)", juce::File());
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto d = fc.getResult();
                if (d.isDirectory()) proc_.loadSampleDir(d);
            });
    };

    addAndMakeVisible(modButton_);
    modButton_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Load drum voices from a .rbm mod you own", juce::File(), "*.rbm");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f.existsAsFile()) proc_.loadModFile(f);
            });
    };

    addAndMakeVisible(saveButton_);
    saveButton_.onClick = [this] {
        // Default to the NATIVE format. .rbs is offered from the same dialog by
        // typing the extension, and only then does the lossy warning appear --
        // warning on every save would just train people to click through it.
        chooser_ = std::make_unique<juce::FileChooser>(
            "Save session (.rbsx), or type a .rbs name to export",
            juce::File(), "*.rbsx;*.rbs");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                auto f = fc.getResult();
                if (f == juce::File()) return;
                const bool asRbs = f.hasFileExtension("rbs");
                if (!asRbs && !f.hasFileExtension("rbsx")) f = f.withFileExtension("rbsx");

                juce::String err;
                const bool ok = asRbs ? proc_.saveSongFile(f, err)
                                      : proc_.saveSessionFile(f, err);
                if (!ok) {
                    juce::NativeMessageBox::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon, "Could not save", err);
                    return;
                }
                proc_.songTitle = f.getFileNameWithoutExtension();
                proc_.pendingModNote = "saved " + f.getFileName();

                if (asRbs) {
                    // Report what the 1998 format could not carry. Always list
                    // the environment, because that is the difference a user
                    // will actually hear when they reopen the file.
                    const reborn::RbsLoss loss = reborn::rbsLossFor(proc_.engine().song());
                    juce::String what;
                    if (loss.overLongPatterns > 0)
                        what << "  - " << loss.overLongPatterns
                             << " pattern(s) longer than 16 steps were truncated\n";
                    if (loss.pluginChains > 0)
                        what << "  - " << loss.pluginChains
                             << " hosted effect chain(s) and their settings were NOT saved\n";
                    // The 303 voice choice lives in the app, not the song, so it
                    // is dropped by a .rbs export. Only non-default voices matter.
                    for (int c = 0; c < 2; ++c) {
                        const auto eng = proc_.bassEngine(c);
                        if (eng == reborn::Bass303Engine::HostedPlugin)
                            what << "  - 303 #" << (c + 1) << "'s hosted plugin ("
                                 << proc_.bassPluginName(c) << ") and its state were NOT "
                                    "saved; it reopens on the default voice\n";
                        else if (eng == reborn::Bass303Engine::Open303)
                            what << "  - 303 #" << (c + 1) << " uses the Open303 voice; "
                                    ".rbs reopens it on the default (TPT parity voice)\n";
                    }
                    if (proc_.modName.isNotEmpty())
                        what << "  - the mod this was played with (" << proc_.modName
                             << ") is referenced by name only\n";
                    else if (proc_.sampleDir.isNotEmpty())
                        what << "  - the sample set was not saved; the song will "
                                "reopen using synthesis\n";
                    if (what.isNotEmpty())
                        juce::NativeMessageBox::showMessageBoxAsync(
                            juce::MessageBoxIconType::InfoIcon,
                            "Exported as .rbs (lossy)",
                            "The song itself saved normally, but the original format "
                            "cannot store everything in this session:\n\n" + what
                            + "\nSave as .rbsx to keep all of it.");
                }
            });
    };

    addAndMakeVisible(shuffleLabel_);
    shuffleLabel_.setText("Shuffle", juce::dontSendNotification);
    shuffleLabel_.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    shuffleLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    shuffleLabel_.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(shuffleSlider_);
    shuffleSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    shuffleSlider_.setRange(0.0, 1.0, 0.01);
    shuffleSlider_.setValue(proc_.engine().shuffle(), juce::dontSendNotification);
    shuffleSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    shuffleSlider_.setColour(juce::Slider::trackColourId, juce::Colour(0xff9aa7b4));
    shuffleSlider_.onValueChange = [this] {
        proc_.engine().setShuffle((float) shuffleSlider_.getValue());
    };

    // Channel strips: identity, level, placement, send, meter, mute/solo.
    for (int c = 0; c < 4; ++c) {
        auto& st = strips_[(size_t) c];
        const juce::Colour hue = RebornTheme::device(c);

        st.name = std::make_unique<juce::Label>();
        st.name->setText(kChanNames[c], juce::dontSendNotification);
        st.name->setColour(juce::Label::textColourId, hue);
        st.name->setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        addAndMakeVisible(*st.name);

        auto makeKnob = [this](std::unique_ptr<juce::Slider>& k,
                               std::unique_ptr<juce::Label>& lbl, const char* text,
                               double lo, double hi, double val, juce::Colour col) {
            k = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
            k->setRange(lo, hi, 0.0);
            k->setValue(val, juce::dontSendNotification);
            k->setColour(juce::Slider::rotarySliderFillColourId, col);
            addAndMakeVisible(*k);
            lbl = std::make_unique<juce::Label>();
            lbl->setText(text, juce::dontSendNotification);
            lbl->setJustificationType(juce::Justification::centred);
            lbl->setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
            lbl->setColour(juce::Label::textColourId, RebornTheme::textDim);
            addAndMakeVisible(*lbl);
        };
        auto& mc = proc_.engine().channel(c);
        const juce::Colour util { 0xff9aa7b4 };
        makeKnob(st.volume, st.volLbl,  "LEVEL",  0.0, 1.0, mc.volume,    hue);
        makeKnob(st.pan,    st.panLbl,  "PAN",   -1.0, 1.0, mc.pan,       util);
        makeKnob(st.send,   st.sendLbl, "DELAY",  0.0, 1.0, mc.delaySend, util);

        juce::Slider* rv = st.volume.get();
        st.volume->onValueChange = [this, c, rv] {
            proc_.engine().channel(c).volume = (float) rv->getValue(); };
        juce::Slider* rp = st.pan.get();
        st.pan->onValueChange = [this, c, rp] {
            proc_.engine().channel(c).pan = (float) rp->getValue(); };
        juce::Slider* rs = st.send.get();
        st.send->onValueChange = [this, c, rs] {
            proc_.engine().channel(c).delaySend = (float) rs->getValue(); };

        st.mute = std::make_unique<juce::TextButton>("M");
        st.mute->setClickingTogglesState(true);
        st.mute->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd05a4a));
        st.mute->onClick = [this, c] {
            proc_.engine().channel(c).mute = strips_[(size_t) c].mute->getToggleState(); };
        addAndMakeVisible(*st.mute);

        st.solo = std::make_unique<juce::TextButton>("S");
        st.solo->setClickingTogglesState(true);
        st.solo->setColour(juce::TextButton::buttonOnColourId, hue);
        st.solo->onClick = [this] {
            int mask = 0;
            for (int i = 0; i < 4; ++i)
                if (strips_[(size_t) i].solo->getToggleState()) mask |= (1 << i);
            proc_.engine().soloMask = mask; };
        addAndMakeVisible(*st.solo);

        // --- the rack's three routing switches -----------------------------
        // ReBirth's mixer strip carries DIST, PCF and COMP. DIST is a flag any
        // number of sections may set; PCF and COMP are exclusive, and clicking
        // one on a strip takes it away from whichever strip had it. The engine
        // owns that rule, so these only ask it and then re-read the answer.
        auto makeRouteBtn = [this, c, hue](std::unique_ptr<juce::TextButton>& b,
                                           const char* text, juce::Colour on) {
            b = std::make_unique<juce::TextButton>(text);
            b->setClickingTogglesState(true);
            b->setColour(juce::TextButton::buttonOnColourId, on);
            addAndMakeVisible(*b);
        };
        makeRouteBtn(st.dist, "D", juce::Colour(0xffe08a3a));
        makeRouteBtn(st.pcf,  "P", hue);
        makeRouteBtn(st.comp, "C", juce::Colour(0xff6aa9d0));
        st.dist->onClick = [this, c] {
            // As with the PCF's button: ReBirth keeps the unit's own on/off on
            // the effect panel and this strip button only routes. We have one
            // button, so routing anywhere switches the unit on.
            auto& e = proc_.engine();
            const bool on = strips_[(size_t) c].dist->getToggleState();
            e.setDistEnable(c, on);
            if (on) e.setDistUnit(true);
            refreshRoutingButtons(); };
        st.pcf->onClick = [this, c] {
            // ReBirth's panel has the unit's own on/off next to its knobs and
            // this strip button only routes. We have one button, so it does
            // both: routing somewhere switches the unit on.
            auto& e = proc_.engine();
            const bool on = strips_[(size_t) c].pcf->getToggleState();
            e.setPcfTarget(on ? c : -1);
            e.setPcfUnit(on);
            refreshRoutingButtons(); };
        st.comp->onClick = [this, c] {
            auto& e = proc_.engine();
            e.setCompTarget(strips_[(size_t) c].comp->getToggleState() ? c : -1);
            refreshRoutingButtons(); };

        st.meter = std::make_unique<Meter>();
        st.meter->proc = &proc_;
        st.meter->channel = c;
        addAndMakeVisible(*st.meter);

        // The two 303 slots carry a voice-engine picker: which synth fills the
        // slot (the native TPT parity voice, the default; Open303; or a hosted
        // plugin). The other two channels are fixed drum machines.
        if (c < 2) {
            st.engine = std::make_unique<juce::TextButton>();
            st.engine->setTooltip("303 voice engine");
            // High-contrast: dark button, bright text, hue only as a thin accent.
            st.engine->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2b2c33));
            st.engine->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            st.engine->setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
            st.engine->onClick = [this, c] { chooseBassEngine(c); };
            addAndMakeVisible(*st.engine);
        }
    }

    // Master-bus peak meter (channel -1, reads engine().masterPeak()). It was
    // declared and laid out but never created, so resized()'s unguarded
    // masterMeter_->setBounds null-dereferenced on the very first layout and
    // crashed the standalone at launch. No headless path builds the editor, so
    // nothing caught it until the GUI was first run.
    masterMeter_ = std::make_unique<Meter>();
    masterMeter_->proc = &proc_;
    masterMeter_->channel = -1;
    addAndMakeVisible(*masterMeter_);

    // Pattern-editor bar: device tabs, pattern select, length.
    for (int d = 0; d < 4; ++d) {
        auto b = std::make_unique<juce::TextButton>(kChanNames[d]);
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1001);
        b->setColour(juce::TextButton::buttonOnColourId, RebornTheme::device(d));
        b->onClick = [this, d] { setEditDevice(d); };
        addAndMakeVisible(*b);
        deviceTabs_[(size_t) d] = std::move(b);
    }
    deviceTabs_[0]->setToggleState(true, juce::dontSendNotification);

    addAndMakeVisible(patPrev_);
    patPrev_.onClick = [this] { selectPattern(grid_.currentPattern() - 1); };
    addAndMakeVisible(patNext_);
    patNext_.onClick = [this] { selectPattern(grid_.currentPattern() + 1); };

    addAndMakeVisible(patLabel_);
    patLabel_.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    patLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    patLabel_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(lengthLabel_);
    lengthLabel_.setText("Len", juce::dontSendNotification);
    lengthLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    lengthLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    lengthLabel_.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(lengthSlider_);
    lengthSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    lengthSlider_.setRange(1.0, 16.0, 1.0);
    lengthSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, 20);
    lengthSlider_.onValueChange = [this] {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        auto& s = proc_.engine().song();
        const int pat = grid_.currentPattern();
        const int L = (int) lengthSlider_.getValue();
        switch (grid_.device) {
            case 0: s.bass1.at(pat).length = L; break;
            case 1: s.bass2.at(pat).length = L; break;
            case 2: s.drum808.at(pat).length = L; break;
            default: s.drum909.at(pat).length = L; break;
        }
        grid_.repaint();
    };

    // The 909's global flam amount (grace-note gap). Shown in the edit bar only
    // while the 909 tab is active; syncPatternUi toggles its visibility.
    addAndMakeVisible(flamLabel_);
    flamLabel_.setText("Flam", juce::dontSendNotification);
    flamLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    flamLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    flamLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(flamSlider_);
    flamSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    flamSlider_.setRange(0.0, 1.0, 0.01);
    flamSlider_.setValue(proc_.engine().flam909(), juce::dontSendNotification);
    flamSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    flamSlider_.setColour(juce::Slider::trackColourId, RebornTheme::device(3));
    flamSlider_.onValueChange = [this] {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        proc_.engine().setFlam909((float) flamSlider_.getValue());
    };

    addAndMakeVisible(hintLabel_);
    hintLabel_.setText("click=step  shift/right=accent  alt=soft  cmd=flam  drag=paint",
                       juce::dontSendNotification);
    hintLabel_.setFont(juce::Font(juce::FontOptions(10.0f)));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    hintLabel_.setJustificationType(juce::Justification::centredRight);

    grid_.proc = &proc_;
    grid_.skin = &skin();
    addAndMakeVisible(grid_);

    // The device/effect panels can float out into snapping dock windows, but
    // only in the standalone app: a plugin editor is one window the host embeds.
    standalone_ = (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone);
    if (standalone_) {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Reborn338";
        o.filenameSuffix      = ".layout";
        o.folderName          = "RebornProject";
        o.osxLibrarySubFolder = "Application Support";
        dockProps_ = std::make_unique<juce::PropertiesFile>(o);
        addAndMakeVisible(dockButton_);
        dockButton_.setClickingTogglesState(true);
        dockButton_.setColour(juce::TextButton::buttonOnColourId, kAccent);
        dockButton_.setTooltip("Float the device and effect panels into snapping windows");
        dockButton_.onClick = [this] { toggleDock(); };

        addAndMakeVisible(gridButton_);
        gridButton_.setClickingTogglesState(true);
        gridButton_.setColour(juce::TextButton::buttonOnColourId, kAccent);
        gridButton_.setTooltip("Open the shared step grid (arrow keys switch engine; also the G key)");
        gridButton_.onClick = [this] { toggleGrid(); };
    }

    // Song/Pattern toggle. It was laid out and its text updated, but never made
    // visible or given a handler, so the mode could not actually be switched.
    addAndMakeVisible(modeButton_);
    modeButton_.setTooltip("Play the arrangement (Song) or the selected patterns (Pattern)");
    modeButton_.onClick = [this] {
        auto& e = proc_.engine();
        if (e.song().arrangement.empty()) return;   // nothing to play as a song
        e.setSongMode(!e.songMode());
        if (e.isPlaying()) { proc_.transportRunning = true; e.play(); }  // restart cleanly
        syncPatternUi();
        grid_.repaint();
    };

    // --- arrangement (song) editor ---
    addAndMakeVisible(songLbl_);
    songLbl_.setText("SONG", juce::dontSendNotification);
    songLbl_.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    songLbl_.setColour(juce::Label::textColourId, juce::Colours::grey);
    songLbl_.setJustificationType(juce::Justification::centred);

    arrBar_.proc = &proc_;
    arrBar_.onSelect = [this](int) { refreshArrangement(); };
    arrBar_.onAppend = [this] { captureArrangementStep(); };
    arrViewport_.setViewedComponent(&arrBar_, false);
    arrViewport_.setScrollBarsShown(false, true);   // horizontal scroll only
    addAndMakeVisible(arrViewport_);

    addAndMakeVisible(arrCapture_);
    arrCapture_.setTooltip("Append a song step from the current live pattern selection");
    arrCapture_.onClick = [this] { captureArrangementStep(); };
    addAndMakeVisible(arrDelete_);
    arrDelete_.onClick = [this] { deleteArrangementStep(); };
    addAndMakeVisible(arrClear_);
    arrClear_.onClick = [this] {
        {
            juce::SpinLock::ScopedLockType lock(proc_.engineLock());
            proc_.engine().song().arrangement.clear();
            proc_.engine().setSongMode(false);
            arrBar_.selected = -1;
        }
        refreshArrangement();
    };

    addAndMakeVisible(repeatsLbl_);
    repeatsLbl_.setText("Rep", juce::dontSendNotification);
    repeatsLbl_.setFont(juce::Font(juce::FontOptions(11.0f)));
    repeatsLbl_.setColour(juce::Label::textColourId, juce::Colours::grey);
    repeatsLbl_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(repeatsSlider_);
    repeatsSlider_.setSliderStyle(juce::Slider::IncDecButtons);
    repeatsSlider_.setRange(1.0, 64.0, 1.0);
    repeatsSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 34, 20);
    repeatsSlider_.onValueChange = [this] {
        setSelectedRepeats((int) repeatsSlider_.getValue()); };

    addAndMakeVisible(viewport_);
    content_.owner = this;
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);

    rebuildPanels();
    updateEngineButtons();

    // Let the processor close our hosted-plugin editor windows before a session
    // restore (a host state reload, or an .rbsx open) frees the voices they wrap,
    // so a CLAP/VST3 UI is torn down while its plugin is still alive rather than
    // left dangling on freed memory. Cleared in the destructor.
    proc_.setVoicesWillChangeCallback([this] {
        pluginWindows_[0].reset();
        pluginWindows_[1].reset();
    });
    // Rebuild synchronously the moment the restore finishes swapping voices, so a
    // knob is never bound to a freed voice (the 20 Hz timer below is only a
    // backstop for a host that restores off the message thread).
    proc_.setVoicesChangedCallback([this] {
        rebuildPanels();
        updateEngineButtons();
        refreshKnobs();
        shownVoiceRevision_ = proc_.voiceRevision();
        if (docked_) {
            if (dockProps_) dockMgr_.saveLayout(*dockProps_);
            buildDeviceBlocks();
            if (!dockProps_ || !dockMgr_.loadLayout(*dockProps_)) dockMgr_.resetLayout();
            dockMgr_.showAll();
        }
    });
    shownVoiceRevision_ = proc_.voiceRevision();

    syncPatternUi();
    refreshArrangement();
    setWantsKeyboardFocus(true);
    setSize(1000, 800);
    setResizable(true, true);
    startTimerHz(20);
}

RebornAudioProcessorEditor::~RebornAudioProcessorEditor() {
    // Clear the callbacks first: the processor outlives this editor and must not
    // invoke a bound lambda into a half-destroyed object.
    proc_.setVoicesWillChangeCallback(nullptr);
    proc_.setVoicesChangedCallback(nullptr);
    stopTimer();
    // Tear the floating dock windows down before anything they reference, and
    // hand the default look-and-feel back (enterDockedMode set it to ours).
    if (docked_) juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    gridBlock_.reset();
    gridOverlay_.reset();
    dockMgr_.clear();
    deviceBlocks_.clear();
    deviceViewports_.clear();
    devicePanels_.clear();
    bassBlocks_.clear();
    drumBlocks_.clear();
    setLookAndFeel(nullptr);
}

void RebornAudioProcessorEditor::togglePlay() {
    auto& e = proc_.engine();
    proc_.followHostTransport = false;
    const bool wasPlaying = e.isPlaying();
    proc_.transportRunning = !wasPlaying;      // standalone intent (see processBlock)
    if (wasPlaying) e.stop(); else e.play();
}

bool RebornAudioProcessorEditor::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (standalone_ && (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G')) {
        toggleGrid(); return true;
    }
    return false;
}

void RebornAudioProcessorEditor::setEditDevice(int device) {
    grid_.device = juce::jlimit(0, 3, device);
    syncPatternUi();
    for (auto& st : strips_) if (st.meter) st.meter->tick();
    if (masterMeter_) masterMeter_->tick();
    grid_.repaint();
}

void RebornAudioProcessorEditor::selectPattern(int pattern) {
    auto& e = proc_.engine();
    pattern = juce::jlimit(0, 31, pattern);
    {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        // A manual choice only sticks in pattern mode; in song mode the
        // arrangement would overwrite it at the next bar.
        if (e.songMode()) e.setSongMode(false);
        if (grid_.device < 2) e.selectBassPattern(grid_.device, pattern);
        else                  e.selectDrumPattern(grid_.device - 2, pattern);
    }
    syncPatternUi();
    for (auto& st : strips_) if (st.meter) st.meter->tick();
    if (masterMeter_) masterMeter_->tick();
    grid_.repaint();
}

void RebornAudioProcessorEditor::syncPatternUi() {
    patLabel_.setText("Pattern " + juce::String(grid_.currentPattern() + 1),
                      juce::dontSendNotification);
    lengthSlider_.setValue(grid_.patternLength(), juce::dontSendNotification);
    // The 909 flam-amount control only makes sense on the 909 tab.
    const bool is909 = (grid_.device == 3);
    flamLabel_.setVisible(is909);
    flamSlider_.setVisible(is909);
    if (is909)
        flamSlider_.setValue(proc_.engine().flam909(), juce::dontSendNotification);
    const bool hasArrangement = !proc_.engine().song().arrangement.empty();
    modeButton_.setEnabled(hasArrangement);
}

void RebornAudioProcessorEditor::refreshArrangement() {
    arrBar_.setSize(juce::jmax(arrViewport_.getWidth(), arrBar_.preferredWidth()),
                    juce::jmax(1, arrViewport_.getHeight()));
    const int n = (int) proc_.engine().song().arrangement.size();
    if (arrBar_.selected >= n) arrBar_.selected = n - 1;
    const bool has = n > 0;
    const bool oneSel = has && arrBar_.selected >= 0;
    arrDelete_.setEnabled(oneSel);
    arrClear_.setEnabled(has);
    repeatsSlider_.setEnabled(oneSel);
    if (oneSel)
        repeatsSlider_.setValue(
            proc_.engine().song().arrangement[(size_t) arrBar_.selected].repeats,
            juce::dontSendNotification);
    modeButton_.setEnabled(has);
    arrBar_.repaint();
}

void RebornAudioProcessorEditor::captureArrangementStep() {
    {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        auto& e = proc_.engine();
        SongStep s;
        s.bass1Pattern   = e.activePattern(kBass1);
        s.bass2Pattern   = e.activePattern(kBass2);
        s.drum808Pattern = e.activePattern(kDrum808);
        s.drum909Pattern = e.activePattern(kDrum909);
        s.pcfPattern     = e.activePcfPattern();
        s.repeats        = 1;
        e.song().arrangement.push_back(s);
        arrBar_.selected = (int) e.song().arrangement.size() - 1;
    }
    refreshArrangement();
}

void RebornAudioProcessorEditor::deleteArrangementStep() {
    {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        auto& e = proc_.engine();
        auto& arr = e.song().arrangement;
        if (arrBar_.selected >= 0 && arrBar_.selected < (int) arr.size()) {
            arr.erase(arr.begin() + arrBar_.selected);
            if (arr.empty()) { e.setSongMode(false); arrBar_.selected = -1; }
            else if (arrBar_.selected >= (int) arr.size())
                arrBar_.selected = (int) arr.size() - 1;
        }
    }
    refreshArrangement();
}

void RebornAudioProcessorEditor::setSelectedRepeats(int r) {
    {
        juce::SpinLock::ScopedLockType lock(proc_.engineLock());
        auto& arr = proc_.engine().song().arrangement;
        if (arrBar_.selected >= 0 && arrBar_.selected < (int) arr.size())
            arr[(size_t) arrBar_.selected].repeats = juce::jlimit(1, 64, r);
    }
    arrBar_.repaint();
}

// ============================================================ standalone dock
namespace {
// The main window in docked mode is the transport master dock: just the header
// (transport) and the song arrangement. The mixer, edit bar, grid and device
// panels all live in the floating blocks.
constexpr int kDockedMainH = kHeaderH + kArrangeH + 6;
constexpr int kDockedMainW = 980;       // compact main width in docked mode
constexpr int kBlockCapH   = 460;       // tall panels scroll inside the block
}

void RebornAudioProcessorEditor::toggleDock() {
    if (docked_) exitDockedMode(); else enterDockedMode();
}

void RebornAudioProcessorEditor::enterDockedMode() {
    docked_ = true;
    if (dockProps_) { dockProps_->setValue("docked", true); dockProps_->saveIfNeeded(); }
    dockButton_.setToggleState(true, juce::dontSendNotification);
    dockButton_.setButtonText("Docked");
    // The dock windows are separate top-levels; give popups + blocks our look.
    juce::LookAndFeel::setDefaultLookAndFeel(&skin().lookAndFeel());
    viewport_.setVisible(false);
    setClassicComponentsVisible(false);   // mixer/grid/edit-bar move into the blocks
    // Compact the actual top-level window to the performance surface and move it
    // to the primary display's top-left, freeing the rest of the screen for the
    // floating panels. Resizing the editor alone does not drive the standalone
    // host window, so set the window's bounds directly (keeping its border).
    if (auto* top = getTopLevelComponent()) {
        undockedTop_ = top->getBounds();
        const auto ua = juce::Desktop::getInstance().getDisplays()
                            .getPrimaryDisplay()->userArea;
        const int borderW = top->getWidth()  - getWidth();
        const int borderH = top->getHeight() - getHeight();
        top->setBounds(ua.getX(), ua.getY(),
                       kDockedMainW + borderW, kDockedMainH + borderH);
    }
    buildDeviceBlocks();
    if (!dockProps_ || !dockMgr_.loadLayout(*dockProps_)) dockMgr_.resetLayout();
    dockMgr_.showAll();
}

void RebornAudioProcessorEditor::exitDockedMode() {
    if (dockProps_) { dockMgr_.saveLayout(*dockProps_); dockProps_->setValue("docked", false); }
    docked_ = false;
    dockButton_.setToggleState(false, juce::dontSendNotification);
    dockButton_.setButtonText("Dock");
    dockMgr_.clear();
    deviceBlocks_.clear();
    deviceViewports_.clear();
    devicePanels_.clear();
    bassBlocks_.clear();
    drumBlocks_.clear();
    setClassicComponentsVisible(true);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    viewport_.setVisible(true);
    if (auto* top = getTopLevelComponent(); top && !undockedTop_.isEmpty())
        top->setBounds(undockedTop_);
    else
        setSize(1000, 800);
}

void RebornAudioProcessorEditor::buildDeviceBlocks() {
    dockMgr_.clear();
    deviceBlocks_.clear();
    deviceViewports_.clear();
    devicePanels_.clear();
    bassBlocks_.clear();
    drumBlocks_.clear();

    auto& e = proc_.engine();
    const juce::Colour fx { 0xff9aa7b4 };
    auto voiceLabel = [this](int slot) -> juce::String {
        switch (proc_.bassEngine(slot)) {
            case Bass303Engine::Native303Tpt: return "TPT";
            case Bass303Engine::HostedPlugin: {
                const auto n = proc_.bassPluginName(slot);
                return n.isNotEmpty() ? n : juce::String("plugin");
            }
            case Bass303Engine::Open303: default: return "Open303";
        }
    };

    struct Spec {
        const char* key;                 // stable dock key + strip label (voice-swap safe)
        juce::String title;              // descriptive panel title
        reborn::Processor* proc;
        dock::DevicePanel::Kind kind;
        juce::Colour accent;
        int width;
    };
    std::vector<Spec> specs = {
        { "BASS 1", "BASS 1 . " + voiceLabel(0), e.instrument(kBass1),  dock::DevicePanel::Kind::Bass,    RebornTheme::device(0), 384 },
        { "BASS 2", "BASS 2 . " + voiceLabel(1), e.instrument(kBass2),  dock::DevicePanel::Kind::Bass,    RebornTheme::device(1), 384 },
        { "808",    "RHYTHM . 808",              e.instrument(kDrum808), dock::DevicePanel::Kind::Drum808, RebornTheme::device(2), 470 },
        { "909",    "RHYTHM . 909",              e.instrument(kDrum909), dock::DevicePanel::Kind::Drum909, RebornTheme::device(3), 470 },
        { "PCF",    "PCF",                       e.pcf(),                dock::DevicePanel::Kind::Pcf,     fx, 314 },
        { "DELAY",  "DELAY",                     e.delay(),              dock::DevicePanel::Kind::Delay,   fx, 314 },
        { "DIST",   "DISTORTION",                e.distortion(),         dock::DevicePanel::Kind::Dist,    fx, 236 },
        { "COMP",   "COMPRESSOR",                e.compressor(),         dock::DevicePanel::Kind::Comp,    fx, 236 },
    };

    // Flow-pack the blocks into the screen area to the RIGHT of the compact main
    // window, wrapping into a new column when the next block would run past the
    // bottom. Tall panels (the drum dial walls) are capped and scroll inside the
    // block, so everything stays a legible height. DockManager's recovery clamps
    // any block whose column ran past the screen edge back on.
    const auto ua = juce::Desktop::getInstance().getDisplays()
                        .getPrimaryDisplay()->userArea;
    const int strip = dock::BlockWindow::kStripH;
    const int gap = 8;
    const int startX = ua.getX() + kDockedMainW + gap;
    const int topY = ua.getY();
    int x = startX, y = topY, colW = 0;

    for (size_t i = 0; i < specs.size(); ++i) {
        const auto& s = specs[i];
        juce::Component* content = nullptr;
        int contentH = 0;
        std::unique_ptr<juce::Viewport> vp;

        if (s.kind == dock::DevicePanel::Kind::Bass) {
            // The 303 reference block: dials + pattern selector + keyboard programmer.
            auto blk = std::make_unique<dock::Bass303Block>(
                proc_.engine(), (int) i, s.title, s.accent, &proc_.engineLock());
            contentH = blk->preferredHeight(s.width);
            content = blk.get();
            bassBlocks_.push_back(std::move(blk));
        } else if (s.kind == dock::DevicePanel::Kind::Drum808
                || s.kind == dock::DevicePanel::Kind::Drum909) {
            // The drum block: dials + pattern selector + step grid + mixer strip.
            const int dev = (s.kind == dock::DevicePanel::Kind::Drum808) ? kDrum808 : kDrum909;
            auto blk = std::make_unique<dock::DrumBlock>(
                proc_.engine(), dev, s.title, s.accent, &proc_.engineLock());
            contentH = blk->preferredHeight(s.width);
            content = blk.get();
            drumBlocks_.push_back(std::move(blk));
        } else {
            auto panel = std::make_unique<dock::DevicePanel>(s.proc, s.title, s.accent, s.kind,
                                                             /*showHeader*/ false);
            const int full = panel->preferredHeight(s.width);
            const bool capped = full > kBlockCapH;
            contentH = capped ? kBlockCapH : full;
            if (capped) {
                panel->setSize(s.width - 14, full);        // leave room for the scrollbar
                vp = std::make_unique<juce::Viewport>();
                vp->setViewedComponent(panel.get(), false);
                vp->setScrollBarsShown(true, false);
                content = vp.get();
            } else {
                content = panel.get();
            }
            devicePanels_.push_back(std::move(panel));
        }

        auto win = std::make_unique<dock::BlockWindow>(s.key, content, s.accent);
        win->setLookAndFeel(&skin().lookAndFeel());
        win->setIntrinsicContentSize(s.width, contentH);
        win->onMenu = [this](dock::BlockWindow*) {
            juce::PopupMenu m;
            m.addItem(1, "Dock all (reset layout)");
            m.addItem(2, "Close docking (return panels)");
            m.showMenuAsync(juce::PopupMenu::Options(), [this](int r) {
                if (r == 1) dockMgr_.resetLayout();
                else if (r == 2) exitDockedMode();
            });
        };
        if (i < 4) {   // an instrument block: activating it points the grid at it
            const int idx = (int) i;
            win->onActivated = [this, idx] { focusInstrument(idx); };
        }
        win->onKey = [this](const juce::KeyPress& k) {   // G toggles the grid from any block
            if (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G') { toggleGrid(); return true; }
            return false;
        };
        if (s.kind == dock::DevicePanel::Kind::Bass) {
            // The KEY/GRID toggle changes the block's height; resize its window.
            auto* w = win.get();
            auto* b = bassBlocks_.back().get();
            const int bw = s.width;
            b->onLayoutChanged = [w, b, bw] { w->setIntrinsicContentSize(bw, b->preferredHeight(bw)); };
        }

        const int blockH = contentH + strip;
        if (y != topY && y + blockH > ua.getBottom()) {    // wrap to a new column
            x += colW + gap;
            y = topY;
            colW = 0;
        }
        dockMgr_.addBlock(win.get(), dock::Rect{ x, y, s.width, blockH });
        y += blockH + gap;
        colW = juce::jmax(colW, s.width);

        if (vp) deviceViewports_.push_back(std::move(vp));
        deviceBlocks_.push_back(std::move(win));
    }
}

void RebornAudioProcessorEditor::refreshDeviceBlocks() {
    for (auto& p : devicePanels_) p->refresh();
    for (auto& b : bassBlocks_)   b->tick();     // dials + keyboard playhead
    for (auto& b : drumBlocks_)   b->tick();     // dials + grid playhead + mixer
}

void RebornAudioProcessorEditor::focusInstrument(int idx) {
    lastInstrument_ = juce::jlimit(0, 3, idx);
    if (gridOverlay_) gridOverlay_->setEngine(lastInstrument_);
}

void RebornAudioProcessorEditor::toggleGrid() {
    if (gridBlock_) {                                   // close
        gridBlock_.reset();
        gridOverlay_.reset();
        gridButton_.setToggleState(false, juce::dontSendNotification);
        return;
    }
    const std::array<juce::Colour, 4> acc { RebornTheme::device(0), RebornTheme::device(1),
                                            RebornTheme::device(2), RebornTheme::device(3) };
    gridOverlay_ = std::make_unique<dock::GridOverlay>(proc_.engine(), acc, &proc_.engineLock());
    gridOverlay_->onClose = [this] { toggleGrid(); };   // the G key inside the grid closes it
    const int w = 660;
    const int contentH = gridOverlay_->preferredHeight();
    gridBlock_ = std::make_unique<dock::BlockWindow>("GRID", gridOverlay_.get(),
                                                     RebornTheme::device(lastInstrument_));
    gridBlock_->setLookAndFeel(&skin().lookAndFeel());
    gridBlock_->setIntrinsicContentSize(w, contentH);
    gridBlock_->onMenu = [this](dock::BlockWindow*) { toggleGrid(); };   // right-click closes
    const auto ua = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    const int totalH = contentH + dock::BlockWindow::kStripH;
    gridBlock_->setBounds(ua.getCentreX() - w / 2, ua.getCentreY() - totalH / 2, w, totalH);
    gridBlock_->setVisible(true);
    gridBlock_->toFront(true);
    gridOverlay_->setEngine(lastInstrument_);
    gridOverlay_->grabKeyboardFocus();
    gridButton_.setToggleState(true, juce::dontSendNotification);
}

void RebornAudioProcessorEditor::setClassicComponentsVisible(bool v) {
    for (auto& st : strips_) {
        for (juce::Component* c : { (juce::Component*) st.name.get(),
                                    (juce::Component*) st.volume.get(),
                                    (juce::Component*) st.pan.get(),
                                    (juce::Component*) st.send.get(),
                                    (juce::Component*) st.volLbl.get(),
                                    (juce::Component*) st.panLbl.get(),
                                    (juce::Component*) st.sendLbl.get(),
                                    (juce::Component*) st.mute.get(),
                                    (juce::Component*) st.solo.get(),
                                    (juce::Component*) st.dist.get(),
                                    (juce::Component*) st.pcf.get(),
                                    (juce::Component*) st.comp.get(),
                                    (juce::Component*) st.engine.get(),
                                    (juce::Component*) st.meter.get() })
            if (c) c->setVisible(v);
    }
    for (auto& t : deviceTabs_) if (t) t->setVisible(v);
    patPrev_.setVisible(v); patNext_.setVisible(v); patLabel_.setVisible(v);
    lengthLabel_.setVisible(v); lengthSlider_.setVisible(v);
    flamLabel_.setVisible(v && grid_.device == 3);
    flamSlider_.setVisible(v && grid_.device == 3);
    hintLabel_.setVisible(v);
    grid_.setVisible(v);
}

void RebornAudioProcessorEditor::layoutArrangement(juce::Rectangle<int> arr) {
    songLbl_.setBounds(arr.removeFromLeft(42));
    arrCapture_.setBounds(arr.removeFromLeft(66).reduced(0, 13));
    arr.removeFromLeft(4);
    arrDelete_.setBounds(arr.removeFromLeft(40).reduced(0, 13));
    arr.removeFromLeft(4);
    arrClear_.setBounds(arr.removeFromLeft(48).reduced(0, 13));
    arr.removeFromLeft(10);
    repeatsLbl_.setBounds(arr.removeFromLeft(30).reduced(0, 13));
    repeatsSlider_.setBounds(arr.removeFromLeft(80).reduced(0, 11));
    arr.removeFromLeft(10);
    arrViewport_.setBounds(arr);
    arrBar_.setSize(juce::jmax(arrViewport_.getWidth(), arrBar_.preferredWidth()),
                    juce::jmax(1, arrViewport_.getHeight()));
}

void RebornAudioProcessorEditor::buildPanel(Processor* proc, const juce::String& title,
                                            juce::Colour accent, bool isDrum) {
    if (!proc) return;

    // Controls ReBirth's own panels do not have stay OUT of the default GUI
    // (Luke, 2026-09-18): the 303's volume/drive (level is the mixer strip), the
    // 808/909 synth-only character knobs, and the PCF's Mod-In. Identified by the
    // panel title, which carries the device.
    auto hidden = [&title](const juce::String& n) -> bool {
        if (title.startsWith("BASS")) return n == "volume" || n == "drive";
        if (title.contains("808"))    return n == "tom_tune" || n == "tom_decay"
                                          || n == "cb_tune" || n == "cp_spread" || n == "ch_decay";
        if (title.contains("909"))    return n == "cr_decay" || n == "rd_decay"
                                          || n == "hh_tune" || n == "accent_cell";
        if (title == "PCF")           return n == "mod";
        return false;
    };

    Panel panel;
    panel.title = title;
    panel.accent = accent;
    panel.firstKnob = (int) knobs_.size();

    for (int i = 0; i < proc->numParams(); ++i) {
        ParamInfo info = proc->paramInfo(i);
        const juce::String name(info.name);
        if (hidden(name)) continue;

        Knob k;
        k.proc = proc;
        k.paramIndex = i;

        // ReBirth-style control types for the discrete effect params, instead of
        // a bare 0..1 rotary: BP/LP and Straight/Triplet switches, and integer
        // steppers for delay Steps (1..16) and PCF Pattern (0..54).
        const bool isToggle  = (title == "PCF" && name == "mode")
                            || (title == "DELAY" && name == "grid");
        const bool isStepper = (title == "DELAY" && name == "steps")
                            || (title == "PCF" && name == "pattern");

        if (isToggle) {
            k.kind = Knob::Toggle;
            if (name == "mode") { k.offText = "BP"; k.onText = "LP"; }        // 0=BP,1=LP
            else                { k.offText = "Triplet"; k.onText = "Straight"; } // grid 0/1
            k.toggle = std::make_unique<juce::TextButton>();
            k.toggle->setClickingTogglesState(true);
            k.toggle->setColour(juce::TextButton::buttonOnColourId, accent);
            const bool on = proc->getParam(i) >= 0.5f;
            k.toggle->setToggleState(on, juce::dontSendNotification);
            k.toggle->setButtonText(on ? k.onText : k.offText);
            juce::TextButton* rb = k.toggle.get();
            const juce::String onT = k.onText, offT = k.offText;
            k.toggle->onClick = [rb, proc, i, onT, offT] {
                const bool s = rb->getToggleState();
                proc->setParam(i, s ? 1.0f : 0.0f);
                rb->setButtonText(s ? onT : offT);
            };
            content_.addAndMakeVisible(*k.toggle);
        } else if (isStepper) {
            k.kind  = Knob::Stepper;
            k.scale = (name == "steps") ? 16.0f : 54.0f;
            const double lo = (name == "steps") ? 1.0 : 0.0;
            k.slider = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons,
                                                      juce::Slider::TextBoxLeft);
            k.slider->setRange(lo, (double) k.scale, 1.0);
            k.slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 30, 20);
            k.slider->setValue(juce::roundToInt(proc->getParam(i) * k.scale),
                               juce::dontSendNotification);
            juce::Slider* raw = k.slider.get();
            const float sc = k.scale;
            k.slider->onValueChange = [raw, proc, i, sc] {
                proc->setParam(i, (float) (raw->getValue() / sc)); };
            content_.addAndMakeVisible(*k.slider);
        } else {
            k.kind = Knob::Rotary;
            k.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                      juce::Slider::NoTextBox);
            k.slider->setRange(info.minValue, info.maxValue, 0.0);
            k.slider->setValue(proc->getParam(i), juce::dontSendNotification);
            // Channel levels are utility, not character: desaturate them so a
            // 16-voice drum panel does not read as a wall of one colour.
            const bool isLevel = name.startsWith("level_");
            k.slider->setColour(juce::Slider::rotarySliderFillColourId,
                                isLevel ? accent.withSaturation(0.35f).withBrightness(0.62f)
                                        : accent);
            juce::Slider* raw = k.slider.get();
            k.slider->onValueChange = [raw, proc, i] { proc->setParam(i, (float) raw->getValue()); };
            content_.addAndMakeVisible(*k.slider);
        }

        k.label = std::make_unique<juce::Label>();
        k.label->setText(info.label.empty() ? info.name : info.label, juce::dontSendNotification);
        k.label->setJustificationType(juce::Justification::centredTop);
        k.label->setFont(juce::Font(juce::FontOptions(10.5f)));
        k.label->setColour(juce::Label::textColourId, RebornTheme::textDim);
        content_.addAndMakeVisible(*k.label);

        // extend the current cluster, or start a new one
        const juce::String gname = groupFor(name, isDrum);
        if (panel.groups.empty() || panel.groups.back().name != gname)
            panel.groups.push_back({ gname, (int) knobs_.size(), 0, {} });
        panel.groups.back().count++;

        knobs_.push_back(std::move(k));
    }
    panel.count = (int) knobs_.size() - panel.firstKnob;   // shown knobs only
    panels_.push_back(panel);
}

void RebornAudioProcessorEditor::rebuildPanels() {
    // Destroying the sliders/labels removes them from content_ (JUCE components
    // detach from their parent on destruction), so a plain clear is enough.
    knobs_.clear();
    panels_.clear();

    auto& e = proc_.engine();
    const juce::Colour fx { 0xff9aa7b4 };            // effects: neutral steel
    // The bass panels title themselves with the voice actually in the slot, so
    // the loaded 303 engine (or hosted plugin) is visible at a glance.
    auto voiceLabel = [this](int slot) -> juce::String {
        switch (proc_.bassEngine(slot)) {
            case Bass303Engine::Native303Tpt: return "TPT";
            case Bass303Engine::HostedPlugin: {
                const auto n = proc_.bassPluginName(slot);
                return (n.isNotEmpty() ? n : juce::String("plugin")) + " (hosted)";
            }
            case Bass303Engine::Open303: default: return "Open303";
        }
    };
    buildPanel(e.instrument(kBass1), "BASS 1 . " + voiceLabel(0), RebornTheme::device(0));
    buildPanel(e.instrument(kBass2), "BASS 2 . " + voiceLabel(1), RebornTheme::device(1));
    buildPanel(e.instrument(kDrum808), "RHYTHM . 808",  RebornTheme::device(2), true);
    buildPanel(e.instrument(kDrum909), "RHYTHM . 909",  RebornTheme::device(3), true);
    buildPanel(e.distortion(), "DISTORTION", fx);
    buildPanel(e.delay(),      "DELAY",      fx);
    buildPanel(e.compressor(), "COMPRESSOR", fx);
    buildPanel(e.pcf(),        "PCF",        fx);

    // Re-lay-out the freshly built knobs inside content_. Harmless before the
    // first setSize (bounds are empty then; the constructor's setSize re-runs it).
    resized();
    content_.repaint();
}

// Re-read the engine's routing into the four strips. The engine, not the UI,
// owns ReBirth's exclusivity rule, so after any click the buttons are told what
// actually happened rather than assuming their own click won.
void RebornAudioProcessorEditor::refreshRoutingButtons() {
    auto& e = proc_.engine();
    const int pcfT  = e.pcfTarget();
    const int compT = e.compTarget();
    for (int c = 0; c < 4; ++c) {
        auto& st = strips_[(std::size_t) c];
        if (st.dist) st.dist->setToggleState(e.channel(c).distEnable, juce::dontSendNotification);
        if (st.pcf)  st.pcf->setToggleState(pcfT == c,  juce::dontSendNotification);
        if (st.comp) st.comp->setToggleState(compT == c, juce::dontSendNotification);
    }
}

void RebornAudioProcessorEditor::updateEngineButtons() {
    for (int c = 0; c < 2; ++c) {
        auto& st = strips_[(size_t) c];
        if (!st.engine) continue;
        switch (proc_.bassEngine(c)) {
            case Bass303Engine::Open303:
                st.engine->setButtonText("O303");
                st.engine->setTooltip("303 voice: Open303 (best of breed)");
                break;
            case Bass303Engine::Native303Tpt:
                st.engine->setButtonText("TPT");
                st.engine->setTooltip("303 voice: native TPT (ReBirth parity)");
                break;
            case Bass303Engine::HostedPlugin: {
                // The button is a compact tag ("HOST"); the readable full plugin
                // name lives in the panel title and the tooltip, since a long name
                // is illegible crammed into this small button.
                const auto n = proc_.bassPluginName(c);
                st.engine->setButtonText("HOST");
                st.engine->setTooltip("303 voice: hosting " + n
                                      + "  (name also shown in the device panel title)");
                break;
            }
        }
    }
}

void RebornAudioProcessorEditor::chooseBassEngine(int channelId) {
    const Bass303Engine cur = proc_.bassEngine(channelId);
    juce::PopupMenu m;
    m.addSectionHeader("303 #" + juce::String(channelId + 1) + " voice");
    m.addItem(1, "Open303  (best of breed)", true, cur == Bass303Engine::Open303);
    m.addItem(2, "TPT  (ReBirth parity)",    true, cur == Bass303Engine::Native303Tpt);
    m.addSeparator();
    m.addItem(3, juce::String::fromUTF8("Load plugin\xE2\x80\xA6"),
              true, cur == Bass303Engine::HostedPlugin);
    if (cur == Bass303Engine::HostedPlugin)
        m.addItem(4, "Open plugin editor");

    auto* target = strips_[(size_t) channelId].engine.get();
    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target),
        [this, channelId](int choice) {
            if (choice == 1 || choice == 2) {
                const Bass303Engine eng = (choice == 1) ? Bass303Engine::Open303
                                                        : Bass303Engine::Native303Tpt;
                if (eng == proc_.bassEngine(channelId)) return;
                pluginWindows_[(size_t) channelId].reset();   // close any hosted UI
                proc_.setBassEngine(channelId, eng);
                rebuildPanels();
                updateEngineButtons();
                shownVoiceRevision_ = proc_.voiceRevision();   // we just rebuilt
            } else if (choice == 3) {
                chooseHostedPlugin(channelId);
            } else if (choice == 4) {
                openHostedEditor(channelId);
            }
        });
}

// Fork P5: the hosted-plugin sub-picker (scan / load a .vst3 / pick a known one).
void RebornAudioProcessorEditor::chooseHostedPlugin(int channelId) {
    const auto types = proc_.pluginHost().knownList().getTypes();
    juce::PopupMenu m;
    m.addItem(1000001, juce::String::fromUTF8("Scan for plugins\xE2\x80\xA6"));
    m.addItem(1000002, juce::String::fromUTF8("Load a plugin file (.vst3 / .clap)\xE2\x80\xA6"));
    if (types.size() > 0) {
        m.addSeparator();
        juce::KnownPluginList::addToMenu(m, types, juce::KnownPluginList::sortByManufacturer);
    }
    auto* target = strips_[(size_t) channelId].engine.get();
    m.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target),
        [this, channelId, types](int result) {
            if (result == 0) return;
            if (result == 1000001) { proc_.pluginHost().scan(); return; }
            if (result == 1000002) { pickPluginFile(channelId); return; }
            const int idx = juce::KnownPluginList::getIndexChosenByMenu(types, result);
            if (idx >= 0) loadHostedPlugin(channelId, types.getReference(idx));
        });
}

void RebornAudioProcessorEditor::loadHostedPlugin(int channelId,
                                                  const juce::PluginDescription& desc) {
    // Refuse to host Reborn 338 itself: nesting the whole groovebox inside its own
    // 303 slot is a recursion/CPU footgun and just confusing (its editor is the
    // entire interface). A user browsing plugin files can pick our own .clap/.vst3
    // by mistake; catch it here.
    if (desc.name.startsWithIgnoreCase("Reborn 338")) {
        juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            "Cannot host Reborn 338",
            "Reborn 338 cannot host itself in a 303 slot. Pick a third-party 303 "
            "(such as JC303) or another synth plugin instead.");
        return;
    }
    juce::String err;
    auto inst = proc_.pluginHost().create(desc, proc_.engine().sampleRate(), 512, err);
    if (inst == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Plugin", desc.name + ":\n" + err);
        return;
    }
    pluginWindows_[(size_t) channelId].reset();   // drop a stale hosted UI first
    auto voice = std::make_unique<HostedInstrument>(std::move(inst), desc.name);
    proc_.installBassPlugin(channelId, std::move(voice), desc.name,
                            desc.createIdentifierString());
    rebuildPanels();
    updateEngineButtons();
    shownVoiceRevision_ = proc_.voiceRevision();   // we just rebuilt
}

void RebornAudioProcessorEditor::pickPluginFile(int channelId) {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Choose a plugin (.vst3 or .clap)", juce::File(), "*.vst3;*.clap");
    const auto browseFlags = juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::canSelectDirectories;
    chooser_->launchAsync(browseFlags, [this, channelId](const juce::FileChooser& fc) {
        const juce::File f = fc.getResult();
        if (f == juce::File()) return;
        const auto added = proc_.pluginHost().addFile(f.getFullPathName());
        if (added.isEmpty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Plugin", "No plugin found in:\n" + f.getFullPathName());
            return;
        }
        loadHostedPlugin(channelId, added.getReference(0));
    });
}

void RebornAudioProcessorEditor::openHostedEditor(int channelId) {
    auto& w = pluginWindows_[(size_t) channelId];
    // Closing the window only HIDES it (PluginWindow::closeButtonPressed), so a
    // second "Open plugin editor" must resurface the existing window. Rebuilding
    // it would call createEditorIfNeeded again, which hands back the SAME editor
    // the old window still owns; the unique_ptr assignment then destroys the old
    // window and deletes that editor, leaving the new window permanently blank.
    if (w != nullptr) {
        w->setVisible(true);
        w->toFront(true);
        return;
    }
    auto* hosted = dynamic_cast<HostedInstrument*>(proc_.engine().instrument(channelId));
    if (hosted == nullptr || hosted->plugin() == nullptr) return;
    w = PluginWindow::makeFor(*hosted->plugin());
}

void RebornAudioProcessorEditor::refreshKnobs() {
    for (auto& k : knobs_) {
        if (!k.proc) continue;
        const float p = k.proc->getParam(k.paramIndex);
        if (k.kind == Knob::Toggle) {
            const bool on = p >= 0.5f;
            k.toggle->setToggleState(on, juce::dontSendNotification);
            k.toggle->setButtonText(on ? k.onText : k.offText);
        } else if (k.kind == Knob::Stepper) {
            k.slider->setValue(juce::roundToInt(p * k.scale), juce::dontSendNotification);
        } else {
            k.slider->setValue(p, juce::dontSendNotification);
        }
    }
    for (int c = 0; c < 4; ++c) {
        auto& mc = proc_.engine().channel(c);
        strips_[(size_t) c].volume->setValue(mc.volume,  juce::dontSendNotification);
        strips_[(size_t) c].pan->setValue(mc.pan,        juce::dontSendNotification);
        strips_[(size_t) c].send->setValue(mc.delaySend, juce::dontSendNotification);
    }
    shuffleSlider_.setValue(proc_.engine().shuffle(), juce::dontSendNotification);
}

void RebornAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    // transport bar (+ mixer strip when not docked) read as one machined top plate
    auto top = juce::Rectangle<int>(0, 0, getWidth(), docked_ ? kHeaderH : kHeaderH + kStripH);
    g.setGradientFill(juce::ColourGradient(RebornTheme::panelHi, 0.0f, 0.0f,
                                           RebornTheme::panel, 0.0f,
                                           (float) top.getBottom(), false));
    g.fillRect(top);
    g.setColour(RebornTheme::bg.withAlpha(0.9f));
    g.fillRect(0, kHeaderH - 1, getWidth(), 1);
    if (!docked_) g.fillRect(0, top.getBottom() - 1, getWidth(), 1);

    // tempo readout sits in a recessed window
    if (!bpmBox_.isEmpty()) {
        auto f = bpmBox_.toFloat();
        g.setColour(RebornTheme::bg.darker(0.35f));
        g.fillRoundedRectangle(f, 4.0f);
        g.setColour(juce::Colour(0xffffa23d).withAlpha(0.18f));
        g.drawRoundedRectangle(f, 4.0f, 1.0f);
    }
    // hairlines separating the transport / file / identity clusters
    g.setColour(RebornTheme::bg.withAlpha(0.85f));
    for (auto d : { transportDiv_, fileDiv_ })
        if (!d.isEmpty()) g.fillRect(d.getX(), d.getY() + 4, 1, d.getHeight() - 8);

    // each channel gets its own recessed face, tinted by its device
    for (int c = 0; c < 4; ++c) {
        const auto& st = strips_[(size_t) c];
        if (st.bounds.isEmpty()) continue;
        RebornTheme::drawPanel(g, st.bounds, RebornTheme::device(c));
    }
}

void RebornAudioProcessorEditor::resized() {
    auto area = getLocalBounds();

    auto header = area.removeFromTop(kHeaderH).reduced(kPad, 7);

    // TRANSPORT cluster: the two controls you reach for constantly.
    playButton_.setBounds(header.removeFromLeft(80));
    header.removeFromLeft(5);
    modeButton_.setBounds(header.removeFromLeft(74));
    header.removeFromLeft(9);
    if (standalone_) {
        dockButton_.setBounds(header.removeFromLeft(60));
        header.removeFromLeft(4);
        gridButton_.setBounds(header.removeFromLeft(52));
        header.removeFromLeft(9);
    }
    transportDiv_ = header.removeFromLeft(1);
    header.removeFromLeft(9);

    // FILE cluster: occasional actions, grouped together and set back.
    loadButton_.setBounds(header.removeFromLeft(88));
    header.removeFromLeft(4);
    samplesButton_.setBounds(header.removeFromLeft(78));
    header.removeFromLeft(4);
    modButton_.setBounds(header.removeFromLeft(56));
    header.removeFromLeft(4);
    saveButton_.setBounds(header.removeFromLeft(62));
    header.removeFromLeft(9);
    fileDiv_ = header.removeFromLeft(1);
    header.removeFromLeft(10);

    // TEMPO readout + fine slider, hard right.
    auto masterCol = header.removeFromRight(58);
    masterMeter_->setBounds(masterCol.removeFromRight(11).reduced(0, 1));
    masterCol.removeFromRight(4);
    masterLbl_.setBounds(masterCol.removeFromBottom(11));
    masterSlider_.setBounds(masterCol);
    header.removeFromRight(10);

    auto tempoCol = header.removeFromRight(126);
    bpmBox_ = tempoCol.removeFromTop(tempoCol.getHeight() - 14);
    auto boxIn = bpmBox_.reduced(7, 3);
    bpmUnitLabel_.setBounds(boxIn.removeFromRight(30));
    bpmLabel_.setBounds(boxIn);
    tempoCol.removeFromTop(2);
    tempoSlider_.setBounds(tempoCol);

    header.removeFromRight(12);
    auto shuffleCol = header.removeFromRight(112);
    shuffleLabel_.setBounds(shuffleCol.removeFromTop(shuffleCol.getHeight() - 14));
    shuffleCol.removeFromTop(2);
    shuffleSlider_.setBounds(shuffleCol);

    header.removeFromRight(12);
    // Song identity fills what is left: title over status.
    titleLabel_.setBounds(header.removeFromTop(header.getHeight() - 15));
    statusLabel_.setBounds(header);

    if (docked_) {
        // Transport master dock: the header, then the song arrangement. The mixer,
        // edit bar, grid and device panels are all floating blocks now.
        area.removeFromTop(2);
        layoutArrangement(area.removeFromTop(kArrangeH).reduced(kPad, 4));
        for (auto& st : strips_) st.bounds = {};    // so paint() skips the strip faces
        return;
    }

    auto strip = area.removeFromTop(kStripH).reduced(kPad, 5);
    const int cw = strip.getWidth() / 4;
    for (int c = 0; c < 4; ++c) {
        auto cell = strip.removeFromLeft(cw).reduced(3, 0);
        auto& st = strips_[(size_t) c];
        st.bounds = cell;
        auto inner = cell.reduced(7, 5);
        auto nameRow = inner.removeFromTop(15);
        // Bass strips share the name row with their voice-engine picker.
        if (c < 2 && st.engine)
            st.engine->setBounds(nameRow.removeFromRight(46).withTrimmedLeft(4));
        st.name->setBounds(nameRow);
        inner.removeFromTop(2);
        auto meterCol = inner.removeFromRight(11);
        st.meter->setBounds(meterCol.reduced(0, 1));
        inner.removeFromRight(6);
        auto btns = inner.removeFromRight(32);
        st.mute->setBounds(btns.removeFromTop(btns.getHeight() / 2).reduced(0, 1));
        st.solo->setBounds(btns.reduced(0, 1));
        inner.removeFromRight(4);
        // The three routing switches, stacked in ReBirth's own order: DIST at
        // the top, then PCF, then COMP.
        auto route = inner.removeFromRight(18);
        const int rh = route.getHeight() / 3;
        if (st.dist) st.dist->setBounds(route.removeFromTop(rh).reduced(0, 1));
        if (st.pcf)  st.pcf->setBounds(route.removeFromTop(rh).reduced(0, 1));
        if (st.comp) st.comp->setBounds(route.reduced(0, 1));
        inner.removeFromRight(4);
        const int kw = inner.getWidth() / 3;
        auto place = [&inner, kw](std::unique_ptr<juce::Slider>& k,
                                  std::unique_ptr<juce::Label>& l) {
            auto col = inner.removeFromLeft(kw);
            l->setBounds(col.removeFromBottom(11));
            k->setBounds(col.reduced(2, 0));
        };
        place(st.volume, st.volLbl);
        place(st.pan,    st.panLbl);
        place(st.send,   st.sendLbl);
    }

    auto bar = area.removeFromTop(kEditBarH).reduced(kPad, 2);
    for (int d = 0; d < 4; ++d) {
        deviceTabs_[(size_t) d]->setBounds(bar.removeFromLeft(66).reduced(1, 0));
        bar.removeFromLeft(2);
    }
    bar.removeFromLeft(kPad);
    patPrev_.setBounds(bar.removeFromLeft(26));
    patLabel_.setBounds(bar.removeFromLeft(80));
    patNext_.setBounds(bar.removeFromLeft(26));
    bar.removeFromLeft(kPad);
    lengthLabel_.setBounds(bar.removeFromLeft(30));
    lengthSlider_.setBounds(bar.removeFromLeft(92));
    bar.removeFromLeft(kPad);
    // 909 flam-amount slot (only visible on the 909 tab; the hint takes the rest).
    flamLabel_.setBounds(bar.removeFromLeft(32));
    flamSlider_.setBounds(bar.removeFromLeft(88));
    bar.removeFromLeft(kPad);
    hintLabel_.setBounds(bar);

    grid_.setBounds(area.removeFromTop(kGridH).reduced(kPad, 2));

    // Song-arrangement section: label + edit controls on the left, then the
    // scrollable chip strip filling the rest.
    layoutArrangement(area.removeFromTop(kArrangeH).reduced(kPad, 4));

    viewport_.setBounds(area);
    const int W = viewport_.getWidth() - viewport_.getScrollBarThickness() - kPad;
    const int perRow = juce::jmax(1, (W - kPad - 16) / kKnobW);
    int y = kPad;
    for (size_t pi = 0; pi < panels_.size(); ++pi) {
        auto& panel = panels_[pi];
        const int panelTop = y;
        y += kPanelHead;
        const bool showGroups = panel.groups.size() > 1;
        for (auto& grp : panel.groups) {
            if (showGroups) {
                grp.labelBounds = juce::Rectangle<int>(kPad + 10, y, W - 20, kGroupHead - 2);
                y += kGroupHead;
            } else {
                grp.labelBounds = {};
            }
            const int rows = (grp.count + perRow - 1) / juce::jmax(1, perRow);
            for (int j = 0; j < grp.count; ++j) {
                const int col = j % perRow, row = j / perRow;
                auto& k = knobs_[(size_t) grp.firstKnob + (size_t) j];
                const int kx = kPad + 8 + col * kKnobW;
                const int ky = y + row * kKnobH;
                if (k.kind == Knob::Rotary)
                    k.slider->setBounds(kx + 6, ky + 2, kKnobW - 12, kKnobH - 22);
                else if (k.kind == Knob::Stepper)
                    k.slider->setBounds(kx + 3, ky + (kKnobH - 22) / 2 - 10, kKnobW - 6, 22);
                else   // Toggle
                    k.toggle->setBounds(kx + 8, ky + (kKnobH - 22) / 2 - 10, kKnobW - 16, 24);
                k.label->setBounds(kx + 2, ky + kKnobH - 20, kKnobW - 4, 15);
            }
            y += rows * kKnobH;
        }
        y += 8;
        panel.bounds = juce::Rectangle<int>(kPad, panelTop, W, y - panelTop);
        y += kPad;
    }
    content_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(),
                     juce::jmax(y, area.getHeight()));
}

void RebornAudioProcessorEditor::timerCallback() {
    // Restore the last docked/undocked choice once, on the first tick, when the
    // top-level window exists so the floating blocks can be placed beside it.
    if (standalone_ && !autoDockChecked_) {
        autoDockChecked_ = true;
        // Docking is the default interface (PLAN_GUI section 3): open docked unless
        // the user has explicitly turned it off in a previous session.
        if (!dockProps_ || dockProps_->getBoolValue("docked", true)) enterDockedMode();
    }
    // A host state restore (getState/setState) can swap the 303 voices without
    // going through our own load path, leaving our knob panels bound to freed
    // voices. Notice it via the revision counter and rebuild. (Any hosted-plugin
    // windows were already closed by the processor's pre-swap callback.)
    if (const std::uint32_t rev = proc_.voiceRevision(); rev != shownVoiceRevision_) {
        shownVoiceRevision_ = rev;
        rebuildPanels();
        updateEngineButtons();
        refreshKnobs();
        if (docked_) {                       // a swapped voice = new proc pointers
            if (dockProps_) dockMgr_.saveLayout(*dockProps_);
            buildDeviceBlocks();
            if (!dockProps_ || !dockMgr_.loadLayout(*dockProps_)) dockMgr_.resetLayout();
            dockMgr_.showAll();
        }
    }
    auto& e = proc_.engine();
    const bool playing = e.isPlaying();
    playButton_.setButtonText(playing ? "Stop" : "Play");
    playButton_.setColour(juce::TextButton::buttonColourId,
                          playing ? juce::Colour(0xff3f7d5c) : RebornTheme::panelHi);
    playButton_.setColour(juce::TextButton::textColourOffId,
                          playing ? juce::Colours::white : RebornTheme::text);
    modeButton_.setButtonText(e.songMode() ? "Song" : "Pattern");
    titleLabel_.setText(proc_.songTitle + (proc_.songAuthor.isNotEmpty()
                        ? "  -  " + proc_.songAuthor : juce::String()),
                        juce::dontSendNotification);
    statusLabel_.setText(proc_.pendingModNote.isNotEmpty()
                         ? proc_.pendingModNote
                         : (proc_.sampleVoices > 0
                            ? (proc_.modName.isNotEmpty()
                               ? proc_.modName + ": " + juce::String(proc_.sampleVoices) + " voices"
                               : juce::String(proc_.sampleVoices) + " sampled voices")
                            : juce::String("synthesis only")),
                         juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId,
                           proc_.pendingModNote.isNotEmpty() ? kAccent : juce::Colours::grey);
    const double t = e.tempo();
    if (std::abs(t - tempoSlider_.getValue()) > 0.05)
        tempoSlider_.setValue(t, juce::dontSendNotification);
    bpmLabel_.setText(juce::String(t, 1), juce::dontSendNotification);
    // In song mode the arrangement drives the pattern, so keep the label live.
    const int shown = grid_.currentPattern() + 1;
    if (patLabel_.getText() != "Pattern " + juce::String(shown))
        syncPatternUi();
    for (auto& st : strips_) if (st.meter) st.meter->tick();
    if (masterMeter_) masterMeter_->tick();
    grid_.repaint();
    if (e.songMode()) arrBar_.repaint();   // follow the playing arrangement row
    if (docked_) refreshDeviceBlocks();    // reflect automation into floating panels
    if (gridOverlay_) gridOverlay_->tick();
}
