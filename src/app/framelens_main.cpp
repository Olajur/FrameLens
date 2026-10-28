// FrameLens — desktop app: monitoring engine + on-screen overlay, with a taskbar window and a tray icon.
//
// Tier 1 build (no uiAccess, see docs/OSD_PLAN.md §3): the overlay appears over windowed and
// borderless games; exclusive / FSO fullscreen games cover it.
//
//   framelens [--pid <pid>] [--x <dip>] [--y <dip>]
//
//   --pid     follow this process instead of the focused game
//   --x --y   overlay distance from the monitor edges it is anchored to (default 12, 12)
// SPDX-License-Identifier: MIT
#include "../engine/MonitorEngine.h"
#include "../osd/OsdRenderer.h"
#include "OverlaySettings.h"
#include "Monitors.h"
#include "SettingsWindow.h"
#include "TrayPanel.h"
#include "../core/CpuCores.h"
#include "MainForm.h"
#include "../ui/Form.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

using namespace fl;

namespace
{
    constexpr wchar_t kMainClass[] = L"FrameLensMain";
    constexpr wchar_t kOverlayClass[] = L"FrameLensOverlay";
    constexpr wchar_t kInstanceMutex[] = L"Local\\FrameLens.App";
    constexpr UINT WM_TRAY = WM_APP + 1;
    constexpr UINT_PTR kTickTimer = 1;
    constexpr UINT kTickMs = 250;                 // 4 Hz: readable, and cheap for the game
    constexpr size_t kLoadPoints = 60, kFrametimePoints = 150;
    constexpr int kHotkeyIdBase = 0x4C30;         // RegisterHotKey ids: base + HotkeyAction

    struct App
    {
        HINSTANCE inst = nullptr;
        HWND main = nullptr, overlay = nullptr;
        ui::FormWindow form;                        // the main (taskbar) window
        ui::Theme overlayTheme;                     // style + high contrast + own colours (OverlayTheme)
        HICON iconBig = nullptr, iconSmall = nullptr;
        UINT taskbarCreated = 0;
        bool trayAdded = false;

        MonitorEngine engine;
        OverlaySettings settings;
        osd::Renderer renderer;
        std::wstring rendererError;
        bool overlayShown = true;
        float offX = 12.f, offY = 12.f;   // distance of the panel from the monitor edges it is anchored to (DIPs)
        int overlayX = INT_MIN, overlayY = INT_MIN;   // window's current top-left
        POINT anchor{ INT_MIN, INT_MIN };            // top-left of the panel at 100 % Size (monitor + offset)
        RECT anchorMonitor{};
        HMONITOR anchorMonitorHandle = nullptr;
        ULONGLONG lastPlaceMs = 0;
        unsigned ticks = 0;

        HardwareSnapshot hw;
        FrameSnapshot fr;
        uint64_t lastHwSeq = 0;
        std::vector<uint32_t> followPids;           // "Follow" list entries -> pid (0 = automatic)
        std::vector<std::vector<float>> gpuHists;   // load history per GPU (index = hw.gpus index)
        std::vector<float> cpuHist, ramHist;
        bool hotkeysPaused = false;                 // a hotkey is being recorded in Customize overlay
        bool hotkeyOk[kHotkeyCount]{};              // registered (false: not set, or another app owns it)
    };
    App g;

    const ui::Theme& OverlayTheme();   // the overlay's look (see "look" below)
    bool HighContrastActive();
    void SettingsChangedHere();        // save + apply a change made outside Customize overlay
    void OpenCustomize();

    // ---------------------------------------------------------------- formatting
    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    std::wstring Num(double v, int decimals)
    {
        wchar_t b[32];
        swprintf(b, 32, L"%.*f", decimals, v);
        return b;
    }

    // n/a = not supported on this hardware/build, -- = temporarily unavailable (same as the CLI).
    std::wstring Val(const Reading& r, int decimals, double scale = 1.0)
    {
        if (r.state == SensorState::Unsupported) return L"n/a";
        if (r.state == SensorState::Unavailable) return L"--";
        return Num(r.value * scale, decimals);
    }

    osd::Tone ToneOf(const Reading& r, double warm, double hot)
    {
        if (!r.ok()) return osd::Tone::Muted;
        if (r.value >= hot) return osd::Tone::Hot;
        if (r.value >= warm) return osd::Tone::Warm;
        return osd::Tone::Normal;
    }

    void Push(std::vector<float>& h, double v, size_t cap)
    {
        h.push_back(static_cast<float>(v));
        if (h.size() > cap) h.erase(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(h.size() - cap));
    }

    // The overlay shows one GPU: the first discrete one, else the first one found.
    const GpuSnapshot* PickGpu(const HardwareSnapshot& hw)
    {
        for (const auto& gpu : hw.gpus)
            if (!gpu.adapter.likelyIntegrated && gpu.adapter.vendor != GpuVendor::Microsoft) return &gpu;
        return hw.gpus.empty() ? nullptr : &hw.gpus.front();
    }

    bool HaveFrames() { return g.fr.status == FrameSourceStatus::Running && g.fr.targetPid != 0; }

    // Why there is no FPS, in one short sentence (empty when frames are flowing).
    std::wstring FrameProblem(bool forOverlay)
    {
        switch (g.fr.status) {
        case FrameSourceStatus::NotStarted: return L"Starting frame capture…";
        case FrameSourceStatus::AccessDenied:
            return forOverlay ? L"No FPS: frame capture needs permission (see FrameLens window)"
                              : L"no permission — run tools\\setup-etw-access.bat once, then sign out and back in";
        case FrameSourceStatus::NotBuilt: return L"This build has no frame capture";
        case FrameSourceStatus::Failed: return forOverlay ? L"Frame capture failed" : L"failed: " + Widen(g.fr.statusText);
        default: break;
        }
        if (!g.fr.targetPid) return forOverlay ? L"Waiting for a game…" : L"";
        if (!g.fr.app.active && g.fr.targetLocked) return L"Not drawing frames: " + Widen(g.fr.targetName);
        if (!g.fr.app.active) return L"No frames from " + Widen(g.fr.targetName);
        return L"";
    }

