// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "ClapPluginFormat.h"

// The CLAP C API (MIT). Pulled in via the clap-core INTERFACE target that
// clap-juce-extensions already fetches for the P4 export.
#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

//========================= shared loaded module ==============================
// One loaded .clap binary: its DynamicLibrary, the clap_entry it exports, and
// the plugin factory. init()/deinit() are ref-counted by the plugin per the
// CLAP spec, so each instance owning its own module is fine (Windows also
// ref-counts the underlying DLL handle).
namespace {

struct ClapModule : public juce::ReferenceCountedObject {
    using Ptr = juce::ReferenceCountedObjectPtr<ClapModule>;

    juce::DynamicLibrary lib;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    juce::String path;

    ~ClapModule() override { if (entry != nullptr) entry->deinit(); }

    static Ptr load(const juce::String& p) {
        Ptr m = new ClapModule();
        m->path = p;
        if (!m->lib.open(p)) return nullptr;
        auto* e = reinterpret_cast<const clap_plugin_entry_t*>(m->lib.getFunction("clap_entry"));
        if (e == nullptr) return nullptr;
        if (!e->init(p.toRawUTF8())) return nullptr;
        m->entry = e;
        m->factory = reinterpret_cast<const clap_plugin_factory_t*>(
            e->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (m->factory == nullptr) return nullptr;
        return m;
    }
};

// "<path>.clap|<clap-plugin-id>": a .clap file can hold several plugins.
juce::String makeIdentifier(const juce::String& file, const char* clapId) {
    return file + "|" + juce::String(juce::CharPointer_UTF8(clapId));
}
void splitIdentifier(const juce::String& id, juce::String& file, juce::String& clapId) {
    const int bar = id.indexOfChar('|');
    if (bar < 0) { file = id; clapId = {}; return; }
    file   = id.substring(0, bar);
    clapId = id.substring(bar + 1);
}

void fillDescription(const clap_plugin_descriptor_t* d, const juce::String& file,
                     juce::PluginDescription& pd) {
    pd.name              = juce::String(juce::CharPointer_UTF8(d->name));
    pd.descriptiveName   = pd.name;
    pd.pluginFormatName  = "CLAP";
    pd.manufacturerName  = d->vendor != nullptr ? juce::String(juce::CharPointer_UTF8(d->vendor)) : juce::String();
    pd.version           = d->version != nullptr ? juce::String(juce::CharPointer_UTF8(d->version)) : juce::String();
    pd.fileOrIdentifier  = makeIdentifier(file, d->id);
    pd.uniqueId          = (int) juce::String(juce::CharPointer_UTF8(d->id)).hashCode();
    pd.deprecatedUid     = pd.uniqueId;
    pd.hasSharedContainer = false;
    pd.isInstrument      = false;
    pd.category          = "CLAP";
    if (d->features != nullptr)
        for (const char* const* f = d->features; *f != nullptr; ++f)
            if (juce::String(*f) == juce::String(CLAP_PLUGIN_FEATURE_INSTRUMENT))
                pd.isInstrument = true;
}

} // namespace

//============================ hosted parameter ===============================
// A CLAP parameter is set by pushing a CLAP_EVENT_PARAM_VALUE into the plugin's
// process events, not by a direct setter. So setValue records a new value and a
// dirty flag; the instance's processBlock drains dirty params into the event
// list. getValue reads our own cached copy (the plugin's true state is captured
// by clap_plugin_state for persistence).
class ClapParameter : public juce::HostedAudioProcessorParameter {
public:
    ClapParameter(clap_id id, const clap_param_info_t& info)
        : paramId(id),
          pname(juce::String(juce::CharPointer_UTF8(info.name))),
          minV(info.min_value), maxV(info.max_value), defV(info.default_value),
          value(info.default_value) {}

    float getValue() const override                 { return (float) norm(value.load()); }
    void  setValue(float newNorm) override          { value.store(denorm(newNorm)); dirty.store(true); }
    float getDefaultValue() const override          { return (float) norm(defV); }
    juce::String getName(int len) const override    { return pname.substring(0, len > 0 ? len : pname.length()); }
    juce::String getLabel() const override          { return {}; }
    float getValueForText(const juce::String& t) const override {
        return juce::jlimit(0.0f, 1.0f, t.getFloatValue());
    }
    juce::String getParameterID() const override    { return juce::String((int) paramId); }

