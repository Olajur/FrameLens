// FrameLens — overlay settings persistence (Win32 INI file).
// SPDX-License-Identifier: MIT
#include "OverlaySettings.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <algorithm>

namespace fl
{
    const ToggleDef kToggles[] = {
        { L"title",       L"GPU name (title)",         &OverlaySettings::title,       ToggleGroup::Frames },
        { L"fps",         L"FPS",                      &OverlaySettings::fps,         ToggleGroup::Frames },
        { L"frametime",   L"Frame time",               &OverlaySettings::frametime,   ToggleGroup::Frames },
        { L"avgMaxMin",   L"AVG / MAX / MIN",          &OverlaySettings::avgMaxMin,   ToggleGroup::Frames },
        { L"lows",        L"1% / 0.1% low",            &OverlaySettings::lows,        ToggleGroup::Frames },
        { L"graphs",      L"Graphs",                   &OverlaySettings::graphs,      ToggleGroup::Frames },
        { L"gpuLoad",     L"Load",                     &OverlaySettings::gpuLoad,     ToggleGroup::Gpu },
        { L"gpuTemp",     L"Temperature",              &OverlaySettings::gpuTemp,     ToggleGroup::Gpu },
        { L"gpuClock",    L"Core clock",               &OverlaySettings::gpuClock,    ToggleGroup::Gpu },
        { L"gpuMemClock", L"Memory clock",             &OverlaySettings::gpuMemClock, ToggleGroup::Gpu },
        { L"gpuPower",    L"Power",                    &OverlaySettings::gpuPower,    ToggleGroup::Gpu },
        { L"gpuFan",      L"Fan speed",                &OverlaySettings::gpuFan,      ToggleGroup::Gpu },
        { L"gpuVram",     L"VRAM used",                &OverlaySettings::gpuVram,     ToggleGroup::Gpu },
        { L"cpuLoad",     L"Load",                     &OverlaySettings::cpuLoad,     ToggleGroup::Cpu },
        { L"cpuTemp",     L"Temperature",              &OverlaySettings::cpuTemp,     ToggleGroup::Cpu },
        { L"cpuClock",    L"Clock",                    &OverlaySettings::cpuClock,    ToggleGroup::Cpu },
        { L"cpuPower",    L"Power",                    &OverlaySettings::cpuPower,    ToggleGroup::Cpu },
        { L"cpuCoreLoad", L"Each core: load",          &OverlaySettings::cpuCoreLoad, ToggleGroup::Cpu },
        { L"cpuCoreClock", L"Each core: clock",        &OverlaySettings::cpuCoreClock, ToggleGroup::Cpu },
        { L"ramUsed",     L"RAM used (GB)",            &OverlaySettings::ramUsed,     ToggleGroup::Memory },
        { L"ramPct",      L"RAM usage (%)",            &OverlaySettings::ramPct,      ToggleGroup::Memory },
        { L"background",  L"Show background",          &OverlaySettings::background,  ToggleGroup::Look },
        { L"hideUnavailable", L"Hide values this PC can't read", &OverlaySettings::hideUnavailable, ToggleGroup::Look },
    };
    const size_t kToggleCount = sizeof kToggles / sizeof kToggles[0];

    const ColorDef kColors[] = {
        { L"colorValues",    L"Values",     &OverlaySettings::colorValues },
        { L"colorFps",       L"FPS",        &OverlaySettings::colorFps },
        { L"colorFrametime", L"Frame time", &OverlaySettings::colorFrametime },
        { L"colorGpu",       L"GPU",        &OverlaySettings::colorGpu },
        { L"colorCpu",       L"CPU",        &OverlaySettings::colorCpu },
        { L"colorRam",       L"RAM",        &OverlaySettings::colorRam },
    };
    const size_t kColorCount = sizeof kColors / sizeof kColors[0];

    const wchar_t* HighContrastName(HighContrastMode m)
    {
        switch (m) {
        case HighContrastMode::Off: return L"Normal";
        case HighContrastMode::On: return L"High";
        default: return L"High when Windows uses it";
        }
    }

