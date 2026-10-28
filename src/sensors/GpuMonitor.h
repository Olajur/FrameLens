// FrameLens — GPU monitoring façade: DXGI discovery + PDH (all vendors) + vendor backends.
// SPDX-License-Identifier: MIT
#pragma once
#include "GpuPdhMonitor.h"
#include "IGpuTelemetryBackend.h"
#include <memory>
#include <string>

namespace fl
{
    class GpuMonitor
    {
    public:
        bool Initialize();
        void Sample(std::vector<GpuSnapshot>& out);
        const std::vector<GpuAdapterInfo>& Adapters() const noexcept { return adapters_; }
        const std::vector<std::string>& Diagnostics() const noexcept { return diagnostics_; }

    private:
        std::vector<GpuAdapterInfo> adapters_;
        GpuPdhMonitor pdh_;
        bool pdhOk_ = false;
        std::vector<std::unique_ptr<IGpuTelemetryBackend>> backends_;
        std::vector<GpuPdhSample> pdhTmp_;
        std::vector<std::string> diagnostics_;
    };
}
