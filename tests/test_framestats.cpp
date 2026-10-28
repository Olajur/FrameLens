// FrameLens — FrameStats unit tests (dependency-free; runs on any OS).
// SPDX-License-Identifier: MIT
#include "../src/core/FrameStats.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace fl;

static int gFailures = 0;
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::printf("FAIL %s:%d  %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, _a, _b); ++gFailures; } } while (0)
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++gFailures; } } while (0)

static double Feed(FrameStats& fs, const std::vector<double>& ms, double t0 = 0.0)
{
    double t = t0;
    for (double m : ms) { t += m / 1000.0; fs.AddFrame(m, t); }
    return t;
}

static void TestEmpty()
{
    FrameStats fs;
    auto s = fs.Compute(1.0);
    CHECK(!s.hasFrames);
    CHECK(!s.active);
    CHECK(!s.low1Valid);
}

static void TestSteady60()
{
    FrameStats fs;
    std::vector<double> ms(600, 1000.0 / 60.0); // 10 seconds at 60 fps
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.active);
    CHECK(s.averageValid);
    CHECK_NEAR(s.averageFps, 60.0, 1e-3);
    CHECK_NEAR(s.currentFps, 60.0, 1e-3);
    CHECK_NEAR(s.currentFrametimeMs, 16.6667, 1e-3);
    CHECK(s.minMaxValid);
    CHECK_NEAR(s.minFps, 60.0, 1e-3);
    CHECK_NEAR(s.maxFps, 60.0, 1e-3);
    CHECK(s.low1Valid);
    CHECK_NEAR(s.low1Fps, 60.0, 1e-3);
    CHECK(!s.low01Valid); // only 600 frames
}

static void TestAverageIsTimeWeighted()
{
    // Alternating 10 ms / 30 ms frames: 2 frames per 40 ms = 50 fps time-weighted,
    // NOT the mean of per-frame fps ((100 + 33.3) / 2 = 66.7).
    FrameStats fs;
    std::vector<double> ms;
    for (int i = 0; i < 500; ++i) ms.push_back((i % 2) ? 30.0 : 10.0);   // 10 s
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.averageValid);
    CHECK_NEAR(s.averageFps, 50.0, 0.5);
    CHECK_NEAR(s.averageFrametimeMs, 20.0, 0.2);
}

static void TestNoAverageBeforeFirstSample()
{
    // Less than one 0.25 s sample of frames: no AVG / MIN / MAX yet (the overlay shows --).
    FrameStats fs;
    std::vector<double> ms(10, 10.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.hasFrames);
    CHECK(!s.averageValid);
    CHECK(!s.minMaxValid);
    CHECK(s.samples == 0);
}

static void TestLows()
{
    // 1000 frames: 990 x 10 ms, 10 x 50 ms (exactly 1% stutters)
    std::vector<double> ms;
    for (int i = 0; i < 1000; ++i) ms.push_back((i % 100 == 0) ? 50.0 : 10.0);

    {
        FrameStats fs;
        double end = Feed(fs, ms);
        auto s = fs.Compute(end, LowMethod::Percentile);
        // 99th percentile frametime (nearest rank 990 of 1000, ascending) is 10 ms.
        CHECK_NEAR(s.low1Fps, 100.0, 1e-3);
        CHECK(s.low01Valid);
        // 99.9th percentile: rank 999 -> 50 ms -> 20 fps
        CHECK_NEAR(s.low01Fps, 20.0, 1e-3);
    }
    {
        FrameStats fs;
        double end = Feed(fs, ms);
        auto s = fs.Compute(end, LowMethod::AverageOfWorst);
        // worst 1% = 10 frames of 50 ms -> 20 fps
        CHECK_NEAR(s.low1Fps, 20.0, 1e-3);
        // worst 0.1% = 1 frame of 50 ms -> 20 fps
        CHECK_NEAR(s.low01Fps, 20.0, 1e-3);
    }
}

static void TestMinMaxBuckets()
{
    FrameStats fs;
    std::vector<double> ms;
    for (int i = 0; i < 120; ++i) ms.push_back(1000.0 / 120); // ~1 s at 120
    for (int i = 0; i < 30; ++i) ms.push_back(1000.0 / 30);   // ~1 s at 30
    for (int i = 0; i < 120; ++i) ms.push_back(1000.0 / 120); // trailing data closes the 30 fps bucket
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.minMaxValid);
    CHECK(s.maxFps > 110.0);
    CHECK(s.minFps < 40.0);
}

