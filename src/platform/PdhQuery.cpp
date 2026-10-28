// FrameLens — PDH wrapper.
// SPDX-License-Identifier: MIT
#include "PdhQuery.h"
#include <pdhmsg.h>

namespace fl::win
{
    PdhQuery::PdhQuery()
    {
        if (PdhOpenQueryW(nullptr, 0, &query_) != ERROR_SUCCESS) query_ = nullptr;
    }

    PdhQuery::~PdhQuery()
    {
        if (query_) PdhCloseQuery(query_);
    }

    int PdhQuery::Add(const wchar_t* englishPath)
    {
        if (!query_) return -1;
        PDH_HCOUNTER c = nullptr;
        if (PdhAddEnglishCounterW(query_, englishPath, 0, &c) != ERROR_SUCCESS) return -1;
        counters_.push_back(c);
        return static_cast<int>(counters_.size() - 1);
    }

    bool PdhQuery::Collect()
    {
        return query_ && PdhCollectQueryData(query_) == ERROR_SUCCESS;
    }

    bool PdhQuery::GetDouble(int index, double& out) const
    {
        if (index < 0 || index >= static_cast<int>(counters_.size())) return false;
        PDH_FMT_COUNTERVALUE v{};
        DWORD type = 0;
        const PDH_STATUS st = PdhGetFormattedCounterValue(counters_[index], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &type, &v);
        if (st != ERROR_SUCCESS || (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA)) return false;
        out = v.doubleValue;
        return true;
    }

    bool PdhQuery::GetArray(int index, std::vector<PdhInstanceValue>& out)
    {
        out.clear();
        if (index < 0 || index >= static_cast<int>(counters_.size())) return false;

        for (int attempt = 0; attempt < 3; ++attempt) {
            DWORD bytes = static_cast<DWORD>(buffer_.size());
            DWORD items = 0;
            auto* items_ptr = buffer_.empty() ? nullptr : reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
            PDH_STATUS st = PdhGetFormattedCounterArrayW(counters_[index], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &bytes, &items, items_ptr);
            if (st == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
                buffer_.resize(bytes + 4096); // instances can appear between calls
                continue;
            }
            if (st != ERROR_SUCCESS) return false;

            out.reserve(items);
            for (DWORD i = 0; i < items; ++i) {
                const auto& it = items_ptr[i];
                PdhInstanceValue v;
                v.instance = it.szName ? it.szName : L"";
                v.ok = it.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA || it.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
                v.value = v.ok ? it.FmtValue.doubleValue : 0.0;
                out.push_back(std::move(v));
            }
            return true;
        }
        return false;
    }
}
