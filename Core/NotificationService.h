#pragma once

#include "Foundation.h"

#include <string_view>

namespace vmex::notification
{
    [[nodiscard]] Status Show(std::wstring_view head, std::wstring_view body);
}
