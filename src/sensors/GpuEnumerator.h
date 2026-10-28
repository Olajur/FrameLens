// FrameLens — discovers hardware GPU adapters through DXGI.
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/Types.h"
#include <vector>

namespace fl
{
    // Returns hardware adapters only (software/WARP adapters are skipped).
    // Adapters are de-duplicated by LUID.
    std::vector<GpuAdapterInfo> EnumerateGpuAdapters();
}
