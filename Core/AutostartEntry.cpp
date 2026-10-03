#include "AutostartEntry.h"

#include "Logger.h"
#include "Platform.h"
#include "TextService.h"

#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace vmex::autostart
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"autostart";

        constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
        constexpr wchar_t kApprovedKey[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

        constexpr wchar_t kHostExecutable[] = L"vmex.exe";
        constexpr wchar_t kHostArguments[] = L"--tray";

        constexpr std::size_t kApprovedHeaderBytes = 2;

        void Warn(std::wstring_view key, std::vector<std::wstring> args = {})
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel, key, args);
        }

        void Info(std::wstring_view key, std::vector<std::wstring> args = {})
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, key, args);
        }

        LSTATUS DeleteValue(const wchar_t* subKey, const wchar_t* name)
        {
            HKEY key = nullptr;
            const LSTATUS opened =
                ::RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_SET_VALUE, &key);
            if (opened != ERROR_SUCCESS)
            {
                return opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND
                    ? ERROR_SUCCESS
                    : opened;
            }

            const LSTATUS deleted = ::RegDeleteValueW(key, name);
            ::RegCloseKey(key);
            return deleted == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : deleted;
        }

        LSTATUS WriteString(const wchar_t* subKey, const wchar_t* name, const std::wstring& value)
        {
            HKEY key = nullptr;
            LSTATUS status = ::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                subKey,
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE,
                nullptr,
                &key,
                nullptr);
            if (status != ERROR_SUCCESS)
            {
                return status;
            }

            const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            status = ::RegSetValueExW(
                key,
                name,
                0,
                REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                bytes);
            ::RegCloseKey(key);
            return status;
        }

        bool ReadString(const wchar_t* subKey, const wchar_t* name, std::wstring& value,
                        LSTATUS& status)
        {
            DWORD bytes = 0;
            status = ::RegGetValueW(
                HKEY_CURRENT_USER, subKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
            if (status != ERROR_SUCCESS)
            {
                return false;
            }

            std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
            status = ::RegGetValueW(
                HKEY_CURRENT_USER, subKey, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
            if (status != ERROR_SUCCESS)
            {
                return false;
            }

            value.assign(buffer.data());
            return true;
        }

        bool IsSuppressedBySystem()
        {
            BYTE buffer[16] = {};
            DWORD bytes = sizeof(buffer);
            const LSTATUS status = ::RegGetValueW(
                HKEY_CURRENT_USER, kApprovedKey, AutostartEntry::kValueName, RRF_RT_REG_BINARY,
                nullptr, buffer, &bytes);
            if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            {
                return false;
            }
            if (status != ERROR_SUCCESS || bytes < kApprovedHeaderBytes)
            {
                return false;
            }

            return (buffer[0] & 0x01) != 0;
        }
    }

    bool AutostartEntry::IsEnabled()
    {
        std::wstring value;
        LSTATUS status = ERROR_SUCCESS;
        if (!ReadString(kRunKey, kValueName, value, status))
        {
            if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
            {
                Info(L"log.autostart.missing");
            }
            else
            {
                Warn(L"log.autostart.read_failed", { std::to_wstring(status) });
            }
            return false;
        }

        if (IsSuppressedBySystem())
        {
            Info(L"log.autostart.suppressed", { value });
            return false;
        }

        Info(L"log.autostart.present", { value });
        return true;
    }

    bool AutostartEntry::Enable(std::wstring& error)
    {
        error.clear();

        const std::wstring exe = (platform::GetInstallDirectory() / kHostExecutable).wstring();
        if (!platform::FileExists(exe))
        {
            error = text::Embedded().ResolveFormat(L"autostart.error.exe_missing", { exe });
            Warn(L"log.autostart.exe_missing", { exe });
            return false;
        }

        const std::wstring command = L"\"" + exe + L"\" " + kHostArguments;

        const LSTATUS written = WriteString(kRunKey, kValueName, command);
        if (written != ERROR_SUCCESS)
        {
            error = text::Embedded().ResolveFormat(L"autostart.error.write_failed",
                                                   { std::to_wstring(written) });
            Warn(L"log.autostart.write_failed", { command, std::to_wstring(written) });
            return false;
        }

        const LSTATUS cleared = DeleteValue(kApprovedKey, kValueName);
        if (cleared != ERROR_SUCCESS)
        {
            Warn(L"log.autostart.approved_clear_failed", { std::to_wstring(cleared) });
        }

        Info(L"log.autostart.registered", { command });
        return true;
    }

    bool AutostartEntry::Disable(std::wstring& error)
    {
        error.clear();

        const LSTATUS deleted = DeleteValue(kRunKey, kValueName);
        if (deleted != ERROR_SUCCESS)
        {
            error = text::Embedded().ResolveFormat(L"autostart.error.delete_failed",
                                                   { std::to_wstring(deleted) });
            Warn(L"log.autostart.delete_failed", { std::to_wstring(deleted) });
            return false;
        }

        const LSTATUS cleared = DeleteValue(kApprovedKey, kValueName);
        if (cleared != ERROR_SUCCESS)
        {
            Warn(L"log.autostart.approved_clear_failed", { std::to_wstring(cleared) });
        }

        Info(L"log.autostart.deleted");
        return true;
    }
}
