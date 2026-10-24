// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#pragma once
//
// DockManager: the policy behind the dockable blocks (docs/PLAN_GUI.md, Stage 1).
// It owns no windows; it holds BlockWindow* it was handed, wires their drag
// callbacks, and turns gestures into magnetic snapping (DockGeometry::computeSnap),
// group movement (DockGeometry::groupIds), multi-screen recovery
// (DockGeometry::recoverOntoDisplays) and layout persistence. All the geometry it
// relies on is proven headlessly in reborn_dock_tests; this class is the JUCE
// glue that could only be judged by a real launch. Header-only, all-inline.
//
#include "BlockWindow.h"
#include "DockGeometry.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <algorithm>

namespace reborn::dock {

class DockManager {
public:
    int snapThreshold = 14;                       // px, matches PLAN section 5 (~12)
    int groupEps      = 3;                        // px slop for "flush"
    int grabStripH    = BlockWindow::kStripH;     // the always-reachable strip

    // Register a block plus the slot "reset layout" returns it to. The default
    // slots together are the section-3 tiling.
    void addBlock(BlockWindow* w, Rect defaultSlot) {
        blocks_.push_back({ w, defaultSlot });
        w->onDragStart = [this](BlockWindow* b, bool solo){ beginDrag(b, solo); };
        w->onDrag      = [this](BlockWindow* b, juce::Point<int> d){ drag(b, d); };
        w->onDragEnd   = [this](BlockWindow* b){ endDrag(b); };
    }

    void showAll() {
        for (auto& e : blocks_) { e.win->setVisible(true); e.win->toFront(false); }
    }

    // Forget every registered block. The caller owns the BlockWindows and is
    // responsible for destroying them; this only drops our non-owning references
    // and any in-flight drag state, so blocks can be rebuilt (e.g. after a 303
    // voice swap) without dangling pointers.
    void clear() {
        blocks_.clear();
        dragGroup_.clear();
        dragStart_.clear();
    }

    // Snap every block to its default slot, then validate onto live displays.
    void resetLayout() {
        for (auto& e : blocks_) setWinRect(e.win, e.defaultSlot);
        validateAllOntoDisplays();
    }

    // Multi-screen recovery for every block (PLAN section 6). Idempotent, so it
    // is safe to call on launch and on any display change.
    void validateAllOntoDisplays() {
        auto disp = displays();
        for (auto& e : blocks_) {
            Rect cur = winRect(e.win);
            Rect ok  = recoverOntoDisplays(cur, disp, grabStripH, e.defaultSlot);
            if (!(ok == cur)) setWinRect(e.win, ok);
        }
        lastDisplaySig_ = displaySignature();
    }

    // Persistence: absolute virtual-desktop coords per block, keyed by title.
    void saveLayout(juce::PropertiesFile& props) const {
        for (auto& e : blocks_) {
            Rect r = winRect(e.win);
            props.setValue(key(e.win),
                juce::String(r.x) + "," + juce::String(r.y) + "," +
                juce::String(r.w) + "," + juce::String(r.h));
        }
        props.saveIfNeeded();
    }

    // Restore stored positions (falling back to default slots), then validate.
    // Returns true if any block had a stored position.
    bool loadLayout(juce::PropertiesFile& props) {
        bool any = false;
        for (auto& e : blocks_) {
            Rect r;
            if (parseRect(props.getValue(key(e.win), {}), r) && r.valid()) {
                setWinRect(e.win, r); any = true;
            } else {
                setWinRect(e.win, e.defaultSlot);
            }
        }
        validateAllOntoDisplays();
        return any;
    }

    // Cheap poll: true once each time the display configuration changes. A
    // portable stand-in for a native displaysChanged hook; call from a timer.
    bool displayConfigChanged() {
        auto sig = displaySignature();
        if (sig != lastDisplaySig_) { lastDisplaySig_ = sig; return true; }
        return false;
    }

private:
    struct Entry { BlockWindow* win = nullptr; Rect defaultSlot; };
    std::vector<Entry> blocks_;

    // Drag state: the group being dragged (indices) and each member's start rect.
    std::vector<int>  dragGroup_;
    std::vector<Rect> dragStart_;
    juce::String      lastDisplaySig_;

