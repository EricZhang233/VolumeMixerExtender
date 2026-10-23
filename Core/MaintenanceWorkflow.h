#pragma once

#include "Foundation.h"

namespace vmex::maintenance
{
    class IUninstallActions
    {
    public:
        virtual ~IUninstallActions() = default;
        virtual Status DisableAutostart() = 0;
        virtual Status StopHost() = 0;
        virtual Status ResetAudioPreferences() = 0;
        virtual Status RemoveSettings() = 0;
        virtual Status RemoveConfig() = 0;
        virtual Status CloseLogsAndRemoveCache() = 0;
    };

    class IAudioResetActions
    {
    public:
        virtual ~IAudioResetActions() = default;
        virtual Status ClearRouting() = 0;
        virtual Status ResetSessionVolumes() = 0;
        virtual Status ClearPersistedVolumes() = 0;
    };

    Status RunUninstall(IUninstallActions& actions);
    Status ResetAudioPreferences(IAudioResetActions& actions);
}
