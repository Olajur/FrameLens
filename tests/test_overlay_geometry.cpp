// FrameLens — overlay placement unit tests (dependency-free; runs on any OS).
// SPDX-License-Identifier: MIT
#include "../src/core/OverlayGeometry.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace fl;

static int gFailures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++gFailures; } } while (0)
#define CHECK_EQ(a, b) do { long _a = (a), _b = (b); if (_a != _b) { \
    std::printf("FAIL %s:%d  %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); ++gFailures; } } while (0)

namespace
{
    const IRect kMain{ 0, 0, 2560, 1440 };
    const IRect kLeft{ -1366, 200, 0, 968 };   // second monitor left of the main one
    const ISize kBase{ 260, 300 };

    ISize Scaled(ISize s, int pct) { return { s.cx * pct / 100, s.cy * pct / 100 }; }
    bool Inside(const IRect& in, const IRect& out)
    {
        return in.left >= out.left && in.top >= out.top && in.right <= out.right && in.bottom <= out.bottom;
    }
    OverlayRects Place(OverlayPosition p, const IRect& m, int pct, ISize base = kBase)
    {
        return ComputeOverlayRects(p, m, 24, 24, base, Scaled(base, pct), 200);
    }
}

static void TestAnchoredAtEverySize()
{
    // Every position keeps its distance from the edges it is attached to, at every Size, and the
    // middle positions stay centred along their edge (shrinking never pulls the panel inward).
    for (const IRect& m : { kMain, kLeft }) {
        const long cx = (m.left + m.right) / 2, cy = (m.top + m.bottom) / 2;
        for (int pct : { 50, 70, 100, 130, 150 }) {
            auto r = Place(OverlayPosition::TopLeft, m, pct);
            CHECK_EQ(r.panel.left, m.left + 24);
            CHECK_EQ(r.panel.top, m.top + 24);
            r = Place(OverlayPosition::TopRight, m, pct);
            CHECK_EQ(r.panel.right, m.right - 24);
            CHECK_EQ(r.panel.top, m.top + 24);
            r = Place(OverlayPosition::BottomLeft, m, pct);
            CHECK_EQ(r.panel.left, m.left + 24);
            CHECK_EQ(r.panel.bottom, m.bottom - 24);
            r = Place(OverlayPosition::BottomRight, m, pct);
            CHECK_EQ(r.panel.right, m.right - 24);
            CHECK_EQ(r.panel.bottom, m.bottom - 24);
            r = Place(OverlayPosition::LeftMiddle, m, pct);
            CHECK_EQ(r.panel.left, m.left + 24);
            CHECK(std::labs(r.panel.top + r.panel.Height() / 2 - cy) <= 1);
            r = Place(OverlayPosition::RightMiddle, m, pct);
            CHECK_EQ(r.panel.right, m.right - 24);
            CHECK(std::labs(r.panel.top + r.panel.Height() / 2 - cy) <= 1);
            r = Place(OverlayPosition::TopMiddle, m, pct);
            CHECK_EQ(r.panel.top, m.top + 24);
            CHECK(std::labs(r.panel.left + r.panel.Width() / 2 - cx) <= 1);
            r = Place(OverlayPosition::BottomMiddle, m, pct);
            CHECK_EQ(r.panel.bottom, m.bottom - 24);
            CHECK(std::labs(r.panel.left + r.panel.Width() / 2 - cx) <= 1);
        }
    }
}

static void TestResizingNeverMovesTheWindow()
{
    for (int p = 0; p < kOverlayPositionCount; ++p) {
        const auto pos = static_cast<OverlayPosition>(p);
        const auto at100 = Place(pos, kMain, 100);
        for (int pct : { 50, 80, 150, 200 }) {
            const auto r = Place(pos, kMain, pct);
            CHECK(r.window == at100.window);           // resizing never moves the window
            CHECK(Inside(r.panel, r.window));
            CHECK(Inside(r.panel, kMain));
        }
    }
}

static void TestPositionOrderListsEveryPosition()
{
    int seen[kOverlayPositionCount] = {};
    for (OverlayPosition p : kPositionOrder) ++seen[static_cast<int>(p)];
    for (int n : seen) CHECK_EQ(n, 1);
}

static void TestBars()
{
    // A bar's panel is as wide as the monitor at any Size (the host lays it out at width / scale).
    for (const IRect& m : { kMain, kLeft }) {
        const ISize base{ m.Width(), 28 };
        for (int pct : { 50, 100, 200 }) {
            const ISize panel{ m.Width(), 28 * pct / 100 };
            auto r = ComputeOverlayRects(OverlayPosition::TopBar, m, 24, 24, base, panel, 200);
            CHECK_EQ(r.panel.left, m.left);
            CHECK_EQ(r.panel.top, m.top);
            CHECK_EQ(r.panel.Width(), m.Width());
            CHECK_EQ(r.window.top, m.top);
            CHECK_EQ(r.window.Height(), 56);   // the 200 % bar: same window at every Size
            r = ComputeOverlayRects(OverlayPosition::BottomBar, m, 24, 24, base, panel, 200);
            CHECK_EQ(r.panel.bottom, m.bottom);
            CHECK_EQ(r.window.bottom, m.bottom);
            CHECK(Inside(r.panel, r.window));
        }
    }
}

static void TestHugePanelStaysOnScreen()
{
    // Panel taller than a small monitor: clamped to the monitor's top-left, never off-screen.
    const IRect small{ 0, 0, 800, 600 };
    const auto r = Place(OverlayPosition::BottomRight, small, 200, { 260, 700 });
    CHECK(r.panel.left >= small.left);
    CHECK(r.panel.top >= small.top);
    CHECK(r.window.left >= small.left);
    CHECK(r.window.top >= small.top);
}

int main()
{
    TestAnchoredAtEverySize();
    TestResizingNeverMovesTheWindow();
    TestPositionOrderListsEveryPosition();
    TestBars();
    TestHugePanelStaysOnScreen();
    if (gFailures == 0) std::printf("All OverlayGeometry tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
