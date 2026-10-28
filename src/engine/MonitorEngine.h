// FrameLens — monitoring engine: owns sensor + frame threads and publishes snapshots.
//
// Threads:
//   sensor thread — polls CPU/RAM/GPU every `sensorIntervalMs` (default 1000 ms)
//   frame thread  — drains ETW frames every 50 ms, recomputes stats every `statsIntervalMs`
//   ETW thread    — owned by the frame source, blocked in ProcessTrace()
// Consumers (CLI, overlay, UI) only copy published snapshots; they never block collection.
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/FrameStats.h"
#include "../core/Types.h"
#include "../frames/IFrameSource.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace fl
{
    struct EngineConfig
    {
        uint32_t sensorIntervalMs = 1000;
        uint32_t statsIntervalMs = 250;
        uint32_t fixedPid = 0;              // 0 = follow the foreground window
        LowMethod lowMethod = LowMethod::Percentile;
        bool enableFrames = true;
        size_t graphPoints = 300;
        size_t windowSamples = kDefaultWindowSamples;   // rolling AVG / MIN / MAX window (0.25 s samples)
    };

    // A process that presented frames during the last second (candidates for the target picker).
    struct PresentingProcess
    {
        uint32_t pid = 0;
        std::string name;
        double fps = 0.0;
    };

    struct FrameSnapshot
    {
        FrameSourceStatus status = FrameSourceStatus::NotStarted;
        std::string statusText;
        uint32_t targetPid = 0;
        std::string targetName;
        bool targetLocked = false;        // chosen by the user (SetTarget / --pid), not following focus
        std::vector<PresentingProcess> presenting;   // busiest first, excludes FrameLens itself
        FrameSummary app;                 // application presents ("FPS" in most tools)
        FrameSummary displayed;           // frames that reached the screen
        double generatedShareLastSec = 0; // fraction of displayed frames from frame generation / repeats (last ~1 s)
        double lostShareLastSec = 0;      // fraction of presents PresentData lost track of (last ~1 s; still counted as frames)
        std::vector<float> frametimeHistoryMs;  // app frame times, oldest first
        std::vector<float> fpsSamples;          // the rolling window's FPS samples, oldest first (FPS graph)
    };

    class MonitorEngine
    {
    public:
        MonitorEngine();
        ~MonitorEngine();
        bool Start(const EngineConfig& cfg, std::string& diagnostics);
        void Stop();

        void GetHardware(HardwareSnapshot& out) const;
        void GetFrames(FrameSnapshot& out) const;
        // Follow this process from now on; 0 = follow the focused game again. Applied within ~50 ms.
        void SetTarget(uint32_t pid);
        // Clears AVG / MIN / MAX / lows and the graph history for the current target (e.g. after
        // a loading screen). Applied within ~50 ms.
        void ResetStats();
        // Resizes the rolling AVG / MIN / MAX window (samples of 0.25 s), keeping the newest samples.
        void SetWindowSamples(size_t n);
        std::vector<std::string> GpuDiagnostics() const;

    private:
        void SensorLoop();
        void FrameLoop();

        EngineConfig cfg_;
        std::atomic<bool> running_{ false };
        std::atomic<int64_t> targetRequest_{ -1 };   // -1 = none pending, else pid (0 = automatic)
        std::atomic<bool> resetRequest_{ false };
        std::atomic<size_t> windowRequest_{ 0 };    // 0 = none pending
        void* stopEvent_ = nullptr;   // HANDLE

        std::thread sensorThread_, frameThread_;

        mutable std::mutex hwMutex_;
        HardwareSnapshot hw_;
        std::vector<std::string> gpuDiag_;

        mutable std::mutex frMutex_;
        FrameSnapshot fr_;

        std::unique_ptr<IFrameSource> frameSource_;
    };
}
