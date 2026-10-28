// FrameLens — overlay settings: which metrics the overlay shows, and its background.
// Stored in %LOCALAPPDATA%\FrameLens\overlay.ini and applied live.
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/OverlayGeometry.h"
#include "../ui/Theme.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fl
{
    const wchar_t* PositionName(OverlayPosition p);   // "Top left", ..., "Bar along the top"

    // High-contrast values on the overlay (ui::HighContrast).
    enum class HighContrastMode : int { Off, On, FollowWindows };
    const wchar_t* HighContrastName(HighContrastMode m);

    // Global hotkeys, packed as modifiers << 16 | virtual-key code (modifiers as for RegisterHotKey:
    // 1 Alt, 2 Ctrl, 4 Shift, 8 Win); 0 = not set.
    enum class HotkeyAction : int { ToggleOverlay, NextPosition, NextPreset };
    constexpr int kHotkeyCount = 3;
    const wchar_t* HotkeyActionName(HotkeyAction a);   // "Overlay on/off", ...
    constexpr uint32_t PackHotkey(uint32_t mods, uint32_t vk) { return mods << 16 | vk; }

    struct OverlaySettings
    {
        // Frames
        bool title = true;          // GPU name header
        bool fps = true;
        bool frametime = true;
        bool avgMaxMin = true;
        bool lows = false;          // 1% / 0.1% low
        bool graphs = true;
        int windowSamples = 100;    // rolling AVG / MIN / MAX window: samples of 0.25 s (10..1000)
        // GPU
        bool gpuLoad = true, gpuTemp = true, gpuClock = false, gpuMemClock = false, gpuPower = false, gpuFan = true, gpuVram = true;
        // CPU
        bool cpuLoad = true, cpuTemp = true, cpuClock = false, cpuPower = false;
        bool cpuCoreLoad = false, cpuCoreClock = false;   // grid of every physical core
        // Memory
        bool ramUsed = true, ramPct = false;
        // Look
        bool background = true;
        int opacityPct = 90;        // background opacity, 10..100
        int scalePct = 100;         // overlay size incl. text, 50..200
        bool hideUnavailable = false;   // drop values this PC can't read instead of showing n/a
        std::wstring monitor;       // monitor device name, e.g. DISPLAY2; empty = the followed game's monitor
        OverlayPosition position = OverlayPosition::TopLeft;
        // Look of the app windows and the overlay
        ui::Style style = ui::Style::Minimal2026;
        ui::ColorMode colorMode = ui::ColorMode::System;
        int overlayStyle = -1;      // -1 = same as the app, else a ui::Style
        HighContrastMode highContrast = HighContrastMode::FollowWindows;
        // Own colours for the overlay's values, 0xRRGGBB; -1 = the style's colour (see kColors)
        int colorValues = -1, colorFps = -1, colorFrametime = -1, colorGpu = -1, colorCpu = -1, colorRam = -1;
        std::vector<std::wstring> hiddenGpus;   // GPU keys ("1002-7590-86011EAE-0") not shown; default: all shown
        // Hotkeys by HotkeyAction; default Alt+1, Alt+2, Alt+3
        uint32_t hotkeys[kHotkeyCount] = { PackHotkey(1, '1'), PackHotkey(1, '2'), PackHotkey(1, '3') };
        int lastPreset = -1;        // the preset applied last (a Preset), so the hotkey moves on to the next
    };

    enum class ToggleGroup { Frames, Gpu, Cpu, Memory, Look };

    struct ToggleDef
    {
        const wchar_t* key;         // INI key
        const wchar_t* label;       // checkbox text
        bool OverlaySettings::* field;
        ToggleGroup group;
    };

    extern const ToggleDef kToggles[];
    extern const size_t kToggleCount;

    struct ColorDef
    {
        const wchar_t* key;         // INI key
        const wchar_t* label;       // "Values", "FPS", ...
        int OverlaySettings::* field;
    };
    extern const ColorDef kColors[];
    extern const size_t kColorCount;

    // Presets set the metric checkboxes and turn on "hide values this PC can't read";
    // background, opacity and size stay as they are.
    enum class Preset { OnlyFps, Basic, Advanced };
    constexpr int kPresetCount = 3;
    const wchar_t* PresetName(Preset p);   // "Only FPS", "Basic", "Advanced"
    void ApplyPreset(OverlaySettings& s, Preset p);

    void LoadSettings(OverlaySettings& s);
    void SaveSettings(const OverlaySettings& s);
}
