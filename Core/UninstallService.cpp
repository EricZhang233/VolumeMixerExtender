#include "UninstallService.h"

#include "AutostartEntry.h"

#include <windows.h>
#include <cwchar>

namespace vmex::uninstall
{
    Result TerminateShellHost(std::uint32_t processId)
    {
        if (processId == 0)
        {
            return { false, L"shellhost-pid-missing" };
        }

        const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                             FALSE, processId);
        if (!process)
        {
            return { false, L"shellhost-open-failed:" + std::to_wstring(::GetLastError()) };
        }

        wchar_t imagePath[MAX_PATH] = {};
        DWORD length = ARRAYSIZE(imagePath);
        const bool identified = ::QueryFullProcessImageNameW(process, 0, imagePath, &length) != FALSE;
        const std::wstring imageName = identified ? std::wstring(imagePath, length) : std::wstring();
        constexpr std::wstring_view kShellHostSuffix = L"ShellHost.exe";
        const bool isShellHost = imageName.size() >= kShellHostSuffix.size() &&
            _wcsicmp(imageName.c_str() + imageName.size() - kShellHostSuffix.size(),
                     kShellHostSuffix.data()) == 0;
        if (!isShellHost)
        {
            ::CloseHandle(process);
            return { false, L"target-is-not-shellhost" };
        }

        const bool terminated = ::TerminateProcess(process, 0) != FALSE;
        const DWORD error = terminated ? ERROR_SUCCESS : ::GetLastError();
        ::CloseHandle(process);
        if (!terminated)
        {
            return { false, L"shellhost-terminate-failed:" + std::to_wstring(error) };
        }

        return { true, {} };
    }

    Result DisableAutostartAndTerminateShellHost(std::uint32_t processId)
    {
        std::wstring error;
        if (!autostart::AutostartEntry::Disable(error))
        {
            return { false, L"autostart-disable-failed:" + error };
        }

        return TerminateShellHost(processId);
    }
}
