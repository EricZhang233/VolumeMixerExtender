#include "RecordingEndpointLock.h"

#include "AudioDeviceManager.h"
#include "AudioInterop.h"
#include "Logger.h"
#include <future>
#include <algorithm>
#include <array>

namespace vmex::recording
{
    Status ProtectPhysicalDefaults(IOutputDefaultActions& actions, std::span<const std::wstring> virtualEndpoints,
        std::span<const std::wstring> physicalEndpoints)
    {
        if (virtualEndpoints.empty()) return Status::Ok();
        std::array<std::wstring, 3> defaults;
        std::wstring fallback;
        Status result;
        for (unsigned role = 0; role < defaults.size(); ++role)
        {
            const auto read = actions.ReadDefault(role, defaults[role]);
            if (!read) { result = read; continue; }
            if (fallback.empty() && std::find(physicalEndpoints.begin(), physicalEndpoints.end(), defaults[role]) != physicalEndpoints.end() &&
                std::find(virtualEndpoints.begin(), virtualEndpoints.end(), defaults[role]) == virtualEndpoints.end())
                fallback = defaults[role];
        }
        if (fallback.empty())
            for (const auto& endpoint : physicalEndpoints)
                if (!endpoint.empty() && std::find(virtualEndpoints.begin(), virtualEndpoints.end(), endpoint) == virtualEndpoints.end())
                { fallback = endpoint; break; }
        for (unsigned role = 0; role < defaults.size(); ++role)
        {
            if (std::find(virtualEndpoints.begin(), virtualEndpoints.end(), defaults[role]) == virtualEndpoints.end()) continue;
            if (fallback.empty()) return Status::Failed(L"driver-pending-physical-output-missing");
            const auto changed = actions.SetDefault(role, fallback);
            if (!changed) result = changed;
            else log::Logger::Instance().WriteFormat(log::Level::Info, L"recording.driver",
                L"Physical default output restored role={0} endpoint={1}", { std::to_wstring(role), fallback });
        }
        return result;
    }

    Status EnforceRecordingEndpoint(IEndpointLockActions& actions, std::wstring_view endpoint)
    {
        if (endpoint.empty()) return Status::InvalidArguments(L"recording-lock-endpoint");
        float level = 0;
        auto result = actions.ReadVolume(level);
        if (!result) return result;
        if (level != 1.0f)
        {
            result = actions.SetVolume(1.0f);
            if (!result) return result;
            log::Logger::Instance().Write(log::Level::Info, L"recording", L"Virtual endpoint volume restored to 100%");
        }
        for (unsigned role = 0; role < 2; ++role)
        {
            std::wstring current;
            const auto queried = actions.ReadDefault(role, current);
            if (queried && current == endpoint) continue;
            result = actions.SetDefault(role, endpoint);
            if (!result) return result;
            log::Logger::Instance().Write(log::Level::Info, L"recording", L"Default output restored to virtual endpoint");
        }
        return Status::Ok();
    }

    struct RecordingEndpointLock::Signal final
    {
        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        ~Signal() { if (event) ::CloseHandle(event); }
    };

    namespace
    {
        Status Enforce(const std::wstring& endpoint, IAudioEndpointVolume* volume)
        {
            struct Actions final : IEndpointLockActions
            {
                IAudioEndpointVolume* volume;
                explicit Actions(IAudioEndpointVolume* value) : volume(value) {}
                Status ReadVolume(float& level) override
                {
                    const auto result = volume->GetMasterVolumeLevelScalar(&level);
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"recording-volume-query", result);
                }
                Status SetVolume(float level) override
                {
                    const auto result = volume->SetMasterVolumeLevelScalar(level, nullptr);
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"recording-volume-lock", result);
                }
                Status ReadDefault(unsigned role, std::wstring& endpoint) override
                {
                    auto enumerator = audio::detail::MakeEnumerator();
                    if (!enumerator) return Status::Failed(L"recording-default-enumerator");
                    audio::detail::Ptr<IMMDevice> current;
                    auto result = enumerator->GetDefaultAudioEndpoint(eRender, static_cast<ERole>(role), current.put());
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(result) && current) result = current->GetId(&id);
                    endpoint = audio::detail::TakeDeviceId(id);
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"recording-default-query", result);
                }
                Status SetDefault(unsigned role, std::wstring_view endpoint) override
                {
                    const auto result = audio::detail::SetSystemDefaultEndpointForRoleResult(std::wstring(endpoint), static_cast<ERole>(role));
                    return SUCCEEDED(result) ? Status::Ok() : Status::FromHResult(L"recording-default-lock", result);
                }
            } actions(volume);
            return EnforceRecordingEndpoint(actions, endpoint);
        }
    }

    RecordingEndpointLock::~RecordingEndpointLock() { Stop(); }

    Status RecordingEndpointLock::Start(std::wstring_view endpoint)
    {
        Stop();
        m_signal = std::make_shared<Signal>();
        if (!m_signal->event) return Status::FromLastError(L"recording-lock-event");
        m_stop = false;
        std::promise<Status> ready;
        auto future = ready.get_future();
        m_thread = std::thread([this, signal = m_signal, endpoint = std::wstring(endpoint), ready = std::move(ready)]() mutable
        {
            struct Apartment final
            {
                HRESULT result = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                ~Apartment() { if (SUCCEEDED(result)) ::CoUninitialize(); }
            } apartment;
            bool announced = false;
            const auto report = [&](Status result)
            {
                { std::lock_guard lock(m_mutex); m_result = result; }
                if (!announced) { ready.set_value(result); announced = true; }
            };
            try
            {
                if (FAILED(apartment.result)) { report(Status::FromHResult(L"recording-lock-com", apartment.result)); return; }
                audio::detail::Ptr<IAudioEndpointVolume> volume;
                if (!audio::detail::EndpointVolume(endpoint, volume)) { report(Status::Failed(L"recording-lock-volume")); return; }
                auto monitor = audio::detail::EndpointVolumeMonitor::Create(endpoint, [signal](float level, bool)
                    { if (level != 1.0f) ::SetEvent(signal->event); });
                if (!monitor) { report(Status::Failed(L"recording-lock-notification")); return; }
                auto watcher = audio::CreateAudioDeviceWatcher();
                const auto watched = watcher->Start([signal](const audio::DeviceEvent& event)
                    { if (event.kind != audio::DeviceEventKind::PropertyChanged) ::SetEvent(signal->event); });
                if (!watched) { report(watched); return; }
                auto result = Enforce(endpoint, volume.get());
                report(result);
                while (result && !m_stop.load(std::memory_order_acquire))
                {
                    const auto wait = ::WaitForSingleObject(signal->event, 1000);
                    if (m_stop.load(std::memory_order_acquire)) break;
                    result = wait == WAIT_FAILED ? Status::FromLastError(L"recording-lock-wait") : Enforce(endpoint, volume.get());
                    if (!result) report(result);
                }
            }
            catch (...) { report(Status::Failed(L"recording-lock-exception")); }
        });
        const auto result = future.get();
        if (!result) Stop();
        return result;
    }

    void RecordingEndpointLock::Stop() noexcept
    {
        m_stop.store(true, std::memory_order_release);
        if (m_signal && m_signal->event) ::SetEvent(m_signal->event);
        if (m_thread.joinable()) m_thread.join();
        m_signal.reset();
    }

    Status RecordingEndpointLock::State() const { std::lock_guard lock(m_mutex); return m_result; }
}
