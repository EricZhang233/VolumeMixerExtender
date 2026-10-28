#include "RecordingBackend.h"

#include "AudioInterop.h"
#include "AudioMirror.h"
#include <winioctl.h>
#include "RecordingDriverContract.h"
#include "RecordingDriverService.h"
#include <array>

namespace vmex::recording
{
    namespace
    {
        class Backend final : public IBackend
        {
        public:
            ~Backend() override { StopMirror(); DisconnectVirtualDevice(); }

            Status SaveDefaults(std::wstring& monitorDevice) override
            {
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
                }
                monitorDevice = m_defaults[eMultimedia];
                return Status::Ok();
            }

            Status ConnectVirtualDevice() override
            {
                m_lease = ::CreateFileW(L"\\\\.\\VmexRecording", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (m_lease == INVALID_HANDLE_VALUE)
                {
                    const auto error = ::GetLastError();
                    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
                        Status::NotSupported(text::Embedded().Resolve(QueryDriver().installed ? L"recording.driver.notReady" :
                            L"recording.driver.missing")) : Status::FromWin32(L"recording-driver-open", error);
                }
                VmexRecordingRequest request{ VMEX_RECORDING_VERSION, 0 };
                DWORD returned = 0;
                if (!::DeviceIoControl(m_lease, VMEX_RECORDING_QUERY, nullptr, 0, &request, sizeof(request), &returned, nullptr))
                    return Status::FromLastError(L"recording-driver-query");
                if (returned != sizeof(request) || request.version != VMEX_RECORDING_VERSION)
                    return Status::NotSupported(L"recording-driver-version");
                request.connected = 1;
                if (!::DeviceIoControl(m_lease, VMEX_RECORDING_CONNECT, &request, sizeof(request), nullptr, 0, &returned, nullptr))
                    return Status::FromLastError(L"recording-driver-connect");
                for (int i = 0; i < 100; ++i)
                {
                    std::wstring friendly;
                    if (audio::detail::FindVirtualEndpoint(m_virtual, friendly)) return Status::Ok();
                    ::Sleep(30);
                }
                return Status::Failed(L"recording-endpoint-timeout");
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
                Status result;
                for (int i = 0; i < 2; ++i)
                {
                    if (m_defaults[i].empty()) continue;
                    const auto restored = audio::detail::SetSystemDefaultEndpointForRoleResult(m_defaults[i], static_cast<ERole>(i));
                    if (FAILED(restored)) result = Status::FromHResult(L"recording-default-restore", restored);
                }
                return result;
            }

            void StopMirror() noexcept override { m_mirror->Stop(); }
            Status MirrorState() const override { return m_mirror->State(); }

            void DisconnectVirtualDevice() noexcept override
            {
                if (m_lease != INVALID_HANDLE_VALUE)
                {
                    ::CloseHandle(m_lease);
                    m_lease = INVALID_HANDLE_VALUE;
                }
                m_virtual.clear();
            }

            Status ChangeMonitor(std::wstring_view monitorDevice) override
            {
                auto candidate = std::make_unique<AudioMirror>();
                const auto result = candidate->Start(m_virtual, monitorDevice);
                if (!result) return result;
                m_mirror.swap(candidate);
                return Status::Ok();
            }

        private:
            HANDLE m_lease = INVALID_HANDLE_VALUE;
            std::wstring m_virtual;
            std::array<std::wstring, 2> m_defaults;
            std::unique_ptr<AudioMirror> m_mirror = std::make_unique<AudioMirror>();
        };
    }

    std::unique_ptr<IBackend> CreateWindowsBackend() { return std::make_unique<Backend>(); }
}