    // Called from processBlock: if the host changed this param, hand back the
    // CLAP value to emit and clear the flag.
    bool takeChange(double& outValue) {
        if (!dirty.exchange(false)) return false;
        outValue = value.load();
        return true;
    }
    // Adopt the plugin's own value (after a state load, or the plugin's editor
    // changed it) without re-emitting it back to the plugin.
    void refreshFromPlugin(double v) { value.store(v); dirty.store(false); }
    clap_id id() const { return paramId; }

private:
    double norm(double v) const   { return maxV > minV ? juce::jlimit(0.0, 1.0, (v - minV) / (maxV - minV)) : 0.0; }
    double denorm(float n) const  { return minV + (double) juce::jlimit(0.0f, 1.0f, n) * (maxV - minV); }

    clap_id paramId;
    juce::String pname;
    double minV, maxV, defV;
    std::atomic<double> value;
    std::atomic<bool>   dirty { false };
};

//============================ hosted instance ================================
class ClapPluginInstance : public juce::AudioPluginInstance {
public:
    ClapPluginInstance(ClapModule::Ptr m, const juce::String& clapId, const juce::String& file)
        : module(std::move(m)), identifierFile(file) {
        host.clap_version = CLAP_VERSION;
        host.host_data    = this;
        host.name    = "Reborn 338";
        host.vendor  = "RebornProject";
        host.url     = "https://github.com/ljferdinand/reborn338";
        host.version = "0.1.0";
        host.get_extension    = &hostGetExtension;
        host.request_restart  = &hostRequestRestart;
        host.request_process  = &hostRequestProcess;
        host.request_callback = &hostRequestCallback;

        plugin = module->factory->create_plugin(module->factory, &host, clapId.toRawUTF8());
        if (plugin != nullptr && !plugin->init(plugin)) {
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (plugin == nullptr) return;

        pluginName   = juce::String(juce::CharPointer_UTF8(plugin->desc->name));
        extParams    = (const clap_plugin_params_t*)     plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        extState     = (const clap_plugin_state_t*)      plugin->get_extension(plugin, CLAP_EXT_STATE);
        extAudio     = (const clap_plugin_audio_ports_t*)plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
        extNote      = (const clap_plugin_note_ports_t*) plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS);
        extGui       = (const clap_plugin_gui_t*)        plugin->get_extension(plugin, CLAP_EXT_GUI);

        // Output channel count from the first output audio port (default stereo).
        outChannels = 2;
        if (extAudio != nullptr && extAudio->count(plugin, false) > 0) {
            clap_audio_port_info_t info {};
            if (extAudio->get(plugin, 0, false, &info))
                outChannels = (int) juce::jlimit((uint32_t) 1, (uint32_t) 2, info.channel_count);
        }
        // Note dialect: prefer raw MIDI (what our sequencer produces); fall back
        // to CLAP notes if the plugin only speaks that.
        useMidi = true;
        if (extNote != nullptr && extNote->count(plugin, true) > 0) {
            clap_note_port_info_t info {};
            if (extNote->get(plugin, 0, true, &info))
                useMidi = (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0
                          || (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) == 0;
        }

        buildParameters();
        // Pre-size the audio-thread input-event list so buildInputEvents never
        // reallocates while processing: all params may change in one block, plus
        // headroom for note events. It is cleared (capacity kept) each block.
        events.reserve((size_t) clapParams.size() + 64);
        setPlayConfigDetails(0, outChannels, getSampleRate() > 0 ? getSampleRate() : 44100.0, 512);
    }

    ~ClapPluginInstance() override {
        if (plugin != nullptr) {
            if (processing) plugin->stop_processing(plugin);
            if (activated)  plugin->deactivate(plugin);
            plugin->destroy(plugin);
        }
    }

    bool isValid() const { return plugin != nullptr; }

    //---- AudioPluginInstance -------------------------------------------------
    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name             = pluginName;
        d.descriptiveName  = pluginName;
        d.pluginFormatName = "CLAP";
        d.manufacturerName = plugin != nullptr && plugin->desc->vendor != nullptr
                           ? juce::String(juce::CharPointer_UTF8(plugin->desc->vendor)) : juce::String();
        d.version          = plugin != nullptr && plugin->desc->version != nullptr
                           ? juce::String(juce::CharPointer_UTF8(plugin->desc->version)) : juce::String();
        d.fileOrIdentifier = makeIdentifier(identifierFile,
                                            plugin != nullptr ? plugin->desc->id : "");
        d.uniqueId         = plugin != nullptr
                           ? (int) juce::String(juce::CharPointer_UTF8(plugin->desc->id)).hashCode() : 0;
        d.deprecatedUid    = d.uniqueId;
        d.isInstrument     = true;
        d.category         = "CLAP";
    }