    const wchar_t* HotkeyActionName(HotkeyAction a)
    {
        switch (a) {
        case HotkeyAction::NextPosition: return L"Next position";
        case HotkeyAction::NextPreset: return L"Next preset";
        default: return L"Overlay on/off";
        }
    }

    const wchar_t* PresetName(Preset p)
    {
        switch (p) {
        case Preset::Basic: return L"Basic";
        case Preset::Advanced: return L"Advanced";
        default: return L"Only FPS";
        }
    }

    const wchar_t* PositionName(OverlayPosition p)
    {
        switch (p) {
        case OverlayPosition::TopRight: return L"Top right";
        case OverlayPosition::BottomLeft: return L"Bottom left";
        case OverlayPosition::BottomRight: return L"Bottom right";
        case OverlayPosition::LeftMiddle: return L"Left, middle";
        case OverlayPosition::RightMiddle: return L"Right, middle";
        case OverlayPosition::TopMiddle: return L"Top, middle";
        case OverlayPosition::BottomMiddle: return L"Bottom, middle";
        case OverlayPosition::TopBar: return L"Bar along the top";
        case OverlayPosition::BottomBar: return L"Bar along the bottom";
        default: return L"Top left";
        }
    }

    void ApplyPreset(OverlaySettings& s, Preset p)
    {
        for (size_t i = 0; i < kToggleCount; ++i)
            if (kToggles[i].group != ToggleGroup::Look) s.*kToggles[i].field = false;
        s.lastPreset = static_cast<int>(p);
        switch (p) {
        case Preset::OnlyFps:
            s.fps = true;
            s.hideUnavailable = true;
            break;
        case Preset::Basic:
            s.title = s.fps = s.frametime = s.graphs = true;
            s.gpuLoad = s.gpuTemp = true;
            s.cpuLoad = s.cpuClock = true;
            s.hideUnavailable = true;
            break;
        case Preset::Advanced:   // everything; whatever this PC can't read is left out
            for (size_t i = 0; i < kToggleCount; ++i)
                if (kToggles[i].group != ToggleGroup::Look) s.*kToggles[i].field = true;
            s.hideUnavailable = true;
            break;
        }
    }

    namespace
    {
        constexpr wchar_t kSection[] = L"overlay";
        constexpr const wchar_t* kHotkeyKeys[kHotkeyCount] = { L"hotkeyOverlay", L"hotkeyPosition", L"hotkeyPreset" };

        std::wstring Path()
        {
            PWSTR base = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return {};
            std::wstring dir = std::wstring(base) + L"\\FrameLens";
            CoTaskMemFree(base);
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir + L"\\overlay.ini";
        }
    }

