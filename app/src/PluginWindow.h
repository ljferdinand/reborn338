// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// PluginWindow: a plain DocumentWindow that owns a hosted plugin's own editor,
// so a user can open jc303's (or any hosted synth's) native UI from Reborn. Falls
// back to a GenericAudioProcessorEditor for plugins that have no editor.
//
#include <juce_audio_processors/juce_audio_processors.h>

class PluginWindow : public juce::DocumentWindow {
public:
    PluginWindow(const juce::String& name, juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow(name, juce::Colour(0xff222226),
                               juce::DocumentWindow::closeButton) {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);
        // NOT resizable: a resizable JUCE window breaks the embedded plugin child
        // window's rendering (it comes up blank). The editor sizes itself to the
        // plugin instead (see ClapEditor).
        setResizable(false, false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    // Build a window for a hosted plugin, preferring its own editor.
    static std::unique_ptr<PluginWindow> makeFor(juce::AudioPluginInstance& p) {
        juce::AudioProcessorEditor* ed =
            p.hasEditor() ? p.createEditorIfNeeded() : nullptr;
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor(p);
        return std::make_unique<PluginWindow>(p.getName(), ed);
    }

    void closeButtonPressed() override { setVisible(false); }
};
