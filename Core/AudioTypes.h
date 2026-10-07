#pragma once

#include "Foundation.h"

#include <map>

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
        std::wstring instanceId;
        std::wstring appKey;
        std::wstring displayName;
        float volume = 0.0f;
        bool muted = false;
        SessionState state = SessionState::Inactive;
    };

    struct AppSessionInfo final
    {
        std::uint32_t processId = 0;
        std::wstring appKey;
        std::wstring displayName;
        std::wstring instanceId;
        std::wstring deviceId;
        std::wstring deviceName;
        float volume = 0.0f;
        bool muted = false;
        bool active = false;
        bool systemSounds = false;
    };

    class IAudioSessionHandle
    {
    public:
        virtual ~IAudioSessionHandle() = default;

        [[nodiscard]] virtual std::wstring_view InstanceId() const noexcept = 0;
        [[nodiscard]] virtual std::wstring_view DeviceId() const noexcept = 0;
        [[nodiscard]] virtual std::uint32_t ProcessId() const noexcept = 0;
        virtual Status GetState(float& volume, bool& muted) = 0;
        virtual Status GetPeak(float& left, float& right) = 0;
        virtual Status SetVolume(float level) = 0;
        virtual Status SetMuted(bool muted) = 0;
        virtual Status Rebind(std::wstring_view deviceId, std::wstring_view instanceId) = 0;
    };

    enum class DeviceEventKind
    {
        DefaultChanged = 0,
        ListChanged = 1,
        PropertyChanged = 2
    };

    struct DeviceEvent final
    {
        DeviceEventKind kind = DeviceEventKind::ListChanged;
        std::wstring deviceId;
    };

    class IAudioDeviceWatcher
    {
    public:
        virtual ~IAudioDeviceWatcher() = default;

        virtual Status Start(std::function<void(const DeviceEvent&)> handler) = 0;
        virtual void Stop() = 0;
    };

    struct EndpointSupport final
    {
        bool defaultDevice = false;
        bool perAppDevice = false;
        bool clearAppDevices = false;
    };

    [[nodiscard]] inline std::wstring ShortDeviceName(std::wstring_view friendlyName)
    {
        if (friendlyName.size() < 4 || friendlyName.back() != L')') return std::wstring(friendlyName);

        const std::size_t open = friendlyName.rfind(L" (");
        if (open == std::wstring_view::npos || open == 0) return std::wstring(friendlyName);
        return std::wstring(friendlyName.substr(0, open));
    }

    [[nodiscard]] inline std::wstring DisplayDeviceName(std::wstring_view friendlyName, bool showDriverName)
    {
        return showDriverName ? std::wstring(friendlyName) : ShortDeviceName(friendlyName);
    }

    [[nodiscard]] inline std::wstring DisplayDeviceName(const DeviceInfo& device, bool showDriverName)
    {
        return DisplayDeviceName(device.friendlyName, showDriverName);
    }
}