    const juce::String getName() const override { return pluginName; }
    double getTailLengthSeconds() const override { return 1.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return guiUsable(); }
    juce::AudioProcessorEditor* createEditor() override;   // defined after ClapEditor

    // Editor access (message thread).
    const clap_plugin_t*     rawPlugin() const { return plugin; }
    const clap_plugin_gui_t* gui()       const { return extGui; }
    bool guiUsable() const {
        return plugin != nullptr && extGui != nullptr && extGui->is_api_supported != nullptr
            && extGui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false);
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sr, int samplesPerBlockExpected) override {
        if (plugin == nullptr) return;
        if (processing) { plugin->stop_processing(plugin); processing = false; }
        if (activated)  { plugin->deactivate(plugin); activated = false; }
        maxBlock = juce::jmax(1, samplesPerBlockExpected);
        setRateAndBufferSizeDetails(sr, maxBlock);
        // min_frames_count = 1: blocks vary (a host may send a short final chunk),
        // and some plugin frameworks reject a minimum of 0.
        activated = plugin->activate(plugin, sr, 1, (uint32_t) maxBlock);
    }

    void releaseResources() override {
        if (plugin == nullptr) return;
        if (processing) { plugin->stop_processing(plugin); processing = false; }
        if (activated)  { plugin->deactivate(plugin);      activated = false; }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        const int nSamples = buffer.getNumSamples();
        if (plugin == nullptr || !activated) { buffer.clear(); return; }
        if (!processing) {
            processing = plugin->start_processing(plugin);
            // CLAP forbids process() unless start_processing() returned true. If
            // the plugin refuses to start, output silence rather than call process
            // on an unstarted plugin (undefined, and it would repeat every block).
            if (!processing) { buffer.clear(); return; }
        }

        buildInputEvents(midi, nSamples);

        float* chans[2];
        const int nch = juce::jmin(outChannels, buffer.getNumChannels(), 2);
        for (int c = 0; c < nch; ++c) chans[c] = buffer.getWritePointer(c);
        for (int c = nch; c < 2; ++c)  chans[c] = chans[nch > 0 ? nch - 1 : 0];

        clap_audio_buffer_t out {};
        out.data32 = chans;
        out.channel_count = (uint32_t) juce::jmax(1, nch);

        clap_process_t proc {};
        proc.steady_time         = steadyTime;
        proc.frames_count        = (uint32_t) nSamples;
        proc.transport           = nullptr;
        proc.audio_inputs        = nullptr;
        proc.audio_inputs_count  = 0;
        proc.audio_outputs       = &out;
        proc.audio_outputs_count = 1;
        proc.in_events           = &inEventsList;
        proc.out_events          = &outEventsList;

        plugin->process(plugin, &proc);
        steadyTime += nSamples;
    }

    void getStateInformation(juce::MemoryBlock& dest) override {
        if (plugin == nullptr || extState == nullptr) return;
        if (!processing) flushPending();   // ensure recent param edits are in the plugin
        dest.reset();
        clap_ostream_t os {};
        os.ctx   = &dest;
        os.write = [](const clap_ostream_t* s, const void* buf, uint64_t n) -> int64_t {
            static_cast<juce::MemoryBlock*>(s->ctx)->append(buf, (size_t) n);
            return (int64_t) n;
        };
        extState->save(plugin, &os);
    }

