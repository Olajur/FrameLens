// FrameLens — frame-presentation source interface.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fl
{
    enum class FrameSourceStatus : uint8_t
    {
        NotStarted,
        Running,
        AccessDenied,   // user is neither admin nor in "Performance Log Users"
        Failed,
        NotBuilt,       // binary compiled without the ETW frame backend
    };

    enum class FrameKind : uint8_t
    {
        AppPresent,     // the application called Present (DXGI/D3D9/DXGK: covers DX9-12, Vulkan, OpenGL)
        Displayed,      // a frame reached the screen
    };

    struct FrameSample
    {
        uint32_t pid = 0;
        uint64_t swapChain = 0;
        FrameKind kind = FrameKind::AppPresent;
        double timeSec = 0.0;        // QPC-based monotonic seconds (same clock as fl::NowSec())
        double intervalMs = 0.0;     // time since the previous sample of the same kind/swapchain; <=0 for the first
        bool generated = false;      // Displayed only: frame produced by frame generation / repeated by the driver
        bool lost = false;           // AppPresent only: PresentData lost track of it before the screen
    };

    class IFrameSource
    {
    public:
        virtual ~IFrameSource() = default;
        virtual bool Start(std::string& error) = 0;
        virtual void Stop() noexcept = 0;
        // Appends completed samples (non-blocking), in time order per swapchain. Call every ~50 ms.
        virtual void Drain(std::vector<FrameSample>& out) = 0;
        virtual FrameSourceStatus Status() const noexcept = 0;
    };

    // ETW (PresentMon PresentData) backend when built with FL_WITH_PRESENTDATA,
    // otherwise a source that reports NotBuilt.
    std::unique_ptr<IFrameSource> CreateEtwFrameSource();

    // Monotonic seconds on the QPC clock.
    double NowSec() noexcept;
}
