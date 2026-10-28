// FrameLens — WDDM GPU counters.
//
// Utilization: each "GPU Engine" instance is one process on one engine. Task
// Manager's GPU % for an adapter is the busiest engine, where an engine's
// load is the sum over processes. We reproduce that exactly.
// SPDX-License-Identifier: MIT
#include "GpuPdhMonitor.h"
#include <algorithm>

namespace fl
{
    namespace
    {
        int FindAdapter(const std::vector<GpuAdapterInfo>& adapters, const EngineInstanceKey& k)
        {
            for (size_t i = 0; i < adapters.size(); ++i)
                if (adapters[i].luidHigh == k.luidHigh && adapters[i].luidLow == k.luidLow) return static_cast<int>(i);
            return -1;
        }

        struct EngineAcc { int adapter, phys, eng; bool is3d; double sum; };
    }

    bool GpuPdhMonitor::Initialize()
    {
        pdh_ = std::make_unique<win::PdhQuery>();
        if (!pdh_->valid()) return false;
        cEngine_    = pdh_->Add(L"\\GPU Engine(*)\\Utilization Percentage");
        cDedicated_ = pdh_->Add(L"\\GPU Adapter Memory(*)\\Dedicated Usage");
        cShared_    = pdh_->Add(L"\\GPU Adapter Memory(*)\\Shared Usage");
        pdh_->Collect(); // prime rate counters
        return cEngine_ >= 0 || cDedicated_ >= 0;
    }

    void GpuPdhMonitor::Sample(const std::vector<GpuAdapterInfo>& adapters, std::vector<GpuPdhSample>& out)
    {
        out.assign(adapters.size(), GpuPdhSample{});
        const bool collected = pdh_ && pdh_->Collect();

        // --- engine utilization ---
        if (cEngine_ < 0) {
            for (auto& o : out) o.busiestEnginePct = o.engine3dPct = Reading::NotSupported();
        }
        else if (!collected || !pdh_->GetArray(cEngine_, tmp_)) {
            for (auto& o : out) o.busiestEnginePct = o.engine3dPct = Reading::Failed();
        }
        else {
            std::vector<EngineAcc> acc;
            acc.reserve(64);
            EngineInstanceKey k;
            for (const auto& iv : tmp_) {
                if (!iv.ok || !ParseGpuInstanceName(iv.instance.c_str(), k, true)) continue;
                const int a = FindAdapter(adapters, k);
                if (a < 0) continue;
                auto it = std::find_if(acc.begin(), acc.end(), [&](const EngineAcc& e) {
                    return e.adapter == a && e.phys == k.phys && e.eng == k.eng;
                });
                if (it == acc.end()) acc.push_back({ a, k.phys, k.eng, k.is3d, iv.value });
                else it->sum += iv.value;
            }
            for (size_t a = 0; a < adapters.size(); ++a) {
                double busiest = 0.0, d3 = 0.0;
                bool any = false, any3d = false;
                for (const auto& e : acc) {
                    if (e.adapter != static_cast<int>(a)) continue;
                    any = true;
                    busiest = std::max(busiest, e.sum);
                    if (e.is3d) { any3d = true; d3 = std::max(d3, e.sum); }
                }
                // An adapter with no engine instances is idle (instances exist only while a process holds a context).
                out[a].busiestEnginePct = Reading::Of(std::clamp(any ? busiest : 0.0, 0.0, 100.0));
                out[a].engine3dPct = Reading::Of(std::clamp(any3d ? d3 : 0.0, 0.0, 100.0));
            }
        }

        // --- adapter memory ---
        auto fillMem = [&](int counter, Reading GpuPdhSample::*field) {
            if (counter < 0) { for (auto& o : out) o.*field = Reading::NotSupported(); return; }
            if (!collected || !pdh_->GetArray(counter, tmp_)) { for (auto& o : out) o.*field = Reading::Failed(); return; }
            std::vector<double> sums(adapters.size(), 0.0);
            std::vector<bool> seen(adapters.size(), false);
            EngineInstanceKey k;
            for (const auto& iv : tmp_) {
                if (!iv.ok || !ParseGpuInstanceName(iv.instance.c_str(), k, false)) continue;
                const int a = FindAdapter(adapters, k);
                if (a < 0) continue;
                sums[a] += iv.value;
                seen[a] = true;
            }
            for (size_t a = 0; a < adapters.size(); ++a)
                out[a].*field = seen[a] ? Reading::Of(sums[a]) : Reading::Failed();
        };
        fillMem(cDedicated_, &GpuPdhSample::dedicatedUsedBytes);
        fillMem(cShared_, &GpuPdhSample::sharedUsedBytes);
    }
}
