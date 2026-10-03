#include "EndpointPolicyService.h"

#include "AudioInterop.h"
#include "Logger.h"

namespace vmex::audio
{
    namespace
    {
        constexpr std::wstring_view kChannel = L"audio.policy";

        EDataFlow ToFlow(DataFlow flow)
        {
            return flow == DataFlow::Capture ? eCapture : eRender;
        }

        ERole ToRole(DeviceRole role)
        {
            switch (role)
            {
            case DeviceRole::Console: return eConsole;
            case DeviceRole::Communications: return eCommunications;
            default: return eMultimedia;
            }
        }

        class WindowsEndpointPolicyService final : public IEndpointPolicyService
        {
        public:
            Status SetDefaultDevice(std::wstring_view deviceId, DeviceRole role) override
            {
                if (deviceId.empty())
                {
                    return Status::InvalidArguments(L"device-id");
                }

                const HRESULT result = detail::SetSystemDefaultEndpointForRoleResult(std::wstring(deviceId), ToRole(role));
                if (FAILED(result))
                {
                    wchar_t hex[16] = {};
                    ::swprintf_s(hex, L"%08lX", static_cast<unsigned long>(result));
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Warn,
                        kChannel,
                        L"log.audio.policy_set_default_failed",
                        { std::wstring(deviceId), hex, std::to_wstring(static_cast<int>(role)) });
                    return Status::FromHResult(L"set-default", static_cast<std::int32_t>(result));
                }

                return Status::Ok();
            }

            Status SetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring_view deviceId) override
            {
                if (processId == 0)
                {
                    return Status::InvalidArguments(L"process-id");
                }

                if (!detail::SetPersistedEndpointForRole(ToFlow(flow), processId, ToRole(role), std::wstring(deviceId)))
                {
                    log::Logger::Instance().WriteKeyFormat(
                        log::Level::Warn,
                        kChannel,
                        L"log.audio.policy_set_redirect_failed",
                        { std::to_wstring(processId) });
                    return Status::Failed(L"set-redirect");
                }

                return Status::Ok();
            }

            Status GetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring& deviceId) override
            {
                deviceId.clear();
                if (processId == 0)
                {
                    return Status::InvalidArguments(L"process-id");
                }

                if (!detail::GetPersistedEndpointForRole(ToFlow(flow), processId, ToRole(role), deviceId))
                {
                    deviceId.clear();
                    return Status::Failed(L"get-redirect");
                }

                return Status::Ok();
            }

            Status ClearAppDefaultDevices() override
            {
                if (!detail::ClearAllAppRedirects())
                {
                    log::Logger::Instance().WriteKey(log::Level::Warn, kChannel, L"log.audio.policy_clear_redirect_failed");
                    return Status::Failed(L"clear-redirect");
                }

                return Status::Ok();
            }

            Status QuerySupport(EndpointSupport& support) override
            {
                support = {};
                support.defaultDevice = detail::CanSetSystemDefaultEndpoint();

                const auto factory = detail::MakePolicyFactory();
                support.perAppDevice = static_cast<bool>(factory);
                support.clearAppDevices = static_cast<bool>(factory);

                return Status::Ok();
            }
        };
    }

    std::unique_ptr<IEndpointPolicyService> CreateEndpointPolicyService()
    {
        return std::make_unique<WindowsEndpointPolicyService>();
    }
}
