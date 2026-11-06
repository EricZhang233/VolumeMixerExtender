#pragma once

#include "AudioTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace vmex::settings
{
    inline constexpr std::wstring_view kRootKey = L"Software\\EricSoft\\VolumeMixerExtender";

    inline constexpr std::wstring_view kShowDriverName = L"ShowDriverName";

    [[nodiscard]] bool ReadBool(std::wstring_view name, bool fallback);
    [[nodiscard]] std::int32_t ReadInt(std::wstring_view name, std::int32_t fallback);
    [[nodiscard]] std::wstring ReadString(std::wstring_view name, std::wstring_view fallback);

    bool WriteBool(std::wstring_view name, bool value);
    bool WriteInt(std::wstring_view name, std::int32_t value);
    bool WriteString(std::wstring_view name, std::wstring_view value);

    [[nodiscard]] bool IsAudioSessionHidden(std::wstring_view appKey, std::wstring_view instanceId);
    bool SetAudioSessionHidden(std::wstring_view appKey, std::wstring_view instanceId, bool hidden);

    [[nodiscard]] audio::DeviceIcon ReadDeviceIcon(std::wstring_view endpointId);
    bool WriteDeviceIcon(std::wstring_view endpointId, audio::DeviceIcon icon);

    bool RemoveAll();
}
