// FrameLens — present reordering tests (dependency-free; runs on any OS).
// SPDX-License-Identifier: MIT
#include "../src/core/PresentOrder.h"
#include <cstdio>
#include <vector>

using namespace fl;

static int gFailures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++gFailures; } } while (0)

struct Out { uint64_t t, interval; bool flag; };

static std::vector<Out> Release(PresentOrder& o, uint64_t hold, bool flush)
{
    std::vector<Out> out;
    o.Release(hold, flush, [&](uint64_t t, uint64_t iv, bool f) { out.push_back({ t, iv, f }); });
    return out;
}

static void TestLatePresentFitsBackIn()
{
    // 10-tick frames; the present at 20 was lost and is delivered after 30 and 40.
    PresentOrder o;
    for (uint64_t t : { 10, 30, 40, 20, 50, 60 }) o.Push(t);
    const auto out = Release(o, 15, false);   // keeps 50 and 60 back
    CHECK(out.size() == 4);
    CHECK(out[0].t == 10 && out[0].interval == 0);
    for (size_t i = 1; i < out.size(); ++i) CHECK(out[i].t == 10 * (i + 1) && out[i].interval == 10);
    CHECK(o.Pending() == 2);
    const auto rest = Release(o, 15, true);
    CHECK(rest.size() == 2 && rest[0].t == 50 && rest[0].interval == 10 && rest[1].t == 60);
    CHECK(o.Dropped() == 0);
}

static void TestTooLateIsDroppedNotCounted()
{
    PresentOrder o;
    o.Push(10); o.Push(20);
    Release(o, 0, true);
    o.Push(15);                    // older than what was already released
    o.Push(20);                    // the same present again
    o.Push(30);
    const auto out = Release(o, 0, true);
    CHECK(out.size() == 1 && out[0].t == 30 && out[0].interval == 10);
    CHECK(o.Dropped() == 2);
}

static void TestDuplicatesInsideOneBatch()
{
    PresentOrder o;
    for (uint64_t t : { 10, 20, 20, 30 }) o.Push(t, t == 30);
    const auto out = Release(o, 0, true);
    CHECK(out.size() == 3);
    CHECK(out[2].t == 30 && out[2].interval == 10 && out[2].flag);
    CHECK(o.Dropped() == 1);
}

static void TestHoldLargerThanTimestamps()
{
    PresentOrder o;
    o.Push(5);
    CHECK(Release(o, 100, false).size() == 1);   // newest < hold: nothing to wait for
}

int main()
{
    TestLatePresentFitsBackIn();
    TestTooLateIsDroppedNotCounted();
    TestDuplicatesInsideOneBatch();
    TestHoldLargerThanTimestamps();
    if (gFailures == 0) std::printf("All PresentOrder tests passed.\n");
    return gFailures == 0 ? 0 : 1;
}
