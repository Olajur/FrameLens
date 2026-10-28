// FrameLens — memory monitor.
// SPDX-License-Identifier: MIT
#include "MemoryMonitor.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fl
{
    void MemoryMonitor::Sample(MemorySnapshot& out)
    {
        MEMORYSTATUSEX ms{};
        ms.dwLength = sizeof(ms);
        if (!GlobalMemoryStatusEx(&ms)) {
            out.totalBytes = out.usedBytes = out.availableBytes = out.utilizationPct = Reading::Failed();
            return;
        }
        const double total = static_cast<double>(ms.ullTotalPhys);
        const double avail = static_cast<double>(ms.ullAvailPhys);
        out.totalBytes = Reading::Of(total);
        out.availableBytes = Reading::Of(avail);
        out.usedBytes = Reading::Of(total - avail);
        out.utilizationPct = Reading::Of(total > 0 ? (total - avail) * 100.0 / total : 0.0);
    }
}
