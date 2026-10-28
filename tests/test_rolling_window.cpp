// FrameLens — RollingWindow unit tests (dependency-free; runs on any OS).
// SPDX-License-Identifier: MIT
#include "../src/core/RollingWindow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace fl;

static int gFailures = 0;
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::printf("FAIL %s:%d  %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, _a, _b); ++gFailures; } } while (0)
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++gFailures; } } while (0)

static void TestFillAndEvict()
{
    RollingWindow w(3);
    CHECK(w.Empty());
    w.Push(10);
    w.Push(20);
    CHECK(w.Size() == 2);
    CHECK_NEAR(w.Average(), 15, 1e-12);
    w.Push(30);
    w.Push(40);   // 10 leaves
    CHECK(w.Size() == 3);
    CHECK_NEAR(w.At(0), 20, 1e-12);
    CHECK_NEAR(w.At(2), 40, 1e-12);
    CHECK_NEAR(w.Average(), 30, 1e-12);
    CHECK_NEAR(w.Min(), 20, 1e-12);
    CHECK_NEAR(w.Max(), 40, 1e-12);
}

static void TestMaxLeavesWindow()
{
    // A spike (e.g. a 2000 FPS loading screen) is MAX only while it is in the window.
    RollingWindow w(4);
    for (double v : { 60.0, 2000.0, 60.0, 60.0 }) w.Push(v);
    CHECK_NEAR(w.Max(), 2000, 1e-12);
    w.Push(61);
    w.Push(59);   // 2000 leaves
    CHECK_NEAR(w.Max(), 61, 1e-12);
    CHECK_NEAR(w.Min(), 59, 1e-12);
}

static void TestMatchesBruteForce()
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> d(0, 500);
    for (size_t cap : { 1, 2, 7, 100 }) {
        RollingWindow w(cap);
        std::vector<double> all;
        for (int i = 0; i < 2000; ++i) {
            const double v = (i % 50 == 0) ? 60.0 : d(rng);   // includes duplicates
            w.Push(v);
            all.push_back(v);
            const size_t n = std::min(all.size(), cap);
            double sum = 0, lo = 1e300, hi = -1e300;
            for (size_t k = all.size() - n; k < all.size(); ++k) { sum += all[k]; lo = std::min(lo, all[k]); hi = std::max(hi, all[k]); }
            CHECK(w.Size() == n);
            CHECK_NEAR(w.Average(), sum / static_cast<double>(n), 1e-9);
            CHECK_NEAR(w.Min(), lo, 0);
            CHECK_NEAR(w.Max(), hi, 0);
        }
    }
}

static void TestSetCapacityKeepsNewest()
{
    RollingWindow w(5);
    for (int i = 1; i <= 5; ++i) w.Push(i * 10.0);   // 10..50
    w.SetCapacity(2);
    CHECK(w.Size() == 2);
    CHECK_NEAR(w.At(0), 40, 1e-12);
    CHECK_NEAR(w.At(1), 50, 1e-12);
    CHECK_NEAR(w.Min(), 40, 1e-12);
    w.SetCapacity(10);   // growing keeps everything
    CHECK(w.Size() == 2);
    w.Push(5);
    CHECK_NEAR(w.Min(), 5, 1e-12);
    CHECK_NEAR(w.Average(), 95.0 / 3.0, 1e-12);
}

static void TestNoDrift()
{
    // Millions of add/subtract steps: the periodic re-sum keeps the average exact.
    RollingWindow w(100);
    for (int i = 0; i < 2000000; ++i) w.Push((i % 2) ? 1e9 + 0.1 : 0.3);
    for (int i = 0; i < 100; ++i) w.Push(60.0);
    CHECK_NEAR(w.Average(), 60.0, 1e-9);
}

int main()
{
    TestFillAndEvict();
    TestMaxLeavesWindow();
    TestMatchesBruteForce();
    TestSetCapacityKeepsNewest();
    TestNoDrift();
    if (gFailures == 0) std::printf("All RollingWindow tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
