#include "AudioDeviceManager.h"

#include "AudioInterop.h"
#include "Logger.h"

#include <algorithm>
#include <functional>
#include <mutex>

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
            info.recommendedIcon = detail::GetRecommendedDeviceIcon(endpoint);
            return info;
        }

        SessionState ToSessionState(AudioSessionState state)
        {
            switch (state)
            {
            case AudioSessionStateActive: return SessionState::Active;
            case AudioSessionStateExpired: return SessionState::Expired;
            default: return SessionState::Inactive;
            }
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
            info.state = ToSessionState(session.state);
            return info;
        }

        AppSessionInfo ToAppSessionInfo(const detail::Session& session, const detail::Endpoint& endpoint)
        {
            AppSessionInfo info;
            info.processId = session.pid;
            info.appKey = session.key;
            info.displayName = session.name;
            info.instanceId = session.id;
            info.deviceId = endpoint.id;
            info.deviceName = endpoint.friendly;
            info.volume = session.volume;
            info.muted = session.mute;
            info.active = (session.state == AudioSessionStateActive);
            info.systemSounds = session.systemSounds;
            return info;
        }

        class AudioSessionHandle final : public IAudioSessionHandle
        {
        public:
            AudioSessionHandle(std::wstring deviceId, std::wstring instanceId, std::uint32_t processId,
                               bool systemSounds, detail::Ptr<ISimpleAudioVolume> volume,
                               detail::Ptr<IAudioMeterInformation> meter, SessionFollowMode followMode)
                : m_deviceId(std::move(deviceId))
                , m_instanceId(std::move(instanceId))
                , m_processId(processId)
                , m_systemSounds(systemSounds)
                , m_volume(std::move(volume))
                , m_meter(std::move(meter))
                , m_followMode(followMode)
            {
            }

            [[nodiscard]] std::wstring_view InstanceId() const noexcept override { return m_instanceId; }
            [[nodiscard]] std::wstring_view DeviceId() const noexcept override { return m_deviceId; }
            [[nodiscard]] std::uint32_t ProcessId() const noexcept override { return m_processId; }

            Status Rebind(std::wstring_view deviceId, std::wstring_view instanceId) override
            {
                if (m_followMode == SessionFollowMode::Endpoint && deviceId != m_deviceId)
                    return Status::InvalidArguments(L"session-endpoint-locked");
                if (deviceId.empty() || instanceId.empty())
                {
                    return Status::InvalidArguments(L"Rebind: deviceId and instanceId are both required");
                }

                if (m_deviceId == deviceId && m_instanceId == instanceId) return Status::Ok();

                m_deviceId.assign(deviceId);
                m_instanceId.assign(instanceId);
                m_volume.reset();
                m_meter.reset();
                return Status::Ok();
            }

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

            Status GetPeak(float& left, float& right) override
            {
                left = 0.0f;
                right = 0.0f;
                if (!m_meter)
                {
                    m_meter = detail::ResolveSessionMeter(m_deviceId, m_instanceId);
                }
                if (!m_meter)
                {
                    return Status::Failed(L"session peak meter unavailable");
                }
                UINT channels = 0;
                if (FAILED(m_meter->GetMeteringChannelCount(&channels)) || channels == 0)
                    return Status::Failed(L"session peak channels unavailable");
                std::vector<float> values(channels);
                if (FAILED(m_meter->GetChannelsPeakValues(channels, values.data())))
                    return Status::Failed(L"session peak meter unavailable");
                left = values[0];
                right = channels > 1 ? values[1] : values[0];
                return Status::Ok();
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
                detail::SessionTarget target;
                if (!detail::ResolveAppSessionTarget(m_instanceId, m_processId, m_systemSounds, m_deviceId, target,
                    m_followMode == SessionFollowMode::Endpoint))
                {
                    return false;
                }

                m_deviceId = std::move(target.deviceId);
                m_instanceId = std::move(target.instanceId);
                m_volume = std::move(target.volume);
                m_meter = std::move(target.meter);
                return static_cast<bool>(m_volume);
            }

            std::wstring m_deviceId;
            std::wstring m_instanceId;
            std::uint32_t m_processId = 0;
            bool m_systemSounds = false;
            detail::Ptr<ISimpleAudioVolume> m_volume;
            detail::Ptr<IAudioMeterInformation> m_meter;
            SessionFollowMode m_followMode;
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
                    log::Logger::Instance().Write(log::Level::Debug, kChannel, L"Only active endpoints are enumerated; requested state ignored");
                }

                std::vector<detail::Endpoint> endpoints;
                if (!detail::EnumEndpoints(ToFlow(flow), endpoints))
                {
                    return Status::Failed(L"EnumAudioEndpoints failed (no device enumerator?)");
                }

                devices.reserve(endpoints.size());
                for (const auto& endpoint : endpoints) devices.push_back(ToDeviceInfo(endpoint));

                log::Logger::Instance().WriteFormat(log::Level::Debug, kChannel, L"Endpoints enumerated flow={0} count={1}",
                    { std::wstring(ToString(flow)), std::to_wstring(devices.size()) });
                return Status::Ok();
            }

            Status EnumerateRoutingDevices(bool recordingMode, std::vector<DeviceInfo>& devices) override
            {
                devices.clear();
                std::vector<detail::Endpoint> endpoints;
                if (!detail::EnumEndpoints(eRender, endpoints, recordingMode))
                    return Status::Failed(L"routing-devices");
                std::wstring virtualId, friendly;
                if (recordingMode) detail::FindVirtualEndpoint(virtualId, friendly);
                for (const auto& endpoint : endpoints)
                {
                    if (detail::IsOurVirtualDevice(endpoint) && endpoint.id != virtualId) continue;
                    devices.push_back(ToDeviceInfo(endpoint));
                }
                return Status::Ok();
            }

            Status FindVirtualDevice(DeviceInfo& device) override
            {
                device = {};

                std::wstring id;
                std::wstring friendly;
                if (!detail::FindVirtualEndpoint(id, friendly))
                {
                    return Status::Failed(L"virtual-endpoint-not-found");
                }

                device.id = id;
                device.friendlyName = friendly.empty() ? id : friendly;
                device.state = DeviceState::Active;
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
                if (detail::EnumEndpoints(ToFlow(flow), endpoints, true))
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

            Status GetPeak(std::wstring_view deviceId, float& left, float& right) override
            {
                auto entry = m_endpointMeters.find(deviceId);
                if (entry == m_endpointMeters.end())
                    entry = m_endpointMeters.try_emplace(std::wstring(deviceId)).first;
                auto& state = entry->second;
                const auto now = ::GetTickCount64();
                if (!state.meter && now >= state.nextOpen)
                {
                    state.nextOpen = now + 200;
                    state.meter = detail::OpenEndpointMeter(entry->first);
                }
                if (detail::ReadMeterPeaks(state.meter.get(), left, right)) return Status::Ok();
                if (state.meter)
                {
                    state.meter.reset();
                    state.nextOpen = now + 200;
                }
                return Status::Failed(L"IAudioMeterInformation::GetPeakValue failed");
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

                log::Logger::Instance().WriteFormat(log::Level::Debug, kChannel, L"Audio sessions enumerated device={0} count={1}",
                    { std::wstring(deviceId), std::to_wstring(sessions.size()) });

                return Status::Ok();
            }

            Status EnumerateAppSessions(const std::map<std::wstring, std::wstring>& preferredDevices,
                                        std::vector<AppSessionInfo>& sessions, std::wstring_view deviceId,
                                        std::vector<std::shared_ptr<IAudioSessionHandle>>* handles,
                                        SessionFollowMode followMode) override
            {
                sessions.clear();
                if (handles) handles->clear();

                std::vector<detail::Endpoint> endpoints;
                if (!detail::EnumEndpoints(eRender, endpoints, true))
                {
                    return Status::Failed(L"EnumAudioEndpoints failed (no device enumerator?)");
                }

                std::wstring defaultId;
                detail::GetDefaultEndpointId(eRender, defaultId);

                struct Candidate final
                {
                    AppSessionInfo info;
                    AudioSessionState state;
                    detail::Ptr<ISimpleAudioVolume> volume;
                    detail::Ptr<IAudioMeterInformation> meter;
                };
                std::vector<Candidate> candidates;
                for (const auto& endpoint : endpoints)
                {
                    if (!deviceId.empty() && endpoint.id != deviceId) continue;
                    std::vector<detail::Session> found;
                    if (!detail::EnumSessionsOnce(endpoint.id, found, handles != nullptr)) continue;

                    for (auto& session : found)
                    {
                        if (session.state == AudioSessionStateExpired) continue;
                        candidates.push_back({ ToAppSessionInfo(session, endpoint), session.state,
                                               std::move(session.vol), std::move(session.meter) });
                    }
                }

                std::vector<std::size_t> chosen;
                for (std::size_t index = 0; index < candidates.size(); ++index)
                {
                    std::size_t slot = chosen.size();
                    for (std::size_t probe = 0; probe < chosen.size(); ++probe)
                    {
                        if (candidates[chosen[probe]].info.appKey == candidates[index].info.appKey)
                        {
                            slot = probe;
                            break;
                        }
                    }

                    if (slot == chosen.size())
                    {
                        chosen.push_back(index);
                        continue;
                    }

                    const auto& candidate = candidates[index];
                    const auto& selected = candidates[chosen[slot]];
                    if (Score(candidate.info, candidate.state, preferredDevices, defaultId) >
                        Score(selected.info, selected.state, preferredDevices, defaultId))
                    {
                        chosen[slot] = index;
                    }
                }

                sessions.reserve(chosen.size());
                if (handles) handles->reserve(chosen.size());
                for (const std::size_t index : chosen)
                {
                    auto& candidate = candidates[index];
                    auto& info = candidate.info;
                    if (handles)
                    {
                        std::shared_ptr<IAudioSessionHandle> handle;
                        if (candidate.volume)
                            handle = std::make_shared<AudioSessionHandle>(info.deviceId, info.instanceId,
                                info.processId, info.systemSounds, std::move(candidate.volume),
                                std::move(candidate.meter), followMode);
                        handles->push_back(std::move(handle));
                    }
                    sessions.push_back(std::move(info));
                }

                log::Logger::Instance().WriteFormat(log::Level::Debug, kChannel, L"Application sessions enumerated apps={0} endpoints={1}",
                    { std::to_wstring(sessions.size()), std::to_wstring(endpoints.size()) });

                return Status::Ok();
            }

        private:
            struct EndpointMeterState final
            {
                detail::Ptr<IAudioMeterInformation> meter;
                ULONGLONG nextOpen = 0;
            };

            std::map<std::wstring, EndpointMeterState, std::less<>> m_endpointMeters;

            static int Score(const AppSessionInfo& info, AudioSessionState state,
                             const std::map<std::wstring, std::wstring>& preferredDevices,
                             const std::wstring& defaultId)
            {
                const auto found = preferredDevices.find(info.appKey);
                const bool onPreferred = found != preferredDevices.end() && found->second == info.deviceId;

                return detail::AppSessionScore(state, onPreferred, info.deviceId == defaultId, false);
            }
        };

        class DeviceWatcher final : public IMMNotificationClient, public IAudioDeviceWatcher
        {
        public:
            ~DeviceWatcher() override { Stop(); }

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
            {
                if (object == nullptr) return E_POINTER;
                *object = nullptr;

                if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient))
                {
                    *object = static_cast<IMMNotificationClient*>(this);
                    AddRef();
                    return S_OK;
                }

                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return static_cast<ULONG>(::InterlockedIncrement(&m_references));
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                const long remaining = ::InterlockedDecrement(&m_references);
                if (remaining == 0) delete this;
                return static_cast<ULONG>(remaining);
            }

            HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD) override
            {
                Notify(DeviceEventKind::ListChanged, deviceId);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override
            {
                Notify(DeviceEventKind::ListChanged, deviceId);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override
            {
                Notify(DeviceEventKind::ListChanged, deviceId);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole, LPCWSTR deviceId) override
            {
                if (flow == eRender || flow == eCapture) Notify(DeviceEventKind::DefaultChanged, deviceId);
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY) override
            {
                Notify(DeviceEventKind::PropertyChanged, deviceId);
                return S_OK;
            }

            Status Start(std::function<void(const DeviceEvent&)> handler) override
            {
                Stop();
                if (!handler) return Status::InvalidArguments(L"Start: handler is required");

                auto enumerator = detail::MakeEnumerator();
                if (!enumerator)
                {
                    log::Logger::Instance().Write(log::Level::Warn, kChannel, L"Endpoint notification registration failed");
                    return Status::Failed(L"Start: no device enumerator");
                }

                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    m_handler = std::move(handler);
                }

                if (FAILED(enumerator->RegisterEndpointNotificationCallback(this)))
                {
                    {
                        std::lock_guard<std::mutex> guard(m_mutex);
                        m_handler = nullptr;
                    }
                    log::Logger::Instance().Write(log::Level::Warn, kChannel, L"Endpoint notification registration failed");
                    return Status::Failed(L"RegisterEndpointNotificationCallback failed");
                }

                m_enumerator = std::move(enumerator);
                log::Logger::Instance().Write(log::Level::Debug, kChannel, L"Endpoint notifications started");
                return Status::Ok();
            }

            void Stop() override
            {
                if (m_enumerator)
                {
                    m_enumerator->UnregisterEndpointNotificationCallback(this);
                    m_enumerator.reset();
                }

                std::lock_guard<std::mutex> guard(m_mutex);
                m_handler = nullptr;
            }

        private:
            void Notify(DeviceEventKind kind, LPCWSTR deviceId)
            {
                std::function<void(const DeviceEvent&)> handler;
                {
                    std::lock_guard<std::mutex> guard(m_mutex);
                    handler = m_handler;
                }
                if (!handler) return;

                DeviceEvent event;
                event.kind = kind;
                if (deviceId != nullptr) event.deviceId = deviceId;

                try { handler(event); } catch (...) {}
            }

            std::mutex m_mutex;
            std::function<void(const DeviceEvent&)> m_handler;
            detail::Ptr<IMMDeviceEnumerator> m_enumerator;
            long m_references = 1;
        };
    }

    std::unique_ptr<IAudioDeviceManager> CreateAudioDeviceManager()
    {
        return std::make_unique<WasapiAudioDeviceManager>();
    }

    std::unique_ptr<IAudioDeviceWatcher> CreateAudioDeviceWatcher()
    {
        return std::unique_ptr<IAudioDeviceWatcher>(new DeviceWatcher());
    }
}
