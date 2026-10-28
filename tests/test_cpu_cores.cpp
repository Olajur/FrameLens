// FrameLens — per-core aggregation tests (dependency-free; runs on any OS).
// SPDX-License-Identifier: MIT
#include "../src/core/CpuCores.h"
#include <cmath>
#include <cstdio>

using namespace fl;

static int gFailures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++gFailures; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::printf("FAIL %s:%d  %s = %.4f, expected %.4f\n", __FILE__, __LINE__, #a, _a, _b); ++gFailures; } } while (0)

static void TestSmtCoresAverageLoadAndTakeFastestClock()
{
    // 2 cores x 2 threads; Windows orders threads 0,1 = core 1 and 2,3 = core 2.
    CpuSnapshot cpu;
    cpu.cores = { { { 0, 1 }, 0 }, { { 2, 3 }, 0 } };
    cpu.coreUtilizationPct = { Reading::Of(80), Reading::Of(20), Reading::Of(10), Reading::Of(0) };
    cpu.coreClockMHz = { Reading::Of(4500), Reading::Of(4700), Reading::Of(3000), Reading::Of(3100) };
    const auto cores = AggregateCores(cpu);
    CHECK(cores.size() == 2);
    CHECK(cores[0].label == "C1" && cores[1].label == "C2");
    CHECK_NEAR(cores[0].loadPct.value, 50, 1e-9);
    CHECK_NEAR(cores[1].loadPct.value, 5, 1e-9);
    CHECK_NEAR(cores[0].clockMHz.value, 4700, 1e-9);
    CHECK_NEAR(cores[1].clockMHz.value, 3100, 1e-9);
}

static void TestHybridLabels()
{
    // Two P-cores (efficiency class 1, SMT) and three E-cores (class 0, one thread each).
    CpuSnapshot cpu;
    cpu.cores = { { { 0, 1 }, 1 }, { { 2, 3 }, 1 }, { { 4 }, 0 }, { { 5 }, 0 }, { { 6 }, 0 } };
    cpu.coreUtilizationPct.assign(7, Reading::Of(10));
    const auto cores = AggregateCores(cpu);
    CHECK(cores.size() == 5);
    CHECK(cores[0].label == "P1" && cores[1].label == "P2");
    CHECK(cores[2].label == "E1" && cores[4].label == "E3");
    CHECK(!cores[0].efficiency && cores[2].efficiency);
    CHECK(cores[0].clockMHz.state == SensorState::Unsupported);   // no per-thread clocks at all
}

static void TestMissingReadings()
{
    CpuSnapshot cpu;
    cpu.cores = { { { 0, 1 }, 0 }, { { 2, 9 }, 0 } };   // thread 9 doesn't exist in the series
    cpu.coreUtilizationPct = { Reading::Failed(), Reading::Failed(), Reading::Of(40) };
    const auto cores = AggregateCores(cpu);
    CHECK(cores[0].loadPct.state == SensorState::Unavailable);   // both threads failed: "--"
    CHECK_NEAR(cores[1].loadPct.value, 40, 1e-9);                // the valid thread counts
    CHECK(AggregateCores(CpuSnapshot{}).empty());
}

int main()
{
    TestSmtCoresAverageLoadAndTakeFastestClock();
    TestHybridLabels();
    TestMissingReadings();
    if (gFailures == 0) std::printf("All CpuCores tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
