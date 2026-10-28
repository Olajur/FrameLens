// FrameLens — ETW frame source built on Intel's MIT-licensed PresentData library.
//
// Why PresentData instead of a hand-written consumer: correctly attributing
// presents to displayed frames requires tracking flip/composition/MPO state
// across DXGI, D3D9, DxgKrnl, Win32k and DWM providers. PresentData is the
// reference implementation of that state machine (used by PresentMon, FrameView,
// CapFrameX...). No code is injected into games; ETW is a passive OS facility,
// which is why this approach is compatible with anti-cheat systems.
// SPDX-License-Identifier: MIT
#include "IFrameSource.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fl
{
    double NowSec() noexcept
    {
        static const double period = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / static_cast<double>(f.QuadPart); }();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return static_cast<double>(t.QuadPart) * period;
    }
}

#if FL_WITH_PRESENTDATA

#include <PresentData/PresentMonTraceConsumer.hpp>
#include <PresentData/PresentMonTraceSession.hpp>
#include "../core/PresentOrder.h"
#include <atomic>
#include <thread>
#include <unordered_map>

namespace fl
{
    namespace
    {
        constexpr wchar_t kSessionName[] = L"FrameLens";

        struct SwapKey
        {
            uint32_t pid; uint64_t swap;
            bool operator==(const SwapKey& o) const noexcept { return pid == o.pid && swap == o.swap; }
        };
        struct SwapKeyHash
        {
            size_t operator()(const SwapKey& k) const noexcept { return std::hash<uint64_t>{}(k.swap ^ (uint64_t(k.pid) << 32)); }
        };
        // Presents (flag: tracking lost) and displayed frames (flag: generated), put back in time order.
        struct SwapState { PresentOrder app, disp; double lastSeenSec = 0; };
        // How far behind the newest present a timestamp waits for late ones (PresentData hands a lost
        // present over a few frames after the next one); a swapchain quiet this long is flushed.
        constexpr double kReorderHoldSec = 0.1;
        constexpr double kReorderFlushSec = 0.5;

        class EtwFrameSource final : public IFrameSource
        {
        public:
            ~EtwFrameSource() override { Stop(); }

            bool Start(std::string& error) override
            {
                if (status_ == FrameSourceStatus::Running) return true;

                LARGE_INTEGER f; QueryPerformanceFrequency(&f);
                qpcPeriod_ = 1.0 / static_cast<double>(f.QuadPart);

                consumer_ = std::make_unique<PMTraceConsumer>();
                consumer_->mTrackDisplay = true;     // displayed-frame analysis
                consumer_->mTrackFrameType = true;   // frame-generation attribution where drivers report it
                session_ = std::make_unique<PMTraceSession>();
                session_->mPMConsumer = consumer_.get();

                ULONG st = session_->Start(nullptr, kSessionName);
                if (st == ERROR_ALREADY_EXISTS) {         // left over from a crash
                    StopNamedTraceSession(kSessionName);
                    st = session_->Start(nullptr, kSessionName);
                }
                if (st != ERROR_SUCCESS) {
                    status_ = (st == ERROR_ACCESS_DENIED) ? FrameSourceStatus::AccessDenied : FrameSourceStatus::Failed;
                    error = (st == ERROR_ACCESS_DENIED)
                        ? "Access denied starting ETW session: run as administrator once, or add your account to the "
                          "'Performance Log Users' group and sign out/in."
                        : "StartTrace failed with Win32 error " + std::to_string(st);
                    session_.reset();
                    consumer_.reset();
                    return false;
                }

                TRACEHANDLE handle = session_->mTraceHandle;   // non-const: ProcessTrace takes a mutable pointer
                thread_ = std::thread([handle]() mutable {
                    SetThreadDescription(GetCurrentThread(), L"FrameLens ETW consumer");
                    // Blocks until the session is stopped. Priority raised so ETW buffers drain promptly;
                    // the thread is idle (blocked in the kernel) between buffer deliveries.
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                    ProcessTrace(&handle, 1, nullptr, nullptr);
                });
                status_ = FrameSourceStatus::Running;
                return true;
            }

