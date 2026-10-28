#include "RecordingBackend.h"

#include "AudioInterop.h"
#include "AudioMirror.h"
#include "RecordingDriverService.h"
#include "RecordingGuard.h"
#include "AudioRoutingCleanup.h"
#include <array>

namespace vmex::recording
{
    namespace
    {
        class Backend final : public IBackend
        {
        public:
            ~Backend() override { StopMirror(); m_guard.Abandon(); }

            Status SaveDefaults(std::wstring& monitorDevice) override
            {
                const auto ready = QueryRecordingReadiness();
                if (!ready) return ready;
                auto enumerator = audio::detail::MakeEnumerator();
                if (!enumerator) return Status::Failed(L"audio-enumerator");
                for (int i = 0; i < 2; ++i)
                {
                    audio::detail::Ptr<IMMDevice> device;
                    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(i), device.put());
                    if (FAILED(result)) return Status::FromHResult(L"recording-default", result);
                    LPWSTR id = nullptr;
                    if (FAILED(device->GetId(&id))) return Status::Failed(L"recording-default-id");
                    m_defaults[i] = audio::detail::TakeDeviceId(id);
                    audio::detail::Endpoint endpoint;
                    if (!audio::detail::ReadEndpoint(device.get(), eRender, endpoint) || audio::detail::IsOurVirtualDevice(endpoint))
                        return Status::Failed(text::Embedded().Resolve(L"recording.monitor.physicalRequired"));
                }
                monitorDevice = m_defaults[eMultimedia];
                return Status::Ok();
            }

            Status ConnectVirtualDevice() override
            {
                const auto standby = DisableCableEndpoints();
                if (!standby) return standby;
                std::wstring friendly;
                if (!audio::detail::FindVirtualEndpoint(m_virtual, friendly, true))
                    return Status::NotSupported(text::Embedded().Resolve(QueryDriver().installed ?
                        L"recording.driver.notReady" : L"recording.driver.missing"));
                const auto guarded = m_guard.Start(m_defaults, m_virtual);
                if (!guarded) return guarded;
                const auto enabled = SetRecordingEndpointEnabled(m_virtual, true);
                return enabled ? RestoreDefaultDevices(true) : enabled;
            }

            Status StartMirror(std::wstring_view monitorDevice) override
            {
                return m_mirror->Start(m_virtual, monitorDevice);
            }

            Status SelectVirtualDevice() override
            {
                for (const auto role : { eConsole, eMultimedia })
                {
                    const auto result = audio::detail::SetSystemDefaultEndpointForRoleResult(m_virtual, role);
                    if (FAILED(result)) return Status::FromHResult(L"recording-default-switch", result);
                }
                return Status::Ok();
            }

            Status RestoreDefaults() override
            {
                const auto result = RestoreDefaultDevices();
                const auto cleaned = ClearVirtualRedirects(m_virtual);
                if (!result && !cleaned) return Status::Failed(result.detail + L"; " + cleaned.detail);
                return result ? cleaned : result;
            }

            void StopMirror() noexcept override { m_mirror->Stop(); }
            Status MirrorState() const override
            {
                return m_guard.Running() ? m_mirror->State() : Status::Failed(L"recording-guard-stopped");
            }

            Status DisconnectVirtualDevice() override
            {
                const auto disabled = SetRecordingEndpointEnabled(m_virtual, false);
                if (!disabled) return disabled;
                m_guard.Stop();
                m_virtual.clear();
                return Status::Ok();
            }

            Status ChangeMonitor(std::wstring_view monitorDevice) override
            {
                auto enumerator = audio::detail::MakeEnumerator();
                auto device = audio::detail::FindDevice(enumerator.get(), std::wstring(monitorDevice));
                audio::detail::Endpoint endpoint;
                if (!device || !audio::detail::ReadEndpoint(device.get(), eRender, endpoint) || audio::detail::IsOurVirtualDevice(endpoint))
                    return Status::InvalidArguments(L"recording-monitor-endpoint");
                auto candidate = std::make_unique<AudioMirror>();
                const auto result = candidate->Start(m_virtual, monitorDevice);
                if (!result) return result;
                m_mirror.swap(candidate);
                return Status::Ok();
            }

        private:
            Status RestoreDefaultDevices(bool onlyIfVirtual = false)
            {
                Status result;
                auto enumerator = audio::detail::MakeEnumerator();
                if (!enumerator) return Status::Failed(L"audio-enumerator");
                for (int i = 0; i < 2; ++i)
                {
                    if (m_defaults[i].empty()) continue;
                    if (onlyIfVirtual)
                    {
                        audio::detail::Ptr<IMMDevice> current;
                        auto queried = enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(i), current.put());
                        LPWSTR id = nullptr;
                        if (SUCCEEDED(queried)) queried = current->GetId(&id);
                        const auto currentId = audio::detail::TakeDeviceId(id);
                        if (FAILED(queried)) return Status::FromHResult(L"recording-default-query", queried);
                        if (currentId != m_virtual) continue;
                    }
                    const auto restored = audio::detail::SetSystemDefaultEndpointForRoleResult(m_defaults[i], static_cast<ERole>(i));
                    if (FAILED(restored)) result = Status::FromHResult(L"recording-default-restore", restored);
                }
                return result;
            }

            Guard m_guard;
            std::wstring m_virtual;
            std::array<std::wstring, 2> m_defaults;
            std::unique_ptr<AudioMirror> m_mirror = std::make_unique<AudioMirror>();
        };
    }

    std::unique_ptr<IBackend> CreateWindowsBackend() { return std::make_unique<Backend>(); }
}
