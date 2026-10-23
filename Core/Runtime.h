#pragma once

#include "App.h"

#include <string>
#include <vector>

namespace vmex::runtime
{
    [[nodiscard]] std::vector<std::wstring> CollectArguments(int argc, wchar_t** argv);
    [[nodiscard]] AppOptions BuildOptions(const std::vector<std::wstring>& arguments,
                                          bool autorun = false);
    [[nodiscard]] std::vector<std::wstring> RemoveGlobalOptions(
        const std::vector<std::wstring>& arguments);
    [[nodiscard]] bool HasAutorunArgument();
}
