// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostedInstrument.h"   // capture/restore a hosted plugin's state (fork P6)
#include "reborn/rbs.h"
#include "reborn/rbsx.h"
#include "reborn/factories.h"
#include "reborn/sampleset.h"
#include "reborn/rbm.h"

using namespace reborn;

// Build the same built-in acid demo the headless renderer uses, so the app
// makes sound the moment it opens.
static Song makeDemoSong() {
    Song song;
    song.title = "Reborn Demo";
    song.tempo = 130.0;
    BassPattern& b = song.bass1.patterns[0];
    b.length = 16; b.octaveBase = 3;
    auto set = [&](int i,int n,int o,bool a,bool s){ b.steps[i]={true,n,o,a,s}; };
    set(0,9,0,true,false); set(1,9,0,false,true); set(2,9,1,false,false); set(3,0,1,false,true);
    set(4,9,0,true,false); set(6,7,0,false,true); set(7,9,0,false,false); set(8,9,0,true,false);
    set(9,9,1,false,true); set(10,12,0,false,false); set(11,9,0,false,false); set(12,5,0,true,false);
    set(14,9,0,false,true); set(15,0,1,false,false);
    DrumPattern& d = song.drum808.patterns[0];
    d.length = 16;
    for (int s : {0,4,8,12}) d.steps[s].setOn(0,true);
    for (int s : {4,12})     d.steps[s].setOn(1,true);
    for (int s : {0,2,4,6,8,10,12,14}) d.steps[s].setOn(14,true);
    d.steps[14].setOn(13,true);
    return song;
}

RebornAudioProcessor::RebornAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    loadDemoSong();
}

void RebornAudioProcessor::loadDemoSong() {
    juce::SpinLock::ScopedLockType lock(engineLock_);
    Song s = makeDemoSong();
    engine_.loadSong(s);
    engine_.channel(kBass1).distEnable = true;
    if (engine_.distortion()) { engine_.distortion()->setParamByName("amount",0.35f); engine_.distortion()->setParamByName("tone",0.6f); }
    songTitle = s.title; songAuthor = "Reborn";
    engine_.play();
}

void RebornAudioProcessor::loadSongFile(const juce::File& file) {
    Song s; std::string err;
    if (loadRbsFile(file.getFullPathName().toStdString(), s, &err)) {
        // Find and read the song's mod before taking the lock: the first read of a mod
        // unscrambles and self-checks it, a tenth of a second, or seconds when its keys
        // must be estimated, and the audio thread's try-lock would render silence for
        // all of it.
        std::string found, modErr;
        std::vector<reborn::ModSample> modVoices;
        if (modsDir.isNotEmpty() && !s.skinName.empty()) {
            found = reborn::findModByName({ modsDir.toStdString() }, s.skinName);
            if (!found.empty()) modVoices = reborn::readModSamples(found, &modErr);
        }
        juce::SpinLock::ScopedLockType lock(engineLock_);
        engine_.loadSong(s);
        songTitle = s.title.empty() ? file.getFileNameWithoutExtension().toStdString() : s.title;
        songAuthor = s.author;
        // A song NAMES the mod it was authored against (GLOB+15) and was
        // composed with that mod's drum samples, so prefer it when we can
        // resolve it against the user's own mods folder.
        pendingModNote = juce::String();
        bool usedMod = false;
        if (modsDir.isNotEmpty() && !s.skinName.empty()) {
            if (!found.empty()) {
                const int n = reborn::installModSamples(engine_, modVoices);
                if (n > 0) {
                    sampleVoices = n;
                    modName = juce::File(found).getFileNameWithoutExtension();
                    modPath = juce::String(found);   // so a session can reopen it
                    usedMod = true;
                } else {
                    // Found but unreadable, for instance a mod that will not unscramble:
                    // say so rather than fall back to other drums in silence.
                    pendingModNote = "mod unreadable: "
                                   + juce::String(modErr.empty() ? "no usable voices" : modErr);
                }
            } else if (juce::String(s.skinName).containsIgnoreCase("standard") == false) {
                pendingModNote = "needs mod: " + juce::String(s.skinName);
            }
        }
        // Otherwise reapply the sample set for the new song's knob variants.
        if (!usedMod && sampleDir.isNotEmpty()) {
            sampleVoices = reborn::loadSampleSet(engine_, sampleDir.toStdString());
            modName = juce::String();
            modPath = juce::String();
        }
        engine_.play();
    }
}

