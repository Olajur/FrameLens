// FrameLens — contents of the main (taskbar) window: header, the game being followed, live FPS,
// frame-capture and overlay status, and the action buttons. The app supplies the data and actions
// through MainModel; the layout lives here so the preview tool can render the same window.
// SPDX-License-Identifier: MIT
#pragma once
#include "../ui/Form.h"
#include <functional>
#include <string>
#include <vector>

namespace fl
{
    struct MainModel
    {
        std::wstring subtitle;
        // "Follow" dropdown
        std::function<std::wstring()> followDisplay;
        std::function<std::vector<std::wstring>()> followItems;
        std::function<int()> followSelected;
        std::function<void(int)> followSelect;
        // Status rows
        std::function<std::wstring()> game, fps, capture, overlay;
        std::function<uint32_t()> captureColor;   // 0 = normal text colour
        // Actions
        std::function<void()> customize, resetStats, toggleOverlay, exit, toggleDarkLight;
        std::function<std::wstring()> overlayButtonText;
        std::function<bool()> isDark;
    };

    constexpr float kMainFormWidth = 580.f, kMainFormHeight = 446.f;
    void BuildMainForm(ui::FormWindow& form, const MainModel& m);
}
