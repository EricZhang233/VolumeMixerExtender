#include "MaintenanceWorkflow.h"

namespace vmex::maintenance
{
    Status RunUninstall(IUninstallActions& actions)
    {
        for (const auto step : { &IUninstallActions::DisableAutostart, &IUninstallActions::StopHost,
                                &IUninstallActions::ResetAudioPreferences, &IUninstallActions::RemoveSettings,
                                &IUninstallActions::RemoveConfig, &IUninstallActions::CloseLogsAndRemoveCache })
        {
            const auto result = (actions.*step)();
            if (!result) return result;
        }
        return Status::Ok();
    }

    Status ResetAudioPreferences(IAudioResetActions& actions)
    {
        std::wstring failures;
        for (const auto step : { &IAudioResetActions::ClearRouting, &IAudioResetActions::ResetSessionVolumes,
                                &IAudioResetActions::ClearPersistedVolumes })
        {
            const auto result = (actions.*step)();
            if (!result)
            {
                if (!failures.empty()) failures.append(L"; ");
                failures.append(result.detail);
            }
        }
        return failures.empty() ? Status::Ok() : Status::Failed(failures);
    }
}
