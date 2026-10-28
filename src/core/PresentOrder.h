// FrameLens — puts one swapchain's presents back in time order before frame intervals are taken.
//
// PresentData delivers presents when their tracking ends, not when they were made. A present it
// loses track of (common with variable refresh / FreeSync, where some flips arrive without the
// events it expects) is handed over later than the presents after it. Taking each interval from
// "the previous one delivered" then skips frames and doubles gaps: the FPS drops and jumps. So
// timestamps wait here `hold` ticks behind the newest one seen and are released sorted.
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace fl
{
    class PresentOrder
    {
    public:
        // A timestamp at or before the last one released arrived too late to fit in; it is dropped.
        void Push(uint64_t t, bool flag = false)
        {
            if (t == 0 || t <= last_) { ++dropped_; return; }
            pending_.push_back({ t, flag });
            if (t > newest_) newest_ = t;
        }

        // Releases, oldest first, every timestamp at least `hold` ticks older than the newest one
        // (all of them with `flush`, e.g. when the swapchain went quiet).
        // emit(t, intervalTicks, flag): intervalTicks is 0 for the first timestamp ever released.
        template <class Emit>
        void Release(uint64_t hold, bool flush, Emit&& emit)
        {
            if (pending_.empty()) return;
            std::sort(pending_.begin(), pending_.end(), [](const Item& a, const Item& b) { return a.t < b.t; });
            const uint64_t limit = (flush || newest_ < hold) ? newest_ : newest_ - hold;
            size_t n = 0;
            for (; n < pending_.size() && pending_[n].t <= limit; ++n) {
                const Item& it = pending_[n];
                if (it.t == last_) { ++dropped_; continue; }   // the same present delivered twice
                emit(it.t, last_ ? it.t - last_ : 0, it.flag);
                last_ = it.t;
            }
            pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
        }

        size_t Pending() const noexcept { return pending_.size(); }
        uint64_t Dropped() const noexcept { return dropped_; }

    private:
        struct Item { uint64_t t; bool flag; };
        std::vector<Item> pending_;
        uint64_t newest_ = 0, last_ = 0, dropped_ = 0;
    };
}
