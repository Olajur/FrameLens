// FrameLens — GPU counter instance-name parser.
// SPDX-License-Identifier: MIT
#include "GpuInstanceName.h"
#include <cwchar>
#include <cstdlib>

namespace fl
{
    bool ParseGpuInstanceName(const wchar_t* name, EngineInstanceKey& key, bool requireEngine)
    {
        if (!name) return false;
        key = {};

        const wchar_t* p = std::wcsstr(name, L"luid_0x");
        if (!p) return false;
        p += 7;
        wchar_t* end = nullptr;
        const unsigned long long hi = std::wcstoull(p, &end, 16);
        if (end == p || std::wcsncmp(end, L"_0x", 3) != 0) return false;
        p = end + 3;
        const unsigned long long lo = std::wcstoull(p, &end, 16);
        if (end == p) return false;
        key.luidHigh = static_cast<int32_t>(static_cast<uint32_t>(hi));
        key.luidLow = static_cast<uint32_t>(lo);

        if (const wchar_t* ph = std::wcsstr(end, L"phys_")) key.phys = static_cast<int>(std::wcstol(ph + 5, nullptr, 10));

        if (const wchar_t* e = std::wcsstr(end, L"_eng_")) {
            wchar_t* e2 = nullptr;
            key.eng = static_cast<int>(std::wcstol(e + 5, &e2, 10));
            if (e2 == e + 5) key.eng = -1;
        }
        if (const wchar_t* t = std::wcsstr(end, L"engtype_")) {
            t += 8;
            key.is3d = (t[0] == L'3' && t[1] == L'D' && (t[2] == L'\0' || t[2] == L'_'));
        }
        return !requireEngine || key.eng >= 0;
    }
}
