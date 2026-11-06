#include "Runtime.h"

#include "Platform.h"

#include <windows.h>
#include <shellapi.h>

namespace vmex::runtime
{
    std::vector<std::wstring> Arguments()
    {
        int argc = 0;
        auto** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
        std::vector<std::wstring> arguments;
        if (!argv) return arguments;
        arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
        for (int index = 1; index < argc; ++index)
        {
            arguments.emplace_back(argv[index]);
        }
        ::LocalFree(argv);
        return arguments;
    }

    AppOptions BuildOptions(bool autorun)
    {
        AppOptions options;
        const auto base = platform::GetInstallDirectory();
        options.configFile = base / L"vmex.ini";
        options.logFile = log::SessionLogFile(L"app");
        options.autorun = autorun;

        return options;
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

}