    void LoadSettings(OverlaySettings& s)
    {
        const std::wstring p = Path();
        if (p.empty()) return;
        for (size_t i = 0; i < kToggleCount; ++i) {
            bool& v = s.*kToggles[i].field;
            v = GetPrivateProfileIntW(kSection, kToggles[i].key, v ? 1 : 0, p.c_str()) != 0;
        }
        s.opacityPct = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"opacity", s.opacityPct, p.c_str())), 10, 100);
        s.scalePct = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"scale", s.scalePct, p.c_str())), 50, 200);
        s.windowSamples = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"window", s.windowSamples, p.c_str())), 10, 1000);
        s.style = static_cast<ui::Style>(std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"style", static_cast<int>(s.style), p.c_str())),
                                                    0, ui::kStyleCount - 1));
        s.colorMode = static_cast<ui::ColorMode>(std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"colorMode", 0, p.c_str())), 0, 2));
        s.highContrast = static_cast<HighContrastMode>(std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"highContrast", 2, p.c_str())), 0, 2));
        for (size_t i = 0; i < kColorCount; ++i) {
            wchar_t hex[16]{};
            GetPrivateProfileStringW(kSection, kColors[i].key, L"", hex, 16, p.c_str());
            wchar_t* end = nullptr;
            const unsigned long v = wcstoul(hex, &end, 16);
            s.*kColors[i].field = (hex[0] && end && !*end && v <= 0xFFFFFF) ? static_cast<int>(v) : -1;
        }
        s.overlayStyle = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"overlayStyle", -1, p.c_str())), -1, ui::kStyleCount - 1);
        s.position = static_cast<OverlayPosition>(std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"position", 0, p.c_str())),
                                                             0, kOverlayPositionCount - 1));
        for (int i = 0; i < kHotkeyCount; ++i) {   // hex; "0" = not set, missing = the default
            wchar_t hex[16]{};
            GetPrivateProfileStringW(kSection, kHotkeyKeys[i], L"", hex, 16, p.c_str());
            wchar_t* end = nullptr;
            const unsigned long v = wcstoul(hex, &end, 16);
            if (hex[0] && end && !*end && v <= 0xFFFFFF) s.hotkeys[i] = static_cast<uint32_t>(v);
        }
        s.lastPreset = std::clamp(static_cast<int>(GetPrivateProfileIntW(kSection, L"lastPreset", -1, p.c_str())), -1, kPresetCount - 1);
        wchar_t mon[64]{};
        GetPrivateProfileStringW(kSection, L"monitor", L"", mon, 64, p.c_str());
        s.monitor = mon;
        wchar_t hidden[1024]{};
        GetPrivateProfileStringW(kSection, L"hiddenGpus", L"", hidden, 1024, p.c_str());
        s.hiddenGpus.clear();
        for (std::wstring rest = hidden; !rest.empty();) {
            const size_t comma = rest.find(L',');
            if (const std::wstring key = rest.substr(0, comma); !key.empty()) s.hiddenGpus.push_back(key);
            rest = comma == std::wstring::npos ? L"" : rest.substr(comma + 1);
        }
    }

    void SaveSettings(const OverlaySettings& s)
    {
        const std::wstring p = Path();
        if (p.empty()) return;
        for (size_t i = 0; i < kToggleCount; ++i)
            WritePrivateProfileStringW(kSection, kToggles[i].key, (s.*kToggles[i].field) ? L"1" : L"0", p.c_str());
        WritePrivateProfileStringW(kSection, L"opacity", std::to_wstring(s.opacityPct).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"scale", std::to_wstring(s.scalePct).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"window", std::to_wstring(s.windowSamples).c_str(), p.c_str());
        for (int i = 0; i < kHotkeyCount; ++i) {
            wchar_t hex[16];
            swprintf(hex, 16, L"%X", s.hotkeys[i]);
            WritePrivateProfileStringW(kSection, kHotkeyKeys[i], hex, p.c_str());
        }
        WritePrivateProfileStringW(kSection, L"lastPreset", std::to_wstring(s.lastPreset).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"style", std::to_wstring(static_cast<int>(s.style)).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"colorMode", std::to_wstring(static_cast<int>(s.colorMode)).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"highContrast", std::to_wstring(static_cast<int>(s.highContrast)).c_str(), p.c_str());
        for (size_t i = 0; i < kColorCount; ++i) {
            wchar_t hex[16]{};
            if (s.*kColors[i].field >= 0) swprintf(hex, 16, L"%06X", static_cast<unsigned>(s.*kColors[i].field));
            WritePrivateProfileStringW(kSection, kColors[i].key, hex, p.c_str());   // empty = the style's colour
        }
        WritePrivateProfileStringW(kSection, L"overlayStyle", std::to_wstring(s.overlayStyle).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"position", std::to_wstring(static_cast<int>(s.position)).c_str(), p.c_str());
        WritePrivateProfileStringW(kSection, L"monitor", s.monitor.c_str(), p.c_str());
        std::wstring hidden;
        for (const auto& key : s.hiddenGpus) hidden += (hidden.empty() ? L"" : L",") + key;
        WritePrivateProfileStringW(kSection, L"hiddenGpus", hidden.c_str(), p.c_str());
    }
}
