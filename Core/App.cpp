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
#include "UninstallService.h"
#include "Version.h"

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

        std::vector<audio::DeviceRole> SelectRoles(const cli::Invocation& invocation, bool& valid)
        {
            const auto roleText = invocation.Option(L"role", L"");
            if (roleText.empty())
            {
                valid = true;
                return { kPolicyRoles.begin(), kPolicyRoles.end() };
            }

            audio::DeviceRole role{};
            if (strings::EqualsIgnoreCase(roleText, L"console"))
            {
                role = audio::DeviceRole::Console;
            }
            else if (strings::EqualsIgnoreCase(roleText, L"multimedia"))
            {
                role = audio::DeviceRole::Multimedia;
            }
            else if (strings::EqualsIgnoreCase(roleText, L"communications"))
            {
                role = audio::DeviceRole::Communications;
            }
            else
            {
                valid = false;
                return {};
            }

            valid = true;
            return { role };
        }

        std::wstring_view StateKey(audio::DeviceState state)
        {
            switch (state)
            {
            case audio::DeviceState::Disabled: return L"audio.state.disabled";
            case audio::DeviceState::NotPresent: return L"audio.state.not-present";
            case audio::DeviceState::Unplugged: return L"audio.state.unplugged";
            case audio::DeviceState::All: return L"audio.state.all";
            default: return L"audio.state.active";
            }
        }

        cli::Command MakeCommand(std::wstring name, std::wstring groupKey, std::wstring summaryKey)
        {
            cli::Command command;
            command.name = std::move(name);
            command.groupKey = std::move(groupKey);
            command.summaryKey = std::move(summaryKey);
            return command;
        }
    }

    struct App::Impl final : inject::ITapCommandHandler
    {
        AppOptions options;
        text::TextService text;
        config::ConfigService config;
        cli::CommandRegistry commands;
        std::unique_ptr<audio::IAudioDeviceManager> audioDevices;
        std::unique_ptr<audio::IEndpointPolicyService> endpointPolicy;
        std::unique_ptr<inject::IInjectionService> injection;
        std::unique_ptr<inject::TapPipeServer> tapPipe;
        std::unique_ptr<inject::InjectionMonitor> monitor;
        inject::Deployment deployment;
        host::HostPresence presence;
        bool comInitialized = false;
        bool ready = false;

        Status PrepareDeployment()
        {
            return inject::DeployPayloads(
                platform::GetCacheDirectory(), platform::GetCurrentSessionId(), deployment);
        }

        void HandleTapCommand(const inject::TapCommand& command) override;
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
        if (options.consoleLog)
        {
            log::Logger::Instance().AddSink(log::CreateDebugSink());
        }
        log::Logger::Instance().SetMinimumLevel(options.logLevel);

        const auto source = text::CreateEmbeddedTextSource();
        const auto attachStatus = m_impl->text.Attach(source);
        if (!attachStatus.IsOk())
        {
            return attachStatus;
        }
        log::Logger::Instance().SetResolver(&m_impl->text);

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.text.loaded",
            { std::to_wstring(m_impl->text.Count()) });

        if (!options.configFile.empty())
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info,
                kChannel,
                L"log.app.init",
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

        RegisterCommands();
        m_impl->ready = true;

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.app.ready",
            { std::to_wstring(m_impl->text.Count()) });

        return Status::Ok();
    }

    void App::Shutdown()
    {
        if (!m_impl->ready)
        {
            return;
        }

        StopServing();

        if (!m_impl->options.configFile.empty())
        {
            m_impl->config.Save();
        }

        log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.app.shutdown");
        log::Logger::Instance().Flush();

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

    cli::CommandRegistry& App::Commands() noexcept
    {
        return m_impl->commands;
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

    void App::Report(log::Level level, std::wstring_view channel, std::wstring_view key)
    {
        log::Logger::Instance().WriteKey(level, channel, key);
    }

    void App::ReportFormat(log::Level level, std::wstring_view channel, std::wstring_view key, const std::vector<std::wstring>& arguments)
    {
        log::Logger::Instance().WriteKeyFormat(level, channel, key, arguments);
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
            log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.app.serve_conflict");
            return presence;
        }

        auto server = std::make_unique<inject::TapPipeServer>(
            inject::TapPipeName(platform::GetCurrentSessionId()),
            *m_impl);

        if (!server->Start())
        {
            m_impl->presence.Release();
            log::Logger::Instance().WriteKey(log::Level::Error, kChannel, L"log.app.serve_failed");
            return Status::Failed(L"pipe-start");
        }

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kChannel,
            L"log.app.serve",
            { server->PipeName() });

        m_impl->tapPipe = std::move(server);

        const auto monitoring = StartMonitoring();
        if (!monitoring.IsOk())
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Warn, kChannel, L"log.app.monitor_failed", { monitoring.detail });
        }

        return Status::Ok();
    }

    void App::StopServing()
    {
        if (m_impl->tapPipe == nullptr)
        {
            return;
        }

        StopMonitoring();
        m_impl->tapPipe.reset();
        m_impl->presence.Release();
        log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.app.serve_stopped");
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
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Error, kChannel, L"log.app.monitor_failed", { prepared.detail });
            return prepared;
        }

        auto monitor = std::make_unique<inject::InjectionMonitor>(
            *m_impl->injection,
            inject::BuildInjectionOptions(m_impl->deployment, kInjectionTimeoutMs),
            kMonitorIntervalMs);
        monitor->Start();

        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info, kChannel, L"log.app.monitor", { m_impl->deployment.directory.wstring() });

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
        case inject::TapVerb::Autostart:
        {
            std::wstring error;
            const bool enable = !command.arguments.empty() &&
                strings::EqualsIgnoreCase(command.arguments[0], L"on");
            const bool disable = !command.arguments.empty() &&
                strings::EqualsIgnoreCase(command.arguments[0], L"off");
            if (!enable && !disable)
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Warn, kPipeChannel, L"log.pipe.autostart_invalid", { command.raw });
                break;
            }

            const bool applied = enable
                ? autostart::AutostartEntry::Enable(error)
                : autostart::AutostartEntry::Disable(error);
            log::Logger::Instance().WriteKeyFormat(
                applied ? log::Level::Info : log::Level::Error,
                kPipeChannel,
                applied ? L"log.pipe.autostart_done" : L"log.pipe.autostart_failed",
                { enable ? L"on" : L"off", error });
            break;
        }
        case inject::TapVerb::Uninstall:
        {
            inject::State state;
            if (monitor != nullptr) state = monitor->Snapshot();
            else injection->QueryState(state);
            monitor.reset();
            const auto result = uninstall::DisableAutostartAndTerminateShellHost(state.target.processId);
            if (result.ok)
            {
                log::Logger::Instance().WriteKey(log::Level::Info, kPipeChannel, L"log.pipe.uninstall_done");
                ::SetEvent(ShutdownEvent());
            }
            else
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Error, kPipeChannel, L"log.pipe.uninstall_failed", { result.detail });
            }
            break;
        }
        case inject::TapVerb::Exit:
        {
            inject::State state;
            if (monitor != nullptr) state = monitor->Snapshot();
            else injection->QueryState(state);
            monitor.reset();
            const auto result = uninstall::TerminateShellHost(state.target.processId);
            if (result.ok)
            {
                log::Logger::Instance().WriteKey(log::Level::Info, kPipeChannel, L"log.pipe.exit_done");
                ::SetEvent(ShutdownEvent());
            }
            else
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Error, kPipeChannel, L"log.pipe.exit_failed", { result.detail });
            }
            break;
        }
        default:
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kPipeChannel, L"log.pipe.click_unhandled", { command.raw });
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
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kPipeChannel, L"log.pipe.invalid", { command.raw });
            return;
        }

        audio::DataFlow flow{};
        if (!audio::TryParseDataFlow(command.arguments[0], flow))
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kPipeChannel, L"log.pipe.invalid", { command.raw });
            return;
        }

        std::wstring deviceId = command.arguments[1];
        if (deviceId == inject::kVirtualDeviceTarget)
        {
            audio::DeviceInfo virtualDevice;
            if (!audioDevices->FindVirtualDevice(virtualDevice).IsOk())
            {
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Warn,
                    kPipeChannel,
                    L"log.pipe.set_default_no_virtual",
                    { std::wstring(audio::ToString(flow)) });
                return;
            }

            deviceId = virtualDevice.id;
        }

        for (const auto role : kPolicyRoles)
        {
            const auto status = endpointPolicy->SetDefaultDevice(deviceId, role);
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info,
                kPipeChannel,
                L"log.pipe.set_default",
                { std::wstring(audio::ToString(flow)), std::wstring(audio::ToString(role)), std::to_wstring(status.code) });
        }
    }

    void App::Impl::HandleSetRedirect(const inject::TapCommand& command)
    {
        if (command.arguments.size() < 2)
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kPipeChannel, L"log.pipe.invalid", { command.raw });
            return;
        }

        audio::DataFlow flow{};
        std::int64_t processId = 0;
        if (!audio::TryParseDataFlow(command.arguments[0], flow) ||
            !strings::TryParseInt(command.arguments[1], processId) ||
            processId <= 0)
        {
            log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kPipeChannel, L"log.pipe.invalid", { command.raw });
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
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Info,
                kPipeChannel,
                L"log.pipe.set_redirect",
                { std::to_wstring(processId), std::wstring(audio::ToString(role)), std::to_wstring(status.code) });
        }

        if (!applied)
        {
            log::Logger::Instance().WriteKeyFormat(
                log::Level::Warn, kPipeChannel, L"log.pipe.redirect_not_applied", { std::to_wstring(processId) });
        }
    }

    void App::Impl::HandleClearRedirect()
    {
        const auto status = endpointPolicy->ClearAppDefaultDevices();
        log::Logger::Instance().WriteKeyFormat(
            log::Level::Info,
            kPipeChannel,
            L"log.pipe.clear_redirect",
            { std::to_wstring(status.code) });

    }

    void App::RegisterCommands()
    {
        auto help = MakeCommand(L"help", L"cli.group.core", L"cmd.help.summary");
        help.aliases = { L"-help", L"--help", L"-h" };
        help.arguments.push_back(cli::ArgumentSpec{ L"command", L"cmd.help.arg.command", false });
        help.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();
            if (const auto* positional = invocation.Positional(0))
            {
                const auto* command = app.Commands().Find(*positional);
                if (command == nullptr)
                {
                    output.Error(app.Text().ResolveFormat(L"cli.error.unknown_command", { *positional }));
                    return cli::Result{ cli::Outcome::InvalidArguments, {} };
                }

                for (const auto& line : app.Commands().RenderCommandHelp(*command, app.Text()))
                {
                    output.Line(line);
                }
                return cli::Result{ cli::Outcome::Success, {} };
            }

            for (const auto& line : app.Commands().RenderGeneralHelp(app.Text()))
            {
                output.Line(line);
            }
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(help));

        auto skill = MakeCommand(L"skill", L"cli.group.core", L"cmd.skill.summary");
        skill.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();
            for (const auto& line : app.Commands().RenderSkill(app.Text()))
            {
                output.Line(line);
            }
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(skill));

        auto version = MakeCommand(L"version", L"cli.group.core", L"cmd.version.summary");
        version.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            output.Line(App::Instance().Version());
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(version));

        auto status = MakeCommand(L"status", L"cli.group.inject", L"cmd.status.summary");
        status.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            const bool hosting = app.IsServing() || host::HostPresence::IsRunning();
            output.Field(app.Text().Resolve(L"cli.field.command"), L"status");
            output.Field(
                app.Text().Resolve(L"cli.field.host"),
                app.Text().Resolve(hosting ? L"cli.value.running" : L"cli.value.stopped"));

            if (hosting)
            {
                output.Field(
                    app.Text().Resolve(L"cli.field.pipe"),
                    app.IsServing() ? app.PipeName() : inject::TapPipeName(platform::GetCurrentSessionId()));
            }

            inject::State state = app.InjectionState();
            const std::wstring_view injectionKey = state.target.processId == 0
                ? L"cli.value.not_injected"
                : (state.tapLoaded ? L"cli.value.injected" : L"cli.value.pending");
            output.Field(app.Text().Resolve(L"cli.field.inject"), app.Text().Resolve(injectionKey));
            if (state.target.processId != 0)
            {
                output.Field(app.Text().Resolve(L"cli.field.pid"), std::to_wstring(state.target.processId));
            }

            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(status));

        auto hostCommand = MakeCommand(L"host", L"cli.group.core", L"cmd.host.summary");
        hostCommand.examples.push_back(cli::ExampleSpec{ L"vmex_cli host", L"cmd.host.example.run" });
        hostCommand.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            const auto started = app.Serve();
            if (!started.IsOk())
            {
                output.Error(app.Text().Resolve(
                    started.detail == L"host-running" ? L"cli.error.host_running" : L"cli.error.host_failed"));
                return cli::Result{ cli::Outcome::Failed, {} };
            }

            output.Field(app.Text().Resolve(L"cli.field.pipe"), app.PipeName());
            output.Field(app.Text().Resolve(L"cli.field.status"), app.Text().Resolve(L"cli.value.hosting"));
            app.WaitForShutdown();
            app.StopServing();
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(hostCommand));

        auto inject = MakeCommand(L"inject", L"cli.group.inject", L"cmd.inject.summary");
        inject.options.push_back(cli::OptionSpec{ L"tap", L"path", L"cmd.inject.opt.tap", true, false });
        inject.options.push_back(cli::OptionSpec{ L"wait", L"ms", L"cmd.inject.opt.wait", true, false });
        inject.examples.push_back(cli::ExampleSpec{ L"vmex_cli inject", L"cmd.inject.example.run" });
        inject.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            inject::State state;
            const auto status = app.InjectNow(state);
            if (!status.IsOk())
            {
                if (status.detail == L"tap-not-ready")
                {
                    output.Error(app.Text().Resolve(L"cli.error.tap_not_ready"));
                }
                else
                {
                    output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { status.detail }));
                }
                return cli::Result{ cli::Outcome::Failed, {} };
            }

            output.Field(app.Text().Resolve(L"cli.field.pid"), std::to_wstring(state.target.processId));
            output.Field(
                app.Text().Resolve(L"cli.field.inject"),
                app.Text().Resolve(state.tapLoaded ? L"cli.value.injected" : L"cli.value.pending"));
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(inject));

        auto payload = MakeCommand(L"payload", L"cli.group.inject", L"cmd.payload.summary");
        payload.options.push_back(cli::OptionSpec{ L"extract", L"dir", L"cmd.payload.opt.extract", true, false });
        payload.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();
            const auto target = invocation.Option(L"extract", L"");

            if (!target.empty())
            {
                payload::ExtractedPair pair;
                const auto status = payload::ExtractPair(std::filesystem::path(target), pair);
                if (!status.IsOk())
                {
                    output.Error(status.detail);
                    return cli::Result{ cli::Outcome::Failed, {} };
                }

                output.Field(app.Text().Resolve(L"cli.field.path"), pair.launcher.wstring());
                output.Field(app.Text().Resolve(L"cli.field.path"), pair.tap.wstring());
                return cli::Result{ cli::Outcome::Success, {} };
            }

            for (const auto item : { payload::Item::Launcher, payload::Item::Tap,
                                     payload::Item::Diagnostics, payload::Item::NotificationToolkit,
                                     payload::Item::Core })
            {
                const auto name = std::wstring(payload::ToString(item));
                output.Field(name, payload::Exists(item) ? std::to_wstring(payload::Size(item)) : app.Text().Resolve(L"cli.field.missing"));
            }
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(payload));

        auto devices = MakeCommand(L"devices", L"cli.group.audio", L"cmd.devices.summary");
        devices.options.push_back(cli::OptionSpec{ L"flow", L"flow", L"cmd.devices.opt.flow", true, false });
        devices.options.push_back(cli::OptionSpec{ L"state", L"state", L"cmd.devices.opt.state", true, false });
        devices.examples.push_back(cli::ExampleSpec{ L"vmex devices --flow render", L"cmd.devices.example.list" });
        devices.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            audio::DataFlow flow = audio::DataFlow::All;
            const auto flowText = invocation.Option(L"flow", L"");
            if (!flowText.empty() && !audio::TryParseDataFlow(flowText, flow))
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { flowText }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            audio::DeviceState state = audio::DeviceState::Active;
            const auto stateText = invocation.Option(L"state", L"");
            if (!stateText.empty() && !audio::TryParseDeviceState(stateText, state))
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { stateText }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            std::vector<audio::DeviceInfo> found;
            const auto status = app.AudioDevices().EnumerateDevices(flow, state, found);
            if (!status.IsOk())
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { status.detail }));
                return cli::Result{ cli::Outcome::Failed, {} };
            }

            output.Field(app.Text().Resolve(L"cli.field.count"), std::to_wstring(found.size()));
            for (const auto& device : found)
            {
                output.Line(device.friendlyName);
                output.Field(app.Text().Resolve(L"cli.field.id"), device.id);
                output.Field(app.Text().Resolve(L"cli.field.state"), app.Text().Resolve(StateKey(device.state)));
                output.Field(
                    app.Text().Resolve(L"cli.field.default"),
                    app.Text().Resolve(device.isDefault ? L"cli.value.yes" : L"cli.value.no"));
            }

            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(devices));

        auto defaultDevice = MakeCommand(L"default", L"cli.group.audio", L"cmd.default.summary");
        defaultDevice.arguments.push_back(cli::ArgumentSpec{ L"device", L"cmd.default.arg.device", true });
        defaultDevice.options.push_back(cli::OptionSpec{ L"role", L"role", L"cmd.default.opt.role", true, false });
        defaultDevice.examples.push_back(cli::ExampleSpec{ L"vmex default {endpoint-id}", L"cmd.default.example.set" });
        defaultDevice.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            bool valid = false;
            const auto roles = SelectRoles(invocation, valid);
            if (!valid)
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { invocation.Option(L"role", L"") }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            const auto deviceId = *invocation.Positional(0);
            std::size_t failures = 0;
            for (const auto role : roles)
            {
                if (!app.EndpointPolicy().SetDefaultDevice(deviceId, role).IsOk())
                {
                    ++failures;
                }
            }

            if (failures != 0)
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { std::to_wstring(failures) }));
                return cli::Result{ cli::Outcome::Failed, {} };
            }

            output.Field(app.Text().Resolve(L"cli.field.value"), deviceId);
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(defaultDevice));

        auto redirect = MakeCommand(L"redirect", L"cli.group.audio", L"cmd.redirect.summary");
        redirect.arguments.push_back(cli::ArgumentSpec{ L"process", L"cmd.redirect.arg.process", true });
        redirect.arguments.push_back(cli::ArgumentSpec{ L"device", L"cmd.redirect.arg.device", false });
        redirect.options.push_back(cli::OptionSpec{ L"flow", L"flow", L"cmd.redirect.opt.flow", true, false });
        redirect.options.push_back(cli::OptionSpec{ L"role", L"role", L"cmd.redirect.opt.role", true, false });
        redirect.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            std::int64_t processId = 0;
            if (!strings::TryParseInt(*invocation.Positional(0), processId) || processId <= 0)
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { *invocation.Positional(0) }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            audio::DataFlow flow = audio::DataFlow::Render;
            const auto flowText = invocation.Option(L"flow", L"");
            if (!flowText.empty() && !audio::TryParseDataFlow(flowText, flow))
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { flowText }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            bool valid = false;
            const auto roles = SelectRoles(invocation, valid);
            if (!valid)
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { invocation.Option(L"role", L"") }));
                return cli::Result{ cli::Outcome::InvalidArguments, {} };
            }

            const std::wstring deviceId = invocation.Positional(1) == nullptr ? std::wstring() : *invocation.Positional(1);
            std::size_t failures = 0;
            for (const auto role : roles)
            {
                if (!app.EndpointPolicy().SetAppDefaultDevice(
                        static_cast<std::uint32_t>(processId), flow, role, deviceId).IsOk())
                {
                    ++failures;
                }
            }

            if (failures != 0)
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { std::to_wstring(failures) }));
                return cli::Result{ cli::Outcome::Failed, {} };
            }

            output.Field(app.Text().Resolve(L"cli.field.value"), deviceId);
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(redirect));

        auto clearRedirect = MakeCommand(L"clear-redirect", L"cli.group.audio", L"cmd.clear-redirect.summary");
        clearRedirect.handler = [](const cli::Invocation&, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();
            const auto status = app.EndpointPolicy().ClearAppDefaultDevices();
            if (!status.IsOk())
            {
                output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { status.detail }));
                return cli::Result{ cli::Outcome::Failed, {} };
            }
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(clearRedirect));

        auto autostart = MakeCommand(L"autostart", L"cli.group.config", L"cmd.autostart.summary");
        autostart.arguments.push_back(cli::ArgumentSpec{ L"state", L"cmd.autostart.arg.state", false });
        autostart.handler = [](const cli::Invocation& invocation, cli::ICliOutput& output) -> cli::Result {
            auto& app = App::Instance();

            if (const auto* state = invocation.Positional(0))
            {
                std::wstring error;
                bool applied = false;
                if (*state == L"on")
                {
                    applied = autostart::AutostartEntry::Enable(error);
                }
                else if (*state == L"off")
                {
                    applied = autostart::AutostartEntry::Disable(error);
                }
                else
                {
                    output.Error(app.Text().ResolveFormat(L"cli.error.invalid_value", { *state }));
                    return cli::Result{ cli::Outcome::InvalidArguments, {} };
                }

                if (!applied)
                {
                    output.Error(app.Text().ResolveFormat(L"cli.error.operation_failed", { error }));
                    return cli::Result{ cli::Outcome::Failed, {} };
                }
            }

            output.Field(
                app.Text().Resolve(L"cli.field.status"),
                app.Text().Resolve(
                    autostart::AutostartEntry::IsEnabled() ? L"cli.value.enabled" : L"cli.value.disabled"));
            return cli::Result{ cli::Outcome::Success, {} };
        };
        m_impl->commands.Add(std::move(autostart));
    }
}
