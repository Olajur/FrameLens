// FrameLens — system RAM via GlobalMemoryStatusEx.
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/Types.h"

namespace fl
{
    class MemoryMonitor
    {
    public:
        void Sample(MemorySnapshot& out);
    };
}
