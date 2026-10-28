// FrameLens — GPU monitor.
//
// Merge policy (documented in docs/ARCHITECTURE.md):
//  * Utilization: vendor metric if available (what gamers expect from
//    Afterburner-style tools), else PDH busiest-engine (Task Manager).
//  * VRAM used / shared used: PDH for every vendor (consistent, matches Task Manager).
//  * VRAM capacity: DXGI DedicatedVideoMemory / SharedSystemMemory.
//  * Temperatures, clocks, power, fan: vendor backend only; otherwise NotSupported.
// SPDX-License-Identifier: MIT
#include "GpuMonitor.h"
#include "GpuEnumerator.h"
#include "AdlBackend.h"
#include "IgclBackend.h"
#include "NvmlBackend.h"

namespace fl
{
    bool GpuMonitor::Initialize()
    {
        adapters_ = EnumerateGpuAdapters();
        if (adapters_.empty()) {
            diagnostics_.push_back("DXGI reported no hardware GPU adapters");
            return false;
        }

        pdhOk_ = pdh_.Initialize();
        if (!pdhOk_) diagnostics_.push_back("GPU performance counters unavailable (requires WDDM 2.0+ driver)");

        // One backend per vendor, each reading that vendor's own driver library. A backend only
        // starts when a matching GPU is present; its absence never affects the others.
        auto add = [&](auto backend, const char* noDevice) {
            if (backend->Initialize(adapters_)) backends_.push_back(std::move(backend));
            else if (backend->LastError() != noDevice) diagnostics_.push_back(std::string(backend->Name()) + ": " + backend->LastError());
        };
        add(std::make_unique<NvmlBackend>(), "no NVIDIA adapter");
        add(std::make_unique<AdlBackend>(), "no AMD adapter");
        add(std::make_unique<IgclBackend>(), "no Intel adapter");
        return true;
    }

    void GpuMonitor::Sample(std::vector<GpuSnapshot>& out)
    {
        out.resize(adapters_.size());
        if (pdhOk_) pdh_.Sample(adapters_, pdhTmp_);
        else pdhTmp_.assign(adapters_.size(), GpuPdhSample{});

        for (size_t i = 0; i < adapters_.size(); ++i) {
            GpuSnapshot& g = out[i];
            g = GpuSnapshot{};             // every field starts as NotSupported
            g.adapter = adapters_[i];

            const GpuPdhSample& p = pdhTmp_[i];
            g.utilizationPct = p.busiestEnginePct;
            g.utilization3dPct = p.engine3dPct;
            g.dedicatedUsedBytes = p.dedicatedUsedBytes;
            g.sharedUsedBytes = p.sharedUsedBytes;
            g.dedicatedTotalBytes = adapters_[i].dedicatedVideoMemory > 0
                ? Reading::Of(static_cast<double>(adapters_[i].dedicatedVideoMemory)) : Reading::NotSupported();
            g.sharedTotalBytes = Reading::Of(static_cast<double>(adapters_[i].sharedSystemMemory));
            g.telemetrySource = pdhOk_ ? "PDH" : "DXGI";

            for (auto& b : backends_) {
                if (!b->Serves(i)) continue;
                const Reading pdhUtil = g.utilizationPct;
                b->Sample(i, g);
                if (!g.utilizationPct.ok()) g.utilizationPct = pdhUtil;   // vendor util failed -> keep PDH
                g.telemetrySource = std::string(b->Name()) + " + " + g.telemetrySource;
                break;
            }
        }
    }
}
