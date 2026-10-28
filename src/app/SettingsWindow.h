// FrameLens — "Customize overlay" window: presets, look (style, dark/light, overlay style),
// placement, and the metric switches. Custom-drawn with the app's theme.
// SPDX-License-Identifier: MIT
#pragma once
#include "Monitors.h"
#include "OverlaySettings.h"
#include "../ui/Form.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fl
{
    struct GpuChoice
    {
        std::wstring key;      // stable id stored in OverlaySettings::hiddenGpus
        std::wstring label;    // "1 · AMD Radeon RX 9060 XT"
    };

    struct SettingsModel
    {
        OverlaySettings* settings = nullptr;
        std::vector<GpuChoice> gpus;                       // with 2+ the GPU card lists them
        std::vector<MonitorEntry> monitors;
        std::function<bool(const ToggleDef&)> notReadable; // marks metrics this PC can't provide
        // The style's colour for a value colour (high contrast applied, own colours not), and the
        // overlay panel colour: for the swatches and the "low contrast" warning.
        std::function<uint32_t(const ColorDef&)> styleColor;
        std::function<uint32_t()> overlayPanel;
        std::function<void()> changed;                     // after every change (the app saves and applies)
        std::function<void()> previewed;                   // a live preview (colour picker open): apply, don't save
        std::function<void()> close;
        std::function<void(bool)> pauseHotkeys;            // while a hotkey is being recorded
        std::function<std::wstring(int)> hotkeyNote;       // by HotkeyAction: why it doesn't work, or empty
    };

    // Lays out the window's controls; returns the client size in DIPs. Used by the preview tool too.
    D2D1_SIZE_F BuildSettingsForm(ui::FormWindow& form, std::shared_ptr<SettingsModel> model);

    // Opens the window (or brings it to the front). Every change updates `settings`, saves it and
    // calls onChange.
    void ShowSettingsWindow(HINSTANCE inst, HWND owner, const ui::Theme& theme, OverlaySettings& settings,
                            std::vector<GpuChoice> gpus, std::function<bool(const ToggleDef&)> notReadable,
                            std::function<uint32_t(const ColorDef&)> styleColor, std::function<uint32_t()> overlayPanel,
                            std::function<void(bool)> pauseHotkeys, std::function<std::wstring(int)> hotkeyNote,
                            std::function<void()> onChange);
    // Re-themes the window when the style or dark/light mode changes (no-op when closed).
    void SetSettingsWindowTheme(const ui::Theme& theme);
    // Repaints it after the settings changed elsewhere (hotkeys); no-op when closed.
    void RefreshSettingsWindow();
}
