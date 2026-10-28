// FrameLens — NVML backend.
//
// Types and constants below are transcribed from NVIDIA's public nvml.h
// (ABI-stable C API). Only read-only queries are used: FrameLens never
// changes clocks, power limits or fan curves.
// SPDX-License-Identifier: MIT
#include "NvmlBackend.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstring>

namespace fl
{
    namespace nvml
    {
        using Return = int;
        using Device = void*;
        constexpr Return kSuccess = 0;
        constexpr Return kNotSupported = 3;
        constexpr Return kFunctionNotFound = 13;
        constexpr Return kInvalidArgument = 2;
        constexpr int TEMPERATURE_GPU = 0;
        constexpr int CLOCK_GRAPHICS = 0;
        constexpr int CLOCK_MEM = 2;

        struct PciInfo {
            char busIdLegacy[16];
            unsigned int domain, bus, device;
            unsigned int pciDeviceId;     // (deviceId << 16) | vendorId
            unsigned int pciSubSystemId;
            char busId[32];
        };
        struct Utilization { unsigned int gpu, memory; };
        struct TemperatureV1 { unsigned int version; int sensorType; int temperature; };
        constexpr unsigned int TemperatureV1Version = static_cast<unsigned int>(sizeof(TemperatureV1)) | (1u << 24);
    }

    struct NvmlBackend::Api
    {
        HMODULE dll = nullptr;
        nvml::Return (*Init)() = nullptr;
        nvml::Return (*Shutdown)() = nullptr;
        nvml::Return (*GetCount)(unsigned int*) = nullptr;
        nvml::Return (*GetHandleByIndex)(unsigned int, nvml::Device*) = nullptr;
        nvml::Return (*GetPciInfo)(nvml::Device, nvml::PciInfo*) = nullptr;
        nvml::Return (*GetTemperatureV)(nvml::Device, nvml::TemperatureV1*) = nullptr;   // newer drivers
        nvml::Return (*GetTemperature)(nvml::Device, int, unsigned int*) = nullptr;      // deprecated fallback
        nvml::Return (*GetClockInfo)(nvml::Device, int, unsigned int*) = nullptr;
        nvml::Return (*GetPowerUsage)(nvml::Device, unsigned int*) = nullptr;            // milliwatts
        nvml::Return (*GetFanSpeed)(nvml::Device, unsigned int*) = nullptr;              // percent
        nvml::Return (*GetUtilizationRates)(nvml::Device, nvml::Utilization*) = nullptr;
    };

    namespace
    {
        template<class F> bool Load(HMODULE m, const char* name, F& fn)
        {
            fn = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
            return fn != nullptr;
        }

        HMODULE LoadNvmlDll()
        {
            // Modern drivers install nvml.dll into System32. Restrict the search to System32
            // to avoid DLL planting; then try the legacy NVSMI location by absolute path.
            if (HMODULE m = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) return m;
            wchar_t pf[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"ProgramW6432", pf, MAX_PATH) == 0) return nullptr;
            std::wstring legacy = std::wstring(pf) + L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";
            return LoadLibraryExW(legacy.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
    }

    NvmlBackend::NvmlBackend() = default;
    NvmlBackend::~NvmlBackend() { Shutdown(); }

    bool NvmlBackend::Initialize(const std::vector<GpuAdapterInfo>& adapters)
    {
        deviceForAdapter_.assign(adapters.size(), nullptr);
        caps_.assign(adapters.size(), Caps{});

        bool anyNvidia = false;
        for (const auto& a : adapters) anyNvidia |= a.vendor == GpuVendor::Nvidia;
        if (!anyNvidia) { lastError_ = "no NVIDIA adapter"; return false; }

        auto api = new Api();
        api->dll = LoadNvmlDll();
        if (!api->dll) { lastError_ = "nvml.dll not found"; delete api; return false; }

        bool ok = Load(api->dll, "nvmlInit_v2", api->Init)
               && Load(api->dll, "nvmlShutdown", api->Shutdown)
               && Load(api->dll, "nvmlDeviceGetCount_v2", api->GetCount)
               && Load(api->dll, "nvmlDeviceGetHandleByIndex_v2", api->GetHandleByIndex)
               && Load(api->dll, "nvmlDeviceGetPciInfo_v3", api->GetPciInfo);
        Load(api->dll, "nvmlDeviceGetTemperatureV", api->GetTemperatureV);
        Load(api->dll, "nvmlDeviceGetTemperature", api->GetTemperature);
        Load(api->dll, "nvmlDeviceGetClockInfo", api->GetClockInfo);
        Load(api->dll, "nvmlDeviceGetPowerUsage", api->GetPowerUsage);
        Load(api->dll, "nvmlDeviceGetFanSpeed", api->GetFanSpeed);
        Load(api->dll, "nvmlDeviceGetUtilizationRates", api->GetUtilizationRates);

        if (!ok || api->Init() != nvml::kSuccess) {
            lastError_ = ok ? "nvmlInit failed" : "nvml.dll is missing required exports";
            FreeLibrary(api->dll);
            delete api;
            return false;
        }
        api_ = api;

        unsigned int count = 0;
        if (api_->GetCount(&count) != nvml::kSuccess) count = 0;

        // Bind NVML devices to DXGI adapters by PCI device + subsystem id. If several
        // identical cards exist, bind in enumeration order (both APIs list by bus order
        // in practice) — documented limitation until LUID matching via D3DKMT is added.
        std::vector<bool> used(count, false);
        bool any = false;
        for (size_t a = 0; a < adapters.size(); ++a) {
            if (adapters[a].vendor != GpuVendor::Nvidia) continue;
            for (unsigned int i = 0; i < count; ++i) {
                if (used[i]) continue;
                nvml::Device dev = nullptr;
                nvml::PciInfo pci{};
                if (api_->GetHandleByIndex(i, &dev) != nvml::kSuccess) continue;
                if (api_->GetPciInfo(dev, &pci) != nvml::kSuccess) continue;
                const uint32_t devId = pci.pciDeviceId >> 16;
                const uint32_t venId = pci.pciDeviceId & 0xFFFF;
                if (venId == adapters[a].vendorId && devId == adapters[a].deviceId &&
                    (pci.pciSubSystemId == adapters[a].subSysId || adapters[a].subSysId == 0)) {
                    deviceForAdapter_[a] = dev;
                    used[i] = true;
                    any = true;
                    break;
                }
            }
        }
        if (!any) lastError_ = "no NVML device matched a DXGI adapter";
        return any;
    }

