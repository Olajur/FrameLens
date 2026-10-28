// FrameLens — vendor-neutral GPU utilization and memory via the WDDM
// "GPU Engine" / "GPU Adapter Memory" performance counters (same data Task
// Manager shows). Works on NVIDIA, AMD and Intel with WDDM 2.x drivers.
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/Types.h"
#include "../platform/PdhQuery.h"
#include "GpuInstanceName.h"
#include <memory>

namespace fl
{
    struct GpuPdhSample
    {
        Reading busiestEnginePct;   // max over engines of summed per-process utilization
        Reading engine3dPct;
        Reading dedicatedUsedBytes;
        Reading sharedUsedBytes;
    };

    class GpuPdhMonitor
    {
    public:
        bool Initialize();
        // Collects once per call; results for every adapter in `adapters` order.
        void Sample(const std::vector<GpuAdapterInfo>& adapters, std::vector<GpuPdhSample>& out);

    private:
        std::unique_ptr<win::PdhQuery> pdh_;
        int cEngine_ = -1, cDedicated_ = -1, cShared_ = -1;
        std::vector<win::PdhInstanceValue> tmp_;
    };
}
