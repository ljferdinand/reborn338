// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Luke Ferdinand
//
// Headless tests for the docking geometry (app/src/dock/DockGeometry). No JUCE,
// no audio: pure rectangle math, so the hard invariants of Stage 1 are proven
// without a GUI. The window layer is verified by a real launch (the dock spike);
// everything provable on paper is proven here.
//
#include "../app/src/dock/DockGeometry.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace reborn::dock;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", #cond, __LINE__); ++g_failures; } \
} while (0)

// ---------------------------------------------------------------------------
// Snapping
// ---------------------------------------------------------------------------
static void test_snap_adjacency_and_alignment() {
    // A neighbour at x=[0..200], y=[0..100]. A block dragged so its left edge is
    // 5 px past the neighbour's right edge and its top 4 px below: within a 12 px
    // threshold it should snap flush (left -> right) and align tops.
    Rect other { 0, 0, 200, 100 };
    Rect moving { 205, 4, 120, 80 };   // left is 5 past other.right(200); top 4 off
    SnapResult s = computeSnap(moving, { other }, 12);
    CHECK(s.snappedX && s.snappedY);
    CHECK(moving.x + s.dx == other.right());   // 205 + (-5) == 200, flush
    CHECK(moving.y + s.dy == other.y);         // 4 + (-4) == 0, tops aligned
}

static void test_snap_perpendicular_gate() {
    // Same x-gap, but the block is far BELOW the neighbour: the x-snap must not
    // fire, because they are not vertically near.
    Rect other { 0, 0, 200, 100 };
    Rect moving { 205, 5000, 120, 80 };
    SnapResult s = computeSnap(moving, { other }, 12);
    CHECK(!s.snappedX);
    CHECK(!s.snappedY);
}

static void test_snap_threshold() {
    Rect other { 0, 0, 200, 100 };
    Rect moving { 220, 0, 120, 80 };   // 20 px gap, beyond a 12 px threshold
    SnapResult s = computeSnap(moving, { other }, 12);
    CHECK(!s.snappedX);
}

static void test_snap_picks_smallest_delta() {
    // Two neighbours offer x-candidates; the nearer edge wins.
    Rect a { 0, 0, 100, 100 };      // right = 100
    Rect b { 106, 0, 100, 100 };    // left = 106
    Rect moving { 103, 0, 40, 100 }; // left 3 from a.right(100); would also near b.left
    SnapResult s = computeSnap(moving, { a, b }, 12);
    CHECK(s.snappedX);
    // Nearest single edge is a.right at delta -3 (moving.left 103 -> 100).
    CHECK(std::abs(s.dx) <= 6);
}

// ---------------------------------------------------------------------------
// Grouping
// ---------------------------------------------------------------------------
static void test_group_side_by_side() {
    std::vector<Rect> r {
        { 0, 0, 100, 100 },
        { 100, 0, 100, 100 },   // flush right edge, full vertical overlap
    };
    auto g = groupIds(r, 2);
    CHECK(g[0] == g[1]);
}

static void test_group_corner_touch_is_not_a_group() {
    std::vector<Rect> r {
        { 0, 0, 100, 100 },
        { 100, 100, 100, 100 },  // touches only at the corner (0 overlap)
    };
    auto g = groupIds(r, 2);
    CHECK(g[0] != g[1]);
}

static void test_group_chain_and_detached() {
    std::vector<Rect> r {
        { 0, 0, 100, 100 },      // A
        { 100, 0, 100, 100 },    // B flush to A
        { 200, 0, 100, 100 },    // C flush to B
        { 500, 500, 100, 100 },  // D far away
    };
    auto g = groupIds(r, 2);
    CHECK(g[0] == g[1] && g[1] == g[2]);   // A,B,C one group
    CHECK(g[3] != g[0]);                    // D on its own
}

static void test_group_stacked() {
    std::vector<Rect> r {
        { 0, 0, 100, 60 },
        { 0, 60, 100, 60 },   // flush bottom edge, full horizontal overlap
    };
    auto g = groupIds(r, 2);
    CHECK(g[0] == g[1]);
}

// ---------------------------------------------------------------------------
// Multi-screen recovery: the hard requirement (PLAN_GUI section 6)
// ---------------------------------------------------------------------------
static bool stripInsideAny(const Rect& b, const std::vector<Rect>& displays, int stripH) {
    Rect strip { b.x, b.y, b.w, stripH < b.h ? stripH : b.h };
    for (const auto& d : displays)
        if (strip.x >= d.x && strip.y >= d.y
         && strip.right() <= d.right() && strip.bottom() <= d.bottom())
            return true;
    return false;
}

