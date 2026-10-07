#pragma once

#include <cstdint>
#include <string>

namespace vmex::uninstall
{
    struct Result final
    {
        bool ok = false;
        std::wstring detail;
    };

    [[nodiscard]] Result TerminateShellHost(std::uint32_t processId);
    [[nodiscard]] Result DisableAutostartAndTerminateShellHost(std::uint32_t processId);
}
