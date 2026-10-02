#include "AudioDeviceManager.h"

#include "Logger.h"

namespace vmex::audio
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"audio";

        class StubAudioDeviceManager final : public IAudioDeviceManager
        {
        public:
            Status EnumerateDevices(DataFlow flow, DeviceState state, std::vector<DeviceInfo>& devices) override
            {
                devices.clear();
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Info,
                    kChannel,
                    L"log.audio.enumerate",
                    { std::wstring(ToString(flow)), std::wstring(ToString(state)) });
                return Unavailable(L"EnumerateDevices");
            }

            Status GetDefaultDevice(DataFlow flow, DeviceRole role, DeviceInfo& device) override
            {
                device = {};
                (void)flow;
                (void)role;
                return Unavailable(L"GetDefaultDevice");
            }

            Status GetVolume(std::wstring_view deviceId, EndpointVolume& volume) override
            {
                volume = {};
                (void)deviceId;
                return Unavailable(L"GetVolume");
            }

            Status SetVolume(std::wstring_view deviceId, float level) override
            {
                (void)deviceId;
                (void)level;
                return Unavailable(L"SetVolume");
            }

            Status SetMuted(std::wstring_view deviceId, bool muted) override
            {
                (void)deviceId;
                (void)muted;
                return Unavailable(L"SetMuted");
            }

            Status EnumerateSessions(std::wstring_view deviceId, std::vector<SessionInfo>& sessions) override
            {
                sessions.clear();
                (void)deviceId;
                return Unavailable(L"EnumerateSessions");
            }

            Status SetSessionVolume(std::wstring_view deviceId, std::uint32_t processId, float level) override
            {
                (void)deviceId;
                (void)processId;
                (void)level;
                return Unavailable(L"SetSessionVolume");
            }

            Status SetSessionMuted(std::wstring_view deviceId, std::uint32_t processId, bool muted) override
            {
                (void)deviceId;
                (void)processId;
                (void)muted;
                return Unavailable(L"SetSessionMuted");
            }

        private:
            static Status Unavailable(std::wstring_view operation)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel, L"log.audio.not_implemented", { std::wstring(operation) });
                return Status::NotSupported(std::wstring(operation));
            }
        };
    }

    std::unique_ptr<IAudioDeviceManager> CreateAudioDeviceManager()
    {
        return std::make_unique<StubAudioDeviceManager>();
    }
}