    static juce::String key(BlockWindow* w) {
        return "block_" + w->blockTitle().replaceCharacter(' ', '_');
    }
    static bool parseRect(const juce::String& s, Rect& out) {
        auto t = juce::StringArray::fromTokens(s, ",", "");
        if (t.size() != 4) return false;
        out = { t[0].getIntValue(), t[1].getIntValue(),
                t[2].getIntValue(), t[3].getIntValue() };
        return true;
    }

    int indexOf(BlockWindow* w) const {
        for (int i = 0; i < (int) blocks_.size(); ++i)
            if (blocks_[i].win == w) return i;
        return -1;
    }

    static Rect toRect(juce::Rectangle<int> r) {
        return { r.getX(), r.getY(), r.getWidth(), r.getHeight() };
    }
    static juce::Rectangle<int> toJuce(Rect r) { return { r.x, r.y, r.w, r.h }; }
    Rect winRect(BlockWindow* w) const { return toRect(w->getScreenBounds()); }
    void setWinRect(BlockWindow* w, Rect r) { w->setBounds(toJuce(r)); }

    std::vector<Rect> currentRects() const {
        std::vector<Rect> v; v.reserve(blocks_.size());
        for (auto& e : blocks_) v.push_back(winRect(e.win));
        return v;
    }
    std::vector<Rect> displays() const {
        std::vector<Rect> v;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays)
            v.push_back(toRect(d.userArea));
        return v;
    }
    juce::String displaySignature() const {
        juce::String s;
        for (auto& d : juce::Desktop::getInstance().getDisplays().displays) {
            auto a = d.userArea;
            s << a.getX() << ":" << a.getY() << ":"
              << a.getWidth() << ":" << a.getHeight() << ";";
        }
        return s;
    }

    void beginDrag(BlockWindow* w, bool soloDrag) {
        int idx = indexOf(w);
        if (idx < 0) return;
        dragGroup_.clear();
        dragStart_.clear();
        auto rects = currentRects();

        if (soloDrag) {
            // Alt-drag: tear this one block out of its group.
            dragGroup_.push_back(idx);
            dragStart_.push_back(rects[idx]);
            return;
        }

        // Normal drag: move the whole connected group.
        auto gid = groupIds(rects, groupEps);
        int myGroup = gid[idx];
        for (int i = 0; i < (int) blocks_.size(); ++i)
            if (gid[i] == myGroup) { dragGroup_.push_back(i); dragStart_.push_back(rects[i]); }
        // The dragged block must be first: its edges drive the group's snap.
        for (size_t k = 0; k < dragGroup_.size(); ++k)
            if (dragGroup_[k] == idx) {
                std::swap(dragGroup_[0], dragGroup_[k]);
                std::swap(dragStart_[0], dragStart_[k]);
                break;
            }
    }

    void drag(BlockWindow* w, juce::Point<int> delta) {
        (void) w;
        if (dragGroup_.empty()) return;

        // Everything not in the dragged group is a snap target.
        std::vector<bool> inGroup(blocks_.size(), false);
        for (int gi : dragGroup_) inGroup[(size_t) gi] = true;
        auto cur = currentRects();
        std::vector<Rect> others;
        for (int i = 0; i < (int) blocks_.size(); ++i)
            if (!inGroup[(size_t) i]) others.push_back(cur[i]);

        // Snap the dragged block (first in the group), apply the same offset to
        // every member so the group stays rigid.
        Rect start = dragStart_[0];
        Rect proposed { start.x + delta.x, start.y + delta.y, start.w, start.h };
        SnapResult snap = computeSnap(proposed, others, snapThreshold);
        int dx = delta.x + snap.dx;
        int dy = delta.y + snap.dy;
        for (size_t k = 0; k < dragGroup_.size(); ++k) {
            Rect s = dragStart_[k];
            setWinRect(blocks_[(size_t) dragGroup_[k]].win, { s.x + dx, s.y + dy, s.w, s.h });
        }
    }

    void endDrag(BlockWindow*) {
        dragGroup_.clear();
        dragStart_.clear();
        validateAllOntoDisplays();   // never leave a block off-screen after a drag
    }
};

} // namespace reborn::dock
