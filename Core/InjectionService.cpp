#include "InjectionService.h"

#include "Logger.h"

namespace vmex::inject
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"inject";

        class StubInjectionService final : public IInjectionService
        {
        public:
            Status FindTarget(TargetProcess& target) override
            {
                target = {};
                log::Logger::Instance().WriteKey(log::Level::Debug, kChannel, L"log.inject.find_target");
                return Unavailable(L"FindTarget");
            }

            Status Inject(const Options& options, State& state) override
            {
                state = {};
                log::Logger::Instance().WriteKeyFormat(
                    log::Level::Info,
                    kChannel,
                    L"log.inject.payload",
                    { options.launcherModule.wstring(), options.tapModule.wstring() });
                return Unavailable(L"Inject");
            }

            Status Eject(const TargetProcess& target) override
            {
                (void)target;
                return Unavailable(L"Eject");
            }

            Status QueryState(State& state) override
            {
                state = {};
                return Unavailable(L"QueryState");
            }

        private:
            static Status Unavailable(std::wstring_view operation)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel, L"log.inject.not_implemented", { std::wstring(operation) });
                return Status::NotSupported(std::wstring(operation));
            }
        };
    }

    std::unique_ptr<IInjectionService> CreateInjectionService()
    {
        return std::make_unique<StubInjectionService>();
    }
}
