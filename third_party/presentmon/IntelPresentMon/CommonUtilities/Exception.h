// FrameLens compatibility shim (not part of upstream PresentMon).
// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <exception>
#include <tuple>
#include <stdexcept>
#include <string>
#include <utility>

namespace pmon::util
{
    class Exception : public std::runtime_error
    {
    public:
        explicit Exception(const std::string& msg) : std::runtime_error(msg) {}
        explicit Exception(const char* msg = "") : std::runtime_error(msg) {}
    };

    template<class E = Exception, typename... R>
    E Except(R&&... args) { return E(std::forward<R>(args)...); }

    // Describes the in-flight exception (call only from a catch block).
    std::string ReportException() noexcept;

    namespace str
    {
        std::string ToNarrow(const std::wstring& wide);
    }
}

#define PM_DEFINE_EX_FROM(base, name) class name : public base { public: using base::base; }
#define PM_DEFINE_EX(name) class name : public ::pmon::util::Exception { public: using Exception::Exception; }
#define pmquell(stat) try { stat; } catch (...) {}
