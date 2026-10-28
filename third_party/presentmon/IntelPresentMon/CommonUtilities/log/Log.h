// FrameLens compatibility shim (not part of upstream PresentMon).
// Replaces PresentMon's full logging framework with a tiny sink so that the
// vendored PresentData library builds without boost/cereal/CLI11.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

namespace pmon::util::log
{
    enum class Level { None, Fatal, Error, Warning, Info, Performance, Debug, Verbose };

    // Optional host callback. Defaults to no-op. Must be thread-safe.
    using SinkFn = void (*)(Level level, const char* message) noexcept;
    void SetSink(SinkFn fn) noexcept;
    SinkFn GetSink() noexcept;

    class GlobalPolicy
    {
    public:
        static GlobalPolicy& Get() noexcept;
        Level GetLogLevel() const noexcept { return level_.load(std::memory_order_relaxed); }
        void SetLogLevel(Level l) noexcept { level_.store(l, std::memory_order_relaxed); }
    private:
        std::atomic<Level> level_{ Level::Warning };
    };

    // Collects a message and emits it on destruction (end of full expression).
    class Entry
    {
    public:
        explicit Entry(Level level) noexcept : level_(level) {}
        Entry(const Entry&) = delete;
        Entry& operator=(const Entry&) = delete;
        ~Entry();
        Entry& note(std::string msg) { msg_ += msg; return *this; }
        Entry& note() { return *this; }
        Entry& hr(long code) { std::ostringstream s; s << " [hr=0x" << std::hex << static_cast<unsigned long>(code) << "]"; msg_ += s.str(); return *this; }
        Entry& code(long c) { msg_ += " [code=" + std::to_string(c) + "]"; return *this; }
        Entry& diag() { return *this; }
        template<typename T>
        Entry& watch(const char* name, const T& value)
        {
            std::ostringstream s; s << " " << name << "=" << value; msg_ += s.str(); return *this;
        }
        // Allows "cond || pmlog_warn(...)" usage in DebugAssert.
        operator bool() const noexcept { return false; }
    private:
        Level level_;
        std::string msg_;
    };
}

#define pmlog_(lvl) ::pmon::util::log::Entry(lvl)
#define pmlog_fatal   pmlog_(::pmon::util::log::Level::Fatal).note
#define pmlog_error   pmlog_(::pmon::util::log::Level::Error).note
#define pmlog_warn    pmlog_(::pmon::util::log::Level::Warning).note
#define pmlog_info    pmlog_(::pmon::util::log::Level::Info).note
#define pmlog_perf    pmlog_(::pmon::util::log::Level::Performance).note
#define pmlog_dbg     pmlog_(::pmon::util::log::Level::Debug).note
#define pmlog_verb(on) pmlog_(::pmon::util::log::Level::Verbose).note
#define pmwatch(expr) watch(#expr, (expr))