    constexpr double kGiB = 1.0 / (1ull << 30);

    // Stable id for a GPU across restarts (LUIDs change at every boot): PCI ids + which of several
    // identical cards it is, e.g. "1002-7590-86011EAE-0".
    std::wstring GpuKey(const HardwareSnapshot& hw, size_t i)
    {
        const GpuAdapterInfo& a = hw.gpus[i].adapter;
        int ordinal = 0;
        for (size_t k = 0; k < i; ++k) {
            const GpuAdapterInfo& b = hw.gpus[k].adapter;
            if (b.vendorId == a.vendorId && b.deviceId == a.deviceId && b.subSysId == a.subSysId) ++ordinal;
        }
        wchar_t key[64];
        swprintf(key, 64, L"%04X-%04X-%08X-%d", a.vendorId, a.deviceId, a.subSysId, ordinal);
        return key;
    }

    bool GpuShown(const HardwareSnapshot& hw, size_t i)
    {
        const auto& hidden = g.settings.hiddenGpus;
        return std::find(hidden.begin(), hidden.end(), GpuKey(hw, i)) == hidden.end();
    }

    std::wstring GpuLabel(const HardwareSnapshot& hw, size_t i)
    {
        return std::to_wstring(i + 1) + L" \u00B7 " + Widen(hw.gpus[i].adapter.name);
    }

    // The GPU reading behind a GPU toggle.
    const Reading* GpuReadingFor(bool OverlaySettings::* f, const GpuSnapshot& gpu)
    {
        if (f == &OverlaySettings::gpuLoad) return &gpu.utilizationPct;
        if (f == &OverlaySettings::gpuTemp) return &gpu.temperatureC;
        if (f == &OverlaySettings::gpuClock) return &gpu.coreClockMHz;
        if (f == &OverlaySettings::gpuMemClock) return &gpu.memoryClockMHz;
        if (f == &OverlaySettings::gpuPower) return &gpu.powerW;
        if (f == &OverlaySettings::gpuFan) return gpu.fanRpm.state != SensorState::Unsupported ? &gpu.fanRpm : &gpu.fanPct;
        if (f == &OverlaySettings::gpuVram) return &gpu.dedicatedUsedBytes;
        return nullptr;
    }

    const Reading* ReadingFor(const ToggleDef& t, const HardwareSnapshot& hw);

    // Whether this PC can't provide a toggle's value at all; for GPU values: on none of its GPUs.
    bool NotReadable(const ToggleDef& t, const HardwareSnapshot& hw)
    {
        if (t.field == &OverlaySettings::cpuCoreLoad) return hw.cpu.cores.empty() || hw.cpu.coreUtilizationPct.empty();
        if (t.field == &OverlaySettings::cpuCoreClock) return hw.cpu.cores.empty() || hw.cpu.coreClockMHz.empty();
        if (t.group == ToggleGroup::Gpu) {
            if (hw.gpus.empty()) return false;
            for (const auto& gpu : hw.gpus) {
                const Reading* r = GpuReadingFor(t.field, gpu);
                if (!r || r->state != SensorState::Unsupported) return false;
            }
            return true;
        }
        const Reading* r = ReadingFor(t, hw);
        return r && r->state == SensorState::Unsupported;
    }

    // Reading behind each non-GPU toggle, so the settings window can mark what this PC can't read.
    const Reading* ReadingFor(const ToggleDef& t, const HardwareSnapshot& hw)
    {
        const auto f = t.field;
        if (f == &OverlaySettings::cpuLoad) return &hw.cpu.totalUtilizationPct;
        if (f == &OverlaySettings::cpuTemp) return &hw.cpu.packageTempC;
        if (f == &OverlaySettings::cpuClock) return &hw.cpu.effectiveClockMHz;
        if (f == &OverlaySettings::cpuPower) return &hw.cpu.packagePowerW;
        if (f == &OverlaySettings::ramUsed) return &hw.memory.usedBytes;
        if (f == &OverlaySettings::ramPct) return &hw.memory.utilizationPct;
        return nullptr;
    }

