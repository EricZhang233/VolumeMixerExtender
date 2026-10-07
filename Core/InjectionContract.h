#pragma once

#include <string>
#include <string_view>

namespace vmex::inject
{
    inline constexpr std::wstring_view kTapClassId = L"{A7C5F1E2-9B34-4D6E-8F21-5C0D3E7A9B44}";

    inline constexpr std::wstring_view kLauncherDllName = L"vmex_launcher.dll";
    inline constexpr std::wstring_view kTapDllName = L"vmex_tap.dll";
    inline constexpr std::wstring_view kCoreDllName = L"vmex_core.dll";

    inline constexpr std::string_view kTapInitDataExport = "VmExtTapProvideInitData";

    inline constexpr std::string_view kLauncherRunExport = "VmExtLauncherRun";

    inline constexpr std::string_view kXamlEntryPointExport = "InitializeXamlDiagnosticsEx";

    inline constexpr std::wstring_view kVirtualDeviceTarget = L"@virtual";

    [[nodiscard]] inline std::wstring DiagEndPointName(unsigned long sessionId)
    {
        return L"VisualDiagConnection" + std::to_wstring(sessionId);
    }

    [[nodiscard]] inline std::wstring TapPipeName(unsigned long sessionId)
    {
        return L"\\\\.\\pipe\\VmExt.Tap.S" + std::to_wstring(sessionId);
    }

    [[nodiscard]] inline std::wstring HostMutexName(unsigned long sessionId)
    {
        return L"Local\\VmExt.Host.S" + std::to_wstring(sessionId);
    }

    inline constexpr std::wstring_view kTapConfigFileName = L"vmex_tap.ini";
    inline constexpr std::wstring_view kTapConfigSection = L"vmex";

    inline constexpr std::wstring_view kConfigKeyTap = L"tap";
    inline constexpr std::wstring_view kConfigKeyDiag = L"diag";
    inline constexpr std::wstring_view kConfigKeyEndpoint = L"endpoint";
    inline constexpr std::wstring_view kConfigKeySession = L"session";
    inline constexpr std::wstring_view kConfigKeyXamlWaitMs = L"xamlwaitms";
}
