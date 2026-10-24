// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Clean-room loader for the original .rbs song format (EA-IFF-85 chunked, big
// endian, form type "RB40"). Reconstructed purely by inspecting the data files;
// no original code is used. See docs/FORMAT_RBS.md.
//
#include <string>
#include <vector>
#include "reborn/pattern.h"

namespace reborn {

// Parse an .rbs file into a Song. Returns false and sets *err on failure.
bool loadRbsFile(const std::string& path, Song& out, std::string* err = nullptr);

// Parse from an in-memory buffer.
bool loadRbs(const unsigned char* data, size_t size, Song& out, std::string* err = nullptr);

// ---------------------------------------------------------------------------
// Writing. Saving .rbs is a first-class but LOSSY export: the 1998 format has
// no field for hosted plugin chains and its pattern records hold exactly 16
// steps. See docs/FORMAT_RBSX.md for the native session format.
// ---------------------------------------------------------------------------

// What a given Song would lose on the way out, so the UI can warn BEFORE
// writing (and stay quiet when the save really is lossless).
struct RbsLoss {
    int  overLongPatterns = 0;   // patterns longer than 16 steps, truncated
    int  pluginChains     = 0;   // hosted FX chains dropped (set by the caller)
    bool lossless         = true;
};
RbsLoss rbsLossFor(const Song& s);

bool saveRbs(const Song& s, std::vector<unsigned char>& out, std::string* err = nullptr);
bool saveRbsFile(const std::string& path, const Song& s, std::string* err = nullptr);

// The skin / mod name at GLOB+15, folded to ASCII exactly as the loader reads
// it into Song::skinName ("" when the payload is too short to hold one). The
// writer uses it to tell an edited name from the one Song::globRaw carries.
std::string skinNameFromGlob(const std::vector<uint8_t>& glob);

// --- 303 TUNE ---------------------------------------------------------------
//
// TUNE is a 32-SLOT LOOKUP TABLE indexed by (byte >> 2), not a continuous law.
// Measured 2026-07-27 by exporting all 128 knob values from ReBirth (batch G
// tune sweep) and reading the dominant partial of a held note. Every step lands
// within 0.05 semitones of an exact integer:
//
//     byte   0.. 7  -> -12          (8 wide)
//     byte   8..51  -> -11 .. -1    (exactly 4 wide each)
//     byte  52..75  ->   0          (a 24-wide CENTRE DETENT)
//     byte  76..119 ->  +1 .. +11   (4 wide each)
//     byte 120..127 -> +12          (8 wide)
//
// The previous law (12/64 below centre, 12/63 above) was fitted from the two
// EXTREME fixtures only, and both of those sit inside the 8-wide end plateaus
// where a continuous law happens to agree. It is wrong by up to 1.38 semitones
// in the middle of the range and mistunes 7 of the 16 shipped songs, Calm and
// Groove It Up worst. Two points cannot tell one law from another.
inline int tuneSemitonesFromByte(int b) {
    const int slot = (b < 0 ? 0 : (b > 127 ? 127 : b)) >> 2;
    if (slot <= 1)  return -12;
    if (slot <= 12) return slot - 13;
    if (slot <= 18) return 0;
    if (slot <= 29) return slot - 18;
    return 12;
}

// Inverse, picking the canonical byte for each semitone: the start of its slot
// range, except that 0 maps to 64 (the natural centre inside the wide detent)
// and the extremes map to 0 and 127. Round-trips through the decoder above.
inline int tuneByteFromSemitones(int s) {
    if (s <= -12) return 0;
    if (s >= 12)  return 127;
    if (s == 0)   return 64;
    return (s < 0 ? (s + 13) : (s + 18)) * 4;
}

} // namespace reborn
