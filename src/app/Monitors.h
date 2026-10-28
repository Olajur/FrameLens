// FrameLens — connected monitors, for choosing where the overlay goes.
// SPDX-License-Identifier: MIT
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace fl
{
    struct MonitorEntry
    {
        HMONITOR handle = nullptr;
        std::wstring device;    // GDI device name, e.g. \\.\DISPLAY1 (stable key for settings)
        std::wstring label;     // e.g. "M27T20  ·  2560×1440  ·  main"
        RECT rect{};
        bool primary = false;
    };

    // Primary monitor first, then left to right.
    std::vector<MonitorEntry> ListMonitors();

    // The monitor with this device name, or nullptr if it is not connected.
    HMONITOR FindMonitor(const std::wstring& device);
}