static void TestStallAndPauseRejection()
{
    FrameStats fs;
    std::vector<double> ms(200, 10.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end + 5.0);   // nothing for 5 s
    CHECK(!s.active);
    CHECK_NEAR(s.currentFps, 0.0, 1e-9);

    fs.AddFrame(8000.0, end + 8.0);   // implausible gap: ignored
    CHECK(fs.size() == 200);
    fs.AddFrame(-1.0, end + 8.0);
    fs.AddFrame(std::nan(""), end + 8.0);
    CHECK(fs.size() == 200);
}

static void TestRingWrap()
{
    FrameStats fs(64);
    std::vector<double> ms(100, 20.0);
    for (int i = 0; i < 50; ++i) ms.push_back(10.0);
    double end = Feed(fs, ms);
    CHECK(fs.size() == 64);
    std::vector<float> hist;
    fs.CopyRecentFrametimes(hist, 1000);
    CHECK(hist.size() == 64);
    CHECK_NEAR(hist.back(), 10.0, 1e-6);
    CHECK_NEAR(hist.front(), 20.0, 1e-6); // 14 x 20ms then 50 x 10ms retained
    (void)end;
}

static void TestWindowForgetsLoadingScreen()
{
    // 10 s loading screen at 2000 FPS, then 30 s of gameplay at 60 FPS. The default window
    // (100 samples = 25 s) now holds only gameplay: AVG, MIN, MAX and lows are all 60.
    FrameStats fs;
    std::vector<double> ms(20000, 0.5);
    for (int i = 0; i < 1800; ++i) ms.push_back(1000.0 / 60.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.samples == kDefaultWindowSamples);
    CHECK_NEAR(s.averageFps, 60.0, 0.01);
    CHECK_NEAR(s.maxFps, 60.0, 0.01);
    CHECK_NEAR(s.minFps, 60.0, 0.01);
    CHECK(s.low1Valid);
    CHECK_NEAR(s.low1Fps, 60.0, 1e-3);
    // Only the frames of the window's span. A sample ends at the first frame >= 0.25 s, so at 60 FPS
    // it holds 16 frames (0.267 s): 100 samples + the one being filled ~ 1616 frames, not 20000+.
    CHECK(s.frameCount > 1500 && s.frameCount <= 1640);
}

static void TestWindowSizeIsAdjustable()
{
    // 5 s at 2000 FPS, then 10 s at 60 FPS: a 100-sample window (25 s) still holds the burst,
    // a 20-sample window (5 s) does not.
    FrameStats fs;
    std::vector<double> ms(10000, 0.5);
    for (int i = 0; i < 600; ++i) ms.push_back(1000.0 / 60.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end);
    CHECK(s.maxFps > 1900.0);
    fs.SetWindowSamples(20);
    s = fs.Compute(end);
    CHECK(s.samples == 20);
    CHECK(fs.WindowSamples() == 20);
    CHECK_NEAR(s.maxFps, 60.0, 0.01);
    CHECK_NEAR(s.averageFps, 60.0, 0.01);
    fs.SetWindowSamples(1);                     // clamped to the minimum
    CHECK(fs.WindowSamples() == kMinWindowSamples);
}

static void TestLowFpsCurrentWindow()
{
    // 4 fps: only a few frames fall in the 0.5 s window that ends at the newest frame
    FrameStats fs;
    std::vector<double> ms(10, 250.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end + 0.4);
    CHECK(s.active);
    CHECK_NEAR(s.currentFps, 4.0, 1e-6);
}

static void TestLateEtwBatch()
{
    // ETW hands frames over in ~1 s batches: the newest frame can be 1.8 s old while the game
    // runs smoothly. It must still count as active, with the real current FPS.
    FrameStats fs;
    std::vector<double> ms(300, 1000.0 / 60.0);
    double end = Feed(fs, ms);
    auto s = fs.Compute(end + 1.8);
    CHECK(s.active);
    CHECK_NEAR(s.currentFps, 60.0, 1e-3);
}

int main()
{
    TestEmpty();
    TestSteady60();
    TestAverageIsTimeWeighted();
    TestLows();
    TestMinMaxBuckets();
    TestStallAndPauseRejection();
    TestRingWrap();
    TestLowFpsCurrentWindow();
    TestLateEtwBatch();
    TestNoAverageBeforeFirstSample();
    TestWindowForgetsLoadingScreen();
    TestWindowSizeIsAdjustable();
    if (gFailures == 0) std::printf("All FrameStats tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