    void setStateInformation(const void* data, int size) override {
        if (plugin == nullptr || extState == nullptr || size <= 0) return;
        struct Reader { const char* p; int left; } reader { (const char*) data, size };
        clap_istream_t is {};
        is.ctx  = &reader;
        is.read = [](const clap_istream_t* s, void* buf, uint64_t n) -> int64_t {
            auto* r = static_cast<Reader*>(s->ctx);
            const int take = juce::jmin((int) n, r->left);
            if (take > 0) { std::memcpy(buf, r->p, (size_t) take); r->p += take; r->left -= take; }
            return take;
        };
        extState->load(plugin, &is);
        // Some plugin frameworks (clap-juce-extensions among them) only adopt a
        // loaded state on the next process call, so run one silent block to make
        // the state take effect before we read the params back.
        if (activated) {
            juce::AudioBuffer<float> tmp(juce::jmax(2, outChannels), 64);
            juce::MidiBuffer none;
            tmp.clear();
            processBlock(tmp, none);
        }
        // The plugin's params now hold the loaded values; adopt them so our
        // getValue (and the generic panel) reflect the restored state.
        if (extParams != nullptr && extParams->get_value != nullptr)
            for (auto* p : clapParams) {
                double v = 0.0;
                if (extParams->get_value(plugin, p->id(), &v)) p->refreshFromPlugin(v);
            }
    }

private:
    //---- host callbacks ------------------------------------------------------
    static const void* hostGetExtension(const clap_host_t*, const char*)  { return nullptr; }
    static void hostRequestRestart(const clap_host_t*)  {}
    static void hostRequestProcess(const clap_host_t*)  {}
    static void hostRequestCallback(const clap_host_t*) {}

    //---- input events (params, then MIDI/notes) ------------------------------
    // Append a CLAP_EVENT_PARAM_VALUE for every param the host has changed since
    // the last drain. Returns how many were appended.
    int appendParamChanges() {
        int n = 0;
        for (auto* p : clapParams) {
            double v;
            if (p->takeChange(v)) {
                ClapEvent e {};
                e.param.header.size     = sizeof(clap_event_param_value_t);
                e.param.header.time     = 0;
                e.param.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.param.header.type     = CLAP_EVENT_PARAM_VALUE;
                e.param.header.flags    = 0;
                e.param.param_id        = p->id();
                e.param.cookie          = nullptr;
                e.param.note_id         = -1;
                e.param.port_index      = -1;
                e.param.channel         = -1;
                e.param.key             = -1;
                e.param.value           = v;
                events.push_back(e);
                ++n;
            }
        }
        return n;
    }

    // Deliver pending param changes to the plugin when NOT processing (audio-off,
    // e.g. right before a state save). While processing they go via the process
    // event list instead; flush must not be called then.
    void flushPending() {
        if (extParams == nullptr || extParams->flush == nullptr) return;
        events.clear();
        if (appendParamChanges() > 0)
            extParams->flush(plugin, &inEventsList, &outEventsList);
    }

