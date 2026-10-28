// FrameLens — frame-time statistics.
// SPDX-License-Identifier: MIT
#include "FrameStats.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace fl
{
    namespace
    {
        constexpr size_t kMinFramesLow1 = 100;
        constexpr size_t kMinFramesLow01 = 1000;
        // Frame times beyond this are treated as pauses (alt-tab, loading) and not recorded.
        constexpr double kMaxPlausibleFrametimeMs = 5000.0;

        size_t ClampWindow(size_t n) { return std::clamp(n, kMinWindowSamples, kMaxWindowSamples); }
    }

    FrameStats::FrameStats(size_t capacity, size_t windowSamples)
        : ring_(capacity < 16 ? 16 : capacity), samples_(ClampWindow(windowSamples))
    {
    }

    void FrameStats::Reset()
    {
        head_ = 0;
        count_ = 0;
        samples_.Clear();
        sampleStarts_.clear();
        slotStart_ = -1.0;
        slotFrames_ = 0;
        slotMs_ = 0.0;
    }

    void FrameStats::SetWindowSamples(size_t n)
    {
        samples_.SetCapacity(ClampWindow(n));
        while (sampleStarts_.size() > samples_.Size()) sampleStarts_.pop_front();
    }

    const FrameStats::Frame& FrameStats::At(size_t i) const noexcept
    {
        const size_t cap = ring_.size();
        const size_t oldest = (head_ + cap - count_) % cap;
        return ring_[(oldest + i) % cap];
    }

    void FrameStats::AddFrame(double frametimeMs, double timestampSec)
    {
        if (!(frametimeMs > 0.0) || frametimeMs > kMaxPlausibleFrametimeMs || !std::isfinite(frametimeMs)) {
            // Restart the sample so a pause doesn't produce a fake low sample.
            slotStart_ = -1.0;
            slotFrames_ = 0;
            slotMs_ = 0.0;
            return;
        }

        ring_[head_] = { static_cast<float>(frametimeMs), timestampSec };
        head_ = (head_ + 1) % ring_.size();
        if (count_ < ring_.size()) ++count_;

        // Close the sample once it spans kSampleIntervalSec of game time; this frame starts the next.
        if (slotStart_ < 0.0) {
            slotStart_ = timestampSec;
        }
        else if (timestampSec - slotStart_ >= kSampleIntervalSec && slotFrames_ > 0) {
            samples_.Push(slotFrames_ * 1000.0 / slotMs_);
            sampleStarts_.push_back(slotStart_);
            while (sampleStarts_.size() > samples_.Size()) sampleStarts_.pop_front();
            slotStart_ = timestampSec;
            slotFrames_ = 0;
            slotMs_ = 0.0;
        }
        ++slotFrames_;
        slotMs_ += frametimeMs;
    }

    FrameSummary FrameStats::Compute(double nowSec, LowMethod method, double currentWindowSec, double stallTimeoutSec) const
    {
        FrameSummary s;
        if (count_ == 0) return s;

        s.hasFrames = true;

        const Frame& newest = At(count_ - 1);
        s.active = (nowSec - newest.t) <= stallTimeoutSec;

        // Rolling window: AVG / MIN / MAX straight from the samples.
        s.samples = samples_.Size();
        if (!samples_.Empty()) {
            s.averageValid = s.minMaxValid = true;
            s.averageFps = samples_.Average();
            s.averageFrametimeMs = 1000.0 / s.averageFps;
            s.minFps = samples_.Min();
            s.maxFps = samples_.Max();
        }

        // The frames the window covers (from its oldest sample to the newest frame) + the current
        // window. Both end at the newest frame, not at nowSec: ETW delivers frames in batches up
        // to ~1 s late, so a window ending "now" would often be empty while the game runs smoothly.
        const double spanStart = sampleStarts_.empty() ? -1e300 : sampleStarts_.front();
        const double curStart = newest.t - currentWindowSec;
        double spanMs = 0.0, curMs = 0.0;
        size_t spanN = 0, curN = 0;
        for (size_t i = count_; i-- > 0;) {
            const Frame& f = At(i);
            if (f.t < spanStart) break;   // the ring is in time order
            spanMs += f.ms;
            ++spanN;
            if (f.t >= curStart) { curMs += f.ms; ++curN; }
        }
        s.frameCount = spanN;
        s.durationSec = spanMs / 1000.0;

        if (s.active) {
            if (curN > 0) {
                s.currentFps = curN * 1000.0 / curMs;
                s.currentFrametimeMs = curMs / curN;
            }
            else { // fewer than one frame per window (very low FPS): use last frame
                s.currentFps = 1000.0 / newest.ms;
                s.currentFrametimeMs = newest.ms;
            }
        }

        if (spanN >= kMinFramesLow1) {
            scratch_.resize(spanN);
            for (size_t i = 0; i < spanN; ++i) scratch_[i] = At(count_ - spanN + i).ms;

            auto lowFps = [&](double fraction) -> double {
                const size_t n = scratch_.size();
                if (method == LowMethod::Percentile) {
                    // Frame time at the (1 - fraction) percentile, nearest-rank.
                    size_t rank = static_cast<size_t>(std::ceil((1.0 - fraction) * n));
                    rank = std::clamp<size_t>(rank, 1, n) - 1;
                    std::nth_element(scratch_.begin(), scratch_.begin() + rank, scratch_.end());
                    return 1000.0 / scratch_[rank];
                }
                // AverageOfWorst: slowest k frames, time-weighted.
                const size_t k = std::max<size_t>(1, static_cast<size_t>(std::floor(fraction * n)));
                std::nth_element(scratch_.begin(), scratch_.begin() + (k - 1), scratch_.end(), std::greater<float>());
                double sum = 0.0;
                for (size_t i = 0; i < k; ++i) sum += scratch_[i];
                return k * 1000.0 / sum;
            };

            s.low1Valid = true;
            s.low1Fps = lowFps(0.01);
            if (spanN >= kMinFramesLow01) {
                s.low01Valid = true;
                s.low01Fps = lowFps(0.001);
            }
        }
        return s;
    }

    void FrameStats::CopyRecentFrametimes(std::vector<float>& out, size_t maxCount) const
    {
        const size_t n = std::min(maxCount, count_);
        out.resize(n);
        for (size_t i = 0; i < n; ++i) out[i] = At(count_ - n + i).ms;
    }

    void FrameStats::CopySamples(std::vector<float>& out) const
    {
        out.resize(samples_.Size());
        for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<float>(samples_.At(i));
    }
}
