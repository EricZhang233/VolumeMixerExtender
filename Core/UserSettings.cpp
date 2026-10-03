#include "UserSettings.h"

#include "Logger.h"

#include <windows.h>

namespace vmex::settings
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"settings";

        bool WriteRaw(std::wstring_view name, DWORD type, const void* data, DWORD bytes)
        {
            HKEY key = nullptr;
            const LSTATUS opened = ::RegCreateKeyExW(
                HKEY_CURRENT_USER, std::wstring(kRootKey).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE, nullptr, &key, nullptr);
            if (opened != ERROR_SUCCESS)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
                    L"log.settings.reg_create_failed", { std::to_wstring(opened) });
                return false;
            }

            const LSTATUS written = ::RegSetValueExW(
                key, std::wstring(name).c_str(), 0, type, static_cast<const BYTE*>(data), bytes);
            ::RegCloseKey(key);

            if (written != ERROR_SUCCESS)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
                    L"log.settings.reg_set_failed", { std::wstring(name), std::to_wstring(written) });
                return false;
            }
            return true;
        }

        bool ReadRaw(std::wstring_view name, DWORD type, void* data, DWORD* bytes)
        {
            const LSTATUS status = ::RegGetValueW(
                HKEY_CURRENT_USER, std::wstring(kRootKey).c_str(), std::wstring(name).c_str(),
                type, nullptr, data, bytes);
            return status == ERROR_SUCCESS;
        }
    }

    bool ReadBool(std::wstring_view name, bool fallback)
    {
        DWORD value = 0;
        DWORD bytes = sizeof(value);
        if (!ReadRaw(name, RRF_RT_REG_DWORD, &value, &bytes)) return fallback;
        return value != 0;
    }

    std::int32_t ReadInt(std::wstring_view name, std::int32_t fallback)
    {
        DWORD value = 0;
        DWORD bytes = sizeof(value);
        if (!ReadRaw(name, RRF_RT_REG_DWORD, &value, &bytes)) return fallback;
        return static_cast<std::int32_t>(value);
    }

    std::wstring ReadString(std::wstring_view name, std::wstring_view fallback)
    {
        const std::wstring wide(name);

        DWORD bytes = 0;
        if (!ReadRaw(wide, RRF_RT_REG_SZ, nullptr, &bytes) || bytes < sizeof(wchar_t)) return std::wstring(fallback);

        std::wstring value(bytes / sizeof(wchar_t), L'\0');
        if (!ReadRaw(wide, RRF_RT_REG_SZ, value.data(), &bytes)) return std::wstring(fallback);

        const auto terminator = value.find(L'\0');
        if (terminator != std::wstring::npos) value.resize(terminator);
        return value;
    }

    bool WriteBool(std::wstring_view name, bool value)
    {
        const DWORD data = value ? 1u : 0u;
        return WriteRaw(name, REG_DWORD, &data, sizeof(data));
    }

    bool WriteInt(std::wstring_view name, std::int32_t value)
    {
        const DWORD data = static_cast<DWORD>(value);
        return WriteRaw(name, REG_DWORD, &data, sizeof(data));
    }

    bool WriteString(std::wstring_view name, std::wstring_view value)
    {
        const std::wstring text(value);
        return WriteRaw(name, REG_SZ, text.c_str(),
                        static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
    }

    bool RemoveAll()
    {
        const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, std::wstring(kRootKey).c_str());
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) return true;

        log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel,
            L"log.settings.reg_delete_failed", { std::to_wstring(status) });
        return false;
    }
}
