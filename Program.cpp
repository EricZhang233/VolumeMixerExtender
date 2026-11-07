#include "App.h"
#include "Runtime.h"
#include "RecordingGuard.h"
#include "RecordingDriverService.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const auto arguments = vmex::runtime::Arguments();
    if (!arguments.empty() && arguments.front() == L"--recording-guard")
        return vmex::recording::RunGuard(arguments);
    if (!arguments.empty() && (arguments.front() == vmex::recording::kDriverInstallArgument ||
        arguments.front() == vmex::recording::kDriverRemoveArgument))
        return arguments.size() == 1 ? vmex::recording::RunDriverMaintenance(arguments.front() == vmex::recording::kDriverInstallArgument) :
            ERROR_INVALID_PARAMETER;
    const bool autorun = vmex::runtime::HasAutorunArgument();
    auto& app = vmex::App::Instance();
    const auto initialized = app.Initialize(vmex::runtime::BuildOptions(autorun));
    if (!initialized.IsOk())
    {
        return vmex::kCodeFailed;
    }

    const auto served = app.Serve();
    if (!served.IsOk())
    {
        app.Shutdown();
        return vmex::kCodeFailed;
    }

    app.WaitForShutdown();
    app.StopServing();
    app.Shutdown();
    return vmex::kCodeOk;
}
