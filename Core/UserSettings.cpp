#include "UserSettings.h"

#include "Logger.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

namespace vmex::settings
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"settings";
        const std::wstring kHiddenSessionsKey = std::wstring(kRootKey) + L"\\HiddenAudioSessions";
        const std::wstring kDeviceIconsKey = std::wstring(kRootKey) + L"\\DeviceIcons";

        std::wstring SessionVisibilityValueName(std::wstring_view appKey, std::wstring_view instanceId)
        {
            const bool useInstance = appKey.empty() || appKey.starts_with(L"#pid");
            const auto key = useInstance ? instanceId : appKey;
            if (key.empty() || key.size() >= MAXDWORD / sizeof(wchar_t)) return {};

            std::wstring identity(1, useInstance ? L'i' : L'a');
            identity.append(key);
            BYTE digest[32]{};
            DWORD size = sizeof(digest);
            if (!::CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr,
                    reinterpret_cast<const BYTE*>(identity.data()),
                    static_cast<DWORD>(identity.size() * sizeof(wchar_t)), digest, &size) || size != sizeof(digest))
                return {};

            constexpr wchar_t digits[] = L"0123456789abcdef";
            std::wstring name;
            name.reserve(sizeof(digest) * 2);
            for (const auto byte : digest)
            {
                name.push_back(digits[byte >> 4]);
                name.push_back(digits[byte & 0xf]);
            }
            return name;
        }

        bool WriteRaw(std::wstring_view name, DWORD type, const void* data, DWORD bytes,
                      std::wstring_view root = kRootKey)
        {
            HKEY key = nullptr;
            const LSTATUS opened = ::RegCreateKeyExW(
                HKEY_CURRENT_USER, std::wstring(root).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE, nullptr, &key, nullptr);
            if (opened != ERROR_SUCCESS)
            {
                log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                    L"Registry key creation failed error={0}", { std::to_wstring(opened) });
                return false;
            }

            const LSTATUS written = ::RegSetValueExW(
                key, std::wstring(name).c_str(), 0, type, static_cast<const BYTE*>(data), bytes);
            ::RegCloseKey(key);

            if (written != ERROR_SUCCESS)
            {
                log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                    L"Registry value write failed name={0} error={1}", { std::wstring(name), std::to_wstring(written) });
                return false;
            }
            return true;
        }

        bool ReadRaw(std::wstring_view name, DWORD type, void* data, DWORD* bytes,
                     std::wstring_view root = kRootKey)
        {
            const LSTATUS status = ::RegGetValueW(
                HKEY_CURRENT_USER, std::wstring(root).c_str(), std::wstring(name).c_str(),
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

    bool IsAudioSessionHidden(std::wstring_view appKey, std::wstring_view instanceId)
    {
        const auto name = SessionVisibilityValueName(appKey, instanceId);
        DWORD value = 0;
        DWORD bytes = sizeof(value);
        return !name.empty() && ReadRaw(name, RRF_RT_REG_DWORD, &value, &bytes, kHiddenSessionsKey) && value != 0;
    }

    bool SetAudioSessionHidden(std::wstring_view appKey, std::wstring_view instanceId, bool hidden)
    {
        const auto name = SessionVisibilityValueName(appKey, instanceId);
        const DWORD value = hidden ? 1u : 0u;
        return !name.empty() && WriteRaw(name, REG_DWORD, &value, sizeof(value), kHiddenSessionsKey);
    }

    audio::DeviceIcon ReadDeviceIcon(std::wstring_view endpointId)
    {
        DWORD value = 0;
        DWORD bytes = sizeof(value);
        if (endpointId.empty() || !ReadRaw(endpointId, RRF_RT_REG_DWORD, &value, &bytes, kDeviceIconsKey) ||
            value >= static_cast<DWORD>(audio::DeviceIcon::Count))
            return audio::DeviceIcon::Recommended;
        return static_cast<audio::DeviceIcon>(value);
    }

    bool WriteDeviceIcon(std::wstring_view endpointId, audio::DeviceIcon icon)
    {
        const auto value = static_cast<DWORD>(icon);
        if (endpointId.empty() || value >= static_cast<DWORD>(audio::DeviceIcon::Count) ||
            !WriteRaw(endpointId, REG_DWORD, &value, sizeof(value), kDeviceIconsKey)) return false;
        log::Logger::Instance().WriteFormat(log::Level::Debug, kChannel,
            L"Device icon preference saved selection={0}", { std::to_wstring(value) });
        return true;
    }

    bool RemoveAll()
    {
        const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, std::wstring(kRootKey).c_str());
        if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) return true;

        log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
            L"Registry key deletion failed error={0}", { std::to_wstring(status) });
        return false;
    }
}
