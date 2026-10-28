// FrameLens — frame-time statistics (platform independent, unit-tested).
// SPDX-License-Identifier: MIT
#pragma once
#include "RollingWindow.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace fl
{
    // How "1% low" / "0.1% low" are computed. Tools disagree, so this is explicit.
    enum class LowMethod : uint8_t
    {
        // FPS equivalent of the 99th / 99.9th percentile frame time (PresentMon/OCAT style).
        Percentile,
        // Average FPS of the slowest 1% / 0.1% of frames (time-weighted).
        AverageOfWorst,
    };

    // AVG / MIN / MAX come from a rolling window of FPS samples: every kSampleIntervalSec of game
    // frames (by frame timestamps, so ETW's batched delivery doesn't matter) becomes one sample,
    // the FPS of those frames. The window keeps the newest N samples (default 100 = ~25 s);
    // older ones drop out, so a loading-screen burst only counts while it is in the window.
    constexpr double kSampleIntervalSec = 0.25;
    constexpr size_t kDefaultWindowSamples = 100;
    constexpr size_t kMinWindowSamples = 10, kMaxWindowSamples = 1000;

    struct FrameSummary
    {
        bool hasFrames = false;        // any frame recorded
        bool active = false;           // a frame arrived within the "stall" timeout
        uint64_t frameCount = 0;       // frames in the span the window covers
        double durationSec = 0.0;      // sum of their frame times

        double currentFps = 0.0;       // over the short "current" window
        double currentFrametimeMs = 0.0;

        // From the rolling window of FPS samples. Samples cover equal stretches of game time, so
        // their plain average equals the time-weighted average (frames / time).
        size_t samples = 0;            // samples in the window (0 until the first 0.25 s is complete)
        bool averageValid = false;
        double averageFps = 0.0;
        double averageFrametimeMs = 0.0;
        bool minMaxValid = false;
        double minFps = 0.0;           // lowest sample in the window
        double maxFps = 0.0;           // highest sample in the window

        // Per-frame, over the frames of the span the window covers (1% of 100 samples would
        // just be the MIN, so lows need the individual frames).
        bool low1Valid = false;        // needs >= 100 frames
        double low1Fps = 0.0;
        bool low01Valid = false;       // needs >= 1000 frames
        double low01Fps = 0.0;
    };

    class FrameStats
    {
    public:
        // capacity: number of frame times retained for lows / history.
        // windowSamples: size of the rolling FPS window (clamped to kMin..kMaxWindowSamples).
        explicit FrameStats(size_t capacity = 32768, size_t windowSamples = kDefaultWindowSamples);

        void Reset();
        // Changes the window size, keeping the newest samples.
        void SetWindowSamples(size_t n);
        size_t WindowSamples() const noexcept { return samples_.Capacity(); }

        // frametimeMs: time between this frame and the previous one (same swapchain).
        // timestampSec: monotonic time of this frame.
        void AddFrame(double frametimeMs, double timestampSec);

        // currentWindowSec is measured back from the newest frame; stallTimeoutSec from nowSec.
        // The 3 s stall default covers ETW's ~1 s batched delivery plus present-completion delay.
        FrameSummary Compute(double nowSec,
                             LowMethod method = LowMethod::Percentile,
                             double currentWindowSec = 0.5,
                             double stallTimeoutSec = 3.0) const;

        // Most recent frame times (oldest first), at most maxCount, for graphs.
        void CopyRecentFrametimes(std::vector<float>& out, size_t maxCount) const;
        // The FPS samples in the window (oldest first), for the FPS graph.
        void CopySamples(std::vector<float>& out) const;

        size_t size() const noexcept { return count_; }

    private:
        struct Frame { float ms; double t; };
        const Frame& At(size_t i) const noexcept;   // 0 = oldest

        std::vector<Frame> ring_;
        size_t head_ = 0;     // next write position
        size_t count_ = 0;

        // Rolling FPS window, and when each of its samples began (for the lows' frame span).
        RollingWindow samples_;
        std::deque<double> sampleStarts_;
        // The sample being filled.
        double slotStart_ = -1.0;
        uint32_t slotFrames_ = 0;
        double slotMs_ = 0.0;

        mutable std::vector<float> scratch_;
    };
}
