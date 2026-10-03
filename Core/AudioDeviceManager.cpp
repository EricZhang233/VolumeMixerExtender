#include "AudioDeviceManager.h"

#include "AudioInterop.h"
#include "Logger.h"

namespace vmex::audio
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"audio";

        EDataFlow ToFlow(DataFlow flow)
        {
            return flow == DataFlow::Capture ? eCapture : eRender;
        }

        ERole ToRole(DeviceRole role)
        {
            switch (role)
            {
            case DeviceRole::Console: return eConsole;
            case DeviceRole::Communications: return eCommunications;
            default: return eMultimedia;
            }
        }

        DeviceInfo ToDeviceInfo(const detail::Endpoint& endpoint)
        {
            DeviceInfo info;
            info.id = endpoint.id;
            info.friendlyName = endpoint.friendly;
            info.description = endpoint.interfaceName;
            info.state = DeviceState::Active;
            info.isDefault = endpoint.isDefault;
            return info;
        }

        SessionInfo ToSessionInfo(const detail::Session& session)
        {
            SessionInfo info;
            info.processId = session.pid;
            info.instanceId = session.id;
            info.appKey = session.key;
            info.displayName = session.name;
            info.volume = session.volume;
            info.muted = session.mute;
            info.state = SessionState::Active;
            return info;
        }

        class AudioSessionHandle final : public IAudioSessionHandle
        {
        public:
            AudioSessionHandle(std::wstring deviceId, std::wstring instanceId, std::uint32_t processId,
                               detail::Ptr<ISimpleAudioVolume> volume)
                : m_deviceId(std::move(deviceId))
                , m_instanceId(std::move(instanceId))
                , m_processId(processId)
                , m_volume(std::move(volume))
            {
            }

            [[nodiscard]] std::wstring_view InstanceId() const noexcept override { return m_instanceId; }
            [[nodiscard]] std::uint32_t ProcessId() const noexcept override { return m_processId; }

            Status GetState(float& volume, bool& muted) override
            {
                volume = 0.0f;
                muted = false;

                if (!m_volume || FAILED(m_volume->GetMasterVolume(&volume)))
                {
                    if (!Reacquire()) return Status::Failed(L"session volume read failed");
                    if (FAILED(m_volume->GetMasterVolume(&volume))) return Status::Failed(L"session volume read failed");
                }

                BOOL flag = FALSE;
                if (FAILED(m_volume->GetMute(&flag))) return Status::Failed(L"session mute read failed");
                muted = (flag != FALSE);
                return Status::Ok();
            }

            Status SetVolume(float level) override
            {
                if (level < 0.0f || level > 1.0f)
                {
                    return Status::InvalidArguments(L"SetVolume: level must be within 0..1");
                }

                if (m_volume && SUCCEEDED(m_volume->SetMasterVolume(level, nullptr))) return Status::Ok();
                if (Reacquire() && SUCCEEDED(m_volume->SetMasterVolume(level, nullptr))) return Status::Ok();
                return Status::Failed(L"ISimpleAudioVolume::SetMasterVolume failed after re-resolve");
            }

            Status SetMuted(bool muted) override
            {
                const BOOL want = muted ? TRUE : FALSE;
                if (m_volume && SUCCEEDED(m_volume->SetMute(want, nullptr))) return Status::Ok();
                if (Reacquire() && SUCCEEDED(m_volume->SetMute(want, nullptr))) return Status::Ok();
                return Status::Failed(L"ISimpleAudioVolume::SetMute failed after re-resolve");
            }

        private:
            [[nodiscard]] bool Reacquire()
            {
                m_volume = detail::ResolveSessionVolume(m_deviceId, m_instanceId);
                return static_cast<bool>(m_volume);
            }

            std::wstring m_deviceId;
            std::wstring m_instanceId;
            std::uint32_t m_processId = 0;
            detail::Ptr<ISimpleAudioVolume> m_volume;
        };

        class WasapiAudioDeviceManager final : public IAudioDeviceManager
        {
        public:
            Status EnumerateDevices(DataFlow flow, DeviceState state, std::vector<DeviceInfo>& devices) override
            {
                devices.clear();
                if (flow == DataFlow::All)
                {
                    return Status::InvalidArguments(
                        L"EnumerateDevices: DataFlow::All is not supported; ask render and capture separately");
                }

                if (state != DeviceState::Active)
                {
                    log::Logger::Instance().WriteKey(log::Level::Debug, kChannel, L"log.audio.state_ignored");
                }

                std::vector<detail::Endpoint> endpoints;
                if (!detail::EnumEndpoints(ToFlow(flow), endpoints))
                {
                    return Status::Failed(L"EnumAudioEndpoints failed (no device enumerator?)");
                }

                devices.reserve(endpoints.size());
                for (const auto& endpoint : endpoints) devices.push_back(ToDeviceInfo(endpoint));

                log::Logger::Instance().WriteKeyFormat(log::Level::Debug, kChannel, L"log.audio.enumerated",
                    { std::wstring(ToString(flow)), std::to_wstring(devices.size()) });
                return Status::Ok();
            }

            Status GetDefaultDevice(DataFlow flow, DeviceRole role, DeviceInfo& device) override
            {
                device = {};

                std::wstring id;
                if (!detail::GetDefaultEndpointId(ToFlow(flow), id))
                {
                    return Status::Failed(L"GetDefaultAudioEndpoint failed");
                }

                device.id = id;
                std::vector<detail::Endpoint> endpoints;
                if (detail::EnumEndpoints(ToFlow(flow), endpoints))
                {
                    for (const auto& endpoint : endpoints)
                    {
                        if (endpoint.id == id)
                        {
                            device = ToDeviceInfo(endpoint);
                            device.isDefault = true;
                            break;
                        }
                    }
                }

                (void)role;
                return Status::Ok();
            }

            Status GetVolume(std::wstring_view deviceId, EndpointVolume& volume) override
            {
                volume = {};

                float level = 0.0f;
                bool muted = false;
                if (!detail::GetEndpointState(std::wstring(deviceId), level, muted))
                {
                    return Status::Failed(L"IAudioEndpointVolume read failed");
                }

                volume.level = level;
                volume.muted = muted;
                return Status::Ok();
            }

            Status SetVolume(std::wstring_view deviceId, float level) override
            {
                if (level < 0.0f || level > 1.0f)
                {
                    return Status::InvalidArguments(L"SetVolume: level must be within 0..1");
                }

                return detail::SetEndpointVolume(std::wstring(deviceId), level)
                    ? Status::Ok()
                    : Status::Failed(L"IAudioEndpointVolume::SetMasterVolumeLevelScalar failed");
            }

            Status SetMuted(std::wstring_view deviceId, bool muted) override
            {
                return detail::SetEndpointMute(std::wstring(deviceId), muted)
                    ? Status::Ok()
                    : Status::Failed(L"IAudioEndpointVolume::SetMute failed");
            }

            Status EnumerateSessions(std::wstring_view deviceId, std::vector<SessionInfo>& sessions) override
            {
                sessions.clear();

                std::vector<detail::Session> found;
                if (!detail::EnumSessions(std::wstring(deviceId), found))
                {
                    return Status::Failed(L"IAudioSessionManager2::GetSessionEnumerator failed");
                }

                sessions.reserve(found.size());
                for (const auto& session : found) sessions.push_back(ToSessionInfo(session));

                log::Logger::Instance().WriteKeyFormat(log::Level::Debug, kChannel, L"log.audio.sessions",
                    { std::wstring(deviceId), std::to_wstring(sessions.size()) });

                return Status::Ok();
            }

            Status OpenSession(std::wstring_view deviceId, std::wstring_view instanceId,
                               std::unique_ptr<IAudioSessionHandle>& handle) override
            {
                handle.reset();
                if (deviceId.empty() || instanceId.empty())
                {
                    return Status::InvalidArguments(L"OpenSession: deviceId and instanceId are both required");
                }

                auto volume = detail::ResolveSessionVolume(std::wstring(deviceId), std::wstring(instanceId));
                if (!volume)
                {
                    return Status::Failed(L"OpenSession: session not found (it may have exited)");
                }

                std::uint32_t processId = 0;
                {
                    std::vector<detail::Session> found;
                    if (detail::EnumSessions(std::wstring(deviceId), found))
                    {
                        for (const auto& session : found)
                        {
                            if (session.id == instanceId) { processId = session.pid; break; }
                        }
                    }
                }

                handle = std::make_unique<AudioSessionHandle>(std::wstring(deviceId), std::wstring(instanceId),
                                                              processId, std::move(volume));
                return Status::Ok();
            }
        };
    }

    std::unique_ptr<IAudioDeviceManager> CreateAudioDeviceManager()
    {
        return std::make_unique<WasapiAudioDeviceManager>();
    }
}
