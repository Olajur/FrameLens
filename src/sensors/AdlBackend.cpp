// FrameLens — ADL backend (AMD Radeon).
//
// The few types, constants and function signatures below are transcribed from AMD's ADL SDK headers
// (github.com/GPUOpen-LibrariesAndSDKs/display-library, include/adl_*.h, ADL 18.1), which carry this
// notice: "Copyright (c) 2016 - 2022 Advanced Micro Devices, Inc. All rights reserved. MIT LICENSE:
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software ...
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software." No SDK file is redistributed; atiadlxx.dll is part of the
// AMD driver and is loaded from System32 only.
//
// Sensors come from the driver's PMLog: preferably the PMLog shared-memory session (AMD's current
// method), else ADL2_New_QueryPMLogData_Get (deprecated in ADL 18.1 but still served by drivers).
// Only read-only queries are used: FrameLens never changes clocks, power limits or fan curves.
// SPDX-License-Identifier: MIT
#include "AdlBackend.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fl
{
    namespace adl
    {
        constexpr int kOk = 0;
        constexpr int kMaxPath = 256;
        constexpr int kMaxSensors = 256;
        using Context = void*;
        using D3dkmtHandle = unsigned int;

        // ADL_PMLOG_SENSORS (adl_defines.h)
        enum Sensor : int
        {
            CLK_GFXCLK = 1, CLK_MEMCLK = 2,
            TEMPERATURE_EDGE = 8, TEMPERATURE_MEM = 9,
            FAN_RPM = 14, FAN_PERCENTAGE = 15,
            INFO_ACTIVITY_GFX = 19, INFO_ACTIVITY_MEM = 20,
            ASIC_POWER = 23,
            TEMPERATURE_HOTSPOT = 27,
            BOARD_POWER = 73,
        };

        struct AdapterInfo   // Windows layout
        {
            int iSize;
            int iAdapterIndex;
            char strUDID[kMaxPath];
            int iBusNumber;
            int iDeviceNumber;
            int iFunctionNumber;
            int iVendorID;
            char strAdapterName[kMaxPath];
            char strDisplayName[kMaxPath];
            int iPresent;
            int iExist;
            char strDriverPath[kMaxPath];
            char strDriverPathExt[kMaxPath];
            char strPNPString[kMaxPath];
            int iOSDisplayIndex;
        };

        struct SingleSensorData { int supported; int value; };
        struct PMLogDataOutput { int size; SingleSensorData sensors[kMaxSensors]; };

        using MallocCallback = void* (__stdcall*)(int);
    }

    struct AdlBackend::Api
    {
        HMODULE dll = nullptr;
        adl::Context ctx = nullptr;
        int (*MainControlCreate)(adl::MallocCallback, int, adl::Context*) = nullptr;
        int (*MainControlDestroy)(adl::Context) = nullptr;
        int (*NumberOfAdapters)(adl::Context, int*) = nullptr;
        int (*AdapterInfoGet)(adl::Context, adl::AdapterInfo*, int) = nullptr;
        int (*QueryPMLogData)(adl::Context, int, adl::PMLogDataOutput*) = nullptr;   // deprecated path
        // Shared-memory PMLog (current path)
        int (*ShmSupport)(adl::Context, int, int*, int) = nullptr;
        int (*DeviceCreate)(adl::Context, int, adl::D3dkmtHandle*) = nullptr;
        int (*DeviceDestroy)(adl::Context, adl::D3dkmtHandle) = nullptr;
        int (*SensorTypes)(adl::Context, int, int*, int**) = nullptr;
        int (*ShmStart)(adl::Context, int, int, int, int*, adl::D3dkmtHandle*, void**, int) = nullptr;
        int (*ShmRead)(adl::Context, int, int, int*, void**, adl::PMLogDataOutput*) = nullptr;
        int (*ShmStop)(adl::Context, int, adl::D3dkmtHandle*) = nullptr;

        struct Session   // per served adapter
        {
            bool shared = false;            // shared-memory session running
            adl::D3dkmtHandle device = 0;
            void* memory = nullptr;
            int sensorCount = 0;
            int* sensors = nullptr;         // allocated by ADL through our malloc callback
        };
        std::vector<Session> sessions;

        void StartSession(int idx, Session& s);   // best effort; falls back to QueryPMLogData
    };

    namespace
    {
        void* __stdcall AdlAlloc(int size) { return std::malloc(static_cast<size_t>(size)); }

        template<class F> bool Load(HMODULE m, const char* name, F& fn)
        {
            fn = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
            return fn != nullptr;
        }

        // "PCI\VEN_1002&DEV_7590&SUBSYS_86011EAE&REV_C0\..." -> device / subsystem ids.
        bool ParsePnp(const char* s, uint32_t& dev, uint32_t& subsys)
        {
            const char* d = std::strstr(s, "DEV_");
            const char* ss = std::strstr(s, "SUBSYS_");
            if (!d) return false;
            dev = static_cast<uint32_t>(std::strtoul(d + 4, nullptr, 16));
            subsys = ss ? static_cast<uint32_t>(std::strtoul(ss + 7, nullptr, 16)) : 0;
            return true;
        }

    }

    void AdlBackend::Api::StartSession(int idx, Session& s)
    {
        int supported = 0;
        if (!ShmSupport || !DeviceCreate || !DeviceDestroy || !ShmStart || !ShmRead || !SensorTypes) return;
        if (ShmSupport(ctx, idx, &supported, 0) != adl::kOk || !supported) return;
        if (DeviceCreate(ctx, idx, &s.device) != adl::kOk) return;
        if (SensorTypes(ctx, idx, &s.sensorCount, &s.sensors) != adl::kOk || s.sensorCount <= 0) {
            DeviceDestroy(ctx, s.device);
            s.device = 0;
            return;
        }
        // 500 ms sampling inside the driver: FrameLens reads once a second.
        if (ShmStart(ctx, idx, 500, -1, nullptr, &s.device, &s.memory, 0) != adl::kOk) {
            DeviceDestroy(ctx, s.device);
            std::free(s.sensors);
            s.device = 0;
            s.sensors = nullptr;
            return;
        }
        s.shared = true;
    }

    AdlBackend::AdlBackend() = default;
    AdlBackend::~AdlBackend() { Shutdown(); }

    bool AdlBackend::Initialize(const std::vector<GpuAdapterInfo>& adapters)
    {
        adlIndexForAdapter_.assign(adapters.size(), -1);
        bool anyAmd = false;
        for (const auto& a : adapters) anyAmd |= a.vendor == GpuVendor::Amd;
        if (!anyAmd) { lastError_ = "no AMD adapter"; return false; }

        auto api = new Api();
        // System32 only: avoids DLL planting.
        api->dll = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!api->dll) { lastError_ = "atiadlxx.dll not found (AMD driver not installed?)"; delete api; return false; }

        const bool ok = Load(api->dll, "ADL2_Main_Control_Create", api->MainControlCreate)
                     && Load(api->dll, "ADL2_Main_Control_Destroy", api->MainControlDestroy)
                     && Load(api->dll, "ADL2_Adapter_NumberOfAdapters_Get", api->NumberOfAdapters)
                     && Load(api->dll, "ADL2_Adapter_AdapterInfo_Get", api->AdapterInfoGet);
        Load(api->dll, "ADL2_New_QueryPMLogData_Get", api->QueryPMLogData);
        Load(api->dll, "ADL2_Overdrive8_PMLog_ShareMemory_Support", api->ShmSupport);
        Load(api->dll, "ADL2_Device_PMLog_Device_Create", api->DeviceCreate);
        Load(api->dll, "ADL2_Device_PMLog_Device_Destroy", api->DeviceDestroy);
        Load(api->dll, "ADL2_Overdrive8_PMLogSenorType_Support_Get", api->SensorTypes);   // sic: "Senor"
        Load(api->dll, "ADL2_Overdrive8_PMLog_ShareMemory_Start", api->ShmStart);
        Load(api->dll, "ADL2_Overdrive8_PMLog_ShareMemory_Read", api->ShmRead);
        Load(api->dll, "ADL2_Overdrive8_PMLog_ShareMemory_Stop", api->ShmStop);

        if (!ok || api->MainControlCreate(AdlAlloc, 1, &api->ctx) != adl::kOk) {
            lastError_ = ok ? "ADL2_Main_Control_Create failed" : "atiadlxx.dll is missing required exports";
            FreeLibrary(api->dll);
            delete api;
            return false;
        }
        api_ = api;

        int n = 0;
        std::vector<adl::AdapterInfo> infos;
        if (api_->NumberOfAdapters(api_->ctx, &n) == adl::kOk && n > 0) {
            infos.assign(static_cast<size_t>(n), adl::AdapterInfo{});
            for (auto& i : infos) i.iSize = sizeof(adl::AdapterInfo);
            if (api_->AdapterInfoGet(api_->ctx, infos.data(), static_cast<int>(infos.size() * sizeof(adl::AdapterInfo))) != adl::kOk)
                infos.clear();
        }

        // One GPU has several ADL indices (one per display output); keep one per PCI bus, in bus
        // order. Bind to DXGI adapters by PCI device + subsystem id; identical cards bind in order
        // (same documented limitation as NVML until LUID matching via D3DKMT is added).
        std::sort(infos.begin(), infos.end(), [](const adl::AdapterInfo& a, const adl::AdapterInfo& b) {
            return a.iBusNumber != b.iBusNumber ? a.iBusNumber < b.iBusNumber : a.iAdapterIndex < b.iAdapterIndex;
        });
        std::vector<int> busesUsed;
        bool any = false;
        for (size_t a = 0; a < adapters.size(); ++a) {
            if (adapters[a].vendor != GpuVendor::Amd) continue;
            for (const auto& info : infos) {
                if (info.iVendorID != 0x1002 && info.iVendorID != 1002) continue;   // ADL reports it either way
                if (std::find(busesUsed.begin(), busesUsed.end(), info.iBusNumber) != busesUsed.end()) continue;
                uint32_t dev = 0, subsys = 0;
                if (!ParsePnp(info.strPNPString, dev, subsys) && !ParsePnp(info.strUDID, dev, subsys)) continue;
                if (dev != adapters[a].deviceId || (subsys && adapters[a].subSysId && subsys != adapters[a].subSysId)) continue;
                adlIndexForAdapter_[a] = info.iAdapterIndex;
                busesUsed.push_back(info.iBusNumber);
                any = true;
                break;
            }
        }
        if (!any) { lastError_ = "no ADL adapter matched a DXGI adapter"; Shutdown(); return false; }

        api_->sessions.assign(adapters.size(), Api::Session{});
        for (size_t a = 0; a < adapters.size(); ++a)
            if (adlIndexForAdapter_[a] >= 0) api_->StartSession(adlIndexForAdapter_[a], api_->sessions[a]);
        return true;
    }

    bool AdlBackend::Serves(size_t i) const noexcept
    {
        return api_ && i < adlIndexForAdapter_.size() && adlIndexForAdapter_[i] >= 0;
    }

    void AdlBackend::Sample(size_t i, GpuSnapshot& g)
    {
        if (!Serves(i)) return;
        const int idx = adlIndexForAdapter_[i];
        Api::Session& s = api_->sessions[i];

        adl::PMLogDataOutput out{};
        out.size = sizeof out;
        int rc = -1;
        if (s.shared) rc = api_->ShmRead(api_->ctx, idx, s.sensorCount, s.sensors, &s.memory, &out);
        if (rc != adl::kOk && api_->QueryPMLogData) rc = api_->QueryPMLogData(api_->ctx, idx, &out);

        if (rc != adl::kOk) {   // transient (e.g. driver reset): mark everything we own as unavailable
            for (Reading* r : { &g.temperatureC, &g.hotspotTempC, &g.memoryTempC, &g.coreClockMHz, &g.memoryClockMHz,
                                &g.powerW, &g.fanPct, &g.fanRpm, &g.memoryControllerPct })
                *r = Reading::Failed();
            return;
        }

        auto get = [&](int sensor, double scale = 1.0) {
            const adl::SingleSensorData& d = out.sensors[sensor];
            return d.supported ? Reading::Of(d.value * scale) : Reading::NotSupported();
        };
        g.temperatureC = get(adl::TEMPERATURE_EDGE);
        g.hotspotTempC = get(adl::TEMPERATURE_HOTSPOT);
        g.memoryTempC = get(adl::TEMPERATURE_MEM);
        g.coreClockMHz = get(adl::CLK_GFXCLK);
        g.memoryClockMHz = get(adl::CLK_MEMCLK);
        // Total board power where the driver reports it (newer GPUs), else the ASIC power sensor.
        g.powerW = get(adl::BOARD_POWER);
        if (!g.powerW.ok() || g.powerW.value <= 0) g.powerW = get(adl::ASIC_POWER);
        g.fanPct = get(adl::FAN_PERCENTAGE);
        g.fanRpm = get(adl::FAN_RPM);
        const Reading gfx = get(adl::INFO_ACTIVITY_GFX);
        if (gfx.ok()) g.utilizationPct = gfx;   // else GpuMonitor keeps the PDH value
        g.memoryControllerPct = get(adl::INFO_ACTIVITY_MEM);
    }

    void AdlBackend::Shutdown() noexcept
    {
        if (!api_) return;
        for (size_t i = 0; i < api_->sessions.size(); ++i) {
            Api::Session& s = api_->sessions[i];
            if (s.shared && api_->ShmStop) api_->ShmStop(api_->ctx, adlIndexForAdapter_[i], &s.device);
            if (s.device && api_->DeviceDestroy) api_->DeviceDestroy(api_->ctx, s.device);
            std::free(s.sensors);
        }
        if (api_->ctx && api_->MainControlDestroy) api_->MainControlDestroy(api_->ctx);
        if (api_->dll) FreeLibrary(api_->dll);
        delete api_;
        api_ = nullptr;
        adlIndexForAdapter_.clear();
    }
}
