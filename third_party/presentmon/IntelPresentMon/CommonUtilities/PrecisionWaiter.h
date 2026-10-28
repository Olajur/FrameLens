// FrameLens compatibility shim (not part of upstream PresentMon).
// Upstream uses a high-resolution waitable timer + spin. PresentData only uses
// it to pace ETL *file* playback, which FrameLens never does (realtime only),
// so a Sleep-based implementation is sufficient.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace pmon::util
{
    int64_t GetCurrentTimestamp() noexcept;           // QPC ticks
    double  GetTimestampPeriodSeconds() noexcept;     // seconds per QPC tick
    double  TimestampDeltaToSeconds(int64_t start, int64_t end, double period) noexcept;

    class PrecisionWaiter
    {
    public:
        static constexpr double standardWaitBuffer = 0.001;
        explicit PrecisionWaiter(double defaultWaitBuffer = standardWaitBuffer) noexcept
            : defaultWaitBuffer_(defaultWaitBuffer) {}
        PrecisionWaiter(const PrecisionWaiter&) = delete;
        PrecisionWaiter& operator=(const PrecisionWaiter&) = delete;
        double Wait(double seconds, bool alertable = false) noexcept;
        double WaitUnbuffered(double seconds, bool alertable = false) noexcept { return Wait(seconds, alertable); }
        double WaitWithBuffer(double seconds, double, bool alertable = false) noexcept { return Wait(seconds, alertable); }
    private:
        double defaultWaitBuffer_;
    };
}
