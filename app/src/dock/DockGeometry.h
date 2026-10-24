// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// Docking geometry: the pure, window-free math behind the instrument's dockable
// blocks (see docs/PLAN_GUI.md, Stage 1). Kept dependency-free on purpose so the
// hard invariants (magnetic snapping, group detection, and above all the
// multi-screen recovery rule "there is always a path back to a visible screen")
// are proven headlessly in reborn_dock_tests, the way every other law in this
// project is proven. The JUCE window layer (BlockWindow / DockManager) converts
// juce::Rectangle<int> to and from these plain Rects at its boundary and owns
// nothing geometric that is not here.
//
#include <vector>
#include <cstdint>

namespace reborn::dock {

// A plain integer rectangle in virtual-desktop coordinates (x grows right, y
// grows down), matching juce::Rectangle<int>'s convention so the boundary
// conversion is a field copy.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int  right()  const { return x + w; }
    int  bottom() const { return y + h; }
    int  cx()     const { return x + w / 2; }
    int  cy()     const { return y + h / 2; }
    bool valid()  const { return w > 0 && h > 0; }
    bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
};

// The offset to add to a moving block so an edge becomes flush and/or aligned
// with a neighbour. dx/dy are zero on an axis that did not snap.
struct SnapResult {
    int  dx = 0, dy = 0;
    bool snappedX = false, snappedY = false;
};

// Find the best snap for `moving` against every rect in `others`, within
// `threshold` px. The two axes snap independently: dragging a block toward the
// right edge of a neighbour snaps its left edge flush (adjacency) AND lines up
// their tops (alignment), which is what makes a tiling read as one instrument.
// A candidate on one axis is only considered when the blocks are near each other
// on the OTHER axis (their spans overlap, or are within threshold), so a block
// across the screen never drags this one sideways.
SnapResult computeSnap(const Rect& moving, const std::vector<Rect>& others,
                       int threshold);

// Group id per rect: two blocks share a group when an edge of one is flush with
// an edge of the other (within `eps`) AND they share more than a corner along
// that edge (their overlap on the shared edge is positive). Groups are the
// connected components of that relation, so dragging any member can move them
// all. Ids are small integers, dense from 0, stable only within one call.
std::vector<int> groupIds(const std::vector<Rect>& rects, int eps);

// Bring a saved block position back onto the live displays. `grabStripH` is the
// height of the block's draggable strip at its top; the invariant is that this
// strip is always reachable. If the strip already sits fully inside some
// display, `saved` is returned unchanged. Otherwise the block is moved onto the
// display it most overlaps (or, if it overlaps none, the display nearest its
// centre) and clamped fully on-screen; a block larger than that display is
// pinned top-left so its grab strip stays visible. `fallback` is used only when
// `saved` is not a usable rectangle or `displays` is empty.
Rect recoverOntoDisplays(const Rect& saved, const std::vector<Rect>& displays,
                         int grabStripH, const Rect& fallback);

} // namespace reborn::dock
