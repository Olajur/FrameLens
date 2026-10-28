// FrameLens — the tray icon's quick-settings panel: overlay on/off, Follow, Preset, Monitor,
// Position, and the app's actions. Unlike a menu it stays open while settings are changed and
// closes when it loses activation (a click elsewhere, switching back to the game) or on Esc.
// SPDX-License-Identifier: MIT
#pragma once
#include "../ui/Form.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fl
{
    struct TrayPanelModel
    {
        std::function<std::wstring()> status;           // e.g. "game.exe  ·  144 FPS"
        std::function<bool()> overlayShown;
        std::function<void(bool)> setOverlayShown;
        std::wstring overlayHotkey;                     // "Alt + 1" or empty
        struct Choice                                   // a dropdown: items, current index, pick
        {
            std::function<std::vector<std::wstring>()> items;
            std::function<int()> selected;
            std::function<void(int)> select;
        };
        Choice follow, monitor, position;
        std::function<int()> preset;                    // the preset applied last, -1 = none
        std::function<void(int)> setPreset;
        std::function<bool()> autostart;
        std::function<void(bool)> setAutostart;
        std::function<void()> open, customize, resetStats, exit;
        std::function<void()> close;                    // set by ShowTrayPanel
    };

    // Lays out the panel; returns its client size in DIPs. Used by the preview tool too.
    D2D1_SIZE_F BuildTrayPanel(ui::FormWindow& form, std::shared_ptr<TrayPanelModel> model);

    // Opens the panel next to `anchor` (the click on the tray icon, screen pixels), or closes it if
    // it is open or was closed by that same click.
    void ToggleTrayPanel(HINSTANCE inst, HWND owner, const ui::Theme& theme, std::shared_ptr<TrayPanelModel> model, POINT anchor);
    void CloseTrayPanel();
    void SetTrayPanelTheme(const ui::Theme& theme);   // no-op when closed
    void RefreshTrayPanel();                          // repaint live values; no-op when closed
}