void RebornAudioProcessor::setBassEngine(int channelId, reborn::Bass303Engine engine) {
    if (channelId != reborn::kBass1 && channelId != reborn::kBass2) return;
    // Build the new voice OUTSIDE the lock: native engines are cheap, and a
    // future hosted voice does its slow construction here too (fork P5).
    auto voice = reborn::makeBass303(engine);
    if (!voice) return;   // HostedPlugin has no core factory; installed elsewhere.
    std::unique_ptr<reborn::Instrument> displaced;
    {
        // The swap is a cheap pointer move; hold the same lock processBlock
        // try-locks so the audio thread never sees a half-installed voice.
        juce::SpinLock::ScopedLockType lock(engineLock_);
        displaced = engine_.setInstrument(channelId, std::move(voice));
    }
    displaced.reset();    // free the old voice off the lock (teardown can be slow)
    const int idx = channelId == reborn::kBass2 ? 1 : 0;
    bassEngine_[idx] = engine;
    bassPluginName_[idx] = juce::String();   // a native engine, no hosted plugin
    bassPluginId_[idx]   = juce::String();
    bassPending_[idx]    = PendingSlot{};     // user overrode any missing-plugin slot
    voiceRevision_.fetch_add(1, std::memory_order_relaxed);   // editor: rebuild knobs
}

void RebornAudioProcessor::installBassPlugin(int channelId,
                                             std::unique_ptr<reborn::Instrument> voice,
                                             const juce::String& name,
                                             const juce::String& identifier) {
    if (channelId != reborn::kBass1 && channelId != reborn::kBass2) return;
    if (!voice) return;
    std::unique_ptr<reborn::Instrument> displaced;
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        displaced = engine_.setInstrument(channelId, std::move(voice));
    }
    displaced.reset();    // free the old voice on the message thread, off the lock
    const int idx = channelId == reborn::kBass2 ? 1 : 0;
    bassEngine_[idx] = reborn::Bass303Engine::HostedPlugin;
    bassPluginName_[idx] = name;
    bassPluginId_[idx]   = identifier;
    bassPending_[idx]    = PendingSlot{};     // a real plugin is now loaded here
    voiceRevision_.fetch_add(1, std::memory_order_relaxed);   // editor: rebuild knobs
}

int RebornAudioProcessor::loadSampleDir(const juce::File& dir) {
    juce::SpinLock::ScopedLockType lock(engineLock_);
    sampleVoices = reborn::loadSampleSet(engine_, dir.getFullPathName().toStdString());
    sampleDir = sampleVoices > 0 ? dir.getFullPathName() : juce::String();
    return sampleVoices;
}

int RebornAudioProcessor::loadModFile(const juce::File& file) {
    // Read (and on first sight unscramble, a tenth of a second or more) off the lock; install under it.
    std::string err;
    const std::vector<reborn::ModSample> voices =
        reborn::readModSamples(file.getFullPathName().toStdString(), &err);
    juce::SpinLock::ScopedLockType lock(engineLock_);
    const int n = reborn::installModSamples(engine_, voices);
    if (n > 0) {
        sampleVoices = n;
        sampleDir = juce::String();      // voices now come from the mod, not a dir
        modName = file.getFileNameWithoutExtension();
        modPath = file.getFullPathName();
        modsDir = file.getParentDirectory().getFullPathName();
        pendingModNote = juce::String();  // an earlier refusal or "needs mod" no longer applies
    } else {
        pendingModNote = "mod unreadable: " + juce::String(err.empty() ? "no usable voices" : err);
    }
    return n;
}

