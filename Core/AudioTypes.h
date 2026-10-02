#pragma once

#include "Foundation.h"

namespace vmex::audio
{
    enum class DataFlow
    {
        Render = 0,
        Capture = 1,
        All = 2
    };

    enum class DeviceRole
    {
        Console = 0,
        Multimedia = 1,
        Communications = 2
    };

    enum class DeviceState
    {
        Active = 0,
        Disabled = 1,
        NotPresent = 2,
        Unplugged = 3,
        All = 4
    };

    enum class SessionState
    {
        Inactive = 0,
        Active = 1,
        Expired = 2
    };

    inline std::wstring_view ToString(DataFlow flow)
    {
        switch (flow)
        {
        case DataFlow::Render: return L"render";
        case DataFlow::Capture: return L"capture";
        default: return L"all";
        }
    }

    inline std::wstring_view ToString(DeviceRole role)
    {
        switch (role)
        {
        case DeviceRole::Console: return L"console";
        case DeviceRole::Multimedia: return L"multimedia";
        default: return L"communications";
        }
    }

    inline std::wstring_view ToString(DeviceState state)
    {
        switch (state)
        {
        case DeviceState::Active: return L"active";
        case DeviceState::Disabled: return L"disabled";
        case DeviceState::NotPresent: return L"not-present";
        case DeviceState::Unplugged: return L"unplugged";
        default: return L"all";
        }
    }

    inline std::wstring_view ToString(SessionState state)
    {
        switch (state)
        {
        case SessionState::Inactive: return L"inactive";
        case SessionState::Active: return L"active";
        default: return L"expired";
        }
    }

    inline bool TryParseDataFlow(std::wstring_view text, DataFlow& flow)
    {
        if (text == L"render") { flow = DataFlow::Render; return true; }
        if (text == L"capture") { flow = DataFlow::Capture; return true; }
        if (text == L"all") { flow = DataFlow::All; return true; }
        return false;
    }

    inline bool TryParseDeviceRole(std::wstring_view text, DeviceRole& role)
    {
        if (text == L"console") { role = DeviceRole::Console; return true; }
        if (text == L"multimedia") { role = DeviceRole::Multimedia; return true; }
        if (text == L"communications") { role = DeviceRole::Communications; return true; }
        return false;
    }

    inline bool TryParseDeviceState(std::wstring_view text, DeviceState& state)
    {
        if (text == L"active") { state = DeviceState::Active; return true; }
        if (text == L"disabled") { state = DeviceState::Disabled; return true; }
        if (text == L"not-present") { state = DeviceState::NotPresent; return true; }
        if (text == L"unplugged") { state = DeviceState::Unplugged; return true; }
        if (text == L"all") { state = DeviceState::All; return true; }
        return false;
    }

    struct EndpointVolume final
    {
        float level = 0.0f;
        bool muted = false;
    };

    struct DeviceInfo final
    {
        std::wstring id;
        std::wstring friendlyName;
        std::wstring description;
        DeviceState state = DeviceState::Active;
        bool isDefault = false;
    };

    struct SessionInfo final
    {
        std::uint32_t processId = 0;
        std::wstring appKey;
        std::wstring displayName;
        float volume = 0.0f;
        bool muted = false;
        SessionState state = SessionState::Inactive;
    };

    struct EndpointSupport final
    {
        bool defaultDevice = false;
        bool perAppDevice = false;
        bool clearAppDevices = false;
    };
}
