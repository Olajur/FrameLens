// FrameLens — IGCL backend (Intel).
//
// Uses Intel's vendored IGCL header and loader (third_party/igcl, Intel Software License — see its
// README). Devices are bound to DXGI adapters by LUID. Only read-only queries are used.
// Not yet verified on Intel hardware (see docs/FEATURE_STATUS.md).
// SPDX-License-Identifier: MIT
#include "IgclBackend.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <igcl_api.h>
#include <cstring>

namespace fl
{
    namespace
    {
        // A telemetry item as a number, or nullopt-like "false" when the driver doesn't provide it.
        bool Value(const ctl_oc_telemetry_item_t& item, double& out)
        {
            if (!item.bSupported) return false;
            switch (item.type) {
            case CTL_DATA_TYPE_INT8: out = item.value.data8; return true;
            case CTL_DATA_TYPE_UINT8: out = item.value.datau8; return true;
            case CTL_DATA_TYPE_INT16: out = item.value.data16; return true;
            case CTL_DATA_TYPE_UINT16: out = item.value.datau16; return true;
            case CTL_DATA_TYPE_INT32: out = item.value.data32; return true;
            case CTL_DATA_TYPE_UINT32: out = item.value.datau32; return true;
            case CTL_DATA_TYPE_INT64: out = static_cast<double>(item.value.data64); return true;
            case CTL_DATA_TYPE_UINT64: out = static_cast<double>(item.value.datau64); return true;
            case CTL_DATA_TYPE_FLOAT: out = item.value.datafloat; return true;
            case CTL_DATA_TYPE_DOUBLE: out = item.value.datadouble; return true;
            default: return false;
            }
        }

        Reading Item(const ctl_oc_telemetry_item_t& item)
        {
            double v = 0;
            return Value(item, v) ? Reading::Of(v) : Reading::NotSupported();
        }
    }

    IgclBackend::IgclBackend() = default;
    IgclBackend::~IgclBackend() { Shutdown(); }

    bool IgclBackend::Initialize(const std::vector<GpuAdapterInfo>& adapters)
    {
        deviceForAdapter_.assign(adapters.size(), nullptr);
        last_.assign(adapters.size(), Counters{});
        bool anyIntel = false;
        for (const auto& a : adapters) anyIntel |= a.vendor == GpuVendor::Intel;
        if (!anyIntel) { lastError_ = "no Intel adapter"; return false; }

        ctl_init_args_t args{};
        args.Size = sizeof args;
        args.Version = 0;
        args.AppVersion = CTL_IMPL_VERSION;
        args.flags = CTL_INIT_FLAG_USE_LEVEL_ZERO;   // required for telemetry
        ctl_api_handle_t api = nullptr;
        if (ctlInit(&args, &api) != CTL_RESULT_SUCCESS || !api) {
            lastError_ = "ctlInit failed (ControlLib.dll missing or Intel driver too old)";
            return false;
        }
        api_ = api;

        uint32_t count = 0;
        std::vector<ctl_device_adapter_handle_t> devices;
        if (ctlEnumerateDevices(api, &count, nullptr) == CTL_RESULT_SUCCESS && count > 0) {
            devices.resize(count);
            if (ctlEnumerateDevices(api, &count, devices.data()) != CTL_RESULT_SUCCESS) devices.clear();
        }

        bool any = false;
        for (ctl_device_adapter_handle_t dev : devices) {
            LUID luid{};
            ctl_device_adapter_properties_t props{};
            props.Size = sizeof props;
            props.pDeviceID = &luid;
            props.device_id_size = sizeof luid;
            if (ctlGetDeviceProperties(dev, &props) != CTL_RESULT_SUCCESS) continue;
            for (size_t a = 0; a < adapters.size(); ++a) {
                if (deviceForAdapter_[a] || adapters[a].vendor != GpuVendor::Intel) continue;
                if (luid.HighPart == adapters[a].luidHigh && luid.LowPart == adapters[a].luidLow) {
                    deviceForAdapter_[a] = dev;
                    any = true;
                    break;
                }
            }
        }
        if (!any) { lastError_ = "no IGCL device matched a DXGI adapter"; Shutdown(); return false; }
        return true;
    }

    bool IgclBackend::Serves(size_t i) const noexcept
    {
        return api_ && i < deviceForAdapter_.size() && deviceForAdapter_[i] != nullptr;
    }

    void IgclBackend::Sample(size_t i, GpuSnapshot& g)
    {
        if (!Serves(i)) return;
        ctl_power_telemetry_t t{};
        t.Size = sizeof t;
        t.Version = 1;   // v1 adds VR temperatures / effective clock; older drivers fill what they know
        if (ctlPowerTelemetryGet(static_cast<ctl_device_adapter_handle_t>(deviceForAdapter_[i]), &t) != CTL_RESULT_SUCCESS) {
            for (Reading* r : { &g.temperatureC, &g.memoryTempC, &g.coreClockMHz, &g.memoryClockMHz, &g.powerW, &g.fanPct, &g.fanRpm })
                *r = Reading::Failed();
            last_[i].valid = false;
            return;
        }

        g.temperatureC = Item(t.gpuCurrentTemperature);
        g.memoryTempC = Item(t.vramCurrentTemperature);
        g.hotspotTempC = Reading::NotSupported();
        g.coreClockMHz = Item(t.gpuCurrentClockFrequency);
        g.memoryClockMHz = Item(t.vramCurrentClockFrequency);

        // The first fan's speed, in whichever unit the driver reports.
        g.fanPct = g.fanRpm = Reading::NotSupported();
        double fan = 0;
        if (Value(t.fanSpeed[0], fan)) {
            if (t.fanSpeed[0].units == CTL_UNITS_ANGULAR_SPEED_RPM) g.fanRpm = Reading::Of(fan);
            else if (t.fanSpeed[0].units == CTL_UNITS_PERCENT) g.fanPct = Reading::Of(fan);
        }

        // Power and utilization from monotonic counters: delta / elapsed time since the last sample.
        Counters now;
        const bool haveTime = Value(t.timeStamp, now.time);
        const bool haveEnergy = haveTime && Value(t.gpuEnergyCounter, now.gpuEnergy);
        const bool haveActivity = haveTime && Value(t.globalActivityCounter, now.activity);
        Counters& prev = last_[i];
        const double dt = now.time - prev.time;
        if (!haveEnergy) g.powerW = Reading::NotSupported();
        else if (!prev.valid || dt <= 0) g.powerW = Reading::Failed();   // needs two samples
        else g.powerW = Reading::Of((now.gpuEnergy - prev.gpuEnergy) / dt);
        if (haveActivity && prev.valid && dt > 0) {
            const double pct = (now.activity - prev.activity) / dt * 100.0;
            if (pct >= 0 && pct <= 100.5) g.utilizationPct = Reading::Of(pct > 100 ? 100 : pct);
        }
        now.valid = haveTime;
        prev = now;
    }

    void IgclBackend::Shutdown() noexcept
    {
        if (api_) ctlClose(static_cast<ctl_api_handle_t>(api_));
        api_ = nullptr;
        deviceForAdapter_.clear();
    }
}
