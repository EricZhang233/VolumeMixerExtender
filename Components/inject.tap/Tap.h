#pragma once

#include "Foundation.h"

#include <windows.h>

namespace vmex::tap
{
    void SetSelfModule(HMODULE module);
    [[nodiscard]] std::filesystem::path ModuleDirectory();

    void AttachLoggingOnce();

    void ProvideInitData(const wchar_t* config);

    [[nodiscard]] std::filesystem::path ConfigModulePath();
}
