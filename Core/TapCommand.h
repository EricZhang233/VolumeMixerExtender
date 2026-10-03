#pragma once

#include "Foundation.h"

namespace vmex::inject
{
    enum class TapVerb
    {
        Click,
        SetDefault,
        SetRedirect,
        ClearRedirect,
        Uninstall
    };

    struct TapCommand final
    {
        TapVerb verb = TapVerb::Click;
        std::wstring raw;
        std::vector<std::wstring> arguments;
        bool argumentsValid = true;
    };

    [[nodiscard]] std::wstring_view ToString(TapVerb verb);

    [[nodiscard]] std::optional<TapCommand> ParseTapCommand(std::string_view line);
}
