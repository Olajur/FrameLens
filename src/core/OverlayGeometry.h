// FrameLens — where the overlay window and its panel go on a monitor (platform independent,
// unit-tested).
//
// The window is a fixed transparent "slot" big enough for the panel at the largest Size, so changing
// Size only redraws the panel at a new offset inside it: the window never moves or resizes then
// (moving a window shows one frame of the old image at the new place, i.e. flicker). The slot only
// changes with the monitor, DPI, position or the panel's 100 % size (the chosen metrics).
// SPDX-License-Identifier: MIT
#pragma once

namespace fl
{
    // The bars are full-width rows along an edge (SteamOS style); the others place the panel
    // `offset` px in from the monitor's edges they touch. Values are stored in settings files:
    // new positions go at the end.
    enum class OverlayPosition : int
    {
        TopLeft, TopRight, BottomLeft, BottomRight, LeftMiddle, RightMiddle, TopBar, BottomBar,
        TopMiddle, BottomMiddle,
    };
    constexpr int kOverlayPositionCount = 10;
    // The order to offer them in: top row, sides, bottom row, bars.
    extern const OverlayPosition kPositionOrder[kOverlayPositionCount];
    constexpr bool IsBar(OverlayPosition p) { return p == OverlayPosition::TopBar || p == OverlayPosition::BottomBar; }

    struct IRect
    {
        long left = 0, top = 0, right = 0, bottom = 0;
        long Width() const noexcept { return right - left; }
        long Height() const noexcept { return bottom - top; }
        bool operator==(const IRect&) const = default;
    };
    struct ISize { long cx = 0, cy = 0; };

    struct OverlayRects
    {
        IRect window;   // the overlay window (screen pixels)
        IRect panel;    // where the panel is drawn (screen pixels), inside `window`
    };

    // monitor: the monitor's rectangle. offsetX/Y: distance from its edges (not used by bars).
    // base: the panel's size at 100 %; panel: at the current Size. maxScalePct: the largest Size.
    // The Size setting scales the panel around its anchor point (the corner, or the middle of the
    // edge, it is attached to), so it keeps its distance from those edges at every size. Both
    // rectangles stay inside the monitor. Bars span the monitor's width and keep their edge.
    OverlayRects ComputeOverlayRects(OverlayPosition position, IRect monitor, long offsetX, long offsetY,
                                     ISize base, ISize panel, int maxScalePct);
}
