#include "App.h"
#include "Runtime.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    const bool autorun = vmex::runtime::HasAutorunArgument();
    auto& app = vmex::App::Instance();
    const auto initialized = app.Initialize(vmex::runtime::BuildOptions({}, autorun));
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

    vmex::runtime::HostNotifications notifications;
    notifications.Start(app, autorun);

    app.WaitForShutdown();
    notifications.Stop();
    app.StopServing();
    app.Shutdown();
    return vmex::kCodeOk;
}