static void test_recover_onscreen_unchanged() {
    std::vector<Rect> disp { { 0, 0, 1920, 1080 } };
    Rect saved { 300, 200, 400, 300 };
    Rect out = recoverOntoDisplays(saved, disp, 24, { 0, 0, 400, 300 });
    CHECK(out == saved);
}

static void test_recover_lost_second_screen() {
    // Block lived on a second monitor to the right; that monitor is now gone.
    std::vector<Rect> before { { 0, 0, 1920, 1080 }, { 1920, 0, 1920, 1080 } };
    Rect saved { 2200, 300, 400, 300 };
    CHECK(stripInsideAny(saved, before, 24));         // was reachable
    std::vector<Rect> now { { 0, 0, 1920, 1080 } };   // second screen unplugged
    Rect out = recoverOntoDisplays(saved, now, 24, { 0, 0, 400, 300 });
    CHECK(stripInsideAny(out, now, 24));              // still reachable
}

static void test_recover_clamps_partial_offscreen() {
    std::vector<Rect> disp { { 0, 0, 1920, 1080 } };
    Rect saved { 1800, 1000, 400, 300 };   // hangs off the right and bottom
    Rect out = recoverOntoDisplays(saved, disp, 24, { 0, 0, 400, 300 });
    CHECK(stripInsideAny(out, disp, 24));
    CHECK(out.right() <= 1920 && out.bottom() <= 1080);
}

static void test_recover_block_bigger_than_display_pins_topleft() {
    std::vector<Rect> disp { { 0, 0, 800, 600 } };
    Rect saved { -50, -50, 1000, 800 };    // larger than the only display
    Rect out = recoverOntoDisplays(saved, disp, 24, { 0, 0, 400, 300 });
    CHECK(out.x == 0 && out.y == 0);       // grab strip pinned visible
}

static void test_recover_invalid_uses_fallback() {
    std::vector<Rect> disp { { 0, 0, 1920, 1080 } };
    Rect fallback { 40, 40, 400, 300 };
    Rect out = recoverOntoDisplays({ 0, 0, 0, 0 }, disp, 24, fallback);
    CHECK(out == fallback);
}

static void test_recover_negative_origin_display() {
    // A left-hand monitor whose origin is negative (common real layout).
    std::vector<Rect> disp { { -1920, 0, 1920, 1080 }, { 0, 0, 2560, 1440 } };
    Rect saved { -1800, 100, 500, 300 };
    Rect out = recoverOntoDisplays(saved, disp, 24, { 0, 0, 400, 300 });
    CHECK(out == saved);                    // already reachable on the left monitor
    CHECK(stripInsideAny(out, disp, 24));
}

static void test_recover_invariant_sweep() {
    // The invariant, exercised broadly: whatever the saved position, recovery
    // returns a rect whose grab strip is inside some live display.
    std::vector<Rect> disp { { 0, 0, 1920, 1080 }, { 1920, -200, 1280, 1024 } };
    const int stripH = 24;
    Rect fallback { 0, 0, 400, 300 };
    for (int gx = -3000; gx <= 5000; gx += 137) {
        for (int gy = -2000; gy <= 3000; gy += 143) {
            Rect saved { gx, gy, 380 + (gx & 255), 260 + (gy & 127) };
            Rect out = recoverOntoDisplays(saved, disp, stripH, fallback);
            CHECK(stripInsideAny(out, disp, stripH));
        }
    }
}

int main() {
    test_snap_adjacency_and_alignment();
    test_snap_perpendicular_gate();
    test_snap_threshold();
    test_snap_picks_smallest_delta();

    test_group_side_by_side();
    test_group_corner_touch_is_not_a_group();
    test_group_chain_and_detached();
    test_group_stacked();

    test_recover_onscreen_unchanged();
    test_recover_lost_second_screen();
    test_recover_clamps_partial_offscreen();
    test_recover_block_bigger_than_display_pins_topleft();
    test_recover_invalid_uses_fallback();
    test_recover_negative_origin_display();
    test_recover_invariant_sweep();

    if (g_failures == 0) std::printf("all dock geometry tests passed\n");
    else                 std::printf("%d dock geometry test(s) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
