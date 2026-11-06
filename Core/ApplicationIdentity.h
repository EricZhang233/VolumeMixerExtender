#pragma once

#include <windows.h>
#include <appmodel.h>
#include <knownfolders.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <string>

namespace vmex::application
{
    inline std::wstring ProcessApplicationId(HANDLE process)
    {
        UINT32 length = 0;
        if (::GetApplicationUserModelId(process, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length <= 1) return {};
        std::wstring id(length, L'\0');
        if (::GetApplicationUserModelId(process, &length, id.data()) != ERROR_SUCCESS || length <= 1) return {};
        id.resize(length - 1);
        return id;
    }

    inline std::wstring ProcessImagePath(DWORD pid, bool* exited = nullptr, std::wstring* applicationId = nullptr)
    {
        if (exited) *exited = false;
        if (applicationId) applicationId->clear();
        std::wstring path;
        if (pid == 0) return path;
        const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process)
        {
            if (exited) *exited = ::GetLastError() == ERROR_INVALID_PARAMETER;
            return path;
        }
        DWORD exitCode = STILL_ACTIVE;
        if (::GetExitCodeProcess(process, &exitCode) && exitCode != STILL_ACTIVE)
        {
            if (exited) *exited = true;
        }
        else
        {
            if (applicationId) *applicationId = ProcessApplicationId(process);
            std::wstring buffer(1024, L'\0');
            for (;;)
            {
                DWORD size = static_cast<DWORD>(buffer.size());
                if (::QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
                {
                    buffer.resize(size);
                    path.swap(buffer);
                    break;
                }
                if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) break;
                buffer.resize(buffer.size() * 2);
            }
        }
        ::CloseHandle(process);
        return path;
    }

    inline Microsoft::WRL::ComPtr<IShellItem> FindRegisteredApplication(const std::wstring& id)
    {
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (!id.empty())
            ::SHCreateItemInKnownFolder(FOLDERID_AppsFolder, 0, id.c_str(), IID_PPV_ARGS(&item));
        return item;
    }
}
