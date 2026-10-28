// FrameLens — NVIDIA telemetry through NVML (nvml.dll ships with every GeForce driver).
// Loaded dynamically: no SDK, no link-time dependency, no effect on non-NVIDIA systems.
// SPDX-License-Identifier: MIT
#pragma once
#include "IGpuTelemetryBackend.h"
#include <string>

namespace fl
{
    class NvmlBackend final : public IGpuTelemetryBackend
    {
    public:
        NvmlBackend();
        ~NvmlBackend() override;
        const char* Name() const noexcept override { return "NVML"; }
        bool Initialize(const std::vector<GpuAdapterInfo>& adapters) override;
        bool Serves(size_t adapterIndex) const noexcept override;
        void Sample(size_t adapterIndex, GpuSnapshot& inOut) override;
        void Shutdown() noexcept override;

        const std::string& LastError() const noexcept { return lastError_; }

    private:
        struct Api;
        Api* api_ = nullptr;
        std::vector<void*> deviceForAdapter_;   // nvmlDevice_t or nullptr
        // Per-device permanent "not supported" flags so we stop querying them.
        struct Caps { bool temp = true, gfxClock = true, memClock = true, power = true, fan = true, util = true; };
        std::vector<Caps> caps_;
        std::string lastError_;
    };
}
