#include "Runtime.h"

#include "NotificationService.h"
#include "Platform.h"

#include <windows.h>
#include <shellapi.h>

namespace vmex::runtime
{
    std::vector<std::wstring> CollectArguments(int argc, wchar_t** argv)
    {
        std::vector<std::wstring> arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index)
        {
            arguments.emplace_back(argv[index]);
        }
        return arguments;
    }

    AppOptions BuildOptions(const std::vector<std::wstring>& arguments, bool autorun)
    {
        AppOptions options;
        const auto base = platform::GetInstallDirectory();
        options.configFile = base / L"vmex.ini";
        options.logFile = log::SessionLogFile(L"app");
        options.autorun = autorun;

        for (const auto& token : arguments)
        {
            if (token == L"--verbose")
            {
                options.logLevel = log::Level::Debug;
                options.consoleLog = true;
            }
            else if (token == L"--trace")
            {
                options.logLevel = log::Level::Trace;
                options.consoleLog = true;
            }
        }
        return options;
    }

    std::vector<std::wstring> RemoveGlobalOptions(const std::vector<std::wstring>& arguments)
    {
        std::vector<std::wstring> filtered;
        filtered.reserve(arguments.size());
        for (const auto& token : arguments)
        {
            if (token != L"--verbose" && token != L"--trace")
            {
                filtered.push_back(token);
            }
        }
        return filtered;
    }

    bool HasAutorunArgument()
    {
        int count = 0;
        LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &count);
        if (arguments == nullptr) return false;

        bool autorun = false;
        for (int index = 1; index < count; ++index)
        {
            if (_wcsicmp(arguments[index], L"-autorun") == 0 ||
                _wcsicmp(arguments[index], L"/autorun") == 0)
            {
                autorun = true;
                break;
            }
        }
        ::LocalFree(arguments);
        return autorun;
    }

    HostNotifications::~HostNotifications()
    {
        Stop();
    }

    void HostNotifications::Start(App& app, bool autorun)
    {
        Stop();
        if (autorun) return;

        const auto started = notification::Show(L"VolumeMixerExtender", L"主程序已启动");
        if (!started.IsOk())
        {
            app.Report(log::Level::Warn, L"app", started.detail);
        }

        m_stop.store(false);
        m_thread = std::thread([this, &app]()
        {
            for (int attempt = 0; attempt < 200 && !m_stop.load(); ++attempt)
            {
                if (app.InjectionState().tapLoaded)
                {
                    const auto completed = notification::Show(L"VolumeMixerExtender", L"注入已完成");
                    if (!completed.IsOk())
                    {
                        app.Report(log::Level::Warn, L"app", completed.detail);
                    }
                    return;
                }
                ::Sleep(100);
            }
        });
    }

    void HostNotifications::Stop()
    {
        m_stop.store(true);
        if (m_thread.joinable()) m_thread.join();
    }
}
