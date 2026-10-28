// FrameLens compatibility shim implementations (not part of upstream PresentMon).
// SPDX-License-Identifier: MIT
#include <windows.h>
#include "../IntelPresentMon/CommonUtilities/Hash.h"
#include "../IntelPresentMon/CommonUtilities/Exception.h"
#include "../IntelPresentMon/CommonUtilities/PrecisionWaiter.h"
#include "../IntelPresentMon/CommonUtilities/log/Log.h"

namespace pmon::util::hash
{
    // Same mixing function as upstream CommonUtilities/Hash.cpp (MIT).
    size_t HashCombine(size_t lhs, size_t rhs) noexcept
    {
        constexpr size_t kHashConstant = 0x517c'c1b7'2722'0a95ull;
        return lhs ^ (rhs + kHashConstant + (lhs << 6) + (lhs >> 2));
    }
    size_t HashGuid(const _GUID& guid) noexcept
    {
        uint64_t a, b;
        static_assert(sizeof(_GUID) == 16);
        memcpy(&a, &guid, 8);
        memcpy(&b, guid.Data4, 8);
        return HashCombine(static_cast<size_t>(a), static_cast<size_t>(b));
    }
}

namespace pmon::util
{
    std::string ReportException() noexcept
    {
        try { throw; }
        catch (const std::exception& e) { return e.what(); }
        catch (...) { return "unknown exception"; }
    }

    namespace str
    {
        std::string ToNarrow(const std::wstring& wide)
        {
            if (wide.empty()) return {};
            const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
            std::string out(n > 0 ? n : 0, '\0');
            if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), out.data(), n, nullptr, nullptr);
            return out;
        }
    }

    int64_t GetCurrentTimestamp() noexcept
    {
        LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart;
    }
    double GetTimestampPeriodSeconds() noexcept
    {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / static_cast<double>(f.QuadPart);
    }
    double TimestampDeltaToSeconds(int64_t start, int64_t end, double period) noexcept
    {
        return static_cast<double>(end - start) * period;
    }
    double PrecisionWaiter::Wait(double seconds, bool alertable) noexcept
    {
        (void)defaultWaitBuffer_;
        if (seconds > 0) SleepEx(static_cast<DWORD>(seconds * 1000.0), alertable ? TRUE : FALSE);
        return seconds;
    }
}

namespace pmon::util::log
{
    static std::atomic<SinkFn> gSink{ nullptr };
    void SetSink(SinkFn fn) noexcept { gSink.store(fn); }
    SinkFn GetSink() noexcept { return gSink.load(); }
    GlobalPolicy& GlobalPolicy::Get() noexcept { static GlobalPolicy p; return p; }
    Entry::~Entry()
    {
        if (level_ > GlobalPolicy::Get().GetLogLevel()) return;
        if (auto s = gSink.load()) s(level_, msg_.c_str());
    }
}
