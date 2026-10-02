#include "App.h"
#include "CliService.h"
#include "Platform.h"

#include <windows.h>

#include <string>
#include <vector>

namespace
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

    vmex::AppOptions BuildOptions(const std::vector<std::wstring>& arguments)
    {
        vmex::AppOptions options;

        const auto base = vmex::platform::GetLocalAppDataDirectory() / L"VolumeMixerExtender";
        options.configFile = base / L"vmex.ini";
        options.logFile = base / L"vmex.log";

        for (const auto& token : arguments)
        {
            if (token == L"--verbose")
            {
                options.logLevel = vmex::log::Level::Debug;
                options.consoleLog = true;
            }
            else if (token == L"--trace")
            {
                options.logLevel = vmex::log::Level::Trace;
                options.consoleLog = true;
            }
        }

        return options;
    }
}

int wmain(int argc, wchar_t** argv)
{
    const auto arguments = CollectArguments(argc, argv);
    auto& app = vmex::App::Instance();

    const auto status = app.Initialize(BuildOptions(arguments));
    if (!status.IsOk())
    {
        std::wstring message(L"initialization failed: ");
        message.append(status.detail);
        message.append(L"\r\n");
        vmex::cli::WriteToConsole(message, true);
        return vmex::kCodeFailed;
    }

    const vmex::cli::CliService service(app.Commands(), app.Text());
    const auto exitCode = service.Run(arguments);

    app.Shutdown();
    return exitCode;
}