            void Stop() noexcept override
            {
                if (session_) session_->Stop();   // closes the trace -> ProcessTrace returns
                if (thread_.joinable()) thread_.join();
                session_.reset();
                consumer_.reset();
                swaps_.clear();
                if (status_ == FrameSourceStatus::Running) status_ = FrameSourceStatus::NotStarted;
            }

            void Drain(std::vector<FrameSample>& out) override
            {
                if (status_ != FrameSourceStatus::Running) return;

                consumer_->DequeueProcessEvents(processTmp_);   // keep the queue from growing; unused for now
                processTmp_.clear();
                consumer_->DequeuePresentEvents(presentTmp_);

                const double now = NowSec();
                for (const auto& sp : presentTmp_) {
                    const PresentEvent& p = *sp;
                    if (p.PresentStartTime == 0) continue;

                    SwapState& s = swaps_[SwapKey{ p.ProcessId, p.SwapChainAddress }];
                    s.lastSeenSec = now;
                    // The app still called Present() when PresentData lost track of a frame on its way
                    // to the screen (frequent with variable refresh): it counts towards the FPS.
                    s.app.Push(p.PresentStartTime, p.IsLost);

                    if (p.IsLost || p.FinalState != PresentResult::Presented) continue;
                    for (const auto& [type, qpc] : p.Displayed)
                        s.disp.Push(qpc, !(type == FrameType::Application || type == FrameType::NotSet || type == FrameType::Unspecified));
                }
                presentTmp_.clear();

                const uint64_t hold = static_cast<uint64_t>(kReorderHoldSec / qpcPeriod_);
                for (auto& [key, s] : swaps_) {
                    const bool flush = now - s.lastSeenSec > kReorderFlushSec;
                    FrameSample a;
                    a.pid = key.pid;
                    a.swapChain = key.swap;
                    s.app.Release(hold, flush, [&](uint64_t t, uint64_t interval, bool lost) {
                        a.kind = FrameKind::AppPresent;
                        a.timeSec = t * qpcPeriod_;
                        a.intervalMs = interval * qpcPeriod_ * 1000.0;
                        a.lost = lost;
                        out.push_back(a);
                    });
                    s.disp.Release(hold, flush, [&](uint64_t t, uint64_t interval, bool generated) {
                        FrameSample d = a;
                        d.kind = FrameKind::Displayed;
                        d.timeSec = t * qpcPeriod_;
                        d.intervalMs = interval * qpcPeriod_ * 1000.0;
                        d.lost = false;
                        d.generated = generated;
                        out.push_back(d);
                    });
                }

                // Forget swapchains idle for 30 s (closed games, launchers).
                if (now - lastGc_ > 5.0) {
                    lastGc_ = now;
                    for (auto it = swaps_.begin(); it != swaps_.end();)
                        it = (now - it->second.lastSeenSec > 30.0) ? swaps_.erase(it) : std::next(it);
                }
            }

            FrameSourceStatus Status() const noexcept override { return status_; }

        private:
            std::unique_ptr<PMTraceConsumer> consumer_;
            std::unique_ptr<PMTraceSession> session_;
            std::thread thread_;
            std::atomic<FrameSourceStatus> status_{ FrameSourceStatus::NotStarted };
            double qpcPeriod_ = 0.0;
            double lastGc_ = 0.0;
            std::vector<std::shared_ptr<PresentEvent>> presentTmp_;
            std::vector<ProcessEvent> processTmp_;
            std::unordered_map<SwapKey, SwapState, SwapKeyHash> swaps_;
        };
    }

    std::unique_ptr<IFrameSource> CreateEtwFrameSource() { return std::make_unique<EtwFrameSource>(); }
}

#else // !FL_WITH_PRESENTDATA

namespace fl
{
    namespace
    {
        class NotBuiltFrameSource final : public IFrameSource
        {
        public:
            bool Start(std::string& error) override
            {
                error = "This build has no frame-monitoring backend (configure with FL_WITH_PRESENTDATA=ON using MSVC).";
                return false;
            }
            void Stop() noexcept override {}
            void Drain(std::vector<FrameSample>&) override {}
            FrameSourceStatus Status() const noexcept override { return FrameSourceStatus::NotBuilt; }
        };
    }
    std::unique_ptr<IFrameSource> CreateEtwFrameSource() { return std::make_unique<NotBuiltFrameSource>(); }
}

#endif
