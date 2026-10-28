// FrameLens — Intel Arc / Iris Xe telemetry through IGCL (Intel Graphics Control Library;
// ControlLib.dll ships with the Intel graphics driver). The vendored wrapper loads it from System32
// at runtime: no effect on systems without an Intel GPU.
// SPDX-License-Identifier: MIT
#pragma once
#include "IGpuTelemetryBackend.h"
#include <string>

namespace fl
{
    class IgclBackend final : public IGpuTelemetryBackend
    {
    public:
        IgclBackend();
        ~IgclBackend() override;
        const char* Name() const noexcept override { return "IGCL"; }
        bool Initialize(const std::vector<GpuAdapterInfo>& adapters) override;
        bool Serves(size_t adapterIndex) const noexcept override;
        void Sample(size_t adapterIndex, GpuSnapshot& inOut) override;
        void Shutdown() noexcept override;

        const std::string& LastError() const noexcept { return lastError_; }

    private:
        void* api_ = nullptr;                    // ctl_api_handle_t
        std::vector<void*> deviceForAdapter_;    // ctl_device_adapter_handle_t or nullptr
        // Previous counter snapshots: power and utilization are deltas over time.
        struct Counters { bool valid = false; double time = 0, gpuEnergy = 0, activity = 0; };
        std::vector<Counters> last_;
        std::string lastError_;
    };
}