    osd::OsdView BuildView()
    {
        const OverlaySettings& st = g.settings;
        osd::OsdView v;
        v.background = st.background;
        v.opacity = static_cast<float>(st.opacityPct) / 100.f;
        if (HighContrastActive()) v.opacity = std::max(v.opacity, 0.85f);   // a solid panel behind high-contrast values
        v.fpsRgb = st.colorFps;
        v.scale = static_cast<float>(st.scalePct) / 100.f;
        v.timeSec = static_cast<double>(GetTickCount64()) / 1000.0;
        if (IsBar(st.position) && g.renderer.Ok()) {   // as wide as the monitor at any Size
            v.bar = true;
            v.barWidthDip = static_cast<float>(g.anchorMonitor.right - g.anchorMonitor.left) * 96.f /
                            static_cast<float>(g.renderer.Dpi()) / v.scale;
        }
        v.graphs = st.graphs;

        // Title: the one shown GPU, or with several the main (first discrete) one; each GPU section
        // then names its card.
        std::vector<size_t> shownGpus;
        for (size_t i = 0; i < g.hw.gpus.size(); ++i)
            if (GpuShown(g.hw, i)) shownGpus.push_back(i);
        const GpuSnapshot* gpu = shownGpus.size() == 1 ? &g.hw.gpus[shownGpus[0]] : PickGpu(g.hw);
        if (st.title) {
            v.title = gpu ? Widen(gpu->adapter.name) : L"FrameLens";
            std::transform(v.title.begin(), v.title.end(), v.title.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        }

        // Frames.
        const FrameSummary& a = g.fr.app;
        const bool live = HaveFrames() && a.active;
        v.showFps = st.fps;
        v.showFrametime = st.frametime;
        v.fps = live ? Num(a.currentFps, 0) : L"--";
        v.frametime = live ? Num(a.currentFrametimeMs, 1) : L"--";
        if (st.fps || st.frametime) v.frameNote = FrameProblem(true);
        if (live && st.graphs) {
            v.fpsHistory = g.fr.fpsSamples;   // the same samples AVG / MIN / MAX come from
            const auto& ft = g.fr.frametimeHistoryMs;
            const size_t n = std::min(ft.size(), kFrametimePoints);
            v.frametimeHistory.assign(ft.end() - static_cast<std::ptrdiff_t>(n), ft.end());
        }

        // Stats keep showing while the game is paused / alt-tabbed; they reset when the target changes.
        const bool stats = HaveFrames() && a.averageValid;
        const double scale = std::max(a.minMaxValid ? a.maxFps : 0.0, stats ? a.averageFps : 0.0) * 1.15;
        auto stat = [&](const wchar_t* label, bool valid, double fps, bool primary) {
            osd::StatRow r{ label, valid ? Num(fps, 0) : L"--", 0.f, primary };
            if (valid && scale > 0) r.bar = static_cast<float>(fps / scale);
            v.stats.push_back(std::move(r));
        };
        if (st.avgMaxMin) {
            stat(L"AVG", stats, a.averageFps, true);
            stat(L"MAX", stats && a.minMaxValid, a.maxFps, false);
            stat(L"MIN", stats && a.minMaxValid, a.minFps, false);
        }
        if (st.lows) {
            stat(L"1%", stats && a.low1Valid, a.low1Fps, false);
            stat(L"0.1%", stats && a.low01Valid, a.low01Fps, false);
        }

        // Hardware sections. "Hide values this PC can't read" drops Unsupported readings;
        // transient failures (--) still show so a value never silently disappears.
        auto shown = [&](bool on, const Reading& r) { return on && !(st.hideUnavailable && r.state == SensorState::Unsupported); };
        auto cell = [&](osd::Section& s, bool on, const Reading& r, int decimals, double mul, const wchar_t* unit, osd::Tone tone) {
            if (shown(on, r)) s.cells.push_back({ Val(r, decimals, mul), unit, r.ok() ? tone : osd::Tone::Muted });
        };
        auto headline = [&](osd::Section& s, bool on, const Reading& r, const std::vector<float>& hist) {
            if (!shown(on, r)) return;
            s.headline = true;
            s.head = { Val(r, 0), L"%", ToneOf(r, 70, 90) };
            s.history = hist;
        };
        auto add = [&](osd::Section&& s) { if (s.headline || !s.cells.empty() || !s.cores.empty()) v.sections.push_back(std::move(s)); };
        constexpr auto N = osd::Tone::Normal;

        static const GpuSnapshot kNoGpu = [] {   // all "--" when no GPU was found yet
            GpuSnapshot n;
            for (Reading* r : { &n.utilizationPct, &n.temperatureC, &n.coreClockMHz, &n.memoryClockMHz, &n.powerW, &n.fanPct, &n.dedicatedUsedBytes })
                *r = Reading::Failed();
            return n;
        }();
        // One section per shown GPU, in Windows' adapter order; numbered and named when there are several.
        static const std::vector<float> kNoHistory;
        auto gpuSection = [&](const GpuSnapshot& gp, const std::wstring& caption, const std::vector<float>& hist) {
            osd::Section gs{ L"GPU", caption, OverlayTheme().c.gpu };
            headline(gs, st.gpuLoad, gp.utilizationPct, hist);
            cell(gs, st.gpuTemp, gp.temperatureC, 0, 1, L"°C", ToneOf(gp.temperatureC, 60, 80));
            cell(gs, st.gpuClock, gp.coreClockMHz, 0, 1, L"MHz", N);
            cell(gs, st.gpuMemClock, gp.memoryClockMHz, 0, 1, L"MHz", N);
            cell(gs, st.gpuPower, gp.powerW, 0, 1, L"W", N);
            // Fan: RPM when the driver reports it (AMD, some Intel), else percent (NVIDIA).
            if (gp.fanRpm.state != SensorState::Unsupported) cell(gs, st.gpuFan, gp.fanRpm, 0, 1, L"RPM", N);
            else cell(gs, st.gpuFan, gp.fanPct, 0, 1, L"% FAN", N);
            cell(gs, st.gpuVram, gp.dedicatedUsedBytes, 1, kGiB, L"GB", N);
            add(std::move(gs));
        };
        if (g.hw.gpus.empty()) gpuSection(kNoGpu, L"", kNoHistory);
        for (size_t i : shownGpus)
            gpuSection(g.hw.gpus[i], shownGpus.size() > 1 ? GpuLabel(g.hw, i) : L"",
                       i < g.gpuHists.size() ? g.gpuHists[i] : kNoHistory);

        const CpuSnapshot& cpu = g.hw.cpu;
        osd::Section cs{ L"CPU", L"", OverlayTheme().c.cpu };
        headline(cs, st.cpuLoad, cpu.totalUtilizationPct, g.cpuHist);
        cell(cs, st.cpuTemp, cpu.packageTempC, 0, 1, L"°C", ToneOf(cpu.packageTempC, 75, 90));
        cell(cs, st.cpuClock, cpu.effectiveClockMHz, 0, 1, L"MHz", N);
        cell(cs, st.cpuPower, cpu.packagePowerW, 0, 1, L"W", N);
        // Every physical core: load (average of its threads) and/or clock (its fastest thread).
        if (st.cpuCoreLoad || st.cpuCoreClock) {
            cs.coreLoad = st.cpuCoreLoad;
            cs.coreClock = st.cpuCoreClock;
            for (const CoreReading& c : AggregateCores(cpu)) {
                osd::CoreCell cc;
                cc.label = Widen(c.label);
                if (st.cpuCoreLoad) {
                    cc.load = c.loadPct.ok() ? Num(c.loadPct.value, 0) + L"%" : Val(c.loadPct, 0);
                    cc.loadFraction = c.loadPct.ok() ? static_cast<float>(c.loadPct.value / 100.0) : 0.f;
                    cc.tone = c.loadPct.ok() ? ToneOf(c.loadPct, 70, 90) : osd::Tone::Muted;
                }
                if (st.cpuCoreClock)
                    cc.clock = c.clockMHz.ok() ? Num(c.clockMHz.value / 1000.0, 2) + (st.cpuCoreLoad ? L" GHz" : L"") : Val(c.clockMHz, 0);
                cs.cores.push_back(std::move(cc));
            }
        }
        add(std::move(cs));

        const MemorySnapshot& mem = g.hw.memory;
        osd::Section rs{ L"RAM", L"", OverlayTheme().c.ram };
        headline(rs, st.ramPct, mem.utilizationPct, g.ramHist);
        const std::wstring ramUnit = mem.totalBytes.ok() ? L"GB/" + Num(std::round(mem.totalBytes.value * kGiB), 0) + L"GB" : L"GB";
        cell(rs, st.ramUsed, mem.usedBytes, 1, kGiB, ramUnit.c_str(), N);
        add(std::move(rs));
        return v;
    }

    // ---------------------------------------------------------------- look
    bool DarkMode()
    {
        switch (g.settings.colorMode) {
        case ui::ColorMode::Dark: return true;
        case ui::ColorMode::Light: return false;
        default: return ui::FormWindow::SystemPrefersDark();
        }
    }

    const ui::Theme& AppTheme() { return ui::GetTheme(g.settings.style, DarkMode()); }

    bool WindowsHighContrast()
    {
        HIGHCONTRASTW hc{ sizeof hc };
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    }

    bool HighContrastActive()
    {
        switch (g.settings.highContrast) {
        case HighContrastMode::Off: return false;
        case HighContrastMode::On: return true;
        default: return WindowsHighContrast();
        }
    }

    // The overlay's style with high contrast applied, before the user's own colours.
    ui::Theme OverlayBaseTheme()
    {
        const ui::Style s = g.settings.overlayStyle < 0 ? g.settings.style : static_cast<ui::Style>(g.settings.overlayStyle);
        const ui::Theme& t = ui::GetTheme(s, DarkMode());
        return HighContrastActive() ? ui::HighContrast(t) : t;
    }

    // The colour a value colour setting replaces.
    uint32_t StyleColor(const ui::Palette& c, int OverlaySettings::* field)
    {
        if (field == &OverlaySettings::colorFrametime) return c.label;
        if (field == &OverlaySettings::colorGpu) return c.gpu;
        if (field == &OverlaySettings::colorCpu) return c.cpu;
        if (field == &OverlaySettings::colorRam) return c.ram;
        return c.panelText;   // values, FPS
    }

    // Kept in g so the renderer can hold a pointer to it; refreshed on every call.
    const ui::Theme& OverlayTheme()
    {
        ui::Theme t = OverlayBaseTheme();
        const OverlaySettings& st = g.settings;
        if (st.colorValues >= 0) t.c.panelText = static_cast<uint32_t>(st.colorValues);
        if (st.colorFrametime >= 0) t.c.label = t.c.labelBar = static_cast<uint32_t>(st.colorFrametime);
        if (st.colorGpu >= 0) t.c.gpu = static_cast<uint32_t>(st.colorGpu);
        if (st.colorCpu >= 0) t.c.cpu = static_cast<uint32_t>(st.colorCpu);
        if (st.colorRam >= 0) t.c.ram = static_cast<uint32_t>(st.colorRam);
        g.overlayTheme = t;
        return g.overlayTheme;
    }

    // ---------------------------------------------------------------- overlay window
    LRESULT CALLBACK OverlayProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        switch (m) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // never take focus from the game
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_DPICHANGED: return 0;                   // size is handled in PlaceOverlay
        case WM_CLOSE: return 0;                        // lives as long as the app; closed via the main window
        }
        return DefWindowProcW(h, m, w, l);
    }

    UINT MonitorDpi(HMONITOR mon)
    {
        using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
            GetProcAddress(LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "GetDpiForMonitor")));
        UINT x = 96, y = 96;
        if (fn) fn(mon, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y);
        return x;
    }

    void CreateRenderer()
    {
        g.renderer.Reset();
        g.rendererError.clear();
        g.renderer.SetTheme(OverlayTheme());
        if (!g.renderer.Create(g.overlay, false, g.rendererError)) g.renderer.Reset();
    }

    // The game's main window: its largest visible top-level window (games may also own launchers/splashes).
    HWND GameWindow(uint32_t pid)
    {
        struct Search { DWORD pid; HWND best; LONG area; } s{ pid, nullptr, 0 };
        if (!pid) return nullptr;
        EnumWindows([](HWND w, LPARAM p) -> BOOL {
            auto* s = reinterpret_cast<Search*>(p);
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            if (owner != s->pid || !IsWindowVisible(w) || IsIconic(w)) return TRUE;
            RECT r{};
            GetWindowRect(w, &r);
            const LONG area = (r.right - r.left) * (r.bottom - r.top);
            if (area > s->area) { s->area = area; s->best = w; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&s));
        return s.best;
    }

    // Where the overlay goes: the monitor picked in Customize overlay, else the followed game's
    // monitor (clicking a window on another screen doesn't move it), else the main monitor.
    HMONITOR OverlayMonitor()
    {
        if (HMONITOR picked = FindMonitor(g.settings.monitor)) return picked;
        if (HWND game = GameWindow(g.fr.targetPid)) return MonitorFromWindow(game, MONITOR_DEFAULTTOPRIMARY);
        if (g.overlayX != INT_MIN) return MonitorFromPoint(POINT{ g.overlayX, g.overlayY }, MONITOR_DEFAULTTOPRIMARY);   // stay put
        return MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }

    // The window is a fixed, transparent, click-through slot for the panel at the largest Size, so
    // resizing only redraws the panel inside it (see src/core/OverlayGeometry.h).
    constexpr int kMaxScalePct = 200;

    void DrawOverlay()
    {
        if (!g.renderer.Ok() || g.anchor.x == INT_MIN) return;
        const osd::OsdView view = BuildView();
        const SIZE panel = g.renderer.Measure(view);
        const SIZE base = g.renderer.BaseSize();
        const RECT& m = g.anchorMonitor;
        // Position + center-pivot Size + "never move the window when resizing": src/core/OverlayGeometry.
        const OverlayRects rects = ComputeOverlayRects(g.settings.position, { m.left, m.top, m.right, m.bottom },
                                                       g.anchor.x - m.left, g.anchor.y - m.top,
                                                       { base.cx, base.cy }, { panel.cx, panel.cy }, kMaxScalePct);
        const RECT slot{ rects.window.left, rects.window.top, rects.window.right, rects.window.bottom };
        const RECT pr{ rects.panel.left, rects.panel.top, rects.panel.right, rects.panel.bottom };

        RECT wr{};
        GetWindowRect(g.overlay, &wr);
        if (!EqualRect(&wr, &slot)) {   // rare: monitor, DPI or metric choice changed
            SetWindowPos(g.overlay, HWND_TOPMOST, slot.left, slot.top, slot.right - slot.left, slot.bottom - slot.top, SWP_NOACTIVATE);
            g.overlayX = slot.left;
            g.overlayY = slot.top;
        }
        const HRESULT hr = g.renderer.Draw(view, { pr.left - slot.left, pr.top - slot.top },
                                           { slot.right - slot.left, slot.bottom - slot.top });
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == D2DERR_RECREATE_TARGET) CreateRenderer();
    }

    // Once a second: keep the overlay on its monitor and on top, and match that monitor's DPI.
    void PlaceOverlay(bool force)
    {
        const ULONGLONG now = GetTickCount64();
        if (!force && now - g.lastPlaceMs < 1000) return;
        g.lastPlaceMs = now;

        HMONITOR mon = OverlayMonitor();
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(mon, &mi);
        const UINT dpi = MonitorDpi(mon);
        g.anchor = { mi.rcMonitor.left + static_cast<LONG>(g.offX * dpi / 96.f), mi.rcMonitor.top + static_cast<LONG>(g.offY * dpi / 96.f) };
        g.anchorMonitor = mi.rcMonitor;
        if (mon != g.anchorMonitorHandle) {
            // New monitor: move there first so the window takes that monitor's DPI.
            g.anchorMonitorHandle = mon;
            SetWindowPos(g.overlay, HWND_TOPMOST, g.anchor.x, g.anchor.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            g.overlayX = g.anchor.x;
            g.overlayY = g.anchor.y;
        }
        if (!g.renderer.Ok() || GetDpiForWindow(g.overlay) != g.renderer.Dpi()) CreateRenderer();
        // Re-assert "on top" over windows that became topmost later; DrawOverlay sizes the slot.
        SetWindowPos(g.overlay, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    void SetOverlayShown(bool shown)
    {
        g.overlayShown = shown;
        if (shown) { PlaceOverlay(true); DrawOverlay(); }
        else ShowWindow(g.overlay, SW_HIDE);
        g.form.Invalidate();
        RefreshTrayPanel();
    }

    // ---------------------------------------------------------------- icon (drawn at runtime: three green bars)
    HICON MakeIcon(int s)
    {
        BITMAPV5HEADER bi{};
        bi.bV5Size = sizeof bi;
        bi.bV5Width = s;
        bi.bV5Height = -s;
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        bi.bV5RedMask = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask = 0x000000FF;
        bi.bV5AlphaMask = 0xFF000000;
        void* bits = nullptr;
        HDC dc = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, dc);
        if (!color) return nullptr;

        auto* px = static_cast<uint32_t*>(bits);
        const float r = s * 0.2f;
        for (int y = 0; y < s; ++y)
            for (int x = 0; x < s; ++x) {
                const float cx = std::clamp(x + 0.5f, r, s - r), cy = std::clamp(y + 0.5f, r, s - r);
                const bool inside = std::hypot(x + 0.5f - cx, y + 0.5f - cy) <= r;
                px[y * s + x] = inside ? 0xFF141414u : 0u;
            }
        const float heights[] = { 0.32f, 0.52f, 0.72f };
        for (int b = 0; b < 3; ++b) {
            const int x0 = static_cast<int>(s * (0.18f + b * 0.23f)), x1 = static_cast<int>(s * (0.18f + b * 0.23f + 0.17f));
            const int y1 = static_cast<int>(s * 0.84f), y0 = static_cast<int>(y1 - s * heights[b]);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) px[y * s + x] = 0xFF4CD94Cu;
        }

        std::vector<uint8_t> maskBits(static_cast<size_t>((s + 15) / 16 * 2) * s, 0);
        HBITMAP mask = CreateBitmap(s, s, 1, 1, maskBits.data());
        ICONINFO ii{ TRUE, 0, 0, mask, color };
        HICON icon = CreateIconIndirect(&ii);
        DeleteObject(mask);
        DeleteObject(color);
        return icon;
    }

    // "Start with Windows": a per-user Run entry, written by FrameLens itself so it always belongs
    // to the signed-in user (the installer calls `framelens --autostart on|off` as that user).
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"FrameLens";

    bool AutostartEnabled()
    {
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }

    bool SetAutostart(bool on)
    {
        if (!on) {
            const LSTATUS st = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
            return st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND;
        }
        wchar_t exe[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;
        const std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        return RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                               static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    // `--autostart on|off`: set it and exit without starting the app. Returns -1 when absent.
    int AutostartCommand()
    {
        int argc = 0, result = -1;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i)
            if (!wcscmp(argv[i], L"--autostart")) result = SetAutostart(!wcscmp(argv[i + 1], L"on")) ? 0 : 1;
        if (argv) LocalFree(argv);
        return result;
    }

    // ---------------------------------------------------------------- tray icon
    void TrayTip(NOTIFYICONDATAW& nid)
    {
        std::wstring tip = L"FrameLens";
        if (HaveFrames() && g.fr.app.active)
            tip += L" — " + Num(g.fr.app.currentFps, 0) + L" FPS (" + Widen(g.fr.targetName) + L")";
        wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    }

    void TrayUpdate(DWORD op)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof nid;
        nid.hWnd = g.main;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
        nid.uCallbackMessage = WM_TRAY;
        nid.hIcon = g.iconSmall;
        TrayTip(nid);
        if (op == NIM_DELETE) { Shell_NotifyIconW(NIM_DELETE, &nid); g.trayAdded = false; return; }
        if (op == NIM_ADD) {
            g.trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
            nid.uVersion = NOTIFYICON_VERSION_4;
            if (g.trayAdded) Shell_NotifyIconW(NIM_SETVERSION, &nid);
            return;
        }
        if (g.trayAdded) Shell_NotifyIconW(NIM_MODIFY, &nid);
    }

    void ShowMain()
    {
        ShowWindow(g.main, IsIconic(g.main) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(g.main);
    }

    // "Follow": automatic, or one of the processes drawing frames right now (list rebuilt on open).
    // Shared by the main window's dropdown and the tray panel; entries map to g.followPids.
    std::vector<std::wstring> FollowItems()
    {
        std::vector<std::wstring> items{ L"Automatic — the game in focus" };
        g.followPids.assign(1, 0);
        bool lockedListed = false;
        for (const auto& p : g.fr.presenting) {
            items.push_back(Widen(p.name) + L"  ·  " + Num(p.fps, 0) + L" FPS  (pid " + std::to_wstring(p.pid) + L")");
            g.followPids.push_back(p.pid);
            lockedListed |= g.fr.targetLocked && p.pid == g.fr.targetPid;
        }
        if (g.fr.targetLocked && !lockedListed) {
            items.push_back(Widen(g.fr.targetName) + L"  (pid " + std::to_wstring(g.fr.targetPid) + L")  ·  not drawing now");
            g.followPids.push_back(g.fr.targetPid);
        }
        return items;
    }

    int FollowSelected()
    {
        const uint32_t want = g.fr.targetLocked ? g.fr.targetPid : 0;
        for (size_t i = 0; i < g.followPids.size(); ++i) if (g.followPids[i] == want) return static_cast<int>(i);
        return 0;
    }

    void FollowSelect(int i)
    {
        if (i >= 0 && i < static_cast<int>(g.followPids.size())) g.engine.SetTarget(g.followPids[static_cast<size_t>(i)]);
    }

    // The tray icon's quick-settings panel (stays open while settings change; see TrayPanel.h).
    void TrayPanel(POINT at)
    {
        auto m = std::make_shared<TrayPanelModel>();
        m->status = [] {
            if (!g.fr.targetPid) return std::wstring(L"Waiting for a game");
            std::wstring s = Widen(g.fr.targetName);
            if (g.fr.app.active) s += L"  ·  " + Num(g.fr.app.currentFps, 0) + L" FPS";
            return s;
        };
        m->overlayShown = [] { return g.overlayShown; };
        m->setOverlayShown = [](bool on) { SetOverlayShown(on); };
        if (const uint32_t key = g.settings.hotkeys[static_cast<int>(HotkeyAction::ToggleOverlay)]) m->overlayHotkey = ui::HotkeyText(key);
        m->follow = { FollowItems, FollowSelected, FollowSelect };
        m->monitor.items = [] {
            std::vector<std::wstring> v{ L"The game's monitor" };
            for (const auto& mon : ListMonitors()) v.push_back(mon.label);
            if (!g.settings.monitor.empty() && !FindMonitor(g.settings.monitor)) v.push_back(g.settings.monitor + L"  ·  not connected");
            return v;
        };
        m->monitor.selected = [] {
            if (g.settings.monitor.empty()) return 0;
            const auto mons = ListMonitors();
            for (size_t i = 0; i < mons.size(); ++i) if (mons[i].device == g.settings.monitor) return static_cast<int>(i) + 1;
            return static_cast<int>(mons.size()) + 1;   // the "not connected" entry
        };
        m->monitor.select = [](int i) {
            const auto mons = ListMonitors();
            if (i == 0) g.settings.monitor.clear();
            else if (i <= static_cast<int>(mons.size())) g.settings.monitor = mons[static_cast<size_t>(i - 1)].device;
            else return;
            SettingsChangedHere();
        };
        m->position.items = [] {
            std::vector<std::wstring> v;
            for (OverlayPosition p : kPositionOrder) v.push_back(PositionName(p));
            return v;
        };
        m->position.selected = [] {
            return static_cast<int>(std::find(std::begin(kPositionOrder), std::end(kPositionOrder), g.settings.position) - std::begin(kPositionOrder));
        };
        m->position.select = [](int i) { g.settings.position = kPositionOrder[i]; SettingsChangedHere(); };
        m->preset = [] { return g.settings.lastPreset; };
        m->setPreset = [](int i) { ApplyPreset(g.settings, static_cast<Preset>(i)); SettingsChangedHere(); };
        m->autostart = [] { return AutostartEnabled(); };
        m->setAutostart = [](bool on) { SetAutostart(on); };
        m->open = [] { ShowMain(); };
        m->customize = [] { OpenCustomize(); };
        m->resetStats = [] { g.engine.ResetStats(); };
        m->exit = [] { PostMessageW(g.main, WM_CLOSE, 0, 0); };
        ToggleTrayPanel(g.inst, g.main, AppTheme(), m, at);
    }

    // ---------------------------------------------------------------- main (taskbar) window
    // Applies the chosen style and dark/light mode to every window and the overlay.
    void ApplyThemes()
    {
        g.form.SetTheme(AppTheme());
        SetSettingsWindowTheme(AppTheme());
        SetTrayPanelTheme(AppTheme());
        g.renderer.SetTheme(OverlayTheme());
        if (g.overlayShown) DrawOverlay();
    }

    std::wstring OverlayStatus()
    {
        if (!g.rendererError.empty()) return L"cannot draw: " + g.rendererError;
        if (!g.overlayShown) return L"hidden";
        std::wstring where = PositionName(g.settings.position);
        std::transform(where.begin(), where.begin() + 1, where.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        std::wstring s = L"shown — " + where + (g.settings.monitor.empty() ? L" of the game's monitor" : L" of the chosen monitor");
        if (!g.settings.monitor.empty() && !FindMonitor(g.settings.monitor)) s += L" (not connected: using the game's)";
        return s;
    }

    // After any settings change: apply it everywhere (the caller saves).
    void SettingsChanged()
    {
        g.engine.SetWindowSamples(static_cast<size_t>(g.settings.windowSamples));
        ApplyThemes();
        if (g.overlayShown) { PlaceOverlay(true); DrawOverlay(); }
        g.form.Invalidate();
    }

    // ---------------------------------------------------------------- global hotkeys
    // Registered on the main window (system-wide, so they work while a game has focus); re-registered
    // whenever the settings change. Paused while Customize overlay records a new combination.
    void RegisterHotkeys()
    {
        for (int i = 0; i < kHotkeyCount; ++i) {
            UnregisterHotKey(g.main, kHotkeyIdBase + i);
            const uint32_t key = g.settings.hotkeys[i];
            g.hotkeyOk[i] = !g.hotkeysPaused && key &&
                            RegisterHotKey(g.main, kHotkeyIdBase + i, (key >> 16) | MOD_NOREPEAT, key & 0xFFFF);
        }
    }

    // A change made outside Customize overlay (tray panel, hotkeys): save, apply, repaint that window.
    void SettingsChangedHere()
    {
        SaveSettings(g.settings);
        SettingsChanged();
        RefreshSettingsWindow();
        RefreshTrayPanel();
    }

    void RunHotkey(HotkeyAction a)
    {
        switch (a) {
        case HotkeyAction::ToggleOverlay:
            SetOverlayShown(!g.overlayShown);
            return;
        case HotkeyAction::NextPosition: {
            const auto* it = std::find(std::begin(kPositionOrder), std::end(kPositionOrder), g.settings.position);
            const auto i = (it == std::end(kPositionOrder) ? 0 : (it - std::begin(kPositionOrder)) + 1) % kOverlayPositionCount;
            g.settings.position = kPositionOrder[i];
            break;
        }
        case HotkeyAction::NextPreset:
            ApplyPreset(g.settings, static_cast<Preset>((g.settings.lastPreset + 1) % kPresetCount));
            break;
        }
        if (!g.overlayShown) SetOverlayShown(true);   // switching something you can't see would be confusing
        SettingsChangedHere();
    }

    void OpenCustomize()
    {
        std::vector<GpuChoice> gpus;
        for (size_t i = 0; i < g.hw.gpus.size(); ++i) gpus.push_back({ GpuKey(g.hw, i), GpuLabel(g.hw, i) });
        ShowSettingsWindow(g.inst, g.main, AppTheme(), g.settings, std::move(gpus),
                           [](const ToggleDef& t) { return NotReadable(t, g.hw); },
                           [](const ColorDef& def) { return StyleColor(OverlayBaseTheme().c, def.field); },
                           [] { return OverlayBaseTheme().c.panel; },
                           [](bool pause) { g.hotkeysPaused = pause; RegisterHotkeys(); },
                           [](int i) {
                               return g.settings.hotkeys[i] && !g.hotkeyOk[i] && !g.hotkeysPaused ? std::wstring(L"used by another app")
                                                                                                   : std::wstring();
                           },
                           [] { RegisterHotkeys(); SettingsChanged(); });
    }

    MainModel BuildMainModel()
    {
        MainModel m;
        m.subtitle = L"FPS & hardware monitor  ·  v" FL_VERSION;
        m.followItems = FollowItems;
        m.followSelected = FollowSelected;
        m.followSelect = FollowSelect;
        m.followDisplay = [] {
            if (!g.fr.targetLocked) return std::wstring(L"Automatic — the game in focus");
            return Widen(g.fr.targetName) + L"  (pid " + std::to_wstring(g.fr.targetPid) + L")";
        };
        m.game = [] {
            if (!g.fr.targetPid) return std::wstring(L"none yet — start a game and click into its window, or pick it under Follow");
            return Widen(g.fr.targetName) + L"  (pid " + std::to_wstring(g.fr.targetPid) + L")" +
                   (g.fr.targetLocked ? L"  — picked" : L"  — automatic");
        };
        m.fps = [] {
            const FrameSummary& a = g.fr.app;
            if (!HaveFrames() || !a.active) return std::wstring(L"--");
            std::wstring s = Num(a.currentFps, 0) + L" FPS  ·  " + Num(a.currentFrametimeMs, 1) + L" ms";
            if (a.averageValid) s += L"  ·  avg " + Num(a.averageFps, 0);
            if (a.low1Valid) s += L"  ·  1% low " + Num(a.low1Fps, 0);
            return s;
        };
        m.capture = [] { return g.fr.status == FrameSourceStatus::Running ? std::wstring(L"running") : FrameProblem(false); };
        m.captureColor = [] { return g.fr.status == FrameSourceStatus::Running ? 0u : AppTheme().c.hot; };
        m.overlay = [] { return OverlayStatus(); };
        m.customize = [] { OpenCustomize(); };
        m.resetStats = [] { g.engine.ResetStats(); };
        m.toggleOverlay = [] { SetOverlayShown(!g.overlayShown); };
        m.overlayButtonText = [] { return std::wstring(g.overlayShown ? L"Hide overlay" : L"Show overlay"); };
        m.exit = [] { DestroyWindow(g.main); };
        m.isDark = [] { return DarkMode(); };
        m.toggleDarkLight = [] {
            g.settings.colorMode = DarkMode() ? ui::ColorMode::Light : ui::ColorMode::Dark;
            SaveSettings(g.settings);
            ApplyThemes();
        };
        return m;
    }

    void Tick()
    {
        g.engine.GetHardware(g.hw);
        g.engine.GetFrames(g.fr);

        if (g.hw.sequence != g.lastHwSeq) {
            g.lastHwSeq = g.hw.sequence;
            if (g.gpuHists.size() != g.hw.gpus.size()) g.gpuHists.assign(g.hw.gpus.size(), {});
            for (size_t i = 0; i < g.hw.gpus.size(); ++i)
                if (g.hw.gpus[i].utilizationPct.ok()) Push(g.gpuHists[i], g.hw.gpus[i].utilizationPct.value, kLoadPoints);
            if (g.hw.cpu.totalUtilizationPct.ok()) Push(g.cpuHist, g.hw.cpu.totalUtilizationPct.value, kLoadPoints);
            if (g.hw.memory.utilizationPct.ok()) Push(g.ramHist, g.hw.memory.utilizationPct.value, kLoadPoints);
        }

        if (g.overlayShown) { PlaceOverlay(false); DrawOverlay(); }

        ++g.ticks;
        if (g.ticks % 2 == 0 && IsWindowVisible(g.main) && !IsIconic(g.main)) g.form.Invalidate();
        if (g.ticks % 4 == 0) { TrayUpdate(NIM_MODIFY); RefreshTrayPanel(); }   // its status line
    }

    // Messages the main window handles besides its controls: timer, hotkeys, tray icon,
    // Explorer restarts and Windows' dark/light switch.
    bool MainMessage(UINT m, WPARAM w, LPARAM l, LRESULT& result)
    {
        result = 0;
        if (m == g.taskbarCreated && m != 0) { TrayUpdate(NIM_ADD); return true; }
        switch (m) {
        case WM_TIMER:
            if (w != kTickTimer) return false;
            Tick();
            return true;
        case WM_HOTKEY:
            if (w >= static_cast<WPARAM>(kHotkeyIdBase) && w < static_cast<WPARAM>(kHotkeyIdBase + kHotkeyCount)) {
                RunHotkey(static_cast<HotkeyAction>(w - kHotkeyIdBase));
                return true;
            }
            return false;
        case WM_TRAY:
            switch (LOWORD(l)) {
            case WM_CONTEXTMENU: TrayPanel(POINT{ GET_X_LPARAM(w), GET_Y_LPARAM(w) }); break;
            case NIN_SELECT: case NIN_KEYSELECT: case WM_LBUTTONDBLCLK: ShowMain(); break;
            }
            return true;
        case WM_SETTINGCHANGE:
            if (l && !wcscmp(reinterpret_cast<const wchar_t*>(l), L"ImmersiveColorSet") && g.settings.colorMode == ui::ColorMode::System)
                ApplyThemes();   // Windows switched between dark and light
            if (w == SPI_SETHIGHCONTRAST && g.settings.highContrast == HighContrastMode::FollowWindows)
                ApplyThemes();   // Windows high contrast turned on or off
            return false;
        }
        return false;
    }

    void ParseArgs(EngineConfig& cfg)
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i < argc; ++i) {
            const wchar_t* a = argv[i];
            const bool hasNext = i + 1 < argc;
            if (!wcscmp(a, L"--pid") && hasNext) cfg.fixedPid = static_cast<uint32_t>(wcstoul(argv[++i], nullptr, 10));
            else if (!wcscmp(a, L"--x") && hasNext) g.offX = static_cast<float>(_wtoi(argv[++i]));
            else if (!wcscmp(a, L"--y") && hasNext) g.offY = static_cast<float>(_wtoi(argv[++i]));
        }
        if (argv) LocalFree(argv);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    if (const int rc = AutostartCommand(); rc >= 0) return rc;

    // One instance: a second launch brings the running window to the front.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kInstanceMutex);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kMainClass, nullptr)) {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC (offscreen previews) and shell
    g.inst = inst;

    LoadSettings(g.settings);
    EngineConfig cfg;
    cfg.windowSamples = static_cast<size_t>(g.settings.windowSamples);
    ParseArgs(cfg);
    std::string diag;
    if (!g.engine.Start(cfg, diag)) {
        MessageBoxW(nullptr, (L"FrameLens could not start: " + Widen(diag)).c_str(), L"FrameLens", MB_ICONERROR);
        return 1;
    }

    const UINT sysDpi = GetDpiForSystem();
    g.iconBig = MakeIcon(GetSystemMetricsForDpi(SM_CXICON, sysDpi));
    g.iconSmall = MakeIcon(GetSystemMetricsForDpi(SM_CXSMICON, sysDpi));

    WNDCLASSEXW oc{};
    oc.cbSize = sizeof oc;
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = inst;
    oc.lpszClassName = kOverlayClass;
    RegisterClassExW(&oc);
    // Click-through, never focused, no taskbar button of its own (same styles as the M3.0 spike).
    g.overlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP |
                                WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                kOverlayClass, L"FrameLens overlay", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, inst, nullptr);
    // A layered window stays invisible until its attributes are set (documented). Some drivers show
    // the DirectComposition content anyway; Intel's only while a game forces recomposition.
    SetLayeredWindowAttributes(g.overlay, 0, 255, LWA_ALPHA);

    // Main window: custom-drawn, themed; its class name lets a second launch find it.
    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.form.message = MainMessage;
    g.form.onDestroy = [] {
        KillTimer(g.main, kTickTimer);
        TrayUpdate(NIM_DELETE);
        PostQuitMessage(0);
    };
    g.form.SetTheme(AppTheme());
    BuildMainForm(g.form, BuildMainModel());
    ui::FormWindow::Options o;
    o.className = kMainClass;
    o.title = L"FrameLens";
    o.widthDip = kMainFormWidth;
    o.heightDip = kMainFormHeight;
    o.iconBig = g.iconBig;
    o.iconSmall = g.iconSmall;
    if (!g.form.Create(inst, o) || !g.overlay) {
        g.engine.Stop();
        return 1;
    }
    g.main = g.form.Hwnd();
    SetTimer(g.main, kTickTimer, kTickMs, nullptr);
    TrayUpdate(NIM_ADD);
    RegisterHotkeys();
    ShowWindow(g.main, show);
    UpdateWindow(g.main);
    SetOverlayShown(true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g.engine.Stop();
    g.renderer.Reset();
    if (g.overlay) DestroyWindow(g.overlay);
    for (HICON i : { g.iconBig, g.iconSmall }) if (i) DestroyIcon(i);
    CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
