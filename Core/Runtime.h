#pragma once

#include "App.h"

#include <string>
#include <vector>

namespace vmex::runtime
{
    [[nodiscard]] std::vector<std::wstring> Arguments();
    [[nodiscard]] AppOptions BuildOptions(bool autorun = false);
    [[nodiscard]] bool HasAutorunArgument();
}
