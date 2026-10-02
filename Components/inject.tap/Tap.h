#pragma once

#include "Foundation.h"

namespace vmex::tap
{
    [[nodiscard]] const wchar_t* ProvideInitData();
    [[nodiscard]] std::filesystem::path ConfigModulePath();
}
