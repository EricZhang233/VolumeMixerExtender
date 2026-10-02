#include "EndpointPolicyService.h"

#include "Logger.h"

namespace vmex::audio
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"audio.policy";

        class StubEndpointPolicyService final : public IEndpointPolicyService
        {
        public:
            Status SetDefaultDevice(std::wstring_view deviceId, DeviceRole role) override
            {
                (void)deviceId;
                (void)role;
                return Unavailable(L"SetDefaultDevice");
            }

            Status SetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring_view deviceId) override
            {
                (void)processId;
                (void)flow;
                (void)role;
                (void)deviceId;
                return Unavailable(L"SetAppDefaultDevice");
            }

            Status GetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring& deviceId) override
            {
                deviceId.clear();
                (void)processId;
                (void)flow;
                (void)role;
                return Unavailable(L"GetAppDefaultDevice");
            }

            Status ClearAppDefaultDevices() override
            {
                return Unavailable(L"ClearAppDefaultDevices");
            }

            Status QuerySupport(EndpointSupport& support) override
            {
                support = {};
                return Unavailable(L"QuerySupport");
            }

        private:
            static Status Unavailable(std::wstring_view operation)
            {
                log::Logger::Instance().WriteKeyFormat(log::Level::Warn, kChannel, L"log.audio.policy_not_implemented", { std::wstring(operation) });
                return Status::NotSupported(std::wstring(operation));
            }
        };
    }

    std::unique_ptr<IEndpointPolicyService> CreateEndpointPolicyService()
    {
        return std::make_unique<StubEndpointPolicyService>();
    }
}
