#pragma once

#include "Foundation.h"
#include "InjectionService.h"

namespace vmex::inject
{
    struct Deployment final
    {
        std::filesystem::path launcher;
        std::filesystem::path tap;
        std::filesystem::path diagnostics;
        std::filesystem::path core;
    };

    [[nodiscard]] Status DeployPayloads(Deployment& deployment);

    [[nodiscard]] Options BuildInjectionOptions(const Deployment& deployment, std::uint32_t timeoutMs);
}
