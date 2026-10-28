// FrameLens — per physical core readings.
// SPDX-License-Identifier: MIT
#include "CpuCores.h"
#include <algorithm>

namespace fl
{
    namespace
    {
        // Average (load) or maximum (clock) of a core's thread readings. Unsupported when the
        // per-thread series doesn't exist, Unavailable when none of the core's threads has a value.
        Reading Combine(const std::vector<Reading>& perThread, const std::vector<uint16_t>& threads, bool average)
        {
            if (perThread.empty()) return Reading::NotSupported();
            double sum = 0.0, best = 0.0;
            int n = 0;
            for (uint16_t t : threads) {
                if (t >= perThread.size() || !perThread[t].ok()) continue;
                sum += perThread[t].value;
                best = n ? std::max(best, perThread[t].value) : perThread[t].value;
                ++n;
            }
            if (!n) return Reading::Failed();
            return Reading::Of(average ? sum / n : best);
        }
    }

    std::vector<CoreReading> AggregateCores(const CpuSnapshot& cpu)
    {
        std::vector<CoreReading> out;
        if (cpu.cores.empty()) return out;
        uint8_t top = 0, bottom = 255;
        for (const auto& c : cpu.cores) { top = std::max(top, c.efficiencyClass); bottom = std::min(bottom, c.efficiencyClass); }
        const bool hybrid = top != bottom;
        int p = 0, e = 0;
        for (const auto& c : cpu.cores) {
            CoreReading r;
            r.efficiency = hybrid && c.efficiencyClass != top;
            r.label = !hybrid ? "C" + std::to_string(++p) : r.efficiency ? "E" + std::to_string(++e) : "P" + std::to_string(++p);
            r.loadPct = Combine(cpu.coreUtilizationPct, c.threads, true);
            r.clockMHz = Combine(cpu.coreClockMHz, c.threads, false);
            out.push_back(std::move(r));
        }
        return out;
    }
}
