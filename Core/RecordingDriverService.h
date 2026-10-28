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
    Status ValidateDriverPackage(const std::filesystem::path& directory, bool& testSigned);
    Status InstallDriver();
    Status UninstallDriver();
}
