// FrameLens — overlay placement.
// SPDX-License-Identifier: MIT
#include "OverlayGeometry.h"
#include <algorithm>

namespace fl
{
    const OverlayPosition kPositionOrder[kOverlayPositionCount] = {
        OverlayPosition::TopLeft, OverlayPosition::TopMiddle, OverlayPosition::TopRight,
        OverlayPosition::LeftMiddle, OverlayPosition::RightMiddle,
        OverlayPosition::BottomLeft, OverlayPosition::BottomMiddle, OverlayPosition::BottomRight,
        OverlayPosition::TopBar, OverlayPosition::BottomBar,
    };

    namespace
    {
        // Where on the panel its anchor point lies (0 = left/top edge, 0.5 = middle, 1 = right/bottom).
        void AnchorOf(OverlayPosition p, float& ax, float& ay)
        {
            ax = 0.f;
            ay = 0.f;
            switch (p) {
            case OverlayPosition::TopMiddle: ax = 0.5f; break;
            case OverlayPosition::TopRight: ax = 1.f; break;
            case OverlayPosition::LeftMiddle: ay = 0.5f; break;
            case OverlayPosition::RightMiddle: ax = 1.f; ay = 0.5f; break;
            case OverlayPosition::BottomLeft: ay = 1.f; break;
            case OverlayPosition::BottomMiddle: ax = 0.5f; ay = 1.f; break;
            case OverlayPosition::BottomRight: ax = 1.f; ay = 1.f; break;
            default: break;
            }
        }

        // A rectangle of size s whose anchor point (ax, ay) sits at (px, py), kept inside the monitor.
        IRect AnchoredOnMonitor(long px, long py, float ax, float ay, ISize s, const IRect& m)
        {
            const long x = std::clamp(px - static_cast<long>(ax * static_cast<float>(s.cx)), m.left, std::max(m.left, m.right - s.cx));
            const long y = std::clamp(py - static_cast<long>(ay * static_cast<float>(s.cy)), m.top, std::max(m.top, m.bottom - s.cy));
            return { x, y, x + s.cx, y + s.cy };
        }
    }

    OverlayRects ComputeOverlayRects(OverlayPosition position, IRect m, long offsetX, long offsetY,
                                     ISize base, ISize panel, int maxScalePct)
    {
        const ISize slotSize{ std::min(base.cx * maxScalePct / 100, m.Width()), std::min(base.cy * maxScalePct / 100, m.Height()) };
        OverlayRects r;

        if (IsBar(position)) {
            // Full width, pinned to its edge; Size changes the height (the slot stays as tall as the
            // largest bar, so resizing never moves the window).
            const long slotH = std::max(slotSize.cy, panel.cy);
            const bool top = position == OverlayPosition::TopBar;
            r.window = { m.left, top ? m.top : m.bottom - slotH, m.right, top ? m.top + slotH : m.bottom };
            r.panel = { m.left, top ? m.top : m.bottom - panel.cy, m.left + panel.cx, top ? m.top + panel.cy : m.bottom };
            return r;
        }

        // The anchor point: a corner `offset` in from the monitor's edges, or the middle of an edge.
        // Size scales the panel around it, so the edges it is attached to keep their distance at
        // every size; along an edge's middle the panel stays centred.
        float ax = 0.f, ay = 0.f;
        AnchorOf(position, ax, ay);
        const long px = ax == 0.f ? m.left + offsetX : ax == 1.f ? m.right - offsetX : (m.left + m.right) / 2;
        const long py = ay == 0.f ? m.top + offsetY : ay == 1.f ? m.bottom - offsetY : (m.top + m.bottom) / 2;
        r.window = AnchoredOnMonitor(px, py, ax, ay, { std::max(slotSize.cx, panel.cx), std::max(slotSize.cy, panel.cy) }, m);
        r.panel = AnchoredOnMonitor(px, py, ax, ay, panel, m);
        return r;
    }
}
