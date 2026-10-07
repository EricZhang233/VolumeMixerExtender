#include "NotificationService.h"

#include "Logger.h"
#include "PayloadResources.h"
#include "Platform.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace vmex::notification
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"notification";

        std::wstring Quote(std::wstring_view value)
        {
            std::wstring quoted = L"\"";
            for (const wchar_t character : value)
            {
                if (character == L'"') quoted.push_back(L'\\');
                quoted.push_back(character);
            }
            quoted.push_back(L'"');
            return quoted;
        }

        Status EnsureScript(std::filesystem::path& script)
        {
            std::error_code error;
            const auto directory = platform::GetCacheDirectory();
            std::filesystem::create_directories(directory, error);
            if (error)
            {
                return Status::Failed(L"notification-cache");
            }

            script = directory / payload::DefaultFileName(payload::Item::NotificationToolkit);
            if (!platform::FileExists(script))
            {
                const auto extracted = payload::ExtractNotificationToolkit(script);
                if (!extracted.IsOk()) return extracted;
            }
            return Status::Ok();
        }
    }

    Status Show(std::wstring_view head, std::wstring_view body)
    {
        std::filesystem::path script;
        const auto prepared = EnsureScript(script);
        if (!prepared.IsOk())
        {
            log::Logger::Instance().Write(log::Level::Warn, kChannel, prepared.detail);
            return prepared;
        }

        wchar_t windowsDirectory[MAX_PATH] = {};
        const DWORD length = ::GetEnvironmentVariableW(L"WINDIR", windowsDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return Status::FromLastError(L"notification-windir");
        }

        const std::filesystem::path executable =
            std::filesystem::path(windowsDirectory) / L"System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        std::wstring command = Quote(executable.wstring()) +
            L" -NoProfile -ExecutionPolicy Bypass -File " + Quote(script.wstring()) +
            L" -Head " + Quote(head) + L" -Body " + Quote(body) + L" -Image info";

        std::vector<wchar_t> commandLine(command.begin(), command.end());
        commandLine.push_back(L'\0');
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        if (!::CreateProcessW(
                executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            return Status::FromLastError(L"notification-start");
        }

        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        log::Logger::Instance().Write(log::Level::Info, kChannel, L"notification-started");
        return Status::Ok();
    }
}
