// FrameLens — interface for vendor GPU telemetry backends (NVML, ADLX, IGCL...).
// SPDX-License-Identifier: MIT
#pragma once
#include "../core/Types.h"
#include <vector>

namespace fl
{
    class IGpuTelemetryBackend
    {
    public:
        virtual ~IGpuTelemetryBackend() = default;
        virtual const char* Name() const noexcept = 0;

        // Load the vendor runtime and bind devices to DXGI adapters.
        // Returns true if at least one adapter is served.
        virtual bool Initialize(const std::vector<GpuAdapterInfo>& adapters) = 0;
        virtual bool Serves(size_t adapterIndex) const noexcept = 0;

        // Fill only the fields this backend owns. Must mark fields it cannot read
        // as NotSupported (permanent) or Failed (transient) — never guess.
        virtual void Sample(size_t adapterIndex, GpuSnapshot& inOut) = 0;

        virtual void Shutdown() noexcept = 0;
    };
}
