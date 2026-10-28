// FrameLens — DXGI adapter enumeration.
// SPDX-License-Identifier: MIT
#include "GpuEnumerator.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>   // NTSTATUS for d3dkmthk.h
#include <d3dkmthk.h>
#include <dxgi1_4.h>

namespace fl
{
    namespace
    {
        std::string Narrow(const wchar_t* w)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1) return {};
            std::string s(n - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        template<class T> struct ComPtr
        {
            T* p = nullptr;
            ~ComPtr() { if (p) p->Release(); }
            T** put() { if (p) { p->Release(); p = nullptr; } return &p; }
            T* operator->() const { return p; }
            explicit operator bool() const { return p != nullptr; }
        };

        // Indirect display adapters (virtual monitors from e.g. Virtual Desktop, Sunshine, Meta Quest
        // Link) are listed by DXGI with the rendering GPU's name and PCI ids but a LUID of their own,
        // so they look like extra copies of the real card. The kernel's adapter type tells them apart.
        bool IsIndirectDisplay(LUID luid)
        {
            D3DKMT_OPENADAPTERFROMLUID open{};
            open.AdapterLuid = luid;
            if (D3DKMTOpenAdapterFromLuid(&open) != 0) return false;
            D3DKMT_ADAPTERTYPE type{};
            D3DKMT_QUERYADAPTERINFO q{};
            q.hAdapter = open.hAdapter;
            q.Type = KMTQAITYPE_ADAPTERTYPE;
            q.pPrivateDriverData = &type;
            q.PrivateDriverDataSize = sizeof type;
            const bool indirect = D3DKMTQueryAdapterInfo(&q) == 0 && type.IndirectDisplayDevice;
            D3DKMT_CLOSEADAPTER close{};
            close.hAdapter = open.hAdapter;
            D3DKMTCloseAdapter(&close);
            return indirect;
        }
    }

    std::vector<GpuAdapterInfo> EnumerateGpuAdapters()
    {
        std::vector<GpuAdapterInfo> result;

        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.put()))))
            return result;

        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(i, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
            if (!adapter) continue;

            DXGI_ADAPTER_DESC1 d{};
            if (FAILED(adapter->GetDesc1(&d))) continue;
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if (d.VendorId == 0x1414 && d.DeviceId == 0x8C) continue; // Microsoft Basic Render Driver
            if (IsIndirectDisplay(d.AdapterLuid)) continue;

            bool dup = false;
            for (const auto& a : result)
                if (a.luidHigh == d.AdapterLuid.HighPart && a.luidLow == d.AdapterLuid.LowPart) { dup = true; break; }
            if (dup) continue;

            GpuAdapterInfo info;
            info.name = Narrow(d.Description);
            info.vendorId = d.VendorId;
            info.deviceId = d.DeviceId;
            info.subSysId = d.SubSysId;
            info.revision = d.Revision;
            info.vendor = VendorFromPciId(d.VendorId);
            info.luidHigh = d.AdapterLuid.HighPart;
            info.luidLow = d.AdapterLuid.LowPart;
            info.dedicatedVideoMemory = d.DedicatedVideoMemory;
            info.sharedSystemMemory = d.SharedSystemMemory;
            info.likelyIntegrated = d.DedicatedVideoMemory < (512ull << 20);
            result.push_back(std::move(info));
        }
        return result;
    }
}
