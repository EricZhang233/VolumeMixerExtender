#include "StartMenuEntry.h"

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

namespace vmex::startmenu
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"startmenu";

        constexpr wchar_t kManagerExecutable[] = L"vmex_manager.exe";
        constexpr wchar_t kShortcutName[] = L"VolumeMixerExtender.lnk";

        void Warn(std::wstring_view message, std::vector<std::wstring> args = {}, std::source_location location = std::source_location::current())
        {
            log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel, message, args, location);
        }

        void Info(std::wstring_view message, std::vector<std::wstring> args = {}, std::source_location location = std::source_location::current())
        {
            log::Logger::Instance().WriteFormat(log::Level::Info, kChannel, message, args, location);
        }

        std::filesystem::path ProgramsFolderPath()
        {
            PWSTR raw = nullptr;
            const HRESULT hr =
                ::SHGetKnownFolderPath(FOLDERID_Programs, KF_FLAG_CREATE, nullptr, &raw);
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
            const auto programs = ProgramsFolderPath();
            if (programs.empty())
            {
                return {};
            }
            return programs / std::wstring(kShortcutName);
        }

        HRESULT CreateShortcut(const std::filesystem::path& link, const std::wstring& target)
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
    }

    bool StartMenuEntry::Register(std::wstring& error)
    {
        error.clear();

        const std::wstring exe = (platform::GetExecutableDirectory() / kManagerExecutable).wstring();
        if (!platform::FileExists(exe))
        {
            error = text::Embedded().ResolveFormat(L"startmenu.error.exe_missing", { exe });
            Warn(L"Start menu registration rejected; executable missing path={0}", { exe });
            return false;
        }

        const auto link = ShortcutPath();
        if (link.empty())
        {
            const std::wstring detail = std::to_wstring(static_cast<unsigned long>(E_FAIL));
            error = text::Embedded().ResolveFormat(L"startmenu.error.write_failed", { detail });
            Warn(L"Start menu shortcut creation failed target={0} detail={1}", { exe, detail });
            return false;
        }

        const HRESULT hr = CreateShortcut(link, exe);
        if (FAILED(hr))
        {
            const std::wstring detail = std::to_wstring(static_cast<unsigned long>(hr));
            error = text::Embedded().ResolveFormat(L"startmenu.error.write_failed", { detail });
            Warn(L"Start menu shortcut creation failed target={0} detail={1}", { exe, detail });
            return false;
        }

        Info(L"Start menu shortcut registered path={0}", { link.wstring() });
        return true;
    }
}
