#pragma once

#include "Foundation.h"

#include <windows.h>

namespace vmex::launcher
{
    using InitializeXamlDiagnosticsExFn = HRESULT(__stdcall*)(
        const wchar_t* endPointName,
        unsigned long pid,
        const wchar_t* xamlDiagnosticsDll,
        const wchar_t* tapDll,
        CLSID tapClsid,
        const wchar_t* initializationData);

    [[nodiscard]] std::filesystem::path ModulePath(HMODULE module);

    void SetSelfModule(HMODULE module);

    [[nodiscard]] HRESULT Inject(const std::filesystem::path& configModule);

    void BeginSelfInjection();
}
