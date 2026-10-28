// FrameLens — CPU monitor.
//
// Utilization uses "% Processor Utility" (the counter Task Manager uses on
// Windows 8+, frequency-aware). Effective clock = "% Processor Performance" x
// "Processor Frequency" / 100, which is how Task Manager computes "Speed".
// Temperature and package power are NOT available from any documented
// user-mode Windows API; see docs/FEATURE_STATUS.md.
// SPDX-License-Identifier: MIT
#include "CpuMonitor.h"
#include <algorithm>
#include <cwchar>

namespace fl
{
    namespace
    {
        std::string ReadCpuName()
        {
            wchar_t buf[256]{};
            DWORD size = sizeof(buf);
            if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                             L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) {
                return "Unknown CPU";
            }
            std::wstring w(buf);
            w.erase(0, w.find_first_not_of(L' '));
            while (!w.empty() && w.back() == L' ') w.pop_back();
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
            return s;
        }

        // Physical cores and their logical processors. Thread indices follow the order of the
        // per-processor counters below: sorted by (processor group, number in group).
        std::vector<CpuCore> ReadCoreTopology()
        {
            DWORD len = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
            std::vector<uint8_t> buf(len);
            if (!len || !GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len))
                return {};
            struct Raw { std::vector<std::pair<int, int>> lps; uint8_t eff; };
            std::vector<Raw> raw;
            std::vector<std::pair<int, int>> all;
            for (DWORD off = 0; off < len;) {
                const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
                if (info->Relationship == RelationProcessorCore) {
                    Raw r;
                    r.eff = info->Processor.EfficiencyClass;
                    for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                        const GROUP_AFFINITY& ga = info->Processor.GroupMask[g];
                        for (int bit = 0; bit < 64; ++bit)
                            if (ga.Mask & (KAFFINITY{ 1 } << bit)) { r.lps.push_back({ ga.Group, bit }); all.push_back({ ga.Group, bit }); }
                    }
                    raw.push_back(std::move(r));
                }
                off += info->Size;
            }
            std::sort(all.begin(), all.end());
            std::vector<CpuCore> cores;
            for (const auto& r : raw) {
                CpuCore c;
                c.efficiencyClass = r.eff;
                for (const auto& lp : r.lps)
                    c.threads.push_back(static_cast<uint16_t>(std::lower_bound(all.begin(), all.end(), lp) - all.begin()));
                cores.push_back(std::move(c));
            }
            return cores;
        }

        // Instance names look like "0,5" (group,index). Totals contain "_Total".
        bool ParseCoreInstance(const std::wstring& inst, int& group, int& index)
        {
            if (inst.find(L"_Total") != std::wstring::npos) return false;
            return swscanf(inst.c_str(), L"%d,%d", &group, &index) == 2;
        }

        // Sort per-core values into (group, index) order and write them out.
        void MapPerCore(const std::vector<win::PdhInstanceValue>& in, std::vector<Reading>& out,
                        double scale = 1.0, const std::vector<win::PdhInstanceValue>* mulBy = nullptr,
                        bool clamp100 = false)
        {
            struct Item { int g, i; double v; bool ok; };
            std::vector<Item> items;
            items.reserve(in.size());
            for (size_t k = 0; k < in.size(); ++k) {
                int g, i;
                if (!ParseCoreInstance(in[k].instance, g, i)) continue;
                double v = in[k].value * scale;
                bool ok = in[k].ok;
                if (mulBy) {
                    auto it = std::find_if(mulBy->begin(), mulBy->end(),
                                           [&](const win::PdhInstanceValue& m) { return m.instance == in[k].instance; });
                    if (it == mulBy->end() || !it->ok) ok = false;
                    else v *= it->value;
                }
                if (clamp100) v = std::clamp(v, 0.0, 100.0);
                items.push_back({ g, i, v, ok });
            }
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                return a.g != b.g ? a.g < b.g : a.i < b.i;
            });
            out.resize(items.size());
            for (size_t k = 0; k < items.size(); ++k)
                out[k] = items[k].ok ? Reading::Of(items[k].v) : Reading::Failed();
        }
    }

    CpuMonitor::CpuMonitor() = default;

    bool CpuMonitor::Initialize()
    {
        name_ = ReadCpuName();
        logical_ = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        cores_ = ReadCoreTopology();

        pdh_ = std::make_unique<win::PdhQuery>();
        if (!pdh_->valid()) return false;

        cTotalUtil_ = pdh_->Add(L"\\Processor Information(_Total)\\% Processor Utility");
        cCoreUtil_  = pdh_->Add(L"\\Processor Information(*)\\% Processor Utility");
        if (cTotalUtil_ < 0) { // very old builds: fall back to % Processor Time
            cTotalUtil_ = pdh_->Add(L"\\Processor Information(_Total)\\% Processor Time");
            cCoreUtil_  = pdh_->Add(L"\\Processor Information(*)\\% Processor Time");
        }
        cTotalPerf_ = pdh_->Add(L"\\Processor Information(_Total)\\% Processor Performance");
        cTotalFreq_ = pdh_->Add(L"\\Processor Information(_Total)\\Processor Frequency");
        cCorePerf_  = pdh_->Add(L"\\Processor Information(*)\\% Processor Performance");
        cCoreFreq_  = pdh_->Add(L"\\Processor Information(*)\\Processor Frequency");

        pdh_->Collect(); // prime rate counters
        return cTotalUtil_ >= 0;
    }

    void CpuMonitor::Sample(CpuSnapshot& out)
    {
        out.name = name_;
        out.logicalProcessors = logical_;
        out.cores = cores_;
        out.packageTempC = Reading::NotSupported();
        out.packagePowerW = Reading::NotSupported();

        if (!pdh_ || !pdh_->Collect()) {
            out.totalUtilizationPct = Reading::Failed();
            out.effectiveClockMHz = Reading::Failed();
            return;
        }

        double v = 0;
        out.totalUtilizationPct = pdh_->GetDouble(cTotalUtil_, v) ? Reading::Of(std::clamp(v, 0.0, 100.0)) : Reading::Failed();
        if (cCoreUtil_ >= 0 && pdh_->GetArray(cCoreUtil_, tmpUtil_))
            MapPerCore(tmpUtil_, out.coreUtilizationPct, 1.0, nullptr, true);

        double perf = 0, freq = 0;
        if (cTotalPerf_ >= 0 && cTotalFreq_ >= 0 && pdh_->GetDouble(cTotalPerf_, perf) && pdh_->GetDouble(cTotalFreq_, freq))
            out.effectiveClockMHz = Reading::Of(perf * freq / 100.0);
        else
            out.effectiveClockMHz = (cTotalPerf_ < 0) ? Reading::NotSupported() : Reading::Failed();

        if (cCorePerf_ >= 0 && cCoreFreq_ >= 0 && pdh_->GetArray(cCorePerf_, tmpPerf_) && pdh_->GetArray(cCoreFreq_, tmpFreq_))
            MapPerCore(tmpPerf_, out.coreClockMHz, 0.01, &tmpFreq_);
    }
}
