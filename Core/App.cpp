#include "App.h"

#include "AutostartEntry.h"
#include "PayloadResources.h"
#include "Platform.h"
#include "Strings.h"
#include "Version.h"

namespace vmex
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"app";

        cli::Result Unsupported(std::wstring_view operation)
        {
            (void)operation;
            return cli::Result{ cli::Outcome::NotSupported, {} };
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

    struct App::Impl final
    {
        AppOptions options;
        text::TextService text;
        config::ConfigService config;
        cli::CommandRegistry commands;
        std::unique_ptr<audio::IAudioDeviceManager> audioDevices;
        std::unique_ptr<audio::IEndpointPolicyService> endpointPolicy;
        std::unique_ptr<inject::IInjectionService> injection;
        bool ready = false;
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

        if (!m_impl->options.configFile.empty())
        {
            m_impl->config.Save();
        }

        log::Logger::Instance().WriteKey(log::Level::Info, kChannel, L"log.app.shutdown");
        log::Logger::Instance().Flush();
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

    void App::RegisterCommands()
    {
        auto help = MakeCommand(L"help", L"cli.group.core", L"cmd.help.summary");
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
            output.Field(app.Text().Resolve(L"cli.field.command"), L"status");
            output.Field(app.Text().Resolve(L"cli.field.status"), app.Text().Resolve(L"cli.outcome.not_supported"));
            return cli::Result{ cli::Outcome::NotSupported, {} };
        };
        m_impl->commands.Add(std::move(status));

        auto inject = MakeCommand(L"inject", L"cli.group.inject", L"cmd.inject.summary");
        inject.options.push_back(cli::OptionSpec{ L"tap", L"path", L"cmd.inject.opt.tap", true, false });
        inject.options.push_back(cli::OptionSpec{ L"wait", L"ms", L"cmd.inject.opt.wait", true, false });
        inject.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"inject");
        };
        m_impl->commands.Add(std::move(inject));

        auto eject = MakeCommand(L"eject", L"cli.group.inject", L"cmd.eject.summary");
        eject.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"eject");
        };
        m_impl->commands.Add(std::move(eject));

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

            for (const auto item : { payload::Item::Launcher, payload::Item::Tap })
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
        devices.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"devices");
        };
        m_impl->commands.Add(std::move(devices));

        auto sessions = MakeCommand(L"sessions", L"cli.group.audio", L"cmd.sessions.summary");
        sessions.arguments.push_back(cli::ArgumentSpec{ L"device", L"cmd.sessions.arg.device", false });
        sessions.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"sessions");
        };
        m_impl->commands.Add(std::move(sessions));

        auto volume = MakeCommand(L"volume", L"cli.group.audio", L"cmd.volume.summary");
        volume.arguments.push_back(cli::ArgumentSpec{ L"value", L"cmd.volume.arg.value", true });
        volume.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"volume");
        };
        m_impl->commands.Add(std::move(volume));

        auto mute = MakeCommand(L"mute", L"cli.group.audio", L"cmd.mute.summary");
        mute.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"mute");
        };
        m_impl->commands.Add(std::move(mute));

        auto defaultDevice = MakeCommand(L"default", L"cli.group.audio", L"cmd.default.summary");
        defaultDevice.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"default");
        };
        m_impl->commands.Add(std::move(defaultDevice));

        auto redirect = MakeCommand(L"redirect", L"cli.group.audio", L"cmd.redirect.summary");
        redirect.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"redirect");
        };
        m_impl->commands.Add(std::move(redirect));

        auto clearRedirect = MakeCommand(L"clear-redirect", L"cli.group.audio", L"cmd.clear-redirect.summary");
        clearRedirect.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"clear-redirect");
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

        auto config = MakeCommand(L"config", L"cli.group.config", L"cmd.config.summary");
        config.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"config");
        };
        m_impl->commands.Add(std::move(config));

        auto logCommand = MakeCommand(L"log", L"cli.group.config", L"cmd.log.summary");
        logCommand.handler = [](const cli::Invocation&, cli::ICliOutput&) -> cli::Result {
            return Unsupported(L"log");
        };
        m_impl->commands.Add(std::move(logCommand));
    }
}
