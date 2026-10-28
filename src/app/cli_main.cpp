// FrameLens CLI — live console view of the monitoring core (Phase 2 MVP host).
//
//   framelens-cli [--interval <ms>] [--pid <pid>] [--low pct|avg] [--no-frames] [--json]
//
// --json prints one JSON object per second (for validation against PresentMon CSV / Task Manager).
// SPDX-License-Identifier: MIT
#include "../engine/MonitorEngine.h"
#include "../core/CpuCores.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace fl;

namespace
{
    std::atomic<bool> gQuit{ false };

    BOOL WINAPI OnCtrl(DWORD) { gQuit = true; return TRUE; }

    std::string Fmt(const Reading& r, const char* fmt, double scale = 1.0)
    {
        if (r.state == SensorState::Unsupported) return "n/a";
        if (r.state == SensorState::Unavailable) return "--";
        char b[64];
        std::snprintf(b, sizeof b, fmt, r.value * scale);
        return b;
    }

    std::string FmtFps(bool valid, double v)
    {
        if (!valid) return "--";
        char b[32];
        std::snprintf(b, sizeof b, "%.1f", v);
        return b;
    }

    std::string JsonReading(const Reading& r, double scale = 1.0)
    {
        if (!r.ok()) return "null";
        char b[48];
        std::snprintf(b, sizeof b, "%.3f", r.value * scale);
        return b;
    }

    std::string Sparkline(const std::vector<float>& ms, size_t width)
    {
        static const char* bars[] = { " ", "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
                                      "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88" };
        if (ms.empty()) return {};
        const size_t n = std::min(width, ms.size());
        float hi = 0.f;
        for (size_t i = ms.size() - n; i < ms.size(); ++i) hi = std::max(hi, ms[i]);
        if (hi <= 0.f) return {};
        std::string s;
        for (size_t i = ms.size() - n; i < ms.size(); ++i) s += bars[static_cast<int>(ms[i] / hi * 8.f + 0.5f)];
        return s;
    }

    const char* StatusName(FrameSourceStatus s)
    {
        switch (s) {
        case FrameSourceStatus::Running: return "running";
        case FrameSourceStatus::AccessDenied: return "access denied";
        case FrameSourceStatus::Failed: return "failed";
        case FrameSourceStatus::NotBuilt: return "not built";
        default: return "starting";
        }
    }

    void PrintConsole(const HardwareSnapshot& hw, const FrameSnapshot& fr, const std::vector<std::string>& diag)
    {
        std::string o;
        o.reserve(4096);
        o += "\x1b[H\x1b[J";   // home + clear
        o += "FrameLens monitor  (Ctrl+C to quit)\n\n";

        o += "FRAMES  [" + std::string(StatusName(fr.status)) + "]";
        if (fr.targetPid) o += "  target: " + fr.targetName + " (pid " + std::to_string(fr.targetPid) + ")";
        o += "\n";
        if (fr.status != FrameSourceStatus::Running) {
            o += "  " + fr.statusText + "\n";
        }
        else if (!fr.targetPid) {
            o += "  Waiting: focus a running game (or pass --pid).\n";
        }
        else {
            const auto& a = fr.app;
            const auto& d = fr.displayed;
            o += "  App FPS   now " + FmtFps(a.active, a.currentFps) + "  avg " + FmtFps(a.averageValid, a.averageFps) +
                 "  min " + FmtFps(a.minMaxValid, a.minFps) + "  max " + FmtFps(a.minMaxValid, a.maxFps) +
                 "  1% " + FmtFps(a.low1Valid, a.low1Fps) + "  0.1% " + FmtFps(a.low01Valid, a.low01Fps) + "\n";
            o += "  Frametime now " + FmtFps(a.active, a.currentFrametimeMs) + " ms  avg " + FmtFps(a.averageValid, a.averageFrametimeMs) + " ms\n";
            o += "  Displayed now " + FmtFps(d.active, d.currentFps) + "  avg " + FmtFps(d.averageValid, d.averageFps);
            char g[96]; std::snprintf(g, sizeof g, "  generated/repeated %.0f%%  tracking lost %.0f%%",
                                      fr.generatedShareLastSec * 100.0, fr.lostShareLastSec * 100.0);
            o += g;
            o += "\n  " + Sparkline(fr.frametimeHistoryMs, 100) + "\n";
        }

        o += "\nCPU  " + hw.cpu.name + "  (" + std::to_string(hw.cpu.logicalProcessors) + " threads)\n";
        o += "  Load " + Fmt(hw.cpu.totalUtilizationPct, "%.0f%%") + "  Clock " + Fmt(hw.cpu.effectiveClockMHz, "%.0f MHz") +
             "  Temp " + Fmt(hw.cpu.packageTempC, "%.0f C") + "  Power " + Fmt(hw.cpu.packagePowerW, "%.0f W") + "\n";
        // Physical cores: load % (average of the core's threads) @ clock MHz (fastest thread).
        const auto cores = AggregateCores(hw.cpu);
        o += "  Cores (" + std::to_string(cores.size()) + " physical):";
        for (size_t i = 0; i < cores.size(); ++i) {
            if (i % 8 == 0) o += "\n   ";
            o += " " + cores[i].label + " " + Fmt(cores[i].loadPct, "%3.0f%%") + "@" + Fmt(cores[i].clockMHz, "%.0f");
        }
        o += "\n";

        o += "\nRAM  " + Fmt(hw.memory.usedBytes, "%.1f", 1.0 / (1ull << 30)) + " / " + Fmt(hw.memory.totalBytes, "%.1f GiB", 1.0 / (1ull << 30)) +
             "  (" + Fmt(hw.memory.utilizationPct, "%.0f%%") + ")\n";

        for (const auto& g : hw.gpus) {
            o += "\nGPU  " + g.adapter.name + "  [" + g.telemetrySource + "]\n";
            o += "  Load " + Fmt(g.utilizationPct, "%.0f%%") + "  3D " + Fmt(g.utilization3dPct, "%.0f%%") +
                 "  MemCtl " + Fmt(g.memoryControllerPct, "%.0f%%") + "\n";
            o += "  Temp " + Fmt(g.temperatureC, "%.0f C") + "  Hotspot " + Fmt(g.hotspotTempC, "%.0f C") +
                 "  Mem " + Fmt(g.memoryTempC, "%.0f C") + "  Power " + Fmt(g.powerW, "%.1f W") + "  Fan " + Fmt(g.fanPct, "%.0f%%") + " / " + Fmt(g.fanRpm, "%.0f RPM") + "\n";
            o += "  Core " + Fmt(g.coreClockMHz, "%.0f MHz") + "  Mem " + Fmt(g.memoryClockMHz, "%.0f MHz") + "\n";
            o += "  VRAM " + Fmt(g.dedicatedUsedBytes, "%.2f", 1.0 / (1ull << 30)) + " / " + Fmt(g.dedicatedTotalBytes, "%.2f GiB", 1.0 / (1ull << 30)) +
                 "  Shared " + Fmt(g.sharedUsedBytes, "%.2f GiB", 1.0 / (1ull << 30)) + "\n";
        }
        if (!diag.empty()) {
            o += "\nNotes:\n";
            for (const auto& d : diag) o += "  - " + d + "\n";
        }
        o += "\n(n/a = not supported on this hardware/build, -- = temporarily unavailable)\n";
        std::fwrite(o.data(), 1, o.size(), stdout);
        std::fflush(stdout);
    }

