#include "App.h"
#include "CliService.h"
#include "Runtime.h"
#include "TextService.h"

#include <string>
#include <vector>

namespace
{
}

int wmain(int argc, wchar_t** argv)
{
    const auto arguments = vmex::runtime::CollectArguments(argc, argv);
    auto& app = vmex::App::Instance();
    const auto status = app.Initialize(vmex::runtime::BuildOptions(arguments));
    if (!status.IsOk())
    {
        std::wstring message(vmex::text::Embedded().Resolve(L"cli.init_failed"));
        message.append(status.detail);
        message.append(L"\r\n");
        vmex::cli::WriteToConsole(message, true);
        return vmex::kCodeFailed;
    }

    const vmex::cli::CliService service(app.Commands(), app.Text());
    const auto commandArguments = vmex::runtime::RemoveGlobalOptions(arguments);

    const auto exitCode = service.Run(commandArguments);
    app.Shutdown();
    return exitCode;
}
