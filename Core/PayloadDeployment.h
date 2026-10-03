#pragma once

#include "Foundation.h"
#include "InjectionService.h"

namespace vmex::inject
{
    struct Deployment final
    {
        std::filesystem::path directory;
        std::filesystem::path launcher;
        std::filesystem::path tap;
        std::filesystem::path config;
    };

    [[nodiscard]] Status DeployPayloads(const std::filesystem::path& root, unsigned long sessionId, Deployment& deployment);

    [[nodiscard]] Options BuildInjectionOptions(const Deployment& deployment, std::uint32_t timeoutMs);
}
