#pragma once

#include "AudioTypes.h"

namespace vmex::audio
{
    class IAudioDeviceManager
    {
    public:
        virtual ~IAudioDeviceManager() = default;

        virtual Status EnumerateDevices(DataFlow flow, DeviceState state, std::vector<DeviceInfo>& devices) = 0;
        virtual Status FindVirtualDevice(DeviceInfo& device) = 0;
        virtual Status GetDefaultDevice(DataFlow flow, DeviceRole role, DeviceInfo& device) = 0;
        virtual Status GetVolume(std::wstring_view deviceId, EndpointVolume& volume) = 0;
        virtual Status SetVolume(std::wstring_view deviceId, float level) = 0;
        virtual Status SetMuted(std::wstring_view deviceId, bool muted) = 0;
        virtual Status GetPeak(std::wstring_view deviceId, float& left, float& right) = 0;
        virtual Status EnumerateSessions(std::wstring_view deviceId, std::vector<SessionInfo>& sessions) = 0;
        virtual Status EnumerateAppSessions(const std::map<std::wstring, std::wstring>& preferredDevices,
                                            std::vector<AppSessionInfo>& sessions) = 0;
        virtual Status OpenSession(std::wstring_view deviceId, std::wstring_view instanceId,
                                   std::unique_ptr<IAudioSessionHandle>& handle) = 0;
    };

    [[nodiscard]] std::unique_ptr<IAudioDeviceManager> CreateAudioDeviceManager();
    [[nodiscard]] std::unique_ptr<IAudioDeviceWatcher> CreateAudioDeviceWatcher();
}
