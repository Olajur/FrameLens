// FrameLens — fixed-size rolling window of values.
// SPDX-License-Identifier: MIT
#include "RollingWindow.h"
#include <algorithm>

namespace fl
{
    RollingWindow::RollingWindow(size_t capacity)
        : buf_(std::max<size_t>(1, capacity))
    {
    }

    void RollingWindow::Clear()
    {
        head_ = size_ = 0;
        sum_ = 0.0;
        minQ_.clear();
        maxQ_.clear();
    }

    void RollingWindow::SetCapacity(size_t capacity)
    {
        capacity = std::max<size_t>(1, capacity);
        if (capacity == buf_.size()) return;
        const size_t keep = std::min(size_, capacity);
        std::vector<double> newest(keep);
        for (size_t i = 0; i < keep; ++i) newest[i] = At(size_ - keep + i);
        buf_.assign(capacity, 0.0);
        Clear();
        for (double v : newest) Push(v);
    }

    void RollingWindow::Push(double value)
    {
        if (size_ == buf_.size()) {   // full: the oldest value leaves the window
            const uint64_t oldestSeq = pushed_ - size_;
            sum_ -= buf_[head_];
            if (!minQ_.empty() && minQ_.front().seq == oldestSeq) minQ_.pop_front();
            if (!maxQ_.empty() && maxQ_.front().seq == oldestSeq) maxQ_.pop_front();
            --size_;
        }

        buf_[head_] = value;
        head_ = (head_ + 1) % buf_.size();
        ++size_;
        sum_ += value;

        // A new value makes every larger (smaller) value before it irrelevant for the min (max).
        while (!minQ_.empty() && minQ_.back().value >= value) minQ_.pop_back();
        minQ_.push_back({ pushed_, value });
        while (!maxQ_.empty() && maxQ_.back().value <= value) maxQ_.pop_back();
        maxQ_.push_back({ pushed_, value });
        ++pushed_;

        // Once per turn of the ring: re-add from scratch so adding/subtracting never drifts.
        if (head_ == 0) {
            sum_ = 0.0;
            for (size_t i = 0; i < size_; ++i) sum_ += At(i);
        }
    }
}
