// FrameLens — monitoring engine.
// SPDX-License-Identifier: MIT
#include "MonitorEngine.h"
#include "../sensors/CpuMonitor.h"
#include "../sensors/GpuMonitor.h"
#include "../sensors/MemoryMonitor.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <unordered_map>

namespace fl
{
    namespace
    {
        std::string ProcessName(uint32_t pid)
        {
            std::string result = "pid " + std::to_string(pid);
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return result;
            wchar_t path[MAX_PATH]{};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, path, &len)) {
                const wchar_t* file = path;
                for (const wchar_t* p = path; *p; ++p) if (*p == L'\\') file = p + 1;
                const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
                if (n > 1) { result.assign(n - 1, '\0'); WideCharToMultiByte(CP_UTF8, 0, file, -1, result.data(), n, nullptr, nullptr); }
            }
            CloseHandle(h);
            return result;
        }

        uint32_t ForegroundPid()
        {
            HWND w = GetForegroundWindow();
            if (!w) return 0;
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            return pid;
        }
    }

    MonitorEngine::MonitorEngine() = default;
    MonitorEngine::~MonitorEngine() { Stop(); }

    bool MonitorEngine::Start(const EngineConfig& cfg, std::string& diagnostics)
    {
        if (running_) return true;
        cfg_ = cfg;
        if (cfg_.sensorIntervalMs < 100) cfg_.sensorIntervalMs = 100;   // guard against self-inflicted overhead
        if (cfg_.statsIntervalMs < 50) cfg_.statsIntervalMs = 50;

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) { diagnostics = "CreateEvent failed"; return false; }
        running_ = true;

        sensorThread_ = std::thread(&MonitorEngine::SensorLoop, this);
        if (cfg_.enableFrames) frameThread_ = std::thread(&MonitorEngine::FrameLoop, this);
        return true;
    }

    void MonitorEngine::Stop()
    {
        if (!running_) return;
        running_ = false;
        SetEvent(static_cast<HANDLE>(stopEvent_));
        if (sensorThread_.joinable()) sensorThread_.join();
        if (frameThread_.joinable()) frameThread_.join();
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }

    void MonitorEngine::GetHardware(HardwareSnapshot& out) const
    {
        std::lock_guard lk(hwMutex_);
        out = hw_;
    }

    void MonitorEngine::GetFrames(FrameSnapshot& out) const
    {
        std::lock_guard lk(frMutex_);
        out = fr_;
    }

    void MonitorEngine::SetTarget(uint32_t pid) { targetRequest_ = pid; }
    void MonitorEngine::ResetStats() { resetRequest_ = true; }
    void MonitorEngine::SetWindowSamples(size_t n) { windowRequest_ = n ? n : 1; }

    std::vector<std::string> MonitorEngine::GpuDiagnostics() const
    {
        std::lock_guard lk(hwMutex_);
        return gpuDiag_;
    }

    void MonitorEngine::SensorLoop()
    {
        SetThreadDescription(GetCurrentThread(), L"FrameLens sensors");
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        CpuMonitor cpu;
        MemoryMonitor mem;
        GpuMonitor gpu;
        cpu.Initialize();
        gpu.Initialize();
        {
            std::lock_guard lk(hwMutex_);
            gpuDiag_ = gpu.Diagnostics();
        }

        HardwareSnapshot local;
        uint64_t seq = 0;
        while (running_) {
            local.sequence = ++seq;
            local.timestampSec = NowSec();
            cpu.Sample(local.cpu);
            mem.Sample(local.memory);
            gpu.Sample(local.gpus);
            {
                std::lock_guard lk(hwMutex_);
                hw_ = local;   // copy is small (a few KB); readers never wait on sensors
            }
            if (WaitForSingleObject(static_cast<HANDLE>(stopEvent_), cfg_.sensorIntervalMs) == WAIT_OBJECT_0) break;
        }
        if (SUCCEEDED(coHr)) CoUninitialize();
    }

    void MonitorEngine::FrameLoop()
    {
        SetThreadDescription(GetCurrentThread(), L"FrameLens frames");

        frameSource_ = CreateEtwFrameSource();
        std::string err;
        const bool started = frameSource_->Start(err);
        {
            std::lock_guard lk(frMutex_);
            fr_.status = frameSource_->Status();
            fr_.statusText = started ? "ETW frame capture running" : err;
        }
        if (!started) { frameSource_.reset(); return; }

        FrameStats appStats(32768, cfg_.windowSamples), dispStats(32768, cfg_.windowSamples);
        std::vector<FrameSample> samples;
        samples.reserve(4096);

        uint32_t locked = cfg_.fixedPid;   // 0 = follow the foreground process
        uint32_t target = locked;
        std::string targetName = target ? ProcessName(target) : std::string();
        uint64_t swapChain = 0;
        std::unordered_map<uint32_t, double> lastPresentByPid;      // pid -> last present time
        std::unordered_map<uint64_t, uint32_t> presentsBySwap;       // target swapchains, current 1 s window
        std::unordered_map<uint32_t, uint32_t> presentsByPid;        // all processes, current 1 s window
        std::unordered_map<uint32_t, std::string> names;             // pid -> exe name cache
        std::vector<PresentingProcess> presenting;
        const uint32_t self = GetCurrentProcessId();
        double windowStart = NowSec(), lastStats = 0.0, lastFgCheck = 0.0;
        uint32_t dispCount = 0, genCount = 0, appCount = 0, lostCount = 0;
        double genShare = 0.0, lostShare = 0.0;

        auto resetTarget = [&](uint32_t pid) {
            target = pid;
            targetName = pid ? ProcessName(pid) : std::string();
            swapChain = 0;
            presentsBySwap.clear();
            appStats.Reset();
            dispStats.Reset();
        };

        while (running_) {
            samples.clear();
            frameSource_->Drain(samples);
            const double now = NowSec();

            for (const auto& s : samples)
                if (s.kind == FrameKind::AppPresent) { lastPresentByPid[s.pid] = s.timeSec; ++presentsByPid[s.pid]; }

            if (const size_t n = windowRequest_.exchange(0)) {
                appStats.SetWindowSamples(n);
                dispStats.SetWindowSamples(n);
            }
            if (resetRequest_.exchange(false)) {
                appStats.Reset();
                dispStats.Reset();
            }

            // User choice from the target picker: lock onto a process, or go back to automatic.
            if (const int64_t req = targetRequest_.exchange(-1); req >= 0) {
                locked = static_cast<uint32_t>(req);
                if (locked && locked != target) resetTarget(locked);
                lastFgCheck = 0.0;   // automatic: re-evaluate the foreground right away
            }

            // Auto-target: follow the foreground process, but only once it is actually presenting.
            // (Focusing a console or the desktop keeps the last game selected.) ETW delivers frames
            // in ~1 s batches, so "recently presenting" allows 3 s.
            if (!locked && now - lastFgCheck > 0.25) {
                lastFgCheck = now;
                const uint32_t fg = ForegroundPid();
                if (fg && fg != target && fg != self) {
                    auto it = lastPresentByPid.find(fg);
                    if (it != lastPresentByPid.end() && now - it->second < 3.0) resetTarget(fg);
                }
            }

            for (const auto& s : samples) {
                if (s.pid != target) continue;
                if (s.kind == FrameKind::AppPresent) ++presentsBySwap[s.swapChain];
                if (swapChain == 0 && s.kind == FrameKind::AppPresent) swapChain = s.swapChain;
                if (s.swapChain != swapChain) continue;
                if (s.kind == FrameKind::AppPresent) {
                    ++appCount;
                    if (s.lost) ++lostCount;
                    if (s.intervalMs > 0) appStats.AddFrame(s.intervalMs, s.timeSec);
                }
                else {
                    ++dispCount;
                    if (s.generated) ++genCount;
                    if (s.intervalMs > 0) dispStats.AddFrame(s.intervalMs, s.timeSec);
                }
            }

            // Once per second: follow the busiest swapchain of the target (games may own several,
            // e.g. launcher + game), and update the generated-frame share.
            if (now - windowStart >= 1.0) {
                uint64_t best = swapChain; uint32_t bestN = 0;
                for (const auto& [sc, n] : presentsBySwap) if (n > bestN) { best = sc; bestN = n; }
                const uint32_t curN = presentsBySwap.count(swapChain) ? presentsBySwap[swapChain] : 0;
                if (best != swapChain && bestN > 2 * curN + 5) {
                    swapChain = best;
                    appStats.Reset();
                    dispStats.Reset();
                }
                presentsBySwap.clear();
                genShare = dispCount ? static_cast<double>(genCount) / dispCount : 0.0;
                dispCount = genCount = 0;
                lostShare = appCount ? static_cast<double>(lostCount) / appCount : 0.0;
                appCount = lostCount = 0;

                // Candidates for the target picker: everything that presented in this window.
                presenting.clear();
                for (const auto& [pid, n] : presentsByPid) {
                    if (pid == self) continue;
                    auto nm = names.find(pid);
                    if (nm == names.end()) nm = names.emplace(pid, ProcessName(pid)).first;
                    presenting.push_back({ pid, nm->second, n / (now - windowStart) });
                }
                std::sort(presenting.begin(), presenting.end(),
                          [](const PresentingProcess& a, const PresentingProcess& b) { return a.fps > b.fps; });
                presentsByPid.clear();
                windowStart = now;

                // Drop stale pids.
                for (auto it = lastPresentByPid.begin(); it != lastPresentByPid.end();) {
                    if (now - it->second > 60.0) { names.erase(it->first); it = lastPresentByPid.erase(it); }
                    else ++it;
                }
            }

            if (now - lastStats >= cfg_.statsIntervalMs / 1000.0) {
                lastStats = now;
                FrameSnapshot snap;
                snap.status = frameSource_->Status();
                snap.statusText = "ETW frame capture running";
                snap.targetPid = target;
                snap.targetName = targetName;
                snap.targetLocked = locked != 0;
                snap.presenting = presenting;
                snap.app = appStats.Compute(now, cfg_.lowMethod);
                snap.displayed = dispStats.Compute(now, cfg_.lowMethod);
                snap.generatedShareLastSec = genShare;
                snap.lostShareLastSec = lostShare;
                appStats.CopyRecentFrametimes(snap.frametimeHistoryMs, cfg_.graphPoints);
                appStats.CopySamples(snap.fpsSamples);
                std::lock_guard lk(frMutex_);
                fr_ = std::move(snap);
            }

            if (WaitForSingleObject(static_cast<HANDLE>(stopEvent_), 50) == WAIT_OBJECT_0) break;
        }
        frameSource_->Stop();
        frameSource_.reset();
    }
}
