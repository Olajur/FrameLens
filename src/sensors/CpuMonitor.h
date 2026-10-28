// FrameLens — CPU utilization and effective clock via PDH (no driver, no admin).
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/Types.h"
#include "../platform/PdhQuery.h"
#include <memory>

namespace fl
{
    class CpuMonitor
    {
    public:
        CpuMonitor();
        bool Initialize();          // safe to call once; returns false if PDH is unusable
        void Sample(CpuSnapshot& out);

    private:
        struct CoreKey { int group; int index; };
        std::unique_ptr<win::PdhQuery> pdh_;
        int cTotalUtil_ = -1, cCoreUtil_ = -1;
        int cTotalPerf_ = -1, cTotalFreq_ = -1;
        int cCorePerf_ = -1, cCoreFreq_ = -1;
        std::string name_;
        uint32_t logical_ = 0;
        std::vector<CpuCore> cores_;
        std::vector<win::PdhInstanceValue> tmpUtil_, tmpPerf_, tmpFreq_;
    };
}
