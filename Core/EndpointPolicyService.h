#pragma once

#include "AudioTypes.h"

namespace vmex::audio
{
    class IEndpointPolicyService
    {
    public:
        virtual ~IEndpointPolicyService() = default;

        virtual Status SetDefaultDevice(std::wstring_view deviceId, DeviceRole role) = 0;
        virtual Status SetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring_view deviceId) = 0;
        virtual Status GetAppDefaultDevice(std::uint32_t processId, DataFlow flow, DeviceRole role, std::wstring& deviceId) = 0;
        virtual Status ClearAppDefaultDevices() = 0;
        virtual Status QuerySupport(EndpointSupport& support) = 0;
    };

    [[nodiscard]] std::unique_ptr<IEndpointPolicyService> CreateEndpointPolicyService();
}