bool RebornAudioProcessor::saveSongFile(const juce::File& file, juce::String& err) {
    reborn::Song s;
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        // The engine owns the live song, including any edits made in the grid,
        // and the live panel (mixer, routing, effect and voice knobs), which
        // the GUI edits in the engine rather than the song (review W3). Both go
        // into a copy under the lock; the disk write happens after it, so
        // playback does not stall on it (review L2).
        s = engine_.songForSave(songTitle.toStdString(), songAuthor.toStdString());
    }
    std::string e;
    if (!reborn::saveRbsFile(file.getFullPathName().toStdString(), s, &e)) {
        err = juce::String(e);
        return false;
    }
    return true;
}

bool RebornAudioProcessor::saveSessionFile(const juce::File& file, juce::String& err) {
    reborn::Session sess;
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        sess = captureSession();
    }
    std::string e;
    if (!reborn::saveRbsx(file.getFullPathName().toStdString(), sess, &e)) {
        err = juce::String(e);
        return false;
    }
    return true;
}

bool RebornAudioProcessor::openAnyFile(const juce::File& file, juce::String& err) {
    if (!file.existsAsFile()) { err = "file does not exist"; return false; }

    if (!file.hasFileExtension("rbsx")) {   // plain song: existing path
        loadSongFile(file);
        return true;
    }

    reborn::Session sess;
    std::string e;
    if (!reborn::loadRbsx(file.getFullPathName().toStdString(), sess, &e)) {
        err = juce::String(e);
        return false;
    }
    restoreSession(sess, file.getFileNameWithoutExtension());
    return true;
}

