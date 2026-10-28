// FrameLens — per physical core readings from per-thread readings (platform independent,
// unit-tested). Windows reports load and clock per logical processor (thread); with SMT one core
// has two threads. A core's load is the average of its threads, its clock the fastest thread's.
// Labels: C1..Cn, or P1.. / E1.. on hybrid CPUs (performance and efficiency cores).
// SPDX-License-Identifier: MIT
#pragma once
#include "Types.h"
#include <string>
#include <vector>

namespace fl
{
    struct CoreReading
    {
        std::string label;      // "C3", "P1", "E4"
        bool efficiency = false;
        Reading loadPct;
        Reading clockMHz;
    };

    std::vector<CoreReading> AggregateCores(const CpuSnapshot& cpu);
}
