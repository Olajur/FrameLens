// FrameLens — AMD Radeon telemetry through ADL (AMD Display Library; atiadlxx.dll ships with every
// Adrenalin driver). Loaded dynamically: no SDK, no link-time dependency, no effect on other systems.
// SPDX-License-Identifier: MIT
#pragma once
#include "IGpuTelemetryBackend.h"
#include <string>

namespace fl
{
    class AdlBackend final : public IGpuTelemetryBackend
    {
    public:
        AdlBackend();
        ~AdlBackend() override;
        const char* Name() const noexcept override { return "ADL"; }
        bool Initialize(const std::vector<GpuAdapterInfo>& adapters) override;
        bool Serves(size_t adapterIndex) const noexcept override;
        void Sample(size_t adapterIndex, GpuSnapshot& inOut) override;
        void Shutdown() noexcept override;

        const std::string& LastError() const noexcept { return lastError_; }

    private:
        struct Api;
        Api* api_ = nullptr;
        std::vector<int> adlIndexForAdapter_;   // ADL adapter index or -1
        std::string lastError_;
    };
}
