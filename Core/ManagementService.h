#pragma once

#include "Foundation.h"

namespace vmex::management
{
    struct Snapshot final
    {
        bool running = false;
        bool responsive = false;
        bool injected = false;
        bool autostart = false;
        bool showDriverName = true;
        std::uint32_t processId = 0;
    };

    [[nodiscard]] Snapshot Query();
    Status StartHost();
    Status StopHost();
    Status OpenManager();
    Status OpenPanel();
    Status OpenLogs();
    Status OpenProject();
    Status SetAutostart(bool enabled);
    Status SetShowDriverName(bool enabled);
    Status RegisterStartMenu();
    Status Uninstall();
    Status StopInjection();
}
