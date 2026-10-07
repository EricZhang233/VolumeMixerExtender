#include "AutostartEntry.h"

#include "Logger.h"
#include "Platform.h"
#include "TextService.h"

#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>
#include <string_view>
#include <vector>

namespace vmex::autostart
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"autostart";

        constexpr wchar_t kStartupApprovedKey[] =
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

        constexpr wchar_t kHostExecutable[] = L"vmex.exe";
        constexpr wchar_t kHostArguments[] = L"-autorun";
        constexpr wchar_t kShortcutName[] = L"VolumeMixerExtender.lnk";

        constexpr std::size_t kApprovedHeaderBytes = 2;

        void Warn(std::wstring_view key, std::vector<std::wstring> args = {})
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel, key, args);
        }

        void Info(std::wstring_view key, std::vector<std::wstring> args = {})
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Info, kChannel, key, args);
        }

        std::filesystem::path StartupFolderPath()
        {
            PWSTR raw = nullptr;
            const HRESULT hr =
                ::SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_CREATE, nullptr, &raw);
            if (FAILED(hr) || raw == nullptr)
            {
                return {};
            }
            std::filesystem::path path(raw);
            ::CoTaskMemFree(raw);
            return path;
        }

        std::filesystem::path ShortcutPath()
        {
            const auto startup = StartupFolderPath();
            if (startup.empty())
            {
                return {};
            }
            return startup / std::wstring(kShortcutName);
        }

        HRESULT CreateShortcut(const std::filesystem::path& link, const std::wstring& target,
                               const std::wstring& arguments)
        {
            IShellLinkW* shellLink = nullptr;
            HRESULT hr = ::CoCreateInstance(
                CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellLinkW),
                reinterpret_cast<void**>(&shellLink));
            if (FAILED(hr))
            {
                return hr;
            }

            shellLink->SetPath(target.c_str());
            shellLink->SetArguments(arguments.c_str());
            shellLink->SetShowCmd(SW_SHOWNORMAL);

            IPersistFile* persist = nullptr;
            hr = shellLink->QueryInterface(
                __uuidof(IPersistFile), reinterpret_cast<void**>(&persist));
            if (SUCCEEDED(hr))
            {
                hr = persist->Save(link.c_str(), TRUE);
                persist->Release();
            }
            shellLink->Release();
            return hr;
        }

        bool IsSuppressedBySystem()
        {
            for (const wchar_t* name : { kShortcutName, AutostartEntry::kValueName })
            {
                BYTE buffer[16] = {};
                DWORD bytes = sizeof(buffer);
                const LSTATUS status = ::RegGetValueW(
                    HKEY_CURRENT_USER, kStartupApprovedKey, name, RRF_RT_REG_BINARY,
                    nullptr, buffer, &bytes);
                if (status == ERROR_SUCCESS && bytes >= kApprovedHeaderBytes &&
                    (buffer[0] & 0x01) != 0)
                {
                    return true;
                }
            }
            return false;
        }

        void ClearSuppression()
        {
            HKEY key = nullptr;
            if (::RegOpenKeyExW(
                    HKEY_CURRENT_USER, kStartupApprovedKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
            {
                return;
            }
            for (const wchar_t* name : { kShortcutName, AutostartEntry::kValueName })
            {
                ::RegDeleteValueW(key, name);
            }
            ::RegCloseKey(key);
        }
    }

    bool AutostartEntry::IsEnabled()
    {
        const auto link = ShortcutPath();
        if (link.empty() || !platform::FileExists(link))
        {
            Info(L"log.autostart.missing");
            return false;
        }

        if (IsSuppressedBySystem())
        {
            Info(L"log.autostart.suppressed", { link.wstring() });
            return false;
        }

        Info(L"log.autostart.present", { link.wstring() });
        return true;
    }

    bool AutostartEntry::Enable(std::wstring& error)
    {
        error.clear();

        const std::wstring exe = (platform::GetExecutableDirectory() / kHostExecutable).wstring();
        if (!platform::FileExists(exe))
        {
            error = text::Embedded().ResolveFormat(L"autostart.error.exe_missing", { exe });
            Warn(L"log.autostart.exe_missing", { exe });
            return false;
        }

        const auto link = ShortcutPath();
        if (link.empty())
        {
            const std::wstring detail = std::to_wstring(static_cast<unsigned long>(E_FAIL));
            error = text::Embedded().ResolveFormat(L"autostart.error.write_failed", { detail });
            Warn(L"log.autostart.write_failed", { exe, detail });
            return false;
        }

        const std::wstring command = L"\"" + exe + L"\" " + kHostArguments;
        const HRESULT hr = CreateShortcut(link, exe, std::wstring(kHostArguments));
        if (FAILED(hr))
        {
            const std::wstring detail = std::to_wstring(static_cast<unsigned long>(hr));
            error = text::Embedded().ResolveFormat(L"autostart.error.write_failed", { detail });
            Warn(L"log.autostart.write_failed", { command, detail });
            return false;
        }

        ClearSuppression();
        Info(L"log.autostart.registered", { command });
        return true;
    }

    bool AutostartEntry::Disable(std::wstring& error)
    {
        error.clear();

        const auto link = ShortcutPath();
        if (!link.empty() && platform::FileExists(link) && ::DeleteFileW(link.c_str()) == FALSE)
        {
            const std::wstring detail = std::to_wstring(::GetLastError());
            error = text::Embedded().ResolveFormat(L"autostart.error.delete_failed", { detail });
            Warn(L"log.autostart.delete_failed", { detail });
            return false;
        }

        ClearSuppression();
        Info(L"log.autostart.deleted");
        return true;
    }
}