    void buildInputEvents(juce::MidiBuffer& midi, int nSamples) {
        juce::ignoreUnused(nSamples);
        events.clear();
        appendParamChanges();   // host parameter changes first (time 0)

        // Notes / MIDI in time order.
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            const uint32_t t = (uint32_t) juce::jmax(0, meta.samplePosition);
            if (useMidi) {
                if (m.getRawDataSize() < 1) continue;
                ClapEvent e {};
                e.midi.header.size     = sizeof(clap_event_midi_t);
                e.midi.header.time     = t;
                e.midi.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.midi.header.type     = CLAP_EVENT_MIDI;
                e.midi.header.flags    = 0;
                e.midi.port_index      = 0;
                const auto* raw = m.getRawData();
                e.midi.data[0] = raw[0];
                e.midi.data[1] = m.getRawDataSize() > 1 ? raw[1] : 0;
                e.midi.data[2] = m.getRawDataSize() > 2 ? raw[2] : 0;
                events.push_back(e);
            } else if (m.isNoteOnOrOff()) {
                ClapEvent e {};
                e.note.header.size     = sizeof(clap_event_note_t);
                e.note.header.time     = t;
                e.note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.note.header.type     = m.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
                e.note.header.flags    = 0;
                e.note.note_id         = -1;
                e.note.port_index      = 0;
                e.note.channel         = (int16_t) (m.getChannel() - 1);
                e.note.key             = (int16_t) m.getNoteNumber();
                e.note.velocity        = m.getFloatVelocity();
                events.push_back(e);
            }
        }
        std::stable_sort(events.begin(), events.end(),
                         [](const ClapEvent& a, const ClapEvent& b) {
                             return a.header.time < b.header.time;
                         });
    }

    void buildParameters() {
        if (extParams == nullptr) return;
        const uint32_t n = extParams->count(plugin);
        for (uint32_t i = 0; i < n; ++i) {
            clap_param_info_t info {};
            if (!extParams->get_info(plugin, i, &info)) continue;
            double cur = info.default_value;
            if (extParams->get_value != nullptr) extParams->get_value(plugin, info.id, &cur);
            info.default_value = cur;
            auto p = std::make_unique<ClapParameter>(info.id, info);
            clapParams.push_back(p.get());
            addHostedParameter(std::move(p));
        }
    }

    union ClapEvent {
        clap_event_header_t      header;
        clap_event_midi_t        midi;
        clap_event_note_t        note;
        clap_event_param_value_t param;
    };

    static uint32_t inSize(const clap_input_events_t* l) {
        return (uint32_t) static_cast<ClapPluginInstance*>(l->ctx)->events.size();
    }
    static const clap_event_header_t* inGet(const clap_input_events_t* l, uint32_t i) {
        return &static_cast<ClapPluginInstance*>(l->ctx)->events[i].header;
    }
    static bool outPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

    ClapModule::Ptr module;
    juce::String identifierFile, pluginName;
    clap_host_t host {};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t*      extParams = nullptr;
    const clap_plugin_state_t*       extState  = nullptr;
    const clap_plugin_audio_ports_t* extAudio  = nullptr;
    const clap_plugin_note_ports_t*  extNote   = nullptr;
    const clap_plugin_gui_t*         extGui    = nullptr;

    std::vector<ClapParameter*> clapParams;   // non-owning; JUCE owns the params
    std::vector<ClapEvent> events;
    clap_input_events_t  inEventsList  { this, &inSize, &inGet };
    clap_output_events_t outEventsList { this, &outPush };

    int  outChannels = 2, maxBlock = 512;
    bool useMidi = true, activated = false, processing = false;
    int64_t steadyTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClapPluginInstance)
};

//============================ hosted editor ==================================
// Embeds the CLAP plugin's own (win32) GUI as a child of this editor's window.
// The plugin draws itself; we just create the CLAP gui, size to it, and parent
// it to our peer HWND once we have one.
class ClapEditor : public juce::AudioProcessorEditor {
public:
    explicit ClapEditor(ClapPluginInstance& o)
        : juce::AudioProcessorEditor(o), plugin(o.rawPlugin()), gui(o.gui()) {
        // Note: no set_scale. It is optional in CLAP and some plugins
        // (clap-juce-extensions among them) crash when it is called; hosts that
        // let the OS/plugin handle DPI skip it. create -> size -> (later) parent.
        if (gui != nullptr && gui->create != nullptr
            && gui->create(plugin, CLAP_WINDOW_API_WIN32, false)) {
            created = true;
            uint32_t w = 480, h = 320;
            if (gui->get_size != nullptr) gui->get_size(plugin, &w, &h);
            // The plugin renders at scale 1.0 (we cannot call set_scale, which
            // crashes some plugins), so its window is `w x h` PHYSICAL pixels.
            // JUCE lays out in logical pixels, so divide by the display scale to
            // make the (non-resizable) window physically match the plugin. The
            // earlier blank window was the resizable window, not this divide.
            double scale = 1.0;
            if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                if (d->scale > 0.1) scale = d->scale;
            setSize(juce::jmax(120, (int) ((double) w / scale)),
                    juce::jmax(80,  (int) ((double) h / scale)));
        } else {
            setSize(360, 120);
        }
    }

    ~ClapEditor() override {
        if (created && gui != nullptr && gui->destroy != nullptr) gui->destroy(plugin);
    }

    void parentHierarchyChanged() override { tryAttach(); }
    void resized() override { tryAttach(); }

