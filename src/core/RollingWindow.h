// FrameLens — fixed-size rolling window of values (platform independent, unit-tested).
//
// A ring buffer: the newest value overwrites the oldest, nothing is shifted. Average, min and
// max are kept up to date as values come and go, so reading them is O(1) and adding a value is
// O(1) amortized, independent of the window size:
//   - average: a running sum (recomputed once per full turn of the ring, so rounding never drifts)
//   - min/max: monotonic deques ("sliding window minimum"): each holds only the values that can
//     still become the min (max), in window order, so the front is always the answer.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace fl
{
    class RollingWindow
    {
    public:
        explicit RollingWindow(size_t capacity = 100);

        // Changes the window size, keeping the newest values that still fit.
        void SetCapacity(size_t capacity);
        void Clear();
        void Push(double value);

        size_t Size() const noexcept { return size_; }
        size_t Capacity() const noexcept { return buf_.size(); }
        bool Empty() const noexcept { return size_ == 0; }

        // Valid only when !Empty().
        double Average() const noexcept { return sum_ / static_cast<double>(size_); }
        double Min() const noexcept { return minQ_.front().value; }
        double Max() const noexcept { return maxQ_.front().value; }

        // i = 0 is the oldest value.
        double At(size_t i) const noexcept { return buf_[(head_ + buf_.size() - size_ + i) % buf_.size()]; }

    private:
        struct Entry { uint64_t seq; double value; };

        std::vector<double> buf_;
        size_t head_ = 0;           // next write position
        size_t size_ = 0;
        uint64_t pushed_ = 0;       // sequence number of the next value
        double sum_ = 0.0;
        std::deque<Entry> minQ_, maxQ_;
    };
}
