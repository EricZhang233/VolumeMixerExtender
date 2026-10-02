#pragma once

#include "Foundation.h"

#include <windows.h>

namespace vmex::launcher
{
    using ProvideInitDataFn = const wchar_t*(__stdcall*)();
    using InitializeXamlDiagnosticsExFn = HRESULT(__stdcall*)(
        const wchar_t* endPointName,
        unsigned long pid,
        const wchar_t* applicationUserModelId,
        const wchar_t* tapDllPath,
        const wchar_t* tapDllClassId,
        const wchar_t* initializationData);

    [[nodiscard]] std::filesystem::path ModulePath(HMODULE module);
    [[nodiscard]] Status Run(const std::filesystem::path& tapModule);
}
