#include "App.h"

#include "AutostartEntry.h"
#include "HostPresence.h"
#include "InjectionContract.h"
#include "InjectionMonitor.h"
#include "PayloadDeployment.h"
#include "PayloadResources.h"
#include "Platform.h"
#include "Strings.h"
#include "TapCommand.h"
#include "TapPipeServer.h"
#include "ManagementService.h"
#include "UserSettings.h"
#include "Version.h"
#include "RecordingBackend.h"
#include "RecordingClient.h"
#include "RecordingPanelState.h"
#include "RecordingDriverService.h"
#include "AboutService.h"
#include "UpdateService.h"

#include <windows.h>

#include <objbase.h>

#include <array>

namespace vmex
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"app";
        constexpr std::wstring_view kPipeChannel = L"pipe";

        constexpr std::uint32_t kInjectionTimeoutMs = 20000;
        constexpr unsigned long kMonitorIntervalMs = 1000;

        constexpr std::array<audio::DeviceRole, 3> kPolicyRoles{
            audio::DeviceRole::Console,
            audio::DeviceRole::Multimedia,
            audio::DeviceRole::Communications,
        };

        HANDLE ShutdownEvent()
        {
            static const HANDLE event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            return event;
        }

        BOOL WINAPI HandleConsoleSignal(DWORD type)
        {
            if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
                type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT)
            {
                if (const HANDLE event = ShutdownEvent())
                {
                    ::SetEvent(event);
                }
                return TRUE;
            }
            return FALSE;
        }

    }

    struct App::Impl final : inject::ITapCommandHandler
    {
        AppOptions options;
        text::TextService text;
        config::ConfigService config;
        std::unique_ptr<audio::IAudioDeviceManager> audioDevices;
        std::unique_ptr<audio::IEndpointPolicyService> endpointPolicy;
        std::unique_ptr<inject::IInjectionService> injection;
        std::unique_ptr<inject::TapPipeServer> tapPipe;
        std::unique_ptr<inject::InjectionMonitor> monitor;
        inject::Deployment deployment;
        host::HostPresence presence;
        std::unique_ptr<recording::IBackend> recordingBackend;
        std::unique_ptr<recording::Mode> recordingMode;
        std::jthread recordingWatchdog;
        std::recursive_mutex recordingMutex;
        recording::PanelStateCache recordingPanel;
        bool recordingAvailable = false;
        update::Service updater;
        bool updateShutdown = false;
        bool comInitialized = false;
        bool ready = false;

        Status PrepareDeployment()
        {
            return inject::DeployPayloads(deployment);
        }

        void PublishRecordingPanel(bool readiness)
        {
            const auto state = recordingMode->State();
            if (readiness)
                recordingAvailable = recording::QueryRecordingReadiness(state.enabled || state.cleanupPending ?
                    std::wstring_view(state.monitorDevice) : std::wstring_view()).IsOk() || state.enabled || state.cleanupPending;
            recordingPanel.Publish({ state, recordingAvailable });
        }

        void HandleTapCommand(const inject::TapCommand& command) override;
        std::string HandleTapRequest(const inject::TapCommand& command) override;
        void HandleSetDefault(const inject::TapCommand& command);
        void HandleSetRedirect(const inject::TapCommand& command);
        void HandleClearRedirect();
    };

    App::App()
        : m_impl(std::make_unique<Impl>())
    {
    }

    App::~App() = default;

    App& App::Instance()
    {
        static App instance;
        return instance;
    }

    Status App::Initialize(const AppOptions& options)
    {
        m_impl->options = options;

        const auto versionStatus = platform::VerifySupportedWindowsVersion();
        if (!versionStatus.IsOk())
        {
            return Status::Failed(L"unsupported-windows:" + versionStatus.detail);
        }

        const HRESULT comStatus = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comStatus) && comStatus != RPC_E_CHANGED_MODE)
        {
            return Status::FromHResult(L"com-init", comStatus);
        }
        m_impl->comInitialized = SUCCEEDED(comStatus);

        if (!options.logFile.empty())
        {
            log::Logger::Instance().AddSink(log::CreateFileSink(options.logFile));
        }
        log::Logger::Instance().SetMinimumLevel(options.logLevel);

        const auto source = text::CreateEmbeddedTextSource();
        const auto attachStatus = m_impl->text.Attach(source);
        if (!attachStatus.IsOk())
        {
            return attachStatus;
        }
        log::Logger::Instance().WriteFormat(
            log::Level::Info,
            kChannel,
            L"UI text resources loaded count={0}",
            { std::to_wstring(m_impl->text.Count()) });

        if (!options.configFile.empty())
        {
            log::Logger::Instance().WriteFormat(
                log::Level::Info,
                kChannel,
                L"Application initialization config={0}",
                { options.configFile.wstring() });

            const auto configStatus = m_impl->config.Load(options.configFile);
            if (!configStatus.IsOk())
            {
                return configStatus;
            }
        }

        m_impl->audioDevices = audio::CreateAudioDeviceManager();
        m_impl->endpointPolicy = audio::CreateEndpointPolicyService();
        m_impl->injection = inject::CreateInjectionService();

        m_impl->ready = true;

        log::Logger::Instance().WriteFormat(
            log::Level::Info,
            kChannel,
            L"Application ready textEntries={0}",
            { std::to_wstring(m_impl->text.Count()) });

        return Status::Ok();
    }

    void App::Shutdown()
    {
        if (!m_impl->ready)
        {
            log::Logger::Instance().ClearSinks();
            return;
        }

        StopServing();

        if (m_impl->options.saveConfig && !m_impl->options.configFile.empty())
        {
            m_impl->config.Save();
        }

        log::Logger::Instance().Write(log::Level::Info, kChannel, L"Application shutdown");
        log::Logger::Instance().Flush();
        log::Logger::Instance().ClearSinks();

        if (m_impl->comInitialized)
        {
            ::CoUninitialize();
            m_impl->comInitialized = false;
        }

        m_impl->ready = false;
    }

    bool App::IsReady() const noexcept
    {
        return m_impl->ready;
    }

    const AppOptions& App::Options() const noexcept
    {
        return m_impl->options;
    }

    std::wstring App::Version() const
    {
        return strings::ToWide(VMEX_VERSION_STRING);
    }

    text::TextService& App::Text() noexcept
    {
        return m_impl->text;
    }

    config::ConfigService& App::Config() noexcept
    {
        return m_impl->config;
    }

    audio::IAudioDeviceManager& App::AudioDevices()
    {
        return *m_impl->audioDevices;
    }

    audio::IEndpointPolicyService& App::EndpointPolicy()
    {
        return *m_impl->endpointPolicy;
    }

    inject::IInjectionService& App::Injection()
    {
        return *m_impl->injection;
    }

    Status App::Serve()
    {
        if (!m_impl->ready)
        {
            return Status::Failed(L"not-ready");
        }

        if (m_impl->tapPipe != nullptr)
        {
            return Status::Ok();
        }

        const auto presence = m_impl->presence.Acquire();
        if (!presence.IsOk())
        {
            log::Logger::Instance().Write(log::Level::Warn, kChannel, L"Background host already running; startup rejected");
            return presence;
        }

        m_impl->updateShutdown = false;
        m_impl->recordingBackend = recording::CreateWindowsBackend();
        m_impl->recordingMode = std::make_unique<recording::Mode>(*m_impl->recordingBackend);
        const auto endpoints = recording::DisableCableEndpoints();
        if (!endpoints)
            log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                L"Standby endpoint configuration failed detail={0}", { endpoints.detail });
        const auto cached = m_impl->recordingPanel.Start();
        if (!cached)
            log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                L"Recording panel cache initialization failed detail={0}", { cached.detail });
        m_impl->PublishRecordingPanel(true);

        auto server = std::make_unique<inject::TapPipeServer>(
            inject::TapPipeName(platform::GetCurrentSessionId()),
            *m_impl);

        if (!server->Start())
        {
            m_impl->recordingPanel.Stop();
            m_impl->recordingMode.reset();
            m_impl->recordingBackend.reset();
            m_impl->presence.Release();
            log::Logger::Instance().Write(log::Level::Error, kChannel, L"Background host startup failed: pipe unavailable");
            return Status::Failed(L"pipe-start");
        }

        log::Logger::Instance().WriteFormat(
            log::Level::Info,
            kChannel,
            L"Background host started pipe={0}",
            { server->PipeName() });

        m_impl->tapPipe = std::move(server);

        m_impl->recordingWatchdog = std::jthread([impl = m_impl.get()](std::stop_token stop)
        {
            const auto com = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(com)) return;
            unsigned ticks = 0;
            while (!stop.stop_requested())
            {
                try
                {
                    std::lock_guard lock(impl->recordingMutex);
                    impl->recordingMode->CheckHealth();
                    const bool refresh = ++ticks % 10 == 0;
                    if (refresh)
                    {
                        const auto protectedDefaults = recording::ProtectPendingDriverDefaults();
                        if (!protectedDefaults)
                            log::Logger::Instance().WriteFormat(log::Level::Warn, kChannel,
                                L"Pending driver default output protection failed detail={0}", { protectedDefaults.detail });
                    }
                    impl->PublishRecordingPanel(refresh);
                }
                catch (...)
                {
                    log::Logger::Instance().Write(log::Level::Warn, kChannel, L"Recording state refresh failed; retaining previous snapshot");
                }
                ::Sleep(200);
            }
            ::CoUninitialize();
        });

        const auto monitoring = StartMonitoring();
        if (!monitoring.IsOk())
        {
            log::Logger::Instance().WriteFormat(
                log::Level::Warn, kChannel, L"Injection monitor startup failed detail={0}", { monitoring.detail });
        }

        m_impl->updater.Start();
        return Status::Ok();
    }

    void App::StopServing()
    {
        if (m_impl->tapPipe == nullptr)
        {
            return;
        }

        m_impl->updater.Stop();
        StopMonitoring();
        m_impl->tapPipe.reset();
        m_impl->recordingWatchdog.request_stop();
        if (m_impl->recordingWatchdog.joinable()) m_impl->recordingWatchdog.join();
        StopRecording();
        m_impl->recordingPanel.Stop();
        m_impl->recordingMode.reset();
        m_impl->recordingBackend.reset();
        m_impl->presence.Release();
        log::Logger::Instance().Write(log::Level::Info, kChannel, L"Background host stopped");
    }

    bool App::IsServing() const noexcept
    {
        return m_impl->tapPipe != nullptr;
    }

    std::wstring App::PipeName() const
    {
        return m_impl->tapPipe == nullptr ? std::wstring() : m_impl->tapPipe->PipeName();
    }

    Status App::InjectNow(inject::State& state)
    {
        state = {};

        if (!m_impl->ready)
        {
            return Status::Failed(L"not-ready");
        }

        const auto prepared = m_impl->PrepareDeployment();
        if (!prepared.IsOk())
        {
            return prepared;
        }

        const auto injected = m_impl->injection->Inject(
            inject::BuildInjectionOptions(m_impl->deployment, kInjectionTimeoutMs), state);
        if (!injected.IsOk())
        {
            return injected;
        }
        if (!state.tapLoaded)
        {
            return Status::Failed(L"tap-not-ready");
        }
        return Status::Ok();
    }

    Status App::StartMonitoring()
    {
        if (!m_impl->ready)
        {
            return Status::Failed(L"not-ready");
        }

        if (m_impl->monitor != nullptr)
        {
            return Status::Ok();
        }

        const auto prepared = m_impl->PrepareDeployment();
        if (!prepared.IsOk())
        {
            log::Logger::Instance().WriteFormat(
                log::Level::Error, kChannel, L"Injection monitor startup failed detail={0}", { prepared.detail });
            return prepared;
        }

        auto monitor = std::make_unique<inject::InjectionMonitor>(
            *m_impl->injection,
            inject::BuildInjectionOptions(m_impl->deployment, kInjectionTimeoutMs),
            kMonitorIntervalMs);
        monitor->Start();

        log::Logger::Instance().WriteFormat(
            log::Level::Info, kChannel, L"Injection monitor started launcher={0}", { m_impl->deployment.launcher.wstring() });

        m_impl->monitor = std::move(monitor);
        return Status::Ok();
    }

    void App::StopMonitoring()
    {
        m_impl->monitor.reset();
    }

    bool App::IsMonitoring() const noexcept
    {
        return m_impl->monitor != nullptr;
    }

    inject::State App::InjectionState() const
    {
        if (m_impl->monitor != nullptr)
        {
            return m_impl->monitor->Snapshot();
        }

        inject::State state;
        m_impl->injection->QueryState(state);
        return state;
    }

    void App::WaitForShutdown()
    {
        ::SetConsoleCtrlHandler(HandleConsoleSignal, TRUE);
        ::WaitForSingleObject(ShutdownEvent(), INFINITE);
        ::SetConsoleCtrlHandler(HandleConsoleSignal, FALSE);
    }

    void App::Impl::HandleTapCommand(const inject::TapCommand& command)
    {
        const HRESULT comStatus = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool release = SUCCEEDED(comStatus);

        switch (command.verb)
        {
        case inject::TapVerb::SetDefault:
            HandleSetDefault(command);
            break;
        case inject::TapVerb::SetRedirect:
            HandleSetRedirect(command);
            break;
        case inject::TapVerb::ClearRedirect:
            HandleClearRedirect();
            break;
        case inject::TapVerb::Manager:
        {
            const auto opened = management::OpenManager();
            if (!opened)
                ::MessageBoxW(nullptr, text.Resolve(L"manager.missing").c_str(),
                              text.Resolve(L"manager.title").c_str(), MB_OK | MB_ICONERROR);
            break;
        }
        case inject::TapVerb::Exit:
        case inject::TapVerb::UpdateExit:
        {
            const bool updating = command.verb == inject::TapVerb::UpdateExit;
            if (updating)
            {
                DWORD foregroundProcess = 0;
                if (const auto foreground = ::GetForegroundWindow())
                    ::GetWindowThreadProcessId(foreground, &foregroundProcess);
                inject::State injectionState;
                if (foregroundProcess && App::Instance().Injection().QueryState(injectionState) &&
                    injectionState.tapLoaded && injectionState.target.processId == foregroundProcess)
                {
                    log::Logger::Instance().Write(log::Level::Info, kPipeChannel, L"Update shutdown deferred while the sound panel is active");
                    break;
                }
                std::lock_guard lock(recordingMutex);
                const auto state = recordingMode->State();
                if (state.enabled || state.cleanupPending)
                {
                    log::Logger::Instance().Write(log::Level::Info, kPipeChannel, L"Update shutdown deferred while recording is active");
                    break;
                }
                updateShutdown = true;
            }
            const auto result = management::StopInjection();
            if (result.IsOk())
            {
                log::Logger::Instance().Write(log::Level::Info, kPipeChannel, L"ShellHost stopped; background host exiting");
                ::SetEvent(ShutdownEvent());
            }
            else
            {
                if (updating)
                {
                    std::lock_guard lock(recordingMutex);
                    updateShutdown = false;
                }
                log::Logger::Instance().WriteFormat(
                    log::Level::Error, kPipeChannel, L"Host exit failed detail={0}", { result.detail });
            }
            break;
        }
        default:
            log::Logger::Instance().WriteFormat(log::Level::Warn, kPipeChannel, L"Unhandled click message raw={0}", { command.raw });
            break;
        }

        if (release)
        {
            ::CoUninitialize();
        }
    }

    void App::Impl::HandleSetDefault(const inject::TapCommand& command)
    {
        if (command.arguments.size() < 2)
        {
            log::Logger::Instance().WriteFormat(log::Level::Warn, kPipeChannel, L"Invalid pipe arguments ignored raw={0}", { command.raw });
            return;
        }

        audio::DataFlow flow{};
        if (!audio::TryParseDataFlow(command.arguments[0], flow))
        {
            log::Logger::Instance().WriteFormat(log::Level::Warn, kPipeChannel, L"Invalid pipe arguments ignored raw={0}", { command.raw });
            return;
        }

        std::wstring deviceId = command.arguments[1];
        if (deviceId == inject::kVirtualDeviceTarget)
        {
            audio::DeviceInfo virtualDevice;
            if (!audioDevices->FindVirtualDevice(virtualDevice).IsOk())
            {
                log::Logger::Instance().WriteFormat(
                    log::Level::Warn,
                    kPipeChannel,
                    L"Virtual endpoint missing; default unchanged flow={0}",
                    { std::wstring(audio::ToString(flow)) });
                return;
            }

            deviceId = virtualDevice.id;
        }

        for (const auto role : kPolicyRoles)
        {
            const auto status = endpointPolicy->SetDefaultDevice(deviceId, role);
            log::Logger::Instance().WriteFormat(
                log::Level::Info,
                kPipeChannel,
                L"Default endpoint changed flow={0} role={1} code={2}",
                { std::wstring(audio::ToString(flow)), std::wstring(audio::ToString(role)), std::to_wstring(status.code) });
        }
    }

    void App::Impl::HandleSetRedirect(const inject::TapCommand& command)
    {
        if (command.arguments.size() < 2)
        {
            log::Logger::Instance().WriteFormat(log::Level::Warn, kPipeChannel, L"Invalid pipe arguments ignored raw={0}", { command.raw });
            return;
        }

        audio::DataFlow flow{};
        std::int64_t processId = 0;
        if (!audio::TryParseDataFlow(command.arguments[0], flow) ||
            !strings::TryParseInt(command.arguments[1], processId) ||
            processId <= 0)
        {
            log::Logger::Instance().WriteFormat(log::Level::Warn, kPipeChannel, L"Invalid pipe arguments ignored raw={0}", { command.raw });
            return;
        }

        const std::wstring deviceId = command.arguments.size() > 2 ? command.arguments[2] : std::wstring();
        bool applied = false;
        for (const auto role : kPolicyRoles)
        {
            const auto status = endpointPolicy->SetAppDefaultDevice(
                static_cast<std::uint32_t>(processId),
                flow,
                role,
                deviceId);
            if (status.IsOk())
            {
                applied = true;
            }
            log::Logger::Instance().WriteFormat(
                log::Level::Info,
                kPipeChannel,
                L"Application route changed pid={0} role={1} code={2}",
                { std::to_wstring(processId), std::wstring(audio::ToString(role)), std::to_wstring(status.code) });
        }

        if (!applied)
        {
            log::Logger::Instance().WriteFormat(
                log::Level::Warn, kPipeChannel, L"Application route was not applied pid={0}", { std::to_wstring(processId) });
        }
    }

    void App::Impl::HandleClearRedirect()
    {
        const auto status = endpointPolicy->ClearAppDefaultDevices();
        log::Logger::Instance().WriteFormat(
            log::Level::Info,
            kPipeChannel,
            L"Application routes cleared code={0}",
            { std::to_wstring(status.code) });

    }

    Status App::StopRecording()
    {
        std::lock_guard lock(m_impl->recordingMutex);
        if (!m_impl->recordingMode) return Status::Ok();
        const auto result = m_impl->recordingMode->Disable();
        m_impl->PublishRecordingPanel(true);
        return result;
    }

    std::string App::Impl::HandleTapRequest(const inject::TapCommand& command)
    {
        if (command.verb != inject::TapVerb::Recording)
        {
            HandleTapCommand(command);
            return {};
        }
        const auto initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Status result;
        if (FAILED(initialized)) result = Status::FromHResult(L"recording-com", initialized);
        else
        {
            try
            {
                std::lock_guard lock(recordingMutex);
                const auto& action = command.arguments[0];
                if (action == L"on" && command.arguments.size() == 1)
                    result = updateShutdown ? Status::Failed(L"update-shutdown") : recordingMode->Enable();
                else if (action == L"off" && command.arguments.size() == 1) result = recordingMode->Disable();
                else if (action == L"monitor" && command.arguments.size() == 2) result = recordingMode->ChangeMonitor(command.arguments[1]);
                else if (action == L"status" && command.arguments.size() == 1) result = recordingMode->CheckHealth();
                else if (action != L"status" || command.arguments.size() != 1) result = Status::InvalidArguments(L"recording-action");
                PublishRecordingPanel(action != L"status");
            }
            catch (...) { result = Status::Failed(L"recording-exception"); }
            ::CoUninitialize();
        }
        const auto state = recordingMode ? recordingMode->State() : recording::Snapshot{};
        return strings::ToNarrow(std::to_wstring(result.code) + L"\t" + (state.enabled ? L"1" : L"0") +
            L"\t" + (state.cleanupPending ? L"1" : L"0") + L"\t" + state.monitorDevice + L"\t" + result.detail + L"\n");
    }

}
