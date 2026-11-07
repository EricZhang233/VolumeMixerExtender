#pragma once

#include "Foundation.h"

namespace vmex::recording
{
    inline constexpr std::wstring_view kDriverInstallArgument = L"--recording-driver-install";
    inline constexpr std::wstring_view kDriverRemoveArgument = L"--recording-driver-remove";

    struct DriverState final
    {
        bool installed = false;
        bool started = false;
        bool packagePresent = false;
        std::uint32_t problem = 0;
        bool restartRequired = false;
    };

    DriverState QueryDriver();
    Status QueryRecordingReadiness(std::wstring_view monitorDevice = {});
    Status SetRecordingEndpointEnabled(std::wstring_view id, bool enabled);
    Status ExtractDriverPackage(const std::filesystem::path& directory);
    Status ValidateDriverPackage(const std::filesystem::path& directory);
    Status DisableCableEndpoints();
    Status RestoreCableEndpoints();
    Status ProtectPendingDriverDefaults();
    Status InstallDriver();
    Status UninstallDriver();
    int RunDriverMaintenance(bool install);
}
