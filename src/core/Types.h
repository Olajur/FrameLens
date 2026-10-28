// FrameLens — core data model shared by all modules.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fl
{
    // Every value carries its provenance state so the UI/overlay can show
    // "N/A" instead of a fabricated number.
    enum class SensorState : uint8_t
    {
        Unsupported,  // No backend can provide this on this hardware/driver.
        Unavailable,  // Backend exists but the last read failed (transient).
        Ok,
    };

    struct Reading
    {
        SensorState state = SensorState::Unsupported;
        double value = 0.0;

        static constexpr Reading Of(double v) noexcept { return { SensorState::Ok, v }; }
        static constexpr Reading Failed() noexcept { return { SensorState::Unavailable, 0.0 }; }
        static constexpr Reading NotSupported() noexcept { return { SensorState::Unsupported, 0.0 }; }
        constexpr bool ok() const noexcept { return state == SensorState::Ok; }
    };

    enum class GpuVendor : uint8_t { Unknown, Nvidia, Amd, Intel, Microsoft };

    constexpr GpuVendor VendorFromPciId(uint32_t vendorId) noexcept
    {
        switch (vendorId) {
        case 0x10DE: return GpuVendor::Nvidia;
        case 0x1002: case 0x1022: return GpuVendor::Amd;
        case 0x8086: return GpuVendor::Intel;
        case 0x1414: return GpuVendor::Microsoft;
        default: return GpuVendor::Unknown;
        }
    }

    constexpr const char* VendorName(GpuVendor v) noexcept
    {
        switch (v) {
        case GpuVendor::Nvidia: return "NVIDIA";
        case GpuVendor::Amd: return "AMD";
        case GpuVendor::Intel: return "Intel";
        case GpuVendor::Microsoft: return "Microsoft";
        default: return "Unknown";
        }
    }

    // Static identity of a GPU adapter, discovered once via DXGI.
    struct GpuAdapterInfo
    {
        std::string name;
        GpuVendor vendor = GpuVendor::Unknown;
        uint32_t vendorId = 0, deviceId = 0, subSysId = 0, revision = 0;
        int32_t luidHigh = 0;
        uint32_t luidLow = 0;
        uint64_t dedicatedVideoMemory = 0;   // bytes
        uint64_t sharedSystemMemory = 0;     // bytes
        bool likelyIntegrated = false;       // heuristic: < 512 MiB dedicated
    };

    // A physical core: its logical processors (indices into the per-logical-processor readings)
    // and Windows' efficiency class (higher = faster; differs between P- and E-cores on hybrid CPUs).
    struct CpuCore
    {
        std::vector<uint16_t> threads;
        uint8_t efficiencyClass = 0;
    };

    struct CpuSnapshot
    {
        std::string name;
        uint32_t logicalProcessors = 0;
        std::vector<CpuCore> cores;               // physical cores (read once at start-up)
        Reading totalUtilizationPct;
        std::vector<Reading> coreUtilizationPct;  // per logical processor
        Reading effectiveClockMHz;                // average across all logical processors
        std::vector<Reading> coreClockMHz;
        Reading packageTempC;                     // requires kernel driver — see docs/FEATURE_STATUS.md
        Reading packagePowerW;                    // requires kernel driver — see docs/FEATURE_STATUS.md
    };

    struct MemorySnapshot
    {
        Reading totalBytes;
        Reading usedBytes;
        Reading availableBytes;
        Reading utilizationPct;
    };

    struct GpuSnapshot
    {
        GpuAdapterInfo adapter;
        std::string telemetrySource;   // e.g. "NVML + PDH", "PDH"

        Reading utilizationPct;        // vendor metric when available, else busiest engine (Task Manager method)
        Reading utilization3dPct;      // PDH 3D engine
        Reading memoryControllerPct;   // "GPU memory utilization" (memory controller load)

        Reading temperatureC;
        Reading hotspotTempC;
        Reading memoryTempC;
        Reading coreClockMHz;
        Reading memoryClockMHz;
        Reading powerW;
        Reading fanPct;
        Reading fanRpm;

        Reading dedicatedUsedBytes;
        Reading dedicatedTotalBytes;
        Reading sharedUsedBytes;
        Reading sharedTotalBytes;
    };

    struct HardwareSnapshot
    {
        uint64_t sequence = 0;
        double timestampSec = 0.0;     // monotonic, seconds
        CpuSnapshot cpu;
        MemorySnapshot memory;
        std::vector<GpuSnapshot> gpus;
    };
}
