#include "RecordingBackend.h"

#include "AudioInterop.h"
#include "AudioMirror.h"
#include "RecordingDriverService.h"
#include "RecordingGuard.h"
#include "AudioRoutingCleanup.h"
#include "RecordingEndpointLock.h"
#include <array>

namespace vmex::recording
{
    namespace
    {
        class Backend final : public IBackend
        {
        public:
            ~Backend() override { m_endpointLock.Stop(); StopMirror(); m_guard.Abandon(); }

            Status SaveDefaults(std::wstring& monitorDevice) override
            {
                const auto ready = QueryRecordingReadiness();
                if (!ready) return ready;
                auto enumerator = audio::detail::MakeEnumerator();
                if (!enumerator) return Status::Failed(L"audio-enumerator");
                std::array<std::wstring, 2> defaults;
                for (int i = 0; i < 2; ++i)
                {
                    audio::detail::Ptr<IMMDevice> device;
                    const auto result = enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(i), device.put());
                    if (FAILED(result)) return Status::FromHResult(L"recording-default", result);
                    LPWSTR id = nullptr;
                    if (FAILED(device->GetId(&id))) return Status::Failed(L"recording-default-id");
                    defaults[i] = audio::detail::TakeDeviceId(id);
                    audio::detail::Endpoint endpoint;
                    if (!audio::detail::ReadEndpoint(device.get(), eRender, endpoint) || audio::detail::IsOurVirtualDevice(endpoint))
                        return Status::Failed(text::Embedded().Resolve(L"recording.monitor.physicalRequired"));
                }
                m_outputs.Remember(std::move(defaults));
                monitorDevice = m_outputs.Monitor();
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
                const auto guarded = m_guard.Start(m_outputs.Initial(), m_virtual);
                if (!guarded) return guarded;
                const auto enabled = SetRecordingEndpointEnabled(m_virtual, true);
                return enabled ? RestoreDefaultDevices(true) : enabled;
            }

            Status StartMirror(std::wstring_view monitorDevice) override
            {
                if (!audio::detail::SetEndpointVolume(m_virtual, 1.0f))
                    return Status::Failed(L"recording-volume-lock");
                return m_mirror->Start(m_virtual, monitorDevice);
            }

            Status SelectVirtualDevice() override
            {
                for (const auto role : { eConsole, eMultimedia })
                {
                    const auto result = audio::detail::SetSystemDefaultEndpointForRoleResult(m_virtual, role);
                    if (FAILED(result)) return Status::FromHResult(L"recording-default-switch", result);
                }
                const auto locked = m_endpointLock.Start(m_virtual);
                if (!locked) return locked;
                const auto saved = m_guard.UpdateDefaults({m_outputs.Monitor(), m_outputs.Monitor()});
                if (saved) m_outputs.SelectMonitor(m_outputs.Monitor());
                return saved;
            }

            Status RestoreDefaults() override
            {
                m_endpointLock.Stop();
                const auto result = RestoreDefaultDevices();
                const auto cleaned = ClearVirtualRedirects(m_virtual);
                if (!result && !cleaned) return Status::Failed(result.detail + L"; " + cleaned.detail);
                return result ? cleaned : result;
            }

            void StopMirror() noexcept override { m_mirror->Stop(); }
            Status MirrorState() const override
            {
                const auto locked = m_endpointLock.State();
                if (!locked) return locked;
                return m_guard.Running() ? m_mirror->State() : Status::Failed(L"recording-guard-stopped");
            }

            Status DisconnectVirtualDevice() override
            {
                m_endpointLock.Stop();
                const auto disabled = SetRecordingEndpointEnabled(m_virtual, false);
                if (!disabled) return disabled;
                m_guard.Stop();
                m_virtual.clear();
                m_outputs = {};
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
                std::wstring monitor(monitorDevice);
                const auto saved = m_guard.UpdateDefaults({monitor, monitor});
                if (!saved) return saved;
                m_mirror.swap(candidate);
                m_outputs.SelectMonitor(std::move(monitor));
                return Status::Ok();
            }

        private:
            Status RestoreDefaultDevices(bool onlyIfVirtual = false)
            {
                Status result;
                auto enumerator = audio::detail::MakeEnumerator();
                if (!enumerator) return Status::Failed(L"audio-enumerator");
                const auto targets = m_outputs.Targets();
                for (int i = 0; i < 2; ++i)
                {
                    if (targets[i].empty()) continue;
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
                    const auto restored = audio::detail::SetSystemDefaultEndpointForRoleResult(targets[i], static_cast<ERole>(i));
                    if (FAILED(restored)) result = Status::FromHResult(L"recording-default-restore", restored);
                }
                return result;
            }

            Guard m_guard;
            RecordingEndpointLock m_endpointLock;
            std::wstring m_virtual;
            DefaultOutputMemory m_outputs;
            std::unique_ptr<AudioMirror> m_mirror = std::make_unique<AudioMirror>();
        };
    }

    std::unique_ptr<IBackend> CreateWindowsBackend() { return std::make_unique<Backend>(); }
}
