#pragma once

#include "Foundation.h"

namespace vmex::recording
{
    struct DriverState final
    {
        bool installed = false;
        bool started = false;
        bool packagePresent = false;
        std::uint32_t problem = 0;
    };

    DriverState QueryDriver();
    Status QueryRecordingReadiness(std::wstring_view monitorDevice = {});
    Status SetRecordingEndpointEnabled(std::wstring_view id, bool enabled);
    Status ValidateDriverPackage(const std::filesystem::path& directory, bool& testSigned);
    Status DisableCableEndpoints();
    Status RestoreCableEndpoints();
    Status InstallDriver();
    Status UninstallDriver();
}