    void PrintJson(const HardwareSnapshot& hw, const FrameSnapshot& fr)
    {
        auto f = [](bool v, double x) { char b[32]; if (!v) return std::string("null"); std::snprintf(b, sizeof b, "%.3f", x); return std::string(b); };
        std::string o = "{\"t\":" + f(true, hw.timestampSec);
        o += ",\"cpu_load\":" + JsonReading(hw.cpu.totalUtilizationPct) + ",\"cpu_mhz\":" + JsonReading(hw.cpu.effectiveClockMHz);
        o += ",\"ram_used_gib\":" + JsonReading(hw.memory.usedBytes, 1.0 / (1ull << 30));
        o += ",\"gpus\":[";
        for (size_t i = 0; i < hw.gpus.size(); ++i) {
            const auto& g = hw.gpus[i];
            if (i) o += ",";
            o += "{\"load\":" + JsonReading(g.utilizationPct) + ",\"temp_c\":" + JsonReading(g.temperatureC) +
                 ",\"power_w\":" + JsonReading(g.powerW) + ",\"core_mhz\":" + JsonReading(g.coreClockMHz) +
                 ",\"vram_used_gib\":" + JsonReading(g.dedicatedUsedBytes, 1.0 / (1ull << 30)) + "}";
        }
        o += "],\"frames\":{\"pid\":" + std::to_string(fr.targetPid) +
             ",\"fps\":" + f(fr.app.active, fr.app.currentFps) + ",\"avg\":" + f(fr.app.averageValid, fr.app.averageFps) +
             ",\"low1\":" + f(fr.app.low1Valid, fr.app.low1Fps) + ",\"low01\":" + f(fr.app.low01Valid, fr.app.low01Fps) +
             ",\"frametime_ms\":" + f(fr.app.active, fr.app.currentFrametimeMs) +
             ",\"displayed_fps\":" + f(fr.displayed.active, fr.displayed.currentFps) + "}}\n";
        std::fwrite(o.data(), 1, o.size(), stdout);
        std::fflush(stdout);
    }
}

int main(int argc, char** argv)
{
    EngineConfig cfg;
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--interval") && i + 1 < argc) cfg.sensorIntervalMs = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(a, "--pid") && i + 1 < argc) cfg.fixedPid = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(a, "--low") && i + 1 < argc) cfg.lowMethod = !std::strcmp(argv[++i], "avg") ? LowMethod::AverageOfWorst : LowMethod::Percentile;
        else if (!std::strcmp(a, "--no-frames")) cfg.enableFrames = false;
        else if (!std::strcmp(a, "--json")) json = true;
        else {
            std::printf("usage: framelens-cli [--interval ms] [--pid pid] [--low pct|avg] [--no-frames] [--json]\n");
            return 2;
        }
    }

    SetConsoleOutputCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleCtrlHandler(OnCtrl, TRUE);

    MonitorEngine engine;
    std::string diag;
    if (!engine.Start(cfg, diag)) {
        std::fprintf(stderr, "failed to start: %s\n", diag.c_str());
        return 1;
    }

    HardwareSnapshot hw;
    FrameSnapshot fr;
    while (!gQuit) {
        Sleep(json ? 1000 : 500);
        engine.GetHardware(hw);
        engine.GetFrames(fr);
        if (json) PrintJson(hw, fr);
        else PrintConsole(hw, fr, engine.GpuDiagnostics());
    }
    engine.Stop();
    return 0;
}