    void paint(juce::Graphics& g) override {
        // Always paint our own background so nothing behind can show through if
        // the embedded plugin window does not cover the whole area.
        g.fillAll(juce::Colour(0xff1a1a1e));
        if (!attached) {
            g.setColour(juce::Colours::grey);
            g.drawText("Loading plugin editor" + juce::String(juce::CharPointer_UTF8("\xE2\x80\xA6")),
                       getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    void tryAttach() {
        if (attached || !created) return;
        auto* peer = getPeer();
        if (peer == nullptr) return;
        clap_window win {};
        win.api   = CLAP_WINDOW_API_WIN32;
        win.win32 = peer->getNativeHandle();   // HWND
        if (gui->set_parent != nullptr && gui->set_parent(plugin, &win)) {
            if (gui->show != nullptr) gui->show(plugin);
            attached = true;
        }
    }

    const clap_plugin_t*     plugin = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    bool created = false, attached = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClapEditor)
};

juce::AudioProcessorEditor* ClapPluginInstance::createEditor() {
    return new ClapEditor(*this);
}

//============================ the format =====================================
bool ClapPluginFormat::fileMightContainThisPluginType(const juce::String& f) {
    return f.endsWithIgnoreCase(".clap") || f.containsIgnoreCase(".clap|");
}

void ClapPluginFormat::findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                                           const juce::String& fileOrIdentifier) {
    juce::String file, clapId;
    splitIdentifier(fileOrIdentifier, file, clapId);
    if (!juce::File(file).exists()) return;

    ClapModule::Ptr m = ClapModule::load(file);
    if (m == nullptr) return;

    const uint32_t n = m->factory->get_plugin_count(m->factory);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_plugin_descriptor_t* d = m->factory->get_plugin_descriptor(m->factory, i);
        if (d == nullptr) continue;
        if (clapId.isNotEmpty() && clapId != juce::String(juce::CharPointer_UTF8(d->id))) continue;
        auto* pd = new juce::PluginDescription();
        fillDescription(d, file, *pd);
        results.add(pd);
    }
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier(const juce::String& id) {
    juce::String file, clapId;
    splitIdentifier(id, file, clapId);
    return juce::File(file).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning(const juce::PluginDescription& d) {
    juce::String file, clapId;
    splitIdentifier(d.fileOrIdentifier, file, clapId);
    return juce::File(file).getLastModificationTime() != d.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist(const juce::PluginDescription& d) {
    juce::String file, clapId;
    splitIdentifier(d.fileOrIdentifier, file, clapId);
    return juce::File(file).exists();
}

juce::StringArray ClapPluginFormat::searchPathsForPlugins(const juce::FileSearchPath& paths,
                                                          bool recursive, bool) {
    juce::StringArray found;
    for (int i = 0; i < paths.getNumPaths(); ++i) {
        const juce::File dir = paths[i];
        if (!dir.isDirectory()) continue;
        for (const auto& e : juce::RangedDirectoryIterator(
                 dir, recursive, "*.clap", juce::File::findFilesAndDirectories))
            found.add(e.getFile().getFullPathName());
    }
    return found;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch() {
    juce::FileSearchPath p;
   #if JUCE_WINDOWS
    const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);
    p.add(programFiles.getChildFile("Common Files").getChildFile("CLAP"));   // C:\Program Files\Common Files\CLAP
    const auto roaming = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    p.add(roaming.getSiblingFile("Local").getChildFile("Programs")
              .getChildFile("Common").getChildFile("CLAP"));                 // %LOCALAPPDATA%\Programs\Common\CLAP
   #elif JUCE_MAC
    p.add(juce::File("~/Library/Audio/Plug-Ins/CLAP"));
    p.add(juce::File("/Library/Audio/Plug-Ins/CLAP"));
   #else
    p.add(juce::File("~/.clap"));
    p.add(juce::File("/usr/lib/clap"));
   #endif
    return p;
}

void ClapPluginFormat::createPluginInstance(const juce::PluginDescription& desc,
                                            double initialSampleRate, int initialBufferSize,
                                            PluginCreationCallback callback) {
    juce::String file, clapId;
    splitIdentifier(desc.fileOrIdentifier, file, clapId);

    ClapModule::Ptr m = ClapModule::load(file);
    if (m == nullptr) { callback(nullptr, "CLAP: could not load " + file); return; }
    if (clapId.isEmpty()) {
        const clap_plugin_descriptor_t* d = m->factory->get_plugin_descriptor(m->factory, 0);
        if (d != nullptr) clapId = juce::String(juce::CharPointer_UTF8(d->id));
    }

    auto inst = std::make_unique<ClapPluginInstance>(m, clapId, file);
    if (!inst->isValid()) { callback(nullptr, "CLAP: could not create " + desc.name); return; }
    inst->prepareToPlay(initialSampleRate, initialBufferSize);
    callback(std::move(inst), {});
}
