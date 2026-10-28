// FrameLens — GPU counter instance-name parser tests.
// SPDX-License-Identifier: MIT
#include "../src/sensors/GpuInstanceName.h"
#include <cstdio>

using namespace fl;
static int gFail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++gFail; } } while (0)

int main()
{
    EngineInstanceKey k;
    CHECK(ParseGpuInstanceName(L"pid_1234_luid_0x00000000_0x0000D1B8_phys_0_eng_3_engtype_3D", k, true));
    CHECK(k.luidHigh == 0 && k.luidLow == 0xD1B8 && k.phys == 0 && k.eng == 3 && k.is3d);

    CHECK(ParseGpuInstanceName(L"pid_8_luid_0x00000001_0x0001A2B3_phys_1_eng_12_engtype_VideoDecode", k, true));
    CHECK(k.luidHigh == 1 && k.luidLow == 0x1A2B3 && k.phys == 1 && k.eng == 12 && !k.is3d);

    CHECK(ParseGpuInstanceName(L"pid_8_luid_0xFFFFFFFF_0x00000010_phys_0_eng_0_engtype_3D_1", k, true));
    CHECK(k.luidHigh == -1 && k.is3d);

    CHECK(ParseGpuInstanceName(L"pid_8_luid_0x0_0x10_phys_0_eng_0_engtype_3DCompute", k, true));
    CHECK(!k.is3d); // not the 3D engine

    CHECK(ParseGpuInstanceName(L"luid_0x00000000_0x0000C0F5_phys_0", k, false));
    CHECK(k.luidLow == 0xC0F5 && k.eng == -1);
    CHECK(!ParseGpuInstanceName(L"luid_0x00000000_0x0000C0F5_phys_0", k, true));

    CHECK(!ParseGpuInstanceName(L"_Total", k, false));
    CHECK(!ParseGpuInstanceName(L"luid_0xZZ", k, false));
    CHECK(!ParseGpuInstanceName(nullptr, k, false));

    if (!gFail) std::printf("All GPU instance-name tests passed.\n");
    return gFail ? 1 : 0;
}