    bool NvmlBackend::Serves(size_t i) const noexcept
    {
        return api_ && i < deviceForAdapter_.size() && deviceForAdapter_[i] != nullptr;
    }

    void NvmlBackend::Sample(size_t i, GpuSnapshot& g)
    {
        if (!Serves(i)) return;
        auto dev = static_cast<nvml::Device>(deviceForAdapter_[i]);
        Caps& c = caps_[i];

        // Maps an NVML status to a Reading; NOT_SUPPORTED disables the query permanently.
        auto map = [](nvml::Return r, bool& cap, double v) -> Reading {
            if (r == nvml::kSuccess) return Reading::Of(v);
            if (r == nvml::kNotSupported || r == nvml::kFunctionNotFound) { cap = false; return Reading::NotSupported(); }
            return Reading::Failed();   // incl. NVML_ERROR_GPU_IS_LOST (15) during a driver reset (TDR)
        };

        if (c.temp) {
            if (api_->GetTemperatureV) {
                nvml::TemperatureV1 t{ nvml::TemperatureV1Version, nvml::TEMPERATURE_GPU, 0 };
                nvml::Return r = api_->GetTemperatureV(dev, &t);
                if (r == nvml::kFunctionNotFound || r == nvml::kInvalidArgument /* struct version mismatch */) {
                    api_->GetTemperatureV = nullptr; // fall through to legacy next sample
                    g.temperatureC = Reading::Failed();
                }
                else g.temperatureC = map(r, c.temp, t.temperature);
            }
            else if (api_->GetTemperature) {
                unsigned int t = 0;
                g.temperatureC = map(api_->GetTemperature(dev, nvml::TEMPERATURE_GPU, &t), c.temp, t);
            }
            else { c.temp = false; g.temperatureC = Reading::NotSupported(); }
        }
        else g.temperatureC = Reading::NotSupported();

        // Hotspot and memory-junction temperatures are not exposed by public NVML for GeForce.
        g.hotspotTempC = Reading::NotSupported();
        g.memoryTempC = Reading::NotSupported();

        unsigned int v = 0;
        if (c.gfxClock && api_->GetClockInfo) g.coreClockMHz = map(api_->GetClockInfo(dev, nvml::CLOCK_GRAPHICS, &v), c.gfxClock, v);
        else g.coreClockMHz = Reading::NotSupported();

        if (c.memClock && api_->GetClockInfo) g.memoryClockMHz = map(api_->GetClockInfo(dev, nvml::CLOCK_MEM, &v), c.memClock, v);
        else g.memoryClockMHz = Reading::NotSupported();

        if (c.power && api_->GetPowerUsage) g.powerW = map(api_->GetPowerUsage(dev, &v), c.power, v / 1000.0);
        else g.powerW = Reading::NotSupported();

        if (c.fan && api_->GetFanSpeed) g.fanPct = map(api_->GetFanSpeed(dev, &v), c.fan, v);
        else g.fanPct = Reading::NotSupported();

        if (c.util && api_->GetUtilizationRates) {
            nvml::Utilization u{};
            const nvml::Return r = api_->GetUtilizationRates(dev, &u);
            g.utilizationPct = map(r, c.util, u.gpu);
            g.memoryControllerPct = map(r, c.util, u.memory);
        }
    }

    void NvmlBackend::Shutdown() noexcept
    {
        if (!api_) return;
        if (api_->Shutdown) api_->Shutdown();
        if (api_->dll) FreeLibrary(api_->dll);
        delete api_;
        api_ = nullptr;
        deviceForAdapter_.clear();
    }
}
