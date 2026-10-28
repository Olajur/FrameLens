// FrameLens — connected monitors.
// SPDX-License-Identifier: MIT
#include "Monitors.h"
#include <algorithm>
#include <cwchar>

namespace fl
{
    namespace
    {
        // The monitor's own name ("M27T20") via the display configuration API; empty if unknown.
        std::wstring FriendlyName(const wchar_t* gdiDevice)
        {
            UINT32 nPaths = 0, nModes = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return {};
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPaths);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(nModes);
            if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS) return {};
            for (UINT32 i = 0; i < nPaths; ++i) {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME src{};
                src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                src.header.size = sizeof src;
                src.header.adapterId = paths[i].sourceInfo.adapterId;
                src.header.id = paths[i].sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS || wcscmp(src.viewGdiDeviceName, gdiDevice) != 0) continue;
                DISPLAYCONFIG_TARGET_DEVICE_NAME tgt{};
                tgt.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                tgt.header.size = sizeof tgt;
                tgt.header.adapterId = paths[i].targetInfo.adapterId;
                tgt.header.id = paths[i].targetInfo.id;
                if (DisplayConfigGetDeviceInfo(&tgt.header) == ERROR_SUCCESS && tgt.monitorFriendlyDeviceName[0])
                    return tgt.monitorFriendlyDeviceName;
            }
            return {};
        }

        BOOL CALLBACK Collect(HMONITOR mon, HDC, LPRECT, LPARAM out)
        {
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (!GetMonitorInfoW(mon, &mi)) return TRUE;
            MonitorEntry e;
            e.handle = mon;
            e.device = mi.szDevice;
            e.rect = mi.rcMonitor;
            e.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            std::wstring name = FriendlyName(mi.szDevice);
            if (name.empty()) name = e.device;
            e.label = name + L"  ·  " + std::to_wstring(e.rect.right - e.rect.left) + L"×" +
                      std::to_wstring(e.rect.bottom - e.rect.top) + (e.primary ? L"  ·  main" : L"");
            reinterpret_cast<std::vector<MonitorEntry>*>(out)->push_back(std::move(e));
            return TRUE;
        }
    }

    std::vector<MonitorEntry> ListMonitors()
    {
        std::vector<MonitorEntry> list;
        EnumDisplayMonitors(nullptr, nullptr, Collect, reinterpret_cast<LPARAM>(&list));
        std::stable_sort(list.begin(), list.end(), [](const MonitorEntry& a, const MonitorEntry& b) {
            if (a.primary != b.primary) return a.primary;
            return a.rect.left < b.rect.left;
        });
        return list;
    }

    // Called once a second by the overlay placement: compares device names only (no name lookups).
    HMONITOR FindMonitor(const std::wstring& device)
    {
        if (device.empty()) return nullptr;
        struct Search { const std::wstring* device; HMONITOR found; } s{ &device, nullptr };
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM p) -> BOOL {
            auto* s = reinterpret_cast<Search*>(p);
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof mi;
            if (GetMonitorInfoW(mon, &mi) && *s->device == mi.szDevice) { s->found = mon; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&s));
        return s.found;
    }
}
