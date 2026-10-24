// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand

#include "DockGeometry.h"
#include <algorithm>
#include <cstdlib>
#include <functional>

namespace reborn::dock {

namespace {

// Do two closed 1-D spans [a0,a1] and [b0,b1] come within `slack` of touching?
// Used as the perpendicular-axis gate: an x-snap is only offered when the blocks
// are vertically near, and vice versa.
bool spansNear(int a0, int a1, int b0, int b1, int slack) {
    return a0 <= b1 + slack && b0 <= a1 + slack;
}

// Length of the overlap of two closed 1-D spans (0 if they only touch or miss).
int spanOverlap(int a0, int a1, int b0, int b1) {
    return std::max(0, std::min(a1, b1) - std::max(a0, b0));
}

// Consider one snap candidate: if |delta| is within threshold and better than
// what we have, take it.
void consider(int delta, int threshold, int& best, bool& got) {
    if (std::abs(delta) <= threshold && (!got || std::abs(delta) < std::abs(best))) {
        best = delta;
        got  = true;
    }
}

} // namespace

SnapResult computeSnap(const Rect& moving, const std::vector<Rect>& others,
                       int threshold) {
    SnapResult r;
    if (!moving.valid() || threshold < 0) return r;

    int bestDx = 0, bestDy = 0;
    bool gotX = false, gotY = false;

    for (const auto& o : others) {
        if (!o.valid()) continue;

        // X snaps need the blocks to be vertically near.
        if (spansNear(moving.y, moving.bottom(), o.y, o.bottom(), threshold)) {
            consider(o.right() - moving.x,     threshold, bestDx, gotX); // left  -> right
            consider(o.x     - moving.right(), threshold, bestDx, gotX); // right -> left
            consider(o.x     - moving.x,       threshold, bestDx, gotX); // lefts align
            consider(o.right() - moving.right(), threshold, bestDx, gotX); // rights align
        }
        // Y snaps need the blocks to be horizontally near.
        if (spansNear(moving.x, moving.right(), o.x, o.right(), threshold)) {
            consider(o.bottom() - moving.y,      threshold, bestDy, gotY); // top -> bottom
            consider(o.y      - moving.bottom(), threshold, bestDy, gotY); // bottom -> top
            consider(o.y      - moving.y,        threshold, bestDy, gotY); // tops align
            consider(o.bottom() - moving.bottom(), threshold, bestDy, gotY); // bottoms align
        }
    }

    r.dx = gotX ? bestDx : 0;  r.snappedX = gotX;
    r.dy = gotY ? bestDy : 0;  r.snappedY = gotY;
    return r;
}

std::vector<int> groupIds(const std::vector<Rect>& rects, int eps) {
    const int n = (int) rects.size();
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;

    // Union-find.
    std::function<int(int)> find = [&](int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    auto unite = [&](int a, int b) { parent[find(a)] = find(b); };

    auto flush = [&](int a, int b) {
        return std::abs(a - b) <= eps;
    };

    for (int i = 0; i < n; ++i) {
        if (!rects[i].valid()) continue;
        for (int j = i + 1; j < n; ++j) {
            if (!rects[j].valid()) continue;
            const Rect& A = rects[i];
            const Rect& B = rects[j];

            // Vertical shared edge (A|B side by side): an edge flush, positive
            // vertical overlap.
            bool vEdge = (flush(A.right(), B.x) || flush(B.right(), A.x))
                       && spanOverlap(A.y, A.bottom(), B.y, B.bottom()) > 0;
            // Horizontal shared edge (A/B stacked): an edge flush, positive
            // horizontal overlap.
            bool hEdge = (flush(A.bottom(), B.y) || flush(B.bottom(), A.y))
                       && spanOverlap(A.x, A.right(), B.x, B.right()) > 0;

            if (vEdge || hEdge) unite(i, j);
        }
    }

    // Relabel roots to dense ids from 0, in first-seen order.
    std::vector<int> id(n, -1);
    int next = 0;
    for (int i = 0; i < n; ++i) {
        int r = find(i);
        if (id[r] == -1) id[r] = next++;
        id[i] = id[r];
    }
    return id;
}

Rect recoverOntoDisplays(const Rect& saved, const std::vector<Rect>& displays,
                         int grabStripH, const Rect& fallback) {
    if (displays.empty()) return fallback;

    Rect block = saved.valid() ? saved : fallback;
    if (!block.valid()) block = displays.front();  // last resort: fill a display

    const int stripH = std::max(1, std::min(grabStripH, block.h));
    Rect strip { block.x, block.y, block.w, stripH };

    auto stripInside = [&](const Rect& d) {
        return strip.x >= d.x && strip.y >= d.y
            && strip.right() <= d.right() && strip.bottom() <= d.bottom();
    };

    // Already reachable on some display: leave it exactly where the user left it.
    for (const auto& d : displays)
        if (d.valid() && stripInside(d)) return block;

    // Pick the display to relocate onto: most whole-block overlap, else nearest
    // to the block's centre.
    int bestIdx = 0;
    long bestOverlap = -1;
    long bestDist = 0;
    for (int i = 0; i < (int) displays.size(); ++i) {
        const Rect& d = displays[i];
        if (!d.valid()) continue;
        long ov = (long) spanOverlap(block.x, block.right(), d.x, d.right())
                * (long) spanOverlap(block.y, block.bottom(), d.y, d.bottom());
        long dx = block.cx() - d.cx();
        long dy = block.cy() - d.cy();
        long dist = dx * dx + dy * dy;
        if (ov > bestOverlap || (ov == bestOverlap && (bestOverlap <= 0) && dist < bestDist)) {
            bestOverlap = ov;
            bestDist = dist;
            bestIdx = i;
        }
    }

    const Rect& d = displays[bestIdx];
    // Clamp fully on-screen. If the block is bigger than the display on an axis,
    // pin its top-left so the grab strip stays visible.
    Rect out = block;
    if (out.w <= d.w) out.x = std::min(std::max(out.x, d.x), d.right() - out.w);
    else              out.x = d.x;
    if (out.h <= d.h) out.y = std::min(std::max(out.y, d.y), d.bottom() - out.h);
    else              out.y = d.y;
    return out;
}

} // namespace reborn::dock