// Restore a whole session: the song and its environment under the lock, then
// the 303 voice slots after releasing it (the swap helpers lock themselves),
// and only then play(). Playing first ran the song's tick-0 dump and step 0 on
// the voices about to be replaced, so the new ones started on the header knobs
// and missed step 0, and a song restored from a session or a DAW project
// rendered differently from the same song opened as a file (all 16 demo songs,
// 90 s on the TPT voice; the fixed order matches the file open on all 16).
void RebornAudioProcessor::restoreSession(const reborn::Session& sess,
                                          const juce::String& fallbackTitle) {
    // Read the session's mod before anything else and off the lock: the first read
    // of a mod unscrambles and self-checks it, a tenth of a second, or seconds when
    // its keys must be estimated, and processBlock would render silence for all of it.
    std::vector<reborn::ModSample> modVoices;
    std::string modErr;
    const bool modOnDisk = !sess.env.modPath.empty()
                        && juce::File(juce::String(sess.env.modPath)).existsAsFile();
    if (modOnDisk) modVoices = reborn::readModSamples(sess.env.modPath, &modErr);
    // Until play() below, the engine holds the new song at the old song's
    // position, so processBlock renders silence rather than run it. The guard
    // clears the flag on every way out, since a flag left set would silence
    // processBlock until the next restore. The guard only clears the flag: a
    // hosted plugin's throw is handled per slot in applyInstrumentSlots, and
    // setStateInformation catches anything else (an allocation failing in
    // loadSong, say) so that nothing escapes into a host.
    struct ClearOnExit {
        std::atomic<bool>& flag;
        ~ClearOnExit() { flag.store(false, std::memory_order_release); }
    } restoringGuard { restoring_ };
    restoring_.store(true, std::memory_order_release);
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        engine_.loadSong(sess.song);
        songTitle = sess.song.title.empty()
                  ? fallbackTitle : juce::String(sess.song.title);
        songAuthor = sess.song.author;

        // Restore the ENVIRONMENT, which is the whole point of the format:
        // without this a reopened session would fall back to synthesis and
        // sound different from the one that was saved.
        pendingModNote = juce::String();
        sampleVoices = 0;
        modName = juce::String(sess.env.modName);
        modPath = juce::String(sess.env.modPath);
        sampleDir = juce::String(sess.env.sampleDir);

        bool restored = false;
        if (!modVoices.empty()) {
            const int n = reborn::installModSamples(engine_, modVoices);
            if (n > 0) { sampleVoices = n; restored = true; }
        }
        if (!restored && sampleDir.isNotEmpty() && juce::File(sampleDir).isDirectory()) {
            const int n = reborn::loadSampleSet(engine_, sampleDir.toStdString());
            if (n > 0) { sampleVoices = n; restored = true; }
        }
        // Say so rather than silently sounding different. A session saved on
        // another machine, or after the mod moved, lands here. A mod that is on
        // disk but cannot be read (it will not unscramble) keeps its name and
        // path, so saving the session again still names it.
        if (!restored && modOnDisk) {
            pendingModNote = "mod unreadable: "
                           + juce::String(modErr.empty() ? "no usable voices" : modErr);
        } else if (!restored && (modName.isNotEmpty() || sampleDir.isNotEmpty())) {
            pendingModNote = "session audio missing: "
                           + (modName.isNotEmpty() ? modName : sampleDir);
            modName = juce::String();
            modPath = juce::String();
            sampleDir = juce::String();
        }
    }
    // A hosted plugin that throws is handled per slot inside
    // applyInstrumentSlots (default voice, record kept as pending). This catch
    // is the backstop for anything else it throws, and the restore then
    // finishes exactly as the normal one does: play() under the lock, then the
    // knob rebuild. play() rather than stop(), because it leaves the engine
    // where a clean restore leaves it, the new song at bar 1, and
    // processBlock's transport logic then starts or stops it as after any
    // restore. stop() would leave the new song at the old song's position until
    // the next play(), the state restoring_ exists to keep out of processBlock.
    // Not rethrown: this runs inside a host's setStateInformation.
    try {
        applyInstrumentSlots(sess);
    } catch (...) {
        // Nothing to undo: every swap applyInstrumentSlots made is complete,
        // and one that threw never reached the engine.
    }
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        engine_.play();
    }
    restoring_.store(false, std::memory_order_release);
    // Rebuild the editor's knob panels only now, after play() has dispatched
    // the tick-0 dump, so they show the song's start rather than the header
    // values loadSong set (Calm's first 303 header says cutoff 120, its
    // start 0).
    // Still in the same message-thread call as the voice swaps above, with
    // nothing in between that lets the editor run, so no knob stays bound to
    // a freed voice, on the throw path as well.
    if (onVoicesChanged_) onVoicesChanged_();
}

// Capture the live rig into a Session. Caller holds engineLock_.
reborn::Session RebornAudioProcessor::captureSession() {
    reborn::Session sess;
    // The live panel as well as the song: see saveSongFile (review W3).
    sess.song = engine_.songForSave(songTitle.toStdString(), songAuthor.toStdString());
    sess.env.sampleDir = sampleDir.toStdString();
    sess.env.modName   = modName.toStdString();
    sess.env.modPath   = modPath.toStdString();
    for (int i = 0; i < 2; ++i) {
        const int ch = (i == 0) ? reborn::kBass1 : reborn::kBass2;
        reborn::InstrumentSlot& slot = sess.bass[i];
        if (bassPending_[i].has) {
            // A plugin that could not be reloaded last time: re-emit it verbatim
            // so the record is never dropped from the session.
            slot.engine   = (int) reborn::Bass303Engine::HostedPlugin;
            slot.pluginId = bassPending_[i].pluginId.toStdString();
            const auto* d = (const unsigned char*) bassPending_[i].state.getData();
            slot.pluginState.assign(d, d + bassPending_[i].state.getSize());
            continue;
        }
        slot.engine = (int) bassEngine_[i];
        if (bassEngine_[i] == reborn::Bass303Engine::HostedPlugin) {
            slot.pluginId = bassPluginId_[i].toStdString();
            if (auto* h = dynamic_cast<HostedInstrument*>(engine_.instrument(ch)))
                if (auto* p = h->plugin()) {
                    juce::MemoryBlock mb;
                    p->getStateInformation(mb);
                    const auto* d = (const unsigned char*) mb.getData();
                    slot.pluginState.assign(d, d + mb.getSize());
                }
        }
    }
    return sess;
}

