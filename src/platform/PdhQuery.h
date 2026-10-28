// FrameLens — thin RAII wrapper over the Windows Performance Data Helper (PDH).
// SPDX-License-Identifier: MIT
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <pdh.h>
#include <string>
#include <vector>

namespace fl::win
{
    struct PdhInstanceValue
    {
        std::wstring instance;
        double value = 0.0;
        bool ok = false;
    };

    class PdhQuery
    {
    public:
        PdhQuery();
        ~PdhQuery();
        PdhQuery(const PdhQuery&) = delete;
        PdhQuery& operator=(const PdhQuery&) = delete;

        bool valid() const noexcept { return query_ != nullptr; }

        // Uses English counter paths so it works on every Windows display language.
        // Returns an index usable with Get*, or -1 if the counter does not exist.
        int Add(const wchar_t* englishPath);

        // Samples all counters. Rate counters need two collections before they are valid.
        bool Collect();

        // Single-instance counter.
        bool GetDouble(int index, double& out) const;

        // Wildcard counter: fills one entry per instance. Reuses `out` and `buffer_` to avoid churn.
        bool GetArray(int index, std::vector<PdhInstanceValue>& out);

    private:
        PDH_HQUERY query_ = nullptr;
        std::vector<PDH_HCOUNTER> counters_;
        std::vector<unsigned char> buffer_;
    };
}
