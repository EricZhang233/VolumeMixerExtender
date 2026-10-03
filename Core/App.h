#pragma once

#include "AudioDeviceManager.h"
#include "CliTypes.h"
#include "CommandRegistry.h"
#include "ConfigService.h"
#include "EndpointPolicyService.h"
#include "Foundation.h"
#include "InjectionService.h"
#include "Logger.h"
#include "TextService.h"

namespace vmex
{
    struct AppOptions final
    {
        std::filesystem::path configFile;
        std::filesystem::path logFile;
        log::Level logLevel = log::Level::Info;
        bool consoleLog = false;
    };

    class App final
    {
    public:
        [[nodiscard]] static App& Instance();

        Status Initialize(const AppOptions& options);
        void Shutdown();

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] const AppOptions& Options() const noexcept;
        [[nodiscard]] std::wstring Version() const;

        [[nodiscard]] text::TextService& Text() noexcept;
        [[nodiscard]] config::ConfigService& Config() noexcept;
        [[nodiscard]] cli::CommandRegistry& Commands() noexcept;
        [[nodiscard]] audio::IAudioDeviceManager& AudioDevices();
        [[nodiscard]] audio::IEndpointPolicyService& EndpointPolicy();
        [[nodiscard]] inject::IInjectionService& Injection();

        void Report(log::Level level, std::wstring_view channel, std::wstring_view key);
        void ReportFormat(log::Level level, std::wstring_view channel, std::wstring_view key, const std::vector<std::wstring>& arguments);

        Status Serve();
        void StopServing();
        void WaitForShutdown();
        [[nodiscard]] bool IsServing() const noexcept;
        [[nodiscard]] std::wstring PipeName() const;

        Status InjectNow(inject::State& state);
        Status StartMonitoring();
        void StopMonitoring();
        [[nodiscard]] bool IsMonitoring() const noexcept;
        [[nodiscard]] inject::State InjectionState() const;

    private:
        App();
        ~App();
        App(const App&) = delete;
        App& operator=(const App&) = delete;
        App(App&&) = delete;
        App& operator=(App&&) = delete;

        void RegisterCommands();

        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
