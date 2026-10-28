// FrameLens — parser for WDDM GPU performance-counter instance names (portable, unit-tested).
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace fl
{
    // Examples:
    //   GPU Engine:          "pid_1234_luid_0x00000000_0x0000D1B8_phys_0_eng_3_engtype_3D"
    //   GPU Adapter Memory:  "luid_0x00000000_0x0000D1B8_phys_0"
    struct EngineInstanceKey
    {
        int32_t luidHigh = 0;
        uint32_t luidLow = 0;
        int phys = 0;
        int eng = -1;
        bool is3d = false;
    };

    bool ParseGpuInstanceName(const wchar_t* name, EngineInstanceKey& key, bool requireEngine);
}
