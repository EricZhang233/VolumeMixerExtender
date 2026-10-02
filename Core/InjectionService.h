#pragma once

#include "Foundation.h"

namespace vmex::inject
{
    struct TargetProcess final
    {
        std::uint32_t processId = 0;
        std::wstring imageName;
        std::wstring windowClass;
        std::wstring windowTitle;
    };

    struct Options final
    {
        std::filesystem::path launcherModule;
        std::filesystem::path tapModule;
        std::filesystem::path configModule;
        std::wstring initData;
        std::uint32_t timeoutMs = 5000;
    };

    struct State final
    {
        TargetProcess target;
        bool launcherLoaded = false;
        bool tapLoaded = false;
        std::uint32_t injectionCount = 0;
    };

    class IInjectionService
    {
    public:
        virtual ~IInjectionService() = default;

        virtual Status FindTarget(TargetProcess& target) = 0;
        virtual Status Inject(const Options& options, State& state) = 0;
        virtual Status Eject(const TargetProcess& target) = 0;
        virtual Status QueryState(State& state) = 0;
    };

    [[nodiscard]] std::unique_ptr<IInjectionService> CreateInjectionService();
}