// Install the per-slot voices a session records. Caller must NOT hold the lock.
void RebornAudioProcessor::applyInstrumentSlots(const reborn::Session& sess) {
    // A restore is about to free and replace the 303 voices. If the editor is
    // open, let it close any hosted-plugin editor windows FIRST, so a CLAP/VST3
    // UI is destroyed while its plugin is still alive (correct teardown order) and
    // no window is left bound to a freed instance. Runs on the message thread,
    // where restores (openAnyFile and setStateInformation) run.
    if (onVoicesWillChange_) onVoicesWillChange_();
    for (int i = 0; i < 2; ++i) {
        const int ch = (i == 0) ? reborn::kBass1 : reborn::kBass2;
        const reborn::InstrumentSlot& sl = sess.bass[i];
        try {
            applyInstrumentSlot(i, ch, sl);
        } catch (...) {
            // A hosted plugin threw from create, setStateInformation or its
            // install. Treat it like a missing plugin: the default voice plays,
            // and the recorded slot is kept as pending so a later save does not
            // drop it. Without this a throw on slot 0 skipped slot 1, which then
            // kept the PREVIOUS session's voice and identity and wrote them into
            // this session's next save.
            setBassEngine(ch, reborn::kDefaultBass303Engine);
            if (sl.engine == (int) reborn::Bass303Engine::HostedPlugin && !sl.pluginId.empty()) {
                PendingSlot pend;
                pend.has = true;
                pend.pluginId = juce::String(sl.pluginId);
                if (!sl.pluginState.empty())
                    pend.state.append(sl.pluginState.data(), sl.pluginState.size());
                bassPending_[i]    = std::move(pend);
                bassPluginName_[i] = "(plugin failed to load)";
            }
        }
    }
    // Voices are now swapped in. restoreSession rebuilds the editor's knob
    // panels after play(), in this same message-thread call, so no knob is
    // left bound to a freed voice for even the up-to-a-tick gap the
    // revision-counter poll would otherwise leave.
}

// One 303 slot of a session. May throw (a hosted plugin); applyInstrumentSlots
// catches per slot so one failing plugin cannot leave the other slot stale.
void RebornAudioProcessor::applyInstrumentSlot(int i, int ch, const reborn::InstrumentSlot& sl) {
    {
        if (sl.engine < 0) {                        // unrecorded: apply the default
            setBassEngine(ch, reborn::kDefaultBass303Engine);
            return;
        }
        if (sl.engine == (int) reborn::Bass303Engine::HostedPlugin && !sl.pluginId.empty()) {
            const juce::String id(sl.pluginId);
            if (auto desc = pluginHost_.knownList().getTypeForIdentifierString(id)) {
                // Never self-host, even from a session: the editor's loader refuses
                // this, but a foreign or hand-edited session could still name our
                // own plugin. Drop to the default voice rather than instantiate a
                // nested Reborn 338 as a 303 voice (the footgun the UI guard exists
                // to prevent). See PluginEditor::loadHostedPlugin.
                if (desc->name.startsWithIgnoreCase("Reborn 338")) {
                    setBassEngine(ch, reborn::kDefaultBass303Engine);
                    return;
                }
                juce::String perr;
                auto inst = pluginHost_.create(*desc, engine_.sampleRate(), 512, perr);
                if (inst) {
                    if (!sl.pluginState.empty())
                        inst->setStateInformation(sl.pluginState.data(),
                                                  (int) sl.pluginState.size());
                    const juce::String nm = desc->name;
                    installBassPlugin(ch,
                        std::make_unique<HostedInstrument>(std::move(inst), nm), nm, id);
                    return;
                }
            }
            // Missing or failed: fall back to the default voice but PRESERVE the
            // record so a re-save round-trips it (the "missing plugin still
            // opens" rule). setBassEngine clears pending, so set it afterwards.
            setBassEngine(ch, reborn::kDefaultBass303Engine);
            PendingSlot pend;
            pend.has = true;
            pend.pluginId = id;
            if (!sl.pluginState.empty())
                pend.state.append(sl.pluginState.data(), sl.pluginState.size());
            bassPending_[i]    = std::move(pend);
            bassPluginName_[i] = "(missing plugin)";
            return;
        }
        setBassEngine(ch, (reborn::Bass303Engine) sl.engine);   // a native engine
    }
}

void RebornAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    reborn::Session sess;
    {
        juce::SpinLock::ScopedLockType lock(engineLock_);
        sess = captureSession();
    }
    std::vector<unsigned char> buf;
    if (reborn::saveRbsxToMemory(buf, sess))
        dest.append(buf.data(), buf.size());
}

void RebornAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (data == nullptr || sizeInBytes <= 0) return;
    // Nothing may escape into the host from here. A throw from parsing, from
    // loadSong or a sample loader inside restoreSession, is dropped: the lock is
    // released by its scope, restoring_ by its guard, and the engine keeps
    // whatever it had, which is no worse than a state the host could not load.
    try {
        reborn::Session sess;
        if (!reborn::loadRbsxFromMemory((const unsigned char*) data,
                                        (std::size_t) sizeInBytes, sess, nullptr))
            return;
        restoreSession(sess, "Session");
    } catch (...) {
    }
}

void RebornAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::SpinLock::ScopedLockType lock(engineLock_);
    engine_.prepare(sampleRate);
    scratch_.resize((std::size_t) juce::jmax(1, samplesPerBlock));
    engine_.play();
}

void RebornAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = buffer.getNumChannels(); --ch >= 0;)
        buffer.clear(ch, 0, numSamples);

    juce::SpinLock::ScopedTryLockType lock(engineLock_);
    if (!lock.isLocked()) return;                       // editor is mutating; skip a block
    // A session restore is mid-way. Read under the lock: restoreSession sets
    // the flag before it takes the lock to load the song, so once we hold the
    // lock after that load we see it set. Read before the lock, it could be a
    // stale false and the new song would run from the old one's position.
    if (restoring_.load(std::memory_order_acquire)) return;

    // Host-transport sync (replaces the original ReWire link). If a host exposes
    // tempo / play state, follow it; standalone has none, so we free-run.
    bool haveHost = false;
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            haveHost = true;
            if (auto bpm = pos->getBpm(); bpm && *bpm > 1.0) engine_.setTempo(*bpm);
            if (followHostTransport) {
                const bool hostPlaying = pos->getIsPlaying();
                if (hostPlaying && !engine_.isPlaying())      engine_.play();
                else if (!hostPlaying && engine_.isPlaying()) engine_.stop();
            }
        }
    }
    // Standalone (no host transport): follow the user's Play/Stop intent.
    // This used to unconditionally re-play every block, which made the Stop
    // button a no-op outside a host.
    if (!haveHost) {
        if (transportRunning && !engine_.isPlaying())      engine_.play();
        else if (!transportRunning && engine_.isPlaying()) engine_.stop();
    }

    if ((int) scratch_.size() < numSamples) scratch_.resize((std::size_t) numSamples);
    engine_.process(scratch_, numSamples);

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i) {
        L[i] = scratch_.left[(std::size_t) i];
        if (R) R[i] = scratch_.right[(std::size_t) i];
    }
}

juce::AudioProcessorEditor* RebornAudioProcessor::createEditor() {
    return new RebornAudioProcessorEditor(*this);
}

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new RebornAudioProcessor();
}
