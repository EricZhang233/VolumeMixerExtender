#include "AudioRoutingCleanup.h"

#include "AudioInterop.h"
#include "Platform.h"
#include <tlhelp32.h>

namespace vmex::recording
{
    Status ClearEndpointPreferences(HKEY root, std::wstring_view endpoint)
    {
        if (endpoint.empty()) return Status::InvalidArguments(L"cleanup-endpoint");
        DWORD maxName = 0, maxData = 0, maxKey = 0;
        auto error = ::RegQueryInfoKeyW(root, nullptr, nullptr, nullptr, nullptr, &maxKey, nullptr,
            nullptr, &maxName, &maxData, nullptr, nullptr);
        if (error != ERROR_SUCCESS) return Status::FromWin32(L"routing-registry-query", error);
        std::vector<wchar_t> name(static_cast<size_t>(maxName) + 1);
        std::vector<wchar_t> value(static_cast<size_t>(maxData) / sizeof(wchar_t) + 2);
        std::vector<std::wstring> remove;
        for (DWORD index = 0;; ++index)
        {
            DWORD characters = static_cast<DWORD>(name.size()), bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t)), type = 0;
            error = ::RegEnumValueW(root, index, name.data(), &characters, nullptr, &type,
                reinterpret_cast<BYTE*>(value.data()), &bytes);
            if (error == ERROR_NO_MORE_ITEMS) break;
            if (error != ERROR_SUCCESS) return Status::FromWin32(L"routing-registry-enum", error);
            if (type != REG_SZ || bytes % sizeof(wchar_t)) continue;
            value[bytes / sizeof(wchar_t)] = 0;
            const auto device = audio::detail::UnpackDeviceId(value.data());
            if (::_wcsicmp(device.c_str(), std::wstring(endpoint).c_str()) == 0)
                remove.emplace_back(name.data(), characters);
        }
        for (const auto& entry : remove)
        {
            error = ::RegDeleteValueW(root, entry.c_str());
            if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
                return Status::FromWin32(L"routing-registry-delete", error);
            error = ::RegDeleteValueW(root, (entry + L"_p").c_str());
            if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
                return Status::FromWin32(L"routing-registry-delete", error);
        }
        std::vector<wchar_t> childName(static_cast<size_t>(maxKey) + 1);
        for (DWORD index = 0;; ++index)
        {
            DWORD characters = static_cast<DWORD>(childName.size());
            error = ::RegEnumKeyExW(root, index, childName.data(), &characters, nullptr, nullptr, nullptr, nullptr);
            if (error == ERROR_NO_MORE_ITEMS) break;
            if (error != ERROR_SUCCESS) return Status::FromWin32(L"routing-registry-keys", error);
            HKEY child = nullptr;
            error = ::RegOpenKeyExW(root, childName.data(), 0, KEY_READ | KEY_SET_VALUE, &child);
            if (error != ERROR_SUCCESS) return Status::FromWin32(L"routing-registry-open", error);
            const auto result = ClearEndpointPreferences(child, endpoint);
            ::RegCloseKey(child);
            if (!result) return result;
        }
        return Status::Ok();
    }

    Status ClearVirtualRedirects(std::wstring_view endpoint)
    {
        if (endpoint.empty()) return Status::Ok();
        const auto snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return Status::FromLastError(L"routing-processes");
        bool complete = true;
        PROCESSENTRY32W process{ sizeof(process) };
        if (::Process32FirstW(snapshot, &process)) do
        {
            DWORD session = 0;
            if (!process.th32ProcessID || !::ProcessIdToSessionId(process.th32ProcessID, &session) ||
                session != platform::GetCurrentSessionId()) continue;
            for (const auto role : { eConsole, eMultimedia, eCommunications })
            {
                std::wstring current;
                if (audio::detail::GetPersistedEndpointForRole(eRender, process.th32ProcessID, role, current) && current == endpoint)
                    complete = audio::detail::SetPersistedEndpointForRole(eRender, process.th32ProcessID, role, L"") && complete;
            }
        } while (::Process32NextW(snapshot, &process));
        ::CloseHandle(snapshot);
        HKEY root = nullptr;
        const auto opened = ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\Audio\\DefaultEndpoint",
            0, KEY_READ | KEY_SET_VALUE, &root);
        if (opened != ERROR_SUCCESS && opened != ERROR_FILE_NOT_FOUND)
            return Status::FromWin32(L"routing-preferences-open", opened);
        if (root)
        {
            const auto result = ClearEndpointPreferences(root, endpoint);
            ::RegCloseKey(root);
            if (!result) return result;
        }
        log::Logger::Instance().WriteKeyFormat(complete ? log::Level::Info : log::Level::Error, L"recording",
            L"log.recording.routingCleared", { std::wstring(endpoint) });
        return complete ? Status::Ok() : Status::Failed(L"recording-redirect-cleanup");
    }
}
