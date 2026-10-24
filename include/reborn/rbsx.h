// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// `.rbsx`: Reborn's native session format. See docs/FORMAT_RBSX.md.
//
// A session is everything Reborn knows: the song itself, plus the environment
// it was played in (which sample set, which mod) and eventually hosted effect
// chains. `.rbs` remains a first-class but LOSSY export.
//
// The song is stored as a byte-for-byte `.rbs` image in the RSNG chunk, so
// exporting `.rbs` is chunk EXTRACTION rather than re-serialisation: the
// interop path cannot drift away from the loader, and every `.rbsx` provably
// contains a file the original program can open.
//
#include <string>
#include <vector>
#include "reborn/pattern.h"

namespace reborn {

// Everything a session carries beyond the song. (Effect chains land here when
// hosting arrives; see docs/PLUGINS.md.)
struct SessionEnv {
    std::string sampleDir;   // canonical-WAV sample-set directory ("" = none)
    std::string modName;     // mod the song asks for, if any
    std::string modPath;     // resolved .rbm path ("" = unresolved/none)
};

// Which voice fills one 303 slot, so a session restores the rig and not just the
// notes. `engine` is a Bass303Engine id (0 = native TPT, 1 = Open303, 2 = hosted
// plugin); -1 means "not recorded" and the loader applies the default. For a
// hosted plugin, `pluginId` is the host's identifier string (to find it again)
// and `pluginState` is its opaque getStateInformation blob. The core only stores
// these bytes; producing/consuming them is the app's job (it needs JUCE). A
// missing plugin still opens: the record is preserved so a re-save round-trips.
struct InstrumentSlot {
    int         engine = -1;
    std::string pluginId;
    std::vector<unsigned char> pluginState;
};

struct Session {
    Song           song;
    SessionEnv     env;
    InstrumentSlot bass[2];   // 303 slot 0 (kBass1) and 1 (kBass2)
    int            version = 1;
};

constexpr int kRbsxVersion = 1;

bool loadRbsx(const std::string& path, Session& out, std::string* err = nullptr);
bool saveRbsx(const std::string& path, const Session& s, std::string* err = nullptr);

// The same session serialisation, to/from a memory buffer instead of a file.
// Used for the plugin's DAW state (getStateInformation / setStateInformation),
// so a host project persists the full rig: song, environment and 303 slots.
bool saveRbsxToMemory(std::vector<unsigned char>& out, const Session& s,
                      std::string* err = nullptr);
bool loadRbsxFromMemory(const unsigned char* data, std::size_t n, Session& out,
                        std::string* err = nullptr);

// Pull the embedded .rbs image straight out of a session file, so "export as
// .rbs" never goes through the writer a second time.
bool extractRbsImage(const std::string& rbsxPath, std::vector<unsigned char>& out,
                     std::string* err = nullptr);

} // namespace reborn
